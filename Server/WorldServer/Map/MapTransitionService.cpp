#include "Server/WorldServer/Map/MapTransitionService.h"

#include "Engine/Debug/Logger.h"
#include "Server/WorldServer/Map/MapRegistry.h"
#include "Server/WorldServer/PlayerSession.h"
#include "Server/WorldServer/WorldServer.h"
#include "Shared/Network/MessageId.h"
#include "Shared/Network/PacketCodec.h"
#include "Shared/Skill/SkillTypes.h"
#include "Shared/World/WorldProtocol.h"
#include "Shared/World/WorldTypes.h"
#include "Shared/WorldMap/MapProtocol.h"

#include <chrono>
#include <memory>

namespace legend::world {

namespace {
using legend::network::MessageId;
using legend::network::Packet;

std::uint64_t TransitionTimeMs() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

} // namespace

// 统一地图切换（指令二十四全序列）。WorldServer 声明本类为 friend。
bool MapTransitionService::TransitionPlayer(const std::shared_ptr<PlayerSession>& player,
                                            std::uint16_t destinationMapId, float destinationX,
                                            float destinationY, const char* reasonTag) {
    WorldServer& s = m_server;
    if (!player) {
        return false;
    }
    // 0) 并发保护（指令六十）：切图事务进行中拒绝第二次切换。
    if (player->IsMapTransitionInProgress()) {
        LOG_WARN("[Map] transition already in progress char=" + player->CharacterName());
        return false;
    }
    // 1) ValidateDestination（指令二十四）：地图存在 + 目的地在边界内。
    const MapDefinition* destination = MapRegistry::Instance().FindMap(destinationMapId);
    if (destination == nullptr) {
        LOG_WARN("[Map] transition rejected (map missing) map=" + std::to_string(destinationMapId));
        return false;
    }
    float destX = destinationX;
    float destY = destinationY;
    if (!destination->InBounds(destX, destY)) {
        LOG_WARN("[Map] transition rejected (out of bounds) map=" +
                 std::to_string(destinationMapId) + " dest=" + std::to_string(destX) + "," +
                 std::to_string(destY));
        return false;
    }
    player->SetMapTransitionInProgress(true);

    // 2) CancelCurrentActions（指令二十五）：Cast / Dialogue / Shop / 攻击目标。
    //    （阶段21：Status Buff/Debuff 不清除——只有复活清空状态容器。）
    if (player->IsCasting()) {
        s.CancelActiveCast(player, SkillCancelReason::Teleported);
    }
    s.CloseNpcSessions(player);

    const std::uint16_t oldMapId = player->MapId();
    // 3) RemoveFromOldMap。
    s.m_mapManager.RemovePlayer(player->ConnectionId(), oldMapId);
    // 4) BroadcastOldDespawn（指令六十六：旧地图观察者收到 ChangedMap）。
    s.NotifyPlayerGoneToObservers(player->CharacterId(), PlayerDespawnReason::ChangedMap);
    // 指令四十八/四十九：追击该玩家的原地图怪物立刻失去目标 Returning（不跨图追踪）。
    s.OnTargetPlayerRemoved(player->CharacterId());
    // 5) ClearVisibility（指令八十一：五类可见集合全清——含阶段21 Portals）。
    player->ClearVisiblePlayers();
    player->ClearVisibleMonsters();
    player->ClearVisibleNpcs();
    player->ClearVisibleItemDrops();
    player->ClearVisiblePortals();
    // 6) UpdateAuthoritativeMapPosition（服务器权威，绕过普通移动限制）。
    player->SetMapId(destination->mapId);
    player->SetPosition(destX, destY);
    player->SetPositionDirty(true);
    // 7) UpdateSpatialGrid。
    s.m_spatialGrid.UpdatePlayerCell(player);
    // 8) EnterNewMap。
    s.m_mapManager.AddPlayer(player);
    // 9) RebuildAOI（Player/Monster/NPC/Drop/Portal 初始可见性）。
    s.InitializePlayerVisibility(player);
    // 10) SendMapChanged + MapSnapshot（只发本人，指令二十六/二十七）。
    {
        MapChangedPayload changed;
        changed.mapId = destination->mapId;
        changed.mapName = destination->name;
        changed.x = player->PositionX();
        changed.y = player->PositionY();
        changed.serverTime = TransitionTimeMs();
        Packet packet;
        packet.header.messageId = static_cast<std::uint16_t>(MessageId::MapChanged);
        if (EncodeMapChanged(changed, packet.payload)) {
            s.SendPacketToPlayer(player, packet);
        }
        MapSnapshotPayload snapshot;
        snapshot.mapId = destination->mapId;
        snapshot.mapName = destination->name;
        snapshot.minX = destination->minX;
        snapshot.minY = destination->minY;
        snapshot.maxX = destination->maxX;
        snapshot.maxY = destination->maxY;
        snapshot.spawnX = destination->spawnX;
        snapshot.spawnY = destination->spawnY;
        snapshot.respawnX = destination->respawnX;
        snapshot.respawnY = destination->respawnY;
        snapshot.serverTime = TransitionTimeMs();
        Packet snapPacket;
        snapPacket.header.messageId = static_cast<std::uint16_t>(MessageId::MapSnapshot);
        if (EncodeMapSnapshot(snapshot, snapPacket.payload)) {
            s.SendPacketToPlayer(player, snapPacket);
        }
    }
    // 11) SendImmediatePositionSnapshot（不等 100ms 周期快照）。
    {
        PlayerPositionSnapshotPayload pos;
        pos.characterId = player->CharacterId();
        pos.positionX = player->PositionX();
        pos.positionY = player->PositionY();
        pos.lastProcessedInputSequence = player->LastProcessedInputSequence();
        pos.serverTime = TransitionTimeMs();
        Packet posPacket;
        posPacket.header.messageId =
            static_cast<std::uint16_t>(MessageId::PlayerPositionSnapshot);
        if (EncodePlayerPositionSnapshot(pos, posPacket.payload)) {
            s.SendPacketToPlayer(player, posPacket);
        }
    }
    // 12) PersistPosition（指令二十八/六十一/六十二：立即保存 + dirty 供周期重试兜底）。
    s.SavePlayerPositionNow(player->CharacterId(), player->MapId(), player->PositionX(),
                            player->PositionY());
    player->SetMapTransitionInProgress(false);
    LOG_INFO("[Map] transition char=" + player->CharacterName() + " reason=" + reasonTag +
             " map " + std::to_string(oldMapId) + " -> " + std::to_string(destination->mapId) +
             " (" + std::to_string(player->PositionX()) + "," + std::to_string(player->PositionY()) +
             ")");
    // 指令五十一：地图切换完成位置设置后调用 QuestService::OnPlayerMoved
    //（Portal/NPC Teleport 落点可触发 ReachArea）。
    s.HandleQuestPlayerMoved(player);
    return true;
}

} // namespace legend::world
