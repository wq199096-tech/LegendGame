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

// 阶段26 指令十一：角色名规则统一放 Shared（Client/Server 共用一套，禁止两套规则）。
// 以 Unicode 码点计数：min 2 / max 12 个码点（兼容旧 ASCII 名 2~12 字符）；
// 字节上限 36（BMP 中文每字 3 字节）。允许：ASCII 字母/数字/下划线、
// CJK 统一表意文字（含扩展A）、中间单个普通空格（首尾禁止）；
// 禁止：控制字符、非法 UTF-8 序列、其它一切符号/emoji/全角字符。
inline constexpr std::size_t kCharacterNameMinCodePoints = 2;
inline constexpr std::size_t kCharacterNameMaxCodePoints = 12;
inline constexpr std::size_t kCharacterNameMaxBytes = 36;

// 阶段26 指令十一：三套初始造型槽位（视觉选择，非职业）。
inline constexpr std::uint16_t kCharacterVisualIdMin = 1;
inline constexpr std::uint16_t kCharacterVisualIdMax = 3;

inline bool IsValidVisualId(std::uint16_t visualId) {
    return visualId >= kCharacterVisualIdMin && visualId <= kCharacterVisualIdMax;
}

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
    std::uint16_t visualId = 1;    // 阶段26 指令十一：初始造型槽位（1~3）
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

namespace detail {

// 解码一个 UTF-8 码点。成功推进 i 到下一码点起始；非法返回 false（i 不保证有效）。
// 拒绝：截断序列、过长编码（overlong）、代理区(0xD800~0xDFFF)、>0x10FFFF。
inline bool DecodeUtf8CodePoint(const std::string& s, std::size_t& i,
                                std::uint32_t& outCodePoint) {
    const unsigned char c0 = static_cast<unsigned char>(s[i]);
    std::size_t length = 0;
    std::uint32_t codePoint = 0;
    if (c0 < 0x80) {
        outCodePoint = c0;
        ++i;
        return true;
    }
    if ((c0 & 0xE0) == 0xC0) {
        length = 2;
        codePoint = c0 & 0x1F;
    } else if ((c0 & 0xF0) == 0xE0) {
        length = 3;
        codePoint = c0 & 0x0F;
    } else if ((c0 & 0xF8) == 0xF0) {
        length = 4;
        codePoint = c0 & 0x07;
    } else {
        return false; // 非法首字节（含 0x80~0xBF 裸 continuation）
    }
    if (i + length > s.size()) {
        return false; // 截断序列
    }
    for (std::size_t k = 1; k < length; ++k) {
        const unsigned char ck = static_cast<unsigned char>(s[i + k]);
        if ((ck & 0xC0) != 0x80) {
            return false; // continuation 字节非法
        }
        codePoint = (codePoint << 6) | (ck & 0x3F);
    }
    // 过长编码（overlong）：2 字节序列必须 >= 0x80，3 字节 >= 0x800，4 字节 >= 0x10000
    static constexpr std::uint32_t kMinValue[] = {0, 0x80, 0x800, 0x10000};
    if (codePoint < kMinValue[length - 1]) {
        return false;
    }
    if (codePoint >= 0xD800 && codePoint <= 0xDFFF) {
        return false; // UTF-16 代理区
    }
    if (codePoint > 0x10FFFF) {
        return false;
    }
    outCodePoint = codePoint;
    i += length;
    return true;
}

inline bool IsAllowedNameCodePoint(std::uint32_t cp) {
    // ASCII：字母/数字/下划线
    if ((cp >= 'A' && cp <= 'Z') || (cp >= 'a' && cp <= 'z') ||
        (cp >= '0' && cp <= '9') || cp == '_') {
        return true;
    }
    // 中间普通空格允许（首尾由外层拒绝）
    if (cp == ' ') {
        return true;
    }
    // CJK 统一表意文字 + 扩展A（常用汉字区间）
    if (cp >= 0x4E00 && cp <= 0x9FFF) {
        return true;
    }
    if (cp >= 0x3400 && cp <= 0x4DBF) {
        return true;
    }
    return false;
}

} // namespace detail

// 阶段26 指令十一：角色名规则（Shared 唯一定义，Client/Server 共用）。
// 2~12 个 Unicode 码点；UTF-8 严格解码；允许 ASCII 字母/数字/下划线 + CJK 汉字 +
// 中间空格（首尾禁止）；拒绝控制字符、符号、emoji、全角字符与非法 UTF-8。
inline bool IsValidCharacterName(const std::string& name) {
    if (name.size() < kCharacterNameMinCodePoints || name.size() > kCharacterNameMaxBytes) {
        return false; // 最短 2 字节（ASCII）与字节上限快速过滤
    }
    if (name.front() == ' ' || name.back() == ' ') {
        return false;
    }
    std::size_t codePoints = 0;
    std::size_t i = 0;
    bool hasNonSpace = false;
    while (i < name.size()) {
        std::uint32_t codePoint = 0;
        if (!detail::DecodeUtf8CodePoint(name, i, codePoint)) {
            return false; // 非法 UTF-8
        }
        if (codePoint != ' ') {
            hasNonSpace = true;
        }
        if (!detail::IsAllowedNameCodePoint(codePoint)) {
            return false;
        }
        ++codePoints;
        if (codePoints > kCharacterNameMaxCodePoints) {
            return false;
        }
    }
    return codePoints >= kCharacterNameMinCodePoints && hasNonSpace;
}

} // namespace legend::account
