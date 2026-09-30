#include "Client/Visuals/AnimationPlayer.h"

#include <algorithm>
#include <cmath>

namespace legend::visual {

void AnimationPlayer::SetClips(const std::map<std::string, const AnimationClipDef*>* clips) {
    m_clips.clear();
    if (clips != nullptr) {
        m_clips = *clips;
    }
    m_current = nullptr;
    m_currentId.clear();
    m_elapsed = 0.0f;
    m_finished = false;
    m_lastFrameIndex = -1;
}

void AnimationPlayer::SetClips(const AnimationSet& set) {
    m_clips.clear();
    for (const auto& clip : set.clips) {
        m_clips[clip.animationId] = &clip;
    }
    m_current = nullptr;
    m_currentId.clear();
    m_elapsed = 0.0f;
    m_finished = false;
    m_lastFrameIndex = -1;
}

const AnimationClipDef* AnimationPlayer::FindClip(const std::string& animationId) const {
    const auto it = m_clips.find(animationId);
    return it == m_clips.end() ? nullptr : it->second;
}

bool AnimationPlayer::Play(const std::string& animationId) {
    const AnimationClipDef* clip = FindClip(animationId);
    if (clip == nullptr) {
        return false; // 找不到：保持现状（调用方 fallback），绝不崩溃
    }
    if (m_current == clip && !m_finished) {
        return true; // 同片段继续播（不重置进度）
    }
    m_current = clip;
    m_currentId = animationId;
    m_elapsed = 0.0f;
    m_finished = false;
    m_lastFrameIndex = -1;
    return true;
}

void AnimationPlayer::Stop() {
    m_current = nullptr;
    m_currentId.clear();
    m_elapsed = 0.0f;
    m_finished = false;
    m_lastFrameIndex = -1;
}

void AnimationPlayer::SetDirection(int directionIndex) {
    m_direction = std::clamp(directionIndex, 0, 7);
}

void AnimationPlayer::Update(float deltaTime) {
    if (m_current == nullptr || deltaTime <= 0.0f) {
        return;
    }
    m_elapsed += deltaTime;
    const float frameDuration = 1.0f / m_current->fps;
    const float totalDuration = frameDuration * static_cast<float>(m_current->frameCount);
    if (m_current->loop) {
        if (totalDuration > 0.0f) {
            m_elapsed = std::fmod(m_elapsed, totalDuration);
        }
        m_finished = false;
    } else if (m_elapsed >= totalDuration) {
        m_elapsed = totalDuration - frameDuration * 0.001f; // 停在最后一帧
        if (m_elapsed < 0.0f) {
            m_elapsed = 0.0f;
        }
        m_finished = true;
    }
}

bool AnimationPlayer::IsFinished() const {
    return m_current != nullptr && m_current->loop == false && m_finished;
}

AnimationPlayer::Frame AnimationPlayer::CurrentFrame() const {
    Frame frame;
    if (m_current == nullptr) {
        return frame;
    }
    const float frameDuration = 1.0f / m_current->fps;
    int index = 0;
    if (frameDuration > 0.0f) {
        index = static_cast<int>(m_elapsed / frameDuration);
    }
    index = std::clamp(index, 0, m_current->frameCount - 1);

    // 方向行：directionCount=1 -> row 0；=4/8 -> 直接映射（Direction8 顺序）。
    int row = 0;
    if (m_current->directionCount == 8) {
        row = std::clamp(m_direction, 0, 7);
    } else if (m_current->directionCount == 4) {
        row = std::clamp(m_direction / 2, 0, 3);
    }

    const int cols = m_current->sheetWidth / m_current->frameWidth;
    if (cols <= 0 || m_current->sheetWidth <= 0 || m_current->sheetHeight <= 0) {
        return frame; // 未 Resolve（无 sheet 尺寸）——返回 invalid frame
    }
    const float u0 = static_cast<float>(index * m_current->frameWidth) /
                     static_cast<float>(m_current->sheetWidth);
    const float u1 = static_cast<float>((index + 1) * m_current->frameWidth) /
                     static_cast<float>(m_current->sheetWidth);
    const float v0 = static_cast<float>(row * m_current->frameHeight) /
                     static_cast<float>(m_current->sheetHeight);
    const float v1 = static_cast<float>((row + 1) * m_current->frameHeight) /
                     static_cast<float>(m_current->sheetHeight);

    frame.valid = true;
    frame.clip = m_current;
    frame.frameIndex = index;
    frame.directionRow = row;
    frame.u0 = u0;
    frame.v0 = v0;
    frame.u1 = u1;
    frame.v1 = v1;
    return frame;
}

} // namespace legend::visual
