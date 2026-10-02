#include "Client/Ui/PlayerFacingText.h"

#include <cstdint>

namespace legend::ui {

const char* ItemTypeName(const std::string& type) {
    if (type == "Weapon") return "武器";
    if (type == "Armor") return "护甲";
    if (type == "Material") return "材料";
    if (type == "Consumable") return "消耗品";
    if (type == "Quest") return "任务";
    return "物品";
}

const char* EquipSlotName(const std::string& slot) {
    if (slot == "Weapon") return "武器";
    if (slot == "Armor") return "护甲";
    return "无";
}

const char* QuestObjectiveVerb(const std::string& type) {
    if (type == "KillMonster") return "击败";
    if (type == "CollectItem") return "收集";
    if (type == "ReachLevel") return "等级达到";
    if (type == "ReachArea") return "前往";
    return "进度";
}

const char* StatusKindName(const std::string& type) {
    if (type == "Buff") return "增益";
    if (type == "Debuff") return "减益";
    return "状态";
}

const char* MapTypeName(const std::string& type) {
    if (type == "Town") return "城镇";
    if (type == "Field") return "野外";
    return "区域";
}

const char* ClassName(int classId) {
    switch (classId) {
        case 1: return "战士";
        case 2: return "法师";
        case 3: return "道士";
        default: return "冒险者";
    }
}

bool ContainsCjk(const std::string& text) {
    size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::size_t length = 0;
        std::uint32_t codePoint = 0;
        if (c < 0x80) {
            ++i;
            continue;
        }
        if ((c & 0xE0) == 0xC0) { length = 2; codePoint = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { length = 3; codePoint = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { length = 4; codePoint = c & 0x07; }
        else { return false; } // 非法 UTF-8 首字节
        if (i + length > text.size()) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            const unsigned char ck = static_cast<unsigned char>(text[i + k]);
            if ((ck & 0xC0) != 0x80) {
                return false;
            }
            codePoint = (codePoint << 6) | (ck & 0x3F);
        }
        // CJK 统一表意文字 + 扩展A。
        if ((codePoint >= 0x4E00 && codePoint <= 0x9FFF) ||
            (codePoint >= 0x3400 && codePoint <= 0x4DBF)) {
            return true;
        }
        i += length;
    }
    return false;
}

} // namespace legend::ui
