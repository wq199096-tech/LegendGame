#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Engine/Animation/AnimationClip.h"
#include "Engine/Animation/SpriteSheet.h"
#include "Engine/Entity/Character.h"

namespace legend::resource {
class ResourceManager;
}

namespace legend::animation {

// character.json 数据定义（版本 1）
struct CharacterDefinition {
    std::string name;
    std::string spriteSheetPath;
    std::string animationsPath;
    int frameWidth = 96;
    int frameHeight = 96;
    float visualWidth = 96.0f;
    float visualHeight = 96.0f;
    entity::CharacterFootprint footprint;
    math::Vector2 pivot{0.5f, 0.85f};
    float moveSpeed = 200.0f;
    // character.json "direction" 字段：默认朝向（south 等 8 方向小写名），NPC 用作固定朝向
    std::string defaultDirection = "south";
};

// character.json -> CharacterDefinition（失败返回 false + 明确日志，不崩溃）
bool LoadCharacterDefinition(const std::string& filePath, CharacterDefinition& out);

// animations.json -> clips 表（版本 1；失败返回空表 + 明确日志）
std::unordered_map<std::string, AnimationClip> LoadAnimationClips(const std::string& filePath);

// 依据 definition 加载 SpriteSheet（纹理经 ResourceManager 缓存）
std::shared_ptr<SpriteSheet> LoadSpriteSheet(const CharacterDefinition& definition,
                                             legend::resource::ResourceManager& resources);

} // namespace legend::animation
