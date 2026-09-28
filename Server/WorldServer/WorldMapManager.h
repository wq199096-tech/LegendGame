#pragma once

#include "Server/WorldServer/PlayerSession.h"

#include "Shared/World/WorldTypes.h"

#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace legend::world {

// 阶段11 指令三十一/三十二/三十四~四十：WorldMapManager。
// mapId -> players；阶段11 仅 mapId=1（无副本/分线）。
// 权威移动：Normalize direction -> speed(120) -> Clamp deltaTime(<=0.1) ->
// position += dir*speed*dt -> Clamp 边界(0~2000)。
class WorldMapManager {
public:
    // 玩家加入地图（mapId 不支持时内部回退到默认地图，由调用方先行 Sanitize）。
    bool AddPlayer(const std::shared_ptr<PlayerSession>& player);
    bool RemovePlayer(std::uint64_t connectionId, std::uint16_t mapId);
    std::size_t PlayerCount(std::uint16_t mapId) const;
    std::vector<std::shared_ptr<PlayerSession>> Players(std::uint16_t mapId) const;

    // 阶段11 指令三十四~四十：应用一次移动输入（返回是否被接受）。
    // sequence 重复/倒退：忽略（指令四十一，不断线）。
    static bool ApplyMoveInput(PlayerSession& player, std::uint32_t inputSequence,
                               float directionX, float directionY, float deltaTime);

private:
    mutable std::mutex m_mutex;
    std::map<std::uint16_t, std::map<std::uint64_t, std::shared_ptr<PlayerSession>>> m_maps;
};

} // namespace legend::world
