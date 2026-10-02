// ---------------------------------------------------------------------------
// LegendAccountTests：阶段10 账号/角色/Session 验收（独立测试程序，指令一百零四）
// 输出：[AccountTest] completed, failures = 0（指令一百一十五）
// 数据库：临时文件（testdata/account_test_<pid>/*），不污染 data/legend_account.db
//（指令一百零三）；Network 回归由 LegendNetworkTests 负责（指令一百零八）。
// ---------------------------------------------------------------------------
#include "Client/Account/AccountClientController.h"
#include "Client/Account/CharacterSelectionController.h"
#include "Client/Network/GameNetworkClient.h"
#include "Engine/Debug/Logger.h"
#include "Engine/Network/NetworkService.h"
#include "Server/Gateway/GatewayServer.h"
#include "Server/LoginServer/Account/AccountRepository.h"
#include "Server/LoginServer/Account/AccountService.h"
#include "Server/LoginServer/Account/CharacterService.h"
#include "Server/LoginServer/Account/Database/DatabaseSchema.h"
#include "Server/LoginServer/Account/DbWorker.h"
#include "Server/LoginServer/Account/PasswordHasher.h"
#include "Server/LoginServer/Account/SessionService.h"
#include "Server/LoginServer/Account/TicketStore.h"
#include "Server/LoginServer/LoginServer.h"
#include "Shared/Account/AccountProtocol.h"
#include "Shared/Account/AccountTypes.h"
#include "Shared/Network/MessageId.h"
#include "Shared/Network/PacketCodec.h"
#include "Shared/Network/Protocol.h"

#include <asio.hpp>

#include <array>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <process.h>
#endif

using namespace legend;
using legend::account::AccountErrorCode;
using legend::account::AccountService;
using legend::account::CharacterService;
using legend::account::Database;
using legend::account::DbWorker;
using legend::account::InitializeSchema;
using legend::account::SessionService;
using legend::account::TicketStore;
using legend::client::AccountClientController;
using legend::client::AccountFlowState;
using legend::client::CharacterSelectionController;
using legend::client::GameNetworkClient;
using legend::client::NetworkEvent;
using legend::client::NetworkState;
using legend::gateway::GatewayConfig;
using legend::gateway::GatewayServer;
using legend::login::LoginServer;
namespace account = legend::account;

int RunDbServerChecks();
int RunCharacterServerChecks();
int RunLogServerChecks();
int RunPersistenceRpcChecks();

namespace {

constexpr std::uint16_t kTestGatewayPort = 17220;
constexpr std::uint16_t kTestLoginPort = 17221;

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

// ---- 临时数据库路径（指令一百零三：testdata/account_test_<pid>/<name>.db） ----
std::string TempDir() {
    static const std::string dir = [] {
        std::ostringstream oss;
        oss << "testdata/account_test_" << std::to_string(_getpid());
        return oss.str();
    }();
    std::filesystem::create_directories(dir);
    return dir;
}

std::string TempDbPath(const std::string& name) {
    return TempDir() + "/" + name + ".db";
}

void RemoveDb(const std::string& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

// 直接打开数据库并执行查询（服务停机后的持久化断言用）。
std::int64_t QueryScalar(const std::string& dbPath, const std::string& sql) {
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

// ---- in-process Login + Gateway（与 NetworkChecks 相同的基建形态） ----
struct TestServers {
    net::NetworkService loginService;
    net::NetworkService gatewayService;
    std::shared_ptr<LoginServer> login;
    std::shared_ptr<GatewayServer> gateway;
    std::string dbPath;

    bool StartLogin(const std::string& databasePath) {
        dbPath = databasePath;
        RemoveDb(dbPath); // 每个测试全新库（除非测重启持久化时传入已存在路径）
        login = std::make_shared<LoginServer>(loginService);
        login->GetConfig().listenPort = kTestLoginPort;
        login->GetConfig().databasePath = dbPath;
        std::string error;
        if (!login->Start(error)) {
            std::printf("[TestServers] login start failed: %s\n", error.c_str());
            return false;
        }
        loginService.Start();
        return true;
    }

    // 重启保留数据（指令九十九）：不删除 DB
    bool RestartLogin(const std::string& databasePath) {
        if (login) {
            login->Stop();
            login.reset();
        }
        loginService.Stop();
        login = std::make_shared<LoginServer>(loginService);
        login->GetConfig().listenPort = kTestLoginPort;
        login->GetConfig().databasePath = databasePath;
        std::string error;
        if (!login->Start(error)) {
            std::printf("[TestServers] login restart failed: %s\n", error.c_str());
            return false;
        }
        loginService.Start();
        return true;
    }

    bool StartGateway() {
        GatewayConfig config;
        config.listenPort = kTestGatewayPort;
        config.loginPort = kTestLoginPort;
        config.loginReconnectSeconds = 0.3;
        config.pendingLoginTimeoutSeconds = 4.0; // Argon2 hash ~100ms，4s 充裕
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
        if (login) {
            login->Stop();
            login.reset();
        }
        gatewayService.Stop();
        loginService.Stop();
    }
};

// ---- 测试客户端：GameNetworkClient + AccountClientController + 事件观测 ----
struct AccountTestClient {
    std::shared_ptr<GameNetworkClient> client = std::make_shared<GameNetworkClient>();
    AccountClientController account{*client};

    int counts[64] = {};
    std::deque<NetworkEvent> recorded[64]; // 按类型 FIFO（同一批到达的多条响应都能取到）

    static int IndexOf(NetworkEvent::Type type) { return static_cast<int>(type); }

    void DrainEvents() {
        std::deque<NetworkEvent> events;
        client->PollEvents(events);
        for (auto& e : events) {
            const int idx = IndexOf(e.type);
            if (idx >= 0 && idx < 64) {
                ++counts[idx];
                recorded[idx].push_back(e);
            }
            account.HandleEvent(e);
        }
    }

    bool ConnectAndWait(int timeoutMs = 5000) {
        client->Connect("127.0.0.1", kTestGatewayPort);
        const bool ok = WaitUntil(
            [&] {
                DrainEvents();
                return client->State() == NetworkState::Ready ||
                       client->State() == NetworkState::Failed;
            },
            timeoutMs);
        if (ok && client->State() == NetworkState::Ready) {
            account.SetState(AccountFlowState::Unauthenticated); // 握手成功 -> Unauthenticated
            return true;
        }
        return false;
    }

    // 取该类型最早未消费的事件（多条同类型响应按序取出，指令一百零一）。
    bool WaitEvent(NetworkEvent::Type type, NetworkEvent& out, int timeoutMs = 10000) {
        const int idx = IndexOf(type);
        const bool got = WaitUntil(
            [&] {
                DrainEvents();
                return !recorded[idx].empty();
            },
            timeoutMs);
        if (got && !recorded[idx].empty()) {
            out = recorded[idx].front();
            recorded[idx].pop_front();
            return true;
        }
        return false;
    }

    void Disconnect() { client->Disconnect(true); }
};

// ---- raw socket 假客户端（MalformedAccountPacketCheck 用） ----
bool RawHandshakeAndSend(const std::vector<std::uint8_t>& payload, std::uint16_t messageId) {
    asio::io_context io;
    asio::ip::tcp::socket socket(io);
    std::error_code ec;
    socket.connect(
        asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), kTestGatewayPort), ec);
    if (ec) {
        return false;
    }
    // ClientHello
    network::ClientHelloPayload hello;
    hello.protocolVersion = network::kProtocolVersion;
    hello.clientBuild = "0.9.0";
    hello.clientName = "RawTestClient";
    std::vector<std::uint8_t> helloPayload;
    network::EncodeClientHello(hello, helloPayload);
    network::Packet helloPacket;
    helloPacket.header.messageId = static_cast<std::uint16_t>(network::MessageId::ClientHello);
    helloPacket.payload = std::move(helloPayload);
    std::vector<std::uint8_t> frame;
    network::PacketCodec::EncodePacket(helloPacket, frame);
    asio::write(socket, asio::buffer(frame), ec);
    if (ec) {
        return false;
    }
    // ServerHello 读取
    std::array<std::uint8_t, 16> header{};
    asio::read(socket, asio::buffer(header), ec);
    if (ec) {
        return false;
    }
    network::PacketHeader decoded;
    std::string headerError;
    if (!network::PacketCodec::DecodeHeader(header.data(), header.size(), decoded, headerError)) {
        return false;
    }
    if (decoded.payloadSize > 0) {
        std::vector<std::uint8_t> responsePayload(decoded.payloadSize);
        asio::read(socket, asio::buffer(responsePayload), ec);
        if (ec) {
            return false;
        }
    }
    // 发送目标包（可能畸形）
    network::Packet out;
    out.header.messageId = messageId;
    out.payload = payload;
    frame.clear();
    network::PacketCodec::EncodePacket(out, frame);
    asio::write(socket, asio::buffer(frame), ec);
    return !ec;
}

// ===========================================================================
// 单元级：Schema / Repository / Service（直接驱动 DB Worker 同层接口）
// ===========================================================================

void RunSchemaAndCryptoChecks() {
    // ---- DatabaseMigrationCheck（指令九十七）----
    {
        const std::string path = TempDbPath("schema");
        RemoveDb(path);
        Database db;
        std::string error;
        bool ok = db.Open(path, error) && InitializeSchema(db, error);
        Check("SchemaCreateCheck: fresh db -> schema version == kCurrentSchemaVersion",
              ok && [&] {
                  account::Statement stmt;
                  return stmt.Prepare(db.Handle(), "SELECT version FROM schema_version;", error) &&
                         stmt.Step(error) && stmt.ColumnInt64(0) == account::kCurrentSchemaVersion;
              }());
        // 插入一行账号后关闭重开：不重复创建、不丢数据
        auto created = account::AccountRepository::CreateAccount(db, "persist_user",
                                                                 "argon2$fakehash");
        Check("SchemaCreateCheck: account inserted", created.success && created.value > 0);
        db.Close();
        Database db2;
        ok = db2.Open(path, error) && InitializeSchema(db2, error);
        Check("DatabaseMigrationCheck: reopen keeps version and data",
              ok && [&] {
                  account::Statement stmt;
                  return stmt.Prepare(db2.Handle(), "SELECT COUNT(*) FROM accounts;", error) &&
                         stmt.Step(error) && stmt.ColumnInt64(0) == 1;
              }());
        // 空库再开 -> 版本仍 1（不重建）
        int version = 0;
        {
            account::Statement stmt;
            stmt.Prepare(db2.Handle(), "SELECT version FROM schema_version;", error);
            if (stmt.Step(error)) {
                version = static_cast<int>(stmt.ColumnInt64(0));
            }
        }
        Check("DatabaseMigrationCheck: version stays 1 after reopen",
              version == account::kCurrentSchemaVersion);
        RemoveDb(path);
    }

    // ---- DatabaseCorruptionFailCheck（指令九十八）----
    {
        const std::string path = TempDbPath("corrupt");
        RemoveDb(path);
        {
            std::ofstream out(path, std::ios::binary);
            out << "this is definitely not a sqlite database file" << std::string(512, 'x');
        }
        Database db;
        std::string error;
        bool openOk = db.Open(path, error);
        bool initOk = openOk && InitializeSchema(db, error);
        Check("DatabaseCorruptionFailCheck: corrupt db fails schema init (clear error)",
              !initOk && !error.empty());
        db.Close();
        RemoveDb(path);
    }

    // ---- PasswordHashCheck（指令七十二）----
    {
        std::string hash;
        std::string error;
        const std::string password = "StrongPass123!";
        bool ok = account::PasswordHasher::Hash(password, hash, error);
        std::string verifyError;
        const bool verifyOk =
            account::PasswordHasher::Verify(password, hash, verifyError) && verifyError.empty();
        const bool verifyBad =
            !account::PasswordHasher::Verify("WrongPass123!", hash, verifyError);
        Check("PasswordHashCheck: hash != password, verify true/false",
              ok && hash != password && hash.find("$argon2id$") == 0 && verifyOk && verifyBad);
        // token：256-bit hex（指令三十）
        const std::string token = account::GenerateTokenHex(32);
        Check("TokenCheck: 256-bit CSPRNG token hex(64)", token.size() == 64);
        Check("TokenCheck: tokens unique",
              token != account::GenerateTokenHex(32));
        Check("Sha256HexCheck", account::Sha256Hex("abc").size() == 64);
    }
}

void RunAccountServiceChecks() {
    const std::string path = TempDbPath("account_service");
    RemoveDb(path);
    Database db;
    std::string error;
    const bool opened = db.Open(path, error) && InitializeSchema(db, error);
    Check("AccountServiceChecks: db ready", opened);

    const AccountService service(5, 60);

    // ---- Username/Password 校验（指令十二/十三）----
    Check("UsernameValidationCheck",
          !account::IsValidUsername("ab") &&                       // < 3
              !account::IsValidUsername(std::string(21, 'a')) &&   // > 20
              !account::IsValidUsername("bad name") &&             // 空格
              !account::IsValidUsername("bad-name") &&             // 非法字符
              account::IsValidUsername("Test_User_01"));
    Check("PasswordValidationCheck",
          !account::IsValidPassword("short") &&                    // < 8
              !account::IsValidPassword(std::string(65, 'x')) &&   // > 64
              account::IsValidPassword("GoodPass123"));

    // ---- AccountRegisterCheck（指令七十）----
    auto created = service.Register(db, "testuser", "GoodPass123");
    Check("AccountRegisterCheck: register success", created.success && created.value > 0);
    Check("AccountRegisterCheck: account row exists",
          QueryScalar(path, "SELECT COUNT(*) FROM accounts WHERE username='testuser';") == 1);

    // ---- DuplicateUsernameCheck（指令七十一：大小写不敏感）----
    auto dup = service.Register(db, "TestUser", "GoodPass456");
    Check("DuplicateUsernameCheck: TestUser after testuser -> UsernameAlreadyExists",
          !dup.success &&
              dup.errorCode == AccountErrorCode::UsernameAlreadyExists);

    // ---- 二次注册同用户 ----
    auto dup2 = service.Register(db, "testuser", "GoodPass456");
    Check("DuplicateUsernameCheck: exact duplicate rejected",
          !dup2.success && dup2.errorCode == AccountErrorCode::UsernameAlreadyExists);

    // ---- LoginSuccessCheck（指令七十四）----
    SessionService sessions;
    auto login = service.Login(db, "TestUser", "GoodPass123", 3600);
    Check("LoginSuccessCheck: login success accountId correct",
          login.success && login.value.accountId == created.value);
    Check("LoginSuccessCheck: session token non-empty", !login.value.sessionToken.empty());
    Check("LoginSuccessCheck: session row exists in db",
          QueryScalar(path, "SELECT COUNT(*) FROM sessions;") == 1);
    Check("LoginSuccessCheck: last_login_at updated",
          QueryScalar(path, "SELECT COUNT(*) FROM accounts WHERE last_login_at IS NOT NULL;") == 1);
    Check("LoginSuccessCheck: failed_login_count reset to 0",
          QueryScalar(path, "SELECT failed_login_count FROM accounts WHERE id=" +
                                 std::to_string(created.value) + ";") == 0);

    // ---- LoginFailureCheck（指令七十五）----
    auto badLogin = service.Login(db, "testuser", "WrongPass999", 3600);
    Check("LoginFailureCheck: wrong password -> InvalidCredentials",
          !badLogin.success && badLogin.errorCode == AccountErrorCode::InvalidCredentials);
    auto noUser = service.Login(db, "ghost_user", "Whatever123", 3600);
    Check("LoginFailureCheck: unknown user -> InvalidCredentials",
          !noUser.success && noUser.errorCode == AccountErrorCode::InvalidCredentials);

    // ---- FailedLoginCountCheck（指令七十六）----
    auto afterFail = account::AccountRepository::FindAccountByUsername(db, "testuser");
    Check("FailedLoginCountCheck: failure increments count",
          afterFail.success && afterFail.value.has_value() &&
              afterFail.value->failedLoginCount == 1);
    auto relogin = service.Login(db, "testuser", "GoodPass123", 3600);
    Check("FailedLoginCountCheck: success resets count",
          relogin.success &&
              QueryScalar(path, "SELECT failed_login_count FROM accounts WHERE id=" +
                                     std::to_string(created.value) + ";") == 0);

    // ---- LockoutCheck（指令七十七：短 lockout）----
    {
        const std::string lockPath = TempDbPath("lockout");
        RemoveDb(lockPath);
        Database lockDb;
        std::string lockError;
        lockDb.Open(lockPath, lockError);
        InitializeSchema(lockDb, lockError);
        const AccountService lockingService(5, 1); // 5 次 -> 锁 1 秒
        lockingService.Register(lockDb, "lock_user", "LockPass123");
        bool sawInvalid = true;
        for (int i = 0; i < 5; ++i) {
            auto attempt = lockingService.Login(lockDb, "lock_user", "BadPass123", 3600);
            if (attempt.success || attempt.errorCode != AccountErrorCode::InvalidCredentials) {
                sawInvalid = false;
            }
        }
        auto sixth = lockingService.Login(lockDb, "lock_user", "LockPass123", 3600);
        Check("LockoutCheck: 5 consecutive failures -> TooManyAttempts",
              sawInvalid && !sixth.success &&
                  sixth.errorCode == AccountErrorCode::TooManyAttempts);
        std::this_thread::sleep_for(std::chrono::milliseconds(1200)); // lockout 过期
        auto afterLock = lockingService.Login(lockDb, "lock_user", "LockPass123", 3600);
        Check("LockoutCheck: unlock after lockout expiry", afterLock.success);
        RemoveDb(lockPath);
    }

    // ---- AccountStatusCheck（指令十六）----
    {
        account::Statement stmt;
        stmt.Prepare(db.Handle(), "UPDATE accounts SET status=1 WHERE id=?;", error);
        stmt.BindInt64(1, static_cast<std::int64_t>(created.value));
        stmt.Step(error);
        auto disabled = service.Login(db, "testuser", "GoodPass123", 3600);
        Check("AccountStatusCheck: disabled -> AccountDisabled",
              !disabled.success && disabled.errorCode == AccountErrorCode::AccountDisabled);
        account::Statement stmt2;
        stmt2.Prepare(db.Handle(), "UPDATE accounts SET status=2 WHERE id=?;", error);
        stmt2.BindInt64(1, static_cast<std::int64_t>(created.value));
        stmt2.Step(error);
        auto banned = service.Login(db, "testuser", "GoodPass123", 3600);
        Check("AccountStatusCheck: banned -> AccountBanned",
              !banned.success && banned.errorCode == AccountErrorCode::AccountBanned);
        account::Statement stmt3;
        stmt3.Prepare(db.Handle(), "UPDATE accounts SET status=0 WHERE id=?;", error);
        stmt3.BindInt64(1, static_cast<std::int64_t>(created.value));
        stmt3.Step(error);
    }

    // ---- SessionExpiryCheck（指令七十九）----
    {
        auto shortSession = sessions.Create(db, created.value, 1); // 1 秒
        Check("SessionExpiryCheck: create ok", shortSession.success);
        auto resumeNow = sessions.Resume(db, shortSession.value.token);
        Check("SessionExpiryCheck: resume before expiry ok", resumeNow.success);
        std::this_thread::sleep_for(std::chrono::milliseconds(1300));
        auto resumeLater = sessions.Resume(db, shortSession.value.token);
        Check("SessionExpiryCheck: resume after expiry fails",
              !resumeLater.success && resumeLater.errorCode == AccountErrorCode::SessionInvalid);
    }

    // ---- SessionRevocationCheck（指令八十）----
    {
        auto session = sessions.Create(db, created.value, 3600);
        auto resumeBefore = sessions.Resume(db, session.value.token);
        Check("SessionRevocationCheck: resume ok before revoke", resumeBefore.success);
        // 直接经 Repository 吊销（DB 行 revoked=1）
        account::Statement stmt;
        stmt.Prepare(db.Handle(), "SELECT id FROM sessions WHERE revoked=0 ORDER BY id DESC;", error);
        std::int64_t sessionId = 0;
        if (stmt.Step(error)) {
            sessionId = stmt.ColumnInt64(0);
        }
        auto revoked = sessions.Revoke(db, static_cast<std::uint64_t>(sessionId));
        auto resumeAfter = sessions.Resume(db, session.value.token);
        Check("SessionRevocationCheck: revoked -> resume fails",
              revoked.success && !resumeAfter.success &&
                  resumeAfter.errorCode == AccountErrorCode::SessionInvalid);
    }

    // ---- Session token 不得入库原文（指令三十）----
    {
        auto session = sessions.Create(db, created.value, 3600);
        account::Statement stmt;
        stmt.Prepare(db.Handle(), "SELECT COUNT(*) FROM sessions WHERE session_token_hash=?;",
                     error);
        stmt.BindText(1, session.value.token);
        bool rawStored = false;
        if (stmt.Step(error)) {
            rawStored = stmt.ColumnInt64(0) > 0;
        }
        Check("SessionTokenHashCheck: raw token never stored (only hash)", !rawStored);
    }

    RemoveDb(path);
}

void RunCharacterServiceChecks() {
    const std::string path = TempDbPath("character_service");
    RemoveDb(path);
    Database db;
    std::string error;
    const bool opened = db.Open(path, error) && InitializeSchema(db, error);
    Check("CharacterServiceChecks: db ready", opened);

    const AccountService accountService(5, 60);
    const CharacterService characters(account::kMaxCharactersPerAccount);

    auto accountA = accountService.Register(db, "owner_a", "OwnerPass1");
    auto accountB = accountService.Register(db, "owner_b", "OwnerPass2");
    if (!accountA.success || !accountB.success) {
        Check("CharacterServiceChecks: accounts created", false);
        return;
    }

    // ---- CharacterListEmptyCheck（指令八十一）----
    auto emptyList = characters.List(db, accountA.value);
    Check("CharacterListEmptyCheck: new account -> 0 characters",
          emptyList.success && emptyList.value.empty());

    // ---- CharacterNameValidationCheck（指令三十六 + 阶段26 指令十一：UTF-8/中文）----
    auto badName = characters.Create(db, accountA.value, " A ", 1, 1, 1);
    auto badName2 = characters.Create(db, accountA.value, "a", 1, 1, 1);
    auto badName3 = characters.Create(db, accountA.value, "bad\x01name", 1, 1, 1);
    // 阶段26：危险符号 / emoji / 非法 UTF-8 / 超长码点（13 个汉字）/ 单个汉字。
    auto badSymbol = characters.Create(db, accountA.value, "bad<name>", 1, 1, 1);
    auto badEmoji = characters.Create(db, accountA.value, "bad\xF0\x9F\x98\x80name", 1, 1, 1);
    auto badUtf8 = characters.Create(db, accountA.value, "bad\xE4\xB8", 1, 1, 1); // 截断的 UTF-8 序列（E4 B8 后中断）
    auto badTooLongCn =
        characters.Create(db, accountA.value,
                          "\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD"
                          "\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD"
                          "\xE4\xB8\xAD\xE4\xB8\xAD\xE4\xB8\xAD", // 13 个"中"
                          1, 1, 1);
    auto badSingleCn = characters.Create(db, accountA.value, "\xE4\xB8\xAD", 1, 1, 1); // 1 个汉字
    Check("CharacterNameValidationCheck: invalid names rejected",
          !badName.success && !badName2.success && !badName3.success && !badSymbol.success &&
              !badEmoji.success && !badUtf8.success && !badTooLongCn.success &&
              !badSingleCn.success);
    // 阶段26 指令十一：中文角色名合法（2~12 码点，白名单内）。
    auto chineseName = characters.Create(db, accountA.value,
                                         "\xE4\xB8\xAD\xE6\x96\x87\xE8\x8B\xB1\xE9\x9B\x84", 1, 1,
                                         1); // "中文英雄"
    Check("CharacterNameValidationCheck: chinese name accepted",
          chineseName.success && chineseName.value.name == "\xE4\xB8\xAD\xE6\x96\x87\xE8\x8B\xB1\xE9\x9B\x84");
    if (chineseName.success) {
        (void)characters.Delete(db, accountA.value, chineseName.value.characterId);
    }
    auto badClass = characters.Create(db, accountA.value, "BadClass", 99, 1, 1);
    auto badGender = characters.Create(db, accountA.value, "BadGender", 1, 9, 1);
    auto badVisual = characters.Create(db, accountA.value, "BadVisual", 1, 1, 99);
    Check("ClassGenderValidationCheck: invalid class/gender rejected",
          !badClass.success && !badGender.success);
    Check("VisualIdValidationCheck: invalid visualId rejected (stage26)", !badVisual.success);

    // ---- CharacterCreateCheck（指令八十二）----
    auto warrior = characters.Create(db, accountA.value, "TestWarrior", 1, 1, 2);
    Check("CharacterCreateCheck: warrior created, level=1, id>0",
          warrior.success && warrior.value.level == 1 && warrior.value.characterId > 0 &&
              warrior.value.name == "TestWarrior" && warrior.value.classId == 1 &&
              warrior.value.gender == 1);
    Check("CharacterCreateCheck: visualId persisted (stage26)",
          warrior.success && warrior.value.visualId == 2);
    Check("CharacterCreateCheck: character row exists",
          QueryScalar(path, "SELECT COUNT(*) FROM characters WHERE name='TestWarrior';") == 1);

    // ---- CharacterNameDuplicateCheck（指令八十四）----
    auto duplicate = characters.Create(db, accountA.value, "TestWarrior", 2, 2, 1);
    Check("CharacterNameDuplicateCheck: same name second attempt fails",
          !duplicate.success && duplicate.errorCode == AccountErrorCode::CharacterNameTaken);

    // ---- CharacterLimitCheck（指令八十五 + 阶段26 指令十三）----
    auto c2 = characters.Create(db, accountA.value, "Hero2", 2, 2, 1);
    auto c3 = characters.Create(db, accountA.value, "Hero3", 3, 1, 3);
    auto c4 = characters.Create(db, accountA.value, "Hero4", 1, 2, 1);
    auto c5 = characters.Create(db, accountA.value, "Hero5", 1, 1, 1);
    Check("CharacterLimitCheck: 4 creations ok, 5th -> CharacterLimitReached",
          c2.success && c3.success && c4.success && !c5.success &&
              c5.errorCode == AccountErrorCode::CharacterLimitReached);
    auto fullList = characters.List(db, accountA.value);
    Check("CharacterLimitCheck: list has exactly 4",
          fullList.success && fullList.value.size() == 4);

    // ---- CharacterOwnershipCheck（指令八十六）----
    auto foreignDelete = characters.Delete(db, accountB.value, warrior.value.characterId);
    auto foreignSelect = characters.Select(db, accountB.value, warrior.value.characterId);
    Check("CharacterOwnershipCheck: other account delete/select rejected",
          !foreignDelete.success && foreignDelete.errorCode == AccountErrorCode::CharacterNotOwned &&
              !foreignSelect.success &&
              foreignSelect.errorCode == AccountErrorCode::CharacterNotOwned);

    // ---- CharacterSelectCheck（指令八十八）----
    auto selectOk = characters.Select(db, accountA.value, warrior.value.characterId);
    Check("CharacterSelectCheck: select own character ok",
          selectOk.success && selectOk.value.characterId == warrior.value.characterId);

    // ---- CharacterDeleteCheck（指令八十七：soft delete）----
    auto deleted = characters.Delete(db, accountA.value, c2.value.characterId);
    auto listAfterDelete = characters.List(db, accountA.value);
    Check("CharacterDeleteCheck: soft delete removes from list",
          deleted.success && listAfterDelete.success && listAfterDelete.value.size() == 3);
    Check("CharacterDeleteCheck: db row still exists with deleted=1",
          QueryScalar(path, "SELECT COUNT(*) FROM characters WHERE id=" +
                                 std::to_string(c2.value.characterId) + " AND deleted=1;") == 1);
    auto deletedAgain = characters.Delete(db, accountA.value, c2.value.characterId);
    Check("CharacterDeleteCheck: double delete -> CharacterNotFound",
          !deletedAgain.success && deletedAgain.errorCode == AccountErrorCode::CharacterNotFound);

    // ---- CharacterPersistenceCheck（指令八十三）----
    db.Close();
    Database db2;
    bool reopenOk = db2.Open(path, error) && InitializeSchema(db2, error);
    auto persisted = characters.List(db2, accountA.value);
    Check("CharacterPersistenceCheck: characters survive db close/reopen",
          reopenOk && persisted.success && persisted.value.size() == 3);
    db2.Close();

    // ---- TicketStore（指令五十一/五十二/五十三 / 八十九/九十/九十一）----
    {
        TicketStore tickets;
        const std::string ticket = tickets.Create(accountA.value, warrior.value.characterId, 60.0);
        Check("SelectionTicketCheck: ticket non-empty and random",
              ticket.size() == 64 && ticket != tickets.Create(accountA.value, 999, 60.0));
        Check("SelectionTicketCheck: validate ok",
              tickets.Validate(ticket, accountA.value, warrior.value.characterId));
        Check("SelectionTicketCheck: validate rejects wrong account/character",
              !tickets.Validate(ticket, accountB.value, warrior.value.characterId) &&
                  !tickets.Validate(ticket, accountA.value, 12345));
        Check("TicketConsumeCheck: first consume ok",
              tickets.Consume(ticket, accountA.value, warrior.value.characterId));
        Check("TicketConsumeCheck: second consume fails",
              !tickets.Consume(ticket, accountA.value, warrior.value.characterId));
        Check("TicketConsumeCheck: consumed ticket validate fails",
              !tickets.Validate(ticket, accountA.value, warrior.value.characterId));

        const std::string shortTicket = tickets.Create(accountA.value, 777, 0.05);
        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        Check("TicketExpiryCheck: expired ticket validate false",
              !tickets.Validate(shortTicket, accountA.value, 777));
        Check("TicketExpiryCheck: expired ticket consume false",
              !tickets.Consume(shortTicket, accountA.value, 777));
    }

    RemoveDb(path);
}

// ===========================================================================
// 阶段10.1：schema_version 单行化 / 旧库兼容 / DbWorker Flush
// ===========================================================================

void RunSchemaHardeningChecks() {
    // ---- SchemaSingleRowCheck（指令六）：新库初始化后恒单行 ----
    {
        const std::string path = TempDbPath("schema_single");
        RemoveDb(path);
        Database db;
        std::string error;
        bool ok = db.Open(path, error) && InitializeSchema(db, error);
        Check("SchemaSingleRowCheck: fresh db schema_version COUNT == 1",
              ok && QueryScalar(path, "SELECT COUNT(*) FROM schema_version;") == 1);
        // 重复 InitializeSchema（幂等重入）仍单行
        ok = ok && InitializeSchema(db, error);
        Check("SchemaSingleRowCheck: re-init keeps single row",
              ok && QueryScalar(path, "SELECT COUNT(*) FROM schema_version;") == 1);
        db.Close();
        RemoveDb(path);
    }

    // ---- MigrationV2SimulationCheck（指令七）：模拟未来 v2 Migration 写版本 ----
    {
        const std::string path = TempDbPath("schema_v2");
        RemoveDb(path);
        Database db;
        std::string error;
        bool ok = db.Open(path, error) && InitializeSchema(db, error);
        // 模拟 v2 Migration 完成：以与 WriteSchemaVersion 相同的 UPSERT 写 version=2
        //（旧实现此处 INSERT 会产生 1、2 两行）
        if (ok) {
            account::Statement stmt;
            stmt.Prepare(db.Handle(),
                         "INSERT INTO schema_version (id, version) VALUES (1, 2) "
                         "ON CONFLICT(id) DO UPDATE SET version = excluded.version;",
                         error);
            stmt.Step(error);
            ok = error.empty();
        }
        db.Close();
        // 重新打开：version=2 且仍单行
        Database db2;
        ok = db2.Open(path, error) && ok;
        const std::int64_t version =
            ok ? QueryScalar(path, "SELECT version FROM schema_version WHERE id = 1;") : -1;
        const std::int64_t count =
            ok ? QueryScalar(path, "SELECT COUNT(*) FROM schema_version;") : -1;
        Check("MigrationV2SimulationCheck: reopen version == 2, single row",
              ok && version == 2 && count == 1);
        db2.Close();
        RemoveDb(path);
    }

    // ---- OldSchemaVersionCompatibilityCheck（指令八）：v1 旧结构兼容升级 ----
    {
        const std::string path = TempDbPath("schema_old");
        RemoveDb(path);
        Database db;
        std::string error;
        bool opened = db.Open(path, error);
        // 手工构造旧 v1 结构（完整三表：accounts/characters/sessions 均无 gold 列）
        // + schema_version(version) 一行 1 + 已有账号数据
        if (opened) {
            opened = db.Execute("CREATE TABLE schema_version (version INTEGER NOT NULL);", error) &&
                     db.Execute("INSERT INTO schema_version (version) VALUES (1);", error) &&
                     db.Execute(
                         "CREATE TABLE accounts ("
                         "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                         "  username TEXT NOT NULL UNIQUE COLLATE NOCASE,"
                         "  password_hash TEXT NOT NULL,"
                         "  created_at INTEGER NOT NULL,"
                         "  last_login_at INTEGER,"
                         "  status INTEGER NOT NULL DEFAULT 0,"
                         "  failed_login_count INTEGER NOT NULL DEFAULT 0,"
                         "  locked_until INTEGER"
                         ");",
                         error) &&
                     db.Execute(
                         "CREATE TABLE characters ("
                         "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
                         "  account_id INTEGER NOT NULL,"
                         "  name TEXT NOT NULL UNIQUE,"
                         "  class_id INTEGER NOT NULL,"
                         "  gender INTEGER NOT NULL,"
                         "  level INTEGER NOT NULL DEFAULT 1,"
                         "  exp INTEGER NOT NULL DEFAULT 0,"
                         "  map_id INTEGER NOT NULL DEFAULT 1,"
                         "  position_x REAL NOT NULL DEFAULT 0,"
                         "  position_y REAL NOT NULL DEFAULT 0,"
                         "  created_at INTEGER NOT NULL,"
                         "  last_played_at INTEGER,"
                         "  deleted INTEGER NOT NULL DEFAULT 0"
                         ");",
                         error) &&
                         db.Execute("INSERT INTO accounts (username, password_hash, created_at) "
                                    "VALUES ('legacy_user', 'legacy_hash', 1);",
                                    error);
        }
        db.Close();
        // 兼容升级：InitializeSchema 自动把旧表迁到单行新格式 + Migration 2 加 gold，
        // 账号数据不丢
        Database db2;
        bool ok = db2.Open(path, error) && opened && InitializeSchema(db2, error);
        const std::int64_t version =
            ok ? QueryScalar(path, "SELECT version FROM schema_version WHERE id = 1;") : -1;
        const std::int64_t count =
            ok ? QueryScalar(path, "SELECT COUNT(*) FROM schema_version;") : -1;
        const std::int64_t legacy =
            ok ? QueryScalar(path, "SELECT COUNT(*) FROM accounts WHERE username='legacy_user';")
               : -1;
        // Migration 2 必须给旧 characters 表补上 gold 列（默认 0）
        const std::int64_t goldCols =
            ok ? QueryScalar(path, "SELECT COUNT(*) FROM pragma_table_info('characters') "
                                   "WHERE name = 'gold';")
               : -1;
        // 账号表经 Repository 仍可正常写入
        auto created = ok ? account::AccountRepository::CreateAccount(db2, "new_user", "hash2")
                          : account::RepositoryResult<std::uint64_t>{};
        Check("OldSchemaVersionCompatibilityCheck: old v1 db upgraded to current, data intact",
              ok && version == account::kCurrentSchemaVersion && count == 1 && legacy == 1 &&
                  goldCols == 1 && created.success);
        db2.Close();
        RemoveDb(path);
    }
}

void RunDbWorkerChecks() {
    // ---- DbWorkerFlushCheck（指令十三）：Flush 必须等执行中任务完成 ----
    {
        DbWorker worker;
        worker.Start();
        std::atomic<bool> done{false};
        worker.Post([&done] {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            done.store(true);
        });
        worker.Flush(); // 若在任务完成前返回，此处 done 仍为 false
        Check("DbWorkerFlushCheck: flush waits for running task", done.load());
        worker.Stop();
    }

    // ---- DbWorkerMultipleFlushCheck（指令十四）：10 任务全部完成 ----
    {
        DbWorker worker;
        worker.Start();
        std::atomic<int> counter{0};
        for (int i = 0; i < 10; ++i) {
            worker.Post([&counter] { ++counter; });
        }
        worker.Flush();
        Check("DbWorkerMultipleFlushCheck: counter == 10 after flush", counter.load() == 10);
        worker.Stop();
    }

    // ---- DbWorkerStopCheck（指令十五）：Stop 后已入队任务全部完成、不得丢 ----
    {
        DbWorker worker;
        worker.Start();
        std::atomic<int> counter{0};
        for (int i = 0; i < 20; ++i) {
            worker.Post([&counter] { ++counter; });
        }
        worker.Stop(); // join 前必须清空队列
        Check("DbWorkerStopCheck: all queued tasks complete after stop", counter.load() == 20);
    }

    // ---- 异常保护（指令十二）：任务抛异常不得杀死线程/卡死 Flush ----
    {
        DbWorker worker;
        worker.Start();
        std::atomic<bool> afterException{false};
        worker.Post([] { throw std::runtime_error("db worker test exception"); });
        worker.Post([&afterException] { afterException.store(true); });
        worker.Flush();
        Check("DbWorkerExceptionCheck: worker survives task exception", afterException.load());
        worker.Stop();
    }
}

// ===========================================================================
// 网络级：完整链路（Client -> Gateway -> LoginServer -> SQLite）
// ===========================================================================

void RunMalformedPacketCheck(TestServers& servers) {
    // ---- MalformedAccountPacketCheck（指令九十二）----
    // 伪造 RegisterRequest 缺 password（requestId + username 后截断）
    {
        std::vector<std::uint8_t> badPayload;
        {
            network::ByteWriter w(badPayload);
            w.WriteUInt64(7777);
            w.WriteString("malformed_user");
        }
        bool sent = RawHandshakeAndSend(badPayload,
                                        static_cast<std::uint16_t>(network::MessageId::RegisterRequest));
        Check("MalformedAccountPacketCheck: malformed packet sent", sent);
    }
    // 非法 string length：uint16 长度 60000 但无数据
    {
        std::vector<std::uint8_t> badPayload;
        network::ByteWriter w(badPayload);
        w.WriteUInt64(8888);
        w.WriteUInt16(60000); // 超出剩余字节的 length 前缀
        RawHandshakeAndSend(badPayload,
                            static_cast<std::uint16_t>(network::MessageId::RegisterRequest));
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    // Gateway/Login 仍可服务：正常客户端注册成功
    AccountTestClient good;
    Check("MalformedAccountPacketCheck: gateway alive after malformed packets",
          good.ConnectAndWait());
    good.account.SendRegister("after_malformed", "GoodPass123");
    NetworkEvent response;
    Check("MalformedAccountPacketCheck: register still works",
          good.WaitEvent(NetworkEvent::Type::RegisterResponse, response) && response.success);
    good.Disconnect();
}

void RunAccountFlowChainCheck(TestServers& servers) {
    // ---- 验收主链（指令一百一十三）----
    AccountTestClient client;
    Check("AccountFlowChainCheck: connect", client.ConnectAndWait());
    Check("AccountFlowChainCheck: state Unauthenticated after handshake",
          client.account.State() == AccountFlowState::Unauthenticated);

    // Register stage10_user / StrongPass123!
    client.account.SendRegister("stage10_user", "StrongPass123!");
    NetworkEvent registerResponse;
    Check("AccountFlowChainCheck: register success",
          client.WaitEvent(NetworkEvent::Type::RegisterResponse, registerResponse) &&
              registerResponse.success);
    Check("AccountFlowChainCheck: back to Unauthenticated after register",
          client.account.State() == AccountFlowState::Unauthenticated);

    // Login
    client.account.SendAccountLogin("stage10_user", "StrongPass123!");
    NetworkEvent loginResponse;
    Check("AccountFlowChainCheck: login success with session",
          client.WaitEvent(NetworkEvent::Type::AccountLoginResponse, loginResponse) &&
              loginResponse.success && !loginResponse.sessionToken.empty() &&
              client.account.State() == AccountFlowState::Authenticated);

    // CharacterList = 0
    client.account.SendCharacterList(client.account.SessionToken());
    NetworkEvent listEmpty;
    Check("AccountFlowChainCheck: character list = 0",
          client.WaitEvent(NetworkEvent::Type::CharacterListResponse, listEmpty) &&
              listEmpty.success && listEmpty.characters.empty() &&
              client.account.State() == AccountFlowState::CharacterListReady);

    // Create StageHero / Warrior / Male
    client.account.SendCreateCharacter(client.account.SessionToken(), "StageHero", 1, 1);
    NetworkEvent createResponse;
    Check("AccountFlowChainCheck: create StageHero ok (level=1, id>0)",
          client.WaitEvent(NetworkEvent::Type::CharacterCreateResponse, createResponse) &&
              createResponse.success && createResponse.character.name == "StageHero" &&
              createResponse.character.level == 1 && createResponse.character.characterId > 0);
    const std::uint64_t heroId = createResponse.character.characterId;

    // CharacterList = 1
    client.account.SendCharacterList(client.account.SessionToken());
    NetworkEvent listOne;
    Check("AccountFlowChainCheck: character list = 1",
          client.WaitEvent(NetworkEvent::Type::CharacterListResponse, listOne) &&
              listOne.success && listOne.characters.size() == 1);

    // Select StageHero -> selectionTicket
    CharacterSelectionController selection;
    selection.RequestSelect(client.account, heroId);
    Check("AccountFlowChainCheck: state SelectingCharacter",
          client.account.State() == AccountFlowState::SelectingCharacter);
    NetworkEvent selectResponse;
    Check("AccountFlowChainCheck: select success with ticket",
          client.WaitEvent(NetworkEvent::Type::CharacterSelectResponse, selectResponse) &&
              selectResponse.success && selectResponse.character.characterId == heroId &&
              !selectResponse.selectionTicket.empty() &&
              client.account.State() == AccountFlowState::CharacterSelected);

    // Ticket 服务器侧 Validate（阶段10 无 WorldServer，仅验证结构）
    if (servers.login) {
        Check("SelectionTicketChainCheck: server validates issued ticket",
              servers.login->Tickets().Validate(selectResponse.selectionTicket,
                                                loginResponse.accountId, heroId));
    }

    // 断开 -> 重连 -> SessionResume
    client.Disconnect();
    WaitUntil([&] { client.DrainEvents(); return client.client->State() == NetworkState::Disconnected; }, 3000);
    client.client->Connect("127.0.0.1", kTestGatewayPort);
    const bool reconnected = WaitUntil(
        [&] {
            client.DrainEvents();
            return client.client->State() == NetworkState::Ready ||
                   client.client->State() == NetworkState::Failed;
        },
        5000);
    Check("AccountFlowChainCheck: reconnect ok", reconnected);
    client.account.SetState(AccountFlowState::Unauthenticated);
    client.account.SendSessionResume(client.account.SessionToken());
    NetworkEvent resumeResponse;
    Check("AccountFlowChainCheck: SessionResume restores accountId",
          client.WaitEvent(NetworkEvent::Type::SessionResumeResponse, resumeResponse) &&
              resumeResponse.success &&
              resumeResponse.accountId == loginResponse.accountId &&
              client.account.State() == AccountFlowState::Authenticated);

    // CharacterList 仍 = 1
    client.account.SendCharacterList(client.account.SessionToken());
    NetworkEvent listAfter;
    Check("AccountFlowChainCheck: character list still = 1 after resume",
          client.WaitEvent(NetworkEvent::Type::CharacterListResponse, listAfter) &&
              listAfter.success && listAfter.characters.size() == 1);

    // Select 仍成功
    CharacterSelectionController selection2;
    selection2.RequestSelect(client.account, heroId);
    NetworkEvent select2;
    Check("AccountFlowChainCheck: select still ok after resume",
          client.WaitEvent(NetworkEvent::Type::CharacterSelectResponse, select2) &&
              select2.success);
    client.Disconnect();
}

void RunRequestCorrelationAndDuplicateChecks(TestServers& servers) {
    AccountTestClient client;
    Check("RequestCorrelationCheck: connect", client.ConnectAndWait());
    client.account.SendRegister("correlation_user", "GoodPass123");
    NetworkEvent reg;
    client.WaitEvent(NetworkEvent::Type::RegisterResponse, reg);
    client.account.SendAccountLogin("correlation_user", "GoodPass123");
    NetworkEvent login;
    client.WaitEvent(NetworkEvent::Type::AccountLoginResponse, login);

    // ---- Request Correlation（指令一百零一）：连发两个 List 请求，requestId 回传 ----
    client.account.SendCharacterList(client.account.SessionToken());
    const std::uint64_t firstId = client.account.LastRequestId();
    client.account.SendCharacterList(client.account.SessionToken());
    const std::uint64_t secondId = client.account.LastRequestId();
    NetworkEvent r1;
    NetworkEvent r2;
    const bool got1 = client.WaitEvent(NetworkEvent::Type::CharacterListResponse, r1);
    const bool got2 = client.WaitEvent(NetworkEvent::Type::CharacterListResponse, r2);
    Check("RequestCorrelationCheck: requestIds echoed",
          got1 && got2 && ((r1.requestId == firstId && r2.requestId == secondId) ||
                           (r1.requestId == secondId && r2.requestId == firstId)));

    // ---- 重复写请求（指令一百零二）：同 Session Register 在途时再次 Register ----
    AccountTestClient dup;
    Check("DuplicateWriteRequestCheck: connect", dup.ConnectAndWait());
    const std::uint64_t firstRequestId = dup.account.LastRequestId() + 1; // 下一个分配 id
    dup.account.SendRegister("dupwrite_user", "GoodPass123");
    dup.account.SendRegister("dupwrite_user", "GoodPass123");
    NetworkEvent firstResponse;
    NetworkEvent secondResponse;
    const bool gotFirst = dup.WaitEvent(NetworkEvent::Type::RegisterResponse, firstResponse);
    const bool gotSecond = dup.WaitEvent(NetworkEvent::Type::RegisterResponse, secondResponse);
    Check("DuplicateWriteRequestCheck: only one success, duplicate rejected",
          gotFirst && gotSecond &&
              (firstResponse.requestId == firstRequestId ? firstResponse.success
                                                          : secondResponse.success) &&
              (firstResponse.requestId == firstRequestId
                   ? (secondResponse.errorCode ==
                              static_cast<std::uint16_t>(AccountErrorCode::RequestPending) ||
                          secondResponse.errorCode ==
                              static_cast<std::uint16_t>(AccountErrorCode::UsernameAlreadyExists))
                   : (firstResponse.errorCode ==
                              static_cast<std::uint16_t>(AccountErrorCode::RequestPending) ||
                      firstResponse.errorCode ==
                          static_cast<std::uint16_t>(AccountErrorCode::UsernameAlreadyExists))));
    dup.Disconnect();
    client.Disconnect();
}

void RunDisconnectDuringDbCheck(TestServers& servers) {
    // ---- NetworkDisconnectDuringDbCheck（指令一百）----
    {
        AccountTestClient vanishing;
        Check("DisconnectDuringDbCheck: connect", vanishing.ConnectAndWait());
        vanishing.account.SendRegister("vanish_user", "GoodPass123");
        vanishing.client->Disconnect(false); // 立刻断线（DB 任务在途中）
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(800)); // DB 完成且结果丢弃
    // 服务器存活：后续客户端正常
    AccountTestClient survivor;
    Check("DisconnectDuringDbCheck: server survives, serves new client",
          survivor.ConnectAndWait());
    survivor.account.SendAccountLogin("vanish_user", "GoodPass123");
    NetworkEvent loginResponse;
    const bool got = survivor.WaitEvent(NetworkEvent::Type::AccountLoginResponse, loginResponse);
    Check("DisconnectDuringDbCheck: vanished register completed (login works)",
          got && loginResponse.success);
    Check("DisconnectDuringDbCheck: failure does not kill connection",
          survivor.client->State() == NetworkState::Ready);
    survivor.Disconnect();
}

void RunConcurrentChecks(TestServers& servers) {
    // ---- ConcurrentRegisterCheck（指令九十三）----
    {
        constexpr int kClients = 10;
        std::vector<std::unique_ptr<AccountTestClient>> clients(kClients);
        for (auto& client : clients) {
            client = std::make_unique<AccountTestClient>();
        }
        std::atomic<int> connected{0};
        std::atomic<int> registered{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < kClients; ++i) {
            threads.emplace_back([&, i] {
                auto& client = *clients[i];
                if (client.ConnectAndWait(10000)) {
                    ++connected;
                }
                client.account.SendRegister("conc_user_" + std::to_string(i), "ConcPass123");
                NetworkEvent response;
                if (client.WaitEvent(NetworkEvent::Type::RegisterResponse, response, 20000) &&
                    response.success) {
                    ++registered;
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        Check("ConcurrentRegisterCheck: 10 connected", connected == kClients);
        Check("ConcurrentRegisterCheck: 10 distinct usernames all registered",
              registered == kClients);
    }

    // ---- ConcurrentDuplicateRegisterCheck（指令九十四）----
    {
        constexpr int kClients = 10;
        std::vector<std::unique_ptr<AccountTestClient>> clients(kClients);
        for (auto& client : clients) {
            client = std::make_unique<AccountTestClient>();
        }
        std::atomic<int> successes{0};
        std::atomic<int> duplicates{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < kClients; ++i) {
            threads.emplace_back([&, i] {
                auto& client = *clients[i];
                // 慢机加固：10 并发连接 + 服务器冷启动下 10s 连接窗口曾超时导致
                // 计数缺口（CI Run 36944412540）——放宽等待，断言强度不变。
                client.ConnectAndWait(30000);
                client.account.SendRegister("dup_race_user", "DupRace123!");
                NetworkEvent response;
                if (client.WaitEvent(NetworkEvent::Type::RegisterResponse, response, 30000)) {
                    if (response.success) {
                        ++successes;
                    } else if (response.errorCode ==
                               static_cast<std::uint16_t>(AccountErrorCode::UsernameAlreadyExists)) {
                        ++duplicates;
                    }
                }
                client.Disconnect();
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        Check("ConcurrentDuplicateRegisterCheck: exactly 1 success",
              successes == 1 && duplicates == kClients - 1);
        Check("ConcurrentDuplicateRegisterCheck: db has exactly 1 row",
              QueryScalar(servers.dbPath, "SELECT COUNT(*) FROM accounts WHERE username='dup_race_user';") == 1);
    }

    // ---- ConcurrentCharacterCreateCheck（指令九十五/九十六）----
    {
        constexpr int kClients = 8;
        AccountTestClient owner;
        Check("ConcurrentCharacterCreateCheck: owner connect", owner.ConnectAndWait());
        owner.account.SendRegister("char_limit_owner", "LimitPass123");
        NetworkEvent reg;
        owner.WaitEvent(NetworkEvent::Type::RegisterResponse, reg);
        owner.account.SendAccountLogin("char_limit_owner", "LimitPass123");
        NetworkEvent login;
        owner.WaitEvent(NetworkEvent::Type::AccountLoginResponse, login);
        owner.Disconnect();

        std::vector<std::unique_ptr<AccountTestClient>> clients(kClients);
        for (auto& client : clients) {
            client = std::make_unique<AccountTestClient>();
        }
        std::atomic<int> created{0};
        std::atomic<int> limitReached{0};
        // 每线程先各自登录同一账号（多 Session 允许，指令三十四），再并发创建
        std::vector<std::thread> threads;
        std::atomic<int> readyToCreate{0};
        for (int i = 0; i < kClients; ++i) {
            threads.emplace_back([&, i] {
                auto& client = *clients[i];
                client.ConnectAndWait(10000);
                client.account.SendAccountLogin("char_limit_owner", "LimitPass123");
                NetworkEvent loginResponse;
                if (client.WaitEvent(NetworkEvent::Type::AccountLoginResponse, loginResponse,
                                     20000) &&
                    loginResponse.success) {
                    ++readyToCreate;
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        // 全部登录就绪后，同帧并发发起创建
        threads.clear();
        for (int i = 0; i < kClients; ++i) {
            threads.emplace_back([&, i] {
                auto& client = *clients[i];
                client.account.SendCreateCharacter(client.account.SessionToken(),
                                                   "Limit_Hero_" + std::to_string(i), 1, 1);
                NetworkEvent response;
                if (client.WaitEvent(NetworkEvent::Type::CharacterCreateResponse, response,
                                     20000)) {
                    if (response.success) {
                        ++created;
                    } else if (response.errorCode ==
                               static_cast<std::uint16_t>(AccountErrorCode::CharacterLimitReached)) {
                        ++limitReached;
                    }
                }
                client.Disconnect();
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        Check("ConcurrentCharacterCreateCheck: all logged in", readyToCreate == kClients);
        Check("ConcurrentCharacterCreateCheck: exactly 4 created, 4 limit-reached",
              created == 4 && limitReached == 4);
        // 复核：数据库中未删除角色数 == 4（事务保护，指令九十六）
        Check("ConcurrentCharacterCreateCheck: db has exactly 4 characters",
              QueryScalar(servers.dbPath,
                          "SELECT COUNT(*) FROM characters WHERE deleted=0 AND name LIKE "
                          "'Limit_Hero_%';") == 4);
    }
}

void RunRestartPersistenceCheck(TestServers& servers) {
    // ---- ServerRestartPersistenceCheck（指令九十九 / 一百一十四）----
    AccountTestClient first;
    Check("ServerRestartPersistenceCheck: connect", first.ConnectAndWait());
    first.account.SendRegister("restart_user", "Restart123!");
    NetworkEvent reg;
    Check("ServerRestartPersistenceCheck: register",
          first.WaitEvent(NetworkEvent::Type::RegisterResponse, reg) && reg.success);
    first.account.SendAccountLogin("restart_user", "Restart123!");
    NetworkEvent login;
    Check("ServerRestartPersistenceCheck: login",
          first.WaitEvent(NetworkEvent::Type::AccountLoginResponse, login) && login.success);
    first.account.SendCreateCharacter(first.account.SessionToken(), "RestartHero", 1, 1);
    NetworkEvent create;
    Check("ServerRestartPersistenceCheck: create",
          first.WaitEvent(NetworkEvent::Type::CharacterCreateResponse, create) && create.success);
    const std::string dbPath = servers.dbPath;
    first.Disconnect();
    servers.StopAll();

    // 重启（保留 SQLite），登录 -> 角色列表仍存在
    Check("ServerRestartPersistenceCheck: login server restarts", servers.RestartLogin(dbPath));
    Check("ServerRestartPersistenceCheck: gateway restarts", servers.StartGateway());
    AccountTestClient second;
    Check("ServerRestartPersistenceCheck: reconnect", second.ConnectAndWait());
    second.account.SendAccountLogin("restart_user", "Restart123!");
    NetworkEvent login2;
    Check("ServerRestartPersistenceCheck: login after restart",
          second.WaitEvent(NetworkEvent::Type::AccountLoginResponse, login2) && login2.success);
    second.account.SendCharacterList(second.account.SessionToken());
    NetworkEvent list;
    Check("ServerRestartPersistenceCheck: character list still has RestartHero",
          second.WaitEvent(NetworkEvent::Type::CharacterListResponse, list) && list.success &&
              list.characters.size() == 1 && list.characters[0].name == "RestartHero");
    second.Disconnect();
}

void RunLegacyLoginAndOfflineChecks(TestServers& servers) {
    // ---- 阶段9 Legacy LoginRequest 兼容（指令二十/一百零八）----
    {
        AccountTestClient legacy;
        Check("LegacyLoginCheck: connect", legacy.ConnectAndWait());
        legacy.client->SendLogin("test", "dev_token");
        NetworkEvent response;
        Check("LegacyLoginCheck: LegacyDevLogin still accepted",
              legacy.WaitEvent(NetworkEvent::Type::LoginResponse, response) &&
                  response.loginSuccess);
        legacy.Disconnect();
    }

    // ---- 客户端离线容错（指令一百零九/一百一十）----
    {
        AccountTestClient offline;
        // 不连接：状态 Disconnected；UpdateHeartbeat / Send* 均安全 no-op
        offline.client->UpdateHeartbeat(0.016f);
        offline.account.SendRegister("never_sent", "NeverSent1");
        offline.account.SendAccountLogin("never_sent", "NeverSent1");
        Check("OfflineToleranceCheck: disconnected state, no crash, no state change",
              offline.client->State() == NetworkState::Disconnected &&
                  offline.account.State() == AccountFlowState::Disconnected);
    }
}

void RunPasswordNeverLoggedCheck(TestServers& servers) {
    // ---- PasswordNeverLoggedCheck（指令七十三）----
    const std::string password = "SuperSecret123!";
    AccountTestClient client;
    Check("PasswordNeverLoggedCheck: connect", client.ConnectAndWait());
    client.account.SendRegister("logcheck_user", password);
    NetworkEvent reg;
    client.WaitEvent(NetworkEvent::Type::RegisterResponse, reg);
    client.account.SendAccountLogin("logcheck_user", password);
    NetworkEvent login;
    const bool loginOk = client.WaitEvent(NetworkEvent::Type::AccountLoginResponse, login);
    client.account.SendCreateCharacter(client.account.SessionToken(), "LogHero", 1, 1);
    NetworkEvent create;
    client.WaitEvent(NetworkEvent::Type::CharacterCreateResponse, create);
    CharacterSelectionController selection;
    selection.RequestSelect(client.account, create.character.characterId);
    NetworkEvent select;
    client.WaitEvent(NetworkEvent::Type::CharacterSelectResponse, select);
    client.Disconnect();

    legend::debug::Logger::Shutdown();
    std::ifstream logFile("testlogs/latest.log");
    std::stringstream buffer;
    buffer << logFile.rdbuf();
    const std::string content = buffer.str();
    // 日志绝不含 password / sessionToken / selectionTicket（指令六十九）
    Check("PasswordNeverLoggedCheck: log contains no password/token/ticket",
          loginOk && !select.selectionTicket.empty() && content.find(password) == std::string::npos &&
              content.find(login.sessionToken) == std::string::npos &&
              content.find(select.selectionTicket) == std::string::npos);
}

} // namespace

int main() {
    std::setvbuf(stdout, nullptr, _IONBF, 0); // 崩溃诊断：立即输出 PASS/FAIL
    // 日志进独立目录（PasswordNeverLoggedCheck 断言敏感字段不落盘；指令七十三）
    legend::debug::Logger::Init("testlogs");
    std::printf("[AccountTest] LegendAccountTests begin\n");

    RunSchemaAndCryptoChecks();
    RunAccountServiceChecks();
    RunCharacterServiceChecks();
    RunSchemaHardeningChecks();
    RunDbWorkerChecks();
    g_failures += RunDbServerChecks();
    g_failures += RunCharacterServerChecks();
    g_failures += RunLogServerChecks();
    g_failures += RunPersistenceRpcChecks();

    {
        TestServers servers;
        const std::string dbPath = TempDbPath("network_chain");
        Check("LoginStartCheck", servers.StartLogin(dbPath));
        Check("GatewayStartCheck", servers.StartGateway());

        RunAccountFlowChainCheck(servers);
        RunMalformedPacketCheck(servers);
        RunRequestCorrelationAndDuplicateChecks(servers);
        RunDisconnectDuringDbCheck(servers);
        RunConcurrentChecks(servers);
        RunLegacyLoginAndOfflineChecks(servers);
        RunPasswordNeverLoggedCheck(servers);
        RunRestartPersistenceCheck(servers); // 最后（会重启服务器）
        servers.StopAll();
    }

    // 临时测试数据库清理（指令一百零三：测试结束删除）
    std::error_code ec;
    std::filesystem::remove_all(TempDir(), ec);

    std::printf("[AccountTest] completed, failures = %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
