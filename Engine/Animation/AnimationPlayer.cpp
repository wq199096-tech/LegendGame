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
    m_finished = false;
    m_pendingEvents.clear();
    return true;
}

void AnimationPlayer::Update(float deltaTime) {
    if (m_current == nullptr || m_paused || m_current->frames.empty()) {
        return;
    }
    if (m_finished) {
        return; // NonLoop 已播完，停在最后一帧
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
                // NonLoop 播完：停在最后一帧；最后一帧事件在进入该帧时已抛出，不重复
                m_frameOrdinal = m_current->GetFrameCount() - 1;
                m_elapsed = 0.0f;
                m_finished = true;
                break;
            }
        }
        EmitFrameEvent(); // 进入新帧：抛出该帧事件（每帧只在此处触发一次）
        if (++guard > 64) {
            break; // deltaTime 异常大时保护
        }
    }
}

std::vector<std::string> AnimationPlayer::ConsumeEvents() {
    std::vector<std::string> events;
    events.swap(m_pendingEvents);
    return events;
}

void AnimationPlayer::EmitFrameEvent() {
    if (m_current == nullptr) {
        return;
    }
    const AnimationFrame& frame = m_current->GetFrame(m_frameOrdinal);
    if (!frame.event.empty()) {
        m_pendingEvents.push_back(frame.event);
    }
}

void AnimationPlayer::Stop() {
    m_current = nullptr;
    m_currentName.clear();
    m_elapsed = 0.0f;
    m_frameOrdinal = 0;
    m_finished = false;
    m_pendingEvents.clear();
}

int AnimationPlayer::GetCurrentFrameIndex() const {
    if (m_current == nullptr || m_current->frames.empty()) {
        return 0;
    }
    return m_current->GetFrame(m_frameOrdinal).frameIndex;
}

} // namespace legend::animation
