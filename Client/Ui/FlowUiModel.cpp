#include "Client/Ui/FlowUiModel.h"

namespace legend::flow {

const char* ClientFlowStateName(ClientFlowState state) {
    switch (state) {
        case ClientFlowState::Boot: return "Boot";
        case ClientFlowState::Connecting: return "Connecting";
        case ClientFlowState::Login: return "Login";
        case ClientFlowState::Register: return "Register";
        case ClientFlowState::CharacterLobby: return "CharacterLobby";
        case ClientFlowState::CharacterCreate: return "CharacterCreate";
        case ClientFlowState::EnteringWorld: return "EnteringWorld";
        case ClientFlowState::InWorld: return "InWorld";
        case ClientFlowState::Disconnected: return "Disconnected";
        case ClientFlowState::FatalError: return "FatalError";
    }
    return "Unknown";
}

namespace {

// 解码一个 UTF-8 码点；返回码点字节数（非法返回 0）。
std::size_t DecodeCodepointLength(const std::string& text, std::size_t i) {
    const auto byteCount = [](unsigned char lead) -> std::size_t {
        if (lead < 0x80) return 1;
        if ((lead & 0xE0) == 0xC0) return 2;
        if ((lead & 0xF0) == 0xE0) return 3;
        if ((lead & 0xF8) == 0xF0) return 4;
        return 0;
    };
    const std::size_t len = byteCount(static_cast<unsigned char>(text[i]));
    if (len == 0 || i + len > text.size()) {
        return 0;
    }
    for (std::size_t k = 1; k < len; ++k) {
        if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
            return 0;
        }
    }
    return len;
}

} // namespace

std::size_t FlowTextCodepointCount(const std::string& text) {
    std::size_t count = 0;
    for (std::size_t i = 0; i < text.size();) {
        const std::size_t len = DecodeCodepointLength(text, i);
        if (len == 0) {
            break; // 非法序列：停止计数（上层校验负责拒绝）
        }
        i += len;
        ++count;
    }
    return count;
}

void FlowTextAppend(std::string& text, const std::string& chunk, std::size_t maxCodepoints) {
    std::size_t consumed = 0;
    while (consumed < chunk.size()) {
        const std::size_t len = DecodeCodepointLength(chunk, consumed);
        if (len == 0) {
            break; // 丢弃非法/截断尾部
        }
        if (FlowTextCodepointCount(text) >= maxCodepoints) {
            break; // 超出上限拒绝追加
        }
        text.append(chunk, consumed, len);
        consumed += len;
    }
}

void FlowTextBackspace(std::string& text) {
    if (text.empty()) {
        return;
    }
    // 从尾部回退，跳过 continuation 字节（10xxxxxx）。
    std::size_t i = text.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(text[i]) & 0xC0) == 0x80) {
        --i;
    }
    text.resize(i);
}

} // namespace legend::flow
