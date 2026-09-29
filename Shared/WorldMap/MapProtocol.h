#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::network {
class ByteWriter;
class ByteReader;
} // namespace legend::network

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段21 指令二十六/二十七：地图切换协议。
// MessageId：MapChanged=344 / MapSnapshot=345。
// 两个包都只发本人（切换/进入世界的玩家）。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令五十七）。
// ---------------------------------------------------------------------------

// MapChanged(344)（指令二十六）：切图完成通知（只发本人）。
struct MapChangedPayload {
    std::uint16_t mapId = 1;
    std::string mapName;
    float x = 0.0f;
    float y = 0.0f;
    std::uint64_t serverTime = 0;
};

// MapSnapshot(345)（指令二十七）：进入新地图的地图基础信息（只发本人）。
struct MapSnapshotPayload {
    std::uint16_t mapId = 1;
    std::string mapName;
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 2000.0f;
    float maxY = 2000.0f;
    float spawnX = 300.0f;
    float spawnY = 300.0f;
    float respawnX = 300.0f;
    float respawnY = 300.0f;
    std::uint64_t serverTime = 0;
};

bool EncodeMapChanged(const MapChangedPayload& p, std::vector<std::uint8_t>& out);
bool DecodeMapChanged(const std::uint8_t* data, std::size_t size, MapChangedPayload& out,
                      std::string& error);
bool EncodeMapSnapshot(const MapSnapshotPayload& p, std::vector<std::uint8_t>& out);
bool DecodeMapSnapshot(const std::uint8_t* data, std::size_t size, MapSnapshotPayload& out,
                       std::string& error);

} // namespace legend::world
