#include "Engine/Entity/Direction8.h"

#include <cmath>

namespace legend::entity {

const char* Direction8Name(Direction8 direction) {
    switch (direction) {
        case Direction8::South:     return "south";
        case Direction8::SouthWest: return "southwest";
        case Direction8::West:      return "west";
        case Direction8::NorthWest: return "northwest";
        case Direction8::North:     return "north";
        case Direction8::NorthEast: return "northeast";
        case Direction8::East:      return "east";
        case Direction8::SouthEast: return "southeast";
    }
    return "south";
}

Direction8 DirectionFromVector(const math::Vector2& vector, Direction8 fallback) {
    if (vector.x == 0.0f && vector.y == 0.0f) {
        return fallback; // 无输入：保留上一次朝向
    }
    // y 轴向下坐标系：atan2(y,x) 0=E 90=S 180=W -90=N
    const float angle = std::atan2(vector.y, vector.x) * 57.29577951308232f; // 180/pi
    // 45 度扇区：0=E 1=SE 2=S 3=SW 4=W 5=NW 6=N 7=NE
    int sector = static_cast<int>(std::lround(angle / 45.0f));
    sector = ((sector % 8) + 8) % 8;
    static const Direction8 table[8] = {
        Direction8::East, Direction8::SouthEast, Direction8::South, Direction8::SouthWest,
        Direction8::West, Direction8::NorthWest, Direction8::North, Direction8::NorthEast,
    };
    return table[sector];
}

} // namespace legend::entity
