#include "Server/WorldServer/WorldServer.h"

#include "Engine/Debug/Logger.h"
#include "Server/LoginServer/Account/Database/DatabaseSchema.h"
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
      m_aoiTimer(service.Io()) {}

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
    m_aoiTimer.cancel(); // 指令六十九：Stop 时 cancel AOI tick
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
    if (session->State() == WorldSessionState::InWorld &&
        static_cast<MessageId>(packet.header.messageId) == MessageId::PlayerMoveInput) {
        HandleMoveInput(connectionId, packet);
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
    if (WorldMapManager::ApplyMoveInput(*player, input.inputSequence, input.directionX,
                                        input.directionY, input.deltaTime)) {
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

} // namespace legend::world
