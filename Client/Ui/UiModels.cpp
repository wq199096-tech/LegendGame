#include "Client/Ui/UiModels.h"

#include <algorithm>

namespace legend::ui {

// ---- ToastManager（指令五十六：最大 5 条，自动淡出）----
void ToastManager::Push(ToastLevel level, const std::string& text, float lifeSeconds) {
    if (m_toasts.size() >= kMaxToasts) {
        m_toasts.erase(m_toasts.begin()); // 挤掉最旧
    }
    Toast toast;
    toast.level = level;
    toast.text = text;
    toast.age = 0.0f;
    toast.life = lifeSeconds;
    m_toasts.push_back(std::move(toast));
}

void ToastManager::Update(float deltaTime) {
    for (auto& toast : m_toasts) {
        toast.age += deltaTime;
    }
    m_toasts.erase(std::remove_if(m_toasts.begin(), m_toasts.end(),
                                  [](const Toast& t) { return t.age >= t.life; }),
                   m_toasts.end());
}

// ---- MapBanner（指令三十六：淡入 0.4s / 保持 / 淡出 0.4s，总约 2s）----
void MapBanner::Show(const std::string& mapName) {
    m_text = mapName;
    m_age = 0.0f;
}

void MapBanner::Update(float deltaTime) {
    if (m_age < m_total) {
        m_age += deltaTime;
    }
}

float MapBanner::Alpha() const {
    const float fadeIn = 0.4f;
    const float fadeOut = 0.4f;
    if (m_age < fadeIn) {
        return m_age / fadeIn;
    }
    const float remain = m_total - m_age;
    if (remain < fadeOut) {
        return std::max(0.0f, remain / fadeOut);
    }
    return 1.0f;
}

// ---- BossBar（指令三十四）----
void BossBar::ShowBoss(std::uint64_t entityId, const std::string& name, std::uint32_t currentHp,
                       std::uint32_t maxHp) {
    m_visible = true;
    m_entityId = entityId;
    m_name = name;
    m_currentHp = currentHp;
    m_maxHp = maxHp;
}

void BossBar::UpdateBoss(std::uint64_t entityId, std::uint32_t currentHp, std::uint32_t maxHp) {
    if (!m_visible || m_entityId != entityId) {
        return;
    }
    m_currentHp = currentHp;
    m_maxHp = maxHp;
}

// ---- QuestTrackerModel ----
bool QuestTrackerModel::HasQuest(std::uint32_t questId) const {
    for (const auto& entry : entries) {
        if (entry.questId == questId) {
            return true;
        }
    }
    return false;
}

// ---- MinimapModel ----
float MinimapModel::NormalizeX(float worldX) const {
    const float span = maxX - minX;
    return span > 0.0f ? (worldX - minX) / span : 0.0f;
}

float MinimapModel::NormalizeY(float worldY) const {
    const float span = maxY - minY;
    return span > 0.0f ? (worldY - minY) / span : 0.0f;
}

// ---- LevelUpFx（指令二十三）----
void LevelUpFx::Trigger() {
    m_age = 0.0f;
}

void LevelUpFx::Update(float deltaTime) {
    if (m_age < m_total) {
        m_age += deltaTime;
    }
}

float LevelUpFx::Alpha() const {
    const float edge = 0.3f;
    if (m_age < edge) {
        return m_age / edge;
    }
    const float remain = m_total - m_age;
    if (remain < edge) {
        return std::max(0.0f, remain / edge);
    }
    return 1.0f;
}

} // namespace legend::ui
