#pragma once

// ---------------------------------------------------------------------------
// 阶段12 指令七十四：World 测试基建共享头。
// WorldChecks.cpp（阶段11）与 WorldAoiChecks.cpp（阶段12 AOI）共用，
// 仍链接 LegendWorldTests 单一测试 exe（不新增第四个测试程序）。
// 端口 17240(World)/17241(Login)/17242(Gateway)；临时 DB 不污染正式库。
// ---------------------------------------------------------------------------
#include "Client/Account/AccountClientController.h"
#include "Client/Network/GameNetworkClient.h"
#include "Client/WorldNetwork/WorldClientController.h"
#include "Client/WorldNetwork/WorldNetworkClient.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Network/NetworkService.h"
#include "Server/Gateway/GatewayServer.h"
#include "Server/LoginServer/Account/AccountRepository.h"
#include "Server/LoginServer/Account/AccountService.h"
#include "Server/LoginServer/Account/CharacterService.h"
#include "Server/LoginServer/Account/Database/DatabaseSchema.h"
#include "Server/LoginServer/Account/PasswordHasher.h"
#include "Server/LoginServer/LoginServer.h"
#include "Server/WorldServer/WorldServer.h"
#include "Shared/Account/AccountProtocol.h"
#include "Shared/Network/MessageId.h"
#include "Shared/Network/PacketCodec.h"
#include "Shared/Network/Protocol.h"
#include "Shared/World/WorldProtocol.h"
#include "Shared/World/WorldTypes.h"

#include <asio.hpp>

#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <memory>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#endif

namespace worldtest {

using namespace legend;
using legend::account::AccountErrorCode;
using legend::account::AccountService;
using legend::account::CharacterService;
using legend::account::Database;
using legend::account::InitializeSchema;
using legend::client::AccountClientController;
using legend::client::AccountFlowState;
using legend::client::GameNetworkClient;
using legend::client::NetworkEvent;
using legend::client::NetworkState;
using legend::client::WorldClientController;
using legend::client::WorldFlowState;
using legend::client::WorldNetworkClient;
using legend::client::WorldNetworkEvent;
using legend::gateway::GatewayConfig;
using legend::gateway::GatewayServer;
using legend::login::LoginServer;
using legend::world::WorldServer;
namespace account = legend::account;
namespace world = legend::world;

constexpr std::uint16_t kWorldPort = 17240;
constexpr std::uint16_t kLoginPort = 17241;
constexpr std::uint16_t kGatewayPort = 17242;

inline int g_failures = 0;

inline void Check(const char* name, bool ok) {
    std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok) {
        ++g_failures;
    }
}

template <typename Predicate>
inline bool WaitUntil(Predicate&& predicate, int timeoutMs) {
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

inline std::string TempDir() {
    static const std::string dir = [] {
        std::ostringstream oss;
        oss << "testdata/world_test_" << std::to_string(_getpid());
        return oss.str();
    }();
    std::filesystem::create_directories(dir);
    return dir;
}

inline std::string TempDbPath(const std::string& name) {
    return TempDir() + "/" + name + ".db";
}

inline void RemoveDb(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

inline double QueryScalarDouble(const std::string& dbPath, const std::string& sql) {
    Database db;
    std::string error;
    if (!db.Open(dbPath, error)) {
        return -1e9;
    }
    account::Statement stmt;
    if (!stmt.Prepare(db.Handle(), sql.c_str(), error)) {
        return -1e9;
    }
    if (stmt.Step(error)) {
        return stmt.ColumnDouble(0);
    }
    return -1e9;
}

inline std::int64_t QueryScalar(const std::string& dbPath, const std::string& sql) {
    Database db;
    std::string error;
    if (!db.Open(dbPath, error)) {
        return -1;
    }
    account::Statement stmt;
    if (!stmt.Prepare(db.Handle(), sql.c_str(), error)) {
        return -1;
    }
    if (stmt.Step(error)) {
        return stmt.ColumnInt64(0);
    }
    return -1;
}

// ---- 服务器基建 ----
struct WorldTestServers {
    net::NetworkService loginService;
    net::NetworkService worldService;
    net::NetworkService gatewayService;
    std::shared_ptr<LoginServer> login;
    std::shared_ptr<WorldServer> world;
    std::shared_ptr<GatewayServer> gateway;
    std::string dbPath;
    // 阶段17：测试可调 respawnDelay（长 delay = 测试期间无重生怪干扰围殴）。
    std::uint32_t worldRespawnDelayMs = 8000;
    // 阶段18：测试可调物品参数（owner lock/TTL/强制掉落/固定 seed）。
    std::uint32_t itemOwnerLockMs = 10000;
    std::uint32_t itemDropTtlMs = 60000;
    bool testForceDropAll = false;
    std::uint64_t dropRollerSeed = 0;
    // 阶段19：QuestSnapshot 周期（默认生产 10s；测试可缩短验证周期纠偏）。
    int questSnapshotIntervalMs = 10000;
    // 阶段20：NPC 会话 TTL（默认生产 30s；NPC 测试缩短验证过期失效）。
    double npcSessionTtlSeconds = 30.0;
    // 阶段21 指令十一：Legacy Test Spawn（默认开启——阶段13~20 套件依赖 Map1 Slime；
    // 新地图布局检查用 Map2/Map3 的怪，与此开关无关）。
    bool legacyMap1TestSpawn = true;

    bool StartLogin() {
        login = std::make_shared<LoginServer>(loginService);
        login->GetConfig().listenPort = kLoginPort;
        login->GetConfig().databasePath = dbPath;
        std::string error;
        if (!login->Start(error)) {
            std::printf("[TestServers] login start failed: %s\n", error.c_str());
            return false;
        }
        loginService.Start();
        return true;
    }

    void StopLogin() {
        if (login) {
            login->Stop();
            login.reset();
        }
        loginService.Stop();
    }

    bool StartWorld() {
        world = std::make_shared<WorldServer>(worldService);
        world->GetConfig().listenPort = kWorldPort;
        world->GetConfig().loginPort = kLoginPort;
        world->GetConfig().databasePath = dbPath;
        world->GetConfig().loginReconnectSeconds = 0.3;
        world->GetConfig().snapshotIntervalMs = 50;      // 快照加速（默认 100ms）
        world->GetConfig().positionSaveIntervalSeconds = 0.5; // 指令一百：测试短周期
        world->GetConfig().respawnDelayMs = worldRespawnDelayMs; // 阶段17：测试可调
        // 阶段18：物品测试参数（Start 前设置——DropRoller 在 Start 时构建）。
        world->GetConfig().itemOwnerLockMs = itemOwnerLockMs;
        world->GetConfig().itemDropTtlMs = itemDropTtlMs;
        world->GetConfig().testForceDropAll = testForceDropAll;
        world->GetConfig().dropRollerSeed = dropRollerSeed;
        world->GetConfig().questSnapshotIntervalMs = questSnapshotIntervalMs;
        world->GetConfig().npcSessionTtlSeconds = npcSessionTtlSeconds;
        world->GetConfig().legacyMap1TestSpawn = legacyMap1TestSpawn;
        std::string error;
        if (!world->Start(error)) {
            std::printf("[TestServers] world start failed: %s\n", error.c_str());
            return false;
        }
        worldService.Start();
        return true;
    }

    void StopWorld() {
        if (world) {
            world->Stop();
            world.reset();
        }
        worldService.Stop();
    }

    bool StartGateway() {
        GatewayConfig config;
        config.listenPort = kGatewayPort;
        config.loginPort = kLoginPort;
        config.loginReconnectSeconds = 0.3;
        config.pendingLoginTimeoutSeconds = 4.0;
        config.clientIdleTimeoutSeconds = 20.0;
        gateway = std::make_shared<GatewayServer>(gatewayService, config);
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
        gatewayService.Stop();
        if (world) {
            world->Stop();
            world.reset();
        }
        worldService.Stop();
        if (login) {
            login->Stop();
            login.reset();
        }
        loginService.Stop();
    }
};

// ---- 登录侧客户端（注册/登录/选角，走 Gateway） ----
struct LoginTestClient {
    std::shared_ptr<GameNetworkClient> client = std::make_shared<GameNetworkClient>();
    AccountClientController account{*client};

    void DrainEvents() {
        std::deque<NetworkEvent> events;
        client->PollEvents(events);
        for (auto& e : events) {
            account.HandleEvent(e);
        }
    }

    bool ConnectAndWait(int timeoutMs = 5000) {
        client->Connect("127.0.0.1", kGatewayPort);
        const bool ok = WaitUntil(
            [&] {
                DrainEvents();
                return client->State() == NetworkState::Ready ||
                       client->State() == NetworkState::Failed;
            },
            timeoutMs);
        if (ok && client->State() == NetworkState::Ready) {
            account.SetState(AccountFlowState::Unauthenticated);
            return true;
        }
        return false;
    }

    bool WaitEvent(NetworkEvent::Type type, NetworkEvent& out, int timeoutMs = 10000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            DrainEvents();
            return WaitUntil([&] { return false; }, 0), true; // 事件在 Drain 中已喂控制器
        }
        return false;
    }

    void Disconnect() { client->Disconnect(true); }
};

// ---- World 客户端 ----
struct WorldTestClient {
    WorldClientController controller; // 拥有自己的 WorldNetworkClient

    WorldTestClient() {
        // 测试端口（生产默认 127.0.0.1:7200）
        controller.SetWorldEndpoint("127.0.0.1", kWorldPort);
    }

    // 阶段17：事件枚举扩容（阶段16 末尾 30 个 + 成长 3 个 + 阶段18 8 个 +
    // 阶段19 任务 7 个 = 48，留余量到 64）。
    static constexpr int kEventCapacity = 64;
    std::deque<WorldNetworkEvent> recorded[kEventCapacity];
    int counts[kEventCapacity] = {};
    // 阶段20：WorldNetworkEvent 因 NPC/Shop/Teleport payload 膨胀（~KB 级/事件），
    // 64 份按值数组使每个栈上客户端占 ~100KB；Monster 套件 12 个栈上客户端会
    // 直接溢出 1MB 默认主线程栈（函数序言 __chkstk 阶段崩溃，任何 printf 之前）。
    // lastEvent 改为堆分配（用法仍是 lastEvent[idx] 索引读写）。
    std::vector<WorldNetworkEvent> lastEvent = std::vector<WorldNetworkEvent>(kEventCapacity);

    static int IndexOf(WorldNetworkEvent::Type type) { return static_cast<int>(type); }

    WorldNetworkClient& client() { return controller.Client(); }

    void DrainEvents() {
        std::deque<WorldNetworkEvent> events;
        client().PollEvents(events);
        for (auto& e : events) {
            const int idx = IndexOf(e.type);
            if (idx >= 0 && idx < kEventCapacity) {
                ++counts[idx];
                recorded[idx].push_back(e);
            }
            controller.HandleEvent(e);
        }
    }

    bool ConnectAndEnter(const std::string& ticket, int timeoutMs = 5000) {
        controller.EnterWorldWithTicket(ticket); // Connect + 握手 + 自动 EnterWorld
        if (!WaitEvent(WorldNetworkEvent::Type::HandshakeSuccess, timeoutMs)) {
            return false;
        }
        // 等 EnterWorldSuccess / EnterWorldFailed 任一（失败也立即返回，不空等）
        const int successIdx = IndexOf(WorldNetworkEvent::Type::EnterWorldSuccess);
        const int failIdx = IndexOf(WorldNetworkEvent::Type::EnterWorldFailed);
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            DrainEvents();
            if (!recorded[successIdx].empty()) {
                lastEvent[successIdx] = recorded[successIdx].front();
                recorded[successIdx].pop_front();
                return true;
            }
            if (!recorded[failIdx].empty()) {
                lastEvent[failIdx] = recorded[failIdx].front();
                recorded[failIdx].pop_front();
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        return false;
    }

    const WorldNetworkEvent& LastEnterFailed() const {
        return lastEvent[IndexOf(WorldNetworkEvent::Type::EnterWorldFailed)];
    }

    bool WaitEvent(WorldNetworkEvent::Type type, int timeoutMs = 5000) {
        const int idx = IndexOf(type);
        const bool got = WaitUntil(
            [&] {
                DrainEvents();
                return !recorded[idx].empty();
            },
            timeoutMs);
        if (got && !recorded[idx].empty()) {
            lastEvent[idx] = recorded[idx].front();
            recorded[idx].pop_front();
            return true;
        }
        return false;
    }

    bool PeekEvent(WorldNetworkEvent::Type type, WorldNetworkEvent& out, int timeoutMs = 5000) {
        const int idx = IndexOf(type);
        const bool got = WaitUntil(
            [&] {
                DrainEvents();
                return !recorded[idx].empty();
            },
            timeoutMs);
        if (got) {
            out = recorded[idx].front();
        }
        return got;
    }

    // 等待 lastProcessedInputSequence >= minSeq 的快照（快照 50~100ms 一发，
    // 可能存在输入前的旧快照——按序丢弃）。
    bool WaitForSnapshotAfterSeq(std::uint32_t minSeq, WorldNetworkEvent& out, int timeoutMs = 3000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline) {
            WorldNetworkEvent snap;
            if (PeekEvent(WorldNetworkEvent::Type::PositionSnapshot, snap, 200)) {
                if (snap.lastProcessedInputSequence >= minSeq) {
                    recorded[IndexOf(WorldNetworkEvent::Type::PositionSnapshot)].pop_front();
                    out = snap;
                    return true;
                }
                recorded[IndexOf(WorldNetworkEvent::Type::PositionSnapshot)].pop_front();
            }
        }
        return false;
    }

    // 阶段12：等待 batch 中包含指定 characterId 的快照（旧 batch 弹出丢弃）。
    bool WaitForBatchContaining(std::uint64_t characterId, WorldNetworkEvent& out,
                                int timeoutMs = 3000) {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        const int idx = IndexOf(WorldNetworkEvent::Type::RemotePlayerBatchSnapshot);
        while (std::chrono::steady_clock::now() < deadline) {
            WorldNetworkEvent batch;
            if (PeekEvent(WorldNetworkEvent::Type::RemotePlayerBatchSnapshot, batch, 200)) {
                recorded[idx].pop_front();
                for (const auto& entry : batch.batchPlayers) {
                    if (entry.characterId == characterId) {
                        out = batch;
                        return true;
                    }
                }
            }
        }
        return false;
    }

    // 阶段12：统计某类型事件中指定 characterId 的出现次数（Spawn/Despawn 计数用）。
    int CountEventsFor(WorldNetworkEvent::Type type, std::uint64_t characterId) const {
        int n = 0;
        for (const auto& e : recorded[IndexOf(type)]) {
            if (e.characterId == characterId) {
                ++n;
            }
        }
        return n;
    }

    const WorldNetworkEvent& LastEnterSuccess() const {
        return lastEvent[IndexOf(WorldNetworkEvent::Type::EnterWorldSuccess)];
    }

    void Disconnect() { controller.Disconnect(); }
};

// ---- 直连服务：快速建号/建角/发 ticket ----
struct CharacterSeed {
    std::uint64_t accountId = 0;
    std::uint64_t characterId = 0;
    std::string name;
};

inline bool SeedAccountAndCharacter(Database& db, AccountService& accounts,
                                    CharacterService& characters, const std::string& username,
                                    const std::string& charName, CharacterSeed& out) {
    auto registered = accounts.Register(db, username, "SeedPass123!");
    if (!registered.success) {
        return false;
    }
    auto created = characters.Create(db, registered.value, charName, 1, 1);
    if (!created.success) {
        return false;
    }
    out.accountId = registered.value;
    out.characterId = created.value.characterId;
    out.name = charName;
    return true;
}

} // namespace worldtest
