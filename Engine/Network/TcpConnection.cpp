#include "Engine/Network/TcpConnection.h"

#include "Engine/Debug/Logger.h"

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

void TcpConnection::SetInternalCloseHandler(InternalCloseHandler handler) {
    // TcpServer 在 create 后、Start 前设置（单线程 accept 路径，无需 strand）
    m_internalClose = std::move(handler);
}

void TcpConnection::Start(PacketHandler onPacket, CloseHandler onClose) {
    // 阶段9.1指令四：防重入——第二次 Start 会产生第二条 async_read 链（P0 根因）
    if (m_started) {
        LOG_ERROR("[TcpConnection] #" + std::to_string(m_id) +
                  " Start called twice; second Start rejected.");
        return;
    }
    m_started = true;
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

void TcpConnection::CloseAfterFlush() {
    asio::post(m_strand, [self = shared_from_this()]() {
        if (self->m_closed) {
            return;
        }
        if (!self->m_writing) {
            self->Fail(std::error_code{}); // 无挂起写：立即关
            return;
        }
        self->m_closeAfterFlush = true; // 写完最后一帧再关
    });
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
                         // 阶段9.1指令二十三：sequence 非0重复/倒退 -> Protocol Error 断开
                         if (header.sequence != 0) {
                             if (header.sequence <= self->m_lastReceivedSequence) {
                                 LOG_ERROR("[TcpConnection] #" + std::to_string(self->m_id) +
                                           " sequence violation: " +
                                           std::to_string(header.sequence) + " <= " +
                                           std::to_string(self->m_lastReceivedSequence));
                                 self->Fail(std::make_error_code(std::errc::protocol_error));
                                 return;
                             }
                             self->m_lastReceivedSequence = header.sequence;
                         }
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
    if (m_closed) {
        return; // Close 后不启动新写（挂起回调自查退出）
    }
    if (m_writeQueue.empty()) {
        m_writing = false;
        return;
    }
    m_writing = true;
    auto& front = m_writeQueue.front();
    auto self = shared_from_this();
    asio::async_write(m_socket, asio::buffer(front.data(), front.size()),
                      asio::bind_executor(m_strand, [self](std::error_code ec, std::size_t) {
                          if (self->m_closed) {
                              return; // Fail 已 close socket：front 引用仍有效，勿动队列
                          }
                          if (ec) {
                              self->Fail(ec);
                              return;
                          }
                          self->m_writeQueue.pop_front();
                          if (self->m_closeAfterFlush && self->m_writeQueue.empty()) {
                              self->Fail(std::error_code{}); // flush 完成
                              return;
                          }
                          self->WriteNext();
                      }));
}

void TcpConnection::Fail(const std::error_code& ec) {
    if (m_closed) {
        return; // 阶段9.1指令二十六：CloseHandler exactly-once
    }
    m_closed = true;
    m_connected.store(false);
    std::error_code ignored;
    m_socket.close(ignored);
    // 注意：不 clear m_writeQueue——挂起的 async_write 仍引用 front；
    // socket close 会让其以 error 完成，回调检测 m_closed 直接退出（不 pop）。
    if (m_internalClose) {
        m_internalClose(m_id); // TcpServer 连接表清理（指令三）
    }
    if (m_onClose) {
        m_onClose(m_id, ec); // 指令六十八：正常断线/异常统一 ConnectionClosed
    }
}

} // namespace legend::net
