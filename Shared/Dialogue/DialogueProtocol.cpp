#include "Shared/Dialogue/DialogueProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {

namespace {
using legend::network::ByteReader;
using legend::network::ByteWriter;
} // namespace

// DialogueOptionRequest(324)。
bool EncodeDialogueOptionRequest(const DialogueOptionRequestPayload& p,
                                 std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt64(p.dialogueSessionId);
    w.WriteUInt32(p.optionId);
    return true;
}

bool DecodeDialogueOptionRequest(const std::uint8_t* data, std::size_t size,
                                 DialogueOptionRequestPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.dialogueSessionId = r.ReadUInt64();
    out.optionId = r.ReadUInt32();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed DialogueOptionRequest payload";
        return false;
    }
    return true;
}

// DialoguePayload(325)。Encode 侧 >16 Option 截断保护（指令二十六）。
bool EncodeDialoguePayload(const DialoguePayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.dialogueSessionId);
    w.WriteUInt64(p.npcEntityId);
    if (!w.WriteString(p.title) || !w.WriteString(p.text)) {
        return false;
    }
    const std::size_t count = p.options.size() < kMaxDialogueOptions ? p.options.size()
                                                                     : kMaxDialogueOptions;
    w.WriteUInt8(static_cast<std::uint8_t>(count));
    for (std::size_t i = 0; i < count; ++i) {
        w.WriteUInt32(p.options[i].optionId);
        w.WriteUInt8(p.options[i].type);
        w.WriteUInt32(p.options[i].referenceId);
        if (!w.WriteString(p.options[i].label)) {
            return false;
        }
    }
    return true;
}

bool DecodeDialoguePayload(const std::uint8_t* data, std::size_t size, DialoguePayload& out,
                           std::string& error) {
    ByteReader r(data, size);
    out.dialogueSessionId = r.ReadUInt64();
    out.npcEntityId = r.ReadUInt64();
    if (!r.ReadString(out.title) || !r.ReadString(out.text)) {
        error = "malformed DialoguePayload strings";
        return false;
    }
    const std::uint8_t optionCount = r.ReadUInt8();
    if (optionCount > kMaxDialogueOptions) {
        // 指令二十六/九十：>16 拒绝。
        r.Invalidate();
    }
    out.options.clear();
    for (std::uint8_t i = 0; i < optionCount && r.IsValid(); ++i) {
        DialogueOptionData option;
        option.optionId = r.ReadUInt32();
        option.type = r.ReadUInt8();
        option.referenceId = r.ReadUInt32();
        if (!r.ReadString(option.label)) {
            break;
        }
        out.options.push_back(std::move(option));
    }
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed DialoguePayload payload";
        return false;
    }
    return true;
}

} // namespace legend::world
