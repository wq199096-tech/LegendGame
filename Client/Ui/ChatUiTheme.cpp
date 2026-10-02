#include "Client/Ui/ChatUiTheme.h"

namespace legend::ui {

const char* ChatUiTheme::TabLabel(std::uint8_t tab) {
    // Stage27 指令十九：综合/附近/世界/私聊/系统。
    switch (tab) {
        case 0: return "综合";
        case 1: return "附近";
        case 2: return "世界";
        case 3: return "私聊";
        case 4: return "系统";
        default: return "?";
    }
}

const char* ChatUiTheme::ChannelTag(std::uint8_t channel) {
    // Stage27 指令二十二：[附近] 界面英雄：你好 / [私聊] A → B：你好 / [系统] …
    switch (channel) {
        case 1: return "附近";
        case 2: return "世界";
        case 3: return "私聊";
        case 4: return "系统";
        default: return "?";
    }
}

ChatUiTheme& DefaultChatUiTheme() {
    static ChatUiTheme theme;
    return theme;
}

} // namespace legend::ui
