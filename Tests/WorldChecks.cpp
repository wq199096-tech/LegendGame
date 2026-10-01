// ---------------------------------------------------------------------------
// LegendWorldTests：阶段11 World / Character Handoff 验收（指令八十二）
// 输出：[WorldTest] completed, failures = 0（指令一百一十一）
// 阶段12 指令七十四：共享测试基建拆至 Tests/WorldTestHarness.h（namespace worldtest）；
// AOI 检查拆至 Tests/WorldAoiChecks.cpp（仍链接 LegendWorldTests 单一 exe）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

using namespace worldtest;

// 阶段12 指令七十四：AOI 检查入口（WorldAoiChecks.cpp，自带 servers 生命周期）。
// 阶段13 指令七十二：怪物检查入口（WorldMonsterChecks.cpp，自带 servers 生命周期）。
// 阶段14 指令九十六：战斗检查入口（WorldCombatChecks.cpp，自带 servers 生命周期）。
// 阶段15 指令九十九：技能检查入口（WorldSkillChecks.cpp，自带 servers 生命周期）。
// 阶段16 指令八十四：状态效果检查入口（WorldStatusChecks.cpp，自带 servers 生命周期）。
// 阶段17 指令三十二：成长/奖励/重生检查入口（WorldProgressionChecks.cpp）。
// 阶段18：掉落/背包/装备检查入口（WorldInventoryChecks.cpp）。
namespace worldtest {
void RunWorldAoiChecks();
void RunWorldMonsterChecks();
void RunWorldCombatChecks();
void RunWorldSkillChecks();
void RunWorldStatusChecks();
void RunWorldProgressionChecks();
void RunWorldInventoryChecks();
void RunWorldQuestChecks();
void RunWorldNpcChecks();
void RunWorldMapChecks(); // 阶段21：多地图 / Portal / 复活
void RunWorldDataChecks(); // 阶段22：Data/World 数据层
void RunMapEditorDataChecks(); // 阶段22：World Editor 文档模型
void RunGameDataChecks(); // 阶段23：Data/Game 数据层 + 迁移回归
void RunDefinitionValidationChecks(); // 阶段23：定义校验 + GameDataDocument
void RunAssetManifestChecks(); // 阶段24：asset_manifest 数据层
void RunAnimationChecks(); // 阶段24：animations + 统一 AnimationPlayer
void RunVisualDefinitionChecks(); // 阶段24：visual_entities/effects/visual_maps + Catalog
void RunClientSmokeChecks(); // 阶段24：LegendClient.exe 真实启动冒烟（15s）
void RunVerticalSliceChecks(); // 阶段25：Vertical Slice 检查（新角色出生/Chapter One 链路）
void RunUiModelChecks(); // 阶段25：UI 模型检查（指令六十七）
void RunChapterOneChecks(); // 阶段25：Chapter One 端到端（真实服务器链路）
void RunFullServerTopologyCheck(); // Stage25.5: config/topology contract
int RunLeaveWorldChecks(WorldTestServers& servers); // Stage26 指令十七：主动离开世界
int RunStage26FlowChecks(WorldTestServers& servers); // Stage26 指令三十六：玩家流程 E2E
}

namespace {

void RunWorldProtocolChecks() {
    // ---- WorldProtocolCheck（指令八十四/六十三）：Encode/Decode 完整消费 ----
    {
        std::string error;
        world::EnterWorldRequestPayload req;
        req.requestId = 42;
        req.selectionTicket = std::string(64, 'a');
        std::vector<std::uint8_t> payload;
        bool ok = world::EncodeEnterWorldRequest(req, payload);
        world::EnterWorldRequestPayload decoded;
        ok = ok && world::DecodeEnterWorldRequest(payload.data(), payload.size(), decoded, error) &&
             decoded.requestId == 42 && decoded.selectionTicket == req.selectionTicket;
        // trailing bytes = malformed
        payload.push_back(0xFF);
        ok = ok && !world::DecodeEnterWorldRequest(payload.data(), payload.size(), decoded, error);

        world::EnterWorldResponsePayload resp;
        resp.requestId = 7;
        resp.success = true;
        resp.accountId = 11;
        resp.characterId = 22;
        resp.characterName = "Hero";
        resp.classId = 1;
        resp.gender = 2;
        resp.level = 3;
        resp.mapId = 1;
        resp.positionX = 12.5f;
        resp.positionY = -4.0f;
        resp.serverTime = 999;
        std::vector<std::uint8_t> respPayload;
        ok = ok && world::EncodeEnterWorldResponse(resp, respPayload);
        world::EnterWorldResponsePayload respDecoded;
        ok = ok && world::DecodeEnterWorldResponse(respPayload.data(), respPayload.size(),
                                                   respDecoded, error) &&
             respDecoded.characterName == "Hero" && respDecoded.level == 3 &&
             respDecoded.positionX == 12.5f && respDecoded.positionY == -4.0f;

        world::PlayerMoveInputPayload move;
        move.inputSequence = 9;
        move.directionX = 1.0f;
        move.directionY = 0.0f;
        move.deltaTime = 0.1f;
        std::vector<std::uint8_t> movePayload;
        ok = ok && world::EncodePlayerMoveInput(move, movePayload);
        world::PlayerMoveInputPayload moveDecoded;
        ok = ok && world::DecodePlayerMoveInput(movePayload.data(), movePayload.size(),
                                                moveDecoded, error) &&
             moveDecoded.inputSequence == 9 && moveDecoded.directionX == 1.0f;

        world::ConsumeSelectionTicketResponsePayload consume;
        consume.requestId = 5;
        consume.success = true;
        consume.accountId = 1;
        consume.characterId = 2;
        std::vector<std::uint8_t> consumePayload;
        ok = ok && world::EncodeConsumeSelectionTicketResponse(consume, consumePayload);
        world::ConsumeSelectionTicketResponsePayload consumeDecoded;
        ok = ok && world::DecodeConsumeSelectionTicketResponse(consumePayload.data(),
                                                               consumePayload.size(),
                                                               consumeDecoded, error) &&
             consumeDecoded.accountId == 1 && consumeDecoded.characterId == 2;
        Check("WorldProtocolCheck: encode/decode roundtrip + trailing bytes rejected", ok);
    }
}

// ===========================================================================
// 握手
// ===========================================================================

void RunWorldHandshakeChecks(WorldTestServers& servers) {
    // ---- WorldHandshakeCheck（指令八十五）----
    {
        WorldTestClient client;
        client.controller.EnterWorldWithTicket(std::string(64, 'f')); // 假 ticket，只测握手
        // 断言：收到 WorldServerHello accepted=true（假 ticket 的 EnterWorld
        // 失败是预期路径，不影响握手判定）。
        const bool ok = client.WaitEvent(WorldNetworkEvent::Type::HandshakeSuccess, 5000);
        Check("WorldHandshakeCheck: ServerHello accepted", ok);
        client.Disconnect();
        WaitUntil([&] { client.DrainEvents(); return true; }, 200);
    }

    // ---- WorldBadVersionCheck（指令八十六）：version=999 -> accepted=false ----
    {
        asio::io_context io;
        asio::ip::tcp::socket socket(io);
        std::error_code ec;
        socket.connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), kWorldPort),
                       ec);
        bool ok = !ec;
        if (ok) {
            network::ClientHelloPayload hello;
            hello.protocolVersion = 999;
            hello.clientBuild = "0.11.0";
            hello.clientName = "BadVersion";
            std::vector<std::uint8_t> payload;
            network::EncodeClientHello(hello, payload);
            network::Packet out;
            out.header.messageId = static_cast<std::uint16_t>(network::MessageId::WorldClientHello);
            out.payload = std::move(payload);
            std::vector<std::uint8_t> frame;
            network::PacketCodec::EncodePacket(out, frame);
            asio::write(socket, asio::buffer(frame), ec);
            // ServerHello accepted=false
            std::array<std::uint8_t, 16> header{};
            asio::read(socket, asio::buffer(header), ec);
            network::PacketHeader decoded;
            std::string headerError;
            ok = ok && network::PacketCodec::DecodeHeader(header.data(), header.size(), decoded,
                                                          headerError);
            if (ok) {
                std::vector<std::uint8_t> responsePayload(decoded.payloadSize);
                asio::read(socket, asio::buffer(responsePayload), ec);
                network::ServerHelloPayload serverHello;
                std::string decodeError;
                ok = network::DecodeServerHello(responsePayload.data(), responsePayload.size(),
                                                serverHello, decodeError) &&
                     !serverHello.accepted;
            }
            // CloseAfterFlush：连接随后被关闭（读到 EOF）
            std::array<std::uint8_t, 1> extra{};
            auto bytesRead = asio::read(socket, asio::buffer(extra), ec);
            ok = ok && (ec || bytesRead == 0);
        }
        Check("WorldBadVersionCheck: version 999 rejected (accepted=false, closed)", ok);
    }
}

// ===========================================================================
// EnterWorld / Ticket
// ===========================================================================

void RunEnterWorldChecks(WorldTestServers& servers) {
    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    Check("EnterWorldChecks: db ready", dbOk);
    if (!dbOk) {
        return;
    }
    AccountService accounts(5, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);

    // ---- TicketConsumeSuccessCheck（指令八十七）----
    CharacterSeed hero;
    bool seeded = SeedAccountAndCharacter(db, accounts, characters, "world_user_1", "WorldHero", hero);
    std::string ticket;
    if (seeded && servers.login) {
        ticket = servers.login->Tickets().Create(hero.accountId, hero.characterId, 60.0);
    }
    {
        WorldTestClient client;
        const bool ok = seeded && !ticket.empty() && client.ConnectAndEnter(ticket, 8000);
        Check("TicketConsumeSuccessCheck: enter world success, characterId matches",
              ok && client.controller.CharacterId() == hero.characterId &&
                  client.controller.IsWorldReady());
        client.Disconnect();
        WaitUntil([&] { client.DrainEvents(); return true; }, 200);
    }

    // ---- TicketReplayCheck（指令八十八）：同一 ticket 第二次失败 ----
    {
        std::string replayTicket;
        if (seeded && servers.login) {
            replayTicket = servers.login->Tickets().Create(hero.accountId, hero.characterId, 60.0);
        }
        WorldTestClient first;
        WorldTestClient second;
        const bool firstOk = !replayTicket.empty() && first.ConnectAndEnter(replayTicket, 8000);
        const bool secondOk = second.ConnectAndEnter(replayTicket, 8000);
        const WorldNetworkEvent& failEvent = second.LastEnterFailed();
        Check("TicketReplayCheck: first success, replay -> InvalidTicket",
              firstOk && !secondOk &&
                  failEvent.errorCode ==
                      static_cast<std::uint16_t>(world::WorldErrorCode::InvalidTicket));
        first.Disconnect();
        WaitUntil([&] { first.DrainEvents(); return true; }, 200);
    }

    // ---- ExpiredTicketCheck（指令八十九）----
    {
        std::string expiredTicket;
        if (servers.login) {
            expiredTicket = servers.login->Tickets().Create(hero.accountId, hero.characterId, 0.05);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        WorldTestClient client;
        const bool entered = client.ConnectAndEnter(expiredTicket, 8000);
        const WorldNetworkEvent& failEvent = client.LastEnterFailed();
        Check("ExpiredTicketCheck: expired ticket -> InvalidTicket to client",
              !entered &&
                  failEvent.errorCode ==
                      static_cast<std::uint16_t>(world::WorldErrorCode::InvalidTicket));
    }

    // ---- InvalidTicketCheck（指令九十）----
    {
        WorldTestClient client;
        const std::string randomToken = account::GenerateTokenHex(32);
        const bool entered = client.ConnectAndEnter(randomToken, 8000);
        const WorldNetworkEvent& failEvent = client.LastEnterFailed();
        Check("InvalidTicketCheck: random token -> InvalidTicket",
              !entered &&
                  failEvent.errorCode ==
                      static_cast<std::uint16_t>(world::WorldErrorCode::InvalidTicket));
    }

    // ---- CharacterLoadCheck（指令九十一）：DB 数据与响应一致 ----
    {
        CharacterSeed seeded2;
        bool ok = SeedAccountAndCharacter(db, accounts, characters, "world_user_2", "LoadedHero",
                                          seeded2);
        // 预置位置 (100, 150)
        if (ok) {
            auto updated = account::CharacterRepository::UpdateWorldPosition(
                db, seeded2.characterId, 1, 100.0f, 150.0f, account::UnixNow());
            ok = updated.success;
        }
        std::string loadTicket;
        if (ok && servers.login) {
            loadTicket = servers.login->Tickets().Create(seeded2.accountId, seeded2.characterId, 60.0);
        }
        WorldTestClient client;
        const bool entered = !loadTicket.empty() && client.ConnectAndEnter(loadTicket, 8000);
        const WorldNetworkEvent& enterEvent = client.LastEnterSuccess();
        Check("CharacterLoadCheck: DB level/map/position match EnterWorldResponse",
              entered && enterEvent.mapId == 1 && enterEvent.level == 1 &&
                  std::fabs(enterEvent.positionX - 100.0f) < 0.01f &&
                  std::fabs(enterEvent.positionY - 150.0f) < 0.01f &&
                  enterEvent.characterName == "LoadedHero");
        client.Disconnect();
        WaitUntil([&] { client.DrainEvents(); return true; }, 200);
    }

    // ---- CharacterAlreadyOnlineCheck（指令九十二/一百零七）----
    {
        std::string ticketA;
        std::string ticketB;
        if (servers.login) {
            ticketA = servers.login->Tickets().Create(hero.accountId, hero.characterId, 60.0);
            ticketB = servers.login->Tickets().Create(hero.accountId, hero.characterId, 60.0);
        }
        WorldTestClient clientA;
        WorldTestClient clientB;
        const bool aOk = !ticketA.empty() && clientA.ConnectAndEnter(ticketA, 8000);
        const bool bOk = !ticketB.empty() && clientB.ConnectAndEnter(ticketB, 8000);
        const WorldNetworkEvent& failEvent = clientB.LastEnterFailed();
        Check("CharacterAlreadyOnlineCheck: duplicate character -> rejected",
              aOk && !bOk &&
                  failEvent.errorCode ==
                      static_cast<std::uint16_t>(world::WorldErrorCode::CharacterAlreadyOnline));
        clientA.Disconnect();
        WaitUntil([&] { clientA.DrainEvents(); return true; }, 300);
    }

    db.Close();
}

// ===========================================================================
// 移动 / 快照
// ===========================================================================

void RunMovementChecks(WorldTestServers& servers) {
    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(5, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("MovementChecks: db ready", false);
        return;
    }
    CharacterSeed mover;
    bool seeded = SeedAccountAndCharacter(db, accounts, characters, "move_user", "Mover", mover);
    std::string ticket;
    if (seeded && servers.login) {
        ticket = servers.login->Tickets().Create(mover.accountId, mover.characterId, 60.0);
    }
    WorldTestClient client;
    const bool entered = !ticket.empty() && client.ConnectAndEnter(ticket, 8000);
    Check("MovementChecks: entered world", entered);
    if (!entered) {
        db.Close();
        return;
    }
    // 清掉进入时的首个快照
    client.WaitEvent(WorldNetworkEvent::Type::PositionSnapshot, 500);

    // ---- MoveInputCheck（指令九十三）：(1,0) dt=0.1 speed=120 -> ~12 ----
    client.client().SendMoveInput(1, 1.0f, 0.0f, 0.1f);
    WorldNetworkEvent snap;
    bool ok = client.WaitForSnapshotAfterSeq(1, snap);
    ok = ok && snap.lastProcessedInputSequence == 1 &&
         std::fabs(snap.positionX - 12.0f) < 0.6f && std::fabs(snap.positionY) < 0.01f;
    Check("MoveInputCheck: (1,0) dt=0.1 -> x ~= 12, seq echoed", ok);

    // ---- MoveNormalizeCheck（指令九十四）：(1,1) 对角线不加速 ----
    client.client().SendMoveInput(2, 1.0f, 1.0f, 0.1f);
    ok = client.WaitForSnapshotAfterSeq(2, snap);
    ok = ok && snap.lastProcessedInputSequence == 2 &&
         std::fabs(snap.positionX - (12.0f + 12.0f / std::sqrt(2.0f))) < 0.6f &&
         std::fabs(snap.positionY - 12.0f / std::sqrt(2.0f)) < 0.6f;
    Check("MoveNormalizeCheck: (1,1) normalized, ~8.49 per axis", ok);

    // ---- MoveDeltaClampCheck（指令九十五）：dt=10 按 0.1 计算 ----
    client.client().SendMoveInput(3, 1.0f, 0.0f, 10.0f);
    ok = client.WaitForSnapshotAfterSeq(3, snap);
    ok = ok && snap.lastProcessedInputSequence == 3 &&
         std::fabs(snap.positionX - (12.0f + 12.0f / std::sqrt(2.0f) + 12.0f)) < 0.6f;
    Check("MoveDeltaClampCheck: dt=10 clamped to 0.1 (+12)", ok);

    // ---- MoveSequenceCheck（指令九十六）：sequence 10,11,10 -> 第三个忽略 ----
    const float xBefore = snap.positionX;
    client.client().SendMoveInput(10, 1.0f, 0.0f, 0.1f); // +12
    client.client().SendMoveInput(11, 1.0f, 0.0f, 0.1f); // +12
    client.client().SendMoveInput(10, 1.0f, 0.0f, 0.1f); // 倒退 -> 忽略
    ok = client.WaitForSnapshotAfterSeq(11, snap);
    ok = ok && snap.lastProcessedInputSequence == 11 &&
         std::fabs(snap.positionX - (xBefore + 24.0f)) < 0.6f;
    Check("MoveSequenceCheck: seq 10,11,10 -> third ignored (last=11)", ok);

    // ---- PositionBoundsCheck（指令九十七）：边界 Clamp 0~2000 ----
    client.Disconnect();
    WaitUntil([&] { client.DrainEvents(); return true; }, 300);
    auto updated = account::CharacterRepository::UpdateWorldPosition(
        db, mover.characterId, 1, 1995.0f, 500.0f, account::UnixNow());
    std::string boundsTicket;
    if (servers.login) {
        boundsTicket = servers.login->Tickets().Create(mover.accountId, mover.characterId, 60.0);
    }
    WorldTestClient client2;
    const bool entered2 = !boundsTicket.empty() && client2.ConnectAndEnter(boundsTicket, 8000);
    client2.WaitEvent(WorldNetworkEvent::Type::PositionSnapshot, 500);
    client2.client().SendMoveInput(1, 1.0f, 0.0f, 0.1f); // 1995+12 -> 2007 -> clamp 2000
    client2.client().SendMoveInput(2, 1.0f, 0.0f, 0.1f);
    ok = updated.success && entered2;
    WorldNetworkEvent boundsSnap;
    ok = ok && client2.WaitForSnapshotAfterSeq(2, boundsSnap);
    ok = ok && boundsSnap.positionX <= world::kMapMaxX + 0.01f &&
         boundsSnap.positionX >= world::kMapMinX;
    Check("PositionBoundsCheck: x clamped to 2000", ok && boundsSnap.positionX <= 2000.01f);
    client2.Disconnect();
    WaitUntil([&] { client2.DrainEvents(); return true; }, 300);
    db.Close();
}

// ===========================================================================
// 保存
// ===========================================================================

void RunSaveChecks(WorldTestServers& servers) {
    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(5, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("SaveChecks: db ready", false);
        return;
    }

    // ---- DisconnectSaveCheck（指令九十九）----
    {
        CharacterSeed seed;
        bool ok = SeedAccountAndCharacter(db, accounts, characters, "save_user_1", "Saver1", seed);
        std::string ticket;
        if (ok && servers.login) {
            ticket = servers.login->Tickets().Create(seed.accountId, seed.characterId, 60.0);
        }
        WorldTestClient client;
        ok = ok && !ticket.empty() && client.ConnectAndEnter(ticket, 8000);
        client.WaitEvent(WorldNetworkEvent::Type::PositionSnapshot, 500);
        client.client().SendMoveInput(1, 1.0f, 0.0f, 0.1f); // x=12
        WorldNetworkEvent snap;
        ok = ok && client.WaitForSnapshotAfterSeq(1, snap);
        client.Disconnect(); // 指令九十九：断开保存
        ok = ok && WaitUntil(
                       [&] {
                           return std::fabs(QueryScalarDouble(
                                      servers.dbPath,
                                      "SELECT position_x FROM characters WHERE id=" +
                                          std::to_string(seed.characterId) + ";") -
                                  12.0) < 0.01;
                       },
                       3000);
        Check("DisconnectSaveCheck: DB position updated after disconnect", ok);
    }

    // ---- PeriodicSaveCheck（指令一百）：0.5s 周期保存 ----
    {
        CharacterSeed seed;
        bool ok = SeedAccountAndCharacter(db, accounts, characters, "save_user_2", "Saver2", seed);
        std::string ticket;
        if (ok && servers.login) {
            ticket = servers.login->Tickets().Create(seed.accountId, seed.characterId, 60.0);
        }
        WorldTestClient client;
        ok = ok && !ticket.empty() && client.ConnectAndEnter(ticket, 8000);
        client.WaitEvent(WorldNetworkEvent::Type::PositionSnapshot, 500);
        client.client().SendMoveInput(1, 0.0f, 1.0f, 0.1f); // y=12
        WorldNetworkEvent snap;
        ok = ok && client.WaitForSnapshotAfterSeq(1, snap);
        // 不断线：等周期保存（0.5s）
        ok = ok && WaitUntil(
                       [&] {
                           return std::fabs(QueryScalarDouble(
                                      servers.dbPath,
                                      "SELECT position_y FROM characters WHERE id=" +
                                          std::to_string(seed.characterId) + ";") -
                                  12.0) < 0.01;
                       },
                       3000);
        Check("PeriodicSaveCheck: DB position updated by periodic save", ok);
        client.Disconnect();
        WaitUntil([&] { client.DrainEvents(); return true; }, 300);
    }

    // ---- ServerShutdownSaveCheck（指令一百零一）：Stop 后位置保存 ----
    {
        CharacterSeed seed;
        bool ok = SeedAccountAndCharacter(db, accounts, characters, "save_user_3", "Saver3", seed);
        std::string ticket;
        if (ok && servers.login) {
            ticket = servers.login->Tickets().Create(seed.accountId, seed.characterId, 60.0);
        }
        WorldTestClient client;
        ok = ok && !ticket.empty() && client.ConnectAndEnter(ticket, 8000);
        client.WaitEvent(WorldNetworkEvent::Type::PositionSnapshot, 500);
        client.client().SendMoveInput(1, -1.0f, 0.0f, 0.1f); // x=-12 -> clamp 0
        WorldNetworkEvent snap;
        ok = ok && client.WaitForSnapshotAfterSeq(1, snap);
        servers.StopWorld(); // Flush Save -> DB Worker Flush -> Close DB
        ok = ok && std::fabs(QueryScalarDouble(
                            servers.dbPath,
                            "SELECT position_x FROM characters WHERE id=" +
                                std::to_string(seed.characterId) + ";") -
                        0.0) < 0.01;
        Check("ServerShutdownSaveCheck: final position saved on world stop", ok);
        // 重启 world 供后续测试
        ok = servers.StartWorld();
        Check("ServerShutdownSaveCheck: world restarts", ok);
    }

    db.Close();
}

// ===========================================================================
// Login 链路
// ===========================================================================

void RunLoginLinkChecks(WorldTestServers& servers) {
    // ---- LoginLinkUnavailableCheck（指令一百零二）----
    {
        // 当前 world 无 login（确保先停 login）
        servers.StopLogin();
        Database db;
        std::string error;
        const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
        AccountService accounts(5, 60);
        CharacterService characters(account::kMaxCharactersPerAccount);
        CharacterSeed seed;
        bool ok = dbOk && SeedAccountAndCharacter(db, accounts, characters, "link_user", "Linker",
                                                  seed);
        std::string ticket;
        if (ok) {
            ticket = account::GenerateTokenHex(32); // 无 login：随便一个 ticket
        }
        WorldTestClient client;
        const bool entered = !ticket.empty() && client.ConnectAndEnter(ticket, 8000);
        const WorldNetworkEvent& failEvent = client.LastEnterFailed();
        ok = ok && !entered &&
             failEvent.errorCode ==
                 static_cast<std::uint16_t>(world::WorldErrorCode::ServiceUnavailable) &&
             servers.world != nullptr; // WorldServer 不 Crash，仍监听
        Check("LoginLinkUnavailableCheck: enter -> ServiceUnavailable, world alive", ok);
        client.Disconnect();
        db.Close();
    }

    // ---- LoginReconnectCheck（指令一百零三）：Login 后启动 -> LoginLinkReady ----
    {
        Check("LoginReconnectCheck: login starts later", servers.StartLogin());
        const bool ready = WaitUntil([&] { return servers.world && servers.world->IsLoginConnected(); },
                                     5000);
        Check("LoginReconnectCheck: world -> LoginLinkReady", ready);
    }

    // ---- LoginRestartCheck（指令一百零四）：玩家在线期间 Login 重启 ----
    {
        Database db;
        std::string error;
        const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
        AccountService accounts(5, 60);
        CharacterService characters(account::kMaxCharactersPerAccount);
        CharacterSeed seed;
        bool ok = dbOk && SeedAccountAndCharacter(db, accounts, characters, "restart_user",
                                                  "RestartLink", seed);
        std::string ticket;
        if (ok && servers.login) {
            ticket = servers.login->Tickets().Create(seed.accountId, seed.characterId, 60.0);
        }
        WorldTestClient client;
        ok = ok && !ticket.empty() && client.ConnectAndEnter(ticket, 8000);
        ok = ok && WaitUntil([&] { client.DrainEvents(); return client.controller.IsWorldReady(); },
                             3000);
        servers.StopLogin(); // 玩家保持在线
        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        bool stillAlive = false;
        {
            WorldNetworkEvent snap;
            stillAlive = client.PeekEvent(WorldNetworkEvent::Type::PositionSnapshot, snap, 1500);
        }
        Check("LoginRestartCheck: player stays online while login down", ok && stillAlive);
        Check("LoginRestartCheck: login restarts", servers.StartLogin());
        const bool relinked =
            WaitUntil([&] { return servers.world && servers.world->IsLoginConnected(); }, 5000);
        Check("LoginRestartCheck: world re-handshakes automatically", relinked);
        client.Disconnect();
        db.Close();
    }
}

// ===========================================================================
// 鲁棒性
// ===========================================================================

void RunRobustnessChecks(WorldTestServers& servers) {
    Database db;
    std::string error;
    const bool dbOk = db.Open(servers.dbPath, error) && InitializeSchema(db, error);
    AccountService accounts(5, 60);
    CharacterService characters(account::kMaxCharactersPerAccount);
    if (!dbOk) {
        Check("RobustnessChecks: db ready", false);
        return;
    }

    // ---- ClientDisconnectDuringTicketValidationCheck（指令一百零五）----
    {
        CharacterSeed seed;
        bool ok = SeedAccountAndCharacter(db, accounts, characters, "robust_user_1",
                                          "Vanisher", seed);
        std::string ticket;
        if (ok && servers.login) {
            ticket = servers.login->Tickets().Create(seed.accountId, seed.characterId, 60.0);
        }
        WorldTestClient client;
        ok = ok && !ticket.empty();
        if (ok) {
            client.controller.EnterWorldWithTicket(ticket);
            // 等连接建立（若 Disconnect 先于 async connect 完成，FIN 不会发出）
            WaitUntil(
                [&] {
                    client.DrainEvents();
                    return client.controller.State() != WorldFlowState::Connecting;
                },
                2000);
            client.client().Disconnect(false); // 立即断（验证进行中）
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(600));
        Check("ClientDisconnectDuringTicketValidationCheck: world survives, no crash",
              ok && servers.world != nullptr && servers.world->PlayerCount() == 0);
    }

    // ---- MalformedWorldPacketCheck（指令一百零八）----
    {
        asio::io_context io;
        asio::ip::tcp::socket socket(io);
        std::error_code ec;
        socket.connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), kWorldPort),
                       ec);
        bool ok = !ec;
        if (ok) {
            network::ClientHelloPayload hello;
            hello.protocolVersion = world::kWorldProtocolVersion;
            hello.clientBuild = "0.11.0";
            hello.clientName = "Malformed";
            std::vector<std::uint8_t> payload;
            network::EncodeClientHello(hello, payload);
            network::Packet helloPacket;
            helloPacket.header.messageId =
                static_cast<std::uint16_t>(network::MessageId::WorldClientHello);
            helloPacket.payload = std::move(payload);
            std::vector<std::uint8_t> frame;
            network::PacketCodec::EncodePacket(helloPacket, frame);
            asio::write(socket, asio::buffer(frame), ec);
            std::array<std::uint8_t, 16> header{};
            asio::read(socket, asio::buffer(header), ec);
            network::PacketHeader decoded;
            std::string headerError;
            ok = !ec && network::PacketCodec::DecodeHeader(header.data(), header.size(), decoded,
                                                           headerError);
            if (ok && decoded.payloadSize > 0) {
                std::vector<std::uint8_t> responsePayload(decoded.payloadSize);
                asio::read(socket, asio::buffer(responsePayload), ec);
                ok = !ec;
            }
            // 畸形 EnterWorldRequest：requestId + 截断 ticket 长度前缀
            network::Packet bad;
            bad.header.messageId = static_cast<std::uint16_t>(network::MessageId::EnterWorldRequest);
            {
                network::ByteWriter w(bad.payload);
                w.WriteUInt64(1);
                w.WriteUInt16(60000); // 非法长度
            }
            frame.clear();
            network::PacketCodec::EncodePacket(bad, frame);
            asio::write(socket, asio::buffer(frame), ec);
            // 只断当前 Client：读 EOF
            std::array<std::uint8_t, 1> extra{};
            asio::read(socket, asio::buffer(extra), ec);
            ok = ok && ec; // EOF/复位
        }
        // WorldServer 继续服务：正常客户端进入成功
        CharacterSeed seed;
        bool seeded = SeedAccountAndCharacter(db, accounts, characters, "robust_user_2",
                                              "AfterMalf", seed);
        std::string ticket;
        if (seeded && servers.login) {
            ticket = servers.login->Tickets().Create(seed.accountId, seed.characterId, 60.0);
        }
        WorldTestClient good;
        const bool goodOk = !ticket.empty() && good.ConnectAndEnter(ticket, 8000);
        Check("MalformedWorldPacketCheck: only current client dropped, world serves on",
              ok && seeded && goodOk && good.LastEnterSuccess().characterName == "AfterMalf");
        good.Disconnect();
        WaitUntil([&] { good.DrainEvents(); return true; }, 300);
    }

    // ---- MultiWorldClientCheck（指令一百零六）：10 角色 10 Client 同时进入 ----
    {
        constexpr int kClients = 10;
        std::vector<CharacterSeed> seeds(kClients);
        std::vector<std::string> tickets(kClients);
        bool seeded = true;
        for (int i = 0; i < kClients; ++i) {
            seeded = seeded && SeedAccountAndCharacter(db, accounts, characters,
                                                       "multi_user_" + std::to_string(i),
                                                       "MultiHero" + std::to_string(i), seeds[i]);
            if (seeded && servers.login) {
                tickets[i] = servers.login->Tickets().Create(seeds[i].accountId,
                                                             seeds[i].characterId, 60.0);
            }
        }
        std::vector<std::unique_ptr<WorldTestClient>> clients(kClients);
        for (auto& client : clients) {
            client = std::make_unique<WorldTestClient>();
        }
        std::atomic<int> ready{0};
        std::vector<std::thread> threads;
        for (int i = 0; i < kClients; ++i) {
            threads.emplace_back([&clients, &tickets, &ready, i] {
                auto& client = *clients[i];
                client.controller.EnterWorldWithTicket(tickets[i]);
                if (WaitUntil(
                        [&] {
                            client.DrainEvents();
                            return client.controller.IsWorldReady() ||
                                   client.controller.State() == WorldFlowState::Failed;
                        },
                        15000) &&
                    client.controller.IsWorldReady()) {
                    ++ready;
                }
            });
        }
        for (auto& thread : threads) {
            thread.join();
        }
        Check("MultiWorldClientCheck: 10 clients all WorldReady", seeded && ready == kClients);
        for (auto& client : clients) {
            client->Disconnect();
        }
        WaitUntil([&] { return true; }, 500);
    }

    db.Close();
}

// ===========================================================================
// 验收主链（指令一百一十八）：注册 -> 登录 -> 选角 -> 进世界 -> 移动 -> 断开保存
// -> 重登 -> 新 ticket -> 位置恢复
// ===========================================================================

void RunWorldFullChainCheck(WorldTestServers& servers) {
    servers.StartGateway();
    LoginTestClient loginClient;
    bool ok = loginClient.ConnectAndWait();
    // 注册 + 登录
    loginClient.account.SendRegister("chain_user", "ChainPass123!");
    ok = ok && WaitUntil(
                   [&] {
                       loginClient.DrainEvents();
                       return loginClient.account.State() == AccountFlowState::Unauthenticated;
                   },
                   8000);
    loginClient.account.SendAccountLogin("chain_user", "ChainPass123!");
    ok = ok && WaitUntil(
                   [&] {
                       loginClient.DrainEvents();
                       return loginClient.account.State() == AccountFlowState::Authenticated;
                   },
                   8000);
    // 创建角色 + 选择
    loginClient.account.SendCreateCharacter(loginClient.account.SessionToken(), "ChainHero", 1, 1);
    ok = ok && WaitUntil(
                   [&] {
                       loginClient.DrainEvents();
                       return loginClient.account.State() == AccountFlowState::CharacterListReady;
                   },
                   8000);
    const std::uint64_t heroId = loginClient.account.Characters().empty()
                                     ? 0
                                     : loginClient.account.Characters()[0].characterId;
    loginClient.account.SendSelectCharacter(loginClient.account.SessionToken(), heroId);
    ok = ok && WaitUntil(
                   [&] {
                       loginClient.DrainEvents();
                       return loginClient.account.State() == AccountFlowState::CharacterSelected;
                   },
                   8000);
    const std::string ticket = loginClient.account.SelectionTicket();
    Check("WorldFullChainCheck: register/login/select -> selectionTicket", ok && !ticket.empty());

    // 进世界
    WorldTestClient worldClient;
    ok = ok && worldClient.ConnectAndEnter(ticket, 8000);
    ok = ok && WaitUntil([&] { worldClient.DrainEvents(); return worldClient.controller.IsWorldReady(); },
                         3000);
    Check("WorldFullChainCheck: enter world -> WorldReady", ok);

    // 移动 + 快照（阶段25 指令四：新角色服务器权威出生 Map1 300,300 -> 移动后 x=312）
    worldClient.client().SendMoveInput(1, 1.0f, 0.0f, 0.1f); // x=300+12
    WorldNetworkEvent snap;
    ok = ok && worldClient.WaitForSnapshotAfterSeq(1, snap);
    ok = ok && std::fabs(snap.positionX - 312.0f) < 0.6f;
    Check("WorldFullChainCheck: move -> snapshot x ~= 312", ok);

    // 断开 -> DB 保存
    worldClient.Disconnect();
    loginClient.Disconnect();
    ok = ok && WaitUntil(
                   [&] {
                       return std::fabs(QueryScalarDouble(
                                  servers.dbPath,
                                  "SELECT position_x FROM characters WHERE id=" +
                                      std::to_string(heroId) + ";") -
                                  312.0) < 0.01;
                   },
                   3000);
    Check("WorldFullChainCheck: disconnect -> position saved", ok);

    // 重新登录 + 新 ticket + 重新进入 -> 位置恢复
    LoginTestClient loginClient2;
    ok = ok && loginClient2.ConnectAndWait();
    loginClient2.account.SendAccountLogin("chain_user", "ChainPass123!");
    ok = ok && WaitUntil(
                   [&] {
                       loginClient2.DrainEvents();
                       return loginClient2.account.State() == AccountFlowState::Authenticated;
                   },
                   8000);
    loginClient2.account.SendCharacterList(loginClient2.account.SessionToken());
    ok = ok && WaitUntil(
                   [&] {
                       loginClient2.DrainEvents();
                       return loginClient2.account.State() == AccountFlowState::CharacterListReady;
                   },
                   8000);
    loginClient2.account.SendSelectCharacter(loginClient2.account.SessionToken(), heroId);
    ok = ok && WaitUntil(
                   [&] {
                       loginClient2.DrainEvents();
                       return loginClient2.account.State() == AccountFlowState::CharacterSelected;
                   },
                   8000);
    const std::string ticket2 = loginClient2.account.SelectionTicket();
    WorldTestClient worldClient2;
    ok = ok && !ticket2.empty() && worldClient2.ConnectAndEnter(ticket2, 8000);
    const WorldNetworkEvent& enter2 = worldClient2.LastEnterSuccess();
    Check("WorldFullChainCheck: re-enter restores saved position (x ~= 312)",
          ok && std::fabs(enter2.positionX - 312.0f) < 0.6f);
    worldClient2.Disconnect();
    loginClient2.Disconnect();
    WaitUntil([&] { return true; }, 300);
    if (servers.gateway) {
        servers.gateway->Stop();
        servers.gateway.reset();
        servers.gatewayService.Stop();
    }
}

} // namespace

#if defined(_WIN32)
// Stage26 临时诊断：崩溃时打印异常码 + 符号化栈回溯（stdout 无缓冲，崩溃前落盘）。
#include <windows.h>
#include <dbghelp.h>
#pragma comment(lib, "dbghelp.lib")
namespace {
LONG WINAPI LegendTestCrashHandler(EXCEPTION_POINTERS* info) {
    static char buf[2048];
    HANDLE proc = GetCurrentProcess();
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(proc, nullptr, TRUE);
    void* stack[62];
    const USHORT frames = CaptureStackBackTrace(0, 62, stack, nullptr);
    int n = std::snprintf(buf, sizeof(buf), "\n[Crash] code=0x%08lX addr=%p frames=%u\n",
                          static_cast<unsigned long>(info->ExceptionRecord->ExceptionCode),
                          info->ExceptionRecord->ExceptionAddress,
                          static_cast<unsigned>(frames));
    if (n > 0) {
        fwrite(buf, 1, static_cast<size_t>(n), stdout);
        fflush(stdout);
    }
    alignas(SYMBOL_INFO) unsigned char symBuf[sizeof(SYMBOL_INFO) + 256 * sizeof(char)];
    auto* sym = reinterpret_cast<SYMBOL_INFO*>(symBuf);
    for (USHORT i = 0; i < frames; ++i) {
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen = 255;
        DWORD64 disp = 0;
        if (SymFromAddr(proc, reinterpret_cast<DWORD64>(stack[i]), &disp, sym)) {
            n = std::snprintf(buf, sizeof(buf), "[Crash] #%.2u %s +0x%llX\n",
                              static_cast<unsigned>(i), sym->Name,
                              static_cast<unsigned long long>(disp));
        } else {
            n = std::snprintf(buf, sizeof(buf), "[Crash] #%.2u %p\n",
                              static_cast<unsigned>(i), stack[i]);
        }
        if (n > 0) {
            fwrite(buf, 1, static_cast<size_t>(n), stdout);
            fflush(stdout);
        }
    }
    fflush(stdout);
    return EXCEPTION_EXECUTE_HANDLER;
}
} // namespace
#endif

int main() {
#if defined(_WIN32)
    SetUnhandledExceptionFilter(LegendTestCrashHandler);
#endif
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    legend::debug::Logger::Init("testlogs");
    std::printf("[WorldTest] LegendWorldTests begin\n");

    RunWorldProtocolChecks();
    RunFullServerTopologyCheck();

    // ---- 阶段25：Chapter One E2E 与新角色出生检查最先执行（快速失败信号；
    //      独立 servers 生命周期，套件顺序不影响结果）----
    RunChapterOneChecks();
    RunVerticalSliceChecks();

    // ---- 阶段11 检查链（握手/EnterWorld/移动/保存/Login 链路/鲁棒性） ----
    {
        WorldTestServers servers;
        servers.dbPath = TempDbPath("world_chain");
        RemoveDb(servers.dbPath);
        Check("LoginStartCheck", servers.StartLogin());
        Check("WorldStartCheck", servers.StartWorld());

        RunWorldHandshakeChecks(servers);
        RunEnterWorldChecks(servers);
        RunLeaveWorldChecks(servers); // 阶段26 指令十七：主动离开世界链路
        RunMovementChecks(servers);
        RunSaveChecks(servers);
        RunLoginLinkChecks(servers);
        RunRobustnessChecks(servers);
        servers.StopAll();
    }

    // ---- Stage26 指令三十六：玩家流程 E2E（注册/登录/建角/进世界/离开/重启持久化；
    //      独立 servers 生命周期 + Gateway 内联拓扑）----
    {
        WorldTestServers servers;
        servers.dbPath = TempDbPath("stage26_flow");
        RemoveDb(servers.dbPath);
        Check("FlowLoginStartCheck", servers.StartLogin());
        Check("FlowWorldStartCheck", servers.StartWorld());
        Check("FlowGatewayStartCheck", servers.StartGateway());

        RunStage26FlowChecks(servers);
        servers.StopAll();
    }

    // ---- 阶段12 AOI 检查（指令七十四：独立 servers 生命周期，同端口串行复用） ----
    RunWorldAoiChecks();

    // ---- 阶段13 怪物检查（指令七十二：独立 servers 生命周期） ----
    RunWorldMonsterChecks();

    // ---- 阶段14 战斗检查（指令九十六：独立 servers 生命周期） ----
    RunWorldCombatChecks();

    // ---- 阶段15 技能检查（指令九十九：独立 servers 生命周期） ----
    RunWorldSkillChecks();

    // ---- 阶段16 状态效果检查（指令八十四：独立 servers 生命周期） ----
    RunWorldStatusChecks();

    // ---- 阶段17 成长/奖励/重生检查（独立 servers 生命周期） ----
    RunWorldProgressionChecks();

    // ---- 阶段18 掉落/背包/装备检查（独立 servers 生命周期） ----
    RunWorldInventoryChecks();

    // ---- 阶段19 服务器权威任务检查（独立 servers 生命周期） ----
    RunWorldQuestChecks();

    // ---- 阶段20 NPC / 对话 / 商店 / 传送检查（独立 servers 生命周期） ----
    RunWorldNpcChecks();

    // ---- 阶段21 多地图 / Portal / 复活检查（独立 servers 生命周期） ----
    RunWorldMapChecks();
    RunWorldDataChecks();
    RunMapEditorDataChecks();
    RunGameDataChecks();
    RunDefinitionValidationChecks();

    // ---- 阶段24 视觉资产/客户端冒烟检查 ----
    RunAssetManifestChecks();
    RunAnimationChecks();
    RunVisualDefinitionChecks();
    RunClientSmokeChecks();

    // ---- 阶段25 Vertical Slice 检查（已在 main 前段执行——此处保留调用点注释） ----

    // ---- 阶段25 UI 模型检查（指令六十七：无服务器，纯模型） ----
    RunUiModelChecks();
    // ---- 阶段11 验收主链（Gateway 全链） ----
    {
        WorldTestServers servers;
        servers.dbPath = TempDbPath("world_chain_full");
        RemoveDb(servers.dbPath);
        servers.StartLogin();
        servers.StartWorld();
        RunWorldFullChainCheck(servers);
        servers.StopAll();
    }

    std::error_code ec;
    std::filesystem::remove_all(TempDir(), ec);

    // 阶段12：g_failures 为共享头计数（阶段11 检查 + AOI 检查同一计数器）
    std::printf("[WorldTest] completed, failures = %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
