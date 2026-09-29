#pragma once

#include <cstdint>
#include <memory>

namespace legend::world {

class PlayerSession;
class WorldServer;

// ---------------------------------------------------------------------------
// 阶段21 指令二十三/二十四/五十三：MapTransitionService —— 统一地图切换。
// Portal / NPC Teleport / Town 复活 / 以后所有地图切换的唯一执行路径
//（不允许第二套切换逻辑）。
// 前置：调用方已完成全部业务验证（Alive/等级/Gold；扣费在内存完成并落库）。
// 本服务负责机械序列（指令二十四）：
//   ValidateDestination -> CancelCurrentActions -> RemoveFromOldMap ->
//   BroadcastOldDespawn -> ClearVisibility -> UpdateAuthoritativeMapPosition ->
//   UpdateSpatialGrid -> EnterNewMap -> RebuildAOI -> SendMapChanged/MapSnapshot ->
//   SendImmediatePositionSnapshot -> PersistPosition。
// ---------------------------------------------------------------------------
class MapTransitionService {
public:
    explicit MapTransitionService(WorldServer& server) : m_server(server) {}

    // 执行统一切换。目的地地图不存在/坐标越界 -> 返回 false 且不产生任何状态变化。
    // 同图切换（destMap == 当前图）同样走完整序列（复活 CurrentMap 语义：AOI 重建）。
    // 完成后调用 QuestService::OnPlayerMoved（指令五十一：ReachArea 可被传送触发）。
    bool TransitionPlayer(const std::shared_ptr<PlayerSession>& player,
                          std::uint16_t destinationMapId, float destinationX, float destinationY,
                          const char* reasonTag);

private:
    WorldServer& m_server;
};

} // namespace legend::world
