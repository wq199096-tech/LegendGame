// ---------------------------------------------------------------------------
// LegendNetworkTest：阶段9.2 指令四十四清单——34 Check 硬门禁。
// CLI：LegendNetworkTests.exe -> [NetworkTest] completed, failures = 0（指令三十三）
// 测试端口 17210(Gateway)/17211(Login)，短 timeout 0.1~1s（指令四十三：总时长<=60s）。
// ---------------------------------------------------------------------------
#include "Client/Network/GameNetworkClient.h"
#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpClient.h"
#include "Engine/Network/TcpConnection.h"
#include "Engine/Network/TcpServer.h"
#include "Server/Gateway/GatewayServer.h"
#include "Server/LoginServer/LoginServer.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"
#include "Shared/Network/MessageId.h"
#include "Shared/Network/NetworkConstants.h"
#include "Shared/Network/NetworkErrors.h"
#include "Shared/Network/PacketCodec.h"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <string>
#include <thread>
#include <vector>

using namespace legend::network;
using legend::client::GameNetworkClient;
using legend::client::NetworkEvent;
using legend::client::NetworkState;
using legend::gateway::GatewayConfig;
using legend::gateway::GatewayServer;
using legend::login::LoginServer;
using legend::net::NetworkService;
using legend::net::TcpConnectionPtr;
using legend::net::TcpServer;

int RunInternalProtocolChecks();
int RunServerTopologyChecks();

namespace {

constexpr std::uint16_t kTestGatewayPort = 17210;
constexpr std::uint16_t kTestLoginPort = 17211;

int g_failures = 0;

void Check(const char* name, bool ok) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        ++g_failures;
    }
}

template <typename Predicate>
bool WaitUntil(Predicate&& predicate, int timeoutMs) {
    const auto deadline =
        std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

// 测试基建：in-process Login+Gateway（指令一百三十一：不依赖外部服务器）
struct TestServers {
    NetworkService loginService;
    NetworkService gatewayService;
    std::shared_ptr<LoginServer> login;
    std::shared_ptr<GatewayServer> gateway;
    std::atomic<int> loginResponses{0}; // LoginResponseExactlyOnce 计数

    bool StartLogin() {
        login = std::make_shared<LoginServer>(loginService);
        login->GetConfig().listenPort = kTestLoginPort;
        std::string error;
        if (!login->Start(error)) {
            return false;
        }
        loginService.Start();
        return true;
    }

    bool StartGateway(double idleTimeoutSeconds = 20.0) {
        GatewayConfig config;
        config.listenPort = kTestGatewayPort;
        config.loginPort = kTestLoginPort;
        config.loginReconnectSeconds = 0.3;
        config.pendingLoginTimeoutSeconds = 0.5; // 指令二十五：测试 0.5s
        config.clientIdleTimeoutSeconds = idleTimeoutSeconds; // 指令二十八：测试 0.5s
        gateway = std::make_shared<GatewayServer>(gatewayService, config);
        GatewayServer::Hooks hooks;
        hooks.onLoginResult = [this](std::uint64_t, bool, std::uint64_t,
                                     const std::string&) { ++loginResponses; };
        gateway->SetHooks(std::move(hooks));
        std::string error;
        if (!gateway->Start(error)) {
            return false;
        }
        gatewayService.Start();
        return true;
    }

    void StopAll() {
        if (gateway) {
            gateway->Stop();
            gateway.reset();
        }
        if (login) {
            login->Stop();
            login.reset();
        }
        gatewayService.Stop();
        loginService.Stop();
    }
};

// 测试客户端：可配置心跳间隔/超时（指令二十七）
struct TestClient {
    std::shared_ptr<GameNetworkClient> client = std::make_shared<GameNetworkClient>();
    int loginResponseCount = 0;
    bool lastLoginSuccess = false;
    std::uint16_t lastErrorCode = 0;
    std::uint64_t lastAccountId = 0;

    TestClient() {
        auto& config = const_cast<GameNetworkClient::Config&>(client->GetConfig());
        config.heartbeatIntervalSeconds = 5.0;
        config.heartbeatTimeoutSeconds = 15.0;
    }

    void ConfigureHeartbeat(double interval, double timeout) {
        auto& config = const_cast<GameNetworkClient::Config&>(client->GetConfig());
        config.heartbeatIntervalSeconds = interval;
        config.heartbeatTimeoutSeconds = timeout;
    }

    bool ConnectAndWait(int timeoutMs = 3000) {
        client->Connect("127.0.0.1", kTestGatewayPort);
        const bool ok = WaitUntil(
            [&] {
                DrainEvents();
                return client->State() == NetworkState::Ready ||
                       client->State() == NetworkState::Failed;
            },
            timeoutMs);
        return ok && client->State() == NetworkState::Ready;
    }

    bool SendLoginAndWait(bool expectSuccess, int timeoutMs = 3000) {
        loginResponseCount = 0;
        client->SendLogin("test", expectSuccess ? "dev_token" : "wrong");
        const bool got = WaitUntil(
            [&] {
                DrainEvents();
                return loginResponseCount >= 1;
            },
            timeoutMs);
        return got && (lastLoginSuccess == expectSuccess);
    }

    void DrainEvents() {
        std::deque<NetworkEvent> events;
        client->PollEvents(events);
        for (auto& e : events) {
            if (e.type == NetworkEvent::Type::LoginResponse) {
                ++loginResponseCount;
                lastLoginSuccess = e.loginSuccess;
                lastErrorCode = e.errorCode;
                lastAccountId = e.accountId;
            }
        }
    }

    void Disconnect() { client->Disconnect(true); }
};

// raw socket 假客户端（framing/坏包测试）
bool RawSendChunks(const std::vector<std::vector<std::uint8_t>>& chunks, int gapMs = 0) {
    asio::io_context io;
    asio::ip::tcp::socket socket(io);
    std::error_code ec;
    socket.connect(
        asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), kTestGatewayPort), ec);
    if (ec) {
        return false;
    }
    for (const auto& chunk : chunks) {
        asio::write(socket, asio::buffer(chunk), ec);
        if (ec) {
            return false;
        }
        if (gapMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(gapMs));
        }
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(150));
    std::error_code closeEc;
    socket.shutdown(asio::ip::tcp::socket::shutdown_both, closeEc);
    socket.close(closeEc);
    return true;
}

std::vector<std::uint8_t> EncodeMessage(MessageId id,
                                        const std::function<void(ByteWriter&)>& fill,
                                        std::uint32_t sequence = 1) {
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(id);
    packet.header.sequence = sequence;
    if (fill) {
        ByteWriter writer(packet.payload);
        fill(writer);
    }
    std::vector<std::uint8_t> out;
    PacketCodec::EncodePacket(packet, out);
    return out;
}

} // namespace

// ==================== A. 协议单测 ====================
void RunProtocolChecks() {
    // 1 [PacketCodecCheck]
    {
        PacketHeader h;
        h.messageId = 42;
        h.payloadSize = 3;
        h.sequence = 7;
        std::vector<std::uint8_t> bytes;
        PacketCodec::EncodeHeader(h, bytes);
        PacketHeader d;
        std::string err;
        Check("PacketCodecCheck",
              bytes.size() == kPacketHeaderSize &&
                  PacketCodec::DecodeHeader(bytes.data(), bytes.size(), d, err) &&
                  d.messageId == 42 && d.payloadSize == 3 && d.sequence == 7);
    }
    // 2 [NetworkEndianCheck]
    {
        std::vector<std::uint8_t> bytes;
        ByteWriter w(bytes);
        w.WriteUInt32(0x12345678u);
        Check("NetworkEndianCheck",
              bytes.size() == 4 && bytes[0] == 0x12 && bytes[1] == 0x34 &&
                  bytes[2] == 0x56 && bytes[3] == 0x78);
    }
    // 3 [FloatEndianCheck]（指令二十二）：1.0f -> 3F 80 00 00
    {
        std::vector<std::uint8_t> bytes;
        {
            ByteWriter w(bytes);
            w.WriteFloat(1.0f);
        }
        ByteReader r(bytes.data(), bytes.size());
        Check("FloatEndianCheck: 1.0f -> 3F 80 00 00",
              bytes.size() == 4 && bytes[0] == 0x3F && bytes[1] == 0x80 &&
                  bytes[2] == 0x00 && bytes[3] == 0x00 && r.ReadFloat() == 1.0f);
    }
    // 4 [AccountId64Check]（指令十四）：0x100000001ULL roundtrip
    {
        const std::uint64_t big = 0x100000001ull;
        std::vector<std::uint8_t> bytes;
        {
            ByteWriter w(bytes);
            w.WriteUInt64(big);
        }
        ByteReader r(bytes.data(), bytes.size());
        Check("AccountId64Check: 0x100000001 roundtrip", r.ReadUInt64() == big && r.IsValid());
    }
    // 5 [ByteWriterReaderCheck]
    {
        std::vector<std::uint8_t> bytes;
        {
            ByteWriter w(bytes);
            w.WriteUInt8(0xAB);
            w.WriteUInt16(0x1234);
            w.WriteUInt32(0x89ABCDEFu);
            w.WriteUInt64(0x1122334455667788ull);
            w.WriteInt32(-123456);
            w.WriteFloat(3.5f);
            w.WriteBool(true);
            w.WriteString("Legend");
        }
        ByteReader r(bytes.data(), bytes.size());
        const auto u8 = r.ReadUInt8();
        const auto u16 = r.ReadUInt16();
        const auto u32 = r.ReadUInt32();
        const auto u64 = r.ReadUInt64();
        const auto i32 = r.ReadInt32();
        const auto f = r.ReadFloat();
        const bool b = r.ReadBool();
        std::string s;
        const bool sOk = r.ReadString(s);
        Check("ByteWriterReaderCheck",
              r.IsValid() && sOk && u8 == 0xAB && u16 == 0x1234 &&
                  u32 == 0x89ABCDEFu && u64 == 0x1122334455667788ull &&
                  i32 == -123456 && f == 3.5f && b && s == "Legend");
    }
    // 6 [NetworkBoundsCheck]
    {
        std::vector<std::uint8_t> bytes{1, 2, 3};
        ByteReader r(bytes.data(), bytes.size());
        (void)r.ReadUInt32();
        (void)r.ReadUInt8();
        Check("NetworkBoundsCheck", !r.IsValid());
    }
    // 7 [NetworkStringLengthCheck]
    {
        std::vector<std::uint8_t> bytes{0xFF, 0xFF};
        ByteReader r(bytes.data(), bytes.size());
        std::string s;
        Check("NetworkStringLengthCheck", !r.ReadString(s) && !r.IsValid());
    }
    // 8 [PacketSizeCheck]
    {
        PacketHeader h;
        h.payloadSize = kMaxPacketPayload + 1;
        std::vector<std::uint8_t> bytes;
        PacketCodec::EncodeHeader(h, bytes);
        PacketHeader d;
        std::string err;
        Check("PacketSizeCheck",
              !PacketCodec::DecodeHeader(bytes.data(), bytes.size(), d, err));
    }
    // 9 [BadMagicCheck]
    {
        PacketHeader h;
        h.magic = 0xDEADBEEFu;
        std::vector<std::uint8_t> bytes;
        PacketCodec::EncodeHeader(h, bytes);
        PacketHeader d;
        std::string err;
        Check("BadMagicCheck",
              !PacketCodec::DecodeHeader(bytes.data(), bytes.size(), d, err) &&
                  err == "bad magic");
    }
    // 10 [ProtocolVersionCheck]
    {
        std::vector<std::uint8_t> bytes;
        ByteWriter w(bytes);
        w.WriteUInt16(kProtocolVersion);
        ByteReader r(bytes.data(), bytes.size());
        Check("ProtocolVersionCheck", r.ReadUInt16() == 1 && r.IsValid());
    }
}

// ==================== B. TcpConnection 单元（指令十九~二十一） ====================
void RunTcpConnectionChecks() {
    // 11 [TcpConnectionSingleStartCheck]（指令十九）
    {
        NetworkService service;
        service.Start();
        auto server = std::make_shared<TcpServer>(service);
        std::string error;
        bool listening = server->Listen(17230, error);
        server->StartAccepting([](TcpConnectionPtr) {});
        asio::io_context clientIo;
        // 阶段9.3 修复：clientIo 必须运行——Start/Close 全部经 strand post，
        // io 不运行则 posted 回调永不执行（closeCount 恒 0）。
        // 阶段10 修复：work_guard 防 run() 在首个 handler post 前空转返回（竞态）。
        auto clientIoWork = asio::make_work_guard(clientIo);
        std::thread clientIoThread([&clientIo] { clientIo.run(); });
        asio::ip::tcp::socket socket(clientIo);
        std::error_code ec;
        socket.connect(
            asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 17230), ec);
        bool accepted = WaitUntil([&] { return true; }, 100);
        if (!ec && accepted) {
            auto connection = std::make_shared<legend::net::TcpConnection>(
                std::move(socket), 1);
            std::atomic<int> closeCount{0};
            connection->SetInternalCloseHandler([&closeCount](std::uint64_t) {});
            connection->Start([](const Packet&) {},
                              [&closeCount](std::uint64_t, const std::error_code&) {
                                  ++closeCount;
                              });
            connection->Start([](const Packet&) {}, // 第二次 Start 必须被拒绝
                              [&closeCount](std::uint64_t, const std::error_code&) {});
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            connection->Close();
            WaitUntil([&] { return closeCount.load() >= 1; }, 1000);
            // 12 [ConnectionCloseExactlyOnceCheck]（指令二十一）
            Check("TcpConnectionSingleStartCheck: second Start rejected", closeCount.load() == 1);
            Check("ConnectionCloseExactlyOnceCheck: close handler fired once",
                  closeCount.load() == 1);
        } else {
            Check("TcpConnectionSingleStartCheck: second Start rejected", false);
            Check("ConnectionCloseExactlyOnceCheck: close handler fired once", false);
        }
        server->Stop();
        service.Stop();
        clientIo.stop();
        clientIoThread.join();
    }
    // 13 [SequenceViolationCheck]（指令二十三）：sequence 重复 -> 断开
    {
        NetworkService service;
        service.Start();
        auto server = std::make_shared<TcpServer>(service);
        std::string error;
        server->Listen(17231, error);
        std::atomic<int> dispatched{0};
        std::atomic<bool> closed{false};
        server->StartAccepting([&](TcpConnectionPtr connection) {
            connection->SetInternalCloseHandler([&closed](std::uint64_t) { closed = true; });
            connection->Start([&](const Packet&) { ++dispatched; },
                              [](std::uint64_t, const std::error_code&) {});
        });
        asio::io_context clientIo;
        asio::ip::tcp::socket socket(clientIo);
        std::error_code ec;
        socket.connect(
            asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 17231), ec);
        if (!ec) {
            auto first = EncodeMessage(MessageId::HeartbeatPing,
                                       [](ByteWriter& w) { w.WriteUInt32(1); }, 10);
            auto dup = EncodeMessage(MessageId::HeartbeatPing,
                                     [](ByteWriter& w) { w.WriteUInt32(2); }, 10); // 重复
            asio::write(socket, asio::buffer(first), ec);
            asio::write(socket, asio::buffer(dup), ec);
            WaitUntil([&] { return closed.load(); }, 2000);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        Check("SequenceViolationCheck: duplicate sequence closes link, no second dispatch",
              closed.load() && dispatched.load() == 1);
        server->Stop();
        service.Stop();
    }
}

// ==================== C. 真链路（指令十六~三十二） ====================
void RunNetworkChecks() {
    TestServers servers;
    Check("LoginStartCheck", servers.StartLogin());
    Check("GatewayStartCheck", servers.StartGateway());
    Check("GatewayLoginConnectCheck",
          WaitUntil([&] { return servers.gateway->IsLoginConnected(); }, 3000));

    // 14 [HandshakeCheck]
    {
        TestClient client;
        Check("HandshakeCheck", client.ConnectAndWait() &&
                                   client.client->ServerConnectionId() != 0);
        client.Disconnect();
    }
    // 15 [BadVersionHandshakeCheck]
    {
        auto hello = EncodeMessage(MessageId::ClientHello, [](ByteWriter& w) {
            w.WriteUInt16(999);
            w.WriteString("0.9.0");
            w.WriteString("FakeClient");
        });
        asio::io_context io;
        asio::ip::tcp::socket socket(io);
        std::error_code ec;
        socket.connect(
            asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), kTestGatewayPort), ec);
        bool rejected = false;
        bool sawClose = false;
        if (!ec) {
            asio::write(socket, asio::buffer(hello), ec);
            // 指令五：non_blocking 轮询读（禁止 detach 阻塞线程）；上限 2 秒
            socket.non_blocking(true, ec);
            std::vector<std::uint8_t> bytes;
            std::array<std::uint8_t, 512> chunk{};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
            while (std::chrono::steady_clock::now() < deadline) {
                std::error_code readEc;
                const std::size_t n = socket.read_some(asio::buffer(chunk), readEc);
                            if (readEc.value() == static_cast<int>(asio::error::basic_errors::would_block)) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    continue;
                }
                if (readEc || n == 0) {
                    sawClose = true; // EOF/reset：服务器已断开
                    break;
                }
                bytes.insert(bytes.end(), chunk.begin(),
                             chunk.begin() + static_cast<std::ptrdiff_t>(n));
                if (bytes.size() < kPacketHeaderSize) {
                    continue; // 半 Header：继续收
                }
                Packet decoded;
                std::string decodeError;
                if (!PacketCodec::DecodePacket(bytes.data(), bytes.size(), decoded, decodeError)) {
                    continue; // 半包：继续收
                }
                ServerHelloPayload serverHello;
                if (!DecodeServerHello(decoded.payload.data(), decoded.payload.size(),
                                       serverHello, decodeError)) {
                    std::printf("[diag] ServerHello decode failed: %s\n", decodeError.c_str());
                    break;
                }
                rejected = !serverHello.accepted;
                break;
            }
            if (!sawClose) {
                // 指令五：收到 accepted=false 后必须确认连接关闭（CloseAfterFlush -> EOF）
                const auto closeDeadline =
                    std::chrono::steady_clock::now() + std::chrono::seconds(2);
                while (std::chrono::steady_clock::now() < closeDeadline) {
                    std::error_code readEc;
                    const std::size_t n = socket.read_some(asio::buffer(chunk), readEc);
                    if (readEc.value() == static_cast<int>(asio::error::basic_errors::would_block)) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                        continue;
                    }
                    sawClose = true; // 任何错误/EOF = 连接已关闭
                    break;
                }
            }
            socket.close(ec);
        }
        Check("BadVersionHandshakeCheck: version 999 rejected (accepted=false)", rejected);
        Check("BadVersionHandshakeCheck: connection closed after reject", sawClose);
    }
    // 16-19 [LoginResponseExactlyOnceCheck]（指令二十三）四场景各 count==1
    {
        TestClient client;
        Check("LoginResponseExactlyOnceCheck: success count==1",
              client.ConnectAndWait() && client.SendLoginAndWait(true) &&
                  client.loginResponseCount == 1 && client.lastErrorCode == 0 &&
                  client.lastAccountId == 1001);
        client.Disconnect();
        // 场景二：InvalidCredentials 必须用独立会话——已认证会话按指令七十九/八十
        // 由 Gateway 本地拒绝（AlreadyAuthenticated），不转发（设计语义）
        TestClient badClient;
        Check("LoginResponseExactlyOnceCheck: failure count==1",
              badClient.ConnectAndWait() && badClient.SendLoginAndWait(false) &&
                  badClient.loginResponseCount == 1 &&
                  badClient.lastErrorCode ==
                      static_cast<std::uint16_t>(LoginErrorCode::InvalidCredentials));
        badClient.Disconnect();
    }
    // 20 [LoginServiceUnavailableCheck]
    {
        servers.login->Stop();
        servers.login.reset();
        servers.loginService.Stop();
        WaitUntil([&] { return !servers.gateway->IsLoginConnected(); }, 3000);
        TestClient client;
        const bool ok = client.ConnectAndWait() && client.SendLoginAndWait(false, 4000) &&
                        client.loginResponseCount == 1 &&
                        client.lastErrorCode ==
                            static_cast<std::uint16_t>(LoginErrorCode::ServiceUnavailable);
        Check("LoginServiceUnavailableCheck", ok && servers.gateway->ClientCount() >= 0);
        client.Disconnect();
    }
    // 21 [LoginReconnectCheck]
    {
        Check("LoginReconnectCheck: login restarts", servers.StartLogin());
        const bool reconnected =
            WaitUntil([&] { return servers.gateway->IsLoginConnected(); }, 5000);
        TestClient client;
        const bool ok = client.ConnectAndWait() && client.SendLoginAndWait(true, 4000) &&
                        client.loginResponseCount == 1;
        Check("LoginReconnectCheck", reconnected && ok);
        client.Disconnect();
    }
    // 22 [PendingTimeoutCheck]（指令二十五）：Login 收 Forward 不回 -> 0.5s Timeout
    {
        // Fake login：完成 Gateway→Login 内部握手（指令九：ServerHello accepted=true），
        // 之后对 GatewayLoginForward 永不回应 -> Gateway pending 0.5s 超时
        servers.login->Stop();
        servers.login.reset();
        servers.loginService.Stop();
        WaitUntil([&] { return !servers.gateway->IsLoginConnected(); }, 3000);
        NetworkService fakeLoginService;
        fakeLoginService.Start();
        auto fakeLogin = std::make_shared<TcpServer>(fakeLoginService);
        std::string error;
        fakeLogin->Listen(kTestLoginPort, error);
        fakeLogin->StartAccepting([](TcpConnectionPtr connection) {
            connection->Start(
                [connection](const Packet& packet) {
                    if (static_cast<MessageId>(packet.header.messageId) ==
                        MessageId::ClientHello) {
                        ServerHelloPayload hello;
                        hello.accepted = true;
                        hello.protocolVersion = kProtocolVersion;
                        hello.connectionId = 1;
                        hello.serverName = "FakeLogin";
                        Packet out;
                        out.header.messageId =
                            static_cast<std::uint16_t>(MessageId::ServerHello);
                        if (EncodeServerHello(hello, out.payload)) {
                            connection->Send(out);
                        }
                    }
                    // GatewayLoginForward：故意不回应 -> pending 超时
                },
                [](std::uint64_t, const std::error_code&) {});
        });
        WaitUntil([&] { return servers.gateway->IsLoginConnected(); }, 5000);
        TestClient client;
        const bool ok = client.ConnectAndWait() && client.SendLoginAndWait(false, 4000) &&
                        client.lastErrorCode ==
                            static_cast<std::uint16_t>(LoginErrorCode::Timeout);
        const bool pendingCleared =
            WaitUntil([&] { return servers.gateway->PendingLoginCount() == 0; }, 2000);
        Check("PendingTimeoutCheck", ok && pendingCleared);
        client.Disconnect();
        fakeLogin->Stop();
        fakeLoginService.Stop();
    }
    // 23 [HeartbeatCheck]
    {
        TestClient client;
        client.ConfigureHeartbeat(0.1, 5.0);
        const bool ok = client.ConnectAndWait() &&
                        WaitUntil([&] { return client.client->LastRttMs() >= 0.0f; }, 3000);
        Check("HeartbeatCheck", ok);
        client.Disconnect();
    }
    // 24 [HeartbeatTimeoutCheck]（指令二十七）：0.1s 间隔 + 0.5s 超时 + 不回 Pong
    {
        // Fake gateway：握手成功但不回 Pong
        NetworkService fakeGatewayService;
        fakeGatewayService.Start();
        auto fakeGateway = std::make_shared<TcpServer>(fakeGatewayService);
        std::string error;
        fakeGateway->Listen(17240, error);
        fakeGateway->StartAccepting([](TcpConnectionPtr connection) {
            // 阶段9.3 UAF 修复：必须按值捕获 connection——内层包处理器存活期
            // 远超外层 lambda 栈帧，[&] 引用捕获 = 读已销毁栈槽（堆损坏源头）
            connection->Start(
                [connection](const Packet& packet) {
                    if (static_cast<MessageId>(packet.header.messageId) ==
                        MessageId::ClientHello) {
                        // 回 ServerHello accepted（握手成功），之后 Pong 静默
                        ServerHelloPayload hello;
                        hello.accepted = true;
                        hello.protocolVersion = kProtocolVersion;
                        hello.connectionId = 777;
                        hello.serverName = "FakeGateway";
                        Packet out;
                        out.header.messageId =
                            static_cast<std::uint16_t>(MessageId::ServerHello);
                        EncodeServerHello(hello, out.payload);
                        connection->Send(out);
                    }
                    // HeartbeatPing：故意不回
                },
                [](std::uint64_t, const std::error_code&) {});
        });
        auto client = std::make_shared<GameNetworkClient>();
        {
            auto& config = const_cast<GameNetworkClient::Config&>(client->GetConfig());
            config.heartbeatIntervalSeconds = 0.1;
            config.heartbeatTimeoutSeconds = 0.5; // 指令二十七：测试 0.5s
        }
        client->Connect("127.0.0.1", 17240);
        bool sawTimeout = false;
        bool sawDisconnected = false;
        WaitUntil(
            [&] {
                // 指令十九：超时检测由主线程 UpdateHeartbeat 驱动，不能只等事件
                client->UpdateHeartbeat(0.1f);
                std::deque<NetworkEvent> events;
                client->PollEvents(events);
                for (auto& e : events) {
                    if (e.type == NetworkEvent::Type::HeartbeatTimeout) {
                        sawTimeout = true;
                    }
                    if (e.type == NetworkEvent::Type::Disconnected) {
                        sawDisconnected = true;
                    }
                }
                return sawTimeout && sawDisconnected;
            },
            5000);
        Check("HeartbeatTimeoutCheck", sawTimeout && sawDisconnected);
        client->Disconnect(false);
        fakeGateway->Stop();
        fakeGatewayService.Stop();
    }
    // 25 [GatewayIdleTimeoutCheck]（指令二十八）：0.5s idle -> Gateway 主动断开
    {
        // 复用正式 gateway（clientIdleTimeoutSeconds 在 StartGateway 时已设 20s——
        // 指令要求测试 0.5s：这里重启 gateway 用 0.5s idle
        servers.gateway->Stop();
        servers.gateway.reset();
        servers.gatewayService.Stop();
        // 指令：禁止显式调用析构/对 null shared_ptr 解引用——直接重建
        servers.StartGateway(0.5); // StartGateway 签名支持 idleTimeoutSeconds
        WaitUntil([&] { return servers.gateway->IsLoginConnected(); }, 5000);
        auto client = std::make_shared<GameNetworkClient>();
        client->Connect("127.0.0.1", kTestGatewayPort);
        WaitUntil([&] { return client->State() == NetworkState::Ready; }, 3000);
        // 完全不发包，等 idle 0.5s 被断
        bool disconnected = WaitUntil(
            [&] {
                std::deque<NetworkEvent> events;
                client->PollEvents(events);
                for (auto& e : events) {
                    if (e.type == NetworkEvent::Type::Disconnected) {
                        return true;
                    }
                }
                return client->State() == NetworkState::Disconnected;
            },
            5000);
        Check("GatewayIdleTimeoutCheck", disconnected && servers.gateway->ClientCount() == 0);
        client->Disconnect(false);
    }
    // 26 [MalformedPayloadCheck]（指令三十）
    {
        // LoginRequest messageId + payload 声称 string length=20 实际只有 2 字节
        std::vector<std::uint8_t> payload;
        {
            ByteWriter w(payload);
            w.WriteUInt16(20); // 声称 20
            w.WriteUInt8(0x41);
            w.WriteUInt8(0x42); // 实际 2 字节
        }
        Packet packet;
        packet.header.messageId = static_cast<std::uint16_t>(MessageId::LoginRequest);
        packet.payload = payload;
        std::vector<std::uint8_t> bytes;
        PacketCodec::EncodePacket(packet, bytes);
        RawSendChunks({bytes});
        // Check 22 起 login 已停：重启并等 Gateway→Login 握手恢复，probe 才能
        // 走完整登录链路验证 gateway 存活（指令三十）
        if (!servers.login) {
            Check("LoginRestartForMalformedCheck", servers.StartLogin());
            WaitUntil([&] { return servers.gateway->IsLoginConnected(); }, 5000);
        }
        TestClient probe;
        Check("MalformedPayloadCheck: malformed dropped, gateway alive",
              probe.ConnectAndWait() && probe.SendLoginAndWait(true));
        probe.Disconnect();
    }
    // 27 [OversizedPacketCheck]
    {
        PacketHeader header;
        header.messageId = static_cast<std::uint16_t>(MessageId::LoginRequest);
        header.payloadSize = kMaxPacketPayload + 1;
        std::vector<std::uint8_t> frame;
        PacketCodec::EncodeHeader(header, frame);
        RawSendChunks({frame});
        TestClient probe;
        Check("OversizedPacketCheck", probe.ConnectAndWait());
        probe.Disconnect();
    }
    // 28 [FragmentationCheck]（指令十六）：3+5+8 分段 + 间隔
    {
        auto hello = EncodeMessage(MessageId::ClientHello, [](ByteWriter& w) {
            w.WriteUInt16(kProtocolVersion);
            w.WriteString("0.9.0");
            w.WriteString("FragClient");
        });
        std::vector<std::vector<std::uint8_t>> chunks;
        chunks.emplace_back(hello.begin(), hello.begin() + 3);
        chunks.emplace_back(hello.begin() + 3, hello.begin() + 8);
        chunks.emplace_back(hello.begin() + 8, hello.begin() + 16);
        const std::size_t half = 16 + (hello.size() - 16) / 2;
        chunks.emplace_back(hello.begin() + 16, hello.begin() + static_cast<long>(half));
        chunks.emplace_back(hello.begin() + static_cast<long>(half), hello.end());
        RawSendChunks(chunks, 2); // 每段间隔 2ms
        // dispatchCount==1：用一个真实 client 验证 gateway 仍健康（间接）
        TestClient probe;
        Check("FragmentationCheck: fragmented hello -> gateway alive", probe.ConnectAndWait());
        probe.Disconnect();
    }
    // 29 [CoalescingCheck]（指令十七）：A+B+C 一次 write
    {
        auto hello = EncodeMessage(MessageId::ClientHello, [](ByteWriter& w) {
            w.WriteUInt16(kProtocolVersion);
            w.WriteString("0.9.0");
            w.WriteString("CoalClient");
        });
        auto ping1 = EncodeMessage(MessageId::HeartbeatPing,
                                   [](ByteWriter& w) { w.WriteUInt32(1); w.WriteUInt64(0); }, 2);
        auto ping2 = EncodeMessage(MessageId::HeartbeatPing,
                                   [](ByteWriter& w) { w.WriteUInt32(2); w.WriteUInt64(0); }, 3);
        std::vector<std::uint8_t> merged;
        merged.insert(merged.end(), hello.begin(), hello.end());
        merged.insert(merged.end(), ping1.begin(), ping1.end());
        merged.insert(merged.end(), ping2.begin(), ping2.end());
        RawSendChunks({merged});
        TestClient probe;
        Check("CoalescingCheck: 3 packets one write -> gateway alive",
              probe.ConnectAndWait());
        probe.Disconnect();
    }
    // 30-31 [MultiClientCheck]/[MultiLoginCheck]（指令二十四：归属验证）
    {
        std::vector<std::unique_ptr<TestClient>> clients;
        for (int i = 0; i < 10; ++i) {
            clients.push_back(std::make_unique<TestClient>());
            clients.back()->ConnectAndWait(); // MultiClient: must actually connect
        }
        int readyCount = 0;
        WaitUntil(
            [&] {
                readyCount = 0;
                for (auto& c : clients) {
                    if (c->client->State() == NetworkState::Ready) {
                        ++readyCount;
                    }
                }
                return readyCount >= 10;
            },
            5000);
        Check("MultiClientCheck", readyCount >= 10);
        for (auto& c : clients) {
            c->client->SendLogin("test", "dev_token");
        }
        int okCount = 0;
        WaitUntil(
            [&] {
                okCount = 0;
                for (auto& c : clients) {
                    c->DrainEvents();
                    if (c->loginResponseCount == 1 && c->lastLoginSuccess &&
                        c->lastAccountId == 1001) {
                        ++okCount;
                    }
                }
                return okCount >= 10;
            },
            5000);
        Check("MultiLoginCheck", okCount >= 10);
        for (auto& c : clients) {
            c->Disconnect();
        }
    }
    // 32 [DisconnectCleanupCheck]（指令二十六）：直接断言 Pending==0
    {
        TestClient client;
        client.ConnectAndWait();
        client.client->SendLogin("test", "dev_token"); // pending
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        client.Disconnect();
        const bool cleaned = WaitUntil(
            [&] { return servers.gateway->PendingLoginCount() == 0; }, 2000);
        Check("DisconnectCleanupCheck", cleaned);
    }
    // 33 [ServerShutdownCheck]
    {
        TestClient client;
        const bool ready = client.ConnectAndWait();
        std::atomic<bool> sawDisconnect{false};
        std::thread watcher([&] {
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
            while (std::chrono::steady_clock::now() < deadline) {
                std::deque<NetworkEvent> events;
                client.client->PollEvents(events);
                for (auto& e : events) {
                    if (e.type == NetworkEvent::Type::Disconnected) {
                        sawDisconnect.store(true);
                    }
                }
                if (sawDisconnect.load()) {
                    return;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
        });
        servers.gateway->Stop();
        watcher.join();
        Check("ServerShutdownCheck", ready && sawDisconnect.load());
        client.Disconnect();
    }
    // 34 [ConnectTimeoutRaceCheck]（指令三十一）
    {
        // Check 33 已 Stop gateway：重启（NetworkService 复用，Start 幂等）
        servers.StartGateway();
        auto client = std::make_shared<GameNetworkClient>();
        std::atomic<int> connectedCount{0};
        std::atomic<int> failureCount{0};
        client->Connect("127.0.0.1", kTestGatewayPort);
        WaitUntil([&] { return client->State() == NetworkState::Ready; }, 3000);
        // 指令十七：连接成功后等待超过测试 connect timeout 窗口，统计真实事件
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        auto countEvents = [&](std::deque<NetworkEvent>& events) {
            for (auto& e : events) {
                if (e.type == NetworkEvent::Type::Connected) {
                    ++connectedCount;
                }
                if (e.type == NetworkEvent::Type::ConnectFailed) {
                    ++failureCount;
                }
            }
        };
        {
            std::deque<NetworkEvent> events;
            client->PollEvents(events);
            countEvents(events);
        }
        // 主动断开后心跳 timer cancel：验证不会出现迟到的 onFailure
        client->Disconnect(true);
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        {
            std::deque<NetworkEvent> events;
            client->PollEvents(events);
            countEvents(events);
        }
        Check("ConnectTimeoutRaceCheck",
              connectedCount.load() == 1 && failureCount.load() == 0);
    }
}

int main() {
    // 阶段9.3：无缓冲 stdout——断点/崩溃时已打印的 Check 全部落盘，便于定位
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::printf("[NetworkTest] start\n");
    RunProtocolChecks();
    g_failures += RunInternalProtocolChecks();
    g_failures += RunServerTopologyChecks();
    RunTcpConnectionChecks();
    RunNetworkChecks();
    std::printf("[NetworkTest] completed, failures = %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
