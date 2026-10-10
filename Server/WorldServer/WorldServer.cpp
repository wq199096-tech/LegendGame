#include "Server/WorldServer/WorldServer.h"

#include "Engine/Debug/Logger.h"
#include "Engine/Entity/Direction8.h"
#include "Engine/Math/Vector2.h"
#include "Server/LoginServer/Account/Database/DatabaseSchema.h"
#include "Server/WorldServer/Combat/CombatService.h"
#include "Server/WorldServer/Combat/DamageCalculator.h"
#include "Server/WorldServer/Item/LootTableRegistry.h"
#include "Server/WorldServer/Map/MapRegistry.h"
#include "Server/WorldServer/Map/RespawnService.h"
#include "Server/WorldServer/Monster/MonsterDefinitionRegistry.h"
#include "Server/WorldServer/Monster/MonsterSpawnRegistry.h"
#include "Server/WorldServer/Npc/NpcRegistry.h"
#include "Server/WorldServer/Npc/ShopService.h"
#include "Server/WorldServer/Npc/TeleportService.h"
#include "Server/WorldServer/Portal/PortalRegistry.h"
#include "Server/WorldServer/Quest/QuestRegistry.h"
#include "Shared/Combat/CombatProtocol.h"
#include "Shared/Combat/CombatTypes.h"
#include "Shared/GameData/GameDataJson.h"
#include "Shared/InternalProtocol/PersistenceMessages.h"
#include "Shared/Monster/MonsterProtocol.h"
#include "Shared/Monster/MonsterSpawnDefinition.h"
#include "Shared/Monster/MonsterTypes.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/Protocol.h"
#include "Shared/World/WorldError.h"
#include "Shared/World/WorldProtocol.h"
#include "Shared/World/WorldTypes.h"
#include "Shared/WorldData/WorldDataJson.h"
#include "Shared/WorldMap/MapTypes.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <random>

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
      m_statusSnapshotTimer(service.Io()),
      m_respawnTimer(service.Io()),
      m_progressionTimer(service.Io()),
      m_itemDropTimer(service.Io()),
      m_questSnapshotTimer(service.Io()),
      m_mapTransition(*this) {}

bool WorldServer::Start(std::string& error) {
    // 阶段11 指令二十二/二十五：WorldServer 直接打开同一 SQLite（WAL 由
    // Database::Open 统一开启）；Schema 幂等初始化（World 先于 Login 启动也可）。
    // 阶段25.5：正式部署（dbPort!=0）改走 DbServer RPC——DB 文件归 DbServer 所有，
    // World io 线程零 SQLite；dbPort==0 保留 legacy 本地 DB（隔离测试）。
    std::error_code fsError;
    const std::filesystem::path dbPath(m_config.databasePath);
    if (dbPath.has_parent_path()) {
        std::filesystem::create_directories(dbPath.parent_path(), fsError);
    }
    if (m_config.dbPort != 0) {
        legend::server::PersistenceClient::Config persistence;
        persistence.host = m_config.dbHost;
        persistence.port = m_config.dbPort;
        persistence.serviceType = legend::internal::ServiceType::WorldServer;
        persistence.instanceId = "world-persistence";
        persistence.serviceToken = m_config.serviceToken;
        persistence.requestTimeout = m_config.dbTimeout;
        m_persistence = std::make_shared<legend::server::PersistenceClient>(m_service,
                                                                           persistence);
        m_persistence->Start();
    } else if (!m_database.Open(m_config.databasePath, error)) {
        error = "world database open failed: " + error;
        return false;
    }
    // 阶段22 22.11/22.12/23.23：世界数据加载。
    // 目录存在 → LoadWorldData + ValidateWorldData（错误明确拒绝启动，不静默回退）；
    // 目录不存在 → 出厂默认（MakeDefaultWorldData——开发/测试环境；日志提示）。
    // 加载后补跨系统校验（spawn zone 边界/quest/shop/teleport 引用）。
    {
        const std::string& dataDir = m_config.worldDataDir;
        if (std::filesystem::exists(std::filesystem::path(dataDir))) {
            WorldDataSet data;
            std::string dataError;
            if (!LoadWorldData(dataDir, data, dataError)) {
                error = "world data load failed from '" + dataDir + "': " + dataError;
                m_database.Close();
                return false;
            }
            if (!ValidateWorldData(data, dataError)) {
                error = "world data validation failed: " + dataError;
                m_database.Close();
                return false;
            }
            MapRegistry::LoadFromDefinitions(std::move(data.maps));
            NpcRegistry::LoadFromDefinitions(std::move(data.npcs),
                                             std::move(data.dialogues));
            MonsterSpawnRegistry::LoadFromDefinitions(std::move(data.monsterSpawns));
            PortalRegistry::LoadFromDefinitions(std::move(data.portals));
            LOG_INFO("[World] World data loaded from '" + dataDir + "'.");
        } else {
            MapRegistry::LoadDefaults();
            NpcRegistry::LoadDefaults();
            MonsterSpawnRegistry::LoadDefaults();
            PortalRegistry::LoadDefaults();
            LOG_WARN("[World] World data dir '" + dataDir +
                     "' not found; using built-in default world data.");
        }
    }
    // 阶段23 23.22/23.23：Game 数据加载（items/monsters/skills/statuses/quests/
    // shops/teleports/loot_tables）。与 World 数据同一策略：目录存在 → Load+
    // ValidateGameData（交叉引用含 World NPC/Map/Spawn；失败拒绝启动）；
    // 不存在 → 出厂默认 + LOG_WARN。
    // 23.19：启动后计算 World/Game 数据 hash 并打印（定位配置错版本）。
    {
        const std::string& gameDataDir = m_config.gameDataDir;
        if (std::filesystem::exists(std::filesystem::path(gameDataDir))) {
            GameDataSet gameData;
            std::string gameError;
            if (!LoadGameData(gameDataDir, gameData, gameError)) {
                error = "game data load failed from '" + gameDataDir + "': " + gameError;
                m_database.Close();
                return false;
            }
            // 交叉引用（23.11）需要 World 数据——World 已在上面加载进各 Registry，
            // 这里重建只读视图（从 Registry 汇回，避免二次读盘）。
            WorldDataSet worldView;
            worldView.maps = MapRegistry::Instance().AllMaps();
            worldView.npcs = NpcRegistry::Instance().AllNpcs();
            worldView.monsterSpawns = MonsterSpawnRegistry::Instance().AllSpawns();
            if (!ValidateGameData(gameData, worldView, gameError)) {
                error = "game data validation failed: " + gameError;
                m_database.Close();
                return false;
            }
            m_itemRegistry.LoadFromDefinitions(std::move(gameData.items));
            MonsterDefinitionRegistry::LoadFromDefinitions(std::move(gameData.monsters));
            m_skillRegistry.LoadFromDefinitions(std::move(gameData.skills));
            m_statusRegistry.LoadFromDefinitions(std::move(gameData.statuses));
            QuestRegistry::LoadFromDefinitions(std::move(gameData.quests));
            ShopRegistry::LoadFromDefinitions(std::move(gameData.shops));
            TeleportRegistry::LoadFromDefinitions(std::move(gameData.teleports));
            LootTableRegistry::LoadFromDefinitions(std::move(gameData.lootTables));
            LOG_INFO("[World] Game data loaded from '" + gameDataDir + "'.");
        } else {
            m_itemRegistry.LoadDefaults();
            MonsterDefinitionRegistry::LoadDefaults();
            m_skillRegistry.LoadDefaults();
            m_statusRegistry.LoadDefaults();
            QuestRegistry::LoadDefaults();
            ShopRegistry::LoadDefaults();
            TeleportRegistry::LoadDefaults();
            LootTableRegistry::LoadDefaults();
            LOG_WARN("[World] Game data dir '" + gameDataDir +
                     "' not found; using built-in default game data.");
        }
        // 23.19：数据快照 hash（FNV-1a over MakeDefaultGameData 序列化大小 +
        // 各 Registry 计数——低成本可复现指纹；完整 hash 由 Data 文件内容决定）。
        std::uint64_t dataHash = 1469598103934665603ull;
        const auto mix = [&dataHash](const void* bytes, std::size_t len) {
            const auto* p = static_cast<const unsigned char*>(bytes);
            for (std::size_t i = 0; i < len; ++i) {
                dataHash ^= p[i];
                dataHash *= 1099511628211ull;
            }
        };
        const auto mixSize = [&mix](std::size_t v) { mix(&v, sizeof(v)); };
        mixSize(MapRegistry::Instance().Count());
        mixSize(NpcRegistry::Instance().Count());
        mixSize(PortalRegistry::Instance().Count());
        mixSize(MonsterSpawnRegistry::Instance().Count());
        mixSize(m_itemRegistry.Count());
        mixSize(MonsterDefinitionRegistry::Instance().All().size());
        mixSize(m_skillRegistry.Count());
        mixSize(m_statusRegistry.Count());
        mixSize(QuestRegistry::Instance().Count());
        mixSize(LootTableRegistry::Instance().All().size());
        char hashLine[96];
        std::snprintf(hashLine, sizeof(hashLine), "[World] Data hash: world+game=%016llx",
                      static_cast<unsigned long long>(dataHash));
        LOG_INFO(hashLine);
    }
    if (m_config.dbPort == 0) {
        // legacy 本地 DB：Schema 幂等初始化（RPC 模式 Schema 由 DbServer 负责）。
        if (!legend::account::InitializeSchema(m_database, error)) {
            error = "world database schema init failed: " + error;
            m_database.Close();
            return false;
        }
    }
    // 阶段19 指令十二：启动时校验任务定义（questId/objectiveId 唯一、前置存在、
    // requiredCount>0、奖励 Item 存在 ItemRegistry）。
    if (!QuestRegistry::Instance().ValidateDefinitions(&m_itemRegistry, error)) {
        error = "quest registry validation failed: " + error;
        m_database.Close();
        return false;
    }
    // 阶段20：启动校验 NPC 定义（引用任务/shop/teleport/dialogue 有效）+ 生成 4 个 NPC。
    if (!NpcRegistry::Instance().ValidateNpcs(QuestRegistry::Instance(), error)) {
        error = "npc registry validation failed: " + error;
        m_database.Close();
        return false;
    }
    // 阶段21 指令七/十四：启动校验地图与 Portal 定义 + 生成 Portal。
    if (!MapRegistry::Instance().ValidateMaps(error)) {
        error = "map registry validation failed: " + error;
        m_database.Close();
        return false;
    }
    if (!PortalRegistry::Instance().ValidatePortals(MapRegistry::Instance(), error)) {
        error = "portal registry validation failed: " + error;
        m_database.Close();
        return false;
    }
    // 阶段22 22.12：刷怪区配置校验（monster 类型/zone 完全在地图边界内）。
    if (!MonsterSpawnRegistry::Instance().ValidateSpawns(MapRegistry::Instance(), error)) {
        error = "monster spawn registry validation failed: " + error;
        m_database.Close();
        return false;
    }
    SpawnInitialPortals();
    SpawnInitialNpcs();
    if (m_config.dbPort == 0) {
        m_dbWorker.Start();
        if (!m_server->Listen(m_config.listenPort, error)) {
            m_dbWorker.Stop();
            m_database.Close();
            return false;
        }
    } else if (!m_server->Listen(m_config.listenPort, error)) {
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
    ScheduleRespawnTick();    // 阶段17 指令二十四：Respawn Tick 250ms
    ScheduleProgressionSnapshotTick(); // 阶段17 指令十五：30s ProgressionSnapshot
    // 阶段18 指令十一：DropRoller（Config 在 Start 前设置完成——固定 seed 可复现）。
    m_dropRoller = m_config.dropRollerSeed != 0
                       ? std::make_unique<SeededDropRoller>(m_config.dropRollerSeed)
                       : std::make_unique<SeededDropRoller>(
                             std::random_device{}());
    ScheduleItemDropTick();   // 阶段18 指令四十三：Drop cleanup Tick 500ms
    ScheduleQuestSnapshotTick(); // 阶段19 指令四十七：QuestSnapshot 纠偏 10s
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
    m_respawnTimer.cancel();    // 阶段17：停止 Respawn Timer
    m_progressionTimer.cancel(); // 阶段17：停止成长快照 timer
    m_itemDropTimer.cancel();   // 阶段18：停止 Drop cleanup Timer
    m_questSnapshotTimer.cancel(); // 阶段19：停止 QuestSnapshot 纠偏 Timer
    // 阶段18 指令四十九：World Drop runtime-only，重启不持久化（ServerCleanup）。
    m_itemDrops.RemoveAll();
    m_nextItemDropId = 1;
    // 阶段17 指令二十九：重启 Respawn Queue 不持久化，全部清空（slot 重新满怪）。
    m_respawnManager.Reset();
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
    if (m_persistence) {
        // 阶段25.5：Graceful Shutdown——等待在途持久化 RPC 完成后再断开（防丢存档）。
        m_persistence->Drain(std::chrono::milliseconds(5000));
        m_persistence->Stop();
        m_persistence.reset();
    }
    m_dbWorker.Stop(); // 等待全部保存任务完成（指令一百一十二；RPC 模式未启动直接返回）
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
    Packet leavePacket;
    if (session->TakePendingLeaveWorld(leavePacket)) {
        HandleLeaveWorldRequest(connectionId, leavePacket); // 阶段26 指令十七
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
        } else if (messageId == MessageId::ItemPickupRequest) {
            HandleItemPickupRequest(connectionId, packet); // 阶段18 指令二十一
        } else if (messageId == MessageId::EquipItemRequest) {
            HandleEquipItemRequest(connectionId, packet);  // 阶段18 指令二十七
        } else if (messageId == MessageId::UnequipItemRequest) {
            HandleUnequipItemRequest(connectionId, packet); // 阶段18 指令三十四
        } else if (messageId == MessageId::QuestAcceptRequest) {
            HandleQuestAcceptRequest(connectionId, packet); // 阶段19 指令十九
        } else if (messageId == MessageId::QuestTurnInRequest) {
            HandleQuestTurnInRequest(connectionId, packet); // 阶段19 指令三十三
        } else if (messageId == MessageId::QuestAbandonRequest) {
            HandleQuestAbandonRequest(connectionId, packet); // 阶段19 指令四十三
        } else if (messageId == MessageId::NpcInteractRequest) {
            HandleNpcInteractRequest(connectionId, packet); // 阶段20 指令十八
        } else if (messageId == MessageId::DialogueOptionRequest) {
            HandleDialogueOptionRequest(connectionId, packet); // 阶段20 指令二十五
        } else if (messageId == MessageId::ShopOpenRequest) {
            HandleShopOpenRequest(connectionId, packet); // 阶段20 指令三十九
        } else if (messageId == MessageId::ShopBuyRequest) {
            HandleShopBuyRequest(connectionId, packet); // 阶段20 指令四十二
        } else if (messageId == MessageId::ShopSellRequest) {
            HandleShopSellRequest(connectionId, packet); // 阶段20 指令四十九
        } else if (messageId == MessageId::TeleportRequest) {
            HandleTeleportRequest(connectionId, packet); // 阶段20 指令六十三
        } else if (messageId == MessageId::PortalUseRequest) {
            HandlePortalUseRequest(connectionId, packet); // 阶段21 指令十九/二十一
        } else if (messageId == MessageId::RespawnRequest) {
            HandleRespawnRequest(connectionId, packet); // 阶段21 指令三十三
        } else if (messageId == MessageId::ChatSendRequest) {
            HandleChatSendRequest(connectionId, packet); // Stage27 指令十三
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
        // Stage27 指令二十八：断线立即清理在线表（私聊离线判定即时生效）。
        m_onlinePlayers.RemoveByCharacterId(player->CharacterId());
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

// 阶段26 指令十七：主动离开世界 —— 保存位置 → AOI Despawn(LeftWorld) → 移除 →
// 回 LeaveWorldResponse → 关闭该 World 会话（客户端收到 Response 后回角色大厅，
// Gateway 把会话从 InWorld 回退到 Authenticated；与断线路径严格区分）。
void WorldServer::HandleLeaveWorldRequest(std::uint64_t connectionId,
                                          const legend::network::Packet& packet) {
    world::LeaveWorldRequestPayload request;
    std::string error;
    if (!legend::world::DecodeLeaveWorldRequest(packet.payload.data(), packet.payload.size(),
                                                request, error)) {
        LOG_INFO("[World] Malformed LeaveWorldRequest from #" + std::to_string(connectionId));
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    auto getSession = [&](std::uint64_t cid) -> std::shared_ptr<WorldSession> {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto it = m_sessions.find(cid);
        return it != m_sessions.end() ? it->second : nullptr;
    };
    if (!player) {
        // 非法状态（未在世界中）——拒绝但不断连（客户端可重试/回大厅）。
        world::LeaveWorldResponsePayload out;
        out.requestId = request.requestId;
        out.success = false;
        out.errorCode = static_cast<std::uint16_t>(WorldErrorCode::InvalidTicket);
        out.message = "not in world";
        Packet resp;
        resp.header.messageId = static_cast<std::uint16_t>(MessageId::LeaveWorldResponse);
        if (legend::world::EncodeLeaveWorldResponse(out, resp.payload)) {
            if (auto session = getSession(connectionId)) {
                session->SendPacket(resp);
            }
        }
        return;
    }
    // 与 OnClientClosed 相同的移除序列（主动离开语义：Despawn reason = LeftWorld）。
    auto removed = m_players.RemoveByConnection(connectionId);
    if (removed) {
        m_mapManager.RemovePlayer(connectionId, removed->MapId());
        // Stage27 指令二十八：主动离开立即清理在线表。
        m_onlinePlayers.RemoveByCharacterId(removed->CharacterId());
        m_spatialGrid.RemovePlayer(removed->CharacterId());
        NotifyPlayerGoneToObservers(removed->CharacterId(), PlayerDespawnReason::LeftWorld);
        OnTargetPlayerRemoved(removed->CharacterId());
        SavePlayerPositionNow(removed->CharacterId(), removed->MapId(), removed->PositionX(),
                              removed->PositionY());
        LOG_INFO("[World] Player left world (voluntary) character=" + removed->CharacterName() +
                 " (#" + std::to_string(removed->CharacterId()) + ")");
        if (m_hooks.onPlayerChanged) {
            m_hooks.onPlayerChanged(removed->CharacterId(), false);
        }
    }
    world::LeaveWorldResponsePayload out;
    out.requestId = request.requestId;
    out.success = true;
    out.errorCode = 0;
    out.message = "ok";
    Packet resp;
    resp.header.messageId = static_cast<std::uint16_t>(MessageId::LeaveWorldResponse);
    if (legend::world::EncodeLeaveWorldResponse(out, resp.payload)) {
        if (auto session = getSession(connectionId)) {
            session->SendPacket(resp);
        }
    }
    // 会话收尾：World 侧职责已完成，关闭连接（客户端收到 Response 后回大厅；
    // Gateway 检测到 worldProxy 关闭但主连接存活 -> 回退 Authenticated）。
    if (auto session = getSession(connectionId)) {
        session->SetState(WorldSessionState::Closing);
        session->Disconnect();
    }
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

    // 阶段11 指令二十一/二十二：加载角色（网络线程禁止 SQLite IO）。
    // 阶段25.5：RPC 模式 = LoadCharacterFull → LoadInventory → LoadQuestState 链式
    // 请求（单连接 FIFO 保序）；legacy 模式 = 单个 DB 任务。应用逻辑共用
    // ApplyEnterWorldLoad（io 线程）。
    session->SetState(WorldSessionState::LoadingCharacter);
    auto self = shared_from_this();
    const std::uint64_t connectionId = session->ConnectionId();
    const std::uint64_t requestId = response.requestId;
    const std::uint64_t accountId = response.accountId;
    const std::uint64_t characterId = response.characterId;
    const auto applyLoad = [self, connectionId, requestId, accountId, characterId](
                               EnterWorldLoadResult load) {
        self->m_service.Post([self, connectionId, requestId, accountId, characterId,
                              load = std::move(load)]() mutable {
            if (self->m_stopped.load()) {
                return;
            }
            self->ApplyEnterWorldLoad(connectionId, requestId, accountId, characterId, load);
        });
    };
    if (m_persistence) {
        std::vector<std::uint8_t> queryPayload;
        legend::internal::EncodeCharacterIdQuery({characterId}, queryPayload);
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::LoadCharacterFull, queryPayload,
            [self, applyLoad, accountId, characterId,
             queryPayload](
                const legend::internal::DbResponse& rowResponse) mutable {
                EnterWorldLoadResult load;
                legend::internal::WorldCharacterRow rpcRow;
                std::string decodeError;
                load.characterOk =
                    rowResponse.errorCode == legend::internal::InternalErrorCode::Ok &&
                    legend::internal::DecodeWorldCharacterRow(
                        rowResponse.payload.data(), rowResponse.payload.size(), rpcRow,
                        decodeError) &&
                    !rpcRow.deleted;
                if (rowResponse.errorCode != legend::internal::InternalErrorCode::Ok) {
                    load.error = rowResponse.message;
                } else if (!decodeError.empty()) {
                    load.error = decodeError;
                }
                if (load.characterOk) {
                    load.row.id = rpcRow.id;
                    load.row.accountId = rpcRow.accountId;
                    load.row.name = rpcRow.name;
                    load.row.classId = rpcRow.classId;
                    load.row.gender = rpcRow.gender;
                    load.row.level = rpcRow.level;
                    load.row.exp = rpcRow.exp;
                    load.row.gold = rpcRow.gold;
                    load.row.mapId = rpcRow.mapId;
                    load.row.positionX = rpcRow.positionX;
                    load.row.positionY = rpcRow.positionY;
                    load.row.visualId = rpcRow.visualId; // Stage27 指令五：持久化造型
                }
                // 第二段：持久化背包（角色有效才加载；失败清空——与 legacy 语义一致）。
                self->m_persistence->AsyncRequest(
                    legend::internal::DbOperation::LoadInventory, queryPayload,
                    [self, applyLoad, characterId, load = std::move(load),
                     queryPayload](const legend::internal::DbResponse& itemResponse) mutable {
                        if (load.characterOk) {
                            legend::internal::WorldInventoryList items;
                            std::string itemError;
                            if (itemResponse.errorCode ==
                                    legend::internal::InternalErrorCode::Ok &&
                                legend::internal::DecodeWorldInventoryList(
                                    itemResponse.payload.data(), itemResponse.payload.size(),
                                    items, itemError)) {
                                load.itemRows.reserve(items.items.size());
                                for (const auto& item : items.items) {
                                    load.itemRows.push_back({item.instanceId, item.definitionId,
                                                             item.quantity, item.slotIndex,
                                                             item.createdAt});
                                }
                            } else {
                                LOG_ERROR("[Inventory] load failed for #" +
                                          std::to_string(characterId) + ": " + itemError);
                                load.itemRows.clear();
                            }
                        }
                        // 第三段：持久化任务状态。
                        self->m_persistence->AsyncRequest(
                            legend::internal::DbOperation::LoadQuestState, queryPayload,
                            [self, applyLoad, characterId,
                             load = std::move(load)](
                                const legend::internal::DbResponse& questResponse) mutable {
                                if (load.characterOk) {
                                    legend::internal::WorldQuestStateList quests;
                                    std::string questError;
                                    if (questResponse.errorCode ==
                                            legend::internal::InternalErrorCode::Ok &&
                                        legend::internal::DecodeWorldQuestStateList(
                                            questResponse.payload.data(),
                                            questResponse.payload.size(), quests, questError)) {
                                        load.questRows.reserve(quests.quests.size());
                                        for (const auto& quest : quests.quests) {
                                            load.questRows.push_back(
                                                {quest.questId, quest.state, quest.acceptedAt,
                                                 quest.completedAt, quest.turnedInAt});
                                        }
                                        load.questObjectiveRows.reserve(quests.objectives.size());
                                        for (const auto& objective : quests.objectives) {
                                            load.questObjectiveRows.push_back(
                                                {objective.questId, objective.objectiveId,
                                                 objective.progress});
                                        }
                                    } else {
                                        LOG_ERROR("[Quest] load failed for #" +
                                                  std::to_string(characterId) + ": " +
                                                  questError);
                                        load.questRows.clear();
                                        load.questObjectiveRows.clear();
                                    }
                                }
                                applyLoad(std::move(load));
                            });
                    });
            });
        return;
    }
    m_dbWorker.Post([self, connectionId, requestId, accountId, characterId, applyLoad]() {
        EnterWorldLoadResult load;
        auto found = CharacterRepository::FindCharacterById(self->m_database, characterId);
        // 阶段18 指令二十八：同一 DB 任务加载持久化背包（装备从 slot_index 码还原）。
        if (found.success && found.value.has_value() && !found.value->deleted) {
            load.row = *found.value;
            load.characterOk = true;
            std::vector<InventoryRepository::InventoryRow> itemRows;
            std::string itemLoadError;
            if (!InventoryRepository::LoadInventory(self->m_database, characterId, itemRows,
                                                    itemLoadError)) {
                LOG_ERROR("[Inventory] load failed for #" + std::to_string(characterId) + ": " +
                          itemLoadError);
                itemRows.clear();
            }
            load.itemRows = std::move(itemRows);
        } else if (!found.success) {
            load.error = found.errorMessage;
        }
        // 阶段19 指令六十八：同一 DB 任务加载持久化任务状态（不能只靠进程内存）。
        if (load.characterOk) {
            std::vector<QuestRepository::QuestRow> questRows;
            std::vector<QuestRepository::ObjectiveRow> questObjectiveRows;
            std::string questLoadError;
            if (!QuestRepository::LoadCharacterQuests(self->m_database, characterId, questRows,
                                                      questObjectiveRows, questLoadError)) {
                LOG_ERROR("[Quest] load failed for #" + std::to_string(characterId) + ": " +
                          questLoadError);
                questRows.clear();
                questObjectiveRows.clear();
            }
            load.questRows = std::move(questRows);
            load.questObjectiveRows = std::move(questObjectiveRows);
        }
        applyLoad(std::move(load));
    });
}

// 阶段25.5：进世界加载应用（io 线程；legacy 与 RPC 共用）。
void WorldServer::ApplyEnterWorldLoad(std::uint64_t connectionId, std::uint64_t requestId,
                                      std::uint64_t accountId, std::uint64_t characterId,
                                      const EnterWorldLoadResult& load) {
    std::shared_ptr<WorldSession> session;
    {
        std::lock_guard<std::mutex> lock(m_sessionsMutex);
        auto sit = m_sessions.find(connectionId);
        if (sit != m_sessions.end()) {
            session = sit->second;
        }
    }
    if (!session || session->State() != WorldSessionState::LoadingCharacter) {
        return; // 指令一百零五：Client 已断开/状态漂移 -> 丢弃
    }
    // 指令七十一：Ticket 绑定 accountId + characterId，缺一不可。
    if (!load.characterOk || load.row.accountId != accountId || load.row.id != characterId) {
        LOG_WARN("[World] Character load failed (characterId=" + std::to_string(characterId) +
                 " accountId=" + std::to_string(accountId) + " ok=" +
                 (load.characterOk ? "1" : "0") + " rowAccount=" +
                 std::to_string(load.row.accountId) + " rowId=" +
                 std::to_string(load.row.id) + " detail=" + load.error + ")");
        SendEnterWorldError(connectionId, requestId, WorldErrorCode::CharacterNotFound,
                            "character not found");
        session->SetState(WorldSessionState::WaitingEnterWorld);
        return;
    }
    const legend::account::CharacterRow row = load.row;
    // 阶段21 指令二十九：无效 mapId/越界位置自动修正 Map1 300,300 并重新持久化。
    std::uint16_t mapId = row.mapId;
    float x = static_cast<float>(row.positionX);
    float y = static_cast<float>(row.positionY);
    const MapDefinition* map = MapRegistry::Instance().FindMap(mapId);
    if (map == nullptr) {
        mapId = kTownMapId;
        map = MapRegistry::Instance().FindMap(mapId);
        LOG_WARN("[World] invalid persisted map " + std::to_string(row.mapId) +
                 " -> corrected to " + std::to_string(mapId));
    }
    // 阶段25 指令四：新角色出生检查（服务器权威）——未出生哨兵(-1,-1) ->
    // 该地图出生点（Map1 = 300,300），落地后立即持久化；Client 不做任何修正。
    const bool newCharacter = (row.positionX < 0.0 || row.positionY < 0.0);
    if (newCharacter) {
        x = map->spawnX;
        y = map->spawnY;
        LOG_INFO("[World] new character spawn map=" + std::to_string(mapId) + " at " +
                 std::to_string(x) + "," + std::to_string(y));
    } else if (!map->InBounds(x, y)) {
        x = map->spawnX; // 指令二十九：越界 -> 该地图出生点
        y = map->spawnY;
        LOG_WARN("[World] out-of-bounds persisted position corrected map=" +
                 std::to_string(mapId));
    }
    auto player = std::make_shared<PlayerSession>(connectionId, accountId, characterId,
                                                  row.name, row.classId, row.gender,
                                                  row.level, mapId, x, y, row.visualId);
    if (row.mapId != mapId || row.positionX != x || row.positionY != y) {
        // 修正后的位置立即重新持久化（不能静默保留非法值）。
        SavePlayerPositionNow(characterId, mapId, x, y);
    }
    // 阶段17 指令二：加载持久化成长数据（level 已进构造；exp/gold 服务器权威）。
    player->SetProgression(row.exp, row.gold);
    // 指令二十九/一百零七：同角色重复上线拒绝。
    if (!m_players.TryAddPlayer(player)) {
        LOG_WARN("[World] EnterWorld rejected (character already online) character=" +
                 row.name);
        SendEnterWorldError(connectionId, requestId,
                            WorldErrorCode::CharacterAlreadyOnline,
                            "character already online");
        session->SetState(WorldSessionState::WaitingEnterWorld);
        return;
    }
    if (!m_mapManager.AddPlayer(player)) {
        m_players.RemoveByConnection(connectionId);
        SendEnterWorldError(connectionId, requestId, WorldErrorCode::InternalError,
                            "map add failed");
        session->SetState(WorldSessionState::WaitingEnterWorld);
        return;
    }
    // 阶段12 指令十二：进入 WorldManager/MapManager/SpatialGrid。
    m_spatialGrid.AddPlayer(player);
    session->SetState(WorldSessionState::InWorld);
    // Stage27 指令二十九：登记在线玩家目录（私聊/玩家查找 O(1)）。
    m_onlinePlayers.Add(player);
    // Stage27 指令十二：进入世界系统消息（ServerSystemMessage API 复用）。
    {
        chat::ChatMessageEventPayload welcome;
        welcome.messageId = m_nextChatMessageId++;
        welcome.channel = static_cast<std::uint8_t>(chat::ChatChannel::System);
        welcome.text = "欢迎进入LegendGame";
        welcome.timestamp = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::system_clock::now().time_since_epoch()).count());
        SendChatMessageEvent(player, welcome);
    }
    SendEnterWorldSuccess(connectionId, requestId, player);
    // 阶段18 指令二十八：应用持久化背包/装备（Snapshot 在进场后下发）。
    ApplyLoadedItems(player, load.itemRows);
    // 阶段19 指令六十八：应用持久化任务状态（先于初始校验与 Snapshot）。
    ApplyLoadedQuests(player, load.questRows, load.questObjectiveRows);
    // 阶段21 指令二十七：进入世界下发 MapSnapshot（地图基础信息，只发本人）。
    {
        MapSnapshotPayload mapSnapshot;
        mapSnapshot.mapId = map->mapId;
        mapSnapshot.mapName = map->name;
        mapSnapshot.minX = map->minX;
        mapSnapshot.minY = map->minY;
        mapSnapshot.maxX = map->maxX;
        mapSnapshot.maxY = map->maxY;
        mapSnapshot.spawnX = map->spawnX;
        mapSnapshot.spawnY = map->spawnY;
        mapSnapshot.respawnX = map->respawnX;
        mapSnapshot.respawnY = map->respawnY;
        mapSnapshot.serverTime = ServerTimeMs();
        Packet mapPacket;
        mapPacket.header.messageId = static_cast<std::uint16_t>(MessageId::MapSnapshot);
        if (EncodeMapSnapshot(mapSnapshot, mapPacket.payload)) {
            SendPacketToPlayer(player, mapPacket);
        }
    }
    // 阶段12 指令十八：进入世界初始可见性（双向 Spawn）。
    InitializePlayerVisibility(player);
    LOG_INFO("[World] Player entered character=" + row.name + " (#" +
             std::to_string(characterId) + ") map=" + std::to_string(mapId));
    // 阶段17 指令十五：进入世界立即下发本人 ProgressionSnapshot。
    SendProgressionSnapshot(player);
    // 阶段18 指令二十八：进入世界下发完整背包/装备 Snapshot（Client 只是镜像）。
    SendInventorySnapshot(player);
    SendEquipmentSnapshot(player);
    // 阶段19 指令四十五：进入世界下发 QuestSnapshot（全部 InProgress/
    // ReadyToTurnIn/Completed）；重登后离线推进结果经此恢复（指令六十三）。
    SendQuestSnapshot(player);
    if (m_hooks.onPlayerChanged) {
        m_hooks.onPlayerChanged(characterId, true);
    }
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
    // Stage27 指令五：本人造型（服务器权威；客户端不得自行猜测 visualId）。
    out.visualId = player->VisualId();
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
    // 阶段21 指令八：按玩家所在地图的 MapDefinition 权威钳制边界。
    const MapDefinition* moveMap = MapRegistry::Instance().FindMap(player->MapId());
    static const MapDefinition kFallbackMap{}; // 地图缺失兜底（0~2000）
    const bool moved = WorldMapManager::ApplyMoveInput(
        *player, moveMap != nullptr ? *moveMap : kFallbackMap, input.inputSequence,
        input.directionX, input.directionY, input.deltaTime);
    // Stage27 指令四：服务器权威朝向（非零方向输入即更新；AOI 对端拿真实朝向）。
    if (input.directionX != 0.0f || input.directionY != 0.0f) {
        const legend::entity::Direction8 dir = legend::entity::DirectionFromVector(
            legend::math::Vector2(input.directionX, input.directionY),
            static_cast<legend::entity::Direction8>(player->Direction()));
        player->SetDirection(static_cast<std::uint8_t>(dir));
    }
    const bool actuallyMoved =
        player->PositionX() != prevX || player->PositionY() != prevY;
    if (actuallyMoved && player->IsCasting()) {
        CancelActiveCast(player, SkillCancelReason::Moved);
    }
    if (moved) {
        // 阶段12 指令十二：移动后重挂 SpatialGrid cell。
        m_spatialGrid.UpdatePlayerCell(player);
    }
    if (actuallyMoved) {
        // 阶段19 指令二十七：位置真正变化才检查 ReachArea（不每 100ms 全量扫描；
        // 服务器权威位置——Client 不能伪造"我到了"，指令一百零七）。
        HandleQuestPlayerMoved(player);
    }
}

// ---------------------------------------------------------------------------
// Stage27：聊天（指令八~十八）。校验链（服务器权威，全部 io 线程）：
//   InWorld 状态 -> requestId 防重放 -> channel 合法性（System 拒绝）->
//   文本校验（120 码点/UTF-8 严格/控制字符/空与纯空格）-> Whisper 目标 ->
//   限流（频道窗口 + 跨频道突发）-> 路由广播 -> 审计。
// 指令四十二：Nearby 用 SpatialGrid 空间索引；World 遍历在线 session；
//   Whisper 查 OnlinePlayerDirectory——全部内存操作，不访问 SQLite。
// ---------------------------------------------------------------------------
void WorldServer::HandleChatSendRequest(std::uint64_t connectionId,
                                        const legend::network::Packet& packet) {
    chat::ChatSendRequestPayload request;
    std::string decodeError;
    if (!chat::DecodeChatSendRequest(packet.payload.data(), packet.payload.size(), request,
                                     decodeError)) {
        // 指令三十六：畸形包只断当前 Client（不能断整个 WorldServer）。
        LOG_INFO("[Chat] Malformed ChatSendRequest from #" + std::to_string(connectionId));
        std::shared_ptr<WorldSession> session;
        {
            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            auto it = m_sessions.find(connectionId);
            if (it != m_sessions.end()) {
                session = it->second;
            }
        }
        if (session) {
            std::lock_guard<std::mutex> lock(m_sessionsMutex);
            m_sessions.erase(connectionId);
            session->Disconnect();
        }
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        // 未进入世界（状态机外）——拒绝但不断连（客户端渲染层不会出现该路径）。
        SendChatSendResponse(nullptr, request.requestId, false, chat::ChatErrorCode::NotInWorld);
        return;
    }
    const auto now = chat::SteadyClock::now();

    // 指令十六：防重放——重复 requestId 不重复广播；成功请求幂等回执。
    if (player->IsRecentChatRequest(request.requestId)) {
        LOG_INFO("[Chat] duplicate chat requestId " + std::to_string(request.requestId) +
                 " from #" + player->CharacterName());
        SendChatSendResponse(player, request.requestId, true, chat::ChatErrorCode::None);
        return;
    }

    const auto finishReject = [&](chat::ChatErrorCode code) {
        LOG_INFO("[Chat] rejected player=" + player->CharacterName() + " channel=" +
                 std::to_string(request.channel) + " code=" + chat::ChatErrorCodeName(
                     static_cast<std::uint16_t>(code)));
        SendChatSendResponse(player, request.requestId, false, code);
    };

    // 指令八/十二：channel 必须合法；System 只能服务器产生。
    if (!chat::IsValidChatChannelValue(request.channel)) {
        finishReject(chat::ChatErrorCode::InvalidChannel);
        return;
    }
    if (static_cast<chat::ChatChannel>(request.channel) == chat::ChatChannel::System) {
        finishReject(chat::ChatErrorCode::SystemChannelForbidden);
        return;
    }

    // 指令十四/二十七：文本校验（UTF-8 严格/120 码点/控制字符/空与纯空格）。
    if (!chat::IsValidChatText(request.text,
                               static_cast<std::size_t>(m_config.chatMaxCodePoints))) {
        // 细分空消息与非法字符，超长单独判断（玩家可读文案更精确）。
        if (request.text.empty()) {
            finishReject(chat::ChatErrorCode::EmptyText);
            return;
        }
        std::size_t codePoints = 0;
        std::size_t i = 0;
        bool hasControl = false;
        bool validUtf8 = true;
        while (i < request.text.size()) {
            std::uint32_t cp = 0;
            if (!legend::account::detail::DecodeUtf8CodePoint(request.text, i, cp)) {
                validUtf8 = false;
                break;
            }
            if (cp < 0x20 || (cp >= 0x7F && cp <= 0x9F)) {
                hasControl = true;
            }
            ++codePoints;
        }
        if (!validUtf8 || hasControl) {
            finishReject(chat::ChatErrorCode::InvalidUtf8);
            return;
        }
        if (codePoints > static_cast<std::size_t>(m_config.chatMaxCodePoints)) {
            finishReject(chat::ChatErrorCode::TextTooLong);
            return;
        }
        finishReject(chat::ChatErrorCode::EmptyText); // 纯空格
        return;
    }

    // 指令十一：私聊目标（必须在线；按角色名查 OnlinePlayerDirectory）。
    std::shared_ptr<PlayerSession> whisperTarget;
    if (static_cast<chat::ChatChannel>(request.channel) == chat::ChatChannel::Whisper) {
        if (!chat::IsValidChatTargetName(request.targetName)) {
            finishReject(chat::ChatErrorCode::TargetInvalid);
            return;
        }
        whisperTarget = m_onlinePlayers.FindByCharacterName(request.targetName);
        if (!whisperTarget) {
            finishReject(chat::ChatErrorCode::TargetOffline); // "该玩家当前不在线"
            return;
        }
    }

    // 指令十五：限流（频道窗口 + 跨频道突发；服务器权威）。
    const int channelValue = static_cast<int>(request.channel);
    int windowMs = 0;
    int maxPerWindow = 0;
    switch (static_cast<chat::ChatChannel>(request.channel)) {
        case chat::ChatChannel::Nearby:
            windowMs = m_config.chatNearbyWindowMs;
            maxPerWindow = m_config.chatNearbyMaxPerWindow;
            break;
        case chat::ChatChannel::World:
            windowMs = m_config.chatWorldWindowMs;
            maxPerWindow = m_config.chatWorldMaxPerWindow;
            break;
        case chat::ChatChannel::Whisper:
            windowMs = m_config.chatWhisperWindowMs;
            maxPerWindow = m_config.chatWhisperMaxPerWindow;
            break;
        default:
            break;
    }
    const auto& channelSends =
        player->ChatChannelSends()[static_cast<std::size_t>(channelValue)];
    if (!player->IsChatRateAllowedAt(channelSends, now, windowMs, maxPerWindow) ||
        !player->IsChatRateAllowedAt(player->ChatRecentSends(), now,
                                     m_config.chatBurstWindowMs,
                                     m_config.chatBurstMaxMessages)) {
        finishReject(chat::ChatErrorCode::RateLimited); // "发言过于频繁，请稍后再试"
        return;
    }

    // 允许：推进限流状态 + 记录成功 requestId（防重放只缓存成功请求）。
    player->TouchChatSent(channelValue, now);
    player->RememberChatRequest(request.requestId);

    // 组装广播事件（指令十三：服务器权威 sender；绝不发账号名/Token）。
    chat::ChatMessageEventPayload event;
    event.messageId = m_nextChatMessageId++;
    event.channel = request.channel;
    event.senderCharacterId = player->CharacterId();
    event.senderName = player->CharacterName();
    event.targetName = request.targetName;
    event.text = request.text;
    event.timestamp = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());

    switch (static_cast<chat::ChatChannel>(request.channel)) {
        case chat::ChatChannel::Nearby: {
            // 指令九：同地图 + kNearbyChatRadius 内（服务器计算距离；排除自己）。
            auto candidates = m_spatialGrid.QueryNearbyPlayers(
                player->PositionX(), player->PositionY(), m_config.chatNearbyRadius,
                player->CharacterId());
            const float radiusSq = m_config.chatNearbyRadius * m_config.chatNearbyRadius;
            for (const auto& candidate : candidates) {
                if (candidate.player->MapId() != player->MapId() ||
                    candidate.distanceSquared > radiusSq) {
                    continue; // 指令三十四：跨地图即使坐标一样也收不到
                }
                SendChatMessageEvent(candidate.player, event);
            }
            SendChatMessageEvent(player, event); // 发送者本人一定收到回显
            break;
        }
        case chat::ChatChannel::World: {
            // 指令十：全部 InWorld 玩家（单 WorldServer；不查数据库）。
            for (const auto& online : m_players.SnapshotPlayers()) {
                SendChatMessageEvent(online, event);
            }
            break;
        }
        case chat::ChatChannel::Whisper: {
            // 指令十一：只有 A/B 收到（C 不收到）；目标必须在线（上方已校验）。
            if (whisperTarget && whisperTarget != player) {
                SendChatMessageEvent(whisperTarget, event);
            }
            SendChatMessageEvent(player, event);
            break;
        }
        default:
            break;
    }

    // 指令四十四：正常日志（不打印 Token/Ticket/密码）。
    LOG_INFO("[Chat] sent player=" + player->CharacterName() + " channel=" +
             std::to_string(request.channel) + " len=" + std::to_string(request.text.size()));
    // 提交成功回执（客户端以 ChatSendResponse 为准；防重放重复请求的幂等回执
    // 也走同一接口——重复请求在上方提前返回）。
    SendChatSendResponse(player, request.requestId, true, chat::ChatErrorCode::None);
    // 指令十八：审计（LogServer 不可用由 EmitChatAudit 内部 warning 兜底）。
    chat::ChatAuditRecord audit;
    audit.timestampMs = event.timestamp;
    audit.senderCharacterId = event.senderCharacterId;
    audit.senderName = event.senderName;
    audit.channel = event.channel;
    audit.targetName = event.targetName;
    audit.text = event.text;
    EmitChatAudit(audit);
}

void WorldServer::BroadcastSystemMessage(const std::string& text) {
    // 指令十二/十四：System 只能服务器产生；文本同样过校验（服务器自身纪律）。
    if (!chat::IsValidChatText(text, static_cast<std::size_t>(m_config.chatMaxCodePoints))) {
        LOG_WARN("[Chat] system message rejected by text validation (len=" +
                 std::to_string(text.size()) + ")");
        return;
    }
    chat::ChatMessageEventPayload event;
    event.messageId = m_nextChatMessageId++;
    event.channel = static_cast<std::uint8_t>(chat::ChatChannel::System);
    event.senderCharacterId = 0;
    event.senderName.clear();
    event.text = text;
    event.timestamp = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()).count());
    for (const auto& online : m_players.SnapshotPlayers()) {
        SendChatMessageEvent(online, event);
    }
    LOG_INFO("[Chat] system message broadcast len=" + std::to_string(text.size()));
}

void WorldServer::SendChatSendResponse(const std::shared_ptr<PlayerSession>& player,
                                       std::uint64_t requestId, bool success,
                                       chat::ChatErrorCode code) {
    if (!player) {
        return;
    }
    chat::ChatSendResponsePayload out;
    out.requestId = requestId;
    out.success = success;
    out.errorCode = static_cast<std::uint16_t>(code);
    out.message = success ? "" : chat::ChatErrorUserText(static_cast<std::uint16_t>(code));
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::ChatSendResponse);
    if (chat::EncodeChatSendResponse(out, packet.payload)) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendChatMessageEvent(const std::shared_ptr<PlayerSession>& receiver,
                                       const chat::ChatMessageEventPayload& event) {
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::ChatMessageEvent);
    if (chat::EncodeChatMessageEvent(event, packet.payload)) {
        SendPacketToPlayer(receiver, packet);
    }
}

void WorldServer::EmitChatAudit(const chat::ChatAuditRecord& record) {
    // 指令十八：LogServer 暂不可用 -> 仅 warning，聊天本身不停止。
    if (!m_hooks.onChatAudit) {
        LOG_WARN("[Chat] audit hook missing — chat continues without LogServer audit.");
        return;
    }
    m_hooks.onChatAudit(record);
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
        // Stage25.6 管理台：tick 耗时计量（平均/最大，微秒精度）
        const auto tickStart = std::chrono::steady_clock::now();
        self->SendPositionSnapshots();
        const auto tickUs = std::chrono::duration_cast<std::chrono::microseconds>(
                                std::chrono::steady_clock::now() - tickStart)
                                .count();
        self->m_snapshotTickCount.fetch_add(1, std::memory_order_relaxed);
        self->m_snapshotTickTotalUs.fetch_add(static_cast<std::uint64_t>(tickUs),
                                              std::memory_order_relaxed);
        auto prevMax = self->m_snapshotTickMaxUs.load(std::memory_order_relaxed);
        while (static_cast<std::uint64_t>(tickUs) > prevMax &&
               !self->m_snapshotTickMaxUs.compare_exchange_weak(
                   prevMax, static_cast<std::uint64_t>(tickUs), std::memory_order_relaxed)) {
        }
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
    // 指令五十二/五十三/五十六：位置保存（legacy 经 DB Worker；RPC 经 DbServer）。
    auto self = shared_from_this();
    if (m_persistence) {
        legend::internal::WorldSavePosition command;
        command.characterId = characterId;
        command.mapId = mapId;
        command.positionX = x;
        command.positionY = y;
        command.lastPlayedAt = legend::account::UnixNow();
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldSavePosition(command, payload)) {
            LOG_ERROR("[World] Save position encode failed character=" +
                      std::to_string(characterId));
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::SavePosition, std::move(payload),
            [self, characterId](const legend::internal::DbResponse& response) {
                if (response.errorCode != legend::internal::InternalErrorCode::Ok) {
                    // Persistence Degraded：内存权威值保留，下个保存周期重试（不静默丢）。
                    LOG_ERROR("[World] Save position failed character=" +
                              std::to_string(characterId) + " detail=" + response.message);
                }
            });
        return;
    }
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

// ---------------------------------------------------------------------------
// 阶段25.5：成长/金币/离线奖励落库（legacy DbWorker 与 DbServer RPC 双模式）。
// ---------------------------------------------------------------------------
void WorldServer::PersistProgression(std::uint64_t characterId, std::uint32_t level,
                                     std::int64_t exp, std::int64_t gold) {
    if (m_persistence) {
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldSaveProgression(
                {characterId, level, exp, gold}, payload)) {
            LOG_ERROR("[Progression] encode failed character=" + std::to_string(characterId));
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::SaveProgression, std::move(payload),
            [characterId](const legend::internal::DbResponse& response) {
                if (response.errorCode != legend::internal::InternalErrorCode::Ok) {
                    LOG_ERROR("[Progression] RPC save failed character=" +
                              std::to_string(characterId) + " detail=" + response.message);
                }
            });
        return;
    }
    m_dbWorker.Post([self = shared_from_this(), characterId, level, exp, gold]() {
        CharacterRepository::SaveProgression(self->m_database, characterId, level, exp, gold);
    });
}

void WorldServer::PersistGold(std::uint64_t characterId, std::int64_t gold) {
    if (m_persistence) {
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldSaveGold({characterId, gold}, payload)) {
            LOG_ERROR("[Gold] encode failed character=" + std::to_string(characterId));
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::SaveGoldWrite, std::move(payload),
            [characterId](const legend::internal::DbResponse& response) {
                if (response.errorCode != legend::internal::InternalErrorCode::Ok) {
                    LOG_ERROR("[Gold] RPC save failed character=" +
                              std::to_string(characterId) + " detail=" + response.message);
                }
            });
        return;
    }
    m_dbWorker.Post([self = shared_from_this(), characterId, gold]() {
        std::string error;
        account::Statement stmt;
        if (!stmt.Prepare(self->m_database.Handle(),
                          "UPDATE characters SET gold = ? WHERE id = ?;", error)) {
            LOG_ERROR("[Gold] prepare failed: " + error);
            return;
        }
        stmt.BindInt64(1, gold);
        stmt.BindInt64(2, static_cast<std::int64_t>(characterId));
        stmt.Step(error);
        if (!error.empty()) {
            LOG_ERROR("[Gold] update failed: " + error);
        }
    });
}

void WorldServer::PersistOfflineReward(std::uint64_t characterId, std::int64_t expDelta,
                                       std::int64_t goldDelta) {
    if (m_persistence) {
        // 读角色行 -> 算升级 -> 整体写回；角色不存在则保底累加（与 legacy 语义一致）。
        std::vector<std::uint8_t> query;
        legend::internal::EncodeCharacterIdQuery({characterId}, query);
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::LoadCharacterFull, std::move(query),
            [this, self = shared_from_this(), characterId, expDelta, goldDelta](
                const legend::internal::DbResponse& rowResponse) {
                if (rowResponse.errorCode == legend::internal::InternalErrorCode::Ok) {
                    legend::internal::WorldCharacterRow row;
                    std::string error;
                    if (legend::internal::DecodeWorldCharacterRow(
                            rowResponse.payload.data(), rowResponse.payload.size(), row, error)) {
                        const auto progression = AddExperience(row.level, row.exp, expDelta);
                        PersistProgression(characterId, progression.level, progression.exp,
                                           AddGold(row.gold, goldDelta));
                        return;
                    }
                }
                std::vector<std::uint8_t> payload;
                if (!legend::internal::EncodeWorldAddRewards(
                        {characterId, expDelta, goldDelta}, payload)) {
                    LOG_ERROR("[Progression] offline reward encode failed character=" +
                              std::to_string(characterId));
                    return;
                }
                m_persistence->AsyncRequest(
                    legend::internal::DbOperation::AddRewardsWrite, std::move(payload),
                    [characterId](const legend::internal::DbResponse& response) {
                        if (response.errorCode != legend::internal::InternalErrorCode::Ok) {
                            LOG_ERROR("[Progression] offline reward failed character=" +
                                      std::to_string(characterId) + " detail=" +
                                      response.message);
                        }
                    });
            });
        return;
    }
    m_dbWorker.Post([self = shared_from_this(), characterId, expDelta, goldDelta]() {
        // DB 线程：读 level/exp -> 算升级 -> 整体写回（事务性由单 UPDATE 保证）。
        auto found = CharacterRepository::FindCharacterById(self->m_database, characterId);
        if (found.success && found.value.has_value()) {
            const auto row = *found.value;
            const auto progression = AddExperience(row.level, row.exp, expDelta);
            CharacterRepository::SaveProgression(self->m_database, characterId,
                                                 progression.level, progression.exp,
                                                 AddGold(row.gold, goldDelta));
        } else {
            // 角色已被删除等边界：保底累加（不丢失奖励）。
            CharacterRepository::AddProgressionRewards(self->m_database, characterId,
                                                       expDelta, goldDelta);
        }
    });
}

// ---------------------------------------------------------------------------
// 阶段25.5：任务持久化（legacy DbWorker 与 DbServer RPC 双模式）。
// ---------------------------------------------------------------------------
void WorldServer::PersistQuestInsert(std::uint64_t characterId, QuestId questId,
                                     std::uint8_t state, std::int64_t acceptedAt,
                                     const std::vector<std::uint32_t>& objectiveIds) {
    if (m_persistence) {
        legend::internal::WorldQuestInsert command;
        command.characterId = characterId;
        command.questId = static_cast<std::uint32_t>(questId);
        command.state = state;
        command.acceptedAt = acceptedAt;
        command.objectiveIds = objectiveIds;
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldQuestInsert(command, payload)) {
            LOG_ERROR("[Quest] InsertQuest encode failed char=" + std::to_string(characterId));
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::QuestInsertWrite, std::move(payload),
            [self = shared_from_this(), characterId, questId](
                const legend::internal::DbResponse& response) {
                if (response.errorCode != legend::internal::InternalErrorCode::Ok) {
                    LOG_ERROR("[Quest] InsertQuest failed char=" + std::to_string(characterId) +
                              " quest=" + std::to_string(questId) + ": " + response.message);
                }
            });
        return;
    }
    m_dbWorker.Post([self = shared_from_this(), characterId, questId, state, acceptedAt,
                     objectiveIds]() {
        std::string error;
        if (!QuestRepository::InsertQuest(self->m_database, characterId, questId,
                                          static_cast<std::int8_t>(state), acceptedAt,
                                          objectiveIds, error)) {
            LOG_ERROR("[Quest] InsertQuest failed char=" + std::to_string(characterId) +
                      " quest=" + std::to_string(questId) + ": " + error);
        }
    });
}

void WorldServer::PersistQuestTurnIn(
    const QuestRepository::TurnInTransaction& tx,
    const std::function<void(bool ok, std::uint64_t itemInstanceId,
                             const std::string& error)>& onDone) {
    if (m_persistence) {
        legend::internal::WorldQuestTurnIn command;
        command.characterId = tx.characterId;
        command.questId = static_cast<std::uint32_t>(tx.questId);
        command.turnedInAt = tx.turnedInAt;
        command.newLevel = tx.newLevel;
        command.newExperience = tx.newExperience;
        command.newGold = tx.newGold;
        command.rewardItemDefinitionId = tx.rewardItemDefinitionId;
        command.rewardItemQuantity = tx.rewardItemQuantity;
        command.rewardItemSlotIndex = tx.rewardItemSlotIndex;
        command.rewardItemCreatedAt = tx.rewardItemCreatedAt;
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldQuestTurnIn(command, payload)) {
            onDone(false, 0, "turn-in encode failed");
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::QuestTurnInWrite, std::move(payload),
            [onDone](const legend::internal::DbResponse& response) {
                if (response.errorCode != legend::internal::InternalErrorCode::Ok) {
                    onDone(false, 0, response.message);
                    return;
                }
                legend::internal::WorldQuestTurnInResult result;
                std::string error;
                if (!legend::internal::DecodeWorldQuestTurnInResult(
                        response.payload.data(), response.payload.size(), result, error)) {
                    onDone(false, 0, error);
                    return;
                }
                onDone(result.ok, result.itemInstanceId, "");
            });
        return;
    }
    m_dbWorker.Post([self = shared_from_this(), tx, onDone]() {
        std::string dbError;
        const auto result = QuestRepository::RunTurnInTransaction(self->m_database, tx, dbError);
        onDone(result.ok, result.itemInstanceId, dbError);
    });
}

void WorldServer::PersistQuestAbandon(std::uint64_t characterId, QuestId questId,
                                      std::int64_t nowUnix) {
    if (m_persistence) {
        legend::internal::WorldQuestAbandon command;
        command.characterId = characterId;
        command.questId = static_cast<std::uint32_t>(questId);
        command.nowUnix = nowUnix;
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldQuestAbandon(command, payload)) {
            LOG_ERROR("[Quest] MarkAbandoned encode failed char=" + std::to_string(characterId));
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::QuestAbandonWrite, std::move(payload),
            [self = shared_from_this(), characterId, questId](
                const legend::internal::DbResponse& response) {
                if (response.errorCode != legend::internal::InternalErrorCode::Ok) {
                    LOG_ERROR("[Quest] MarkAbandoned failed char=" + std::to_string(characterId) +
                              " quest=" + std::to_string(questId) + ": " + response.message);
                }
            });
        return;
    }
    m_dbWorker.Post([self = shared_from_this(), characterId, questId, nowUnix]() {
        std::string error;
        if (!QuestRepository::MarkAbandoned(self->m_database, characterId, questId, nowUnix,
                                            error)) {
            LOG_ERROR("[Quest] MarkAbandoned failed char=" + std::to_string(characterId) +
                      " quest=" + std::to_string(questId) + ": " + error);
        }
    });
}

void WorldServer::PersistQuestObjective(std::uint64_t characterId, QuestId questId,
                                        std::uint32_t objectiveId, std::uint32_t progress) {
    if (m_persistence) {
        legend::internal::WorldQuestObjective command;
        command.characterId = characterId;
        command.questId = static_cast<std::uint32_t>(questId);
        command.objectiveId = objectiveId;
        command.progress = progress;
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldQuestObjective(command, payload)) {
            LOG_ERROR("[Quest] UpdateObjectiveProgress encode failed char=" +
                      std::to_string(characterId));
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::QuestObjectiveWrite, std::move(payload),
            [self = shared_from_this(), characterId, questId](
                const legend::internal::DbResponse& response) {
                if (response.errorCode != legend::internal::InternalErrorCode::Ok) {
                    LOG_ERROR("[Quest] UpdateObjectiveProgress failed char=" +
                              std::to_string(characterId) + " quest=" +
                              std::to_string(questId) + ": " + response.message);
                }
            });
        return;
    }
    m_dbWorker.Post([self = shared_from_this(), characterId, questId, objectiveId, progress]() {
        std::string error;
        if (!QuestRepository::UpdateObjectiveProgress(self->m_database, characterId, questId,
                                                      objectiveId, progress, error)) {
            LOG_ERROR("[Quest] UpdateObjectiveProgress failed char=" +
                      std::to_string(characterId) + " quest=" + std::to_string(questId) + ": " +
                      error);
        }
    });
}

void WorldServer::PersistQuestState(std::uint64_t characterId, QuestId questId,
                                    std::uint8_t state, std::int64_t timestamp,
                                    bool setTurnedInAt) {
    if (m_persistence) {
        legend::internal::WorldQuestState command;
        command.characterId = characterId;
        command.questId = static_cast<std::uint32_t>(questId);
        command.state = state;
        command.timestamp = timestamp;
        command.setTurnedInAt = setTurnedInAt;
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldQuestState(command, payload)) {
            LOG_ERROR("[Quest] UpdateQuestState encode failed char=" + std::to_string(characterId));
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::QuestStateWrite, std::move(payload),
            [self = shared_from_this(), characterId, questId](
                const legend::internal::DbResponse& response) {
                if (response.errorCode != legend::internal::InternalErrorCode::Ok) {
                    LOG_ERROR("[Quest] UpdateQuestState failed char=" +
                              std::to_string(characterId) + " quest=" +
                              std::to_string(questId) + ": " + response.message);
                }
            });
        return;
    }
    m_dbWorker.Post([self = shared_from_this(), characterId, questId, state, timestamp,
                     setTurnedInAt]() {
        std::string error;
        if (!QuestRepository::UpdateQuestState(self->m_database, characterId, questId,
                                               static_cast<std::int8_t>(state), timestamp,
                                               setTurnedInAt, error)) {
            LOG_ERROR("[Quest] UpdateQuestState failed char=" + std::to_string(characterId) +
                      " quest=" + std::to_string(questId) + ": " + error);
        }
    });
}

void WorldServer::PersistOfflineKill(std::uint64_t characterId,
                                     const std::vector<QuestRepository::KillCandidate>& candidates) {
    if (m_persistence) {
        legend::internal::WorldOfflineKill command;
        command.characterId = characterId;
        command.candidates.reserve(candidates.size());
        for (const auto& candidate : candidates) {
            command.candidates.push_back({static_cast<std::uint32_t>(candidate.questId),
                                          candidate.objectiveId, candidate.requiredCount});
        }
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldOfflineKill(command, payload)) {
            LOG_ERROR("[Quest] OfflineAdvanceKill encode failed char=" +
                      std::to_string(characterId));
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::OfflineKillWrite, std::move(payload),
            [self = shared_from_this(), characterId](
                const legend::internal::DbResponse& response) {
                if (response.errorCode != legend::internal::InternalErrorCode::Ok) {
                    LOG_ERROR("[Quest] OfflineAdvanceKill failed char=" +
                              std::to_string(characterId) + ": " + response.message);
                } else {
                    LOG_INFO("[Quest] offline kill advanced char=" +
                             std::to_string(characterId));
                }
            });
        return;
    }
    m_dbWorker.Post([self = shared_from_this(), characterId, candidates]() {
        std::string error;
        if (QuestRepository::OfflineAdvanceKill(self->m_database, characterId, candidates,
                                                error)) {
            LOG_INFO("[Quest] offline kill advanced char=" + std::to_string(characterId));
        }
        if (!error.empty()) {
            LOG_ERROR("[Quest] OfflineAdvanceKill failed char=" +
                      std::to_string(characterId) + ": " + error);
        }
    });
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
        // 阶段18 指令十八：掉落 AOI 差量（同一 tick，独立 resolver + Spatial Grid）。
        UpdatePlayerItemDropVisibility(player, false);
        // 阶段20 指令十一/十四：NPC AOI 差量（同一 tick；NPC 不全图广播）。
        UpdatePlayerNpcVisibility(player, false);
        // 阶段21 指令十七：Portal AOI 差量（同一 tick；Portal 不全图广播）。
        UpdatePlayerPortalVisibility(player, false);
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
    // 阶段18 指令十八：进入世界初始掉落可见性（只发 <=600 的初始 spawn）。
    UpdatePlayerItemDropVisibility(player, true);
    // 阶段20 指令十一：进入世界初始 NPC 可见性（NpcSpawn + per-player Marker）。
    UpdatePlayerNpcVisibility(player, true);
    // 阶段21 指令十七：进入世界初始 Portal 可见性（PortalSpawn）。
    UpdatePlayerPortalVisibility(player, true);
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
    // Stage27 指令四/五：真实造型 + 朝向（服务器权威）。
    payload.visualId = target->VisualId();
    payload.direction = target->Direction();
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
                           remote->LastProcessedInputSequence(), remote->Direction()});
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
    // 阶段22 22.7/22.11：刷怪全部来自 MonsterSpawnRegistry（Data/World/monster_spawns.json
    // 或出厂默认）；GenerateSpawnPoints 确定性展开（与编辑器/测试三方一致）。
    // legacyMap1TestSpawn（阶段13 兼容开关）仍用固定 20 点表占用 slotId 1~20。
    const MonsterDefinition* definition = FindMonsterDefinition(kTrainingSlimeTypeId);
    if (!definition) {
        LOG_ERROR("[World] Monster definition missing (Training Slime).");
        return;
    }
    m_respawnManager.Reset();
    std::uint32_t nextSlotId = 1;
    if (m_config.legacyMap1TestSpawn) {
        const std::vector<MonsterSpawnPoint> legacyPoints(
            kInitialMonsterSpawnTable, kInitialMonsterSpawnTable + kInitialMonsterCount);
        m_respawnManager.InitializeFromPoints(legacyPoints, definition->monsterTypeId,
                                              kDefaultMapId, m_config.respawnDelayMs,
                                              nextSlotId);
        nextSlotId += static_cast<std::uint32_t>(legacyPoints.size());
    }
    std::uint32_t spawnZoneCount = 0;
    for (const auto& spawn : MonsterSpawnRegistry::Instance().AllSpawns()) {
        if (!spawn.enabled) {
            continue;
        }
        const std::vector<MonsterSpawnPoint> points = GenerateSpawnPoints(spawn);
        if (points.empty()) {
            continue;
        }
        m_respawnManager.InitializeFromPoints(points, spawn.monsterDefinitionId, spawn.mapId,
                                              spawn.respawnSeconds * 1000, nextSlotId);
        nextSlotId += static_cast<std::uint32_t>(points.size());
        ++spawnZoneCount;
    }
    for (const auto& slot : m_respawnManager.Slots()) {
        const std::uint64_t entityId = m_nextMonsterEntityId++;
        auto monster = SpawnMonsterAtSlot(slot, entityId);
        if (!monster) {
            LOG_WARN("[World] Monster spawn duplicate entityId=" + std::to_string(entityId));
            continue;
        }
        m_respawnManager.ConfirmSpawned(slot.spawnSlotId, monster->EntityId());
    }
    LOG_INFO("[World] Spawned " + std::to_string(m_monsters.Count()) + " " +
             definition->name + " monsters (" + std::to_string(m_respawnManager.SlotCount()) +
             " spawn slots from " + std::to_string(spawnZoneCount) + " zones, legacyMap1=" +
             (m_config.legacyMap1TestSpawn ? "on" : "off") + ").");
}

// 阶段17 指令二十七/二十八：按 slot 生成新怪（满 HP/无状态/Idle/target=0/满 Combat 状态；
// 正确加入 MonsterManager + MonsterSpatialGrid；AOI 由下个 tick 通知附近玩家）。
std::shared_ptr<MonsterEntity> WorldServer::SpawnMonsterAtSlot(const MonsterSpawnSlot& slot,
                                                               std::uint64_t entityId) {
    const MonsterDefinition* definition = FindMonsterDefinition(slot.monsterTypeId);
    if (!definition) {
        return nullptr;
    }
    auto monster = std::make_shared<MonsterEntity>(entityId, slot.monsterTypeId, slot.mapId,
                                                   slot.spawnX, slot.spawnY,
                                                   definition->moveSpeed);
    // 阶段14 指令五/六：战斗属性初始化（满血）；阶段16 指令二十四：Base/Derived 属性。
    monster->InitializeCombat(definition->maxHp);
    monster->SetBaseStats(definition->attackPower, definition->defense, definition->moveSpeed);
    if (!m_monsters.SpawnMonster(monster)) {
        return nullptr;
    }
    m_monsterGrid.AddMonster(monster);
    return monster;
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
    std::vector<std::chrono::steady_clock::time_point> deathTimes;
    for (const auto& monster : m_monsters.SnapshotMonsters()) {
        if (!monster->Alive() &&
            std::chrono::duration<float>(now - monster->DeadSince()).count() >=
                kMonsterDeathCleanupSeconds) {
            expired.push_back(monster->EntityId());
            deathTimes.push_back(monster->DeadSince());
        }
    }
    for (std::size_t i = 0; i < expired.size(); ++i) {
        // 阶段17 指令十八/二十二：尸体 Remove 后进入 Respawn Queue
        //（respawnTime 从 MonsterDeath 起算，尸体 3 秒包含在 8 秒内）。
        m_respawnManager.NotifyEntityRemoved(expired[i], deathTimes[i]);
        RemoveMonster(expired[i]); // 广播 MonsterDespawn(Removed)
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
    // 阶段21 指令四十六：复活保护期内怪物伤害无效（服务器 runtime flag）。
    if (target->IsRespawnProtected(std::chrono::steady_clock::now())) {
        return;
    }
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
    // 阶段21 指令四十六：主动攻击立即取消复活保护。
    attacker->ClearRespawnProtection();
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
    // 阶段17 指令七/八：击杀归属（最后致死伤害的 Player；DOT 用 StatusEffect 的
    // sourceEntityId）-> 服务器发放 EXP/Gold（先奖励，指令三十九顺序）。
    GrantMonsterReward(monster, killerCharacterId);
    // 阶段18 指令三十九：Reward 之后 GenerateLoot（服务器权威掉落）。
    GenerateMonsterDrops(monster, killerCharacterId);
    // 阶段19 指令五十三：Reward/Loot 之后 Quest Kill Progress（一只 Monster 只计
    // 一次——KillMonster 每次死亡只被调用一次；killer 离线走 DB 推进，指令六十二）。
    if (killerCharacterId != 0) {
        HandleQuestMonsterKilled(killerCharacterId, monster->MonsterTypeId());
    }
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
        // 阶段20 指令二十二：玩家死亡 → Dialogue/Shop Session 失效。
        CloseNpcSessions(victim);
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
        // 阶段17：Respawn slot 出生点同步"重新安家"——否则 respawn 仍发生在
        // 初始表位置，与 monster 当前 home 脱节（AOI/位置断言均会失配）。
        self->m_respawnManager.RelocateSlot(entityId, x, y);
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
    // 阶段21 指令四十六：主动施法立即取消复活保护。
    caster->ClearRespawnProtection();
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
        // 阶段18 指令三十一/三十二：Effective = Base/Level + Equipment + Status。
        // 装备加成并入 Base 传给统一 RecalculateDerivedStats（不另写一套属性）。
        const auto stats =
            RecalculateDerivedStats(player->BaseAttackPower() + player->EquipmentAttackBonus(),
                                    player->BaseDefense() + player->EquipmentDefenseBonus(),
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
        // 阶段21 指令四十：死亡玩家不再被 DOT 结算伤害（死亡即停止）。
        if (!player->Alive() || player->StatusEffects().Empty()) {
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

// ---------------------------------------------------------------------------
// 阶段17：成长/奖励/重生编排（服务器权威；WorldServer 只做编排，规则在
// ProgressionService / RewardService，指令九）。
// ---------------------------------------------------------------------------

void WorldServer::GrantMonsterReward(const std::shared_ptr<MonsterEntity>& monster,
                                     std::uint64_t killerCharacterId) {
    if (killerCharacterId == 0) {
        return; // 无归属（理论上不会发生：所有致死来源都带 player attackerId）
    }
    const MonsterDefinition* definition = FindMonsterDefinition(monster->MonsterTypeId());
    if (!definition) {
        return;
    }
    const std::uint32_t expGain = definition->rewardExp;
    const std::uint32_t goldGain = definition->rewardGold;
    if (expGain == 0 && goldGain == 0) {
        return;
    }
    auto killer = m_players.FindByCharacter(killerCharacterId);
    if (killer) {
        // 指令八：在线 killer —— session 结算（支持跨多级/满级封顶）。
        const auto progression =
            AddExperience(killer->Level(), killer->Experience(),
                          static_cast<std::int64_t>(expGain));
        const std::int64_t newGold = AddGold(killer->Gold(), static_cast<std::int64_t>(goldGain));
        const std::uint32_t oldLevel = killer->Level();
        killer->SetProgression(progression.exp, newGold);
        if (progression.levelUp) {
            // 指令十/十二：基础属性成长 + CurrentHp 回满；Derived 经统一重算
            //（不覆盖 StatusEffect 派生属性，指令十一）。
            killer->ApplyLevelGrowth(progression.level);
            RecalculateTargetDerivedStats(CombatEntityType::Player, killerCharacterId);
            // 升级回满血 -> 立即纠偏 HP（1s HealthSnapshot 之前先可见）。
            SendEntityHealthSnapshot(killer, CombatEntityType::Player, killerCharacterId,
                                     killer->CurrentHp(), killer->MaxHp(), killer->Alive());
        }
        // 指令十三/十六：RewardGranted 只给本人。
        SendRewardGranted(killer, monster->EntityId(), expGain, goldGain, progression.exp,
                          newGold);
        if (progression.levelUp) {
            // 指令十四/十六：LevelUpEvent 给本人 + 能看到该 Player 的附近玩家。
            std::vector<std::uint64_t> receivers =
                PlayerStatusReceivers(killerCharacterId);
            SendLevelUpEvent(receivers, killer, oldLevel, progression.level);
            LOG_INFO("[Progression] Player #" + std::to_string(killerCharacterId) + " leveled " +
                     std::to_string(oldLevel) + " -> " + std::to_string(progression.level));
            // 阶段19 指令五十六：LevelUp 后推进 ReachLevel 任务（杀怪升级同样触发）。
            HandleQuestLevelChanged(killer);
        }
        LOG_INFO("[Progression] Reward " + std::to_string(expGain) + " exp / " +
                 std::to_string(goldGain) + " gold to #" + std::to_string(killerCharacterId));
        // 指令三十四：成长写 DB（io 线程禁止同步 SQLite 写；阶段25.5 走双模式辅助）。
        PersistProgression(killerCharacterId, progression.level, progression.exp, newGold);
        return;
    }
    // 指令七：离线 killer —— 奖励入库（不能因 PlayerSession 不在线就丢失奖励）。
    PersistOfflineReward(killerCharacterId, static_cast<std::int64_t>(expGain),
                         static_cast<std::int64_t>(goldGain));
}

void WorldServer::SendRewardGranted(const std::shared_ptr<PlayerSession>& killer,
                                    std::uint64_t sourceMonsterEntityId, std::uint32_t expGain,
                                    std::uint32_t goldGain, std::int64_t newExp,
                                    std::int64_t newGold) {
    const auto payload =
        BuildRewardEvent(killer->CharacterId(), sourceMonsterEntityId, expGain, goldGain,
                         newExp, newGold, killer->Level(), ServerTimeMs());
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::RewardGranted);
    if (EncodeRewardGranted(payload, packet.payload)) {
        SendPacketToPlayer(killer, packet); // 指令十六：只给本人
    }
}

void WorldServer::SendLevelUpEvent(const std::vector<std::uint64_t>& receivers,
                                   const std::shared_ptr<PlayerSession>& player,
                                   std::uint32_t oldLevel, std::uint32_t newLevel) {
    const auto payload =
        BuildLevelUpEvent(player->CharacterId(), oldLevel, newLevel, player->Experience(),
                          ServerTimeMs());
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::LevelUpEvent);
    if (EncodeLevelUpEvent(payload, packet.payload)) {
        for (const auto characterId : receivers) {
            auto target = m_players.FindByCharacter(characterId);
            if (target) {
                SendPacketToPlayer(target, packet);
            }
        }
    }
}

void WorldServer::SendProgressionSnapshot(const std::shared_ptr<PlayerSession>& player) {
    const auto payload =
        BuildProgressionSnapshot(player->CharacterId(), player->Level(), player->Experience(),
                                 player->Gold(), ServerTimeMs());
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::ProgressionSnapshot);
    if (EncodeProgressionSnapshot(payload, packet.payload)) {
        SendPacketToPlayer(player, packet); // 指令十五：本人纠偏
    }
}

void WorldServer::ScheduleProgressionSnapshotTick() {
    if (m_stopped.load()) {
        return;
    }
    m_progressionTimer.expires_after(
        std::chrono::milliseconds(m_config.progressionSnapshotIntervalMs));
    auto self = shared_from_this();
    m_progressionTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->SendProgressionSnapshots();
        self->ScheduleProgressionSnapshotTick();
    });
}

void WorldServer::SendProgressionSnapshots() {
    // 指令十五：每 30s 对每个在线玩家发本人 ProgressionSnapshot 纠偏。
    for (const auto& player : m_players.SnapshotPlayers()) {
        SendProgressionSnapshot(player);
    }
}

void WorldServer::ScheduleRespawnTick() {
    if (m_stopped.load()) {
        return;
    }
    m_respawnTimer.expires_after(std::chrono::milliseconds(m_config.respawnTickMs));
    auto self = shared_from_this();
    m_respawnTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->RunRespawnTick();
        self->ScheduleRespawnTick();
    });
}

void WorldServer::RunRespawnTick() {
    // 指令二十四/二十五/二十七：250ms 轮询到点 slot -> 生成全新 MonsterEntity
    //（新 entityId、满 HP、无状态、Idle、target=0）并重新加入 Manager/Grid；
    // AOI 附近玩家经下个 AOI tick 收到 MonsterSpawn（不全图广播，指令二十八）。
    const auto now = std::chrono::steady_clock::now();
    for (const auto& due : m_respawnManager.Poll(now)) {
        const std::uint64_t entityId = m_nextMonsterEntityId++;
        auto monster = SpawnMonsterAtSlot(due.slot, entityId);
        if (!monster) {
            continue; // entityId 冲突等异常：放弃本轮（队列不重试，防风暴）
        }
        m_respawnManager.ConfirmSpawned(due.spawnSlotId, monster->EntityId());
        LOG_INFO("[Respawn] slot " + std::to_string(due.spawnSlotId) + " -> new Monster #" +
                 std::to_string(monster->EntityId()));
    }
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

// ---------------------------------------------------------------------------
// 阶段18：服务器权威掉落/背包/装备（指令一~五十七）
// ---------------------------------------------------------------------------

void WorldServer::GenerateMonsterDrops(const std::shared_ptr<MonsterEntity>& monster,
                                       std::uint64_t killerCharacterId) {
    // 指令三十九：MonsterDeath -> GenerateLoot（Gold/EXP 已由 GrantMonsterReward 处理）。
    // 阶段23 23.16/23.17：掉落表从 LootTableRegistry 读取（monsterDefinition.lootTableId），
    // Client 永远不决定掉落；无表（lootTableId=0 或未找到）→ 无掉落。
    if (!m_dropRoller) {
        return; // Stop 后残余调用防御
    }
    const MonsterDefinition* monsterDefinition =
        MonsterDefinitionRegistry::Instance().Find(monster->MonsterTypeId());
    if (monsterDefinition == nullptr || monsterDefinition->lootTableId == 0) {
        return;
    }
    const LootTableDefinition* table =
        LootTableRegistry::Instance().Find(monsterDefinition->lootTableId);
    if (table == nullptr || !table->enabled || table->entries.empty()) {
        return;
    }
    // 23.16：LootTableEntry → DropRoller::Entry（dropChance 语义一致）。
    std::vector<DropRoller::Entry> rollTable;
    rollTable.reserve(table->entries.size());
    for (const auto& entry : table->entries) {
        rollTable.push_back({entry.itemDefinitionId, entry.dropChance, entry.minQuantity,
                             entry.maxQuantity});
    }
    if (m_config.testForceDropAll) {
        // 阶段18 测试专用：概率确定性（链路完全不变，见阶段十七 SetTestLootOverride 先例）。
        for (auto& entry : rollTable) {
            entry.chance = 1.0;
        }
    }
    const auto results = m_dropRoller->Roll(rollTable);
    if (results.empty()) {
        return;
    }
    const auto now = std::chrono::steady_clock::now();
    // 指令十四：死亡位置 + 轻微确定性偏移（不生成到地图外——偏移 <= 24 单位）。
    static constexpr float kDropOffsets[][2] = {
        {0.0f, 0.0f}, {14.0f, 0.0f}, {0.0f, 14.0f}, {-14.0f, 0.0f}, {0.0f, -14.0f},
    };
    std::size_t dropIndex = 0;
    for (const auto& result : results) {
        const ItemDefinition* definition = m_itemRegistry.Find(result.definitionId);
        if (!definition) {
            continue;
        }
        WorldItemDrop drop;
        drop.dropEntityId = m_nextItemDropId++; // 指令十三：与 instanceId 严格区分
        drop.itemDefinitionId = result.definitionId;
        drop.quantity = result.quantity;
        drop.mapId = monster->MapId();
        const auto& offset = kDropOffsets[dropIndex % 5];
        drop.x = monster->PositionX() + offset[0];
        drop.y = monster->PositionY() + offset[1];
        // 指令十五/四十/四十一：owner = 击杀者（含离线/DOT source），独占 10s（配置可调）。
        drop.ownerCharacterId = killerCharacterId;
        drop.ownerUntil = now + std::chrono::milliseconds(m_config.itemOwnerLockMs);
        drop.expireAt = now + std::chrono::milliseconds(m_config.itemDropTtlMs);
        drop.active = true;
        m_itemDrops.Add(drop);
        LOG_INFO("[ItemDrop] drop=" + std::to_string(drop.dropEntityId) + " item=" +
                 std::to_string(drop.itemDefinitionId) + " x" + std::to_string(drop.quantity) +
                 " at (" + std::to_string(drop.x) + "," + std::to_string(drop.y) + ") owner=#" +
                 std::to_string(killerCharacterId));
        ++dropIndex;
    }
}

void WorldServer::ScheduleItemDropTick() {
    if (m_stopped.load()) {
        return;
    }
    m_itemDropTimer.expires_after(std::chrono::milliseconds(m_config.itemDropTickMs));
    auto self = shared_from_this();
    m_itemDropTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->RunItemDropTick();
        self->ScheduleItemDropTick();
    });
}

void WorldServer::RunItemDropTick() {
    // 指令四十三：统一 500ms 扫描 TTL 过期（不建 per-drop Timer）。
    const auto now = std::chrono::steady_clock::now();
    for (const auto& expired : m_itemDrops.ExpireScan(now)) {
        NotifyItemDropGoneToObservers(expired.dropEntityId, ItemDespawnReason::Expired);
        LOG_INFO("[ItemDrop] drop=" + std::to_string(expired.dropEntityId) + " expired (TTL)");
    }
}

void WorldServer::UpdatePlayerItemDropVisibility(const std::shared_ptr<PlayerSession>& player,
                                                 bool initialVisibility) {
    // 指令十八：Enter 600 / Leave 700 滞回；visibleItemDrops 由服务器权威维护。
    auto candidates = m_itemDrops.QueryNearby(player->PositionX(), player->PositionY(),
                                              m_config.aoiLeaveRadius, player->MapId());
    const DropAoiDelta delta =
        ResolveDropAoiVisibility(candidates, player->MapId(), player->VisibleItemDrops(),
                                 m_config.aoiEnterRadius, m_config.aoiLeaveRadius,
                                 m_config.aoiVisibleLimit);
    for (const auto* drop : delta.spawns) {
        SendWorldItemSpawn(player, *drop);
        player->AddVisibleItemDrop(drop->dropEntityId);
        LOG_DEBUG("[ItemDrop] AOI enter char=" + std::to_string(player->CharacterId()) +
                  " drop=" + std::to_string(drop->dropEntityId));
    }
    if (!initialVisibility) {
        for (const auto dropId : delta.despawns) {
            SendWorldItemDespawn(player, dropId, ItemDespawnReason::ServerCleanup);
            player->EraseVisibleItemDrop(dropId);
            LOG_DEBUG("[ItemDrop] AOI leave char=" + std::to_string(player->CharacterId()) +
                      " drop=" + std::to_string(dropId));
        }
    }
}

void WorldServer::NotifyItemDropGoneToObservers(std::uint64_t dropEntityId,
                                                ItemDespawnReason reason) {
    // 指令四十二：所有可见该掉落的玩家收到 Despawn 并清除 visibleItemDrops。
    for (const auto& observer : m_players.SnapshotPlayers()) {
        if (observer->EraseVisibleItemDrop(dropEntityId)) {
            SendWorldItemDespawn(observer, dropEntityId, reason);
        }
    }
}

void WorldServer::SendWorldItemSpawn(const std::shared_ptr<PlayerSession>& receiver,
                                     const WorldItemDrop& drop) {
    // 指令二十：不直接暴露完整 owner——isOwnedByYou + ownerLockRemainingMs。
    const auto now = std::chrono::steady_clock::now();
    WorldItemSpawnPayload payload;
    payload.dropEntityId = drop.dropEntityId;
    payload.itemDefinitionId = drop.itemDefinitionId;
    payload.quantity = drop.quantity;
    payload.mapId = drop.mapId;
    payload.x = drop.x;
    payload.y = drop.y;
    payload.isOwnedByYou =
        drop.ownerCharacterId != 0 && drop.ownerCharacterId == receiver->CharacterId();
    payload.ownerLockRemainingMs =
        now >= drop.ownerUntil
            ? 0
            : static_cast<std::uint32_t>(
                std::chrono::duration_cast<std::chrono::milliseconds>(drop.ownerUntil - now)
                    .count());
    payload.serverTime = ServerTimeMs();
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::WorldItemSpawn);
    if (EncodeWorldItemSpawn(payload, out.payload)) {
        SendPacketToPlayer(receiver, out);
    }
}

void WorldServer::SendWorldItemDespawn(const std::shared_ptr<PlayerSession>& receiver,
                                       std::uint64_t dropEntityId,
                                       ItemDespawnReason reason) {
    WorldItemDespawnPayload payload;
    payload.dropEntityId = dropEntityId;
    payload.reason = static_cast<std::uint8_t>(reason);
    payload.serverTime = ServerTimeMs();
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::WorldItemDespawn);
    if (EncodeWorldItemDespawn(payload, out.payload)) {
        SendPacketToPlayer(receiver, out);
    }
}

void WorldServer::SendItemPickupResponse(const std::shared_ptr<PlayerSession>& player,
                                         std::uint64_t requestId, std::uint64_t dropEntityId,
                                         bool success, ItemResultCode code) {
    ItemPickupResponsePayload payload;
    payload.requestId = requestId;
    payload.dropEntityId = dropEntityId;
    payload.success = success;
    payload.resultCode = static_cast<std::uint8_t>(code);
    payload.serverTime = ServerTimeMs();
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::ItemPickupResponse);
    if (EncodeItemPickupResponse(payload, out.payload)) {
        SendPacketToPlayer(player, out);
    }
}

void WorldServer::HandleItemPickupRequest(std::uint64_t connectionId,
                                          const legend::network::Packet& packet) {
    // 指令八十六：Malformed 回 MalformedRequest（requestId 尽力回显），不断开。
    ItemPickupRequestPayload request;
    std::string decodeError;
    if (!DecodeItemPickupRequest(packet.payload.data(), packet.payload.size(), request,
                                       decodeError)) {
        LOG_INFO("[Item] Malformed ItemPickupRequest from #" + std::to_string(connectionId));
        SendItemPickupResponse(nullptr, request.requestId, request.dropEntityId, false,
                               ItemResultCode::MalformedRequest);
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        SendItemPickupResponse(nullptr, request.requestId, request.dropEntityId, false,
                               ItemResultCode::NotInWorld);
        return;
    }
    // 指令二十二：死亡玩家不能拾取。
    if (!player->Alive()) {
        SendItemPickupResponse(player, request.requestId, request.dropEntityId, false,
                               ItemResultCode::Dead);
        return;
    }
    // 指令三十六：重复成功 requestId 不重复获得物品。
    if (player->IsRecentItemRequest(request.requestId)) {
        SendItemPickupResponse(player, request.requestId, request.dropEntityId, false,
                               ItemResultCode::DuplicateRequest);
        return;
    }
    const WorldItemDrop* drop = m_itemDrops.Find(request.dropEntityId);
    if (!drop || !drop->active) {
        SendItemPickupResponse(player, request.requestId, request.dropEntityId, false,
                               ItemResultCode::DropNotFound);
        return;
    }
    // 指令二十二：必须在 visibleItemDrops（服务器 AOI 权威，防远程作弊）。
    if (player->VisibleItemDrops().count(request.dropEntityId) == 0) {
        SendItemPickupResponse(player, request.requestId, request.dropEntityId, false,
                               ItemResultCode::NotVisible);
        return;
    }
    if (drop->mapId != player->MapId()) {
        SendItemPickupResponse(player, request.requestId, request.dropEntityId, false,
                               ItemResultCode::WrongMap);
        return;
    }
    const float dx = drop->x - player->PositionX();
    const float dy = drop->y - player->PositionY();
    if (dx * dx + dy * dy > m_config.itemPickupRange * m_config.itemPickupRange) {
        SendItemPickupResponse(player, request.requestId, request.dropEntityId, false,
                               ItemResultCode::TooFar);
        return;
    }
    // 指令十五：归属他人且仍在独占期内 -> 拒绝。
    const auto now = std::chrono::steady_clock::now();
    if (drop->ownerCharacterId != 0 && drop->ownerCharacterId != player->CharacterId() &&
        now < drop->ownerUntil) {
        SendItemPickupResponse(player, request.requestId, request.dropEntityId, false,
                               ItemResultCode::OwnerLocked);
        return;
    }
    // 指令二十五：背包空间预检（拷贝容器试算，满则 Drop 留在地上）。
    InventoryContainer trial = player->Inventory();
    const auto addResult =
        trial.Add(m_itemRegistry, drop->itemDefinitionId, drop->quantity, 0, 0);
    if (addResult.code == InventoryAddCode::Full) {
        SendItemPickupResponse(player, request.requestId, request.dropEntityId, false,
                               ItemResultCode::InventoryFull);
        return;
    }
    // 指令二十三/二十四：remove-before-grant（single-thread world authority；
    // claim 失败 = 同 tick 已被抢走 -> DropNotFound）。
    WorldItemDrop claimed;
    if (!m_itemDrops.Claim(request.dropEntityId, claimed)) {
        SendItemPickupResponse(player, request.requestId, request.dropEntityId, false,
                               ItemResultCode::DropNotFound);
        return;
    }
    // 提交背包变更（io 线程权威）。
    player->Inventory() = trial;
    // 指令三十六：只缓存成功请求。
    player->RememberItemRequest(request.requestId);
    LOG_INFO("[Item] Pickup char=" + player->CharacterName() + " drop=" +
             std::to_string(claimed.dropEntityId) + " item=" +
             std::to_string(claimed.itemDefinitionId) + " x" +
             std::to_string(claimed.quantity));
    // 指令二十四：claimed -> DB 事务 -> 成功后 Delta + Despawn；失败回滚恢复 Drop。
    // 阶段25.5：应用逻辑（io 线程）由 legacy/RPC 两路共用（applyResult）。
    auto self = shared_from_this();
    const std::uint64_t characterId = player->CharacterId();
    const std::uint32_t definitionId = claimed.itemDefinitionId;
    const std::uint32_t quantity = claimed.quantity;
    const std::uint64_t requestId = request.requestId;
    const std::uint64_t dropEntityId = claimed.dropEntityId;
    const bool mergedIntoStack = addResult.code == InventoryAddCode::Merged;
    const std::uint32_t bagSlotIndex = addResult.slotIndex;
    // 并入堆叠：捕获既有 instanceId 与合并后总量（提交后读取，同 io 线程一致）。
    std::uint64_t mergeInstanceId = 0;
    std::uint32_t mergeQuantity = 0;
    if (mergedIntoStack) {
        const InventoryEntry* current = player->Inventory().At(bagSlotIndex);
        if (current) {
            mergeInstanceId = current->instanceId;
            mergeQuantity = current->quantity;
        }
    }
    const auto applyResult = [self, characterId, requestId, dropEntityId, bagSlotIndex,
                              definitionId, quantity, mergedIntoStack,
                              claimed](bool dbOk, std::uint64_t newInstanceId) {
        self->m_service.Post([self, characterId, requestId, dropEntityId, bagSlotIndex,
                              definitionId, quantity, mergedIntoStack, dbOk, newInstanceId,
                              claimed]() {
            if (self->m_stopped.load()) {
                return;
            }
            auto player = self->m_players.FindByCharacter(characterId);
            if (!player) {
                return; // 玩家已离线：DB 已持久化，重进加载
            }
            if (!dbOk) {
                // 指令二十四：DB 失败 -> 回滚背包 + 恢复 Drop（不能吞物品）。
                if (mergedIntoStack) {
                    InventoryEntry* entry = player->Inventory().MutableAt(bagSlotIndex);
                    if (entry) {
                        entry->quantity =
                            entry->quantity > quantity ? entry->quantity - quantity : 0;
                    }
                } else {
                    InventoryEntry removed;
                    (void)player->Inventory().TakeAt(bagSlotIndex, removed);
                }
                self->m_itemDrops.Restore(claimed);
                LOG_ERROR("[Item] pickup DB failed -> drop " + std::to_string(dropEntityId) +
                          " restored");
                self->SendItemPickupResponse(player, requestId, dropEntityId, false,
                                             ItemResultCode::InternalError);
                return;
            }
            // 成功：回填 instanceId（新堆叠）。
            if (!mergedIntoStack && newInstanceId != 0) {
                InventoryEntry entry;
                if (player->Inventory().TakeAt(bagSlotIndex, entry)) {
                    entry.instanceId = newInstanceId;
                    player->Inventory().PutAt(bagSlotIndex, entry);
                }
            }
            // Delta（opcode 1 = Set）：merged 取容器当前总量，新堆叠取插入值。
            const InventoryEntry* current = player->Inventory().At(bagSlotIndex);
            InventoryEntry deltaEntry;
            if (current) {
                deltaEntry = *current;
            } else {
                deltaEntry.definitionId = definitionId;
                deltaEntry.quantity = quantity;
                deltaEntry.instanceId = newInstanceId;
            }
            self->SendInventoryDelta(player, 1, deltaEntry, bagSlotIndex);
            self->SendItemPickupResponse(player, requestId, dropEntityId, true,
                                         ItemResultCode::Success);
            // 指令五十四：成功 Pickup 后重算 Collect 任务（"当前拥有数量"型）。
            self->HandleQuestInventoryChanged(player);
            // 指令二十四：Despawn 广播给所有可见者（含本人）。
            self->NotifyItemDropGoneToObservers(dropEntityId,
                                                ItemDespawnReason::PickedUp);
        });
    };
    if (m_persistence) {
        // 阶段25.5：RPC 模式——写 inventory_items 事务在 DbServer 内执行。
        legend::internal::WorldItemInsert command;
        command.characterId = characterId;
        command.definitionId = definitionId;
        command.quantity = quantity;
        command.slotIndex = static_cast<std::int64_t>(bagSlotIndex);
        command.createdAt = legend::account::UnixNow();
        command.mergedIntoStack = mergedIntoStack;
        command.mergeInstanceId = mergeInstanceId;
        command.mergeQuantity = mergeQuantity;
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldItemInsert(command, payload)) {
            applyResult(false, 0);
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::ItemInsertWrite, std::move(payload),
            [applyResult](const legend::internal::DbResponse& response) {
                if (response.errorCode != legend::internal::InternalErrorCode::Ok) {
                    applyResult(false, 0);
                    return;
                }
                legend::internal::WorldItemInsertResult result;
                std::string error;
                if (!legend::internal::DecodeWorldItemInsertResult(
                        response.payload.data(), response.payload.size(), result, error)) {
                    applyResult(false, 0);
                    return;
                }
                applyResult(true, result.newInstanceId);
            });
        return;
    }
    m_dbWorker.Post([self, applyResult, characterId, definitionId, quantity, bagSlotIndex,
                     mergedIntoStack, mergeInstanceId, mergeQuantity]() {
        // DB 线程：写 inventory_items（prepared statement，指令五十四）。
        bool dbOk = false;
        std::uint64_t newInstanceId = 0;
        if (mergedIntoStack) {
            dbOk = mergeInstanceId != 0 &&
                   InventoryRepository::UpdateQuantity(self->m_database, mergeInstanceId,
                                                       mergeQuantity);
        } else {
            newInstanceId = InventoryRepository::InsertItem(
                self->m_database, characterId, definitionId, quantity,
                static_cast<std::int64_t>(bagSlotIndex), legend::account::UnixNow());
            dbOk = newInstanceId != 0;
        }
        applyResult(dbOk, newInstanceId);
    });
}

void WorldServer::HandleEquipItemRequest(std::uint64_t connectionId,
                                         const legend::network::Packet& packet) {
    EquipItemRequestPayload request;
    std::string decodeError;
    if (!DecodeEquipItemRequest(packet.payload.data(), packet.payload.size(), request,
                                      decodeError)) {
        LOG_INFO("[Item] Malformed EquipItemRequest from #" + std::to_string(connectionId));
        SendEquipItemResponse(nullptr, request.requestId, false,
                              ItemResultCode::MalformedRequest, EquipmentSlot::None);
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        SendEquipItemResponse(nullptr, request.requestId, false,
                              ItemResultCode::NotInWorld, EquipmentSlot::None);
        return;
    }
    // 阶段21 指令三十二：死亡状态禁止装备操作。
    if (!player->Alive()) {
        SendEquipItemResponse(player, request.requestId, false, ItemResultCode::Dead,
                              EquipmentSlot::None);
        return;
    }
    // 指令三十七：防重放。
    if (player->IsRecentItemRequest(request.requestId)) {
        SendEquipItemResponse(player, request.requestId, false,
                              ItemResultCode::DuplicateRequest,
                              EquipmentSlot::None);
        return;
    }
    const auto result =
        EquipmentService::Equip(m_itemRegistry, player->Inventory(), request.slotIndex,
                                player->EquipmentRef());
    if (result.code == EquipResultCode::InvalidSlot) {
        SendEquipItemResponse(player, request.requestId, false, ItemResultCode::InvalidItem,
                              EquipmentSlot::None);
        return;
    }
    if (result.code == EquipResultCode::InvalidItem) {
        SendEquipItemResponse(player, request.requestId, false, ItemResultCode::InvalidItem,
                              EquipmentSlot::None);
        return;
    }
    if (result.code == EquipResultCode::WrongSlot) {
        SendEquipItemResponse(player, request.requestId, false, ItemResultCode::WrongSlot,
                              EquipmentSlot::None);
        return;
    }
    // instanceId 待持久化（拾取 DB 未完成）-> 拒绝（防孤儿装备行）。
    if (result.equipped.instanceId == 0) {
        // 回滚内存状态：新装备回原槽；Replaced 时装备槽恢复旧装备。
        if (result.code == EquipResultCode::Replaced) {
            if (result.slot == EquipmentSlot::Weapon) {
                player->EquipmentRef().weapon = result.unequipped;
            } else {
                player->EquipmentRef().armor = result.unequipped;
            }
        } else if (result.slot == EquipmentSlot::Weapon) {
            player->EquipmentRef().weapon = InventoryEntry{};
        } else {
            player->EquipmentRef().armor = InventoryEntry{};
        }
        (void)player->Inventory().PutAt(request.slotIndex, result.equipped);
        SendEquipItemResponse(player, request.requestId, false,
                              ItemResultCode::InternalError, EquipmentSlot::None);
        return;
    }
    // 指令三十七：只缓存成功请求。
    player->RememberItemRequest(request.requestId);
    LOG_INFO("[Item] Equip char=" + player->CharacterName() + " instance=" +
             std::to_string(result.equipped.instanceId) + " slot=" +
             std::to_string(static_cast<int>(result.slot)));
    RefreshEquipmentBonuses(player);
    // 指令三十二：装备变化走统一 Derived 重算（不覆盖状态加成）。
    RecalculateTargetDerivedStats(CombatEntityType::Player, player->CharacterId());
    auto self = shared_from_this();
    const std::uint64_t characterId = player->CharacterId();
    const std::uint64_t requestId = request.requestId;
    const EquipmentSlot slot = result.slot;
    InventoryRepository::EquipTransaction tx;
    tx.characterId = characterId;
    tx.newItemInstanceId = result.equipped.instanceId;
    tx.equipmentSlotCode = static_cast<std::int64_t>(
        result.slot == EquipmentSlot::Weapon
            ? InventoryRepository::kWeaponSlotCode
            : InventoryRepository::kArmorSlotCode);
    tx.hadPrevious = result.code == EquipResultCode::Replaced;
    tx.previousInstanceId = tx.hadPrevious ? result.unequipped.instanceId : 0;
    tx.freedBagSlotIndex = static_cast<std::int64_t>(request.slotIndex);
    const auto applyResult = [self, characterId, requestId, slot](bool dbOk) {
        self->m_service.Post([self, characterId, requestId, slot, dbOk]() {
            if (self->m_stopped.load()) {
                return;
            }
            auto player = self->m_players.FindByCharacter(characterId);
            if (!player) {
                return;
            }
            // 指令二十八/二十九：装备变化后以 Snapshot 全量纠偏（Client 只是镜像）。
            self->SendInventorySnapshot(player);
            self->SendEquipmentSnapshot(player);
            self->SendEquipItemResponse(player, requestId, dbOk,
                                        dbOk ? ItemResultCode::Success
                                             : ItemResultCode::InternalError,
                                        slot);
        });
    };
    if (m_persistence) {
        // 阶段25.5：RPC 模式——装备原子事务在 DbServer 内执行。
        legend::internal::WorldEquipItem command;
        command.characterId = tx.characterId;
        command.newItemInstanceId = tx.newItemInstanceId;
        command.equipmentSlotCode = tx.equipmentSlotCode;
        command.hadPrevious = tx.hadPrevious;
        command.previousInstanceId = tx.previousInstanceId;
        command.freedBagSlotIndex = tx.freedBagSlotIndex;
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldEquipItem(command, payload)) {
            applyResult(false);
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::EquipItemWrite, std::move(payload),
            [applyResult](const legend::internal::DbResponse& response) {
                applyResult(response.errorCode == legend::internal::InternalErrorCode::Ok);
            });
        return;
    }
    m_dbWorker.Post([self, applyResult, tx]() {
        const bool dbOk = InventoryRepository::RunEquipTransaction(self->m_database, tx);
        applyResult(dbOk);
    });
}

void WorldServer::HandleUnequipItemRequest(std::uint64_t connectionId,
                                           const legend::network::Packet& packet) {
    UnequipItemRequestPayload request;
    std::string decodeError;
    if (!DecodeUnequipItemRequest(packet.payload.data(), packet.payload.size(), request,
                                        decodeError)) {
        LOG_INFO("[Item] Malformed UnequipItemRequest from #" + std::to_string(connectionId));
        SendUnequipItemResponse(nullptr, request.requestId, false,
                                ItemResultCode::MalformedRequest,
                                EquipmentSlot::None);
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        SendUnequipItemResponse(nullptr, request.requestId, false,
                                ItemResultCode::NotInWorld, EquipmentSlot::None);
        return;
    }
    if (player->IsRecentItemRequest(request.requestId)) {
        SendUnequipItemResponse(player, request.requestId, false,
                                ItemResultCode::DuplicateRequest,
                                EquipmentSlot::None);
        return;
    }
    const auto slot = static_cast<EquipmentSlot>(request.equipmentSlot);
    const auto result =
        EquipmentService::Unequip(m_itemRegistry, player->Inventory(), slot,
                                  player->EquipmentRef());
    if (result.code == EquipResultCode::InvalidSlot) {
        SendUnequipItemResponse(player, request.requestId, false,
                                ItemResultCode::NotEquipped, slot);
        return;
    }
    if (result.code == EquipResultCode::InvalidItem) {
        SendUnequipItemResponse(player, request.requestId, false,
                                ItemResultCode::InvalidItem, slot);
        return;
    }
    if (result.code == EquipResultCode::BagFull) {
        // 指令三十四：背包满 -> 失败且装备保持不变。
        SendUnequipItemResponse(player, request.requestId, false,
                                ItemResultCode::InventoryFull, slot);
        return;
    }
    // instanceId 待持久化 -> 回滚并拒绝。
    if (result.unequipped.instanceId == 0) {
        InventoryEntry back;
        (void)player->Inventory().TakeAt(result.bagSlotIndex, back);
        if (slot == EquipmentSlot::Weapon) {
            player->EquipmentRef().weapon = back;
        } else {
            player->EquipmentRef().armor = back;
        }
        SendUnequipItemResponse(player, request.requestId, false,
                                ItemResultCode::InternalError, slot);
        return;
    }
    player->RememberItemRequest(request.requestId);
    LOG_INFO("[Item] Unequip char=" + player->CharacterName() + " instance=" +
             std::to_string(result.unequipped.instanceId) + " -> bag slot " +
             std::to_string(result.bagSlotIndex));
    RefreshEquipmentBonuses(player);
    RecalculateTargetDerivedStats(CombatEntityType::Player, player->CharacterId());
    auto self = shared_from_this();
    const std::uint64_t characterId = player->CharacterId();
    const std::uint64_t requestId = request.requestId;
    InventoryRepository::UnequipTransaction tx;
    tx.instanceId = result.unequipped.instanceId;
    tx.equipmentSlotCode =
        slot == EquipmentSlot::Weapon ? InventoryRepository::kWeaponSlotCode
                                            : InventoryRepository::kArmorSlotCode;
    tx.bagSlotIndex = static_cast<std::int64_t>(result.bagSlotIndex);
    const auto applyResult = [self, characterId, requestId, slot](bool dbOk) {
        self->m_service.Post([self, characterId, requestId, slot, dbOk]() {
            if (self->m_stopped.load()) {
                return;
            }
            auto player = self->m_players.FindByCharacter(characterId);
            if (!player) {
                return;
            }
            self->SendInventorySnapshot(player);
            self->SendEquipmentSnapshot(player);
            self->SendUnequipItemResponse(player, requestId, dbOk,
                                          dbOk ? ItemResultCode::Success
                                               : ItemResultCode::InternalError,
                                          slot);
        });
    };
    if (m_persistence) {
        // 阶段25.5：RPC 模式——卸下原子事务在 DbServer 内执行。
        legend::internal::WorldUnequipItem command;
        command.instanceId = tx.instanceId;
        command.equipmentSlotCode = tx.equipmentSlotCode;
        command.bagSlotIndex = tx.bagSlotIndex;
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldUnequipItem(command, payload)) {
            applyResult(false);
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::UnequipItemWrite, std::move(payload),
            [applyResult](const legend::internal::DbResponse& response) {
                applyResult(response.errorCode == legend::internal::InternalErrorCode::Ok);
            });
        return;
    }
    m_dbWorker.Post([self, applyResult, tx]() {
        const bool dbOk = InventoryRepository::RunUnequipTransaction(self->m_database, tx);
        applyResult(dbOk);
    });
}

void WorldServer::SendEquipItemResponse(const std::shared_ptr<PlayerSession>& player,
                                        std::uint64_t requestId, bool success,
                                        ItemResultCode code, EquipmentSlot slot) {
    EquipItemResponsePayload payload;
    payload.requestId = requestId;
    payload.success = success;
    payload.resultCode = static_cast<std::uint8_t>(code);
    payload.equipmentSlot = static_cast<std::uint8_t>(slot);
    payload.serverTime = ServerTimeMs();
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::EquipItemResponse);
    if (EncodeEquipItemResponse(payload, out.payload)) {
        SendPacketToPlayer(player, out);
    }
}

void WorldServer::SendUnequipItemResponse(const std::shared_ptr<PlayerSession>& player,
                                          std::uint64_t requestId, bool success,
                                          ItemResultCode code, EquipmentSlot slot) {
    UnequipItemResponsePayload payload;
    payload.requestId = requestId;
    payload.success = success;
    payload.resultCode = static_cast<std::uint8_t>(code);
    payload.equipmentSlot = static_cast<std::uint8_t>(slot);
    payload.serverTime = ServerTimeMs();
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::UnequipItemResponse);
    if (EncodeUnequipItemResponse(payload, out.payload)) {
        SendPacketToPlayer(player, out);
    }
}

void WorldServer::SendInventorySnapshot(const std::shared_ptr<PlayerSession>& player) {
    // 指令二十八：完整 40 格 Snapshot（Client 只是镜像，指令二十九）。
    InventorySnapshotPayload payload;
    payload.characterId = player->CharacterId();
    for (std::size_t i = 0; i < player->Inventory().SlotCount(); ++i) {
        const InventoryEntry* entry = player->Inventory().At(i);
        if (!entry || entry->quantity == 0) {
            continue;
        }
        InventoryEntryData data;
        data.instanceId = entry->instanceId;
        data.definitionId = entry->definitionId;
        data.quantity = entry->quantity;
        data.slotIndex = static_cast<std::uint32_t>(i);
        payload.entries.push_back(data);
    }
    payload.serverTime = ServerTimeMs();
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::InventorySnapshot);
    if (EncodeInventorySnapshot(payload, out.payload)) {
        SendPacketToPlayer(player, out);
    }
}

void WorldServer::SendInventoryDelta(const std::shared_ptr<PlayerSession>& player,
                                     std::uint8_t opcode, const InventoryEntry& entry,
                                     std::uint32_t slotIndex) {
    InventoryDeltaPayload payload;
    payload.characterId = player->CharacterId();
    payload.opcode = opcode;
    payload.entry.instanceId = entry.instanceId;
    payload.entry.definitionId = entry.definitionId;
    payload.entry.quantity = entry.quantity;
    payload.entry.slotIndex = slotIndex;
    payload.serverTime = ServerTimeMs();
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::InventoryDelta);
    if (EncodeInventoryDelta(payload, out.payload)) {
        SendPacketToPlayer(player, out);
    }
}

void WorldServer::SendEquipmentSnapshot(const std::shared_ptr<PlayerSession>& player) {
    EquipmentSnapshotPayload payload;
    payload.characterId = player->CharacterId();
    payload.weaponInstanceId = player->EquipmentRef().weapon.instanceId;
    payload.weaponDefinitionId = player->EquipmentRef().weapon.definitionId;
    payload.armorInstanceId = player->EquipmentRef().armor.instanceId;
    payload.armorDefinitionId = player->EquipmentRef().armor.definitionId;
    payload.equipmentAttackBonus = player->EquipmentAttackBonus();
    payload.equipmentDefenseBonus = player->EquipmentDefenseBonus();
    payload.serverTime = ServerTimeMs();
    Packet out;
    out.header.messageId = static_cast<std::uint16_t>(MessageId::EquipmentSnapshot);
    if (EncodeEquipmentSnapshot(payload, out.payload)) {
        SendPacketToPlayer(player, out);
    }
}

void WorldServer::RefreshEquipmentBonuses(const std::shared_ptr<PlayerSession>& player) {
    // 指令三十一：装备加成缓存刷新（EquipmentAttackBonus/DefenseBonus 读取依据）。
    std::uint32_t attack = 0;
    std::uint32_t defense = 0;
    if (player->EquipmentRef().weapon.quantity > 0) {
        if (const auto* def = m_itemRegistry.Find(player->EquipmentRef().weapon.definitionId)) {
            attack += def->attackBonus;
        }
    }
    if (player->EquipmentRef().armor.quantity > 0) {
        if (const auto* def = m_itemRegistry.Find(player->EquipmentRef().armor.definitionId)) {
            defense += def->defenseBonus;
        }
    }
    player->SetEquipmentBonuses(attack, defense);
}

void WorldServer::ApplyLoadedItems(const std::shared_ptr<PlayerSession>& player,
                                   const std::vector<InventoryRepository::InventoryRow>& rows) {
    // 指令二十八：进世界加载持久化背包/装备（slot_index 1001/1002 = 装备中）。
    player->Inventory().Clear();
    player->EquipmentRef() = EquipmentSlots{};
    for (const auto& row : rows) {
        InventoryEntry entry;
        entry.instanceId = row.instanceId;
        entry.definitionId = row.definitionId;
        entry.quantity = row.quantity;
        if (row.slotIndex >= InventoryRepository::kEquippedSlotBase) {
            if (row.slotIndex == InventoryRepository::kWeaponSlotCode) {
                player->EquipmentRef().weapon = entry;
            } else if (row.slotIndex == InventoryRepository::kArmorSlotCode) {
                player->EquipmentRef().armor = entry;
            }
        } else if (row.slotIndex >= 0 &&
                   row.slotIndex < static_cast<std::int64_t>(kInventorySlots)) {
            player->Inventory().PutAt(static_cast<std::size_t>(row.slotIndex), entry);
        }
    }
    RefreshEquipmentBonuses(player);
    RecalculateTargetDerivedStats(CombatEntityType::Player, player->CharacterId());
    LOG_INFO("[Inventory] loaded char #" + std::to_string(player->CharacterId()) + " rows=" +
             std::to_string(rows.size()));
}

// ---------------------------------------------------------------------------
// 阶段18：测试布景辅助（io 线程投递，与游戏逻辑串行；仅供测试白盒使用）
// ---------------------------------------------------------------------------

bool WorldServer::TestSpawnDrop(float x, float y, std::uint16_t mapId,
                                std::uint64_t ownerCharacterId, std::uint32_t definitionId) {
    if (m_stopped.load()) {
        return false;
    }
    auto self = shared_from_this();
    const auto now = std::chrono::steady_clock::now();
    m_service.Post([self, x, y, mapId, ownerCharacterId, definitionId, now]() {
        if (self->m_stopped.load()) {
            return;
        }
        WorldItemDrop drop;
        drop.dropEntityId = self->m_nextItemDropId++;
        drop.itemDefinitionId = definitionId;
        drop.quantity = 1;
        drop.mapId = mapId;
        drop.x = x;
        drop.y = y;
        drop.ownerCharacterId = ownerCharacterId;
        drop.ownerUntil = now + std::chrono::milliseconds(self->m_config.itemOwnerLockMs);
        drop.expireAt = now + std::chrono::milliseconds(self->m_config.itemDropTtlMs);
        drop.active = true;
        self->m_itemDrops.Add(drop);
        LOG_INFO("[ItemDrop] test drop=" + std::to_string(drop.dropEntityId) + " at (" +
                 std::to_string(x) + "," + std::to_string(y) + ") map=" + std::to_string(mapId));
    });
    return true;
}

bool WorldServer::TestEraseVisibleItemDrop(std::uint64_t characterId,
                                           std::uint64_t dropEntityId) {
    if (m_stopped.load()) {
        return false;
    }
    auto self = shared_from_this();
    m_service.Post([self, characterId, dropEntityId]() {
        auto player = self->m_players.FindByCharacter(characterId);
        if (player) {
            player->EraseVisibleItemDrop(dropEntityId);
        }
    });
    return true;
}

bool WorldServer::TestAddVisibleItemDrop(std::uint64_t characterId, std::uint64_t dropEntityId) {
    if (m_stopped.load()) {
        return false;
    }
    auto self = shared_from_this();
    m_service.Post([self, characterId, dropEntityId]() {
        auto player = self->m_players.FindByCharacter(characterId);
        if (player) {
            player->AddVisibleItemDrop(dropEntityId);
        }
    });
    return true;
}

bool WorldServer::TestFillInventory(std::uint64_t characterId) {
    if (m_stopped.load()) {
        return false;
    }
    auto self = shared_from_this();
    m_service.Post([self, characterId]() {
        auto player = self->m_players.FindByCharacter(characterId);
        if (!player) {
            return;
        }
        player->Inventory().Clear();
        for (std::size_t i = 0; i < player->Inventory().SlotCount(); ++i) {
            InventoryEntry entry;
            entry.instanceId = 900000 + i; // 伪持久 instanceId（测试布景）
            entry.definitionId = kItemSlimeCoreId;
            entry.quantity = kSlimeCoreMaxStack;
            player->Inventory().PutAt(i, entry);
        }
        LOG_INFO("[Inventory] test-filled char #" + std::to_string(characterId));
    });
    return true;
}

bool WorldServer::TestMarkPlayerDead(std::uint64_t characterId) {
    if (m_stopped.load()) {
        return false;
    }
    auto self = shared_from_this();
    m_service.Post([self, characterId]() {
        auto player = self->m_players.FindByCharacter(characterId);
        if (player) {
            player->MarkDead(); // 指令二十二：死亡玩家拾取由服务器拒绝
        }
    });
    return true;
}

bool WorldServer::TestRevivePlayer(std::uint64_t characterId) {
    if (m_stopped.load()) {
        return false;
    }
    auto self = shared_from_this();
    m_service.Post([self, characterId]() {
        auto player = self->m_players.FindByCharacter(characterId);
        if (player) {
            player->Revive(); // 测试白盒：复活 + 满血（防死亡级联影响后续检查）
        }
    });
    return true;
}

bool WorldServer::TestBuffPlayerHp(std::uint64_t characterId, std::uint32_t hp) {
    if (m_stopped.load()) {
        return false;
    }
    auto self = shared_from_this();
    m_service.Post([self, characterId, hp]() {
        auto player = self->m_players.FindByCharacter(characterId);
        if (player) {
            player->TestBuffHp(hp); // 测试白盒：抬高 HP 上限（防死亡级联）
        }
    });
    return true;
}

// ---------------------------------------------------------------------------
// 阶段19：服务器权威任务（Quest Core V0.19）
// 职责分离（指令一百四十四）：QuestRegistry 定义 / QuestService 规则 /
// QuestRepository 数据库 / PlayerQuestContainer 状态 / WorldServer 只做编排。
// ---------------------------------------------------------------------------

namespace {

// 指令二十四/五十四：Collect 用"当前拥有数量"——只统计背包 Material
//（Equip/Unequip 不影响 Material 数量，指令五十四）。
std::uint32_t OwnedMaterialCount(const PlayerSession& player, std::uint32_t definitionId) {
    std::uint32_t total = 0;
    for (std::size_t i = 0; i < player.Inventory().SlotCount(); ++i) {
        const InventoryEntry* entry = player.Inventory().At(i);
        if (entry != nullptr && entry->definitionId == definitionId) {
            total += entry->quantity;
        }
    }
    return total;
}

QuestService::OwnedCountFn OwnedCountFnFor(const std::shared_ptr<PlayerSession>& player) {
    return [&player](std::uint32_t definitionId) {
        return OwnedMaterialCount(*player, definitionId);
    };
}

} // namespace

void WorldServer::HandleQuestAcceptRequest(std::uint64_t connectionId,
                                           const legend::network::Packet& packet) {
    // 指令八十二：Malformed 回 MalformedRequest（requestId 尽力回显），不断开。
    QuestAcceptRequestPayload request;
    std::string decodeError;
    if (!DecodeQuestAcceptRequest(packet.payload.data(), packet.payload.size(), request,
                                  decodeError)) {
        LOG_INFO("[Quest] Malformed QuestAcceptRequest from #" + std::to_string(connectionId));
        SendQuestAcceptResponse(nullptr, request.requestId, request.questId, false,
                                QuestResultCode::MalformedRequest);
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        SendQuestAcceptResponse(nullptr, request.requestId, request.questId, false,
                                QuestResultCode::NotInWorld);
        return;
    }
    // 阶段20 指令二十九：核心校验/接取抽为 AcceptQuestForPlayer（NPC Option 复用同一套）。
    const QuestResultCode code = AcceptQuestForPlayer(player, request.questId);
    SendQuestAcceptResponse(player, request.requestId, request.questId, code == QuestResultCode::Success,
                            code);
}

QuestResultCode WorldServer::AcceptQuestForPlayer(const std::shared_ptr<PlayerSession>& player,
                                                  QuestId questId) {
    // 指令二十：校验链。
    const QuestDefinition* definition = QuestRegistry::Instance().FindQuest(questId);
    QuestResultCode code = QuestResultCode::Success;
    if (definition == nullptr) {
        code = QuestResultCode::UnknownQuest;
    } else if (!player->Alive()) {
        code = QuestResultCode::Dead;
    } else if (player->Level() < definition->minLevel) {
        code = QuestResultCode::LevelTooLow;
    } else if (definition->prerequisiteQuestId != 0 &&
               !player->Quests().IsCompleted(definition->prerequisiteQuestId)) {
        // 指令六十五/六十六：ReadyToTurnIn / Abandoned 都不算 Completed。
        code = QuestResultCode::PrerequisiteNotMet;
    } else if (player->Quests().IsActiveOrReady(questId)) {
        code = QuestResultCode::AlreadyAccepted;
    } else if (player->Quests().IsCompleted(questId)) {
        // 指令二十：任务没有 Completed 且不可重复（阶段19 全部不可重复）。
        code = QuestResultCode::AlreadyCompleted;
    } else if (player->Quests().CountActive() >= kMaxActiveQuests) {
        // 指令二十一：同时进行中任务上限 20（Completed 不算进行中）。
        code = QuestResultCode::QuestLogFull;
    }
    if (code != QuestResultCode::Success) {
        return code;
    }
    // 指令二十三：接取初始化（InProgress + 全部进度 0 + ReachLevel/Collect
    // 立即初始校验）。
    auto changes = QuestService::AcceptQuest(QuestRegistry::Instance(), player->Quests(),
                                             questId, player->Level(),
                                             OwnedCountFnFor(player), legend::account::UnixNow());
    auto stateChanges = QuestService::EvaluateQuestCompletion(
        QuestRegistry::Instance(), player->Quests(), legend::account::UnixNow());
    LOG_INFO("[Quest] accepted quest=" + std::to_string(questId) + " char=" +
             player->CharacterName());
    // 指令十七：接取写 DB（先于进度写——DbWorker FIFO 保证顺序）。
    {
        std::vector<std::uint32_t> objectiveIds;
        for (const auto& objective : definition->objectives) {
            objectiveIds.push_back(objective.objectiveId);
        }
        // 指令十七：接取写 DB（先于进度写——FIFO/RPC 单连接保序）。
        PersistQuestInsert(player->CharacterId(), questId,
                           static_cast<std::uint8_t>(QuestState::InProgress),
                           legend::account::UnixNow(), objectiveIds);
    }
    SendQuestStateChanged(player, questId, QuestState::NotAccepted, QuestState::InProgress); // 指令三十二
    HandleQuestObjectiveChanges(player, changes, stateChanges);
    // 阶段20 指令三十三：任务状态变化 → 重算相关 NPC per-player Marker。
    SendNpcQuestMarkersFor(player);
    return QuestResultCode::Success;
}

void WorldServer::HandleQuestTurnInRequest(std::uint64_t connectionId,
                                           const legend::network::Packet& packet) {
    QuestTurnInRequestPayload request;
    std::string decodeError;
    if (!DecodeQuestTurnInRequest(packet.payload.data(), packet.payload.size(), request,
                                  decodeError)) {
        LOG_INFO("[Quest] Malformed QuestTurnInRequest from #" + std::to_string(connectionId));
        SendQuestTurnInResponse(nullptr, request.requestId, request.questId, false,
                                QuestResultCode::MalformedRequest);
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        SendQuestTurnInResponse(nullptr, request.requestId, request.questId, false,
                                QuestResultCode::NotInWorld);
        return;
    }
    // 指令六十：TurnIn 防重放最高优先级——重复 requestId 不重复领奖。
    if (player->IsRecentQuestRequest(request.requestId)) {
        SendQuestTurnInResponse(player, request.requestId, request.questId, false,
                                QuestResultCode::DuplicateRequest);
        return;
    }
    // 阶段20 指令三十：核心抽取为 BeginQuestTurnIn（NPC TurnIn Option 复用同一套）。
    const QuestResultCode code =
        BeginQuestTurnIn(player, request.questId, request.requestId, true);
    if (code != QuestResultCode::Success) {
        SendQuestTurnInResponse(player, request.requestId, request.questId, false, code);
    }
}

QuestResultCode WorldServer::BeginQuestTurnIn(const std::shared_ptr<PlayerSession>& player,
                                              QuestId questId, std::uint64_t requestId,
                                              bool sendResponsePacket) {
    // 指令三十四：TurnIn 校验（状态必须 ReadyToTurnIn）。
    const QuestResultCode validateCode = QuestService::ValidateTurnIn(
        player->Quests(), questId, QuestRegistry::Instance());
    if (validateCode != QuestResultCode::Success) {
        return validateCode;
    }
    const QuestDefinition* definition =
        QuestRegistry::Instance().FindQuest(questId);
    // 指令三十九：奖励含物品且背包无空间 -> InventoryFull（任务仍 ReadyToTurnIn，
    // 不先发 EXP/Gold 再因 Item 失败）。
    InventoryContainer trial = player->Inventory();
    std::int64_t rewardSlotIndex = -1;
    if (definition->reward.itemDefinitionId != 0 && definition->reward.itemQuantity > 0) {
        const auto addResult =
            trial.Add(m_itemRegistry, definition->reward.itemDefinitionId,
                      definition->reward.itemQuantity, 0, 0);
        if (addResult.code == InventoryAddCode::Full) {
            return QuestResultCode::InventoryFull;
        }
        rewardSlotIndex = static_cast<std::int64_t>(addResult.slotIndex);
    }
    // 指令六十：立即缓存 requestId（先于 DB 提交）——连发重放不重复进入结算管线
    //（请求风暴下首个提交未完成时，后续同 id 必须被拒绝）。
    if (requestId != 0) {
        player->RememberQuestRequest(requestId);
    }
    // 指令三十五/三十六/三十七：奖励复用 ProgressionService/InventoryService，
    // 不写第三套经验金币系统；EXP 奖励可能触发升级（支持跨多级）。
    const auto progression =
        AddExperience(player->Level(), player->Experience(),
                      static_cast<std::int64_t>(definition->reward.exp));
    const std::int64_t newGold = AddGold(player->Gold(),
                                         static_cast<std::int64_t>(definition->reward.gold));
    const std::uint32_t oldLevel = player->Level();
    // 指令四十：原子 TurnIn 事务（Quest Completed + 成长写回 + 物品入库）。
    auto self = shared_from_this();
    const std::uint64_t characterId = player->CharacterId();
    QuestRepository::TurnInTransaction tx;
    tx.characterId = characterId;
    tx.questId = questId;
    tx.turnedInAt = legend::account::UnixNow();
    tx.newLevel = progression.level;
    tx.newExperience = progression.exp;
    tx.newGold = newGold;
    tx.rewardItemDefinitionId = definition->reward.itemDefinitionId;
    tx.rewardItemQuantity = definition->reward.itemQuantity;
    tx.rewardItemSlotIndex = rewardSlotIndex;
    tx.rewardItemCreatedAt = tx.turnedInAt;
    // 指令四十：原子 TurnIn 事务（Quest Completed + 成长写回 + 物品入库）。
    // 阶段25.5：事务执行双模式（legacy DB Worker / DbServer RPC）；应用共用 applyResult。
    const auto applyResult = [self, characterId, questId, requestId, tx, definition, oldLevel,
                              sendResponsePacket](bool ok, std::uint64_t itemInstanceId,
                                                  const std::string& dbError) {
        self->m_service.Post([self, characterId, questId, requestId, tx, ok, itemInstanceId,
                              definition, oldLevel, dbError, sendResponsePacket]() {
            if (self->m_stopped.load()) {
                return;
            }
            auto player = self->m_players.FindByCharacter(characterId);
            if (!player) {
                return; // 玩家已离线：DB 事务已提交，重进恢复 Completed
            }
            if (!ok) {
                // 指令四十：失败整体回滚——任务仍 ReadyToTurnIn，奖励未发。
                LOG_ERROR("[Quest] TurnIn transaction failed char=" +
                          std::to_string(characterId) + " quest=" + std::to_string(questId) +
                          ": " + dbError);
                if (sendResponsePacket && requestId != 0) {
                    self->SendQuestTurnInResponse(player, requestId, questId, false,
                                                  QuestResultCode::InternalError);
                }
                return;
            }
            const std::uint64_t rewardItemInstanceId = itemInstanceId;
            // ---- 成功：更新内存并广播（指令四十） ----
            PlayerQuestState* state = player->Quests().MutableFind(questId);
            const QuestState oldState = state ? state->state : QuestState::ReadyToTurnIn;
            if (state) {
                state->state = QuestState::Completed;
                state->turnedInAt = tx.turnedInAt;
            }
            // 物品奖励入内存（instanceId 回填，同 Pickup 流程）。
            if (tx.rewardItemDefinitionId != 0 && tx.rewardItemSlotIndex >= 0 &&
                rewardItemInstanceId != 0) {
                InventoryEntry entry;
                entry.instanceId = rewardItemInstanceId;
                entry.definitionId = tx.rewardItemDefinitionId;
                entry.quantity = tx.rewardItemQuantity;
                player->Inventory().PutAt(static_cast<std::size_t>(tx.rewardItemSlotIndex), entry);
                self->SendInventoryDelta(player, 1, entry,
                                         static_cast<std::uint32_t>(tx.rewardItemSlotIndex));
                // 指令五十五：任务奖励物品影响其它 Collect 任务 -> 重算。
                self->HandleQuestInventoryChanged(player);
            }
            // EXP/Gold 奖励（指令三十六/三十七）：升级 -> 属性成长 + LevelUpEvent。
            player->SetProgression(tx.newExperience, tx.newGold);
            if (tx.newLevel != oldLevel) {
                player->ApplyLevelGrowth(tx.newLevel);
                self->RecalculateTargetDerivedStats(CombatEntityType::Player, characterId);
                self->SendEntityHealthSnapshot(player, CombatEntityType::Player, characterId,
                                               player->CurrentHp(), player->MaxHp(),
                                               player->Alive());
                std::vector<std::uint64_t> receivers = self->PlayerStatusReceivers(characterId);
                self->SendLevelUpEvent(receivers, player, oldLevel, tx.newLevel);
                // 指令五十七：任务奖励升级也必须推进其它 ReachLevel 任务。
                self->HandleQuestLevelChanged(player);
                LOG_INFO("[Quest] reward leveled player #" + std::to_string(characterId) + " " +
                         std::to_string(oldLevel) + " -> " + std::to_string(tx.newLevel));
            }
            // QuestRewardGranted（指令四十一：只发本人）。
            {
                QuestRewardGrantedPayload reward;
                reward.questId = questId;
                reward.exp = definition->reward.exp;
                reward.gold = definition->reward.gold;
                reward.itemDefinitionId = definition->reward.itemDefinitionId;
                reward.itemQuantity = definition->reward.itemQuantity;
                reward.newLevel = tx.newLevel;
                reward.newExperience = tx.newExperience;
                reward.newGold = tx.newGold;
                reward.serverTime = ServerTimeMs();
                Packet packet;
                packet.header.messageId = static_cast<std::uint16_t>(MessageId::QuestRewardGranted);
                if (EncodeQuestRewardGranted(reward, packet.payload)) {
                    self->SendPacketToPlayer(player, packet);
                }
            }
            self->SendQuestStateChanged(player, questId, oldState, QuestState::Completed);
            if (sendResponsePacket && requestId != 0) {
                self->SendQuestTurnInResponse(player, requestId, questId, true,
                                              QuestResultCode::Success);
            }
            self->SendProgressionSnapshot(player);
            // 阶段20 指令三十三：任务状态变化 → 重算相关 NPC per-player Marker。
            self->SendNpcQuestMarkersFor(player);
            LOG_INFO("[Quest] turned in quest=" + std::to_string(questId) + " char=" +
                     player->CharacterName() + " reward exp=" +
                     std::to_string(definition->reward.exp) + " gold=" +
                     std::to_string(definition->reward.gold));
        });
    };
    PersistQuestTurnIn(tx, applyResult);
    return QuestResultCode::Success;
}

void WorldServer::HandleQuestAbandonRequest(std::uint64_t connectionId,
                                            const legend::network::Packet& packet) {
    QuestAbandonRequestPayload request;
    std::string decodeError;
    if (!DecodeQuestAbandonRequest(packet.payload.data(), packet.payload.size(), request,
                                   decodeError)) {
        LOG_INFO("[Quest] Malformed QuestAbandonRequest from #" + std::to_string(connectionId));
        SendQuestAbandonResponse(nullptr, request.requestId, request.questId, false,
                                 QuestResultCode::MalformedRequest);
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        SendQuestAbandonResponse(nullptr, request.requestId, request.questId, false,
                                 QuestResultCode::NotInWorld);
        return;
    }
    if (player->IsRecentQuestRequest(request.requestId)) {
        SendQuestAbandonResponse(player, request.requestId, request.questId, false,
                                 QuestResultCode::DuplicateRequest);
        return;
    }
    // 指令四十三：只允许 InProgress / ReadyToTurnIn；Completed 不能 Abandon。
    const QuestResultCode code = QuestService::ValidateAbandon(
        player->Quests(), request.questId, QuestRegistry::Instance());
    if (code != QuestResultCode::Success) {
        SendQuestAbandonResponse(player, request.requestId, request.questId, false, code);
        return;
    }
    PlayerQuestState* state = player->Quests().MutableFind(request.questId);
    const QuestState oldState = state ? state->state : QuestState::InProgress;
    state->state = QuestState::Abandoned;
    state->ResetProgress(); // 指令四十三：进度清零（以后可再次接取）
    player->RememberQuestRequest(request.requestId);
    LOG_INFO("[Quest] abandoned quest=" + std::to_string(request.questId) + " char=" +
             player->CharacterName());
    // 指令十七/四十三：保留记录 state=Abandoned + 进度清零。
    PersistQuestAbandon(player->CharacterId(), request.questId, legend::account::UnixNow());
    SendQuestAbandonResponse(player, request.requestId, request.questId, true,
                             QuestResultCode::Success);
    SendQuestStateChanged(player, request.questId, oldState, QuestState::Abandoned);
    // 阶段20 指令三十三/七十六：Abandon 影响任务状态 → 重算相关 NPC per-player Marker。
    SendNpcQuestMarkersFor(player);
}

void WorldServer::SendQuestAcceptResponse(const std::shared_ptr<PlayerSession>& player,
                                          std::uint64_t requestId, QuestId questId, bool success,
                                          QuestResultCode code) {
    QuestAcceptResponsePayload out;
    out.requestId = requestId;
    out.questId = questId;
    out.success = success;
    out.resultCode = static_cast<std::uint8_t>(code);
    out.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::QuestAcceptResponse);
    if (EncodeQuestAcceptResponse(out, packet.payload) && player) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendQuestTurnInResponse(const std::shared_ptr<PlayerSession>& player,
                                          std::uint64_t requestId, QuestId questId, bool success,
                                          QuestResultCode code) {
    QuestTurnInResponsePayload out;
    out.requestId = requestId;
    out.questId = questId;
    out.success = success;
    out.resultCode = static_cast<std::uint8_t>(code);
    out.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::QuestTurnInResponse);
    if (EncodeQuestTurnInResponse(out, packet.payload) && player) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendQuestAbandonResponse(const std::shared_ptr<PlayerSession>& player,
                                           std::uint64_t requestId, QuestId questId, bool success,
                                           QuestResultCode code) {
    QuestAbandonResponsePayload out;
    out.requestId = requestId;
    out.questId = questId;
    out.success = success;
    out.resultCode = static_cast<std::uint8_t>(code);
    out.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::QuestAbandonResponse);
    if (EncodeQuestAbandonResponse(out, packet.payload) && player) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendQuestProgressUpdated(const std::shared_ptr<PlayerSession>& player,
                                           QuestId questId, std::uint32_t objectiveId,
                                           std::uint32_t current, std::uint32_t required,
                                           QuestState state) {
    // 指令三十/一百二十五：QuestProgressUpdated 只发本人。
    QuestProgressUpdatedPayload out;
    out.questId = questId;
    out.objectiveId = objectiveId;
    out.current = current;
    out.required = required;
    out.questState = static_cast<std::uint8_t>(state);
    out.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::QuestProgressUpdated);
    if (EncodeQuestProgressUpdated(out, packet.payload)) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendQuestStateChanged(const std::shared_ptr<PlayerSession>& player,
                                        QuestId questId, QuestState oldState, QuestState newState) {
    // 指令三十一/一百二十六：QuestStateChanged 只发本人（任务不是公开信息）。
    QuestStateChangedPayload out;
    out.questId = questId;
    out.oldState = static_cast<std::uint8_t>(oldState);
    out.newState = static_cast<std::uint8_t>(newState);
    out.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::QuestStateChanged);
    if (EncodeQuestStateChanged(out, packet.payload)) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendQuestSnapshot(const std::shared_ptr<PlayerSession>& player) {
    // 指令四十五/四十六：快照只发本人；Encode 侧 >256 Quest / >16 Objective 截断。
    QuestSnapshotPayload out;
    out.characterId = player->CharacterId();
    out.quests = player->Quests().Snapshot();
    out.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::QuestSnapshot);
    if (EncodeQuestSnapshot(out, packet.payload)) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::ScheduleQuestSnapshotTick() {
    if (m_stopped.load()) {
        return;
    }
    m_questSnapshotTimer.expires_after(
        std::chrono::milliseconds(m_config.questSnapshotIntervalMs));
    auto self = shared_from_this();
    m_questSnapshotTimer.async_wait([self](const std::error_code& ec) {
        if (ec || self->m_stopped.load()) {
            return;
        }
        self->SendQuestSnapshots();
        self->ScheduleQuestSnapshotTick();
    });
}

void WorldServer::SendQuestSnapshots() {
    // 指令四十七：每 10s 对每个在线玩家发本人 QuestSnapshot 纠偏。
    for (const auto& player : m_players.SnapshotPlayers()) {
        SendQuestSnapshot(player);
    }
}

void WorldServer::HandleQuestObjectiveChanges(
    const std::shared_ptr<PlayerSession>& player,
    const std::vector<QuestService::ObjectiveChange>& changes,
    const std::vector<QuestService::StateChange>& stateChanges) {
    // 编排（指令七十一/七十二）：有实际变化才写 DB（指令七十三）+ 只发本人事件。
    if (changes.empty() && stateChanges.empty()) {
        return;
    }
    const std::uint64_t characterId = player->CharacterId();
    for (const auto& change : changes) {
        const PlayerQuestState* state = player->Quests().Find(change.questId);
        const QuestState questState = state ? state->state : QuestState::InProgress;
        const QuestDefinition* definition =
            QuestRegistry::Instance().FindQuest(change.questId);
        std::uint32_t required = change.newProgress;
        if (definition != nullptr) {
            for (const auto& objective : definition->objectives) {
                if (objective.objectiveId == change.objectiveId) {
                    required = objective.requiredCount;
                    break;
                }
            }
        }
        SendQuestProgressUpdated(player, change.questId, change.objectiveId, change.newProgress,
                                 required, questState);
        PersistQuestObjective(characterId, change.questId, change.objectiveId,
                              change.newProgress);
    }
    for (const auto& change : stateChanges) {
        // 指令二十九：InProgress -> ReadyToTurnIn（不自动领奖，指令七十八）。
        SendQuestStateChanged(player, change.questId, change.oldState, change.newState);
        PersistQuestState(characterId, change.questId,
                          static_cast<std::uint8_t>(change.newState),
                          legend::account::UnixNow(), false);
    }
    // 阶段20 指令三十三：任务状态变化（Kill/Collect/Reach 推进 -> ReadyToTurnIn 等）
    // 同样必须重算相关 NPC per-player Marker。
    if (!stateChanges.empty()) {
        SendNpcQuestMarkersFor(player);
    }
}

void WorldServer::HandleQuestMonsterKilled(std::uint64_t killerCharacterId,
                                           std::uint32_t monsterTypeId) {
    // 指令二十五：Kill 目标只在玩家是最终 killer 时增加（别人杀不加）；
    // AOE 多杀每只分别调用（每只 +1）；DOT kill 归 Status source player。
    const auto& registry = QuestRegistry::Instance();
    auto killer = m_players.FindByCharacter(killerCharacterId);
    if (killer) {
        // 在线 killer：容器推进（io 线程）-> DB + 事件。
        auto changes = QuestService::OnMonsterKilled(registry, killer->Quests(), monsterTypeId);
        if (changes.empty()) {
            return;
        }
        auto stateChanges = QuestService::EvaluateQuestCompletion(
            registry, killer->Quests(), legend::account::UnixNow());
        HandleQuestObjectiveChanges(killer, changes, stateChanges);
        return;
    }
    // 指令六十二：离线 killer —— DB 直接推进（不加载假 PlayerSession）。
    std::vector<QuestRepository::KillCandidate> candidates;
    for (const auto& definition : registry.AllQuests()) {
        for (const auto& objective : definition.objectives) {
            if (objective.type == QuestObjectiveType::KillMonster &&
                objective.targetId == monsterTypeId) {
                QuestRepository::KillCandidate candidate;
                candidate.questId = definition.questId;
                candidate.objectiveId = objective.objectiveId;
                candidate.requiredCount = objective.requiredCount;
                candidates.push_back(candidate);
            }
        }
    }
    if (candidates.empty()) {
        return;
    }
    PersistOfflineKill(killerCharacterId, candidates);
}

void WorldServer::HandleQuestInventoryChanged(const std::shared_ptr<PlayerSession>& player) {
    // 指令五十四/五十五：Inventory 变化后重算 Collect 任务（"当前拥有数量"型）。
    const auto& registry = QuestRegistry::Instance();
    auto changes =
        QuestService::OnInventoryChanged(registry, player->Quests(), OwnedCountFnFor(player));
    if (changes.empty()) {
        return;
    }
    auto stateChanges = QuestService::EvaluateQuestCompletion(
        registry, player->Quests(), legend::account::UnixNow());
    HandleQuestObjectiveChanges(player, changes, stateChanges);
}

void WorldServer::HandleQuestLevelChanged(const std::shared_ptr<PlayerSession>& player) {
    // 指令五十六/五十七：LevelUp（杀怪奖励或任务奖励）推进 ReachLevel 任务。
    const auto& registry = QuestRegistry::Instance();
    auto changes = QuestService::OnPlayerLevelChanged(registry, player->Quests(), player->Level());
    if (changes.empty()) {
        return;
    }
    auto stateChanges = QuestService::EvaluateQuestCompletion(
        registry, player->Quests(), legend::account::UnixNow());
    HandleQuestObjectiveChanges(player, changes, stateChanges);
}

void WorldServer::HandleQuestPlayerMoved(const std::shared_ptr<PlayerSession>& player) {
    // 指令二十七：服务器权威 MoveInput 位置变化后检查 ReachArea（一次性，指令五十八）。
    const auto& registry = QuestRegistry::Instance();
    auto changes = QuestService::OnPlayerMoved(registry, player->Quests(), player->MapId(),
                                               player->PositionX(), player->PositionY());
    if (changes.empty()) {
        return;
    }
    auto stateChanges = QuestService::EvaluateQuestCompletion(
        registry, player->Quests(), legend::account::UnixNow());
    HandleQuestObjectiveChanges(player, changes, stateChanges);
}

void WorldServer::ApplyLoadedQuests(
    const std::shared_ptr<PlayerSession>& player,
    const std::vector<QuestRepository::QuestRow>& questRows,
    const std::vector<QuestRepository::ObjectiveRow>& objectiveRows) {
    // 指令六十八：EnterWorld 从 SQLite 加载任务状态（不能只靠进程内存）；
    // 未知 questId（定义已删除）防御性跳过。
    player->Quests().Clear();
    const auto& registry = QuestRegistry::Instance();
    for (const auto& row : questRows) {
        if (registry.FindQuest(row.questId) == nullptr) {
            continue;
        }
        PlayerQuestState& state = player->Quests().Add(row.questId);
        state.questId = row.questId;
        state.state = static_cast<QuestState>(row.state);
        state.acceptedAt = row.acceptedAt;
        state.completedAt = row.completedAt;
        state.turnedInAt = row.turnedInAt;
        const QuestDefinition* definition = registry.FindQuest(row.questId);
        if (definition != nullptr) {
            state.InitObjectives(*definition);
        }
    }
    for (const auto& row : objectiveRows) {
        PlayerQuestState* state = player->Quests().MutableFind(row.questId);
        if (state != nullptr) {
            state->SetObjectiveProgress(row.objectiveId, row.progress);
        }
    }
    // 加载后初始校验（离线升级奖励等边界：ReachLevel/Collect 重算；进度变化写 DB）。
    auto levelChanges =
        QuestService::OnPlayerLevelChanged(registry, player->Quests(), player->Level());
    auto collectChanges =
        QuestService::OnInventoryChanged(registry, player->Quests(), OwnedCountFnFor(player));
    auto changes = levelChanges;
    changes.insert(changes.end(), collectChanges.begin(), collectChanges.end());
    auto stateChanges =
        QuestService::EvaluateQuestCompletion(registry, player->Quests(), legend::account::UnixNow());
    if (!changes.empty() || !stateChanges.empty()) {
        HandleQuestObjectiveChanges(player, changes, stateChanges);
    }
    LOG_INFO("[Quest] loaded char #" + std::to_string(player->CharacterId()) + " quests=" +
             std::to_string(questRows.size()));
}

// ---------------------------------------------------------------------------
// 阶段19：测试布景辅助（io 线程投递，与游戏逻辑串行；仅供测试白盒使用）
// ---------------------------------------------------------------------------

bool WorldServer::TestSeedQuestProgress(
    std::uint64_t characterId, QuestId questId,
    const std::unordered_map<std::uint32_t, std::uint32_t>& progress, QuestState state) {
    if (m_stopped.load()) {
        return false;
    }
    auto self = shared_from_this();
    m_service.Post([self, characterId, questId, progress, state]() {
        auto player = self->m_players.FindByCharacter(characterId);
        if (!player) {
            return;
        }
        const QuestDefinition* definition =
            QuestRegistry::Instance().FindQuest(questId);
        PlayerQuestState& questState = player->Quests().Add(questId);
        questState.questId = questId;
        questState.state = state;
        questState.acceptedAt = legend::account::UnixNow();
        questState.ResetProgress();
        if (definition != nullptr) {
            questState.InitObjectives(*definition);
        }
        for (const auto& [objectiveId, value] : progress) {
            questState.SetObjectiveProgress(objectiveId, value);
        }
        LOG_INFO("[Quest] test-seeded quest=" + std::to_string(questId) + " char #" +
                 std::to_string(characterId));
    });
    return true;
}

bool WorldServer::TestAcceptQuest(std::uint64_t characterId, QuestId questId) {
    if (m_stopped.load()) {
        return false;
    }
    auto self = shared_from_this();
    m_service.Post([self, characterId, questId]() {
        auto player = self->m_players.FindByCharacter(characterId);
        if (!player) {
            return;
        }
        const QuestDefinition* definition =
            QuestRegistry::Instance().FindQuest(questId);
        if (definition == nullptr) {
            return;
        }
        auto changes = QuestService::AcceptQuest(QuestRegistry::Instance(), player->Quests(),
                                                 questId, player->Level(),
                                                 OwnedCountFnFor(player),
                                                 legend::account::UnixNow());
        auto stateChanges = QuestService::EvaluateQuestCompletion(
            QuestRegistry::Instance(), player->Quests(), legend::account::UnixNow());
        self->HandleQuestObjectiveChanges(player, changes, stateChanges);
        LOG_INFO("[Quest] test-accepted quest=" + std::to_string(questId) + " char #" +
                 std::to_string(characterId));
    });
    return true;
}

// ---------------------------------------------------------------------------
// 阶段20：NPC / Dialogue / Shop / Teleport 编排（Quest Interaction Core V0.20）。
// 100% 服务器权威（指令二）：Client 只表达意图，所有结果由 WorldServer 重新验证。
// ---------------------------------------------------------------------------

void WorldServer::SpawnInitialNpcs() {
    const std::size_t spawned = m_npcs.SpawnFromRegistry();
    m_npcs.AddToGrid(m_npcGrid);
    LOG_INFO("[Npc] spawned " + std::to_string(spawned) + " NPCs");
}

void WorldServer::UpdatePlayerNpcVisibility(const std::shared_ptr<PlayerSession>& player,
                                            bool initialVisibility) {
    // 指令十一/十四：NPC 只通过 AOI 同步（Enter 600 / Leave 700 滞回；不全图广播）。
    const auto candidates = m_npcGrid.QueryRange(player->PositionX(), player->PositionY(),
                                                 m_config.aoiLeaveRadius);
    const float enterRadiusSq = m_config.aoiEnterRadius * m_config.aoiEnterRadius;
    const float leaveRadiusSq = m_config.aoiLeaveRadius * m_config.aoiLeaveRadius;
    // Enter：范围候选中未可见且 <= EnterRadius 的 NPC。
    for (const std::uint64_t npcEntityId : candidates) {
        if (player->VisibleNpcs().count(npcEntityId) != 0) {
            continue;
        }
        const NpcEntity* npc = m_npcs.Find(npcEntityId);
        if (npc == nullptr || !npc->Active() || npc->MapId() != player->MapId()) {
            continue;
        }
        const float dx = npc->X() - player->PositionX();
        const float dy = npc->Y() - player->PositionY();
        if (dx * dx + dy * dy > enterRadiusSq) {
            continue;
        }
        SendNpcSpawn(player, *npc);
        player->AddVisibleNpc(npcEntityId);
        // 指令三十三：Marker 是 per-player——Spawn 后立即下发该玩家的 Marker。
        if (npc->Definition() != nullptr) {
            SendNpcQuestMarkerUpdate(player, *npc,
                                     NpcInteractionService::ComputeQuestMarker(
                                         *player, *npc->Definition()));
        }
    }
    // Leave：已可见但超出 LeaveRadius 的 NPC（初始可见性阶段无 leave）。
    if (!initialVisibility) {
        std::vector<std::uint64_t> leaves;
        for (const std::uint64_t npcEntityId : player->VisibleNpcs()) {
            const NpcEntity* npc = m_npcs.Find(npcEntityId);
            if (npc == nullptr || !npc->Active() || npc->MapId() != player->MapId()) {
                leaves.push_back(npcEntityId);
                continue;
            }
            const float dx = npc->X() - player->PositionX();
            const float dy = npc->Y() - player->PositionY();
            if (dx * dx + dy * dy > leaveRadiusSq) {
                leaves.push_back(npcEntityId);
            }
        }
        for (const std::uint64_t npcEntityId : leaves) {
            SendNpcDespawn(player, npcEntityId, NpcDespawnReason::LeftAOI);
            player->EraseVisibleNpc(npcEntityId);
        }
    }
}

void WorldServer::SendNpcSpawn(const std::shared_ptr<PlayerSession>& receiver,
                               const NpcEntity& npc) {
    const NpcDefinition* definition = npc.Definition();
    if (definition == nullptr) {
        return;
    }
    std::vector<std::uint8_t> payload;
    if (EncodeNpcSpawn(payload, npc.EntityId(), definition->npcDefinitionId, definition->name,
                       npc.MapId(), npc.X(), npc.Y(), static_cast<std::uint8_t>(definition->npcType),
                       definition->visualId)) {
        Packet packet;
        packet.header.messageId = static_cast<std::uint16_t>(MessageId::NpcSpawn);
        packet.payload = std::move(payload);
        SendPacketToPlayer(receiver, packet);
    }
}

void WorldServer::SendNpcDespawn(const std::shared_ptr<PlayerSession>& receiver,
                                 std::uint64_t npcEntityId, NpcDespawnReason reason) {
    std::vector<std::uint8_t> payload;
    if (EncodeNpcDespawn(payload, npcEntityId, static_cast<std::uint8_t>(reason))) {
        Packet packet;
        packet.header.messageId = static_cast<std::uint16_t>(MessageId::NpcDespawn);
        packet.payload = std::move(payload);
        SendPacketToPlayer(receiver, packet);
    }
}

void WorldServer::SendNpcQuestMarkerUpdate(const std::shared_ptr<PlayerSession>& player,
                                           const NpcEntity& npc, NpcQuestMarker marker) {
    // 指令三十三/一百二十五类比：Marker 是 per-player（任务状态属于每个玩家），只发本人。
    NpcQuestMarkerUpdatePayload out;
    out.npcEntityId = npc.EntityId();
    out.npcDefinitionId = npc.DefinitionId();
    out.marker = static_cast<std::uint8_t>(marker);
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::NpcQuestMarkerUpdate);
    if (EncodeNpcQuestMarkerUpdate(out, packet.payload)) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendNpcQuestMarkersFor(const std::shared_ptr<PlayerSession>& player) {
    // 指令七十六：Quest 状态变化（Accept/Progress/Ready/TurnIn/Abandon/Level/
    // Inventory）后重算该玩家可见 NPC 的 Marker（仅相关 NPC 需要重算——4 个 NPC
    // 全量重算代价可忽略）。
    for (const std::uint64_t npcEntityId : player->VisibleNpcs()) {
        const NpcEntity* npc = m_npcs.Find(npcEntityId);
        if (npc == nullptr || npc->Definition() == nullptr) {
            continue;
        }
        SendNpcQuestMarkerUpdate(player, *npc,
                                 NpcInteractionService::ComputeQuestMarker(*player,
                                                                           *npc->Definition()));
    }
}

void WorldServer::SendDialogueToPlayer(const std::shared_ptr<PlayerSession>& player,
                                       const DialoguePayload& payload) {
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::DialoguePayload);
    if (EncodeDialoguePayload(payload, packet.payload)) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::CloseNpcSessions(const std::shared_ptr<PlayerSession>& player) {
    player->ClearDialogueSession();
    player->ClearShopSession();
}

void WorldServer::HandleNpcInteractRequest(std::uint64_t connectionId,
                                           const legend::network::Packet& packet) {
    // 指令八十八：Malformed 回 MalformedRequest（requestId 尽力回显），不断开。
    NpcInteractRequestPayload request;
    std::string decodeError;
    if (!DecodeNpcInteractRequest(packet.payload.data(), packet.payload.size(), request,
                                  decodeError)) {
        LOG_INFO("[Npc] Malformed NpcInteractRequest from #" + std::to_string(connectionId));
        SendNpcInteractResponse(nullptr, request.requestId, false, NpcResultCode::MalformedRequest,
                                request.npcEntityId, 0, 0);
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        SendNpcInteractResponse(nullptr, request.requestId, false, NpcResultCode::NotInWorld,
                                request.npcEntityId, 0, 0);
        return;
    }
    // 指令五十七：防重放。
    if (player->IsRecentNpcRequest(request.requestId)) {
        SendNpcInteractResponse(player, request.requestId, false,
                                NpcResultCode::DuplicateRequest, request.npcEntityId, 0, 0);
        return;
    }
    // 指令七十四：死亡玩家不能 NPC 交互。
    if (!player->Alive()) {
        SendNpcInteractResponse(player, request.requestId, false, NpcResultCode::Dead,
                                request.npcEntityId, 0, 0);
        return;
    }
    const NpcEntity* npc = m_npcs.Find(request.npcEntityId);
    if (npc == nullptr) {
        SendNpcInteractResponse(player, request.requestId, false, NpcResultCode::NpcNotFound,
                                request.npcEntityId, 0, 0);
        return;
    }
    // 指令十九：服务器验证链（NPC active/same map/visibleNpcs/权威距离）。
    const NpcResultCode code = NpcInteractionService::ValidateInteraction(*player, *npc, std::chrono::steady_clock::now());
    if (code != NpcResultCode::Success) {
        SendNpcInteractResponse(player, request.requestId, false, code, request.npcEntityId, 0, 0);
        return;
    }
    // 指令二十一：创建 Dialogue Session（服务器单调 sessionId；PlayerSession 保存）。
    ActiveDialogueSession session;
    session.sessionId = m_nextDialogueSessionId++;
    session.npcEntityId = npc->EntityId();
    session.openedAt = std::chrono::steady_clock::now();
    player->SetDialogueSession(session);
    player->RememberNpcRequest(request.requestId); // 指令五十七：只缓存成功请求
    LOG_INFO("[Npc] interact char=" + player->CharacterName() + " npc=" +
             std::to_string(request.npcEntityId) + " session=" +
             std::to_string(session.sessionId));
    SendNpcInteractResponse(player, request.requestId, true, NpcResultCode::Success,
                            request.npcEntityId, session.sessionId,
                            npc->Definition() ? npc->Definition()->dialogueId : 0);
    // 指令二十五：返回 Dialogue（一层菜单，动态生成）。
    if (npc->Definition() != nullptr) {
        SendDialogueToPlayer(player, NpcInteractionService::BuildDialogue(*player, *npc,
                                                                          session.sessionId));
    }
}

void WorldServer::HandleDialogueOptionRequest(std::uint64_t connectionId,
                                              const legend::network::Packet& packet) {
    DialogueOptionRequestPayload request;
    std::string decodeError;
    if (!DecodeDialogueOptionRequest(packet.payload.data(), packet.payload.size(), request,
                                     decodeError)) {
        LOG_INFO("[Npc] Malformed DialogueOptionRequest from #" + std::to_string(connectionId));
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        return;
    }
    // 指令五十七：防重放。
    if (player->IsRecentNpcRequest(request.requestId)) {
        return;
    }
    const NpcEntity* npc = m_npcs.Find(player->DialogueSession().npcEntityId);
    if (npc == nullptr) {
        // 会话指向的 NPC 不存在 → 会话失效（指令二十二）。
        CloseNpcSessions(player);
        SendDialogueToPlayer(player, DialoguePayload{});
        return;
    }
    // 指令二十二/七十四：会话有效性（含 TTL/走远/死亡）。
    if (!player->Alive()) {
        CloseNpcSessions(player);
        SendDialogueToPlayer(player, DialoguePayload{});
        return;
    }
    const NpcResultCode sessionCode = NpcInteractionService::ValidateDialogueSession(
        *player, request.dialogueSessionId, *npc, m_config.npcSessionTtlSeconds,
        std::chrono::steady_clock::now());
    if (sessionCode != NpcResultCode::Success) {
        CloseNpcSessions(player);
        SendDialogueToPlayer(player, DialoguePayload{});
        return;
    }
    // 指令二十五：Option 必须是服务器生成的菜单项（重新生成 + optionId 匹配）。
    DialoguePayload menu =
        NpcInteractionService::BuildDialogue(*player, *npc, request.dialogueSessionId);
    const DialogueOptionData* chosen = nullptr;
    for (const auto& option : menu.options) {
        if (option.optionId == request.optionId) {
            chosen = &option;
            break;
        }
    }
    if (chosen == nullptr) {
        SendDialogueToPlayer(player, DialoguePayload{});
        return;
    }
    player->RememberNpcRequest(request.requestId);
    player->TouchDialogueSession(); // 刷新会话活跃时间（TTL 从最近交互起算）
    const auto optionType = static_cast<DialogueOptionType>(chosen->type);
    LOG_INFO("[Npc] option char=" + player->CharacterName() + " npc=" +
             std::to_string(npc->EntityId()) + " optionId=" + std::to_string(chosen->optionId) +
             " type=" + DialogueOptionTypeName(chosen->type) + " ref=" +
             std::to_string(chosen->referenceId));
    if (optionType == DialogueOptionType::Close) {
        // Close：关闭会话（Dialogue/Shop Session 全失效）。
        CloseNpcSessions(player);
        SendDialogueToPlayer(player, DialoguePayload{});
        return;
    }
    if (optionType == DialogueOptionType::Quest) {
        // 指令二十九/三十：复用阶段19 QuestService（不写第二套 Quest 逻辑）。
        const QuestId questId = chosen->referenceId;
        const PlayerQuestState* state = player->Quests().Find(questId);
        const bool ready = state != nullptr && state->state == QuestState::ReadyToTurnIn;
        QuestResultCode code = QuestResultCode::NotAccepted;
        if (ready) {
            code = BeginQuestTurnIn(player, questId, 0, false);
        } else {
            code = AcceptQuestForPlayer(player, questId);
        }
        if (code == QuestResultCode::Success) {
            // FIFO：TurnIn 的 DB 回调先于本刷新执行（菜单反映最新状态）。
            auto self = shared_from_this();
            const std::uint64_t npcEntityId = npc->EntityId();
            const std::uint64_t sessionId = request.dialogueSessionId;
            m_service.Post([self, characterId = player->CharacterId(), npcEntityId, sessionId]() {
                if (self->m_stopped.load()) {
                    return;
                }
                auto player = self->m_players.FindByCharacter(characterId);
                const NpcEntity* npc = self->m_npcs.Find(npcEntityId);
                if (!player || npc == nullptr ||
                    player->DialogueSession().sessionId != sessionId) {
                    return;
                }
                self->SendDialogueToPlayer(player, NpcInteractionService::BuildDialogue(
                                                        *player, *npc, sessionId));
            });
        } else {
            // 失败：关闭菜单（选项语义不再可用）。
            CloseNpcSessions(player);
            SendDialogueToPlayer(player, DialoguePayload{});
        }
        return;
    }
    if (optionType == DialogueOptionType::Shop) {
        // 指令三十九：Open Shop 必须经有效 Dialogue Session（此处会话已验证）。
        const NpcDefinition* definition = npc->Definition();
        const ShopDefinition* shop =
            definition ? ShopRegistry::Instance().FindShop(definition->shopId) : nullptr;
        if (shop == nullptr) {
            SendDialogueToPlayer(player, DialoguePayload{});
            return;
        }
        ActiveShopSession shopSession;
        shopSession.sessionId = m_nextShopSessionId++;
        shopSession.npcEntityId = npc->EntityId();
        shopSession.shopId = shop->shopId;
        shopSession.openedAt = std::chrono::steady_clock::now();
        player->SetShopSession(shopSession);
        SendShopOpenResponse(player, request.requestId, true, ShopResultCode::Success,
                             shopSession.sessionId, shop->shopId, npc->EntityId(), shop);
        return;
    }
    if (optionType == DialogueOptionType::Teleport) {
        (void)TeleportPlayerViaNpc(player, request.requestId, request.dialogueSessionId,
                                   chosen->referenceId);
        return;
    }
}

void WorldServer::HandleShopOpenRequest(std::uint64_t connectionId,
                                        const legend::network::Packet& packet) {
    // 指令三十九：Shop 必须经 Dialogue Session 打开——独立 ShopOpenRequest 同样
    // 需要有效会话（不允许凭空发 shopId 打开远程商店）。
    ShopOpenRequestPayload request;
    std::string decodeError;
    if (!DecodeShopOpenRequest(packet.payload.data(), packet.payload.size(), request,
                               decodeError)) {
        LOG_INFO("[Npc] Malformed ShopOpenRequest from #" + std::to_string(connectionId));
        SendShopOpenResponse(nullptr, request.requestId, false, ShopResultCode::MalformedRequest,
                             0, 0, 0, nullptr);
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        SendShopOpenResponse(nullptr, request.requestId, false, ShopResultCode::NotInWorld, 0, 0,
                             0, nullptr);
        return;
    }
    if (player->IsRecentNpcRequest(request.requestId)) {
        SendShopOpenResponse(player, request.requestId, false, ShopResultCode::DuplicateRequest, 0,
                             0, 0, nullptr);
        return;
    }
    const NpcEntity* npc = m_npcs.Find(player->DialogueSession().npcEntityId);
    if (npc == nullptr || !player->Alive() ||
        NpcInteractionService::ValidateDialogueSession(
            *player, request.dialogueSessionId, *npc, m_config.npcSessionTtlSeconds,
            std::chrono::steady_clock::now()) != NpcResultCode::Success) {
        CloseNpcSessions(player);
        SendShopOpenResponse(player, request.requestId, false, ShopResultCode::SessionNotFound, 0,
                             0, 0, nullptr);
        return;
    }
    const NpcDefinition* definition = npc->Definition();
    const ShopDefinition* shop =
        definition ? ShopRegistry::Instance().FindShop(definition->shopId) : nullptr;
    if (shop == nullptr) {
        SendShopOpenResponse(player, request.requestId, false, ShopResultCode::ShopNotFound, 0, 0,
                             0, nullptr);
        return;
    }
    player->RememberNpcRequest(request.requestId);
    ActiveShopSession shopSession;
    shopSession.sessionId = m_nextShopSessionId++;
    shopSession.npcEntityId = npc->EntityId();
    shopSession.shopId = shop->shopId;
    shopSession.openedAt = std::chrono::steady_clock::now();
    player->SetShopSession(shopSession);
    SendShopOpenResponse(player, request.requestId, true, ShopResultCode::Success,
                         shopSession.sessionId, shop->shopId, npc->EntityId(), shop);
}

void WorldServer::HandleShopBuyRequest(std::uint64_t connectionId,
                                       const legend::network::Packet& packet) {
    ShopBuyRequestPayload request;
    std::string decodeError;
    if (!DecodeShopBuyRequest(packet.payload.data(), packet.payload.size(), request, decodeError)) {
        LOG_INFO("[Npc] Malformed ShopBuyRequest from #" + std::to_string(connectionId));
        SendShopBuyResponse(nullptr, request.requestId, false, ShopResultCode::MalformedRequest, 0,
                            0, 0);
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        SendShopBuyResponse(nullptr, request.requestId, false, ShopResultCode::NotInWorld, 0, 0, 0);
        return;
    }
    if (player->IsRecentNpcRequest(request.requestId)) {
        SendShopBuyResponse(player, request.requestId, false, ShopResultCode::DuplicateRequest, 0,
                            0, 0);
        return;
    }
    // 指令四十一：Shop Session 有效性（TTL/存活/同图/距离）。
    const NpcEntity* npc = m_npcs.Find(player->ShopSession().npcEntityId);
    if (npc == nullptr || !player->Alive()) {
        CloseNpcSessions(player);
        SendShopBuyResponse(player, request.requestId, false, ShopResultCode::SessionNotFound, 0,
                            0, 0);
        return;
    }
    const auto& shopSession = player->ShopSession();
    if (shopSession.sessionId == 0 || shopSession.sessionId != request.shopSessionId) {
        SendShopBuyResponse(player, request.requestId, false, ShopResultCode::SessionNotFound, 0,
                            0, 0);
        return;
    }
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - shopSession.openedAt)
            .count();
    if (elapsed > m_config.npcSessionTtlSeconds) {
        CloseNpcSessions(player);
        SendShopBuyResponse(player, request.requestId, false, ShopResultCode::SessionExpired, 0, 0,
                            0);
        return;
    }
    // 指令六十四/四十四：离开 NPC 交互范围失效。
    {
        const float dx = npc->X() - player->PositionX();
        const float dy = npc->Y() - player->PositionY();
        const NpcDefinition* definition = npc->Definition();
        const float range =
            definition ? definition->interactionRange : kNpcDefaultInteractionRange;
        if (npc->MapId() != player->MapId() || dx * dx + dy * dy > range * range) {
            CloseNpcSessions(player);
            SendShopBuyResponse(player, request.requestId, false, ShopResultCode::TooFar, 0, 0, 0);
            return;
        }
    }
    // 指令三十八：价格 100% 服务器权威（ShopRegistry 取价；Client 只传 id + quantity）。
    const ShopDefinition* shop = ShopRegistry::Instance().FindShop(shopSession.shopId);
    const ShopEntry* entry = shop ? shop->FindEntry(request.itemDefinitionId) : nullptr;
    if (shop == nullptr || entry == nullptr) {
        SendShopBuyResponse(player, request.requestId, false, ShopResultCode::ItemNotInShop, 0, 0,
                            0);
        return;
    }
    if (!entry->canBuy) {
        SendShopBuyResponse(player, request.requestId, false, ShopResultCode::CannotBuy, 0, 0, 0);
        return;
    }
    // 指令四十三：quantity 1~99；装备 quantity 必须 1。
    const ItemDefinition* itemDef = m_itemRegistry.Find(request.itemDefinitionId);
    std::uint32_t quantity = request.quantity;
    if (quantity < kBuyQuantityMin || quantity > kBuyQuantityMax) {
        SendShopBuyResponse(player, request.requestId, false, ShopResultCode::InvalidQuantity, 0,
                            0, 0);
        return;
    }
    if (itemDef != nullptr && itemDef->type != ItemType::Material && quantity != 1) {
        SendShopBuyResponse(player, request.requestId, false, ShopResultCode::InvalidQuantity, 0,
                            0, 0);
        return;
    }
    // 指令四十五：uint64 中间计算防溢出。
    const std::uint64_t totalCost =
        static_cast<std::uint64_t>(entry->buyPrice) * static_cast<std::uint64_t>(quantity);
    if (totalCost > static_cast<std::uint64_t>(player->Gold())) {
        SendShopBuyResponse(player, request.requestId, false, ShopResultCode::NotEnoughGold, 0, 0,
                            0);
        return;
    }
    // 指令四十四/四十六：背包空间预检（试算；Material 并入堆叠优先）。
    InventoryContainer trial = player->Inventory();
    const auto addResult = trial.Add(m_itemRegistry, request.itemDefinitionId, quantity, 0, 0);
    if (addResult.code == InventoryAddCode::Full) {
        SendShopBuyResponse(player, request.requestId, false, ShopResultCode::InventoryFull, 0, 0,
                            0);
        return;
    }
    // 指令四十六/五十七：预检通过 → 立即缓存 requestId（DB 提交前）→ 原子事务
    //（Gold 扣除 + Inventory 写入一次提交）→ 成功后更新内存并广播。
    player->RememberNpcRequest(request.requestId);
    const std::int64_t newGold = player->Gold() - static_cast<std::int64_t>(totalCost);
    auto self = shared_from_this();
    const std::uint64_t characterId = player->CharacterId();
    const bool mergedIntoStack = addResult.code == InventoryAddCode::Merged;
    const std::uint32_t bagSlotIndex = addResult.slotIndex;
    std::uint64_t mergeInstanceId = 0;
    std::uint32_t mergeQuantity = 0;
    if (mergedIntoStack) {
        const InventoryEntry* current = trial.At(bagSlotIndex);
        if (current) {
            mergeInstanceId = current->instanceId;
            mergeQuantity = current->quantity;
        }
    }
    const auto applyResult = [self, characterId, requestId = request.requestId, newGold,
                              itemDefinitionId = request.itemDefinitionId, quantity,
                              bagSlotIndex, mergedIntoStack,
                              totalCost](bool ok, std::uint64_t instanceId) {
        self->m_service.Post([self, characterId, requestId, newGold, itemDefinitionId, quantity,
                              bagSlotIndex, mergedIntoStack, ok, instanceId, totalCost]() {
            if (self->m_stopped.load()) {
                return;
            }
            auto player = self->m_players.FindByCharacter(characterId);
            if (!ok) {
                // 指令四十六：失败整体回滚（DB ROLLBACK），内存未变。
                if (player) {
                    self->SendShopBuyResponse(player, requestId, false,
                                              ShopResultCode::InternalError, 0, 0, 0);
                }
                return;
            }
            if (!player) {
                return; // 玩家已离线：DB 事务已提交，重进加载
            }
            // 成功后更新内存（指令四十六）。
            player->SetProgression(player->Experience(), newGold);
            if (mergedIntoStack) {
                InventoryEntry* entry = player->Inventory().MutableAt(bagSlotIndex);
                if (entry) {
                    entry->quantity += quantity;
                }
            } else {
                InventoryEntry entry;
                entry.instanceId = instanceId;
                entry.definitionId = itemDefinitionId;
                entry.quantity = quantity;
                player->Inventory().PutAt(bagSlotIndex, entry);
            }
            // 指令四十八：InventoryDelta + BuyResponse(newGold)。
            const InventoryEntry* current = player->Inventory().At(bagSlotIndex);
            if (current) {
                self->SendInventoryDelta(player, 1, *current, bagSlotIndex);
            }
            self->SendShopBuyResponse(player, requestId, true, ShopResultCode::Success,
                                      itemDefinitionId, quantity,
                                      static_cast<std::uint32_t>(totalCost));
            // 指令八十五：购买 Slime Core 触发 Collect 任务重算（持有数量型）。
            self->HandleQuestInventoryChanged(player);
            self->SendProgressionSnapshot(player);
            self->SendNpcQuestMarkersFor(player);
        });
    };
    InventoryRepository::ShopBuyTransaction tx;
    tx.characterId = characterId;
    tx.newGold = newGold;
    tx.itemDefinitionId = request.itemDefinitionId;
    tx.quantity = quantity;
    tx.bagSlotIndex = static_cast<std::int64_t>(bagSlotIndex);
    tx.mergedIntoStack = mergedIntoStack;
    tx.mergeInstanceId = mergeInstanceId;
    tx.mergeQuantity = mergeQuantity;
    if (m_persistence) {
        // 阶段25.5：RPC 模式——购买原子事务在 DbServer 内执行。
        legend::internal::WorldShopBuy command;
        command.characterId = tx.characterId;
        command.newGold = tx.newGold;
        command.itemDefinitionId = tx.itemDefinitionId;
        command.quantity = tx.quantity;
        command.bagSlotIndex = tx.bagSlotIndex;
        command.mergedIntoStack = tx.mergedIntoStack;
        command.mergeInstanceId = tx.mergeInstanceId;
        command.mergeQuantity = tx.mergeQuantity;
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldShopBuy(command, payload)) {
            applyResult(false, 0);
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::ShopBuyWrite, std::move(payload),
            [applyResult](const legend::internal::DbResponse& response) {
                if (response.errorCode != legend::internal::InternalErrorCode::Ok) {
                    applyResult(false, 0);
                    return;
                }
                legend::internal::WorldShopBuyResult result;
                std::string error;
                if (!legend::internal::DecodeWorldShopBuyResult(
                        response.payload.data(), response.payload.size(), result, error)) {
                    applyResult(false, 0);
                    return;
                }
                applyResult(true, result.newInstanceId);
            });
        return;
    }
    m_dbWorker.Post([self, applyResult, tx]() {
        // DB 线程：BEGIN → Gold 扣除 → Inventory 写入 → COMMIT（指令四十六原子性）。
        std::uint64_t newInstanceId = 0;
        std::string error;
        const bool ok =
            InventoryRepository::RunShopBuyTransaction(self->m_database, tx, newInstanceId, error);
        applyResult(ok, newInstanceId);
    });
}

void WorldServer::HandleShopSellRequest(std::uint64_t connectionId,
                                        const legend::network::Packet& packet) {
    ShopSellRequestPayload request;
    std::string decodeError;
    if (!DecodeShopSellRequest(packet.payload.data(), packet.payload.size(), request,
                               decodeError)) {
        LOG_INFO("[Npc] Malformed ShopSellRequest from #" + std::to_string(connectionId));
        SendShopSellResponse(nullptr, request.requestId, false, ShopResultCode::MalformedRequest,
                             0, 0, 0);
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        SendShopSellResponse(nullptr, request.requestId, false, ShopResultCode::NotInWorld, 0, 0,
                             0);
        return;
    }
    if (player->IsRecentNpcRequest(request.requestId)) {
        SendShopSellResponse(player, request.requestId, false, ShopResultCode::DuplicateRequest, 0,
                             0, 0);
        return;
    }
    // 指令四十一/四十四：Shop Session 有效性（同 Buy）。
    const NpcEntity* npc = m_npcs.Find(player->ShopSession().npcEntityId);
    if (npc == nullptr || !player->Alive()) {
        CloseNpcSessions(player);
        SendShopSellResponse(player, request.requestId, false, ShopResultCode::SessionNotFound, 0,
                             0, 0);
        return;
    }
    const auto& shopSession = player->ShopSession();
    if (shopSession.sessionId == 0 || shopSession.sessionId != request.shopSessionId) {
        SendShopSellResponse(player, request.requestId, false, ShopResultCode::SessionNotFound, 0,
                             0, 0);
        return;
    }
    const auto elapsed =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - shopSession.openedAt)
            .count();
    if (elapsed > m_config.npcSessionTtlSeconds) {
        CloseNpcSessions(player);
        SendShopSellResponse(player, request.requestId, false, ShopResultCode::SessionExpired, 0,
                             0, 0);
        return;
    }
    {
        const float dx = npc->X() - player->PositionX();
        const float dy = npc->Y() - player->PositionY();
        const NpcDefinition* definition = npc->Definition();
        const float range =
            definition ? definition->interactionRange : kNpcDefaultInteractionRange;
        if (npc->MapId() != player->MapId() || dx * dx + dy * dy > range * range) {
            CloseNpcSessions(player);
            SendShopSellResponse(player, request.requestId, false, ShopResultCode::TooFar, 0, 0,
                                 0);
            return;
        }
    }
    // 指令五十：从背包实例取真实 definitionId（不能 Client 告诉服务器物品是什么）。
    InventoryEntry* entry = nullptr;
    std::uint32_t entrySlotIndex = 0;
    for (std::size_t i = 0; i < player->Inventory().SlotCount(); ++i) {
        InventoryEntry* slot = player->Inventory().MutableAt(i);
        if (slot != nullptr && slot->quantity > 0 && slot->instanceId == request.inventoryInstanceId) {
            entry = slot;
            entrySlotIndex = static_cast<std::uint32_t>(i);
            break;
        }
    }
    if (entry == nullptr || entry->instanceId == 0) {
        // 装备中的物品不在背包容器（指令五十一：必须先 Unequip 才能卖）。
        SendShopSellResponse(player, request.requestId, false, ShopResultCode::ItemNotFound, 0, 0,
                             0);
        return;
    }
    const ShopDefinition* shop = ShopRegistry::Instance().FindShop(shopSession.shopId);
    const ShopEntry* shopEntry = shop ? shop->FindEntry(entry->definitionId) : nullptr;
    if (shopEntry == nullptr || !shopEntry->canSell) {
        // 指令八十四：Shop 只收 canSell=true 的物品。
        SendShopSellResponse(player, request.requestId, false, ShopResultCode::CannotSell, 0, 0, 0);
        return;
    }
    // 指令五十二：Material 允许部分 stack 卖出；装备 quantity=1。
    const std::uint32_t quantity = request.quantity;
    if (quantity == 0 || quantity > entry->quantity) {
        SendShopSellResponse(player, request.requestId, false, ShopResultCode::InvalidQuantity, 0,
                             0, 0);
        return;
    }
    const std::uint64_t goldReceived =
        static_cast<std::uint64_t>(shopEntry->sellPrice) * static_cast<std::uint64_t>(quantity);
    const std::int64_t newGold = player->Gold() + static_cast<std::int64_t>(goldReceived);
    player->RememberNpcRequest(request.requestId);
    const std::uint32_t definitionId = entry->definitionId;
    const std::uint64_t instanceId = entry->instanceId;
    const bool wholeStack = quantity == entry->quantity;
    auto self = shared_from_this();
    const std::uint64_t characterId = player->CharacterId();
    const auto applyResult = [self, characterId, requestId = request.requestId, newGold,
                              instanceId, quantity, wholeStack, definitionId,
                              goldReceived](bool ok) {
        self->m_service.Post([self, characterId, requestId, newGold, instanceId, quantity,
                              wholeStack, definitionId, goldReceived, ok]() {
            if (self->m_stopped.load()) {
                return;
            }
            auto player = self->m_players.FindByCharacter(characterId);
            if (!ok) {
                // 指令五十四：失败整体回滚（DB ROLLBACK），内存未变。
                if (player) {
                    self->SendShopSellResponse(player, requestId, false,
                                               ShopResultCode::InternalError, 0, 0, 0);
                }
                return;
            }
            if (!player) {
                return;
            }
            // 成功后更新内存。
            player->SetProgression(player->Experience(), newGold);
            for (std::size_t i = 0; i < player->Inventory().SlotCount(); ++i) {
                InventoryEntry* slot = player->Inventory().MutableAt(i);
                if (slot != nullptr && slot->instanceId == instanceId) {
                    if (wholeStack) {
                        player->Inventory().TakeAt(i, *slot);
                    } else {
                        slot->quantity -= quantity;
                    }
                    self->SendInventoryDelta(player, wholeStack ? 2 : 1, *slot,
                                             static_cast<std::uint32_t>(i));
                    break;
                }
            }
            self->SendShopSellResponse(player, requestId, true, ShopResultCode::Success,
                                       definitionId, quantity,
                                       static_cast<std::uint32_t>(goldReceived));
            // 指令八十六：InProgress 的 Collect 按持有量下降（Ready 冻结不回退）。
            self->HandleQuestInventoryChanged(player);
            self->SendProgressionSnapshot(player);
            self->SendNpcQuestMarkersFor(player);
        });
    };
    InventoryRepository::ShopSellTransaction tx;
    tx.characterId = characterId;
    tx.newGold = newGold;
    tx.instanceId = instanceId;
    tx.quantity = quantity;
    tx.wholeStack = wholeStack;
    if (m_persistence) {
        // 阶段25.5：RPC 模式——出售原子事务在 DbServer 内执行。
        legend::internal::WorldShopSell command;
        command.characterId = tx.characterId;
        command.newGold = tx.newGold;
        command.instanceId = tx.instanceId;
        command.quantity = tx.quantity;
        command.wholeStack = tx.wholeStack;
        std::vector<std::uint8_t> payload;
        if (!legend::internal::EncodeWorldShopSell(command, payload)) {
            applyResult(false);
            return;
        }
        m_persistence->AsyncRequest(
            legend::internal::DbOperation::ShopSellWrite, std::move(payload),
            [applyResult](const legend::internal::DbResponse& response) {
                applyResult(response.errorCode == legend::internal::InternalErrorCode::Ok);
            });
        return;
    }
    m_dbWorker.Post([self, applyResult, tx]() {
        // 指令五十四：DB 事务 —— Inventory 扣除 + Gold 增加一次提交；失败全部回滚。
        std::string error;
        const bool ok = InventoryRepository::RunShopSellTransaction(self->m_database, tx, error);
        applyResult(ok);
    });
}

void WorldServer::HandleTeleportRequest(std::uint64_t connectionId,
                                        const legend::network::Packet& packet) {
    TeleportRequestPayload request;
    std::string decodeError;
    if (!DecodeTeleportRequest(packet.payload.data(), packet.payload.size(), request,
                               decodeError)) {
        LOG_INFO("[Npc] Malformed TeleportRequest from #" + std::to_string(connectionId));
        SendTeleportResponse(nullptr, request.requestId, false,
                             TeleportResultCode::MalformedRequest, 1, 0.0f, 0.0f, 0, 0);
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        SendTeleportResponse(nullptr, request.requestId, false, TeleportResultCode::NotInWorld, 1,
                             0.0f, 0.0f, 0, 0);
        return;
    }
    if (player->IsRecentNpcRequest(request.requestId)) {
        SendTeleportResponse(player, request.requestId, false,
                             TeleportResultCode::DuplicateRequest, player->MapId(),
                             player->PositionX(), player->PositionY(), 0, player->Gold());
        return;
    }
    (void)TeleportPlayerViaNpc(player, request.requestId, request.dialogueSessionId,
                               request.teleportId);
}

bool WorldServer::TeleportPlayerViaNpc(const std::shared_ptr<PlayerSession>& player,
                                       std::uint64_t requestId, std::uint64_t dialogueSessionId,
                                       std::uint32_t teleportId) {
    if (!player->Alive()) {
        SendTeleportResponse(player, requestId, false, TeleportResultCode::Dead,
                             player->MapId(), player->PositionX(), player->PositionY(), 0,
                             player->Gold());
        return false;
    }
    // 指令六十二/六十四：必须经有效 Dialogue Session + NPC 确实提供该传送。
    const NpcEntity* npc = m_npcs.Find(player->DialogueSession().npcEntityId);
    if (npc == nullptr ||
        NpcInteractionService::ValidateDialogueSession(
            *player, dialogueSessionId, *npc, m_config.npcSessionTtlSeconds,
            std::chrono::steady_clock::now()) != NpcResultCode::Success) {
        CloseNpcSessions(player);
        SendTeleportResponse(player, requestId, false, TeleportResultCode::SessionNotFound,
                             player->MapId(), player->PositionX(), player->PositionY(), 0,
                             player->Gold());
        return false;
    }
    const NpcDefinition* definition = npc->Definition();
    const TeleportDefinition* teleport = TeleportRegistry::Instance().FindTeleport(teleportId);
    if (teleport == nullptr) {
        SendTeleportResponse(player, requestId, false, TeleportResultCode::TeleportNotFound,
                             player->MapId(), player->PositionX(), player->PositionY(), 0,
                             player->Gold());
        return false;
    }
    if (definition == nullptr || definition->teleportId != teleportId) {
        // 指令六十四：对应 NPC 确实提供该 Teleport。
        SendTeleportResponse(player, requestId, false, TeleportResultCode::NotOfferedByNpc,
                             player->MapId(), player->PositionX(), player->PositionY(), 0,
                             player->Gold());
        return false;
    }
    if (player->Level() < teleport->minLevel) {
        SendTeleportResponse(player, requestId, false, TeleportResultCode::LevelTooLow,
                             player->MapId(), player->PositionX(), player->PositionY(), 0,
                             player->Gold());
        return false;
    }
    if (static_cast<std::uint64_t>(player->Gold()) < teleport->goldCost) {
        SendTeleportResponse(player, requestId, false, TeleportResultCode::NotEnoughGold,
                             player->MapId(), player->PositionX(), player->PositionY(), 0,
                             player->Gold());
        return false;
    }
    if (requestId != 0) {
        player->RememberNpcRequest(requestId);
    }
    // ---- 服务器权威执行（指令六十五~七十三；阶段21 指令五十三：内部走统一
    // MapTransitionService，不再保留第二套切图序列）----
    // 目的地预验证（扣费前；传送定义与注册表一致，理论上恒通过）。
    const MapDefinition* destination =
        MapRegistry::Instance().FindMap(teleport->destinationMapId);
    if (destination == nullptr || !destination->InBounds(teleport->destinationX,
                                                         teleport->destinationY)) {
        SendTeleportResponse(player, requestId, false, TeleportResultCode::InternalError,
                             player->MapId(), player->PositionX(), player->PositionY(), 0,
                             player->Gold());
        return false;
    }
    // 指令七十二：Cast 中传送 → 先取消（Teleported；Transition 内也会统一取消）。
    if (player->IsCasting()) {
        CancelActiveCast(player, SkillCancelReason::Teleported);
    }
    // 扣 Gold（内存）。
    const std::int64_t newGold = player->Gold() - static_cast<std::int64_t>(teleport->goldCost);
    player->SetProgression(player->Experience(), newGold);
    // 指令六十八：金币扣除必须落库（服务器权威价格/费用的持久化证据）。
    PersistGold(player->CharacterId(), newGold);
    // 指令二十三/二十四：统一地图切换（旧区 Despawn/可见集清理/权威位置/AOI 重建/
    // MapChanged/MapSnapshot/立即位置快照/持久化/Quest OnPlayerMoved 全部在服务内）。
    if (!m_mapTransition.TransitionPlayer(player, teleport->destinationMapId,
                                          teleport->destinationX, teleport->destinationY,
                                          "npc-teleport")) {
        SendTeleportResponse(player, requestId, false, TeleportResultCode::InternalError,
                             player->MapId(), player->PositionX(), player->PositionY(), 0,
                             player->Gold());
        return false;
    }
    SendTeleportResponse(player, requestId, true, TeleportResultCode::Success,
                         teleport->destinationMapId, teleport->destinationX,
                         teleport->destinationY, teleport->goldCost, newGold);
    SendProgressionSnapshot(player);
    return true;
}

void WorldServer::SendNpcInteractResponse(const std::shared_ptr<PlayerSession>& player,
                                          std::uint64_t requestId, bool success,
                                          NpcResultCode code, std::uint64_t npcEntityId,
                                          std::uint64_t dialogueSessionId,
                                          std::uint32_t dialogueId) {
    NpcInteractResponsePayload out;
    out.requestId = requestId;
    out.success = success;
    out.resultCode = static_cast<std::uint8_t>(code);
    out.npcEntityId = npcEntityId;
    out.dialogueSessionId = dialogueSessionId;
    out.dialogueId = dialogueId;
    out.message = NpcResultCodeName(static_cast<std::uint8_t>(code));
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::NpcInteractResponse);
    if (EncodeNpcInteractResponse(out, packet.payload) && player) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendShopOpenResponse(const std::shared_ptr<PlayerSession>& player,
                                       std::uint64_t requestId, bool success,
                                       ShopResultCode code, std::uint64_t shopSessionId,
                                       std::uint32_t shopId, std::uint64_t npcEntityId,
                                       const ShopDefinition* shop) {
    ShopOpenResponsePayload out;
    out.requestId = requestId;
    out.success = success;
    out.resultCode = static_cast<std::uint8_t>(code);
    out.shopSessionId = shopSessionId;
    out.shopId = shopId;
    out.npcEntityId = npcEntityId;
    if (shop != nullptr) {
        for (const auto& entry : shop->entries) {
            out.entries.push_back({entry.itemDefinitionId, entry.buyPrice, entry.sellPrice,
                                   entry.canBuy, entry.canSell});
        }
    }
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::ShopOpenResponse);
    if (EncodeShopOpenResponse(out, packet.payload) && player) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendShopBuyResponse(const std::shared_ptr<PlayerSession>& player,
                                      std::uint64_t requestId, bool success,
                                      ShopResultCode code, std::uint32_t itemDefinitionId,
                                      std::uint32_t quantity, std::uint32_t goldSpent) {
    // 指令四十七：BuyItemResponse（newGold 从 PlayerSession 权威值回填）。
    ShopBuyResponsePayload out;
    out.requestId = requestId;
    out.success = success;
    out.resultCode = static_cast<std::uint8_t>(code);
    out.itemDefinitionId = itemDefinitionId;
    out.quantity = quantity;
    out.goldSpent = goldSpent;
    out.newGold = player ? player->Gold() : 0;
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::ShopBuyResponse);
    if (EncodeShopBuyResponse(out, packet.payload) && player) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendShopSellResponse(const std::shared_ptr<PlayerSession>& player,
                                       std::uint64_t requestId, bool success,
                                       ShopResultCode code, std::uint32_t itemDefinitionId,
                                       std::uint32_t quantity, std::uint32_t goldReceived) {
    ShopSellResponsePayload out;
    out.requestId = requestId;
    out.success = success;
    out.resultCode = static_cast<std::uint8_t>(code);
    out.itemDefinitionId = itemDefinitionId;
    out.quantity = quantity;
    out.goldReceived = goldReceived;
    out.newGold = player ? player->Gold() : 0;
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::ShopSellResponse);
    if (EncodeShopSellResponse(out, packet.payload) && player) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendTeleportResponse(const std::shared_ptr<PlayerSession>& player,
                                       std::uint64_t requestId, bool success,
                                       TeleportResultCode code, std::uint16_t mapId, float x,
                                       float y, std::uint32_t goldCost, std::int64_t newGold) {
    TeleportResponsePayload out;
    out.requestId = requestId;
    out.success = success;
    out.resultCode = static_cast<std::uint8_t>(code);
    out.mapId = mapId;
    out.x = x;
    out.y = y;
    out.goldCost = goldCost;
    out.newGold = newGold;
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::TeleportResponse);
    if (EncodeTeleportResponse(out, packet.payload) && player) {
        SendPacketToPlayer(player, packet);
    }
}

bool WorldServer::TestTeleportPlayer(std::uint64_t characterId, std::uint16_t mapId, float x,
                                     float y) {
    if (m_stopped.load()) {
        return false;
    }
    auto self = shared_from_this();
    m_service.Post([self, characterId, mapId, x, y]() {
        auto player = self->m_players.FindByCharacter(characterId);
        if (!player) {
            return;
        }
        // 阶段21 指令二十三：测试白盒同样走统一 MapTransitionService。
        (void)self->m_mapTransition.TransitionPlayer(player, mapId, x, y, "test-teleport");
    });
    return true;
}

// ---------------------------------------------------------------------------
// 阶段21：多地图 / Portal / 复活编排。
// ---------------------------------------------------------------------------

// 指令十四/十七：启动生成 4 个 Portal（registry 顺序即 entity 1~4）。
void WorldServer::SpawnInitialPortals() {
    const std::size_t spawned = m_portals.SpawnFromRegistry();
    m_portals.AddToGrid(m_portalGrid);
    LOG_INFO("[Portal] spawned " + std::to_string(spawned) + " portals");
}

// 指令十七：Portal AOI（Enter 600 / Leave 700；仅同 mapId；visiblePortals 权威维护）。
void WorldServer::UpdatePlayerPortalVisibility(const std::shared_ptr<PlayerSession>& player,
                                               bool initialVisibility) {
    const auto candidates = m_portalGrid.QueryRange(player->PositionX(), player->PositionY(),
                                                    m_config.aoiLeaveRadius);
    const float enterRadiusSq = m_config.aoiEnterRadius * m_config.aoiEnterRadius;
    const float leaveRadiusSq = m_config.aoiLeaveRadius * m_config.aoiLeaveRadius;
    for (const std::uint64_t portalEntityId : candidates) {
        if (player->VisiblePortals().count(portalEntityId) != 0) {
            continue;
        }
        const PortalDefinition* portal = m_portals.Find(portalEntityId);
        if (portal == nullptr || !portal->enabled || portal->sourceMapId != player->MapId()) {
            continue;
        }
        const float dx = portal->x - player->PositionX();
        const float dy = portal->y - player->PositionY();
        if (dx * dx + dy * dy > enterRadiusSq) {
            continue;
        }
        SendPortalSpawn(player, portalEntityId, *portal);
        player->AddVisiblePortal(portalEntityId);
    }
    if (!initialVisibility) {
        std::vector<std::uint64_t> leaves;
        for (const std::uint64_t portalEntityId : player->VisiblePortals()) {
            const PortalDefinition* portal = m_portals.Find(portalEntityId);
            if (portal == nullptr || portal->sourceMapId != player->MapId()) {
                leaves.push_back(portalEntityId);
                continue;
            }
            const float dx = portal->x - player->PositionX();
            const float dy = portal->y - player->PositionY();
            if (dx * dx + dy * dy > leaveRadiusSq) {
                leaves.push_back(portalEntityId);
            }
        }
        for (const std::uint64_t portalEntityId : leaves) {
            SendPortalDespawn(player, portalEntityId, PortalDespawnReason::LeftAOI);
            player->EraseVisiblePortal(portalEntityId);
        }
    }
}

void WorldServer::SendPortalSpawn(const std::shared_ptr<PlayerSession>& receiver,
                                  std::uint64_t portalEntityId, const PortalDefinition& portal) {
    const MapDefinition* destination = MapRegistry::Instance().FindMap(portal.destinationMapId);
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::PortalSpawn);
    if (EncodePortalSpawn(packet.payload, portalEntityId, portal.portalId,
                          "Portal " + std::to_string(portal.portalId), portal.sourceMapId,
                          portal.x, portal.y, portal.interactionRadius,
                          portal.destinationMapId,
                          destination != nullptr ? destination->name : "Unknown")) {
        SendPacketToPlayer(receiver, packet);
    }
}

void WorldServer::SendPortalDespawn(const std::shared_ptr<PlayerSession>& receiver,
                                    std::uint64_t portalEntityId, PortalDespawnReason reason) {
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::PortalDespawn);
    if (EncodePortalDespawn(packet.payload, portalEntityId, static_cast<std::uint8_t>(reason))) {
        SendPacketToPlayer(receiver, packet);
    }
}

// 指令十九~二十二/五十七/五十九/六十：PortalUseRequest 验证链 + 服务器权威执行。
void WorldServer::HandlePortalUseRequest(std::uint64_t connectionId,
                                         const legend::network::Packet& packet) {
    PortalUseRequestPayload request;
    std::string decodeError;
    std::printf("[Diag] PortalUse handler: conn=%llu payload=%zu bytes\n",
                static_cast<unsigned long long>(connectionId), packet.payload.size());
    if (!DecodePortalUseRequest(packet.payload.data(), packet.payload.size(), request,
                                decodeError)) {
        LOG_INFO("[Portal] Malformed PortalUseRequest from #" + std::to_string(connectionId) +
                 " detail=" + decodeError);
        return; // Malformed 不断线（指令五十七）
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        return;
    }
    const auto fail = [&](PortalResultCode code) {
        SendPortalUseResponse(player, request.requestId, false, code, player->MapId(), 0.0f, 0.0f,
                              0, player->Gold());
    };
    // 指令五十九：防重放（最近 64 成功 requestId）。
    if (player->IsRecentMapRequest(request.requestId)) {
        fail(PortalResultCode::DuplicateRequest);
        return;
    }
    if (player->IsMapTransitionInProgress()) { // 指令六十：并发保护
        fail(PortalResultCode::TransitionInProgress);
        return;
    }
    if (!player->Alive()) { // 指令三十二：Dead 禁用 Portal
        fail(PortalResultCode::Dead);
        return;
    }
    const PortalDefinition* portal = m_portals.Find(request.portalEntityId);
    if (portal == nullptr) {
        fail(PortalResultCode::PortalNotFound);
        return;
    }
    if (!portal->enabled) {
        fail(PortalResultCode::PortalDisabled);
        return;
    }
    if (portal->sourceMapId != player->MapId()) { // 指令二十一：same map
        fail(PortalResultCode::WrongMap);
        return;
    }
    if (player->VisiblePortals().count(request.portalEntityId) == 0) { // 指令二十一：visible
        fail(PortalResultCode::NotVisible);
        return;
    }
    const float dx = portal->x - player->PositionX();
    const float dy = portal->y - player->PositionY();
    if (dx * dx + dy * dy >
        portal->interactionRadius * portal->interactionRadius) { // 指令二十一：距离
        fail(PortalResultCode::TooFar);
        return;
    }
    if (player->Level() < portal->minLevel) { // 指令七十八：等级
        fail(PortalResultCode::LevelTooLow);
        return;
    }
    if (static_cast<std::uint64_t>(player->Gold()) < portal->goldCost) { // 指令七十九：Gold
        fail(PortalResultCode::NotEnoughGold);
        return;
    }
    // ---- 验证链通过：服务器权威执行（扣费 + 统一切图）。----
    player->RememberMapRequest(request.requestId);
    const std::int64_t newGold = player->Gold() - static_cast<std::int64_t>(portal->goldCost);
    player->SetProgression(player->Experience(), newGold);
    if (portal->goldCost > 0) {
        PersistGold(player->CharacterId(), newGold);
    }
    if (!m_mapTransition.TransitionPlayer(player, portal->destinationMapId, portal->destinationX,
                                          portal->destinationY, "portal")) {
        fail(PortalResultCode::MapNotFound);
        return;
    }
    SendPortalUseResponse(player, request.requestId, true, PortalResultCode::Success,
                          portal->destinationMapId, portal->destinationX, portal->destinationY,
                          portal->goldCost, newGold);
    SendProgressionSnapshot(player);
}

// 指令三十三~四十二：RespawnRequest 验证 + 服务器权威执行。
void WorldServer::HandleRespawnRequest(std::uint64_t connectionId,
                                       const legend::network::Packet& packet) {
    RespawnRequestPayload request;
    std::string decodeError;
    if (!DecodeRespawnRequest(packet.payload.data(), packet.payload.size(), request,
                              decodeError)) {
        LOG_INFO("[Respawn] Malformed RespawnRequest from #" + std::to_string(connectionId));
        return;
    }
    auto player = m_players.FindByConnection(connectionId);
    if (!player) {
        return;
    }
    // 指令五十八：mode 语义校验（1/2 之外拒绝——Decode 已保证单字节）。
    if (request.respawnMode != static_cast<std::uint8_t>(RespawnMode::CurrentMap) &&
        request.respawnMode != static_cast<std::uint8_t>(RespawnMode::Town)) {
        SendRespawnResponse(player, request.requestId, false, RespawnResultCode::InvalidMode,
                            player->MapId(), player->PositionX(), player->PositionY(), 0,
                            player->Gold());
        return;
    }
    (void)ExecuteRespawn(player, static_cast<RespawnMode>(request.respawnMode),
                         request.requestId);
}

// 指令三十四~四十八：复活编排（规则 = RespawnService；执行 = WorldServer）。
bool WorldServer::ExecuteRespawn(const std::shared_ptr<PlayerSession>& player, RespawnMode mode,
                                 std::uint64_t requestId) {
    const auto now = std::chrono::steady_clock::now();
    const auto fail = [&](RespawnResultCode code) {
        SendRespawnResponse(player, requestId, false, code, player->MapId(),
                            player->PositionX(), player->PositionY(), 0, player->Gold());
        return false;
    };
    // 指令五十九：防重放（重复复活/重复扣金）。
    if (player->IsRecentMapRequest(requestId)) {
        return fail(RespawnResultCode::DuplicateRequest);
    }
    if (player->IsMapTransitionInProgress()) { // 指令六十：并发保护
        return fail(RespawnResultCode::TransitionInProgress);
    }
    const RespawnPlan plan = RespawnService::PlanRespawn(*player, mode, now);
    if (!plan.valid) {
        return fail(plan.code);
    }
    player->RememberMapRequest(requestId);
    // 指令三十八：扣复活费（CurrentMap 10G；Town 免费）+ 落库。
    const std::int64_t newGold = player->Gold() - static_cast<std::int64_t>(plan.goldCost);
    player->SetProgression(player->Experience(), newGold);
    PersistGold(player->CharacterId(), newGold);
    // 指令四十：复活清空全部状态（Buff/Debuff 统一清除 -> StatusContainer 为空）。
    if (!player->StatusEffects().Empty()) {
        const auto effects = player->StatusEffects().All();
        std::vector<ActiveStatusEffect> removed;
        for (const auto& [effectId, effect] : effects) {
            removed.push_back(effect);
        }
        player->StatusEffects().Clear();
        RecalculateTargetDerivedStats(CombatEntityType::Player, player->CharacterId());
        for (const auto& effect : removed) {
            SendStatusRemoved(PlayerStatusReceivers(player->CharacterId()), effect,
                              StatusRemovedReason::Expired);
        }
    }
    // 指令三十九：复活状态恢复（HP/Mana 满 + Alive）。
    player->Revive();
    // 指令四十一/四十五：AOI 重建（同图/跨图统一走 MapTransitionService）+
    // 复活保护 3 秒（期间怪物伤害无效；主动攻击/施法立即取消）。
    if (!m_mapTransition.TransitionPlayer(player, plan.destMapId, plan.destX, plan.destY,
                                          plan.crossMap ? "respawn-town" : "respawn-current")) {
        std::printf("[Diag] Respawn transition FAILED: char=%llu dest=%u gold=%lld alive=%d "
                    "transitionFlag=%d\n",
                    static_cast<unsigned long long>(player->CharacterId()),
                    static_cast<unsigned>(plan.destMapId), static_cast<long long>(player->Gold()),
                    player->Alive() ? 1 : 0, player->IsMapTransitionInProgress() ? 1 : 0);
        return fail(RespawnResultCode::InternalError);
    }
    player->SetRespawnProtection(kRespawnProtectionSeconds);
    // 指令三十三/三十四：成功 RespawnResponse（fail 路径之外必须显式回执）。
    SendRespawnResponse(player, requestId, true, RespawnResultCode::Success, plan.destMapId,
                        plan.destX, plan.destY, plan.goldCost, newGold);
    // 指令四十二：PlayerRespawned（只发本人）+ Health/Mana 快照。
    SendPlayerRespawned(player);
    SendEntityHealthSnapshot(player, CombatEntityType::Player, player->CharacterId(),
                             player->CurrentHp(), player->MaxHp(), true);
    SendManaSnapshots();
    SendProgressionSnapshot(player);
    LOG_INFO("[Respawn] char=" + player->CharacterName() + " mode=" +
             RespawnModeName(static_cast<std::uint8_t>(mode)) + " -> map=" +
             std::to_string(plan.destMapId) + " cost=" + std::to_string(plan.goldCost));
    return true;
}

void WorldServer::SendPortalUseResponse(const std::shared_ptr<PlayerSession>& player,
                                        std::uint64_t requestId, bool success,
                                        PortalResultCode code, std::uint16_t destinationMapId,
                                        float destinationX, float destinationY,
                                        std::uint32_t goldCost, std::int64_t newGold) {
    PortalUseResponsePayload out;
    out.requestId = requestId;
    out.success = success;
    out.resultCode = static_cast<std::uint8_t>(code);
    out.sourceMapId = player->MapId();
    out.destinationMapId = destinationMapId;
    out.destinationX = destinationX;
    out.destinationY = destinationY;
    out.goldCost = goldCost;
    out.newGold = newGold;
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::PortalUseResponse);
    if (EncodePortalUseResponse(out, packet.payload)) {
        SendPacketToPlayer(player, packet);
    }
}

void WorldServer::SendRespawnResponse(const std::shared_ptr<PlayerSession>& player,
                                      std::uint64_t requestId, bool success,
                                      RespawnResultCode code, std::uint16_t mapId, float x,
                                      float y, std::uint32_t goldCost, std::int64_t newGold) {
    RespawnResponsePayload out;
    out.requestId = requestId;
    out.success = success;
    out.resultCode = static_cast<std::uint8_t>(code);
    out.mapId = mapId;
    out.x = x;
    out.y = y;
    out.goldCost = goldCost;
    out.newGold = newGold;
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::RespawnResponse);
    if (EncodeRespawnResponse(out, packet.payload)) {
        SendPacketToPlayer(player, packet);
    }
}

// 指令四十二：PlayerRespawned 事件（mapId/位置/HP/Mana/Gold/serverTime；只发本人）。
void WorldServer::SendPlayerRespawned(const std::shared_ptr<PlayerSession>& player) {
    PlayerRespawnedPayload out;
    out.mapId = player->MapId();
    out.x = player->PositionX();
    out.y = player->PositionY();
    out.hp = player->CurrentHp();
    out.maxHp = player->MaxHp();
    out.mana = player->CurrentMana();
    out.maxMana = player->MaxMana();
    out.gold = player->Gold();
    out.serverTime = ServerTimeMs();
    Packet packet;
    packet.header.messageId = static_cast<std::uint16_t>(MessageId::PlayerRespawned);
    if (EncodePlayerRespawned(out, packet.payload)) {
        SendPacketToPlayer(player, packet);
    }
}

// 测试白盒：直接设置等级（Portal 8003 Level 验证布景；与 ApplyLevelGrowth 同步属性）。
bool WorldServer::TestSetPlayerLevel(std::uint64_t characterId, std::uint32_t level) {
    if (m_stopped.load()) {
        return false;
    }
    auto self = shared_from_this();
    m_service.Post([self, characterId, level]() {
        auto player = self->m_players.FindByCharacter(characterId);
        if (player && level >= 1) {
            player->ApplyLevelGrowth(level);
            self->SendProgressionSnapshot(player);
        }
    });
    return true;
}

// 测试白盒：设置金币（Portal/复活 Gold 验证布景；内存权威值，不写 DB）。
bool WorldServer::TestSetPlayerGold(std::uint64_t characterId, std::int64_t gold) {
    if (m_stopped.load()) {
        return false;
    }
    auto self = shared_from_this();
    m_service.Post([self, characterId, gold]() {
        auto player = self->m_players.FindByCharacter(characterId);
        if (player && gold >= 0) {
            player->SetProgression(player->Experience(), gold);
            self->SendProgressionSnapshot(player);
        }
    });
    return true;
}

// Stage25.6 服务器管理台：World 只读统计快照（GUI 线程每 500ms 调用）。
WorldServer::WorldStatsSnapshot WorldServer::CollectStats() const {
    WorldStatsSnapshot stats;
    stats.playerCount = m_players.PlayerCount();
    stats.map1Players = m_mapManager.PlayerCount(1);
    stats.map2Players = m_mapManager.PlayerCount(2);
    stats.map3Players = m_mapManager.PlayerCount(3);
    stats.monsterCount = m_monsters.Count();
    stats.npcCount = m_npcs.Count();
    stats.portalCount = m_portals.Count();
    stats.dropCount = m_itemDrops.Count();
    stats.castingPlayers = CastingPlayerCount();
    const auto count = m_snapshotTickCount.load(std::memory_order_relaxed);
    const auto totalUs = m_snapshotTickTotalUs.load(std::memory_order_relaxed);
    stats.snapshotTickCount = count;
    stats.tickAvgMs = count > 0 ? static_cast<double>(totalUs) / count / 1000.0 : 0.0;
    stats.tickMaxMs = static_cast<double>(m_snapshotTickMaxUs.load(std::memory_order_relaxed)) / 1000.0;
    if (m_server) {
        stats.packetsReceived = m_server->PacketsReceived();
        stats.packetsSent = m_server->PacketsSent();
    }
    if (m_persistence) {
        stats.dbAvailable = m_persistence->IsAvailable();
        stats.dbDegraded = m_persistence->IsDegraded();
    } else {
        // dbPort=0 legacy 隔离模式：本地 SQLite/DB Worker
        stats.dbAvailable = m_config.dbPort == 0;
        stats.dbDegraded = false;
    }
    return stats;
}

} // namespace legend::world
