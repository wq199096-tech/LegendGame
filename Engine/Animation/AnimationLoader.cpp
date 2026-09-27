#include "Engine/Animation/AnimationLoader.h"

#include <nlohmann/json.hpp>

#include <fstream>

#include "Engine/Debug/Logger.h"
#include "Engine/Resource/ResourceManager.h"

namespace legend::animation {

namespace {
using json = nlohmann::json;
constexpr int kSupportedVersion = 1;
} // namespace

bool LoadCharacterDefinition(const std::string& filePath, CharacterDefinition& out) {
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_ERROR("AnimationLoader: cannot open character definition: " + filePath);
        return false;
    }
    json root;
    try {
        file >> root;
    } catch (const json::parse_error& error) {
        LOG_ERROR("AnimationLoader: JSON parse error in '" + filePath + "': " + error.what());
        return false;
    }
    if (!root.is_object()) {
        LOG_ERROR("AnimationLoader: character definition root must be an object.");
        return false;
    }
    if (!root.contains("version") || root["version"].get<int>() != kSupportedVersion) {
        LOG_ERROR("AnimationLoader: unsupported character definition version in " + filePath +
                  " (expected " + std::to_string(kSupportedVersion) + ").");
        return false;
    }
    if (!root.contains("name") || !root.contains("spriteSheet") || !root.contains("animations") ||
        !root.contains("frameWidth") || !root.contains("frameHeight")) {
        LOG_ERROR("AnimationLoader: character definition missing required fields (name / "
                  "spriteSheet / animations / frameWidth / frameHeight): " + filePath);
        return false;
    }

    out.name = root["name"].get<std::string>();
    out.spriteSheetPath = root["spriteSheet"].get<std::string>();
    out.animationsPath = root["animations"].get<std::string>();
    out.frameWidth = root["frameWidth"].get<int>();
    out.frameHeight = root["frameHeight"].get<int>();
    out.visualWidth = root.value("visualWidth", static_cast<float>(out.frameWidth));
    out.visualHeight = root.value("visualHeight", static_cast<float>(out.frameHeight));
    out.moveSpeed = root.value("moveSpeed", 200.0f);
    out.defaultDirection = root.value("direction", std::string("south"));

    if (root.contains("footprint")) {
        const json& fp = root["footprint"];
        out.footprint.width = fp.value("width", 28.0f);
        out.footprint.height = fp.value("height", 18.0f);
        out.footprint.offsetX = fp.value("offsetX", 0.0f);
        out.footprint.offsetY = fp.value("offsetY", 20.0f);
    }
    if (root.contains("pivot")) {
        const json& pivot = root["pivot"];
        out.pivot.x = pivot.value("x", 0.5f);
        out.pivot.y = pivot.value("y", 0.85f);
    }

    if (out.frameWidth <= 0 || out.frameHeight <= 0) {
        LOG_ERROR("AnimationLoader: invalid frame size in " + filePath);
        return false;
    }
    LOG_INFO("Character definition loaded: '" + out.name + "' (" + filePath + ")");
    return true;
}

std::unordered_map<std::string, AnimationClip> LoadAnimationClips(const std::string& filePath) {
    std::unordered_map<std::string, AnimationClip> clips;
    std::ifstream file(filePath);
    if (!file.is_open()) {
        LOG_ERROR("AnimationLoader: cannot open animations file: " + filePath);
        return clips;
    }
    json root;
    try {
        file >> root;
    } catch (const json::parse_error& error) {
        LOG_ERROR("AnimationLoader: JSON parse error in '" + filePath + "': " + error.what());
        return clips;
    }
    if (!root.contains("version") || root["version"].get<int>() != kSupportedVersion) {
        LOG_ERROR("AnimationLoader: unsupported animations version in " + filePath +
                  " (expected " + std::to_string(kSupportedVersion) + ").");
        return clips;
    }
    if (!root.contains("clips") || !root["clips"].is_object()) {
        LOG_ERROR("AnimationLoader: animations file missing 'clips' object: " + filePath);
        return clips;
    }

    for (auto it = root["clips"].begin(); it != root["clips"].end(); ++it) {
        AnimationClip clip;
        clip.name = it.key();
        const json& clipJson = it.value();
        clip.loop = clipJson.value("loop", true);
        if (!clipJson.contains("frames") || !clipJson["frames"].is_array() ||
            clipJson["frames"].empty()) {
            LOG_WARN("AnimationLoader: clip '" + clip.name + "' has no frames, skipped.");
            continue;
        }
        for (const auto& frameJson : clipJson["frames"]) {
            AnimationFrame frame;
            frame.frameIndex = frameJson.value("index", 0);
            frame.duration = frameJson.value("duration", 0.1f);
            clip.frames.push_back(frame);
        }
        clips[clip.name] = std::move(clip);
    }

    LOG_INFO("Animations loaded: " + std::to_string(clips.size()) + " clips (" + filePath + ")");
    return clips;
}

std::shared_ptr<SpriteSheet> LoadSpriteSheet(const CharacterDefinition& definition,
                                             legend::resource::ResourceManager& resources) {
    auto texture = resources.LoadTexture(definition.spriteSheetPath);
    if (!texture || !texture->IsValid()) {
        LOG_ERROR("AnimationLoader: sprite sheet texture load failed: " + definition.spriteSheetPath);
        return nullptr;
    }
    auto sheet = std::make_shared<SpriteSheet>();
    if (!sheet->Initialize(std::move(texture), definition.frameWidth, definition.frameHeight)) {
        return nullptr;
    }
    return sheet;
}

} // namespace legend::animation
