#include "Server/Db/DbServer.h"

#include "Engine/Debug/Logger.h"
#include "Server/LoginServer/Account/Database/DatabaseSchema.h"
#include "Server/WorldServer/Item/InventoryRepository.h"
#include "Server/WorldServer/Quest/QuestRepository.h"
#include "Shared/InternalProtocol/PersistenceMessages.h"
#include "Shared/Network/MessageId.h"

#include <filesystem>

namespace legend::db {

using namespace legend::internal;
using legend::network::MessageId;
using legend::network::Packet;

namespace {
InternalErrorCode MapAccountError(legend::account::AccountErrorCode code) {
    using A = legend::account::AccountErrorCode;
    switch (code) {
        case A::CharacterNotFound: return InternalErrorCode::CharacterNotFound;
        case A::CharacterNotOwned: return InternalErrorCode::CharacterOwnershipMismatch;
        case A::SessionInvalid: return InternalErrorCode::NotAuthenticated;
        case A::DatabaseError: return InternalErrorCode::DatabaseUnavailable;
        case A::InvalidCredentials: return InternalErrorCode::InvalidCredentials;
        default: return InternalErrorCode::InvalidRequest;
    }
}

// Stage25.6 管理台：操作分类（读 / 写 / 原子事务）
bool IsReadOperation(DbOperation op) {
    switch (op) {
        case DbOperation::LoadAccount:
        case DbOperation::LoadCharacterList:
        case DbOperation::LoadCharacterState:
        case DbOperation::LoadCharacterFull:
        case DbOperation::LoadInventory:
        case DbOperation::LoadQuestState:
        case DbOperation::LoadEquipment:
        case DbOperation::LoadPosition:
        case DbOperation::LoadProgression:
        case DbOperation::ValidateSession:
            return true;
        default:
            return false;
    }
}

bool IsAtomicTransaction(DbOperation op) {
    switch (op) {
        case DbOperation::EquipItemWrite:
        case DbOperation::UnequipItemWrite:
        case DbOperation::QuestTurnInWrite:
        case DbOperation::ShopBuyWrite:
        case DbOperation::ShopSellWrite:
            return true;
        default:
            return false;
    }
}
}

DbServer::DbServer(legend::net::NetworkService& service)
    : m_service(service), m_server(std::make_shared<legend::net::TcpServer>(service)) {}

bool DbServer::Start(std::string& error) {
    if (!m_stopped.exchange(false)) return true;
    const std::filesystem::path path(m_config.databasePath);
    std::error_code ec;
    if (path.has_parent_path()) std::filesystem::create_directories(path.parent_path(), ec);
    if (!m_database.Open(m_config.databasePath, error)) {
        m_stopped.store(true); return false;
    }
    if (!legend::account::InitializeSchema(m_database, error)) {
        m_database.Close(); m_stopped.store(true); return false;
    }
    m_worker.Start();
    // Stage25.6 管理台：DB 线程读取运行时 Migration 版本（连接仅限 DB 线程使用）
    m_worker.Post([self = shared_from_this()] {
        int version = 0; std::string versionError;
        if (legend::account::ReadSchemaVersion(self->m_database, version, versionError)) {
            self->m_schemaVersion.store(version, std::memory_order_relaxed);
        }
    });
    if (!m_server->Listen(m_config.listenPort, error)) {
        m_worker.Stop(); m_database.Close(); m_stopped.store(true); return false;
    }
    m_server->StartAccepting([self = shared_from_this()](legend::net::TcpConnectionPtr connection) {
        self->OnAccepted(std::move(connection));
    });
    m_health.store(ServiceHealth::Healthy);
    return true;
}

void DbServer::Stop() {
    if (m_stopped.exchange(true)) return;
    m_health.store(ServiceHealth::Unavailable);
    m_server->Stop();
    { std::lock_guard<std::mutex> lock(m_mutex); m_links.clear(); }
    m_worker.Stop();
    m_database.Close();
}

std::size_t DbServer::ConnectionCount() const {
    std::lock_guard<std::mutex> lock(m_mutex); return m_links.size();
}

void DbServer::OnAccepted(legend::net::TcpConnectionPtr connection) {
    const auto id = connection->Id();
    { std::lock_guard<std::mutex> lock(m_mutex); m_links[id] = {connection, false}; }
    auto self = shared_from_this();
    connection->Start([self, id](const Packet& packet) { self->OnPacket(id, packet); },
                      [self, id](std::uint64_t, const std::error_code&) { self->OnClosed(id); });
}

void DbServer::OnClosed(std::uint64_t connectionId) {
    std::lock_guard<std::mutex> lock(m_mutex); m_links.erase(connectionId);
}

void DbServer::OnPacket(std::uint64_t connectionId, const Packet& packet) {
    bool authenticated = false;
    { std::lock_guard<std::mutex> lock(m_mutex); const auto it = m_links.find(connectionId);
      if (it == m_links.end()) return; authenticated = it->second.authenticated; }
    const auto id = static_cast<MessageId>(packet.header.messageId);
    if (!authenticated) {
        if (id != MessageId::InternalServiceHandshake) { OnClosed(connectionId); return; }
        ServiceHandshake handshake; std::string error;
        ServiceHandshakeAck ack;
        const bool decoded = DecodeServiceHandshake(packet.payload.data(), packet.payload.size(), handshake, error);
        const bool allowed = decoded && (handshake.serviceType == ServiceType::LoginServer ||
            handshake.serviceType == ServiceType::CharacterServer ||
            handshake.serviceType == ServiceType::WorldServer) &&
            (m_config.serviceToken.empty() || handshake.serviceToken == m_config.serviceToken);
        ack.accepted = allowed;
        ack.errorCode = allowed ? InternalErrorCode::Ok : InternalErrorCode::ProtocolMismatch;
        ack.message = allowed ? "ready" : "service identity rejected";
        Packet response; response.header.messageId = static_cast<std::uint16_t>(MessageId::InternalServiceHandshakeAck);
        EncodeServiceHandshakeAck(ack, response.payload); Send(connectionId, response);
        if (allowed) { std::lock_guard<std::mutex> lock(m_mutex); auto it = m_links.find(connectionId);
            if (it != m_links.end()) it->second.authenticated = true; }
        return;
    }
    if (id == MessageId::InternalHeartbeat) {
        Heartbeat heartbeat; std::string error;
        if (!DecodeHeartbeat(packet.payload.data(), packet.payload.size(), heartbeat, error)) OnClosed(connectionId);
        else Send(connectionId, packet);
        return;
    }
    if (id != MessageId::InternalDbRequest) { OnClosed(connectionId); return; }
    DbRequest request; std::string error;
    if (!DecodeDbRequest(packet.payload.data(), packet.payload.size(), request, error)) {
        OnClosed(connectionId); return;
    }
    HandleRequest(connectionId, std::move(request));
}

void DbServer::HandleRequest(std::uint64_t connectionId, DbRequest request) {
    auto self = shared_from_this();
    m_worker.Post([self, connectionId, request = std::move(request)]() mutable {
        auto response = self->Execute(request);
        // Stage25.6 管理台埋点：读/写/事务/失败分类计数（DB 线程内，原子）
        if (IsReadOperation(request.operation)) {
            self->m_queries.fetch_add(1, std::memory_order_relaxed);
        } else {
            self->m_writes.fetch_add(1, std::memory_order_relaxed);
            if (IsAtomicTransaction(request.operation)) {
                self->m_transactions.fetch_add(1, std::memory_order_relaxed);
            }
        }
        if (response.errorCode != InternalErrorCode::Ok) {
            self->m_failures.fetch_add(1, std::memory_order_relaxed);
        }
        self->m_service.Post([self, connectionId, response = std::move(response)]() mutable {
            Packet packet; packet.header.messageId = static_cast<std::uint16_t>(MessageId::InternalDbResponse);
            if (EncodeDbResponse(response, packet.payload)) self->Send(connectionId, packet);
        });
    });
}

DbResponse DbServer::Execute(const DbRequest& request) {
    using legend::world::InventoryRepository;
    using legend::world::QuestRepository;
    DbResponse response; response.requestId = request.requestId;
    response.recordVersion = m_revision.load();
    std::string error;
    if (request.expectedVersion != 0 && request.expectedVersion != response.recordVersion) {
        response.errorCode = InternalErrorCode::PersistenceConflict;
        response.message = "record version conflict";
        return response;
    }
    switch (request.operation) {
        case DbOperation::SaveAccount: {
            AccountCredentials command;
            if (!DecodeAccountCredentials(request.payload.data(), request.payload.size(), command, error)) break;
            auto result = m_accountService.Register(m_database, command.username, command.password);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            EncodeAccountRegisterResult({result.value}, response.payload);
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::LoadAccount: {
            AccountCredentials query;
            if (!DecodeAccountCredentials(request.payload.data(), request.payload.size(), query, error)) break;
            auto result = m_accountService.Login(m_database, query.username, query.password, 24 * 60 * 60);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            EncodeAccountLoginResult({result.value.accountId, result.value.sessionToken,
                                      result.value.sessionExpiresAt}, response.payload);
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::ValidateSession: {
            ValidateSessionRequest query;
            if (!DecodeValidateSessionRequest(request.payload.data(), request.payload.size(), query, error)) break;
            auto result = m_sessionService.Resume(m_database, query.sessionToken);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            EncodeValidateSessionResult({result.value.accountId, result.value.expiresAt}, response.payload);
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::LoadCharacterList: {
            CharacterListQuery query;
            if (!DecodeCharacterListQuery(request.payload.data(), request.payload.size(), query, error)) break;
            auto result = m_characterService.List(m_database, query.accountId);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            EncodeCharacterList(result.value, response.payload);
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::CreateCharacter: {
            CharacterCreateCommand command;
            if (!DecodeCharacterCreateCommand(request.payload.data(), request.payload.size(), command, error)) break;
            auto result = m_characterService.Create(m_database, command.accountId, command.name,
                                                    command.classId, command.gender,
                                                    command.visualId);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            EncodeCharacterSummary(result.value, response.payload);
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::DeleteCharacter: {
            CharacterCommand command;
            if (!DecodeCharacterCommand(request.payload.data(), request.payload.size(), command, error)) break;
            auto result = m_characterService.Delete(m_database, command.accountId, command.characterId);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::LoadCharacterState: {
            CharacterCommand command;
            if (!DecodeCharacterCommand(request.payload.data(), request.payload.size(), command, error)) break;
            auto result = m_characterService.Select(m_database, command.accountId, command.characterId);
            if (!result.success) { response.errorCode = MapAccountError(result.errorCode);
                response.message = result.errorMessage; return response; }
            EncodeCharacterSummary(result.value, response.payload);
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        // -------------------------------------------------------------------
        // 阶段25.5：World 持久化 RPC（事务在 DbServer DB Worker 线程执行；
        // World io 线程零 SQLite）。失败返回非 Ok 错误码（不中断连接）。
        // -------------------------------------------------------------------
        case DbOperation::LoadCharacterFull: {
            CharacterIdQuery query;
            if (!DecodeCharacterIdQuery(request.payload.data(), request.payload.size(), query, error)) break;
            auto found = legend::account::CharacterRepository::FindCharacterById(
                m_database, query.characterId);
            if (!found.success) { response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = found.errorMessage; return response; }
            if (!found.value.has_value() || found.value->deleted) {
                response.errorCode = InternalErrorCode::CharacterNotFound;
                response.message = "character not found"; return response;
            }
            const auto& row = *found.value;
            WorldCharacterRow out;
            out.id = row.id; out.accountId = row.accountId; out.name = row.name;
            out.classId = row.classId; out.gender = row.gender; out.level = row.level;
            out.exp = row.exp; out.gold = row.gold; out.mapId = row.mapId;
            out.positionX = static_cast<float>(row.positionX);
            out.positionY = static_cast<float>(row.positionY);
            out.deleted = row.deleted;
            if (!EncodeWorldCharacterRow(out, response.payload)) {
                LOG_ERROR("[Db] LoadCharacterFull encode failed (nameSize=" +
                          std::to_string(row.name.size()) + " deleted=" +
                          std::to_string(row.deleted ? 1 : 0) + ")");
                response.payload.clear();
                response.errorCode = InternalErrorCode::InternalError;
                response.message = "character row encode failed";
                return response;
            }
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::LoadInventory: {
            CharacterIdQuery query;
            if (!DecodeCharacterIdQuery(request.payload.data(), request.payload.size(), query, error)) break;
            WorldInventoryList out;
            out.characterId = query.characterId;
            std::vector<InventoryRepository::InventoryRow> rows;
            if (!InventoryRepository::LoadInventory(m_database, query.characterId, rows, error)) {
                response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = error; return response;
            }
            out.items.reserve(rows.size());
            for (const auto& row : rows) {
                out.items.push_back({row.instanceId, row.definitionId, row.quantity,
                                     row.slotIndex, row.createdAt});
            }
            EncodeWorldInventoryList(out, response.payload);
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::LoadQuestState: {
            CharacterIdQuery query;
            if (!DecodeCharacterIdQuery(request.payload.data(), request.payload.size(), query, error)) break;
            WorldQuestStateList out;
            std::vector<QuestRepository::QuestRow> quests;
            std::vector<QuestRepository::ObjectiveRow> objectives;
            if (!QuestRepository::LoadCharacterQuests(m_database, query.characterId, quests,
                                                      objectives, error)) {
                response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = error; return response;
            }
            out.quests.reserve(quests.size());
            for (const auto& row : quests) {
                out.quests.push_back({static_cast<std::uint32_t>(row.questId), row.state,
                                      row.acceptedAt, row.completedAt, row.turnedInAt});
            }
            out.objectives.reserve(objectives.size());
            for (const auto& row : objectives) {
                out.objectives.push_back({static_cast<std::uint32_t>(row.questId),
                                          row.objectiveId, row.progress});
            }
            EncodeWorldQuestStateList(out, response.payload);
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::SavePosition: {
            WorldSavePosition command;
            if (!DecodeWorldSavePosition(request.payload.data(), request.payload.size(), command, error)) break;
            auto saved = legend::account::CharacterRepository::UpdateWorldPosition(
                m_database, command.characterId, command.mapId, command.positionX,
                command.positionY, command.lastPlayedAt);
            if (!saved.success) { response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = saved.errorMessage; return response; }
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::SaveProgression: {
            WorldSaveProgression command;
            if (!DecodeWorldSaveProgression(request.payload.data(), request.payload.size(), command, error)) break;
            auto saved = legend::account::CharacterRepository::SaveProgression(
                m_database, command.characterId, command.level, command.exp, command.gold);
            if (!saved.success) { response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = saved.errorMessage; return response; }
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::ItemInsertWrite: {
            WorldItemInsert command;
            if (!DecodeWorldItemInsert(request.payload.data(), request.payload.size(), command, error)) break;
            const auto newInstanceId = InventoryRepository::InsertOrMergeItem(
                m_database, command.characterId, command.definitionId, command.quantity,
                command.slotIndex, command.createdAt, command.mergedIntoStack,
                command.mergeInstanceId, command.mergeQuantity);
            if (newInstanceId == 0) { response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = "item insert failed"; return response; }
            EncodeWorldItemInsertResult({newInstanceId}, response.payload);
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::EquipItemWrite: {
            WorldEquipItem command;
            if (!DecodeWorldEquipItem(request.payload.data(), request.payload.size(), command, error)) break;
            InventoryRepository::EquipTransaction tx;
            tx.characterId = command.characterId;
            tx.newItemInstanceId = command.newItemInstanceId;
            tx.equipmentSlotCode = command.equipmentSlotCode;
            tx.hadPrevious = command.hadPrevious;
            tx.previousInstanceId = command.previousInstanceId;
            tx.freedBagSlotIndex = command.freedBagSlotIndex;
            if (!InventoryRepository::RunEquipTransaction(m_database, tx)) {
                response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = "equip transaction failed"; return response;
            }
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::UnequipItemWrite: {
            WorldUnequipItem command;
            if (!DecodeWorldUnequipItem(request.payload.data(), request.payload.size(), command, error)) break;
            InventoryRepository::UnequipTransaction tx;
            tx.instanceId = command.instanceId;
            tx.equipmentSlotCode = command.equipmentSlotCode;
            tx.bagSlotIndex = command.bagSlotIndex;
            if (!InventoryRepository::RunUnequipTransaction(m_database, tx)) {
                response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = "unequip transaction failed"; return response;
            }
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::QuestInsertWrite: {
            WorldQuestInsert command;
            if (!DecodeWorldQuestInsert(request.payload.data(), request.payload.size(), command, error)) break;
            if (!QuestRepository::InsertQuest(m_database, command.characterId, command.questId,
                                              static_cast<std::int8_t>(command.state),
                                              command.acceptedAt, command.objectiveIds, error)) {
                response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = error; return response;
            }
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::QuestTurnInWrite: {
            WorldQuestTurnIn command;
            if (!DecodeWorldQuestTurnIn(request.payload.data(), request.payload.size(), command, error)) break;
            QuestRepository::TurnInTransaction tx;
            tx.characterId = command.characterId;
            tx.questId = command.questId;
            tx.turnedInAt = command.turnedInAt;
            tx.newLevel = command.newLevel;
            tx.newExperience = command.newExperience;
            tx.newGold = command.newGold;
            tx.rewardItemDefinitionId = command.rewardItemDefinitionId;
            tx.rewardItemQuantity = command.rewardItemQuantity;
            tx.rewardItemSlotIndex = command.rewardItemSlotIndex;
            tx.rewardItemCreatedAt = command.rewardItemCreatedAt;
            const auto result = QuestRepository::RunTurnInTransaction(m_database, tx, error);
            if (!result.ok) { response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = error; return response; }
            EncodeWorldQuestTurnInResult({true, result.itemInstanceId}, response.payload);
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::QuestAbandonWrite: {
            WorldQuestAbandon command;
            if (!DecodeWorldQuestAbandon(request.payload.data(), request.payload.size(), command, error)) break;
            if (!QuestRepository::MarkAbandoned(m_database, command.characterId, command.questId,
                                                command.nowUnix, error)) {
                response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = error; return response;
            }
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::QuestObjectiveWrite: {
            WorldQuestObjective command;
            if (!DecodeWorldQuestObjective(request.payload.data(), request.payload.size(), command, error)) break;
            if (!QuestRepository::UpdateObjectiveProgress(m_database, command.characterId,
                                                           command.questId, command.objectiveId,
                                                           command.progress, error)) {
                response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = error; return response;
            }
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::QuestStateWrite: {
            WorldQuestState command;
            if (!DecodeWorldQuestState(request.payload.data(), request.payload.size(), command, error)) break;
            if (!QuestRepository::UpdateQuestState(m_database, command.characterId,
                                                   command.questId,
                                                   static_cast<std::int8_t>(command.state),
                                                   command.timestamp, command.setTurnedInAt,
                                                   error)) {
                response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = error; return response;
            }
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::OfflineKillWrite: {
            WorldOfflineKill command;
            if (!DecodeWorldOfflineKill(request.payload.data(), request.payload.size(), command, error)) break;
            std::vector<QuestRepository::KillCandidate> candidates;
            candidates.reserve(command.candidates.size());
            for (const auto& candidate : command.candidates) {
                candidates.push_back({static_cast<legend::world::QuestId>(candidate.questId),
                                      candidate.objectiveId, candidate.requiredCount});
            }
            // 返回 false = 无 InProgress 任务可推进（合法，非错误）；仅 error 非空才是失败。
            const bool changed = QuestRepository::OfflineAdvanceKill(
                m_database, command.characterId, candidates, error);
            if (!error.empty()) {
                response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = error; return response;
            }
            (void)changed;
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::ShopBuyWrite: {
            WorldShopBuy command;
            if (!DecodeWorldShopBuy(request.payload.data(), request.payload.size(), command, error)) break;
            InventoryRepository::ShopBuyTransaction tx;
            tx.characterId = command.characterId;
            tx.newGold = command.newGold;
            tx.itemDefinitionId = command.itemDefinitionId;
            tx.quantity = command.quantity;
            tx.bagSlotIndex = command.bagSlotIndex;
            tx.mergedIntoStack = command.mergedIntoStack;
            tx.mergeInstanceId = command.mergeInstanceId;
            tx.mergeQuantity = command.mergeQuantity;
            std::uint64_t newInstanceId = 0;
            if (!InventoryRepository::RunShopBuyTransaction(m_database, tx, newInstanceId, error)) {
                response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = error; return response;
            }
            EncodeWorldShopBuyResult({newInstanceId}, response.payload);
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::ShopSellWrite: {
            WorldShopSell command;
            if (!DecodeWorldShopSell(request.payload.data(), request.payload.size(), command, error)) break;
            InventoryRepository::ShopSellTransaction tx;
            tx.characterId = command.characterId;
            tx.newGold = command.newGold;
            tx.instanceId = command.instanceId;
            tx.quantity = command.quantity;
            tx.wholeStack = command.wholeStack;
            if (!InventoryRepository::RunShopSellTransaction(m_database, tx, error)) {
                response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = error; return response;
            }
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::SaveGoldWrite: {
            WorldSaveGold command;
            if (!DecodeWorldSaveGold(request.payload.data(), request.payload.size(), command, error)) break;
            auto saved = legend::account::CharacterRepository::SaveGold(m_database,
                                                                        command.characterId,
                                                                        command.gold);
            if (!saved.success) { response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = saved.errorMessage; return response; }
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        case DbOperation::AddRewardsWrite: {
            WorldAddRewards command;
            if (!DecodeWorldAddRewards(request.payload.data(), request.payload.size(), command, error)) break;
            auto saved = legend::account::CharacterRepository::AddProgressionRewards(
                m_database, command.characterId, command.expDelta, command.goldDelta);
            if (!saved.success) { response.errorCode = InternalErrorCode::DatabaseUnavailable;
                response.message = saved.errorMessage; return response; }
            response.recordVersion = m_revision.fetch_add(1) + 1;
            response.errorCode = InternalErrorCode::Ok; return response;
        }
        default:
            response.errorCode = InternalErrorCode::InvalidRequest;
            response.message = "database operation not implemented";
            return response;
    }
    response.errorCode = InternalErrorCode::InvalidRequest;
    response.message = error.empty() ? "malformed operation payload" : error;
    return response;
}

void DbServer::Send(std::uint64_t connectionId, const Packet& packet) {
    legend::net::TcpConnectionPtr connection;
    { std::lock_guard<std::mutex> lock(m_mutex); const auto it = m_links.find(connectionId);
      if (it != m_links.end()) connection = it->second.connection; }
    if (connection) connection->Send(packet);
}

// Stage25.6 服务器管理台：Db 只读统计快照（GUI 线程每 500ms 调用）。
DbServer::DbStatsSnapshot DbServer::CollectStats() const {
    DbStatsSnapshot stats;
    stats.connectionCount = ConnectionCount();
    stats.dbOpen = m_database.IsOpen();
    stats.workerRunning = m_worker.IsRunning();
    stats.queries = m_queries.load(std::memory_order_relaxed);
    stats.writes = m_writes.load(std::memory_order_relaxed);
    stats.transactions = m_transactions.load(std::memory_order_relaxed);
    stats.failures = m_failures.load(std::memory_order_relaxed);
    stats.queueLength = m_worker.QueueLength();
    stats.schemaVersion = m_schemaVersion.load(std::memory_order_relaxed);
    stats.databasePath = m_config.databasePath;
    if (m_server) {
        stats.packetsReceived = m_server->PacketsReceived();
        stats.packetsSent = m_server->PacketsSent();
    }
    return stats;
}

} // namespace legend::db
