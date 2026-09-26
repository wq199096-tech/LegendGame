#pragma once

#include <memory>
#include <string>
#include <unordered_map>

#include "Engine/Animation/AnimationClip.h"

namespace legend::animation {

// 动画播放器：推进时间、处理 Loop/NonLoop、切换片段。
// 重复 Play 同名 Clip 不会重置进度。
class AnimationPlayer {
public:
    void SetClips(std::shared_ptr<const std::unordered_map<std::string, AnimationClip>> clips);

    // 播放指定片段；同名且正在播放时保持进度；找不到时记录警告并保持现状
    bool Play(const std::string& clipName);
    void Update(float deltaTime);
    void Pause() { m_paused = true; }
    void Resume() { m_paused = false; }
    void Stop();

    void SetSpeed(float speed) { m_speed = speed > 0.0f ? speed : 1.0f; }
    float GetSpeed() const { return m_speed; }
    bool IsPlaying() const { return m_current != nullptr && !m_paused; }

    const std::string& GetCurrentClipName() const { return m_currentName; }
    // 当前帧在 SpriteSheet 中的帧序号（贴图索引）
    int GetCurrentFrameIndex() const;
    // 当前帧在 Clip 内的序号（用于 Frame: 2/4 这类显示）
    int GetCurrentFrameOrdinal() const { return m_frameOrdinal; }
    int GetCurrentFrameCount() const { return m_current ? m_current->GetFrameCount() : 0; }

private:
    std::shared_ptr<const std::unordered_map<std::string, AnimationClip>> m_clips;
    const AnimationClip* m_current = nullptr;
    std::string m_currentName;
    float m_elapsed = 0.0f;
    float m_speed = 1.0f;
    bool m_paused = false;
    int m_frameOrdinal = 0;
};

} // namespace legend::animation
