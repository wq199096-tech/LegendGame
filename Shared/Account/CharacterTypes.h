#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace legend::account {

// 阶段10 指令三十七：角色职业基础枚举（只是数据结构，不做职业技能树）。
enum class CharacterClass : std::uint16_t {
    None = 0,
    Warrior = 1,
    Mage = 2,
    Taoist = 3,
};

// 阶段10 指令三十八：性别（阶段10 只保存）。
enum class Gender : std::uint16_t {
    None = 0,
    Male = 1,
    Female = 2,
};

// 阶段10 指令三十六：角色名 2~12 字符。
inline constexpr std::size_t kCharacterNameMinLength = 2;
inline constexpr std::size_t kCharacterNameMaxLength = 12;

// 阶段10 指令三十五：每账号最多 4 个角色（未删除）。
inline constexpr std::size_t kMaxCharactersPerAccount = 4;

// 阶段10 指令四十一：CharacterSummary（wire + 列表共用）。
struct CharacterSummary {
    std::uint64_t characterId = 0;
    std::string name;
    std::uint16_t classId = 0;
    std::uint16_t gender = 0;
    std::uint32_t level = 1;
    std::uint16_t mapId = 1;
    std::int64_t lastPlayedAt = 0; // 0 = 从未游玩
};

inline bool IsValidClassId(std::uint16_t classId) {
    switch (static_cast<CharacterClass>(classId)) {
        case CharacterClass::Warrior:
        case CharacterClass::Mage:
        case CharacterClass::Taoist:
            return true;
        default:
            return false;
    }
}

inline bool IsValidGenderId(std::uint16_t gender) {
    switch (static_cast<Gender>(gender)) {
        case Gender::Male:
        case Gender::Female:
            return true;
        default:
            return false;
    }
}

// 阶段10 指令三十六：2~12 字符；禁止纯空格 / 前后空格 / 控制字符。
inline bool IsValidCharacterName(const std::string& name) {
    if (name.size() < kCharacterNameMinLength || name.size() > kCharacterNameMaxLength) {
        return false;
    }
    if (name.front() == ' ' || name.back() == ' ') {
        return false;
    }
    bool hasNonSpace = false;
    for (const unsigned char c : name) {
        if (c < 0x20 || c == 0x7F) {
            return false; // 控制字符
        }
        if (c != ' ') {
            hasNonSpace = true;
        }
    }
    return hasNonSpace;
}

} // namespace legend::account
