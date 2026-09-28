#include "Server/WorldServer/WorldServer.h"

#include "Engine/Debug/Logger.h"
#include "Server/LoginServer/Account/Database/DatabaseSchema.h"
#include "Server/WorldServer/Combat/CombatService.h"
#include "Server/WorldServer/Combat/DamageCalculator.h"
#include "Shared/Combat/CombatProtocol.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/Monster/MonsterProtocol.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/Protocol.h"
#include "Shared/World/WorldError.h"
#include "Shared/World/WorldProtocol.h"
#include "Shared/World/WorldTypes.h"

#include <algorithm>
#include <cmath>
#include <filesystem>

namespace legend::world {

namespace CharacterRepository = legend::account::CharacterRepository;

using legend::network::ClientHelloPayload;
using legend::network::DecodeClientHello;
using legend::network::DecodeServerHello;
using legend::network::EncodeClientHello;
using legend::network::EncodeServerHello;
using legend::network::kProtocolVersion;
using legend::network::MessageId;
using legend::network::Packet;
using legend::network::ServerHelloPayload;
namespace net = legend::net;

namespace {

std::uint64_t ServerTimeMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace

WorldServer::WorldServer(net::NetworkService& service)
    : m_service(service),
      m_server(std::make_shared<net::TcpServer>(service)),
      m_loginClient(std::make_shared<net::TcpClient>(service)),
      m_reconnectTimer(service.Io()),
      m_ticketTimer(service.Io()),
      m_snapshotTimer(service.Io()),
      m_saveTimer(service.Io()),
      m_idleTimer(service.Io()),
      m_aoiTimer(service.Io()),
      m_monsterAiTimer(service.Io()),
      m_healthTimer(service.Io()),
      m_skillTimer(service.Io()),
      m_manaTimer(service.Io()),
      m_statusTimer(service.Io()),
      m_statusSnapshotTimer(service.Io()) {}

bool WorldServer::Start(std::string& error) {
    // 阶段11 指令二十二/二十五：WorldServer 直接打开同一 SQLite（WAL 由
    // Database::Open 统一开启）；Schema 幂等初始化（World 先于 Login 启动也可）。
    std::error_code fsError;
    const std::filesystem::path dbPath(m_config.databasePath);
    if (dbPath.has_parent_path()) {
        std::filesystem::create_directories(dbPath.parent_path(), fsError);
    }
    if (!m_database.Open(m_config.databasePath, error)) {
        error = "world database open failed: " + error;
        return false;
    }
    if (!legend::account::InitializeSchema(m_database, error)) {
        error = "world database schema init failed: " + error;
        m_database.Close();
        return false;
    }
    m_dbWorker.Start();
    if (!m_server->Listen(m_config.listenPort, error)) {
        m_dbWorker.Stop();
        m_database.Close();
        return false;
    }
    m_server->StartAccepting([this](net::TcpConnectionPtr connection) {
        OnClientAccepted(std::move(connection));
    });
    ConnectToLogin();
    ScheduleTicketTimeoutCheck();
    ScheduleSnapshotTimer();
    ScheduleSaveTimer();
    ScheduleClientIdleCheck();
    ScheduleAoiTick(); // 阶段12 指令六十九：AOI tick 200ms
    SpawnInitialMonsters();   // 阶段13 指令十五/十六：固定 20 只 Training Slime
    ScheduleMonsterAiTick();  // 阶段13 指令三十：AI tick 200ms
    ScheduleHealthSnapshotTick(); // 阶段14 指令六十八：HP 纠偏 1s
    ScheduleSkillTick();      // 阶段15 指令七十七/七十八：Skill Tick 50ms（与 AI 分离）
    ScheduleManaSnapshotTick(); // 阶段15 指令六十七：Mana 快照 1s
    ScheduleStatusTick();     // 阶段16 指令四十：Status Tick 100ms
    ScheduleStatusSnapshotTick(); // 阶段16 指令五十九：状态快照 2s
    return true;
}

void WorldServer::Stop() {
    m_stopped.store(true);
    // 指令一百一十二：停新连接 -> Flush Save -> DB Worker Flush -> Close DB。
    m_reconnectTimer.cancel();
    m_ticketTimer.cancel();
    m_snapshotTimer.cancel();
    m_saveTimer.cancel();
    m_idleTimer.cancel();
    m_aoiTimer.cancel();        // 指令六十九：Stop 时 cancel AOI tick
    m_monsterAiTimer.cancel();  // 阶段13 指令六十五：停止 AI Timer
    m_healthTimer.cancel();     // 阶段14：停止 HP 纠偏 timer
    m_skillTimer.cancel();      // 阶段15 指令七十九：停止 Skill Timer
    m_manaTimer.cancel();      // 阶段15 指令七十九：停止 Mana 快照 timer
    m_statusTimer.cancel();     // 阶段16 指令一百三十七：停止 Status Timer
    m_statusSnapshotTimer.cancel(); // 阶段16：停止状态快照 timer
    // 阶段13 指令六十五/六十六：清 Monster（runtime only，无持久化）。
    m_monsters.Clear();
    m_monsterGrid = MonsterSpatialGrid{};
    m_nextMonsterEntityId = 1; // 指令六十七：重启 entityId 允许重新开始
    m_nextCombatEventId = 1;   // 阶段14：eventId 同步复位
    m_nextCastId = 1;          // 阶段15 指令一百四十七：重启 castId 复位，Pending Cast 随
                               // PlayerSession 一起消失（无网络 Cancel，指令七十九）
    m_nextStatusInstanceId = 1; // 阶段16 指令一百三十四/七十九：重启状态全部清空
    m_loginClient->Cancel();
    if (m_loginConnection) {
        m_loginConnection->Close();
        m_loginConnection.reset();
    }
    m_loginAvailable.store(false);
    m_server->Stop();
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        m_sessions.clear();
        m_pendingTickets.clear();
    }
    // Flush Save：对所有在线玩家保存最终位置（指令一百零一）。
    for (const auto& player : m_players.SnapshotPlayers()) {
        SavePlayerPositionNow(player->CharacterId(), player->MapId(), player->PositionX(),
                              player->PositionY());
    }
    m_dbWorker.Stop(); // 等待全部保存任务完成（指令一百一十二）
    m_database.Close();
}

// ---------------------------------------------------------------------------
// Client 连接
// ---------------------------------------------------------------------------

void WorldServer::OnClientAccepted(net::TcpConnectionPtr connection) {
    const std::uint64_t id = connection->Id();
    LOG_INFO("[World] Client #" + std::to_string(id) + " connected.");
    auto session = std::make_shared<WorldSession>(connection, id);
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        m_sessions[id] = session;
    }
    auto self = shared_from_this();
    connection->Start(
        [self, id](const Packet& packet) { self->OnClientPacket(id, packet); },
        [self](std::uint64_t closedId, const std::error_code& ec) {
            self->OnClientClosed(closedId, ec);
        });
}

void WorldServer::OnClientPacket(std::uint64_t connectionId, const Packet& packet) {
    std::shared_ptr<WorldSession> session;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = m_sessions.find(connectionId);
        if (it == m_sessions.end()) {
            return;
        }
        session = it->second;
    }
    std::string error;
    if (!session->OnPacket(packet, error)) {
        // 指令一百零八：协议错误只断当前 Client，WorldServer 继续服务。
        LOG_INFO("[World] Client #" + std::to_string(connectionId) + " rejected: " + error);
        {
            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            m_sessions.erase(connectionId);
        }
        session->Disconnect();
        return;
    }
    Packet enterPacket;
    if (session->TakePendingEnterWorld(enterPacket)) {
        HandleEnterWorldRequest(connectionId, enterPacket);
        return;
    }
    // 握手完成后允许的其它消息
    const auto messageId = static_cast<MessageId>(packet.header.messageId);
    if (session->State() == WorldSessionState::InWorld) {
        if (messageId == MessageId::PlayerMoveInput) {
            HandleMoveInput(connectionId, packet);
        } else if (messageId == MessageId::PlayerAttackRequest) {
            HandlePlayerAttack(connectionId, packet); // 阶段14 指令三十九
        } else if (messageId == MessageId::SkillCastRequest) {
            HandleSkillCastRequest(connectionId, packet); // 阶段15 指令二十二
        }
    }
}

void WorldServer::OnClientClosed(std::uint64_t connectionId, const std::error_code& ec) {
    std::shared_ptr<WorldSession> session;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        m_sessions.erase(connectionId);
        // 指令一百零五：断开时清理该连接的 Ticket pending（响应到达后自然丢弃）
        for (auto it = m_pendingTickets.begin(); it != m_pendingTickets.end();) {
            if (it->second.clientConnectionId == connectionId) {
                it = m_pendingTickets.erase(it);
            } else {
                ++it;
            }
        }
    }
    // 指令五十二/五十三：主动/异常断线都保存最终位置。
    auto player = m_players.RemoveByConnection(connectionId);
    if (player) {
        m_mapManager.RemovePlayer(connectionId, player->MapId());
        // 阶段12 指令十九/五十九/六十：Grid 移除 + 通知所有能看到 B 的玩家 Despawn
        //（防 ghost player），并从他们的 visiblePlayers 清除。
        m_spatialGrid.RemovePlayer(player->CharacterId());
        NotifyPlayerGoneToObservers(player->CharacterId(), PlayerDespawnReason::Disconnected);
        // 阶段13 指令四十四：目标玩家离线 -> 追击它的怪物立刻 Returning。
        OnTargetPlayerRemoved(player->CharacterId());
        SavePlayerPositionNow(player->CharacterId(), player->MapId(), player->PositionX(),
                              player->PositionY());
        LOG_INFO("[World] Player left character=" + player->CharacterName() + " (#" +
                 std::to_string(player->CharacterId()) + ")");
        if (m_hooks.onPlayerChanged) {
            m_hooks.onPlayerChanged(player->CharacterId(), false);
        }
    }
    (void)ec;
    (void)session;
}

// ---------------------------------------------------------------------------
// Login 内部链路（指令七/十/六十八/六十九）
// ---------------------------------------------------------------------------

void WorldServer::ConnectToLogin() {
    if (m_stopped.load()) {
        return;
    }
    auto self = shared_from_this();
    m_loginClient->Connect(
        m_config.loginHost, m_config.loginPort,
        [self](net::TcpConnectionPtr connection) { self->OnLoginConnected(std::move(connection)); },
        [self](const std::error_code&) { self->HandleLoginLinkClosed(); });
}

void WorldServer::ScheduleLoginReconnect() {
    if (m_stopped.load() || m_reconnectScheduled.exchange(true)) {
        return;
    }
    m_reconnectTimer.expires_after(
        std::chrono::milliseconds(static_cast<int>(m_config.loginReconnectSeconds * 1000)));
    auto self = shared_from_this();
    m_reconnectTimer.async_wait([self](const std::error_code& ec) {
        self->m_reconnectScheduled.store(false);
        if (ec || self->m_stopped.load() || self->m_loginConnection ||
            self->m_loginAvailable.load()) {
            return;
        }
        self->ConnectToLogin();
    });
}

void WorldServer::OnLoginConnected(net::TcpConnectionPtr connection) {
    if (m_loginConnection) {
        auto stale = m_loginConnection;
        m_loginConnection.reset();
        stale->Close();
    }
    m_loginConnection = connection;
    m_loginAvailable.store(false);
    m_loginHandshakeDone = false;
    auto self = shared_from_this();
    connection->Start(
        [self, linkId = connection->Id()](const Packet& packet) {
            self->OnLoginPacket(linkId, packet);
        },
        [self](std::uint64_t, const std::error_code&) { self->HandleLoginLinkClosed(); });
    // 指令十：内部握手，clientName="LegendWorldServer"（LoginServer 白名单）
    ClientHelloPayload hello;
    hello.protocolVersion = kProtocolVersion;
    hello.clientBuild = "0.11.0";
    hello.clientName = kServiceNameWorldServer;
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::ClientHello);
    if (!EncodeClientHello(hello, out.payload)) {
        connection->Close();
        return;
    }
    connection->Send(out);
}

void WorldServer::HandleLoginLinkClosed() {
    m_loginConnection.reset();
    m_loginHandshakeDone = false;
    m_loginAvailable.store(false);
    LOG_WARN("[World] Login link closed (players stay online, will reconnect).");
    if (m_hooks.onLoginConnectionChanged) {
        m_hooks.onLoginConnectionChanged(false);
    }
    ScheduleLoginReconnect();
}

void WorldServer::OnLoginPacket(std::uint64_t linkId, const Packet& packet) {
    if (!m_loginConnection || m_loginConnection->Id() != linkId) {
        return;
    }
    const auto messageId = static_cast<MessageId>(packet.header.messageId);
    if (!m_loginHandshakeDone) {
        if (messageId != MessageId::ServerHello) {
            if (m_loginConnection) {
                m_loginConnection->Close();
            }
            return;
        }
        ServerHelloPayload hello;
        std::string error;
        if (!DecodeServerHello(packet.payload.data(), packet.payload.size(), hello, error) ||
            !hello.accepted) {
            if (m_loginConnection) {
                m_loginConnection->Close();
            }
            return;
        }
        m_loginHandshakeDone = true;
        m_loginAvailable.store(true);
        LOG_INFO("[World] Login link ready (server=" + hello.serverName + ").");
        if (m_hooks.onLoginConnectionChanged) {
            m_hooks.onLoginConnectionChanged(true);
        }
        return;
    }
    switch (messageId) {
        case MessageId::ConsumeSelectionTicketResponse:
            HandleConsumeResponse(packet);
            break;
        default:
            break;
    }
}

void WorldServer::SendConsumeRequest(std::uint64_t clientConnectionId, std::uint64_t requestId,
                                     const std::string& ticket) {
    ConsumeSelectionTicketRequestPayload payload;
    payload.requestId = requestId;
    payload.selectionTicket = ticket;
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::ConsumeSelectionTicketRequest);
    if (!EncodeConsumeSelectionTicketRequest(payload, out.payload)) {
        return;
    }
    m_loginConnection->Send(out);
    (void)clientConnectionId;
}

// ---------------------------------------------------------------------------
// EnterWorld 管线（指令十九/二十/二十一/二十九/七十四/七十五/七十六）
// ---------------------------------------------------------------------------

void WorldServer::HandleEnterWorldRequest(std::uint64_t connectionId, const Packet& packet) {
    std::shared_ptr<WorldSession> session;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = m_sessions.find(connectionId);
        if (it == m_sessions.end()) {
            return;
        }
        session = it->second;
    }
    EnterWorldRequestPayload request;
    std::string decodeError;
    if (!DecodeEnterWorldRequest(packet.payload.data(), packet.payload.size(), request,
                                 decodeError) ||
        request.selectionTicket.size() > kSelectionTicketMaxLength) {
        // 指令六十五/一百零八：畸形/超长 ticket -> 只断当前 Client
        LOG_INFO("[World] Malformed EnterWorldRequest from #" + std::to_string(connectionId));
        {
            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            m_sessions.erase(connectionId);
        }
        session->Disconnect();
        return;
    }
    if (!m_loginAvailable.load() || !m_loginConnection) {
        // 指令一百零二：Login 不可用 -> ServiceUnavailable，WorldServer 不 Crash
        LOG_WARN("[World] EnterWorld rejected (login link unavailable) #" +
                 std::to_string(connectionId));
        SendEnterWorldError(connectionId, request.requestId, WorldErrorCode::ServiceUnavailable,
                            "login service unavailable");
        return;
    }
    session->SetState(WorldSessionState::ValidatingTicket);
    const std::uint64_t requestId = m_nextRequestId++;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        PendingTicket pending;
        pending.clientConnectionId = connectionId;
        m_pendingTickets[requestId] = pending; // 指令七十三：requestId -> connectionId
    }
    SendConsumeRequest(connectionId, requestId, request.selectionTicket);
}

void WorldServer::HandleConsumeResponse(const Packet& packet) {
    ConsumeSelectionTicketResponsePayload response;
    std::string decodeError;
    if (!DecodeConsumeSelectionTicketResponse(packet.payload.data(), packet.payload.size(),
                                              response, decodeError)) {
        LOG_WARN("[World] Malformed ConsumeSelectionTicketResponse dropped.");
        return;
    }
    std::shared_ptr<WorldSession> session;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = m_pendingTickets.find(response.requestId);
        if (it == m_pendingTickets.end()) {
            // 指令七十六：不存在的 requestId -> 忽略 + LOG_WARN
            LOG_WARN("[World] Ticket response for unknown requestId " +
                     std::to_string(response.requestId) + " dropped.");
            return;
        }
        const std::uint64_t clientConnectionId = it->second.clientConnectionId;
        m_pendingTickets.erase(it);
        auto sit = m_sessions.find(clientConnectionId);
        if (sit == m_sessions.end()) {
            return; // 指令一百零五：Client 已断开 -> 丢弃（不访问悬空 Session）
        }
        session = sit->second;
    }
    if (session->State() != WorldSessionState::ValidatingTicket) {
        return; // 迟到响应
    }
    if (!response.success) {
        // 指令六十七：Ticket 类错误对 Client 统一 InvalidTicket，精确原因进日志。
        const auto code = static_cast<WorldErrorCode>(response.errorCode);
        WorldErrorCode clientCode = WorldErrorCode::InvalidTicket;
        if (code != WorldErrorCode::InvalidTicket && code != WorldErrorCode::ExpiredTicket &&
            code != WorldErrorCode::ConsumedTicket) {
            clientCode = code;
        }
        LOG_WARN("[World] EnterWorld ticket rejected: " +
                 std::string(WorldErrorCodeName(response.errorCode)));
        SendEnterWorldError(session->ConnectionId(), response.requestId, clientCode,
                            "enter world rejected");
        session->SetState(WorldSessionState::WaitingEnterWorld);
        return;
    }

    // 阶段11 指令二十一/二十二：DB Worker 加载角色（网络线程禁止 SQLite IO）。
    session->SetState(WorldSessionState::LoadingCharacter);
    auto self = shared_from_this();
    const std::uint64_t connectionId = session->ConnectionId();
    const std::uint64_t requestId = response.requestId;
    const std::uint64_t accountId = response.accountId;
    const std::uint64_t characterId = response.characterId;
    m_dbWorker.Post([self, connectionId, requestId, accountId, characterId]() {
        auto found = CharacterRepository::FindCharacterById(self->m_database, characterId);
        // 结果 post 回 io 线程（self 保活，Stop 时 m_stopped 丢弃）
        self->m_service.Post([self, connectionId, requestId, accountId, characterId, found]() {
            if (self->m_stopped.load()) {
                return;
            }
            std::shared_ptr<WorldSession> session;
            {
                std::lock_guard<std::mutex> lock(self->m_sessionsMutex);
                auto sit = self->m_sessions.find(connectionId);
                if (sit != self->m_sessions.end()) {
                    session = sit->second;
                }
            }
            if (!session || session->State() != WorldSessionState::LoadingCharacter) {
                return; // 指令一百零五：Client 已断开/状态漂移 -> 丢弃
            }
            // 指令七十一：Ticket 绑定 accountId + characterId，缺一不可。
            if (!found.success || !found.value.has_value() || found.value->deleted ||
                found.value->accountId != accountId ||
                found.value->id != characterId) {
                LOG_WARN("[World] Character load failed (characterId=" +
                         std::to_string(characterId) + ")");
                self->SendEnterWorldError(connectionId, requestId,
                                          WorldErrorCode::CharacterNotFound,
                                          "character not found");
                session->SetState(WorldSessionState::WaitingEnterWorld);
                return;
            }
            const legend::account::CharacterRow row = *found.value;
            // 指令三十二/五十八：无效 mapId/异常位置回退默认地图 + (0,0)。
            std::uint16_t mapId = row.mapId;
            float x = row.positionX;
            float y = row.positionY;
            if (!IsMapIdSupported(mapId)) {
                mapId = kDefaultMapId;
                x = 0.0f;
                y = 0.0f;
            }
            x = SanitizeCoord(x);
            y = SanitizeCoord(y);
            auto player = std::make_shared<PlayerSession>(connectionId, accountId, characterId,
                                                          row.name, row.classId, row.gender,
                                                          row.level, mapId, x, y);
            // 指令二十九/一百零七：同角色重复上线拒绝。
            if (!self->m_players.TryAddPlayer(player)) {
                LOG_WARN("[World] EnterWorld rejected (character already online) character=" +
                         row.name);
                self->SendEnterWorldError(connectionId, requestId,
                                          WorldErrorCode::CharacterAlreadyOnline,
                                          "character already online");
                session->SetState(WorldSessionState::WaitingEnterWorld);
                return;
            }
            if (!self->m_mapManager.AddPlayer(player)) {
                self->m_players.RemoveByConnection(connectionId);
                self->SendEnterWorldError(connectionId, requestId, WorldErrorCode::InternalError,
                                          "map add failed");
                session->SetState(WorldSessionState::WaitingEnterWorld);
                return;
            }
            // 阶段12 指令十二：进入 WorldManager/MapManager/SpatialGrid。
            self->m_spatialGrid.AddPlayer(player);
            session->SetState(WorldSessionState::InWorld);
            self->SendEnterWorldSuccess(connectionId, requestId, player);
            // 阶段12 指令十八：进入世界初始可见性（双向 Spawn）。
            self->InitializePlayerVisibility(player);
            LOG_INFO("[World] Player entered character=" + row.name + " (#" +
                     std::to_string(characterId) + ") map=" + std::to_string(mapId));
            if (self->m_hooks.onPlayerChanged) {
                self->m_hooks.onPlayerChanged(characterId, true);
            }
        });
    });
}

void WorldServer::SendEnterWorldError(std::uint64_t connectionId, std::uint64_t requestId,
                                      WorldErrorCode errorCode, const std::string& message) {
    EnterWorldResponsePayload out;
    out.requestId = requestId;
    out.success = false;
    out.errorCode = static_cast<std::uint16_t>(errorCode);
    out.message = message;
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::EnterWorldResponse);
    if (!EncodeEnterWorldResponse(out, packet.payload)) {
        return;
    }
    std::shared_ptr<WorldSession> session;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = m_sessions.find(connectionId);
        if (it != m_sessions.end()) {
            session = it->second;
        }
    }
    if (session) {
        session->SendPacket(packet);
    }
}

void WorldServer::SendEnterWorldSuccess(std::uint64_t connectionId, std::uint64_t requestId,
                                        const std::shared_ptr<PlayerSession>& player) {
    EnterWorldResponsePayload out;
    out.requestId = requestId;
    out.success = true;
    out.accountId = player->AccountId();
    out.characterId = player->CharacterId();
    out.characterName = player->CharacterName();
    out.classId = player->ClassId();
    out.gender = player->Gender();
    out.level = player->Level();
    out.mapId = player->MapId();
    out.positionX = player->PositionX();
    out.positionY = player->PositionY();
    out.serverTime = ServerTimeMs();
    // 阶段14 指令十七：进入世界返回玩家 HP（默认满血，不持久化）。
    out.currentHp = player->CurrentHp();
    out.maxHp = player->MaxHp();
    out.alive = player->Alive();
    // 阶段15 指令六十九：进入世界返回玩家 Mana（不持久化，恢复 100/100）。
    out.currentMana = player->CurrentMana();
    out.maxMana = player->MaxMana();
    out.errorCode = static_cast<std::uint16_t>(WorldErrorCode::None);
    out.message = "ok";
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::EnterWorldResponse);
    if (!EncodeEnterWorldResponse(out, packet.payload)) {
        return;
    }
    std::shared_ptr<WorldSession> session;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = m_sessions.find(connectionId);
        if (it != m_sessions.end()) {
            session = it->second;
        }
    }
    if (session) {
        session->SendPacket(packet);
    }
}

// ---------------------------------------------------------------------------
// 权威移动 / 快照 / 保存 / 超时
// ---------------------------------------------------------------------------

void WorldServer::HandleMoveInput(std::uint64_t connectionId, const Packet& packet) {
    PlayerMoveInputPayload input;
    std::string decodeError;
    if (!DecodePlayerMoveInput(packet.payload.data(), packet.payload.size(), input, decodeError)) {
        // 指令六十三/一百零八：畸形包只断当前 Client
        LOG_INFO("[World] Malformed PlayerMoveInput from #" + std::to_string(connectionId));
        std::shared_ptr<WorldSession> session;
        {
            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            auto it = m_sessions.find(connectionId);
            if (it != m_sessions.end()) {
                session = it->second;
            }
        }
        if (session) {
            {
                std::lock_guard<std::mutex> lock(m_sessionsMutex);
                m_sessions.erase(connectionId);
            }
            session->Disconnect();
        }
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        return;
    }
    // 阶段14 指令五十八：死亡玩家 MoveInput 直接忽略（位置不变）。
    if (!player->Alive()) {
        return;
    }
    // 阶段15 指令七十二/七十三：有效移动（位置将实际变化）取消当前施法——
    // 先取消再执行移动；direction 0,0、地图 Clamp 抵消等位置不变的情况不取消
    //（ApplyMoveInput 对任何单调 sequence 都返回 true，必须以位置差为准）。
    const float prevX = player->PositionX();
    const float prevY = player->PositionY();
    const bool moved = WorldMapManager::ApplyMoveInput(*player, input.inputSequence,
                                                       input.directionX, input.directionY,
                                                       input.deltaTime);
    const bool actuallyMoved =
        player->PositionX() != prevX || player->PositionY() != prevY;
    if (actuallyMoved && player->IsCasting()) {
        CancelActiveCast(player, SkillCancelReason::Moved);
    }
    if (moved) {
        // 阶段12 指令十二：移动后重挂 SpatialGrid cell。
        m_spatialGrid.UpdatePlayerCell(player);
    }
}

void WorldServer::ScheduleSnapshotTimer() {
    if (m_stopped.load()) {
        return;
    }
    m_snapshotTimer.expires_after(std::chrono::milliseconds(m_config.snapshotIntervalMs));
    auto self = shared_from_this();
    m_snapshotTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->SendPositionSnapshots();
        self->ScheduleSnapshotTimer();
    });
}

void WorldServer::SendPositionSnapshots() {
    // 阶段11 指令四十二/四十三：100ms 同步自己的权威位置。
    // 阶段12 指令二十九/三十/七十：同一 timer 扩展 visiblePlayers batch 快照。
    const std::uint64_t serverTime = ServerTimeMs();
    for (const auto& player : m_players.SnapshotPlayers()) {
        PlayerPositionSnapshotPayload snapshot;
        snapshot.characterId = player->CharacterId();
        snapshot.positionX = player->PositionX();
        snapshot.positionY = player->PositionY();
        snapshot.lastProcessedInputSequence = player->LastProcessedInputSequence();
        snapshot.serverTime = serverTime;
        Packet out;
        out.header.messageId = static_cast<std::uint16_t>(MessageId::PlayerPositionSnapshot);
        if (!EncodePlayerPositionSnapshot(snapshot, out.payload)) {
            continue;
        }
        SendPacketToPlayer(player, out);
        // 阶段12 指令二十九：visiblePlayers 的位置 batch（100ms）。
        SendRemoteBatches(player, serverTime);
        // 阶段13 指令四十九/五十/七十：visibleMonsters 的 MonsterBatch（同一 timer）。
        SendMonsterBatches(player, serverTime);
    }
}

void WorldServer::ScheduleSaveTimer() {
    if (m_stopped.load()) {
        return;
    }
    m_saveTimer.expires_after(
        std::chrono::milliseconds(static_cast<int>(m_config.positionSaveIntervalSeconds * 1000)));
    auto self = shared_from_this();
    m_saveTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->SaveDirtyPositions();
        self->ScheduleSaveTimer();
    });
}

void WorldServer::SaveDirtyPositions() {
    // 阶段11 指令五十四：周期保存（dirtyPosition 才写库）。
    for (const auto& player : m_players.SnapshotPlayers()) {
        if (player->IsPositionDirty()) {
            player->SetPositionDirty(false);
            SavePlayerPositionNow(player->CharacterId(), player->MapId(), player->PositionX(),
                                  player->PositionY());
        }
    }
}

void WorldServer::SavePlayerPositionNow(std::uint64_t characterId, std::uint16_t mapId, float x,
                                        float y) {
    // 指令五十二/五十三/五十六：位置保存经 DB Worker（prepared statement）。
    auto self = shared_from_this();
    m_dbWorker.Post([self, characterId, mapId, x, y]() {
        auto saved = CharacterRepository::UpdateWorldPosition(self->m_database, characterId, mapId,
                                                              x, y, legend::account::UnixNow());
        if (!saved.success) {
            LOG_ERROR("[World] Save position failed character=" + std::to_string(characterId) +
                      " detail=" + saved.errorMessage);
        }
    });
}

void WorldServer::SavePlayerPosition(const std::shared_ptr<PlayerSession>& player,
                                     bool touchLastPlayed) {
    (void)touchLastPlayed;
    SavePlayerPositionNow(player->CharacterId(), player->MapId(), player->PositionX(),
                          player->PositionY());
}

void WorldServer::ScheduleTicketTimeoutCheck() {
    if (m_stopped.load()) {
        return;
    }
    m_ticketTimer.expires_after(std::chrono::milliseconds(250));
    auto self = shared_from_this();
    m_ticketTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->CheckTicketTimeouts();
        self->ScheduleTicketTimeoutCheck();
    });
}

void WorldServer::CheckTicketTimeouts() {
    const auto now = std::chrono::steady_clock::now();
    const auto timeout = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(m_config.ticketPendingTimeoutSeconds));
    std::vector<std::pair<std::uint64_t, PendingTicket>> expired;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (auto it = m_pendingTickets.begin(); it != m_pendingTickets.end();) {
            if (now - it->second.createdAt > timeout) {
                expired.emplace_back(it->first, it->second);
                it = m_pendingTickets.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (const auto& [requestId, pending] : expired) {
        // 指令七十四：Ticket 验证 3s 超时 -> ServiceUnavailable + pending 清理。
        LOG_WARN("[World] Ticket validation timeout requestId=" + std::to_string(requestId));
        SendEnterWorldError(pending.clientConnectionId, requestId,
                            WorldErrorCode::ServiceUnavailable, "ticket validation timeout");
        std::shared_ptr<WorldSession> session;
        {
            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            auto sit = m_sessions.find(pending.clientConnectionId);
            if (sit != m_sessions.end()) {
                session = sit->second;
            }
        }
        if (session && session->State() == WorldSessionState::ValidatingTicket) {
            session->SetState(WorldSessionState::WaitingEnterWorld);
        }
    }
}

void WorldServer::ScheduleClientIdleCheck() {
    if (m_stopped.load()) {
        return;
    }
    m_idleTimer.expires_after(std::chrono::milliseconds(250));
    auto self = shared_from_this();
    m_idleTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->CheckClientIdle();
        self->ScheduleClientIdleCheck();
    });
}

void WorldServer::CheckClientIdle() {
    const auto now = std::chrono::steady_clock::now();
    const auto idle = std::chrono::duration_cast<std::chrono::steady_clock::duration>(
        std::chrono::duration<double>(m_config.clientIdleTimeoutSeconds));
    std::vector<std::shared_ptr<WorldSession>> expired;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        for (auto it = m_sessions.begin(); it != m_sessions.end();) {
            if (now - it->second->LastPacketTime() > idle) {
                expired.push_back(it->second);
                it = m_sessions.erase(it);
            } else {
                ++it;
            }
        }
    }
    for (auto& session : expired) {
        LOG_INFO("[World] Client #" + std::to_string(session->ConnectionId()) +
                 " idle timeout, disconnected.");
        session->Disconnect();
    }
}

// ---------------------------------------------------------------------------
// 阶段12：AOI（指令九~二十一/五十~五十二/六十六~七十）
// ---------------------------------------------------------------------------

void WorldServer::ScheduleAoiTick() {
    if (m_stopped.load()) {
        return;
    }
    m_aoiTimer.expires_after(std::chrono::milliseconds(m_config.aoiTickMs));
    auto self = shared_from_this();
    m_aoiTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->RunAoiTick();
        self->ScheduleAoiTick();
    });
}

void WorldServer::RunAoiTick() {
    // 指令十五：200ms 一次（非每帧全量扫描）；指令十六：Grid 候选 -> 滞回 -> delta。
    // 指令六十六：禁止全图广播——只查 Grid 候选 + visible set 差量。
    for (const auto& player : m_players.SnapshotPlayers()) {
        auto candidates =
            m_spatialGrid.QueryNearbyPlayers(player->PositionX(), player->PositionY(),
                                             m_config.aoiLeaveRadius, player->CharacterId());
        const AoiDelta delta =
            ResolveAoiVisibility(candidates, player->MapId(), player->VisiblePlayers(),
                                 m_config.aoiEnterRadius, m_config.aoiLeaveRadius,
                                 m_config.aoiVisibleLimit);
        for (const auto& spawn : delta.spawns) {
            // 指令五十：进入 visible set 只发一次 Spawn（由 set diff 保证）。
            SendPlayerSpawn(player, spawn);
            player->AddVisiblePlayer(spawn->CharacterId());
            LOG_DEBUG("[World] AOI enter char=" + std::to_string(player->CharacterId()) + " sees=" +
                      std::to_string(spawn->CharacterId()));
        }
        for (const auto despawnId : delta.despawns) {
            // 指令五十一/五十二：离开只发一次 Despawn；重新进入由下轮 tick Spawn。
            SendPlayerDespawn(player, despawnId, PlayerDespawnReason::LeftAOI);
            player->EraseVisiblePlayer(despawnId);
            LOG_DEBUG("[World] AOI leave char=" + std::to_string(player->CharacterId()) +
                      " lost=" + std::to_string(despawnId));
        }
        // 阶段13 指令二十五~二十九：怪物 AOI 差量（同一 tick，独立 resolver）。
        UpdatePlayerMonsterVisibility(player, false);
    }
}

void WorldServer::InitializePlayerVisibility(const std::shared_ptr<PlayerSession>& player) {
    // 指令十八：A 进入世界 -> 对 A 发附近已有玩家 Spawn；A 在附近玩家 AOI 内 ->
    // 向他们发 A 的 Spawn（双向初始可见性，紧接的 AOI tick 维护一致性）。
    auto candidates =
        m_spatialGrid.QueryNearbyPlayers(player->PositionX(), player->PositionY(),
                                         m_config.aoiEnterRadius, player->CharacterId());
    // 指令二十八：距离升序 -> characterId 升序（稳定）。
    std::sort(candidates.begin(), candidates.end(),
              [](const AoiCandidate& a, const AoiCandidate& b) {
                  if (a.distanceSquared != b.distanceSquared) {
                      return a.distanceSquared < b.distanceSquared;
                  }
                  return a.player->CharacterId() < b.player->CharacterId();
              });
    const float enterRadiusSq = m_config.aoiEnterRadius * m_config.aoiEnterRadius;
    for (const auto& candidate : candidates) {
        if (candidate.player->MapId() != player->MapId() ||
            candidate.distanceSquared > enterRadiusSq) {
            continue; // 指令二十：跨地图绝不可见
        }
        if (player->VisibleCount() < m_config.aoiVisibleLimit) {
            SendPlayerSpawn(player, candidate.player);
            player->AddVisiblePlayer(candidate.player->CharacterId());
        }
        if (candidate.player->VisibleCount() < m_config.aoiVisibleLimit) {
            SendPlayerSpawn(candidate.player, player);
            candidate.player->AddVisiblePlayer(player->CharacterId());
        }
    }
    // 阶段13 指令二十四：进入世界还必须收到附近怪物 MonsterSpawn。
    UpdatePlayerMonsterVisibility(player, true);
}

void WorldServer::NotifyPlayerGoneToObservers(std::uint64_t characterId,
                                              PlayerDespawnReason reason) {
    // 指令十九/五十九/六十：所有当前能看到该角色的玩家必须收到 Despawn，并清除集合。
    for (const auto& observer : m_players.SnapshotPlayers()) {
        if (observer->CharacterId() == characterId) {
            continue;
        }
        if (observer->EraseVisiblePlayer(characterId)) {
            SendPlayerDespawn(observer, characterId, reason);
        }
    }
}

void WorldServer::SendPlayerSpawn(const std::shared_ptr<PlayerSession>& receiver,
                                  const std::shared_ptr<PlayerSession>& target) {
    PlayerSpawnPayload payload;
    payload.characterId = target->CharacterId();
    payload.name = target->CharacterName();
    payload.classId = target->ClassId();
    payload.gender = target->Gender();
    payload.level = target->Level();
    payload.mapId = target->MapId();
    payload.positionX = target->PositionX();
    payload.positionY = target->PositionY();
    payload.serverTime = ServerTimeMs();
    // 阶段14 指令十六：PlayerSpawn 携带 HP。
    payload.currentHp = target->CurrentHp();
    payload.maxHp = target->MaxHp();
    payload.alive = target->Alive();
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::PlayerSpawn);
    if (EncodePlayerSpawn(payload, out.payload)) {
        SendPacketToPlayer(receiver, out);
    }
}

void WorldServer::SendPlayerDespawn(const std::shared_ptr<PlayerSession>& receiver,
                                    std::uint64_t targetCharacterId, PlayerDespawnReason reason) {
    PlayerDespawnPayload payload;
    payload.characterId = targetCharacterId;
    payload.reason = static_cast<std::uint8_t>(reason);
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::PlayerDespawn);
    if (EncodePlayerDespawn(payload, out.payload)) {
        SendPacketToPlayer(receiver, out);
    }
}

void WorldServer::SendRemoteBatches(const std::shared_ptr<PlayerSession>& player,
                                    std::uint64_t serverTime) {
    // 指令三十：batch 不含接收者自己；指令三十一：位置来自服务器权威 PlayerSession。
    std::vector<RemotePlayerBatchEntry> entries;
    entries.reserve(player->VisibleCount());
    for (const auto remoteId : player->VisiblePlayers()) {
        if (remoteId == player->CharacterId()) {
            continue;
        }
        const auto remote = m_players.FindByCharacter(remoteId);
        if (!remote) {
            continue; // 可见集短暂残留（下个 AOI tick 清理）
        }
        entries.push_back({remote->CharacterId(), remote->PositionX(), remote->PositionY(),
                           remote->LastProcessedInputSequence()});
    }
    // 指令二十六/六十三：单 batch <= 128，超过拆包。
    for (std::size_t offset = 0; offset < entries.size(); offset += kRemoteBatchMaxPlayers) {
        RemotePlayerBatchSnapshotPayload batch;
        batch.serverTime = serverTime;
        const auto begin = entries.begin() + static_cast<std::ptrdiff_t>(offset);
        const auto end = entries.begin() +
                         static_cast<std::ptrdiff_t>(
                             std::min(offset + kRemoteBatchMaxPlayers, entries.size()));
        batch.players.assign(begin, end);
        Packet out;
        out.header.messageId = static_cast<std::uint16_t>(MessageId::RemotePlayerBatchSnapshot);
        if (EncodeRemotePlayerBatchSnapshot(batch, out.payload)) {
            SendPacketToPlayer(player, out);
        }
    }
    // 指令六十四：visiblePlayers 为空时不发空 batch（entries.size()==0 循环不执行）。
}

void WorldServer::SendPacketToPlayer(const std::shared_ptr<PlayerSession>& player,
                                     const legend::network::Packet& packet) {
    std::shared_ptr<WorldSession> session;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = m_sessions.find(player->ConnectionId());
        if (it != m_sessions.end()) {
            session = it->second;
        }
    }
    if (session) {
        session->SendPacket(packet);
    }
}

// ---------------------------------------------------------------------------
// 阶段13：服务器权威怪物（指令十五/十七~三十一/四十九/五十八/六十五~七十/一百零六）
// ---------------------------------------------------------------------------

std::vector<std::uint64_t> WorldServer::MonsterEntityIds() const {
    std::vector<std::uint64_t> ids;
    for (const auto& monster : m_monsters.SnapshotMonsters()) {
        ids.push_back(monster->EntityId());
    }
    return ids;
}

void WorldServer::SpawnInitialMonsters() {
    // 指令十五/十六：map1 固定生成 20 只 Training Slime（固定位置表，测试可复现）。
    const MonsterDefinition* definition = FindMonsterDefinition(kTrainingSlimeTypeId);
    if (!definition) {
        LOG_ERROR("[World] Monster definition missing (Training Slime).");
        return;
    }
    for (const auto& point : kInitialMonsterSpawnTable) {
        auto monster = std::make_shared<MonsterEntity>(m_nextMonsterEntityId++,
                                                       definition->monsterTypeId, kDefaultMapId,
                                                       point.x, point.y, definition->moveSpeed);
        // 阶段14 指令五/六：战斗属性初始化（满血）；阶段16 指令二十四：Base/Derived 属性。
        monster->InitializeCombat(definition->maxHp);
        monster->SetBaseStats(definition->attackPower, definition->defense,
                              definition->moveSpeed);
        if (!m_monsters.SpawnMonster(monster)) {
            LOG_WARN("[World] Monster spawn duplicate entityId=" +
                     std::to_string(monster->EntityId()));
            continue;
        }
        m_monsterGrid.AddMonster(monster);
    }
    LOG_INFO("[World] Spawned " + std::to_string(m_monsters.Count()) + " " +
             definition->name + " monsters (fixed table).");
}

void WorldServer::ScheduleMonsterAiTick() {
    if (m_stopped.load()) {
        return;
    }
    m_monsterAiTimer.expires_after(std::chrono::milliseconds(m_config.monsterAiTickMs));
    auto self = shared_from_this();
    m_monsterAiTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->RunMonsterAiTick();
        self->ScheduleMonsterAiTick();
    });
}

void WorldServer::RunMonsterAiTick() {
    // 指令三十/三十一/六十九：AI tick 200ms，World io 线程执行（无 AI 线程池）；
    // AI 与 AOI 解耦（指令六十八）：本 tick 只推进状态与位置。
    const MonsterDefinition* definition = FindMonsterDefinition(kTrainingSlimeTypeId);
    if (!definition) {
        return;
    }
    // 阶段14 指令五十四/七十七：死亡 3 秒后清理（统一 AI tick 检查 deadSince，
    // 不建 per-monster timer）；RemoveMonster 会广播 MonsterDespawn(Removed)。
    CleanupDeadMonsters();
    const float dt = static_cast<float>(m_config.monsterAiTickMs) / 1000.0f; // server tick dt
    for (const auto& monster : m_monsters.SnapshotMonsters()) {
        StepMonsterAi(*monster, *definition, dt, m_spatialGrid, m_players);
        m_monsterGrid.UpdateMonsterCell(monster); // AI 移动后重挂 cell
        // 阶段14 指令四十三/四十五：进入攻击范围后由 AI tick 触发普通攻击。
        TryMonsterAttack(monster, *definition);
    }
}

void WorldServer::CleanupDeadMonsters() {
    const auto now = std::chrono::steady_clock::now();
    std::vector<std::uint64_t> expired;
    for (const auto& monster : m_monsters.SnapshotMonsters()) {
        if (!monster->Alive() &&
            std::chrono::duration<float>(now - monster->DeadSince()).count() >=
                kMonsterDeathCleanupSeconds) {
            expired.push_back(monster->EntityId());
        }
    }
    for (const auto entityId : expired) {
        RemoveMonster(entityId); // 广播 MonsterDespawn(Removed)
    }
}

void WorldServer::TryMonsterAttack(const std::shared_ptr<MonsterEntity>& monster,
                                   const MonsterDefinition& definition) {
    // 阶段14 指令四十三/四十五/四十六：AI tick 触发的普通攻击（不是 Client 请求）。
    // 前置：怪物存活且在 Chase；StepMonsterAi 已把死亡/离线/死亡目标转 Returning。
    if (!monster->Alive() || monster->State() != MonsterState::Chase) {
        return;
    }
    auto target = m_players.FindByCharacter(monster->TargetCharacterId());
    if (!target || !target->Alive() || target->MapId() != monster->MapId()) {
        return; // StepMonsterAi 已处理 Returning；此处仅防御
    }
    // 指令十九/四十四：攻击距离判定（服务器权威位置，distanceSquared）。
    const float dx = monster->PositionX() - target->PositionX();
    const float dy = monster->PositionY() - target->PositionY();
    const float rangeSq = definition.attackRange * definition.attackRange;
    if (dx * dx + dy * dy > rangeSq) {
        return; // 未进入攻击范围
    }
    // 指令四十六/二十八：怪物攻击冷却（1.2s，steady_clock 权威）。
    if (!IsAttackOffCooldown(std::chrono::steady_clock::now(), monster->LastAttackTime(),
                             definition.attackCooldownSeconds)) {
        return;
    }
    monster->TouchAttackTime();
    // 指令二十四/二十六：伤害 = max(1, atk - def) = max(1, 10 - 5) = 5。
    // 阶段16 指令二十九：改用 Derived Stats（Effective）。
    const std::uint32_t damage =
        CalculateDamage(monster->EffectiveAttackPower(), target->EffectiveDefense());
    const bool killed = target->ApplyDamage(damage);
    // 指令四十/四十一：广播范围 = 能看到受害玩家的附近玩家 ∪ 受害者本人（去重）。
    std::vector<std::uint64_t> receivers;
    receivers.push_back(target->CharacterId());
    for (const auto& observer : m_players.SnapshotPlayers()) {
        if (observer->CharacterId() != target->CharacterId() &&
            observer->VisiblePlayers().count(target->CharacterId()) != 0) {
            receivers.push_back(observer->CharacterId());
        }
    }
    CombatEventPayload event;
    event.eventId = m_nextCombatEventId++;
    event.attackerType = static_cast<std::uint8_t>(CombatEntityType::Monster);
    event.attackerId = monster->EntityId();
    event.targetType = static_cast<std::uint8_t>(CombatEntityType::Player);
    event.targetId = target->CharacterId();
    event.damage = damage;
    event.targetHpAfter = target->CurrentHp();
    event.targetMaxHp = target->MaxHp();
    event.killed = killed;
    event.serverTime = ServerTimeMs();
    event.sourceType = static_cast<std::uint8_t>(CombatSource::BasicAttack); // 阶段15 指令三十二
    event.sourceId = 0;
    BroadcastCombatEvent(event, receivers);
    // 指令七十六：战斗日志只记成功攻击（不刷失败包）。
    LOG_INFO("[Combat] Monster #" + std::to_string(monster->EntityId()) + " hit player " +
             target->CharacterName() + " (#" + std::to_string(target->CharacterId()) + ") for " +
             std::to_string(damage) + " (hp=" + std::to_string(target->CurrentHp()) + ")");
    if (killed) {
        // 指令四十七/四十八：玩家死亡 + 所有以其为 target 的怪 Returning（含本怪）。
        KillPlayer(target, CombatEntityType::Monster, monster->EntityId(), receivers);
    }
}

void WorldServer::UpdatePlayerMonsterVisibility(const std::shared_ptr<PlayerSession>& player,
                                                bool initialVisibility) {
    // 指令二十五/二十六/二十七：怪物 AOI 差量（Enter 600 / Leave 700 滞回）。
    // initialVisibility=true 时为进入世界初始可见性（只发 <=600 的初始 spawn）。
    auto candidates =
        m_monsterGrid.QueryNearbyMonsters(player->PositionX(), player->PositionY(),
                                          m_config.aoiLeaveRadius, 0);
    const MonsterAoiDelta delta = ResolveMonsterAoiVisibility(
        candidates, player->MapId(), player->VisibleMonsters(), m_config.aoiEnterRadius,
        m_config.aoiLeaveRadius, m_config.aoiVisibleLimit);
    for (const auto& spawn : delta.spawns) {
        // 指令二十八：visibleMonsters 期间只 Spawn 一次。
        SendMonsterSpawn(player, spawn);
        player->AddVisibleMonster(spawn->EntityId());
        LOG_DEBUG("[World] Monster AOI enter char=" + std::to_string(player->CharacterId()) +
                  " monster=" + std::to_string(spawn->EntityId()));
    }
    if (!initialVisibility) {
        for (const auto despawnId : delta.despawns) {
            // 指令二十六/二十九：离开只 Despawn 一次；重新进入由下轮 tick Spawn。
            SendMonsterDespawn(player, despawnId, MonsterDespawnReason::LeftAOI);
            player->EraseVisibleMonster(despawnId);
            LOG_DEBUG("[World] Monster AOI leave char=" + std::to_string(player->CharacterId()) +
                      " monster=" + std::to_string(despawnId));
        }
    }
}

void WorldServer::NotifyMonsterGoneToObservers(std::uint64_t monsterEntityId,
                                               MonsterDespawnReason reason) {
    // 指令一百零六：所有可见该怪物的玩家收到 Despawn，并清除 visibleMonsters。
    for (const auto& observer : m_players.SnapshotPlayers()) {
        if (observer->EraseVisibleMonster(monsterEntityId)) {
            SendMonsterDespawn(observer, monsterEntityId, reason);
        }
    }
}

bool WorldServer::RemoveMonster(std::uint64_t entityId) {
    auto monster = m_monsters.RemoveMonster(entityId);
    if (!monster) {
        return false;
    }
    m_monsterGrid.RemoveMonster(entityId);
    NotifyMonsterGoneToObservers(entityId, MonsterDespawnReason::Removed);
    LOG_INFO("[World] Monster removed entityId=" + std::to_string(entityId));
    return true;
}

void WorldServer::OnTargetPlayerRemoved(std::uint64_t characterId) {
    // 指令四十四：目标玩家离线 -> 追击它的怪物立刻 Returning。
    for (const auto& monster : m_monsters.SnapshotMonsters()) {
        if (monster->State() == MonsterState::Chase &&
            monster->TargetCharacterId() == characterId) {
            monster->SetTargetCharacterId(0);
            monster->SetState(MonsterState::Returning);
            monster->TouchStateEnterTime();
            LOG_DEBUG("[World] Monster #" + std::to_string(monster->EntityId()) +
                      " target lost (player removed) -> Returning");
        }
    }
}

// ---------------------------------------------------------------------------
// 阶段14：服务器权威战斗（指令一~三/十~十四/二十四~三十四/三十九~四十二/
// 四十七~五十/五十三~五十五/六十八/七十七~七十九/八十二~八十九/一百二十九）
// ---------------------------------------------------------------------------

void WorldServer::HandlePlayerAttack(std::uint64_t connectionId,
                                     const legend::network::Packet& packet) {
    // 指令八十六：Malformed 遵循现有 WorldServer 协议策略——返回 MalformedRequest
    // 响应（requestId 尽力回显），不断开，WorldServer 继续运行。
    PlayerAttackRequestPayload request;
    std::string decodeError;
    if (!DecodePlayerAttackRequest(packet.payload.data(), packet.payload.size(), request,
                                   decodeError)) {
        LOG_INFO("[Combat] Malformed PlayerAttackRequest from #" + std::to_string(connectionId));
        SendAttackResponse(nullptr, request.requestId, false, CombatResultCode::MalformedRequest,
                           0);
        return;
    }
    auto attacker = m_players.FindByConnection(connectionId);
    if (!attacker) {
        SendAttackResponse(nullptr, request.requestId, false, CombatResultCode::NotInWorld,
                           request.targetEntityId);
        return;
    }
    // 指令八十三：targetType=Player（含 Self）阶段14 一律 InvalidTarget（不做 PvP）；
    // 指令八十四：Monster vs Monster 禁止（客户端只发 Monster 目标，其它类型拒绝）。
    if (request.targetEntityType != static_cast<std::uint8_t>(CombatEntityType::Monster)) {
        SendAttackResponse(attacker, request.requestId, false, CombatResultCode::InvalidTarget,
                           request.targetEntityId);
        return;
    }
    // 指令三十/三十一/一百零二：重复 requestId 不重复造成伤害 -> DuplicateRequest。
    // 只记录成功造成伤害的请求（失败请求允许重试）。
    if (attacker->IsRecentAttackRequest(request.requestId)) {
        SendAttackResponse(attacker, request.requestId, false, CombatResultCode::DuplicateRequest,
                           request.targetEntityId);
        return;
    }
    // 指令二十一/二十二：死亡玩家不能攻击。
    if (!attacker->Alive()) {
        SendAttackResponse(attacker, request.requestId, false, CombatResultCode::AttackerDead,
                           request.targetEntityId);
        return;
    }
    // 阶段15 指令七十一：施法中禁止普通攻击（Busy，不伤害）。
    if (attacker->IsCasting()) {
        SendAttackResponse(attacker, request.requestId, false, CombatResultCode::Busy,
                           request.targetEntityId);
        return;
    }
    // 指令三十二/三十三/三十四/八十八：目标必须存在于服务器 MonsterManager 且在
    // 攻击者 visibleMonsters 内（防远程作弊：AOI 离开立即失效）。
    if (attacker->VisibleMonsters().count(request.targetEntityId) == 0) {
        SendAttackResponse(attacker, request.requestId, false, CombatResultCode::InvalidTarget,
                           request.targetEntityId);
        return;
    }
    auto target = m_monsters.FindMonster(request.targetEntityId);
    if (!target) {
        SendAttackResponse(attacker, request.requestId, false, CombatResultCode::InvalidTarget,
                           request.targetEntityId);
        return;
    }
    // 指令二十/二十三：死亡/跨地图目标验证（服务器权威状态）。
    AttackContext context;
    context.attackerAlive = attacker->Alive();
    context.attackerInWorld = true;
    context.targetAlive = target->Alive();
    context.targetVisible = true; // 已验证 visibleMonsters
    context.sameMap = attacker->MapId() == target->MapId();
    const float dx = attacker->PositionX() - target->PositionX();
    const float dy = attacker->PositionY() - target->PositionY();
    context.distanceSquared = dx * dx + dy * dy;
    const float range = attacker->AttackRange();
    context.attackRangeSquared = range * range;
    context.now = std::chrono::steady_clock::now();
    context.lastAttackTime = attacker->LastAttackTime();
    context.attackCooldownSeconds = attacker->AttackCooldownSeconds();
    const CombatResultCode code = ValidateAttack(context);
    if (code != CombatResultCode::Success) {
        // 指令八十七：验证失败的请求不记录 requestId，不造成伤害（防刷）。
        SendAttackResponse(attacker, request.requestId, false, code, request.targetEntityId);
        return;
    }
    // ---- 攻击生效（服务器权威扣血）----
    attacker->RememberAttackRequest(request.requestId);
    attacker->TouchAttackTime();
    attacker->SetCombatTargetEntityId(target->EntityId());
    // 指令二十四/二十五/三十六：damage = max(1, atk - def) = max(1, 20 - 2) = 18
    //（怪物防御来自 Entity 的 EffectiveDefense——阶段16 指令二十九）。
    const std::uint32_t damage =
        CalculateDamage(attacker->EffectiveAttackPower(), target->EffectiveDefense());
    const bool killed = target->ApplyDamage(damage);
    // 指令四十/四十二：广播范围 = 目标怪物当前所有观察者 ∪ 攻击者本人（去重，
    // 不全世界广播，指令一百一十五）。
    std::vector<std::uint64_t> receivers;
    receivers.push_back(attacker->CharacterId());
    for (const auto& observer : m_players.SnapshotPlayers()) {
        if (observer->CharacterId() != attacker->CharacterId() &&
            observer->VisibleMonsters().count(target->EntityId()) != 0) {
            receivers.push_back(observer->CharacterId());
        }
    }
    CombatEventPayload event;
    event.eventId = m_nextCombatEventId++; // 指令十三：单调 eventId
    event.attackerType = static_cast<std::uint8_t>(CombatEntityType::Player);
    event.attackerId = attacker->CharacterId();
    event.targetType = static_cast<std::uint8_t>(CombatEntityType::Monster);
    event.targetId = target->EntityId();
    event.damage = damage;
    event.targetHpAfter = target->CurrentHp();
    event.targetMaxHp = target->MaxHp();
    event.killed = killed;
    event.serverTime = ServerTimeMs();
    event.sourceType = static_cast<std::uint8_t>(CombatSource::BasicAttack); // 阶段15 指令三十二
    event.sourceId = 0;
    SendAttackResponse(attacker, request.requestId, true, CombatResultCode::Success,
                       target->EntityId());
    BroadcastCombatEvent(event, receivers);
    LOG_INFO("[Combat] Player " + attacker->CharacterName() + " (#" +
             std::to_string(attacker->CharacterId()) + ") hit Monster #" +
             std::to_string(target->EntityId()) + " for " + std::to_string(damage) +
             " (hp=" + std::to_string(target->CurrentHp()) + ")");
    if (killed) {
        // 指令四十七/五十三：怪物死亡（AI 停/target 清空/state=Dead）+ MonsterDeath 广播。
        KillMonster(target, attacker->CharacterId(), receivers);
    }
}

void WorldServer::SendAttackResponse(const std::shared_ptr<PlayerSession>& player,
                                     std::uint64_t requestId, bool success,
                                     CombatResultCode code, std::uint64_t targetEntityId) {
    if (!player) {
        return;
    }
    PlayerAttackResponsePayload out;
    out.requestId = requestId;
    out.success = success;
    out.resultCode = static_cast<std::uint8_t>(code);
    out.targetEntityId = targetEntityId;
    out.message = CombatResultCodeName(out.resultCode);
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::PlayerAttackResponse);
    if (EncodePlayerAttackResponse(out, packet.payload)) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::BroadcastCombatEvent(const CombatEventPayload& event,
                                       const std::vector<std::uint64_t>& receiverCharacterIds) {
    // 指令四十/四十一/四十二：只发相关玩家（观察者 ∪ 当事人），去重。
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::CombatEvent);
    if (!EncodeCombatEvent(event, packet.payload)) {
        return;
    }
    for (const auto characterId : receiverCharacterIds) {
        auto player = m_players.FindByCharacter(characterId);
        if (player) {
            SendPacketToPlayer(player, packet);
        }
    }
}

void WorldServer::KillMonster(const std::shared_ptr<MonsterEntity>& monster,
                              std::uint64_t killerCharacterId,
                              const std::vector<std::uint64_t>& observers) {
    // 指令二十三/四十七/五十三：HP=0 -> alive=false / state=Dead / target 清空 /
    // AI 停止（Patrol/Chase/Returning 全停）。
    monster->MarkDead();
    monster->SetState(MonsterState::Dead);
    monster->SetTargetCharacterId(0);
    // 阶段16 指令三十八/一百一十四：死亡 -> 逐个 StatusRemoved(TargetDied) + 清空容器。
    ClearStatusOnDeath(CombatEntityType::Monster, monster->EntityId(), monster->StatusEffects(),
                       observers);
    // 指令五十：MonsterDeath 广播（观察者集合已含 killer）。
    MonsterDeathPayload death;
    death.entityId = monster->EntityId();
    death.killerCharacterId = killerCharacterId;
    death.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::MonsterDeath);
    if (EncodeMonsterDeath(death, packet.payload)) {
        for (const auto characterId : observers) {
            auto player = m_players.FindByCharacter(characterId);
            if (player) {
                SendPacketToPlayer(player, packet);
            }
        }
    }
    // 指令五十一/八十九：死亡期间保留实体 3 秒（仍属于 visibleMonsters，继续快照
    // state=Dead）；3 秒后由 CleanupDeadMonsters 统一 RemoveMonster（指令五十四/七十七）。
    LOG_INFO("[Combat] Monster #" + std::to_string(monster->EntityId()) + " killed by player #" +
             std::to_string(killerCharacterId));
}

void WorldServer::KillPlayer(const std::shared_ptr<PlayerSession>& victim,
                             CombatEntityType killerType, std::uint64_t killerId,
                             const std::vector<std::uint64_t>& observers) {
    // 指令四十八/五十七：currentHp=0 / alive=false，不自动复活。
    // 阶段15 指令十七：死亡取消当前施法（reason=Dead，Mana 不返还——指令九十四）。
    if (victim->IsCasting()) {
        CancelActiveCast(victim, SkillCancelReason::Dead);
    }
    victim->MarkDead();
    // 阶段16 指令三十九/一百一十五：玩家死亡 -> 清除全部状态（StatusRemoved(TargetDied)）。
    {
        std::vector<std::uint64_t> statusReceivers;
        statusReceivers.push_back(victim->CharacterId());
        for (const auto characterId : observers) {
            MergeReceiver(statusReceivers, characterId);
        }
        ClearStatusOnDeath(CombatEntityType::Player, victim->CharacterId(),
                           victim->StatusEffects(), statusReceivers);
    }
    // 指令四十九：PlayerDeath 广播（观察者 ∪ 受害者本人）。
    PlayerDeathPayload death;
    death.characterId = victim->CharacterId();
    death.killerType = static_cast<std::uint8_t>(killerType);
    death.killerId = killerId;
    death.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::PlayerDeath);
    if (EncodePlayerDeath(death, packet.payload)) {
        for (const auto characterId : observers) {
            auto player = m_players.FindByCharacter(characterId);
            if (player) {
                SendPacketToPlayer(player, packet);
            }
        }
    }
    // 指令一百二十九：所有以其为 target 的怪停止攻击 -> Returning。
    for (const auto& monster : m_monsters.SnapshotMonsters()) {
        if (monster->TargetCharacterId() == victim->CharacterId()) {
            monster->SetTargetCharacterId(0);
            monster->SetState(MonsterState::Returning);
            monster->TouchStateEnterTime();
        }
    }
    LOG_INFO("[Combat] Player " + victim->CharacterName() + " (#" +
             std::to_string(victim->CharacterId()) + ") died (killer type=" +
             std::to_string(static_cast<int>(death.killerType)) + " id=" +
             std::to_string(killerId) + ")");
}

void WorldServer::ScheduleHealthSnapshotTick() {
    if (m_stopped.load()) {
        return;
    }
    m_healthTimer.expires_after(std::chrono::milliseconds(m_config.healthSnapshotIntervalMs));
    auto self = shared_from_this();
    m_healthTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->SendHealthSnapshots();
        self->ScheduleHealthSnapshotTick();
    });
}

void WorldServer::SendHealthSnapshots() {
    // 指令六十八/六十九：每 1s 给每个玩家发可见实体的 HP 纠偏（单条 EntityHealthSnapshot，
    // 不用 batch——数量不大）。
    const std::uint64_t serverTime = ServerTimeMs();
    for (const auto& player : m_players.SnapshotPlayers()) {
        // 自己（本地 HP 由客户端维护 + 此处纠偏）
        SendEntityHealthSnapshot(player, CombatEntityType::Player, player->CharacterId(),
                                 player->CurrentHp(), player->MaxHp(), player->Alive());
        // 可见玩家
        for (const auto remoteId : player->VisiblePlayers()) {
            const auto remote = m_players.FindByCharacter(remoteId);
            if (remote) {
                SendEntityHealthSnapshot(player, CombatEntityType::Player, remoteId,
                                         remote->CurrentHp(), remote->MaxHp(), remote->Alive());
            }
        }
        // 可见怪物（死亡期间仍可见，alive=false 纠偏）
        for (const auto entityId : player->VisibleMonsters()) {
            const auto monster = m_monsters.FindMonster(entityId);
            if (monster) {
                SendEntityHealthSnapshot(player, CombatEntityType::Monster, entityId,
                                         monster->CurrentHp(), monster->MaxHp(),
                                         monster->Alive());
            }
        }
        (void)serverTime;
    }
}

void WorldServer::SendEntityHealthSnapshot(const std::shared_ptr<PlayerSession>& receiver,
                                           CombatEntityType entityType, std::uint64_t entityId,
                                           std::uint32_t currentHp, std::uint32_t maxHp,
                                           bool alive) {
    EntityHealthSnapshotPayload out;
    out.entityType = static_cast<std::uint8_t>(entityType);
    out.entityId = entityId;
    out.currentHp = currentHp;
    out.maxHp = maxHp;
    out.alive = alive;
    out.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::EntityHealthSnapshot);
    if (EncodeEntityHealthSnapshot(out, packet.payload)) {
        SendPacketToPlayer(receiver, packet);
    }
}

// ---------------------------------------------------------------------------
// 阶段15：技能与施法（Skill & Ability Replication Core V0.15）
// 编排原则（指令三十六）：WorldSession 只做协议状态；SkillRegistry 提供定义；
// SkillService 提供规则；WorldServer 只负责接收->验证->扣费->执行->广播。
// ---------------------------------------------------------------------------

std::size_t WorldServer::CastingPlayerCount() const {
    std::size_t count = 0;
    for (const auto& player : m_players.SnapshotPlayers()) {
        if (player->IsCasting()) {
            ++count;
        }
    }
    return count;
}

bool WorldServer::MoveMonsterTo(std::uint64_t entityId, float x, float y) {
    // 阶段15：测试/布景辅助。位置 + spawn 点 + SpatialGrid cell 一致更新（移动即
    // "重新安家"，避免 leash 立即触发 Returning 走出布景）；必须与 AI Tick 同在
    // io 线程串行执行（grid 非线程安全）——经 m_service.Post 投递，测试侧轮询等待。
    auto monster = m_monsters.FindMonster(entityId);
    if (!monster) {
        return false;
    }
    auto self = shared_from_this();
    m_service.Post([self, entityId, x, y]() {
        if (self->m_stopped.load()) {
            return;
        }
        auto moved = self->m_monsters.FindMonster(entityId);
        if (!moved) {
            return;
        }
        moved->SetPosition(x, y);
        moved->SetSpawnPoint(x, y);
        self->m_monsterGrid.UpdateMonsterCell(moved);
    });
    return true;
}

void WorldServer::MergeReceiver(std::vector<std::uint64_t>& receivers,
                                std::uint64_t characterId) {
    if (std::find(receivers.begin(), receivers.end(), characterId) == receivers.end()) {
        receivers.push_back(characterId);
    }
}

std::vector<std::uint64_t> WorldServer::CasterObservers(std::uint64_t casterCharacterId) const {
    // 指令五十一：Started/Completed/Cancelled 广播范围 = Caster 本人 + 能看到
    // Caster 的玩家（去重，不全世界广播）。
    std::vector<std::uint64_t> receivers;
    receivers.push_back(casterCharacterId);
    for (const auto& observer : m_players.SnapshotPlayers()) {
        if (observer->CharacterId() != casterCharacterId &&
            observer->VisiblePlayers().count(casterCharacterId) != 0) {
            MergeReceiver(receivers, observer->CharacterId());
        }
    }
    return receivers;
}

void WorldServer::HandleSkillCastRequest(std::uint64_t connectionId,
                                         const legend::network::Packet& packet) {
    // 指令八十七：Malformed 只影响当前 Client（回 MalformedRequest，不断开）。
    SkillCastRequestPayload request;
    std::string decodeError;
    if (!DecodeSkillCastRequest(packet.payload.data(), packet.payload.size(), request,
                                decodeError)) {
        LOG_INFO("[Skill] Malformed SkillCastRequest from #" + std::to_string(connectionId));
        SendSkillCastResponse(nullptr, request.requestId, request.skillId, false,
                              SkillResultCode::MalformedRequest);
        return;
    }
    auto caster = m_players.FindByConnection(connectionId);
    if (!caster) {
        SendSkillCastResponse(nullptr, request.requestId, request.skillId, false,
                              SkillResultCode::NotInWorld);
        return;
    }
    // 指令八十八：未知技能 -> UnknownSkill（Mana 不变，CD 不启动）。
    const SkillDefinition* skill = m_skillRegistry.FindSkill(request.skillId);
    if (!skill) {
        SendSkillCastResponse(caster, request.requestId, request.skillId, false,
                              SkillResultCode::UnknownSkill);
        return;
    }
    // 指令七十四：重复 accepted requestId -> DuplicateRequest（不重复扣 Mana/
    // 不重复 CD/不重复伤害）；指令七十五：失败请求允许重试（不缓存）。
    if (caster->IsRecentSkillRequest(request.requestId)) {
        SendSkillCastResponse(caster, request.requestId, request.skillId, false,
                              SkillResultCode::DuplicateRequest);
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    SkillCastContext context;
    context.casterAlive = caster->Alive();
    context.casterInWorld = true;
    context.alreadyCasting = caster->IsCasting(); // 指令十五：Casting 期间拒绝一切新技能
    const auto requestTargetType = static_cast<SkillTargetType>(request.targetType);
    context.requestTargetType = requestTargetType;
    context.definitionTargetType = skill->targetType;
    context.cooldownReady = caster->IsSkillReady(request.skillId, now);
    context.currentMana = caster->CurrentMana();
    context.manaCost = skill->manaCost;

    std::shared_ptr<MonsterEntity> target;
    if (skill->targetType == SkillTargetType::Monster) {
        // 指令四十九：单体目标必须在 visibleMonsters（防远程猜 entityId）。
        if (caster->VisibleMonsters().count(request.targetEntityId) == 0) {
            SendSkillCastResponse(caster, request.requestId, request.skillId, false,
                                  SkillResultCode::InvalidTarget);
            return;
        }
        target = m_monsters.FindMonster(request.targetEntityId);
        if (!target) {
            SendSkillCastResponse(caster, request.requestId, request.skillId, false,
                                  SkillResultCode::InvalidTarget);
            return;
        }
        context.targetAlive = target->Alive();
        context.targetVisible = true; // 已验证 visibleMonsters
        context.sameMap = caster->MapId() == target->MapId();
        const float dx = caster->PositionX() - target->PositionX();
        const float dy = caster->PositionY() - target->PositionY();
        context.distanceSquared = dx * dx + dy * dy;
        context.rangeSquared = skill->range * skill->range;
    }
    // 指令三十九：验证链（CasterDead -> NotInWorld -> AlreadyCasting -> TargetDead ->
    // InvalidTarget -> DifferentMap -> OutOfRange -> Cooldown -> NotEnoughMana）。
    const SkillResultCode code = ValidateSkillCast(context);
    if (code != SkillResultCode::Success) {
        // 指令八十九/九十/九十一/九十二/九十三：失败不扣 Mana、不启动 CD、
        // 不记录 requestId。
        SendSkillCastResponse(caster, request.requestId, request.skillId, false, code);
        return;
    }

    // ---- 施法被服务器正式接受（指令十九/二十）：扣 Mana + 启动 CD + 记录请求 ----
    if (!caster->ConsumeMana(skill->manaCost)) {
        // 防御：验证与扣费之间同线程无窗口，理论不可达；保底 NotEnoughMana。
        SendSkillCastResponse(caster, request.requestId, request.skillId, false,
                              SkillResultCode::NotEnoughMana);
        return;
    }
    caster->StartSkillCooldown(request.skillId, skill->cooldownSeconds);
    caster->RememberSkillRequest(request.requestId);
    const std::uint64_t castId = m_nextCastId++; // 指令二十六：服务器单调 castId
    // 指令二十四：成功回执（accepted + 当前 Mana），先于 Started 发送。
    SendSkillCastResponse(caster, request.requestId, request.skillId, true,
                          SkillResultCode::Success);

    if (skill->castType == SkillCastType::Instant) {
        ExecuteInstantCast(caster, *skill, castId, request.targetEntityId);
    } else {
        BeginTimedCast(caster, *skill, castId, request.targetEntityId, request.requestId);
    }
}

void WorldServer::ExecuteInstantCast(const std::shared_ptr<PlayerSession>& caster,
                                     const SkillDefinition& skill, std::uint64_t castId,
                                     std::uint64_t targetEntityId) {
    // 指令三十九：Instant 也必须 Started -> Completed -> Impact -> CombatEvent
    //（统一协议，castTimeMs=0；指令八十三顺序：Completed 在 Impact/CombatEvent 之前）。
    SendSkillCastStarted(CasterObservers(caster->CharacterId()), castId,
                         caster->CharacterId(), skill.skillId,
                         static_cast<std::uint8_t>(skill.targetType), targetEntityId, 0);
    SendSkillCastCompleted(CasterObservers(caster->CharacterId()), castId,
                           caster->CharacterId(), skill.skillId,
                           static_cast<std::uint8_t>(skill.targetType), targetEntityId);
    ResolveAndApplySkillDamage(caster, skill, castId, targetEntityId,
                               skill.targetType == SkillTargetType::Monster);
}

void WorldServer::BeginTimedCast(const std::shared_ptr<PlayerSession>& caster,
                                 const SkillDefinition& skill, std::uint64_t castId,
                                 std::uint64_t targetEntityId, std::uint64_t requestId) {
    // 指令十四/四十：进入 Casting；Skill Tick 统一检查完成（指令七十七：不 new thread）。
    PendingSkillCast cast;
    cast.active = true;
    cast.castId = castId;
    cast.skillId = skill.skillId;
    cast.castRequestId = requestId;
    cast.targetType = static_cast<std::uint8_t>(skill.targetType);
    cast.targetEntityId = targetEntityId;
    cast.castStartTime = std::chrono::steady_clock::now();
    cast.castCompleteTime =
        cast.castStartTime +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<float>(skill.castTimeSeconds));
    caster->SetCasting(cast);
    SendSkillCastStarted(CasterObservers(caster->CharacterId()), castId,
                         caster->CharacterId(), skill.skillId,
                         static_cast<std::uint8_t>(skill.targetType), targetEntityId,
                         static_cast<std::uint32_t>(skill.castTimeSeconds * 1000.0f));
}

void WorldServer::CancelActiveCast(const std::shared_ptr<PlayerSession>& caster,
                                   SkillCancelReason reason) {
    if (!caster->IsCasting()) {
        return;
    }
    const PendingSkillCast cast = caster->Casting();
    caster->ClearCasting();
    // 指令九十四：取消不返还 Mana / 不清除 CD（Cooldown 已在接受时启动）。
    SendSkillCastCancelled(CasterObservers(caster->CharacterId()), cast.castId,
                           caster->CharacterId(), cast.skillId, reason);
    LOG_INFO("[Skill] Player " + caster->CharacterName() + " (#" +
             std::to_string(caster->CharacterId()) + ") cast " +
             std::to_string(cast.skillId) + " cancelled (" +
             SkillCancelReasonName(static_cast<std::uint8_t>(reason)) + ")");
}

void WorldServer::ScheduleSkillTick() {
    if (m_stopped.load()) {
        return;
    }
    m_skillTimer.expires_after(std::chrono::milliseconds(m_config.skillTickMs));
    auto self = shared_from_this();
    m_skillTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->RunSkillTick();
        self->ScheduleSkillTick();
    });
}

void WorldServer::RunSkillTick() {
    // 指令七十七/七十八/八十：50ms Skill Tick（与 AI Tick 200ms 分离），统一
    // 检查 Pending Cast 完成时间；不 new thread。
    const auto now = std::chrono::steady_clock::now();
    for (const auto& player : m_players.SnapshotPlayers()) {
        if (!player->IsCasting() || now < player->Casting().castCompleteTime) {
            continue;
        }
        PendingSkillCast cast = player->Casting();
        player->ClearCasting();
        CompleteCast(player, cast);
    }
}

void WorldServer::CompleteCast(const std::shared_ptr<PlayerSession>& caster,
                               PendingSkillCast& cast) {
    // 防御：施法完成时施法者死亡（AI 击杀发生在独立路径，正常已在 KillPlayer
    // 取消）——不再结算。
    if (!caster->Alive()) {
        return;
    }
    const SkillDefinition* skill = m_skillRegistry.FindSkill(cast.skillId);
    if (!skill) {
        return; // 定义不可达（runtime only 注册表常驻）——防御
    }
    // 指令十八/四十二/四十三：完成时重新验证目标（exists/alive/sameMap/visible/
    // range），失败 -> Cancelled(TargetInvalid)，不造成伤害。
    if (skill->targetType == SkillTargetType::Monster) {
        auto target = m_monsters.FindMonster(cast.targetEntityId);
        bool valid = target != nullptr && target->Alive() &&
                     target->MapId() == caster->MapId() &&
                     caster->VisibleMonsters().count(cast.targetEntityId) != 0;
        float dx = 0.0f;
        float dy = 0.0f;
        if (valid && target) {
            dx = caster->PositionX() - target->PositionX();
            dy = caster->PositionY() - target->PositionY();
            const float rangeSquared = skill->range * skill->range;
            valid = (dx * dx + dy * dy) <= rangeSquared; // 完成时重算服务器距离
        }
        if (!valid) {
            SendSkillCastCancelled(CasterObservers(caster->CharacterId()), cast.castId,
                                   caster->CharacterId(), cast.skillId,
                                   SkillCancelReason::TargetInvalid);
            LOG_INFO("[Skill] Player " + caster->CharacterName() + " cast " +
                     std::to_string(cast.skillId) + " cancelled (TargetInvalid at complete).");
            return;
        }
    }
    // 指令二十七：SkillCastCompleted（顺序：Completed -> Impact -> CombatEvent
    // -> MonsterDeath，指令八十三）。
    SendSkillCastCompleted(CasterObservers(caster->CharacterId()), cast.castId,
                           caster->CharacterId(), cast.skillId, cast.targetType,
                           cast.targetEntityId);
    ResolveAndApplySkillDamage(caster, *skill, cast.castId, cast.targetEntityId,
                               skill->targetType == SkillTargetType::Monster);
}

void WorldServer::ResolveAndApplySkillDamage(const std::shared_ptr<PlayerSession>& caster,
                                             const SkillDefinition& skill, std::uint64_t castId,
                                             std::uint64_t targetEntityId,
                                             bool hasTargetEntity) {
    // ---- 收集目标 ----
    std::vector<std::shared_ptr<MonsterEntity>> targets;
    if (skill.aoeRadius > 0.0f) {
        // 指令四十四：以 Caster 服务器权威位置为中心查 MonsterSpatialGrid
        //（禁止遍历全部 Monster，指令一百二十一）。
        auto candidates = m_monsterGrid.QueryNearbyMonsters(
            caster->PositionX(), caster->PositionY(), skill.aoeRadius, 0);
        targets = ResolveAoeTargets(candidates, caster->MapId(), skill.aoeRadius,
                                    skill.maxTargets);
    } else if (hasTargetEntity) {
        // 单体：完成时/即时执行时目标仍存在（CompleteCast 已重验；Instant 在
        // HandleSkillCastRequest 已验证——此处防御性复查 alive）。
        auto target = m_monsters.FindMonster(targetEntityId);
        if (target && target->Alive()) {
            targets.push_back(target);
        }
    }

    // ---- SkillImpactEvent 广播范围（指令五十二）：Caster + 能看到 Caster 的玩家
    // + 能看到至少一个受影响 Monster 的玩家（去重，不全世界广播——指令五十四）。 ----
    std::vector<std::uint64_t> receivers = CasterObservers(caster->CharacterId());

    SkillImpactEventPayload impact;
    impact.castId = castId;
    impact.skillId = skill.skillId;
    impact.casterCharacterId = caster->CharacterId();
    impact.serverTime = ServerTimeMs();

    // ---- 逐目标结算（指令三十七：damage = max(1, base + atk - def)）----
    // 阶段16：baseDamage == 0 的技能（Battle Focus）不结算伤害；
    // 命中后若目标存活 -> 施加 Definition 指定的状态效果（指令六十九：
    // 直接杀死的目标不再施加状态）。
    const bool dealsDamage = skill.baseDamage > 0;
    for (const auto& target : targets) {
        const MonsterDefinition* targetDefinition = FindMonsterDefinition(target->MonsterTypeId());
        const std::uint32_t damage =
            dealsDamage ? CalculateSkillDamage(skill.baseDamage, caster->EffectiveAttackPower(),
                                               target->EffectiveDefense())
                        : 0u;
        bool killed = false;
        if (dealsDamage) {
            killed = target->ApplyDamage(damage);
        }

        if (dealsDamage) {
            SkillImpactTarget impactTarget;
            impactTarget.entityType = static_cast<std::uint8_t>(CombatEntityType::Monster);
            impactTarget.entityId = target->EntityId();
            impactTarget.damage = damage;
            impactTarget.hpAfter = target->CurrentHp();
            impactTarget.maxHp = target->MaxHp();
            impactTarget.killed = killed;
            impact.targets.push_back(impactTarget);
            if (impact.targets.size() >= kSkillImpactMaxTargets) {
                break; // 指令三十/八十五：硬上限 16
            }
        }

        // 指令五十三：CombatEvent 继续复用阶段14 广播范围（目标怪观察者 ∪ 攻击者）。
        std::vector<std::uint64_t> combatReceivers;
        MergeReceiver(combatReceivers, caster->CharacterId());
        for (const auto& observer : m_players.SnapshotPlayers()) {
            if (observer->CharacterId() != caster->CharacterId() &&
                observer->VisibleMonsters().count(target->EntityId()) != 0) {
                MergeReceiver(combatReceivers, observer->CharacterId());
            }
        }
        // 指令五十二：能看到受影响 Monster 的玩家也进 Impact 接收者。
        for (const auto characterId : combatReceivers) {
            MergeReceiver(receivers, characterId);
        }

        if (dealsDamage) {
            CombatEventPayload event;
            event.eventId = m_nextCombatEventId++; // 指令十三：单调 eventId
            event.attackerType = static_cast<std::uint8_t>(CombatEntityType::Player);
            event.attackerId = caster->CharacterId();
            event.targetType = static_cast<std::uint8_t>(CombatEntityType::Monster);
            event.targetId = target->EntityId();
            event.damage = damage;
            event.targetHpAfter = target->CurrentHp();
            event.targetMaxHp = target->MaxHp();
            event.killed = killed;
            event.serverTime = ServerTimeMs();
            // 指令三十二/一百二十四：技能伤害来源（sourceType=Skill, sourceId=skillId）。
            event.sourceType = static_cast<std::uint8_t>(CombatSource::Skill);
            event.sourceId = skill.skillId;
            BroadcastCombatEvent(event, combatReceivers);
            LOG_INFO("[Combat] Skill " + std::to_string(skill.skillId) + " hit Monster #" +
                     std::to_string(target->EntityId()) + " for " + std::to_string(damage) +
                     " (hp=" + std::to_string(target->CurrentHp()) + ")");
        }

        // 阶段16 指令六十九：目标未死 -> 施加技能关联的状态效果。
        if (skill.applyStatusEffectId != 0 && !killed) {
            ApplySkillStatus(caster, skill, static_cast<std::uint8_t>(CombatEntityType::Monster),
                             target->EntityId(), combatReceivers);
        }
    }

    // 阶段16 指令十九：Self 技能（Battle Focus）——状态施加给施法者本人。
    if (skill.targetType == SkillTargetType::Self && skill.applyStatusEffectId != 0) {
        ApplySkillStatus(caster, skill, static_cast<std::uint8_t>(CombatEntityType::Player),
                         caster->CharacterId(), receivers);
    }

    // 指令八十三：Impact 在 CombatEvent 之后、MonsterDeath 之前发送（测试一致）。
    SendSkillImpactEvent(receivers, impact);

    // 指令八十一：技能击杀复用阶段14 KillMonster（不写第二套死亡代码）；
    // 指令八十二：No Loot / No EXP；指令八十四：多死亡各自独立 MonsterDeath。
    for (const auto& target : targets) {
        if (!target->Alive() && target->CurrentHp() == 0 &&
            target->State() != MonsterState::Dead) {
            std::vector<std::uint64_t> deathReceivers;
            MergeReceiver(deathReceivers, caster->CharacterId());
            for (const auto& observer : m_players.SnapshotPlayers()) {
                if (observer->CharacterId() != caster->CharacterId() &&
                    observer->VisibleMonsters().count(target->EntityId()) != 0) {
                    MergeReceiver(deathReceivers, observer->CharacterId());
                }
            }
            KillMonster(target, caster->CharacterId(), deathReceivers);
        }
    }
}

void WorldServer::ScheduleManaSnapshotTick() {
    if (m_stopped.load()) {
        return;
    }
    m_manaTimer.expires_after(std::chrono::milliseconds(m_config.manaSnapshotIntervalMs));
    auto self = shared_from_this();
    m_manaTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->SendManaSnapshots();
        self->ScheduleManaSnapshotTick();
    });
}

void WorldServer::SendManaSnapshots() {
    // 指令六十七：每 1s 给玩家本人发 ManaSnapshot（指令六十八：无 Regen，值恒定）。
    const std::uint64_t serverTime = ServerTimeMs();
    for (const auto& player : m_players.SnapshotPlayers()) {
        ManaSnapshotPayload out;
        out.currentMana = player->CurrentMana();
        out.maxMana = player->MaxMana();
        out.serverTime = serverTime;
        Packet packet;
        packet.header.messageId = static_cast<std::uint16_t>(MessageId::ManaSnapshot);
        if (EncodeManaSnapshot(out, packet.payload)) {
            SendPacketToPlayer(player, packet);
        }
    }
}

void WorldServer::SendSkillCastResponse(const std::shared_ptr<PlayerSession>& player,
                                        std::uint64_t requestId, SkillId skillId, bool accepted,
                                        SkillResultCode code) {
    if (!player) {
        return; // NotInWorld/Malformed 时尽力回执（无会话则丢弃）
    }
    SkillCastResponsePayload out;
    out.requestId = requestId;
    out.skillId = skillId;
    out.accepted = accepted;
    out.resultCode = static_cast<std::uint8_t>(code);
    out.currentMana = player->CurrentMana(); // 指令二十四：回执带服务器权威 Mana
    out.message = SkillResultCodeName(out.resultCode);
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::SkillCastResponse);
    if (EncodeSkillCastResponse(out, packet.payload)) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendSkillCastStarted(const std::vector<std::uint64_t>& receivers,
                                       std::uint64_t castId, std::uint64_t casterCharacterId,
                                       SkillId skillId, std::uint8_t targetType,
                                       std::uint64_t targetEntityId, std::uint32_t castTimeMs) {
    SkillCastStartedPayload out;
    out.castId = castId;
    out.casterCharacterId = casterCharacterId;
    out.skillId = skillId;
    out.targetType = targetType;
    out.targetEntityId = targetEntityId;
    out.castTimeMs = castTimeMs;
    out.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::SkillCastStarted);
    if (!EncodeSkillCastStarted(out, packet.payload)) {
        return;
    }
    for (const auto characterId : receivers) {
        auto player = m_players.FindByCharacter(characterId);
        if (player) {
            SendPacketToPlayer(player, packet);
        }
    }
}

void WorldServer::SendSkillCastCompleted(const std::vector<std::uint64_t>& receivers,
                                         std::uint64_t castId, std::uint64_t casterCharacterId,
                                         SkillId skillId, std::uint8_t targetType,
                                         std::uint64_t targetEntityId) {
    SkillCastCompletedPayload out;
    out.castId = castId;
    out.casterCharacterId = casterCharacterId;
    out.skillId = skillId;
    out.targetType = targetType;
    out.targetEntityId = targetEntityId;
    out.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::SkillCastCompleted);
    if (!EncodeSkillCastCompleted(out, packet.payload)) {
        return;
    }
    for (const auto characterId : receivers) {
        auto player = m_players.FindByCharacter(characterId);
        if (player) {
            SendPacketToPlayer(player, packet);
        }
    }
}

void WorldServer::SendSkillCastCancelled(const std::vector<std::uint64_t>& receivers,
                                         std::uint64_t castId, std::uint64_t casterCharacterId,
                                         SkillId skillId, SkillCancelReason reason) {
    SkillCastCancelledPayload out;
    out.castId = castId;
    out.casterCharacterId = casterCharacterId;
    out.skillId = skillId;
    out.reason = static_cast<std::uint8_t>(reason);
    out.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::SkillCastCancelled);
    if (!EncodeSkillCastCancelled(out, packet.payload)) {
        return;
    }
    for (const auto characterId : receivers) {
        auto player = m_players.FindByCharacter(characterId);
        if (player) {
            SendPacketToPlayer(player, packet);
        }
    }
}

void WorldServer::SendSkillImpactEvent(const std::vector<std::uint64_t>& receivers,
                                       const SkillImpactEventPayload& impact) {
    if (impact.targets.empty()) {
        return; // 无目标不广播（例如单体目标已消失的防御路径）
    }
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::SkillImpactEvent);
    if (!EncodeSkillImpactEvent(impact, packet.payload)) {
        return;
    }
    for (const auto characterId : receivers) {
        auto player = m_players.FindByCharacter(characterId);
        if (player) {
            SendPacketToPlayer(player, packet);
        }
    }
}

// ---------------------------------------------------------------------------
// 阶段16：状态效果编排（Status Effect Core V0.16）
// WorldServer 只负责编排与广播（指令一百五十一）：策略在 StatusEffectService，
// 定义在 StatusEffectRegistry，容器在 StatusEffectContainer。
// ---------------------------------------------------------------------------

std::vector<std::uint64_t> WorldServer::MonsterStatusReceivers(
    std::uint64_t monsterEntityId, std::uint64_t sourceCharacterId) const {
    // 指令五十七：Monster 状态 -> 当前能看到 Monster 的玩家 + 若 source 是玩家则
    // source 本人（去重，不全世界广播）。
    std::vector<std::uint64_t> receivers;
    for (const auto& observer : m_players.SnapshotPlayers()) {
        if (observer->VisibleMonsters().count(monsterEntityId) != 0) {
            MergeReceiver(receivers, observer->CharacterId());
        }
    }
    if (sourceCharacterId != 0) {
        MergeReceiver(receivers, sourceCharacterId);
    }
    return receivers;
}

std::vector<std::uint64_t> WorldServer::PlayerStatusReceivers(std::uint64_t characterId) const {
    // 指令五十七：Player 状态 -> 本人 + 能看到该玩家的玩家。
    std::vector<std::uint64_t> receivers;
    receivers.push_back(characterId);
    for (const auto& observer : m_players.SnapshotPlayers()) {
        if (observer->CharacterId() != characterId &&
            observer->VisiblePlayers().count(characterId) != 0) {
            MergeReceiver(receivers, observer->CharacterId());
        }
    }
    return receivers;
}

StatusApplyOutcome WorldServer::ApplyStatusToTarget(std::uint8_t targetType,
                                                    std::uint64_t targetEntityId,
                                                    StatusEffectId effectId, std::uint8_t stacks,
                                                    std::uint8_t sourceType,
                                                    std::uint64_t sourceEntityId,
                                                    std::uint32_t sourceSkillId) {
    // 阶段16：统一施加入口（技能命中 / 测试白盒）。解析目标 -> 校验 -> 策略应用 ->
    // 派生属性重算 -> 广播 Applied/Updated（指令十七：Client 不能施加）。
    const auto now = std::chrono::steady_clock::now();
    StatusApplyContext context;
    context.effectId = effectId;
    context.sourceType = sourceType;
    context.sourceEntityId = sourceEntityId;
    context.sourceSkillId = sourceSkillId;
    context.targetType = targetType;
    context.targetEntityId = targetEntityId;

    std::shared_ptr<MonsterEntity> monster;
    std::shared_ptr<PlayerSession> player;
    if (targetType == static_cast<std::uint8_t>(CombatEntityType::Monster)) {
        monster = m_monsters.FindMonster(targetEntityId);
        context.targetValid = monster != nullptr;
        context.targetAlive = monster != nullptr && monster->Alive();
    } else {
        player = m_players.FindByCharacter(targetEntityId);
        context.targetValid = player != nullptr;
        context.targetAlive = player != nullptr && player->Alive();
    }

    StatusEffectContainer& container =
        targetType == static_cast<std::uint8_t>(CombatEntityType::Monster)
            ? (monster ? monster->StatusEffects() : m_detachedStatusContainer)
            : (player ? player->StatusEffects() : m_detachedStatusContainer);
    StatusApplyOutcome outcome =
        ApplyEffect(container, m_statusRegistry, context, m_nextStatusInstanceId++, now);
    if (outcome.result == StatusApplyResult::Applied ||
        outcome.result == StatusApplyResult::Refreshed ||
        outcome.result == StatusApplyResult::StackAdded ||
        outcome.result == StatusApplyResult::AtMaxStacksRefreshed) {
        RecalculateTargetDerivedStats(static_cast<CombatEntityType>(targetType), targetEntityId);
        if (outcome.result == StatusApplyResult::Applied) {
            const auto* definition = m_statusRegistry.FindEffect(effectId);
            SendStatusApplied(MonsterOrPlayerStatusReceivers(targetType, targetEntityId,
                                                             sourceType, sourceEntityId),
                              *outcome.effect, definition ? definition->durationMs : 0u);
        } else {
            SendStatusUpdated(MonsterOrPlayerStatusReceivers(targetType, targetEntityId,
                                                             sourceType, sourceEntityId),
                              *outcome.effect);
        }
        LOG_INFO("[Status] effect " + std::to_string(effectId) + " x" +
                 std::to_string(outcome.effect->Stacks()) + " on target " +
                 std::to_string(targetEntityId) + " -> " +
                 StatusApplyResultName(static_cast<std::uint8_t>(outcome.result)));
    }
    return outcome;
}

std::vector<std::uint64_t> WorldServer::MonsterOrPlayerStatusReceivers(std::uint8_t targetType,
                                                                       std::uint64_t targetEntityId,
                                                                       std::uint8_t sourceType,
                                                                       std::uint64_t sourceEntityId) {
    if (targetType == static_cast<std::uint8_t>(CombatEntityType::Monster)) {
        // 指令五十七：source 为玩家时包含 source 本人。
        return MonsterStatusReceivers(targetEntityId,
                                      sourceType == static_cast<std::uint8_t>(
                                                        CombatEntityType::Player)
                                          ? sourceEntityId
                                          : 0);
    }
    return PlayerStatusReceivers(targetEntityId);
}

void WorldServer::ApplySkillStatus(const std::shared_ptr<PlayerSession>& caster,
                                   const SkillDefinition& skill, std::uint8_t targetType,
                                   std::uint64_t targetEntityId,
                                   const std::vector<std::uint64_t>& combatReceivers) {
    // 阶段16 指令十八：技能命中 -> 施加 applyStatusEffectId（applyStatusStacks 层，
    // 逐次应用以支持 stacks>1 的定义扩展）。
    (void)combatReceivers; // 广播范围由 MonsterOrPlayerStatusReceivers 统一决定
    for (std::uint8_t i = 0; i < skill.applyStatusStacks; ++i) {
        ApplyStatusToTarget(targetType, targetEntityId, skill.applyStatusEffectId, 1,
                            static_cast<std::uint8_t>(CombatEntityType::Player),
                            caster->CharacterId(), skill.skillId);
    }
}

void WorldServer::RecalculateTargetDerivedStats(CombatEntityType targetType,
                                                std::uint64_t targetEntityId) {
    // 阶段16 指令二十五：仅在状态 Applied/stack 变化/Removed/Expired 时重算。
    if (targetType == CombatEntityType::Monster) {
        auto monster = m_monsters.FindMonster(targetEntityId);
        if (monster) {
            const auto stats = RecalculateDerivedStats(monster->BaseAttackPower(),
                                                       monster->BaseDefense(),
                                                       monster->BaseMoveSpeed(),
                                                       monster->StatusEffects(), m_statusRegistry);
            monster->SetEffectiveCombatStats(stats.attackPower, stats.defense, stats.moveSpeed);
        }
        return;
    }
    auto player = m_players.FindByCharacter(targetEntityId);
    if (player) {
        const auto stats =
            RecalculateDerivedStats(player->BaseAttackPower(), player->BaseDefense(),
                                    player->BaseMoveSpeed(), player->StatusEffects(),
                                    m_statusRegistry);
        player->SetEffectiveCombatStats(stats.attackPower, stats.defense, stats.moveSpeed);
    }
}

void WorldServer::ScheduleStatusTick() {
    if (m_stopped.load()) {
        return;
    }
    m_statusTimer.expires_after(std::chrono::milliseconds(m_config.statusTickMs));
    auto self = shared_from_this();
    m_statusTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->RunStatusTick();
        self->ScheduleStatusTick();
    });
}

void WorldServer::RunStatusTick() {
    // 阶段16 指令四十/四十一：100ms 统一扫描在线 Player + 活着 Monster 的状态容器
    //（不建 per-status Timer）；DOT Tick -> CombatEvent -> 死亡 -> 清状态。
    const auto now = std::chrono::steady_clock::now();
    for (const auto& player : m_players.SnapshotPlayers()) {
        if (player->StatusEffects().Empty()) {
            continue;
        }
        // 阶段16：玩家当前无 DOT 类状态（Battle Focus 无 Tick）；保留过期路径。
        const auto expired = ExpireEffects(player->StatusEffects(), now);
        for (const auto& effect : expired) {
            RecalculateTargetDerivedStats(CombatEntityType::Player, player->CharacterId());
            SendStatusRemoved(PlayerStatusReceivers(player->CharacterId()), effect,
                              StatusRemovedReason::Expired);
            LOG_INFO("[Status] effect " + std::to_string(effect.EffectId()) +
                     " expired on player #" + std::to_string(player->CharacterId()));
        }
    }
    for (const auto& monster : m_monsters.SnapshotMonsters()) {
        if (!monster->Alive() || monster->StatusEffects().Empty()) {
            continue;
        }
        auto onDamage = [this](const CombatEventPayload& event,
                               const std::vector<std::uint64_t>& receivers) {
            BroadcastCombatEvent(event, receivers);
        };
        auto onKilled = [this, &monster](StatusEffectId effectId, std::uint64_t killerId) {
            (void)effectId;
            // 指令三十七/七十一：DOT 击杀复用 KillMonster，killer = 原始施加者。
            std::vector<std::uint64_t> receivers =
                MonsterStatusReceivers(monster->EntityId(), killerId);
            KillMonster(monster, killerId, receivers);
        };
        TickStatusContainer(monster->StatusEffects(), CombatEntityType::Monster,
                            monster->EntityId(), onDamage, onKilled, now);
        const auto expired = ExpireEffects(monster->StatusEffects(), now);
        for (const auto& effect : expired) {
            RecalculateTargetDerivedStats(CombatEntityType::Monster, monster->EntityId());
            SendStatusRemoved(MonsterStatusReceivers(monster->EntityId(), 0), effect,
                              StatusRemovedReason::Expired);
        }
    }
}

void WorldServer::TickStatusContainer(
    StatusEffectContainer& container, CombatEntityType targetType, std::uint64_t targetEntityId,
    const std::function<void(const CombatEventPayload&,
                             const std::vector<std::uint64_t>&)>& onDamage,
    const std::function<void(StatusEffectId, std::uint64_t)>& onKilled,
    std::chrono::steady_clock::time_point now) {
    // 指令四十二/四十三：DOT due-tick 结算（每跳独立 CombatEvent）；死亡即中断。
    for (auto& [effectId, effect] : container.All()) {
        const StatusEffectDefinition* definition = m_statusRegistry.FindEffect(effectId);
        if (!definition) {
            continue;
        }
        const std::uint32_t ticks = TickEffect(effect, *definition, now);
        if (ticks == 0) {
            continue;
        }
        const std::uint32_t damage = ComputeDotDamage(*definition, effect.Stacks());
        if (damage == 0) {
            continue;
        }
        for (std::uint32_t tick = 0; tick < ticks; ++tick) {
            // 阶段16：DOT 目标仅 Monster（Player 无 DOT 来源）。
            if (targetType != CombatEntityType::Monster) {
                continue;
            }
            auto monster = m_monsters.FindMonster(targetEntityId);
            if (!monster || !monster->Alive()) {
                return; // 已死亡/移除：停止后续 Tick
            }
            const bool killed = monster->ApplyDamage(damage);
            CombatEventPayload event;
            event.eventId = m_nextCombatEventId++;
            event.attackerType = effect.SourceType();
            event.attackerId = effect.SourceEntityId(); // 指令七十一：原始施加者
            event.targetType = static_cast<std::uint8_t>(CombatEntityType::Monster);
            event.targetId = targetEntityId;
            event.damage = damage;
            event.targetHpAfter = monster->CurrentHp();
            event.targetMaxHp = monster->MaxHp();
            event.killed = killed;
            event.serverTime = ServerTimeMs();
            // 指令三十六/一百三十：DOT 来源（sourceType=StatusEffect, sourceId=effectId）。
            event.sourceType = static_cast<std::uint8_t>(CombatSource::StatusEffect);
            event.sourceId = effectId;
            std::vector<std::uint64_t> receivers =
                MonsterStatusReceivers(targetEntityId, effect.SourceEntityId());
            onDamage(event, receivers);
            LOG_INFO("[Combat] DOT " + std::to_string(effectId) + " hit Monster #" +
                     std::to_string(targetEntityId) + " for " + std::to_string(damage) +
                     " (hp=" + std::to_string(monster->CurrentHp()) + ")");
            if (killed) {
                onKilled(effectId, effect.SourceEntityId());
                return; // 死亡后立即清状态并停止本容器 Tick
            }
        }
    }
}

void WorldServer::ScheduleStatusSnapshotTick() {
    if (m_stopped.load()) {
        return;
    }
    m_statusSnapshotTimer.expires_after(
        std::chrono::milliseconds(m_config.statusSnapshotIntervalMs));
    auto self = shared_from_this();
    m_statusSnapshotTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->SendStatusSnapshots();
        self->ScheduleStatusSnapshotTick();
    });
}

void WorldServer::SendStatusSnapshots() {
    // 阶段16 指令五十九/六十：每 2s 对每个玩家发自身 + 可见玩家 + 可见怪物的状态
    // 快照；绝不发全世界状态。
    const auto now = std::chrono::steady_clock::now();
    for (const auto& player : m_players.SnapshotPlayers()) {
        SendStatusSnapshotFor(player, CombatEntityType::Player, player->CharacterId(),
                              player->StatusEffects(), now);
        for (const auto remoteId : player->VisiblePlayers()) {
            const auto remote = m_players.FindByCharacter(remoteId);
            if (remote) {
                SendStatusSnapshotFor(player, CombatEntityType::Player, remoteId,
                                      remote->StatusEffects(), now);
            }
        }
        for (const auto entityId : player->VisibleMonsters()) {
            const auto monster = m_monsters.FindMonster(entityId);
            if (monster) {
                SendStatusSnapshotFor(player, CombatEntityType::Monster, entityId,
                                      monster->StatusEffects(), now);
            }
        }
    }
}

void WorldServer::SendStatusSnapshotFor(const std::shared_ptr<PlayerSession>& receiver,
                                        CombatEntityType targetType, std::uint64_t targetEntityId,
                                        const StatusEffectContainer& container,
                                        std::chrono::steady_clock::time_point now) {
    StatusEffectSnapshotPayload payload;
    payload.targetType = static_cast<std::uint8_t>(targetType);
    payload.targetEntityId = targetEntityId;
    payload.serverTime = ServerTimeMs();
    for (const auto* effect : container.Snapshot()) {
        if (payload.effects.size() >= kStatusEffectMaxSnapshotCount) {
            break; // 指令五十六：上限 32
        }
        StatusEffectSnapshotEntry entry;
        entry.instanceId = effect->InstanceId();
        entry.effectId = effect->EffectId();
        entry.stacks = effect->Stacks();
        const auto remaining =
            std::chrono::duration_cast<std::chrono::milliseconds>(effect->ExpireTime() - now);
        entry.remainingMs = remaining.count() > 0 ? static_cast<std::uint32_t>(remaining.count())
                                                  : 0u;
        payload.effects.push_back(entry);
    }
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::StatusEffectSnapshot);
    if (EncodeStatusEffectSnapshot(payload, packet.payload)) {
        SendPacketToPlayer(receiver, packet);
    }
}

void WorldServer::SendStatusApplied(const std::vector<std::uint64_t>& receivers,
                                    const ActiveStatusEffect& effect, std::uint32_t durationMs) {
    StatusEffectAppliedPayload payload;
    payload.instanceId = effect.InstanceId();
    payload.effectId = effect.EffectId();
    payload.targetType = effect.TargetType();
    payload.targetEntityId = effect.TargetEntityId();
    payload.sourceType = effect.SourceType();
    payload.sourceEntityId = effect.SourceEntityId();
    payload.sourceSkillId = effect.SourceSkillId();
    payload.stacks = effect.Stacks();
    payload.durationMs = durationMs;
    payload.remainingMs = durationMs;
    payload.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::StatusEffectApplied);
    if (EncodeStatusEffectApplied(payload, packet.payload)) {
        for (const auto characterId : receivers) {
            auto player = m_players.FindByCharacter(characterId);
            if (player) {
                SendPacketToPlayer(player, packet);
            }
        }
    }
}

void WorldServer::SendStatusUpdated(const std::vector<std::uint64_t>& receivers,
                                    const ActiveStatusEffect& effect) {
    // 指令五十三：Updated 携带服务器权威 remainingMs。
    const auto now = std::chrono::steady_clock::now();
    const auto remaining =
        std::chrono::duration_cast<std::chrono::milliseconds>(effect.ExpireTime() - now);
    StatusEffectUpdatedPayload payload;
    payload.instanceId = effect.InstanceId();
    payload.effectId = effect.EffectId();
    payload.targetType = effect.TargetType();
    payload.targetEntityId = effect.TargetEntityId();
    payload.stacks = effect.Stacks();
    payload.remainingMs =
        remaining.count() > 0 ? static_cast<std::uint32_t>(remaining.count()) : 0u;
    payload.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::StatusEffectUpdated);
    if (EncodeStatusEffectUpdated(payload, packet.payload)) {
        for (const auto characterId : receivers) {
            auto player = m_players.FindByCharacter(characterId);
            if (player) {
                SendPacketToPlayer(player, packet);
            }
        }
    }
}

void WorldServer::SendStatusRemoved(const std::vector<std::uint64_t>& receivers,
                                    const ActiveStatusEffect& effect,
                                    StatusRemovedReason reason) {
    StatusEffectRemovedPayload payload;
    payload.instanceId = effect.InstanceId();
    payload.effectId = effect.EffectId();
    payload.targetType = effect.TargetType();
    payload.targetEntityId = effect.TargetEntityId();
    payload.reason = static_cast<std::uint8_t>(reason);
    payload.serverTime = ServerTimeMs();
    SendStatusRemovedToPayload(receivers, payload);
}

void WorldServer::SendStatusRemovedToPayload(const std::vector<std::uint64_t>& receivers,
                                             const StatusEffectRemovedPayload& payload) {
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::StatusEffectRemoved);
    if (EncodeStatusEffectRemoved(payload, packet.payload)) {
        for (const auto characterId : receivers) {
            auto player = m_players.FindByCharacter(characterId);
            if (player) {
                SendPacketToPlayer(player, packet);
            }
        }
    }
}

void WorldServer::ClearStatusOnDeath(CombatEntityType targetType, std::uint64_t targetEntityId,
                                     StatusEffectContainer& container,
                                     const std::vector<std::uint64_t>& receivers) {
    // 阶段16 指令三十八/三十九/八十：死亡 -> 逐个 StatusRemoved(TargetDied) -> 清空。
    if (container.Empty()) {
        return;
    }
    for (const auto* effect : container.Snapshot()) {
        SendStatusRemoved(receivers, *effect, StatusRemovedReason::TargetDied);
    }
    container.Clear();
    (void)targetType;
    (void)targetEntityId;
    LOG_INFO("[Status] target " + std::to_string(targetEntityId) +
             " died -> status cleared");
}

void WorldServer::SendMonsterSpawn(const std::shared_ptr<PlayerSession>& receiver,
                                   const std::shared_ptr<MonsterEntity>& monster) {
    const MonsterDefinition* definition = FindMonsterDefinition(monster->MonsterTypeId());
    MonsterSpawnPayload payload;
    payload.entityId = monster->EntityId();
    payload.monsterTypeId = monster->MonsterTypeId();
    payload.name = definition ? definition->name : "Monster";
    payload.level = definition ? definition->level : 1;
    payload.mapId = monster->MapId();
    payload.positionX = monster->PositionX();
    payload.positionY = monster->PositionY();
    payload.state = static_cast<std::uint8_t>(monster->State());
    payload.serverTime = ServerTimeMs();
    // 阶段14 指令十五：MonsterSpawn 携带 HP。
    payload.currentHp = monster->CurrentHp();
    payload.maxHp = monster->MaxHp();
    payload.alive = monster->Alive();
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::MonsterSpawn);
    if (EncodeMonsterSpawn(payload, out.payload)) {
        SendPacketToPlayer(receiver, out);
    }
}

void WorldServer::SendMonsterDespawn(const std::shared_ptr<PlayerSession>& receiver,
                                     std::uint64_t monsterEntityId, MonsterDespawnReason reason) {
    MonsterDespawnPayload payload;
    payload.entityId = monsterEntityId;
    payload.reason = static_cast<std::uint8_t>(reason);
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::MonsterDespawn);
    if (EncodeMonsterDespawn(payload, out.payload)) {
        SendPacketToPlayer(receiver, out);
    }
}

void WorldServer::SendMonsterBatches(const std::shared_ptr<PlayerSession>& player,
                                     std::uint64_t serverTime) {
    // 指令四十九/五十：只发 visibleMonsters（禁止全地图广播）；
    // 指令二十/二十一：entry {entityId,x,y,state,target}；指令二十二：单包 <=128 拆包。
    std::vector<MonsterSnapshotEntry> entries;
    entries.reserve(player->VisibleMonsterCount());
    for (const auto entityId : player->VisibleMonsters()) {
        const auto monster = m_monsters.FindMonster(entityId);
        if (!monster) {
            continue; // 可见集短暂残留（下个 AOI tick 清理）
        }
        entries.push_back({monster->EntityId(), monster->PositionX(), monster->PositionY(),
                           static_cast<std::uint8_t>(monster->State()),
                           monster->TargetCharacterId(), monster->CurrentHp(), monster->MaxHp(),
                           monster->Alive()});
    }
    for (std::size_t offset = 0; offset < entries.size(); offset += kMonsterBatchMaxMonsters) {
        MonsterBatchSnapshotPayload batch;
        batch.serverTime = serverTime;
        const auto begin = entries.begin() + static_cast<std::ptrdiff_t>(offset);
        const auto end = entries.begin() +
                         static_cast<std::ptrdiff_t>(
                             std::min(offset + kMonsterBatchMaxMonsters, entries.size()));
        batch.monsters.assign(begin, end);
        Packet out;
        out.header.messageId = static_cast<std::uint16_t>(MessageId::MonsterBatchSnapshot);
        if (EncodeMonsterBatchSnapshot(batch, out.payload)) {
            SendPacketToPlayer(player, out);
        }
    }
}

} // namespace legend::world
