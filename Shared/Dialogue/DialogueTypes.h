#pragma once

#include <cstddef>
#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20：Dialogue 公共类型（一层菜单，不做复杂树）。
// ---------------------------------------------------------------------------

// Option 数量上限（指令二十六/九十）：Decode >16 拒绝。
inline constexpr std::size_t kMaxDialogueOptions = 16;

// Option 类型（指令二十四）。
enum class DialogueOptionType : std::uint8_t {
    Quest = 1,
    Shop = 2,
    Teleport = 3,
    Close = 4,
};

inline const char* DialogueOptionTypeName(std::uint8_t type) {
    switch (static_cast<DialogueOptionType>(type)) {
        case DialogueOptionType::Quest: return "Quest";
        case DialogueOptionType::Shop: return "Shop";
        case DialogueOptionType::Teleport: return "Teleport";
        case DialogueOptionType::Close: return "Close";
    }
    return "Unknown";
}

} // namespace legend::world
