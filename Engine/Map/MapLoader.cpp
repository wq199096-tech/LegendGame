#include "Engine/Map/MapLoader.h"

#include <nlohmann/json.hpp>

#include <fstream>
#include <sstream>

#include "Engine/Debug/Logger.h"

namespace legend::map {

namespace {

using json = nlohmann::json;

constexpr int kSupportedVersion = 1;
constexpr int kMaxMapDimension = 1024;

std::unique_ptr<TileLayer> ParseTileLayer(const json& layerJson, int width, int height,
                                          const std::string& name, bool& ok) {
    auto layer = std::make_unique<TileLayer>();
    if (!layerJson.contains("data") || !layerJson["data"].is_array()) {
        LOG_ERROR("Map tile layer '" + name + "' is missing required field 'data' (array).");
        ok = false;
        return layer;
    }
    const json& data = layerJson["data"];
    if (static_cast<int>(data.size()) != width * height) {
        LOG_ERROR("Map tile layer '" + name + "' data size mismatch: expected " +
                  std::to_string(static_cast<long long>(width) * height) + ", got " +
                  std::to_string(data.size()) + ".");
        ok = false;
        return layer;
    }
    std::vector<uint16_t> tiles;
    tiles.reserve(data.size());
    for (const auto& value : data) {
        tiles.push_back(static_cast<uint16_t>(value.get<int>() & 0xFFFF));
    }
    layer->SetData(std::move(tiles), width, height);
    if (layerJson.contains("visible")) {
        layer->SetVisible(layerJson["visible"].get<bool>());
    }
    return layer;
}

std::unique_ptr<CollisionLayer> ParseCollisionLayer(const json& layerJson, int width, int height,
                                                    const std::string& name, bool& ok) {
    auto layer = std::make_unique<CollisionLayer>();
    if (!layerJson.contains("data") || !layerJson["data"].is_array()) {
        LOG_ERROR("Map collision layer '" + name + "' is missing required field 'data' (array).");
        ok = false;
        return layer;
    }
    const json& data = layerJson["data"];
    if (static_cast<int>(data.size()) != width * height) {
        LOG_ERROR("Map collision layer '" + name + "' data size mismatch: expected " +
                  std::to_string(static_cast<long long>(width) * height) + ", got " +
                  std::to_string(data.size()) + ".");
        ok = false;
        return layer;
    }
    std::vector<uint8_t> tiles;
    tiles.reserve(data.size());
    for (const auto& value : data) {
        tiles.push_back(value.get<int>() != 0 ? 1 : 0);
    }
    layer->SetData(std::move(tiles), width, height);
    if (layerJson.contains("visible")) {
        layer->SetVisible(layerJson["visible"].get<bool>());
    }
    return layer;
}

std::unique_ptr<ObjectLayer> ParseObjectLayer(const json& layerJson, const std::string& name) {
    auto layer = std::make_unique<ObjectLayer>();
    if (layerJson.contains("visible")) {
        layer->SetVisible(layerJson["visible"].get<bool>());
    }
    if (!layerJson.contains("objects") || !layerJson["objects"].is_array()) {
        return layer; // 空物件层合法
    }
    int skipped = 0;
    for (const auto& objectJson : layerJson["objects"]) {
        if (!objectJson.is_object()) {
            ++skipped;
            continue;
        }
        MapObject object;
        // 必需字段缺失：记录错误并跳过该物件，不中断整张地图加载
        bool valid = true;
        if (objectJson.contains("id")) {
            object.id = objectJson["id"].get<uint32_t>();
        } else {
            LOG_ERROR("Map object layer '" + name + "': object missing 'id', skipped.");
            valid = false;
        }
        if (objectJson.contains("name")) {
            object.name = objectJson["name"].get<std::string>();
        } else {
            LOG_ERROR("Map object layer '" + name + "': object " +
                      std::to_string(object.id) + " missing 'name', skipped.");
            valid = false;
        }
        if (objectJson.contains("textureId")) {
            object.textureId = objectJson["textureId"].get<std::string>();
        }
        if (objectJson.contains("x") && objectJson.contains("y")) {
            object.x = objectJson["x"].get<float>();
            object.y = objectJson["y"].get<float>();
        } else {
            LOG_ERROR("Map object layer '" + name + "': object '" + object.name +
                      "' missing 'x'/'y', skipped.");
            valid = false;
        }
        if (!valid) {
            ++skipped;
            continue;
        }
        object.width = objectJson.value("width", 64.0f);
        object.height = objectJson.value("height", 64.0f);
        object.rotationDegrees = objectJson.value("rotation", 0.0f);
        object.renderOrder = objectJson.value("renderOrder", 0);
        object.sortLayer = objectJson.value("sortLayer", 0);
        object.blocking = objectJson.value("blocking", false);
        object.occluder = objectJson.value("occluder", false);
        layer->AddObject(std::move(object));
    }
    if (skipped > 0) {
        LOG_WARN("Map object layer '" + name + "': skipped " + std::to_string(skipped) +
                 " invalid object(s).");
    }
    return layer;
}

std::unique_ptr<OcclusionLayer> ParseOcclusionLayer(const json& layerJson) {
    auto layer = std::make_unique<OcclusionLayer>();
    if (layerJson.contains("visible")) {
        layer->SetVisible(layerJson["visible"].get<bool>());
    }
    if (layerJson.contains("objects") && layerJson["objects"].is_array()) {
        std::vector<uint32_t> ids;
        for (const auto& value : layerJson["objects"]) {
            ids.push_back(value.get<uint32_t>());
        }
        layer->SetOccluderIds(std::move(ids));
    }
    return layer;
}

} // namespace

std::shared_ptr<Map> MapLoader::Load(const std::string& filePath) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_ERROR("MapLoader: cannot open map file: " + filePath);
        return nullptr;
    }

    json root;
    try {
        file >> root;
    } catch (const json::parse_error& error) {
        LOG_ERROR("MapLoader: JSON parse error in '" + filePath + "': " + error.what());
        return nullptr;
    }

    if (!root.is_object()) {
        LOG_ERROR("MapLoader: map root must be a JSON object: " + filePath);
        return nullptr;
    }

    // 版本检查
    if (!root.contains("version")) {
        LOG_ERROR("MapLoader: map file missing 'version': " + filePath);
        return nullptr;
    }
    const int version = root["version"].get<int>();
    if (version != kSupportedVersion) {
        LOG_ERROR("Unsupported map version: " + std::to_string(version) +
                  " (supported: " + std::to_string(kSupportedVersion) + ") in " + filePath);
        return nullptr;
    }

    // 基础字段
    if (!root.contains("width") || !root.contains("height") || !root.contains("tileSize")) {
        LOG_ERROR("MapLoader: map file must contain 'width', 'height' and 'tileSize': " + filePath);
        return nullptr;
    }
    const int width = root["width"].get<int>();
    const int height = root["height"].get<int>();
    const int tileSize = root["tileSize"].get<int>();
    if (width <= 0 || height <= 0 || tileSize <= 0 ||
        width > kMaxMapDimension || height > kMaxMapDimension) {
        LOG_ERROR("MapLoader: invalid map dimensions " + std::to_string(width) + "x" +
                  std::to_string(height) + " tile " + std::to_string(tileSize) + ".");
        return nullptr;
    }
    if (!root.contains("layers") || !root["layers"].is_array()) {
        LOG_ERROR("MapLoader: map file missing 'layers' array: " + filePath);
        return nullptr;
    }

    auto map = std::make_shared<Map>();
    map->SetName(root.value("name", "Unnamed"));
    map->SetSize(width, height, tileSize);

    bool hasTileLayer = false;
    bool loadFailed = false;

    for (const auto& layerJson : root["layers"]) {
        if (!layerJson.is_object() || !layerJson.contains("type")) {
            LOG_WARN("MapLoader: layer without 'type' skipped.");
            continue;
        }
        const std::string type = layerJson["type"].get<std::string>();
        const std::string layerName = layerJson.value("name", "unnamed");

        if (type == "tile") {
            bool ok = true;
            auto layer = ParseTileLayer(layerJson, width, height, layerName, ok);
            if (!ok) {
                loadFailed = true;
                break;
            }
            map->GetGround().SetVisible(layer->IsVisible());
            map->GetGround().SetData(layer->Data().empty()
                                         ? std::vector<uint16_t>{}
                                         : std::vector<uint16_t>(layer->Data()),
                                     width, height);
            hasTileLayer = true;
        } else if (type == "collision") {
            bool ok = true;
            auto layer = ParseCollisionLayer(layerJson, width, height, layerName, ok);
            if (!ok) {
                loadFailed = true;
                break;
            }
            map->GetCollision().SetData(std::vector<uint8_t>(layer->Data()), width, height);
            map->GetCollision().SetVisible(layer->IsVisible());
        } else if (type == "object") {
            auto layer = ParseObjectLayer(layerJson, layerName);
            map->GetObjects().SetVisible(layer->IsVisible());
            for (const auto& object : layer->Objects()) {
                map->GetObjects().AddObject(object);
            }
        } else if (type == "occlusion") {
            auto layer = ParseOcclusionLayer(layerJson);
            map->GetOcclusion().SetVisible(layer->IsVisible());
            map->GetOcclusion().SetOccluderIds(layer->GetOccluderIds());
        } else {
            LOG_WARN("MapLoader: unknown layer type '" + type + "', skipped.");
        }
    }

    if (loadFailed) {
        return nullptr;
    }
    if (!hasTileLayer) {
        LOG_ERROR("MapLoader: map has no tile layer: " + filePath);
        return nullptr;
    }

    // 使用物件层 occluder 标记重建遮挡层（保证编辑器保存/加载一致）
    map->GetOcclusion().SetOccluderIds({});
    for (const auto& object : map->GetObjects().Objects()) {
        if (object.occluder) {
            map->GetOcclusion().AddOccluder(object.id);
        }
    }

    // 职责边界：Ground -> Ground，Collision -> Manual，Objects -> Objects，Occlusion -> Occlusion。
    // Terrain（Water）与 Object 碰撞由 Map::IsTileBlocked 运行时派生，
    // 不写入也不清洗 Manual 数据；这里仅重建 Object 碰撞引用计数。
    map->RebuildObjectBlockCounts();

    LOG_INFO("Map loaded: '" + map->GetName() + "' (" + filePath + "), " +
             std::to_string(width) + "x" + std::to_string(height) + " tiles, " +
             std::to_string(map->GetObjects().Objects().size()) + " objects.");
    return map;
}

bool MapLoader::Save(const Map& map, const std::string& filePath) {
    json root;
    root["version"] = kSupportedVersion;
    root["name"] = map.GetName();
    root["tileSize"] = map.GetTileSize();
    root["width"] = map.GetWidth();
    root["height"] = map.GetHeight();

    json layers = json::array();

    // Ground (tile)
    json groundLayer;
    groundLayer["name"] = "Ground";
    groundLayer["type"] = "tile";
    groundLayer["visible"] = map.GetGround().IsVisible();
    json groundData = json::array();
    for (const uint16_t tile : map.GetGround().Data()) {
        groundData.push_back(tile);
    }
    groundLayer["data"] = std::move(groundData);
    layers.push_back(std::move(groundLayer));

    // Objects
    json objectLayer;
    objectLayer["name"] = "Objects";
    objectLayer["type"] = "object";
    objectLayer["visible"] = map.GetObjects().IsVisible();
    json objects = json::array();
    for (const auto& object : map.GetObjects().Objects()) {
        json objectJson;
        objectJson["id"] = object.id;
        objectJson["name"] = object.name;
        objectJson["textureId"] = object.textureId;
        objectJson["x"] = object.x;
        objectJson["y"] = object.y;
        objectJson["width"] = object.width;
        objectJson["height"] = object.height;
        objectJson["rotation"] = object.rotationDegrees;
        objectJson["renderOrder"] = object.renderOrder;
        objectJson["sortLayer"] = object.sortLayer;
        objectJson["blocking"] = object.blocking;
        objectJson["occluder"] = object.occluder;
        objects.push_back(std::move(objectJson));
    }
    objectLayer["objects"] = std::move(objects);
    layers.push_back(std::move(objectLayer));

    // Collision
    json collisionLayer;
    collisionLayer["name"] = "Collision";
    collisionLayer["type"] = "collision";
    collisionLayer["visible"] = map.GetCollision().IsVisible();
    json collisionData = json::array();
    for (const uint8_t tile : map.GetCollision().Data()) {
        collisionData.push_back(tile);
    }
    collisionLayer["data"] = std::move(collisionData);
    layers.push_back(std::move(collisionLayer));

    // Occlusion
    json occlusionLayer;
    occlusionLayer["name"] = "Occlusion";
    occlusionLayer["type"] = "occlusion";
    occlusionLayer["visible"] = map.GetOcclusion().IsVisible();
    json occlusionObjects = json::array();
    for (const uint32_t id : map.GetOcclusion().GetOccluderIds()) {
        occlusionObjects.push_back(id);
    }
    occlusionLayer["objects"] = std::move(occlusionObjects);
    layers.push_back(std::move(occlusionLayer));

    root["layers"] = std::move(layers);

    std::ofstream file(filePath, std::ios::trunc);
    if (!file.is_open()) {
        LOG_ERROR("MapLoader: cannot open file for writing: " + filePath);
        return false;
    }
    file << root.dump();
    if (!file.good()) {
        LOG_ERROR("MapLoader: failed to write map file: " + filePath);
        return false;
    }
    LOG_INFO("Map saved: '" + map.GetName() + "' -> " + filePath);
    return true;
}

} // namespace legend::map
