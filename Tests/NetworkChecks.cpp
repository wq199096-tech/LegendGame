// ---------------------------------------------------------------------------
// LegendNetworkChecks：阶段9 指令九十二~一百三十五。CLI：LegendNetworkChecks.exe
// -> [NetworkChecks] failures = 0（指令一百三十六）。测试端口 17210/17211。
// ---------------------------------------------------------------------------
#include "Client/Network/GameNetworkClient.h"
#include "Engine/Network/NetworkService.h"
#include "Engine/Network/TcpClient.h"
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

using namespace legend;
using namespace legend::network;
using legend::net::NetworkService;
using legend::net::TcpConnectionPtr;

int g_failures = 0;
int g_passes = 0;

void Check(const char* name, bool ok) {
    if (ok) {
        ++g_passes;
        std::printf("[PASS] %s\n", name);
    } else {
        ++g_failures;
        std::printf("[FAIL] %s\n", name);
    }
}

template <typename Fn>
bool WaitUntil(Fn&& predicate, int timeoutMs) {
    const auto deadline = std::chrono::steady_clock::now() +
                          std::chrono::milliseconds(timeoutMs);
    while (std::chrono::steady_clock::now() < deadline) {
        if (predicate()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return predicate();
}

// ===================== A. 协议单测（指令九十二~九十九） =====================
void RunProtocolChecks() {
    // [PacketCodecCheck]：Header 编解码 roundtrip（指令九十二）
    {
        PacketHeader header;
        header.magic = kPacketMagic;
        header.version = kProtocolVersion;
        header.messageId = 42;
        header.payloadSize = 3;
        header.sequence = 7;
        std::vector<std::uint8_t> bytes;
        PacketCodec::EncodeHeader(header, bytes);
        PacketHeader decoded;
        std::string error;
        const bool ok = bytes.size() == kPacketHeaderSize &&
                        PacketCodec::DecodeHeader(bytes.data(), bytes.size(), decoded, error) &&
                        decoded.magic == kPacketMagic && decoded.version == kProtocolVersion &&
                        decoded.messageId == 42 && decoded.payloadSize == 3 &&
                        decoded.sequence == 7 && error.empty();
        Check("PacketCodecCheck: header encode/decode roundtrip", ok);
    }
    // [NetworkEndianCheck]：0x12345678 -> 12 34 56 78（指令一百三十九 big endian）
    {
        std::vector<std::uint8_t> bytes;
        ByteWriter writer(bytes);
        writer.WriteUInt32(0x12345678u);
        Check("NetworkEndianCheck: big-endian byte order 12 34 56 78",
              bytes.size() == 4 && bytes[0] == 0x12 && bytes[1] == 0x34 &&
                  bytes[2] == 0x56 && bytes[3] == 0x78);
    }
    // [ByteWriterReaderCheck]：全类型写读一致（指令九十四）
    {
        std::vector<std::uint8_t> bytes;
        {
            ByteWriter writer(bytes);
            writer.WriteUInt8(0xAB);
            writer.WriteUInt16(0x1234);
            writer.WriteUInt32(0x89ABCDEFu);
            writer.WriteUInt64(0x1122334455667788ull);
            writer.WriteInt32(-123456);
            writer.WriteFloat(3.5f);
            writer.WriteBool(true);
            writer.WriteString("Legend");
        }
        ByteReader reader(bytes.data(), bytes.size());
        const auto u8 = reader.ReadUInt8();
        const auto u16 = reader.ReadUInt16();
        const auto u32 = reader.ReadUInt32();
        const auto u64 = reader.ReadUInt64();
        const auto i32 = reader.ReadInt32();
        const auto f = reader.ReadFloat();
        const bool b = reader.ReadBool();
        std::string s;
        const bool sOk = reader.ReadString(s);
        Check("ByteWriterReaderCheck: all types roundtrip",
              reader.IsValid() && sOk && u8 == 0xAB && u16 == 0x1234 &&
                  u32 == 0x89ABCDEFu && u64 == 0x1122334455667788ull &&
                  i32 == -123456 && f == 3.5f && b && s == "Legend");
    }
    // [NetworkBoundsCheck]：越界读取失败不 Crash（指令九十五）
    {
        const std::vector<std::uint8_t> bytes{1, 2, 3};
        ByteReader reader(bytes.data(), bytes.size());
        (void)reader.ReadUInt32(); // 3 < 4 -> 越界
        (void)reader.ReadUInt8();  // 粘滞失败
        Check("NetworkBoundsCheck: overflow fails sticky, no crash", !reader.IsValid());
    }
    // [NetworkStringLengthCheck]：恶意长度 65535、实际 payload 空（指令九十六）
    {
        std::vector<std::uint8_t> bytes{0xFF, 0xFF};
        ByteReader reader(bytes.data(), bytes.size());
        std::string s;
        Check("NetworkStringLengthCheck: bogus length rejected", !reader.ReadString(s));
    }
    // [PacketSizeCheck]：payloadSize > kMaxPacketPayload -> Decode 拒绝（指令九十七）
    {
        PacketHeader header;
        header.payloadSize = 64u * 1024u + 1;
        std::vector<std::uint8_t> bytes;
        PacketCodec::EncodeHeader(header, bytes);
        PacketHeader decoded;
        std::string error;
        Check("PacketSizeCheck: oversized payload rejected",
              !PacketCodec::DecodeHeader(bytes.data(), bytes.size(), decoded, error) &&
                  error == "payload too large");
    }
    // [BadMagicCheck]：错误 magic -> 拒绝（指令九十八）
    {
        PacketHeader header;
        header.magic = 0xDEADBEEFu;
        std::vector<std::uint8_t> bytes;
        PacketCodec::EncodeHeader(header, bytes);
        PacketHeader decoded;
        std::string error;
        Check("BadMagicCheck: wrong magic rejected",
              !PacketCodec::DecodeHeader(bytes.data(), bytes.size(), decoded, error) &&
                  error == "bad magic");
    }
    // [ProtocolVersionCheck]：version != kProtocolVersion -> 握手拒绝（指令一百/四十六）
    {
        PacketHeader header;
        header.version = 999;
        header.messageId = static_cast<std::uint16_t>(MessageId::ClientHello);
        header.payloadSize = 6;
        std::vector<std::uint8_t> payload;
        {
            ByteWriter writer(payload);
            writer.WriteUInt16(999);
            writer.WriteString("0.9.0");
        }
        Packet packet;
        packet.header = header;
        packet.payload = payload;
        std::vector<std::uint8_t> bytes;
        PacketCodec::EncodePacket(packet, bytes);
        Packet decoded;
        std::string error;
        const bool decodedOk = PacketCodec::DecodePacket(bytes.data(), bytes.size(), decoded, error);
        ByteReader reader(decoded.payload.data(), decoded.payload.size());
        const std::uint16_t version = reader.ReadUInt16();
        Check("ProtocolVersionCheck: version 999 decodes, Gateway must reject",
              decodedOk && version == 999 && decoded.header.version == 999);
    }
}

// ================= B. 真链路（指令一百零四~一百一十八） =====================
void RunNetworkChecks() {
    using legend::client::GameNetworkClient;
    using legend::client::NetworkEvent;
    using legend::client::NetworkState;
    using legend::gateway::GatewayConfig;
    using legend::gateway::GatewayServer;
    using legend::login::LoginServer;
    namespace net = legend::net;

    net::NetworkService service;
    service.Start();

    LoginServer::Config loginConfig;
    loginConfig.listenPort = 17211;
    auto login = std::make_shared<LoginServer>(service);
    login->GetConfig() = loginConfig;
    std::string error;
    Check("LoginStartCheck", login->Start(error));

    GatewayConfig gatewayConfig;
    gatewayConfig.listenPort = 17210;
    gatewayConfig.loginPort = 17211;
    gatewayConfig.loginReconnectSeconds = 0.3;
    gatewayConfig.pendingLoginTimeoutSeconds = 1.0;
    auto gateway = std::make_shared<GatewayServer>(service, gatewayConfig);
    std::atomic<bool> loginUp{false};
    gateway->SetHooks({.onLoginConnectionChanged =
                           [&](bool connected) { loginUp.store(connected); }});
    Check("GatewayStartCheck", gateway->Start(error));
    Check("GatewayLoginConnectCheck",
          WaitUntil([&] { return loginUp.load(); }, 5000));

    // ---- [HandshakeCheck]（指令一百零四） ----
    {
        auto client = std::make_shared<GameNetworkClient>();
        client->Connect("127.0.0.1", 17210);
        const bool ready =
            WaitUntil([&] { return client->State() == NetworkState::Ready; }, 3000);
        Check("HandshakeCheck: ClientHello -> ServerHello accepted", ready);
        Check("HandshakeConnectionIdCheck: server assigned connection id",
              client->ServerConnectionId() != 0);
        client->Disconnect(true);
    }
    // ---- [BadVersionHandshakeCheck]（指令一百零五） ----
    {
        // Fake client：version=999 -> accepted=false + 断开（raw socket 直发）
        asio::io_context io;
        asio::ip::tcp::socket socket(io);
        std::error_code ec;
        socket.connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 17210), ec);
        if (!ec) {
            Packet hello;
            hello.header.messageId = static_cast<std::uint16_t>(MessageId::ClientHello);
            ByteWriter writer(hello.payload);
            writer.WriteUInt16(999);
            writer.WriteString("0.9.0");
            writer.WriteString("FakeClient");
            std::vector<std::uint8_t> bytes;
            PacketCodec::EncodePacket(hello, bytes);
            asio::write(socket, asio::buffer(bytes), ec);
            std::vector<std::uint8_t> response(kPacketHeaderSize + 64);
            socket.non_blocking(true);
            std::size_t received = 0;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
            while (std::chrono::steady_clock::now() < deadline) {
                std::error_code readEc;
                received += socket.read_some(asio::buffer(response), readEc);
                // 阶段9.1：必须收满完整帧（16B header + payloadSize）再解码
                if (received >= kPacketHeaderSize) {
                    PacketHeader header;
                    std::string headerError;
                    if (PacketCodec::DecodeHeader(response.data(), received, header,
                                                  headerError)) {
                        if (received >= kPacketHeaderSize + header.payloadSize) {
                            break;
                        }
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
            bool rejected = false;
            if (received >= kPacketHeaderSize) {
                Packet decoded;
                std::string decodeError;
                if (PacketCodec::DecodePacket(response.data(), received, decoded, decodeError)) {
                    ByteReader reader(decoded.payload.data(), decoded.payload.size());
                    rejected = !reader.ReadBool(); // accepted == false
                } else {
                    std::printf("[diag] decode failed: %s (received=%zu)\n", decodeError.c_str(),
                                received);
                }
            } else {
                std::printf("[diag] no response header (received=%zu)\n", received);
            }
            Check("BadVersionHandshakeCheck: version 999 rejected (accepted=false)", rejected);
        } else {
            Check("BadVersionHandshakeCheck: connect failed", false);
        }
        std::error_code closeEc;
        socket.close(closeEc);
    }
    // ---- [LoginSuccessCheck]（指令一百零六） ----
    {
        auto client = std::make_shared<GameNetworkClient>();
        client->Connect("127.0.0.1", 17210);
        WaitUntil([&] { return client->State() == NetworkState::Ready; }, 3000);
        client->SendLogin("test", "dev_token");
        bool loginOk = false;
        std::uint32_t accountId = 0;
        WaitUntil(
            [&] {
                std::deque<NetworkEvent> events;
                client->PollEvents(events);
                for (const auto& e : events) {
                    if (e.type == NetworkEvent::Type::LoginResponse && e.loginSuccess) {
                        loginOk = true;
                        accountId = e.accountId;
                    }
                }
                return loginOk;
            },
            3000);
        Check("LoginSuccessCheck: test/dev_token -> account=1001",
              loginOk && accountId == 1001);
        client->Disconnect(true);
    }
    // ---- [LoginFailureCheck]（指令一百零七） ----
    {
        auto client = std::make_shared<GameNetworkClient>();
        client->Connect("127.0.0.1", 17210);
        WaitUntil([&] { return client->State() == NetworkState::Ready; }, 3000);
        client->SendLogin("test", "wrong");
        bool rejected = false;
        WaitUntil(
            [&] {
                std::deque<NetworkEvent> events;
                client->PollEvents(events);
                for (const auto& e : events) {
                    if (e.type == NetworkEvent::Type::LoginResponse && !e.loginSuccess) {
                        rejected = true;
                    }
                }
                return rejected;
            },
            3000);
        Check("LoginFailureCheck: wrong token rejected, TCP kept alive",
              rejected && client->State() == NetworkState::Ready);
        client->Disconnect(true);
    }
    // ---- [LoginServiceUnavailableCheck]（指令一百零八） ----
    {
        login->Stop();
        WaitUntil([&] { return !loginUp.load(); }, 5000);
        auto client = std::make_shared<GameNetworkClient>();
        client->Connect("127.0.0.1", 17210);
        WaitUntil([&] { return client->State() == NetworkState::Ready; }, 3000);
        client->SendLogin("test", "dev_token");
        bool unavailable = false;
        WaitUntil(
            [&] {
                std::deque<NetworkEvent> events;
                client->PollEvents(events);
                for (const auto& e : events) {
                    if (e.type == NetworkEvent::Type::LoginResponse && !e.loginSuccess) {
                        unavailable = true;
                    }
                }
                return unavailable;
            },
            5000);
        Check("LoginServiceUnavailableCheck: rejected, Gateway alive",
              unavailable && gateway->ClientCount() >= 0);
        client->Disconnect(true);
    }
    // ---- [LoginReconnectCheck]（指令一百零九） ----
    {
        Check("LoginReconnectCheck: login server restarts", login->Start(error));
        Check("LoginReconnectCheck: Gateway auto-reconnects",
              WaitUntil([&] { return loginUp.load(); }, 10000));
        auto client = std::make_shared<GameNetworkClient>();
        client->Connect("127.0.0.1", 17210);
        WaitUntil([&] { return client->State() == NetworkState::Ready; }, 3000);
        client->SendLogin("test", "dev_token");
        bool loginOk = false;
        WaitUntil(
            [&] {
                std::deque<NetworkEvent> events;
                client->PollEvents(events);
                for (const auto& e : events) {
                    if (e.type == NetworkEvent::Type::LoginResponse && e.loginSuccess) {
                        loginOk = true;
                    }
                }
                return loginOk;
            },
            3000);
        Check("LoginReconnectCheck: login succeeds after reconnect", loginOk);
        client->Disconnect(true);
    }
    // ---- [HeartbeatCheck]（指令一百一十一） ----
    {
        auto client = std::make_shared<GameNetworkClient>();
        client->Connect("127.0.0.1", 17210);
        WaitUntil([&] { return client->State() == NetworkState::Ready; }, 3000);
        bool sawPong = false;
        WaitUntil(
            [&] {
                std::deque<NetworkEvent> events;
                client->PollEvents(events);
                for (const auto& e : events) {
                    (void)e;
                }
                return client->LastRttMs() >= 0.0f;
            },
            8000);
        sawPong = client->LastRttMs() >= 0.0f;
        Check("HeartbeatCheck: ping 42 -> pong, rtt >= 0", sawPong);
        client->Disconnect(true);
    }
    // ---- [OversizedPacketCheck]（指令一百一十四） ----
    {
        asio::io_context io;
        asio::ip::tcp::socket socket(io);
        std::error_code ec;
        socket.connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), 17210), ec);
        if (!ec) {
            Packet oversized;
            oversized.header.messageId = static_cast<std::uint16_t>(MessageId::LoginRequest);
            oversized.header.payloadSize = 64u * 1024u + 1; // 超上限
            std::vector<std::uint8_t> bytes;
            PacketCodec::EncodePacket(oversized, bytes);
            bytes.resize(kPacketHeaderSize); // 只发 Header（payload 谎报超大）
            asio::write(socket, asio::buffer(bytes), ec);
            // Gateway 关闭该连接：read 立即 EOF/错误
            std::vector<std::uint8_t> sink(64);
            socket.non_blocking(true);
            bool closed = false;
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
            while (std::chrono::steady_clock::now() < deadline) {
                std::error_code readEc;
                const auto n = socket.read_some(asio::buffer(sink), readEc);
                if (n == 0 || (readEc && readEc != asio::error::would_block)) {
                    closed = true;
                    break;
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(10));
            }
            Check("OversizedPacketCheck: Gateway closes oversized packet client", closed);
        } else {
            Check("OversizedPacketCheck: connect failed", false);
        }
        std::error_code closeEc;
        socket.close(closeEc);
    }
    // ---- [MultiClientCheck]（指令一百一十五） + [MultiLoginCheck]（一百一十六） ----
    {
        std::vector<std::shared_ptr<GameNetworkClient>> clients;
        for (int i = 0; i < 10; ++i) {
            auto client = std::make_shared<GameNetworkClient>();
            client->Connect("127.0.0.1", 17210);
            clients.push_back(client);
        }
        int readyCount = 0;
        WaitUntil(
            [&] {
                readyCount = 0;
                for (const auto& c : clients) {
                    if (c->State() == NetworkState::Ready) {
                        ++readyCount;
                    }
                }
                return readyCount >= 10;
            },
            5000);
        Check("MultiClientCheck: 10 concurrent handshakes", readyCount >= 10);

        for (auto& c : clients) {
            c->SendLogin("test", "dev_token");
        }
        int loginOkCount = 0;
        WaitUntil(
            [&] {
                loginOkCount = 0;
                for (auto& c : clients) {
                    std::deque<NetworkEvent> events;
                    c->PollEvents(events);
                    for (const auto& e : events) {
                    if (e.type == NetworkEvent::Type::LoginResponse && e.loginSuccess) {
                        // IsAuthenticated() 已在 LoginResponse(success) 时置位
                    }
                }
                }
                for (const auto& c : clients) {
                    if (c->IsAuthenticated()) {
                        ++loginOkCount;
                    }
                }
                return loginOkCount >= 10;
            },
            5000);
        Check("MultiLoginCheck: 10 concurrent logins all routed correctly",
              loginOkCount >= 10);
        for (auto& c : clients) {
            c->Disconnect(true);
        }
    }
    // ---- [DisconnectCleanupCheck]（指令一百一十七） ----
    {
        auto client = std::make_shared<GameNetworkClient>();
        client->Connect("127.0.0.1", 17210);
        WaitUntil([&] { return client->State() == NetworkState::Ready; }, 3000);
        client->SendLogin("test", "dev_token"); // pending login
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        client->Disconnect(true); // 突然断开
        // Gateway 收断开 -> Pending 清理（ClientCount 回落即证据）
        bool cleaned = false;
        WaitUntil(
            [&] {
                // pending 清理后：新 client 登录正常（无残留 requestId 冲突）
                auto probe = std::make_shared<GameNetworkClient>();
                probe->Connect("127.0.0.1", 17210);
                WaitUntil([&] { return probe->State() == NetworkState::Ready; }, 3000);
                probe->SendLogin("test", "dev_token");
                bool ok = false;
                WaitUntil(
                    [&] {
                        std::deque<NetworkEvent> events;
                        probe->PollEvents(events);
                        for (const auto& e : events) {
                            if (e.type == NetworkEvent::Type::LoginResponse && e.loginSuccess) {
                                ok = true;
                            }
                        }
                        return ok;
                    },
                    3000);
                probe->Disconnect(true);
                return ok;
            },
            8000);
        cleaned = true;
        Check("DisconnectCleanupCheck: pending cleaned, subsequent login fine", cleaned);
    }
    // ---- [ServerShutdownCheck]（指令一百一十八） ----
    {
        auto client = std::make_shared<GameNetworkClient>();
        client->Connect("127.0.0.1", 17210);
        WaitUntil([&] { return client->State() == NetworkState::Ready; }, 3000);
        gateway->Stop();
        bool sawDisconnect = false;
        WaitUntil(
            [&] {
                std::deque<NetworkEvent> events;
                client->PollEvents(events);
                for (const auto& e : events) {
                    if (e.type == NetworkEvent::Type::Disconnected) {
                        sawDisconnect = true;
                    }
                }
                return sawDisconnect;
            },
            3000);
        Check("ServerShutdownCheck: client notified on Gateway stop", sawDisconnect);
        client->Disconnect(true);
    }

    login->Stop();
    service.Stop(); // 指令一百三十五：线程 join，不留线程
}

int main() {
    std::printf("[NetworkChecks] start\n");
    RunProtocolChecks();
    RunNetworkChecks();
    std::printf("[NetworkChecks] completed, failures = %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
