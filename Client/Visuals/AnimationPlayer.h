#pragma once

#include "Client/Visuals/VisualAssetData.h"

#include <map>
#include <string>

namespace legend::visual {

// ---------------------------------------------------------------------------
// 阶段24 指令十六：统一 AnimationPlayer——Player / Monster / NPC / Portal 共用，
// 禁止各实体分别实现播放器。
//
// 帧布局约定：SpriteSheet 行 = 方向（Direction8 顺序 S,SW,W,NW,N,NE,E,SE，
// directionCount=1 时仅 1 行），列 = 帧序号。
// 只做纯逻辑推进（无 GL 依赖），UV 由 CurrentFrame() 计算返回。
// ---------------------------------------------------------------------------
class AnimationPlayer {
public:
    // clips 生命周期由调用方保证（VisualRuntime 持有 AnimationSet）。
    void SetClips(const std::map<std::string, const AnimationClipDef*>* clips);
    // 便捷重载：直接从 AnimationSet 构建内部查找表（拷贝指针，不拷贝定义）。
    void SetClips(const AnimationSet& set);

    // 播放指定片段；同名且正在播放时保持进度（避免 Idle/Idle 反复重置）。
    // 找不到时返回 false 并保持现状（调用方自行 fallback）。
    bool Play(const std::string& animationId);
    void Stop();
    // 0..7（Direction8 顺序）；越界会被 clamp 到有效范围。
    void SetDirection(int directionIndex);
    int Direction() const { return m_direction; }

    void Update(float deltaTime);

    bool IsPlaying() const { return m_current != nullptr; }
    bool IsFinished() const; // NonLoop 播完（Loop 永远 false）
    const std::string& CurrentClipId() const { return m_currentId; }

    struct Frame {
        bool valid = false;
        const AnimationClipDef* clip = nullptr;
        int frameIndex = 0;         // 帧序号（列）
        int directionRow = 0;       // 方向行
        float u0 = 0.0f;
        float v0 = 0.0f;
        float u1 = 1.0f;
        float v1 = 1.0f;
    };

    Frame CurrentFrame() const;

private:
    const AnimationClipDef* FindClip(const std::string& animationId) const;

    std::map<std::string, const AnimationClipDef*> m_clips;
    const AnimationClipDef* m_current = nullptr;
    std::string m_currentId;
    float m_elapsed = 0.0f;
    int m_direction = 0;
    bool m_finished = false;
    int m_lastFrameIndex = -1;
};

} // namespace legend::visual
