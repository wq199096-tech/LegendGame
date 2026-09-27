#pragma once

#include <asio.hpp>

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "Shared/Network/PacketCodec.h"

namespace legend::net {

// 阶段9 指令二十一：TcpConnection——封装 tcp socket。
// - 收包：async_read exactly Header(16B) -> decode/validate -> async_read exactly
//   payload -> dispatch（指令二十二/二十三/二十四，绝不 read_some 当整包）
// - 发包：outgoingQueue 队列化，同一时刻仅一个 async_write 在飞（指令二十五）
// - 线程模型：所有 socket 操作经 strand（指令二十六）
class TcpConnection : public std::enable_shared_from_this<TcpConnection> {
public:
    using PacketHandler = std::function<void(const legend::network::Packet&)>;
    using CloseHandler = std::function<void(std::uint64_t, const std::error_code&)>;
    // 阶段9.1指令三：内部关闭观察者（TcpServer 连接表清理用，与业务 CloseHandler 分离）
    using InternalCloseHandler = std::function<void(std::uint64_t)>;

    TcpConnection(asio::ip::tcp::socket socket, std::uint64_t connectionId);

    // 阶段9.1指令一/四：只能 Start 一次（防双 async_read 链）；重复调用 LOG_ERROR 拒绝
    void Start(PacketHandler onPacket, CloseHandler onClose);
    void SetInternalCloseHandler(InternalCloseHandler handler);
    void Send(const legend::network::Packet& packet);
    void SendRaw(std::vector<std::uint8_t> bytes); // 已编码完整帧
    void Close();
    // 阶段9.1：写队列清空后再关（ServerHello 等 pending 写先落盘——拒绝握手场景）
    void CloseAfterFlush();

    bool IsConnected() const { return m_connected.load(); }
    std::uint64_t Id() const { return m_id; }
    std::string RemoteEndpoint() const;

private:
    void ReadHeader();
    void ReadPayload();
    void DispatchPacket();
    void WriteNext();
    void Fail(const std::error_code& ec);

    asio::ip::tcp::socket m_socket;
    asio::strand<asio::any_io_executor> m_strand;
    std::uint64_t m_id = 0;
    std::atomic<bool> m_connected{false};
    bool m_closed = false; // strand 内：防重复 Close/回调

    std::array<std::uint8_t, 16> m_headerBuffer{};
    std::vector<std::uint8_t> m_payloadBuffer;
    legend::network::Packet m_packet;

    std::deque<std::vector<std::uint8_t>> m_writeQueue;
    bool m_writing = false;

    PacketHandler m_onPacket;
    CloseHandler m_onClose;
    InternalCloseHandler m_internalClose; // TcpServer 连接表清理（Fail exactly-once 内调用）
    bool m_started = false;               // 阶段9.1指令四：Start 防重入
    bool m_closeAfterFlush = false;       // 写队列清空后关闭
    std::uint32_t m_sendSequence = 0;      // 指令二十：1,2,3...
    std::uint32_t m_lastReceivedSequence = 0; // 指令七十三
};

using TcpConnectionPtr = std::shared_ptr<TcpConnection>;

} // namespace legend::net
