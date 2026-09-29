#include "Shared/Quest/QuestProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

#include <algorithm>

namespace legend::world {

namespace {
using legend::network::ByteReader;
using legend::network::ByteWriter;
// ByteWriter/ByteReader 无 Int64 接口——exp/gold 非负（<2^63），用 UInt64 位模式
// 双向传输无损（static_cast 语义，同 ProgressionProtocol）。
inline std::uint64_t ToBits(std::int64_t v) { return static_cast<std::uint64_t>(v); }
inline std::int64_t FromBits(std::uint64_t v) { return static_cast<std::int64_t>(v); }
} // namespace

// QuestAcceptRequest(310)。
bool EncodeQuestAcceptRequest(const QuestAcceptRequestPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt32(p.questId);
    return true;
}

bool DecodeQuestAcceptRequest(const std::uint8_t* data, std::size_t size,
                              QuestAcceptRequestPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.questId = r.ReadUInt32();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed QuestAcceptRequest payload";
        return false;
    }
    return true;
}

// QuestAcceptResponse(311)。
bool EncodeQuestAcceptResponse(const QuestAcceptResponsePayload& p,
                               std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt32(p.questId);
    w.WriteBool(p.success);
    w.WriteUInt8(p.resultCode);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeQuestAcceptResponse(const std::uint8_t* data, std::size_t size,
                               QuestAcceptResponsePayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.questId = r.ReadUInt32();
    out.success = r.ReadBool();
    out.resultCode = r.ReadUInt8();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed QuestAcceptResponse payload";
        return false;
    }
    return true;
}

// QuestTurnInRequest(312)。
bool EncodeQuestTurnInRequest(const QuestTurnInRequestPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt32(p.questId);
    return true;
}

bool DecodeQuestTurnInRequest(const std::uint8_t* data, std::size_t size,
                              QuestTurnInRequestPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.questId = r.ReadUInt32();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed QuestTurnInRequest payload";
        return false;
    }
    return true;
}

// QuestTurnInResponse(313)。
bool EncodeQuestTurnInResponse(const QuestTurnInResponsePayload& p,
                               std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt32(p.questId);
    w.WriteBool(p.success);
    w.WriteUInt8(p.resultCode);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeQuestTurnInResponse(const std::uint8_t* data, std::size_t size,
                               QuestTurnInResponsePayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.questId = r.ReadUInt32();
    out.success = r.ReadBool();
    out.resultCode = r.ReadUInt8();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed QuestTurnInResponse payload";
        return false;
    }
    return true;
}

// QuestAbandonRequest(314)。
bool EncodeQuestAbandonRequest(const QuestAbandonRequestPayload& p,
                               std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt32(p.questId);
    return true;
}

bool DecodeQuestAbandonRequest(const std::uint8_t* data, std::size_t size,
                               QuestAbandonRequestPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.questId = r.ReadUInt32();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed QuestAbandonRequest payload";
        return false;
    }
    return true;
}

// QuestAbandonResponse(315)。
bool EncodeQuestAbandonResponse(const QuestAbandonResponsePayload& p,
                                std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt32(p.questId);
    w.WriteBool(p.success);
    w.WriteUInt8(p.resultCode);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeQuestAbandonResponse(const std::uint8_t* data, std::size_t size,
                                QuestAbandonResponsePayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.questId = r.ReadUInt32();
    out.success = r.ReadBool();
    out.resultCode = r.ReadUInt8();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed QuestAbandonResponse payload";
        return false;
    }
    return true;
}

// QuestProgressUpdated(316)。
bool EncodeQuestProgressUpdated(const QuestProgressUpdatedPayload& p,
                                std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt32(p.questId);
    w.WriteUInt32(p.objectiveId);
    w.WriteUInt32(p.current);
    w.WriteUInt32(p.required);
    w.WriteUInt8(p.questState);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeQuestProgressUpdated(const std::uint8_t* data, std::size_t size,
                                QuestProgressUpdatedPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.questId = r.ReadUInt32();
    out.objectiveId = r.ReadUInt32();
    out.current = r.ReadUInt32();
    out.required = r.ReadUInt32();
    out.questState = r.ReadUInt8();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed QuestProgressUpdated payload";
        return false;
    }
    return true;
}

// QuestStateChanged(317)。
bool EncodeQuestStateChanged(const QuestStateChangedPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt32(p.questId);
    w.WriteUInt8(p.oldState);
    w.WriteUInt8(p.newState);
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeQuestStateChanged(const std::uint8_t* data, std::size_t size,
                             QuestStateChangedPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.questId = r.ReadUInt32();
    out.oldState = r.ReadUInt8();
    out.newState = r.ReadUInt8();
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed QuestStateChanged payload";
        return false;
    }
    return true;
}

// QuestSnapshot(318)。Encode 侧同样执行上限截断保护（>256/>16 不写出）。
bool EncodeQuestSnapshot(const QuestSnapshotPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.characterId);
    const std::size_t questCount = std::min(p.quests.size(), kQuestSnapshotMaxQuests);
    w.WriteUInt16(static_cast<std::uint16_t>(questCount));
    for (std::size_t i = 0; i < questCount; ++i) {
        const auto& quest = p.quests[i];
        w.WriteUInt32(quest.questId);
        w.WriteUInt8(quest.state);
        const std::size_t objCount = std::min(quest.objectives.size(), kQuestMaxObjectives);
        w.WriteUInt8(static_cast<std::uint8_t>(objCount));
        for (std::size_t j = 0; j < objCount; ++j) {
            w.WriteUInt32(quest.objectives[j].objectiveId);
            w.WriteUInt32(quest.objectives[j].current);
            w.WriteUInt32(quest.objectives[j].required);
        }
    }
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeQuestSnapshot(const std::uint8_t* data, std::size_t size, QuestSnapshotPayload& out,
                         std::string& error) {
    ByteReader r(data, size);
    out.characterId = r.ReadUInt64();
    const std::uint16_t questCount = r.ReadUInt16();
    if (questCount > kQuestSnapshotMaxQuests) {
        // 指令八十三：count 超上限直接拒绝（粘滞失败）。
        r.Invalidate();
    }
    out.quests.clear();
    for (std::uint16_t i = 0; i < questCount && r.IsValid(); ++i) {
        QuestSnapshotEntryData quest;
        quest.questId = r.ReadUInt32();
        quest.state = r.ReadUInt8();
        const std::uint8_t objCount = r.ReadUInt8();
        if (objCount > kQuestMaxObjectives) {
            r.Invalidate();
            break;
        }
        for (std::uint8_t j = 0; j < objCount && r.IsValid(); ++j) {
            QuestSnapshotObjectiveData objective;
            objective.objectiveId = r.ReadUInt32();
            objective.current = r.ReadUInt32();
            objective.required = r.ReadUInt32();
            quest.objectives.push_back(objective);
        }
        if (!r.IsValid()) {
            break;
        }
        out.quests.push_back(std::move(quest));
    }
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed QuestSnapshot payload";
        return false;
    }
    return true;
}

// QuestRewardGranted(319)。
bool EncodeQuestRewardGranted(const QuestRewardGrantedPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt32(p.questId);
    w.WriteUInt32(p.exp);
    w.WriteUInt32(p.gold);
    w.WriteUInt32(p.itemDefinitionId);
    w.WriteUInt32(p.itemQuantity);
    w.WriteUInt32(p.newLevel);
    w.WriteUInt64(ToBits(p.newExperience));
    w.WriteUInt64(ToBits(p.newGold));
    w.WriteUInt64(p.serverTime);
    return true;
}

bool DecodeQuestRewardGranted(const std::uint8_t* data, std::size_t size,
                              QuestRewardGrantedPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.questId = r.ReadUInt32();
    out.exp = r.ReadUInt32();
    out.gold = r.ReadUInt32();
    out.itemDefinitionId = r.ReadUInt32();
    out.itemQuantity = r.ReadUInt32();
    out.newLevel = r.ReadUInt32();
    out.newExperience = FromBits(r.ReadUInt64());
    out.newGold = FromBits(r.ReadUInt64());
    out.serverTime = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed QuestRewardGranted payload";
        return false;
    }
    return true;
}

} // namespace legend::world
