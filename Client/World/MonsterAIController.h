#pragma once

#include <random>
#include <cstdint>

#include "Client/World/AggroTable.h"
#include "Engine/Entity/ActorRegistry.h"
#include "Engine/Entity/CharacterController.h"
#include "Engine/Map/MapTypes.h"
#include "Engine/Math/Vector2.h"

namespace legend::map {
class Map;
}

namespace legend::world {
class MonsterCharacter;

// 怪物 AI 控制器：每个怪物实例拥有自己的 AI context。
// 只负责生成移动方向 / 状态切换；真正位移复用 CharacterController（不写第二套移动碰撞）。
//
// 状态机：Idle -> (超时) -> Wander -> (到达/超时/撞墙) -> Idle
//         任意状态感知到目标 -> Chase
//         Chase 超出 loseTargetRange -> Idle；距 home > leashRange -> ReturnHome
//         ReturnHome 到家 -> Idle（回家途中不重新 Aggro，防抖动）
class MonsterAIController {
public:
    static constexpr float kPerceptionInterval = 0.15f; // 感知节流（0.1~0.25s）
    static constexpr float kArriveDistance = 16.0f;     // Wander 目标到达判定
    static constexpr float kHomeArriveDistance = 24.0f; // ReturnHome 到家判定
    static constexpr float kWanderTimeout = 10.0f;      // Wander 单目标超时
    static constexpr float kStuckCheckInterval = 0.5f;  // 撞墙检测周期
    static constexpr float kStuckDistance = 4.0f;       // 周期内位移小于该值视为撞墙

    // 更新一步；rng 由 WorldActorManager 统一持有（LEGEND_AI_SEED 可复现）
    void Update(MonsterCharacter& monster, const map::Map& map,
                const entity::ActorRegistry& registry,
                const entity::CharacterController& controller, std::mt19937& rng,
                float deltaTime);

    const AggroTable& GetAggroTable() const { return m_aggro; }
    // F3 Debug
    const math::Vector2& GetWanderTarget() const { return m_context.wanderTarget; }
    bool HasWanderTarget() const { return m_context.hasWanderTarget; }
    uint64_t GetScanCount() const { return m_context.scanCount; }

private:
    struct Context {
        float perceptionTimer = 0.0f;
        float stateTimer = 0.0f; // 进入当前状态的累计时间
        float idleDuration = 0.0f; // Idle 随机等待时长
        math::Vector2 wanderTarget{0.0f, 0.0f};
        bool hasWanderTarget = false;
        float stuckCheckTimer = 0.0f;
        math::Vector2 lastStuckCheckPos{0.0f, 0.0f};
        bool chasePaused = false; // stop/resume 滞回：true=已在目标旁停步
        uint64_t scanCount = 0;   // 感知扫描计数（窗口标题 Scans）
    };

    // 感知：扫描 Player 进入 Aggro 范围 -> AddThreat -> 目标选择
    void RunPerception(MonsterCharacter& monster, const entity::ActorRegistry& registry);
    void EnterIdle(MonsterCharacter& monster, std::mt19937& rng);
    void EnterWander(MonsterCharacter& monster, const map::Map& map, std::mt19937& rng);
    void EnterChase(MonsterCharacter& monster);
    void EnterReturnHome(MonsterCharacter& monster);

    Context m_context;
    AggroTable m_aggro;
};

} // namespace legend::world
