#include "Client/Ui/UiTheme.h"

namespace legend::ui {

const UiTheme& DefaultUiTheme() {
    static const UiTheme theme;
    return theme;
}

} // namespace legend::ui
