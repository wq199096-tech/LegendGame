#include "Engine/Network/TcpConnection.h"

#include <algorithm>

namespace legend::net {

TcpConnection::TcpConnection(asio::ip::tcp::socket socket, std::uint64_t connectionId)
    : m_socket(std::move(socket)),
      m_strand(asio::make_strand(m_socket.get_executor())),
      m_id(connectionId) {}

std::string TcpConnection::RemoteEndpoint() const {
    std::error_code ec;
    auto ep = m_socket.remote_endpoint(ec);
    return ec ? std::string("unknown") : ep.address().to_string() + ":" +
                   std::to_string(ep.port());
}

void TcpConnection::Start(PacketHandler onPacket, CloseHandler onClose) {
    // Post 到 strand：保证回调设置与首次 ReadHeader 同序
    asio::post(m_strand, [self = shared_from_this(), onPacket = std::move(onPacket),
                          onClose = std::move(onClose)]() mutable {
        self->m_onPacket = std::move(onPacket);
        self->m_onClose = std::move(onClose);
        self->m_connected.store(true);
        self->ReadHeader();
    });
}

void TcpConnection::Send(const legend::network::Packet& packet) {
    std::vector<std::uint8_t> encoded;
    legend::network::PacketCodec::EncodePacket(packet, encoded);
    SendRaw(std::move(encoded));
}

void TcpConnection::SendRaw(std::vector<std::uint8_t> bytes) {
    asio::post(m_strand, [self = shared_from_this(), bytes = std::move(bytes)]() mutable {
        if (self->m_closed) {
            return;
        }
        self->m_writeQueue.push_back(std::move(bytes));
        if (!self->m_writing) {
            self->WriteNext(); // 指令二十五：仅当没有在写的 async_write 时启动
        }
    });
}

void TcpConnection::Close() {
    asio::post(m_strand, [self = shared_from_this()]() { self->Fail(std::error_code{}); });
}

void TcpConnection::ReadHeader() {
    // 指令二十三：async_read exactly 16 字节，绝不假设一次 read 拿全
    auto self = shared_from_this();
    asio::async_read(m_socket, asio::buffer(m_headerBuffer.data(), m_headerBuffer.size()),
                     asio::bind_executor(m_strand, [self](std::error_code ec, std::size_t) {
                         if (ec) {
                             self->Fail(ec);
                             return;
                         }
                         legend::network::PacketHeader header;
                         std::string error;
                         if (!legend::network::PacketCodec::DecodeHeader(
                                 self->m_headerBuffer.data(), self->m_headerBuffer.size(),
                                 header, error)) {
                             // 指令六十九：bad magic / oversized -> LOG 由上层打，此处断开
                             self->Fail(std::make_error_code(std::errc::protocol_error));
                             return;
                         }
                         // 指令七十三：sequence 乱序检测（只记录，正常 TCP 不会乱序）
                         if (header.sequence != 0 &&
                             header.sequence <= self->m_lastReceivedSequence) {
                             // 保留包继续处理；乱序仅 Debug 关注
                         }
                         self->m_lastReceivedSequence = header.sequence;
                         self->m_packet.header = header;
                         if (header.payloadSize == 0) {
                             self->m_packet.payload.clear();
                             self->DispatchPacket(); // 指令二十四：无 payload 直接派发
                             return;
                         }
                         self->ReadPayload();
                     }));
}

void TcpConnection::ReadPayload() {
    m_payloadBuffer.resize(m_packet.header.payloadSize);
    auto self = shared_from_this();
    asio::async_read(m_socket, asio::buffer(m_payloadBuffer.data(), m_payloadBuffer.size()),
                     asio::bind_executor(m_strand, [self](std::error_code ec, std::size_t) {
                         if (ec) {
                             self->Fail(ec);
                             return;
                         }
                         self->m_packet.payload = self->m_payloadBuffer;
                         self->DispatchPacket();
                     }));
}

void TcpConnection::DispatchPacket() {
    if (m_onPacket) {
        m_onPacket(m_packet);
    }
    ReadHeader(); // 继续下一帧
}

void TcpConnection::WriteNext() {
    if (m_writeQueue.empty()) {
        m_writing = false;
        return;
    }
    m_writing = true;
    auto& front = m_writeQueue.front();
    auto self = shared_from_this();
    asio::async_write(m_socket, asio::buffer(front.data(), front.size()),
                      asio::bind_executor(m_strand, [self](std::error_code ec, std::size_t) {
                          if (ec) {
                              self->Fail(ec);
                              return;
                          }
                          self->m_writeQueue.pop_front();
                          self->WriteNext();
                      }));
}

void TcpConnection::Fail(const std::error_code& ec) {
    if (m_closed) {
        return;
    }
    m_closed = true;
    m_connected.store(false);
    std::error_code ignored;
    m_socket.close(ignored);
    m_writeQueue.clear();
    if (m_onClose) {
        m_onClose(m_id, ec); // 指令六十八：正常断线/异常统一 ConnectionClosed
    }
}

} // namespace legend::net
