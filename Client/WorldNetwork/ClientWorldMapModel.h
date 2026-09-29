#pragma once

#include "Shared/WorldMap/MapProtocol.h"

#include <cstdint>
#include <string>

namespace legend::client {

// ---------------------------------------------------------------------------
// 阶段21 指令五十四：ClientWorldMapModel —— 当前地图镜像（currentMapId/mapName/
// bounds）。收到 MapSnapshot/MapChanged 后更新；服务器仍是权威 mapId。
// ---------------------------------------------------------------------------
class ClientWorldMapModel {
public:
    void Clear() {
        m_currentMapId = 1;
        m_currentMapName.clear();
        m_hasSnapshot = false;
    }

    // MapSnapshot(345)：进入世界的地图基础信息。
    void ApplySnapshot(const world::MapSnapshotPayload& payload) {
        m_currentMapId = payload.mapId;
        m_currentMapName = payload.mapName;
        m_minX = payload.minX;
        m_minY = payload.minY;
        m_maxX = payload.maxX;
        m_maxY = payload.maxY;
        m_spawnX = payload.spawnX;
        m_spawnY = payload.spawnY;
        m_respawnX = payload.respawnX;
        m_respawnY = payload.respawnY;
        m_hasSnapshot = true;
    }

    // MapChanged(344)：切换完成（边界沿用 Snapshot——地图静态定义不变）。
    void ApplyChanged(const world::MapChangedPayload& payload) {
        m_currentMapId = payload.mapId;
        m_currentMapName = payload.mapName;
    }

    std::uint16_t CurrentMapId() const { return m_currentMapId; }
    const std::string& CurrentMapName() const { return m_currentMapName; }
    bool HasSnapshot() const { return m_hasSnapshot; }
    float MinX() const { return m_minX; }
    float MinY() const { return m_minY; }
    float MaxX() const { return m_maxX; }
    float MaxY() const { return m_maxY; }
    float RespawnX() const { return m_respawnX; }
    float RespawnY() const { return m_respawnY; }

    // F9 Map Debug Panel 文本（指令一百一十二）。
    std::string DebugText() const {
        std::string text = "[F9 Map] map=" + std::to_string(m_currentMapId) + " (" +
                           m_currentMapName + ")";
        if (m_hasSnapshot) {
            text += " bounds=(" + std::to_string(m_minX) + "," + std::to_string(m_minY) + ")~(" +
                    std::to_string(m_maxX) + "," + std::to_string(m_maxY) + ")";
        }
        text += "\n";
        return text;
    }

private:
    std::uint16_t m_currentMapId = 1;
    std::string m_currentMapName;
    bool m_hasSnapshot = false;
    float m_minX = 0.0f;
    float m_minY = 0.0f;
    float m_maxX = 2000.0f;
    float m_maxY = 2000.0f;
    float m_spawnX = 300.0f;
    float m_spawnY = 300.0f;
    float m_respawnX = 300.0f;
    float m_respawnY = 300.0f;
};

} // namespace legend::client
