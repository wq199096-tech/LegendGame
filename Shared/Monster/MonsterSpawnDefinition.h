#pragma once

#include "Shared/Monster/MonsterTypes.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段22 指令 22.7：MonsterSpawnDefinition —— 刷怪区静态定义（JSON 数据驱动）。
// 画布使用圆形刷怪范围（centerX/centerY/radius）；服务器按确定性算法展开布点，
// Editor/Server/Tests 共用同一展开结果（22.11：核心逻辑服务器掌控，JSON 只是配置）。
// ---------------------------------------------------------------------------
struct MonsterSpawnDefinition {
    std::uint32_t spawnId = 0;
    std::uint16_t mapId = 1;
    std::uint32_t monsterDefinitionId = kTrainingSlimeTypeId;
    float centerX = 0.0f;
    float centerY = 0.0f;
    float radius = 0.0f;              // 0 = 全部退化为 center 单点
    std::uint32_t count = 0;
    std::uint32_t respawnSeconds = 8; // 阶段17：Training Slime = 8 秒（respawnDelayMs=8000）
    bool enabled = true;
};

// 确定性布点展开：spawnId 派生种子的 xorshift64* 在圆内均匀生成 count 个点。
// 同一 spawnId 永远得到同一组点（服务器重启/编辑器预览/测试三方一致）。
inline std::vector<MonsterSpawnPoint> GenerateSpawnPoints(const MonsterSpawnDefinition& spawn) {
    std::vector<MonsterSpawnPoint> points;
    points.reserve(spawn.count);
    std::uint64_t state = 0x9E3779B97F4A7C15ull ^
                          (static_cast<std::uint64_t>(spawn.spawnId) * 0xBF58476D1CE4E5B9ull + 1);
    auto next01 = [&state]() {
        state ^= state >> 12;
        state ^= state << 25;
        state ^= state >> 27;
        return static_cast<double>((state * 0x2545F4914F6CDD1Dull) >> 11) /
               9007199254740992.0; // [0,1)
    };
    for (std::uint32_t i = 0; i < spawn.count; ++i) {
        if (spawn.radius <= 0.0f) {
            points.push_back({spawn.centerX, spawn.centerY});
            continue;
        }
        const double r = std::sqrt(next01()) * static_cast<double>(spawn.radius);
        const double theta = next01() * 6.283185307179586;
        points.push_back({static_cast<float>(spawn.centerX + r * std::cos(theta)),
                          static_cast<float>(spawn.centerY + r * std::sin(theta))});
    }
    return points;
}

} // namespace legend::world
