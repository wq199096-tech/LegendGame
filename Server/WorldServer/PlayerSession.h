#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace legend::world {

// 阶段11 指令二十七：PlayerSession —— 进入世界后的权威玩家数据。
// 由 WorldManager 持有（io 线程访问）；位置为服务器权威（指令三十三）。
class PlayerSession {
public:
    PlayerSession() = default;
    PlayerSession(std::uint64_t connectionId, std::uint64_t accountId, std::uint64_t characterId,
                  const std::string& characterName, std::uint16_t classId, std::uint16_t gender,
                  std::uint32_t level, std::uint16_t mapId, float positionX, float positionY);

    std::uint64_t ConnectionId() const { return m_connectionId; }
    std::uint64_t AccountId() const { return m_accountId; }
    std::uint64_t CharacterId() const { return m_characterId; }
    const std::string& CharacterName() const { return m_characterName; }
    std::uint16_t ClassId() const { return m_classId; }
    std::uint16_t Gender() const { return m_gender; }
    std::uint32_t Level() const { return m_level; }
    std::uint16_t MapId() const { return m_mapId; }

    float PositionX() const { return m_positionX; }
    float PositionY() const { return m_positionY; }
    void SetPosition(float x, float y) {
        m_positionX = x;
        m_positionY = y;
    }

    std::uint32_t LastProcessedInputSequence() const { return m_lastProcessedInputSequence; }
    void SetLastProcessedInputSequence(std::uint32_t sequence) {
        m_lastProcessedInputSequence = sequence;
    }

    bool IsPositionDirty() const { return m_dirtyPosition; }
    void SetPositionDirty(bool dirty) { m_dirtyPosition = dirty; }

    std::chrono::steady_clock::time_point LastMoveTime() const { return m_lastMoveTime; }
    void TouchMoveTime() { m_lastMoveTime = std::chrono::steady_clock::now(); }

private:
    std::uint64_t m_connectionId = 0;
    std::uint64_t m_accountId = 0;
    std::uint64_t m_characterId = 0;
    std::string m_characterName;
    std::uint16_t m_classId = 0;
    std::uint16_t m_gender = 0;
    std::uint32_t m_level = 1;
    std::uint16_t m_mapId = 1;
    float m_positionX = 0.0f;
    float m_positionY = 0.0f;
    std::uint32_t m_lastProcessedInputSequence = 0;
    bool m_dirtyPosition = false;
    std::chrono::steady_clock::time_point m_lastMoveTime{std::chrono::steady_clock::now()};
};

} // namespace legend::world
