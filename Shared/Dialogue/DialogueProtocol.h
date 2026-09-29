#pragma once

#include "Shared/Dialogue/DialogueTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令二十五/二十六/九十：Dialogue 协议（一层菜单，Option ≤16）。
// MessageId：DialogueOptionRequest=324（Client 只发 sessionId + optionId）/
// DialoguePayload=325（服务器返回 title/text/options）。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令八十八）。
// ---------------------------------------------------------------------------

struct DialogueOptionData {
    std::uint32_t optionId = 0;
    std::uint8_t type = 0;        // DialogueOptionType
    std::uint32_t referenceId = 0; // Quest→questId；Shop→shopId；Teleport→teleportId；Close→0
    std::string label;
};

// DialogueOptionRequest(324)。
struct DialogueOptionRequestPayload {
    std::uint64_t requestId = 0;
    std::uint64_t dialogueSessionId = 0;
    std::uint32_t optionId = 0;
};

// DialoguePayload(325)。
struct DialoguePayload {
    std::uint64_t dialogueSessionId = 0;
    std::uint64_t npcEntityId = 0;
    std::string title;
    std::string text;
    std::vector<DialogueOptionData> options;
};

bool EncodeDialogueOptionRequest(const DialogueOptionRequestPayload& p,
                                 std::vector<std::uint8_t>& out);
bool DecodeDialogueOptionRequest(const std::uint8_t* data, std::size_t size,
                                 DialogueOptionRequestPayload& out, std::string& error);
bool EncodeDialoguePayload(const DialoguePayload& p, std::vector<std::uint8_t>& out);
bool DecodeDialoguePayload(const std::uint8_t* data, std::size_t size, DialoguePayload& out,
                           std::string& error);

} // namespace legend::world
