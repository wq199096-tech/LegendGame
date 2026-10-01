#include "Client/Ui/CharacterVisualCatalog.h"

namespace legend::ui {

const char* CharacterVisualEntityName(std::uint16_t visualId) {
    switch (visualId) {
        case 1: return "player_warrior";
        case 2: return "player_mage";
        case 3: return "player_taoist";
        default: return "player_warrior";
    }
}

const char* CharacterVisualDisplayName(std::uint16_t visualId) {
    switch (visualId) {
        case 1: return "造型一";
        case 2: return "造型二";
        case 3: return "造型三";
        default: return "造型一";
    }
}

const char* CharacterGenderDisplayName(std::uint16_t gender) {
    return gender == 2 ? "女" : "男";
}

} // namespace legend::ui
