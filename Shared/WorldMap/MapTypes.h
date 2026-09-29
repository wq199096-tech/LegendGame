#pragma once

#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段21 指令三/六：MapType（服务器权威逻辑地图类型）。
// 阶段21 只使用 Town / Field；Dungeon 预留枚举。
// ---------------------------------------------------------------------------
enum class MapType : std::uint8_t {
    Town = 1,
    Field = 2,
    Dungeon = 3, // 预留
};

inline const char* MapTypeName(MapType type) {
    switch (type) {
        case MapType::Town: return "Town";
        case MapType::Field: return "Field";
        case MapType::Dungeon: return "Dungeon";
    }
    return "Unknown";
}

// 指令三：Town 固定 Map1（主城复活点）。
inline constexpr std::uint16_t kTownMapId = 1;

// 指令三十七：死亡后至少 3 秒才允许复活（服务器判断；Client 倒计时只是显示）。
inline constexpr double kRespawnMinDelaySeconds = 3.0;

// 指令三十八：复活费用（服务器权威）。CurrentMap 10 Gold；Town 免费。
inline constexpr std::uint32_t kRespawnCurrentMapGoldCost = 10;
inline constexpr std::uint32_t kRespawnTownGoldCost = 0;

// 指令四十五/四十六：复活保护 3 秒（PlayerSession runtime flag；
// 期间玩家不能受到 Monster 伤害；主动攻击/施法立即取消）。
inline constexpr double kRespawnProtectionSeconds = 3.0;

} // namespace legend::world
