#include "Engine/Animation/AnimationPlayer.h"

#include "Engine/Debug/Logger.h"

namespace legend::animation {

void AnimationPlayer::SetClips(
    std::shared_ptr<const std::unordered_map<std::string, AnimationClip>> clips) {
    m_clips = std::move(clips);
    Stop();
}

bool AnimationPlayer::Play(const std::string& clipName) {
    if (m_currentName == clipName && m_current != nullptr) {
        return true; // 同一 Clip：保持进度，不重置
    }
    if (!m_clips) {
        return false;
    }
    const auto it = m_clips->find(clipName);
    if (it == m_clips->end() || it->second.frames.empty()) {
        LOG_WARN("AnimationPlayer: clip '" + clipName + "' not found.");
        return false;
    }
    m_current = &it->second;
    m_currentName = clipName;
    m_elapsed = 0.0f;
    m_frameOrdinal = 0;
    return true;
}

void AnimationPlayer::Update(float deltaTime) {
    if (m_current == nullptr || m_paused || m_current->frames.empty()) {
        return;
    }
    m_elapsed += deltaTime * m_speed;

    int guard = 0;
    while (m_elapsed >= m_current->GetFrame(m_frameOrdinal).duration) {
        m_elapsed -= m_current->GetFrame(m_frameOrdinal).duration;
        ++m_frameOrdinal;
        if (m_frameOrdinal >= m_current->GetFrameCount()) {
            if (m_current->loop) {
                m_frameOrdinal = 0; // Loop 回到第一帧
            } else {
                m_frameOrdinal = m_current->GetFrameCount() - 1;
                m_elapsed = 0.0f;
                break;
            }
        }
        if (++guard > 64) {
            break; // deltaTime 异常大时保护
        }
    }
}

void AnimationPlayer::Stop() {
    m_current = nullptr;
    m_currentName.clear();
    m_elapsed = 0.0f;
    m_frameOrdinal = 0;
    m_paused = false;
}

int AnimationPlayer::GetCurrentFrameIndex() const {
    if (m_current == nullptr || m_current->frames.empty()) {
        return 0;
    }
    return m_current->GetFrame(m_frameOrdinal).frameIndex;
}

} // namespace legend::animation
