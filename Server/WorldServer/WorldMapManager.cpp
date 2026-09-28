#include "Server/WorldServer/WorldMapManager.h"

#include <cmath>

namespace legend::world {

bool WorldMapManager::AddPlayer(const std::shared_ptr<PlayerSession>& player) {
    if (!player) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    auto& mapPlayers = m_maps[player->MapId()];
    return mapPlayers.emplace(player->ConnectionId(), player).second;
}

bool WorldMapManager::RemovePlayer(std::uint64_t connectionId, std::uint16_t mapId) {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_maps.find(mapId);
    if (it == m_maps.end()) {
        return false;
    }
    if (it->second.erase(connectionId) == 0) {
        return false;
    }
    if (it->second.empty()) {
        m_maps.erase(it);
    }
    return true;
}

std::size_t WorldMapManager::PlayerCount(std::uint16_t mapId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_maps.find(mapId);
    return it != m_maps.end() ? it->second.size() : 0;
}

std::vector<std::shared_ptr<PlayerSession>> WorldMapManager::Players(std::uint16_t mapId) const {
    std::lock_guard<std::mutex> lock(m_mutex);
    std::vector<std::shared_ptr<PlayerSession>> players;
    const auto it = m_maps.find(mapId);
    if (it != m_maps.end()) {
        players.reserve(it->second.size());
        for (auto& [id, player] : it->second) {
            players.push_back(player);
        }
    }
    return players;
}

bool WorldMapManager::ApplyMoveInput(PlayerSession& player, std::uint32_t inputSequence,
                                     float directionX, float directionY, float deltaTime) {
    // 指令四十一：sequence 必须单调增加；重复/倒退忽略（不断线）。
    if (inputSequence <= player.LastProcessedInputSequence()) {
        return false;
    }
    // 指令三十八：deltaTime Clamp [0, 0.1]，防伪造大 dt 瞬移。
    float dt = deltaTime;
    if (!(dt > 0.0f)) {
        dt = 0.0f; // NaN/负值 -> 0
    }
    if (dt > kMaxMoveDeltaTime) {
        dt = kMaxMoveDeltaTime;
    }
    float dx = directionX;
    float dy = directionY;
    if (!std::isfinite(dx)) {
        dx = 0.0f;
    }
    if (!std::isfinite(dy)) {
        dy = 0.0f;
    }
    // 指令三十九：方向长度 >1 必须 Normalize（对角线不能更快）。
    const float lengthSq = dx * dx + dy * dy;
    if (lengthSq > 1.0f) {
        const float invLength = 1.0f / std::sqrt(lengthSq);
        dx *= invLength;
        dy *= invLength;
    }
    // 指令三十六：position += direction * speed * deltaTime。
    float x = player.PositionX() + dx * kWorldMoveSpeed * dt;
    float y = player.PositionY() + dy * kWorldMoveSpeed * dt;
    // 指令四十：地图边界服务器 Clamp。
    if (!(x >= kMapMinX)) {
        x = kMapMinX;
    } else if (x > kMapMaxX) {
        x = kMapMaxX;
    }
    if (!(y >= kMapMinY)) {
        y = kMapMinY;
    } else if (y > kMapMaxY) {
        y = kMapMaxY;
    }
    player.SetPosition(x, y);
    player.SetLastProcessedInputSequence(inputSequence);
    player.SetPositionDirty(true);
    player.TouchMoveTime();
    return true;
}

} // namespace legend::world
