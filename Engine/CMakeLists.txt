# ---------------------------------------------------------------------------
# legend_engine：自研引擎静态库
# ---------------------------------------------------------------------------
set(LEGEND_ENGINE_SOURCES
    Combat/CombatResolver.cpp
    Combat/CombatStats.cpp
    Combat/CombatSystem.cpp
    Combat/CombatTarget.cpp
    Core/Application.cpp
    Core/Engine.cpp
    Core/Timer.cpp
    Core/Window.cpp
    Debug/Logger.cpp
    Entity/ActorRegistry.cpp
    Entity/Character.cpp
    Entity/CharacterController.cpp
    Entity/Direction8.cpp
    Entity/Entity.cpp
    Entity/TargetHandle.cpp
    Input/InputManager.cpp
    Animation/AnimationPlayer.cpp
    Animation/AnimationStateMachine.cpp
    Animation/AnimationLoader.cpp
    Animation/SpriteSheet.cpp
    Item/ItemDatabase.cpp
    Item/ItemDefinition.cpp
    Item/Inventory.cpp
    Item/EquipmentComponent.cpp
    Item/EquipmentSystem.cpp
    Map/Map.cpp
    Map/MapLayer.cpp
    Map/MapLoader.cpp
    Map/MapRenderer.cpp
    Map/CollisionLayer.cpp
    Map/ObjectLayer.cpp
    Map/OcclusionLayer.cpp
    Map/TileLayer.cpp
    Math/Matrix4x4.cpp
    Progression/ExperienceTable.cpp
    Progression/LevelSystem.cpp
    Render/Camera2D.cpp
    Render/CharacterRenderer.cpp
    Render/GLApi.cpp
    Render/Renderer.cpp
    Render/Shader.cpp
    Render/SpriteBatch.cpp
    Render/Texture.cpp
    Resource/ResourceManager.cpp
    Scene/GameObject.cpp
    Scene/Scene.cpp
    Scene/SceneManager.cpp
    Skill/SkillCooldowns.cpp
    Skill/SkillDatabase.cpp
    Skill/SkillLoadout.cpp
    Skill/SkillTypes.cpp
)

add_library(legend_engine STATIC ${LEGEND_ENGINE_SOURCES})

# 我方源码含中文注释，MSVC 需要 /utf-8（第三方库源码不受影响）
if(MSVC)
    target_compile_options(legend_engine PRIVATE /utf-8)
endif()

# 以工程根目录为 include 基准（#include "Engine/..."、"Shared/..."）
target_include_directories(legend_engine PUBLIC ${PROJECT_SOURCE_DIR})

target_link_libraries(legend_engine
    PUBLIC
        SDL3::SDL3
        nlohmann_json::nlohmann_json
)

if(WIN32)
    target_link_libraries(legend_engine PUBLIC opengl32)
endif()
