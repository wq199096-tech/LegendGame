#pragma once

#include "Shared/Quest/QuestTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段19 指令十九/三十/三十一/三十三/四十一/四十五/八十一/八十三：
// Quest 协议 payload。MessageId：QuestAcceptRequest=310 / QuestAcceptResponse=311 /
// QuestTurnInRequest=312 / QuestTurnInResponse=313 / QuestAbandonRequest=314 /
// QuestAbandonResponse=315 / QuestProgressUpdated=316 / QuestStateChanged=317 /
// QuestSnapshot=318 / QuestRewardGranted=319。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0。
// Client 只能发 Accept/TurnIn/Abandon 三种请求（指令二），绝不能上报进度/状态/奖励。
// ---------------------------------------------------------------------------

// QuestAcceptRequest(310)（指令十九）。
struct QuestAcceptRequestPayload {
    std::uint64_t requestId = 0;
    std::uint32_t questId = 0;
};

// QuestAcceptResponse(311)。
struct QuestAcceptResponsePayload {
    std::uint64_t requestId = 0;
    std::uint32_t questId = 0;
    bool success = false;
    std::uint8_t resultCode = 0; // QuestResultCode
    std::uint64_t serverTime = 0;
};

// QuestTurnInRequest(312)（指令三十三）。
struct QuestTurnInRequestPayload {
    std::uint64_t requestId = 0;
    std::uint32_t questId = 0;
};

// QuestTurnInResponse(313)。
struct QuestTurnInResponsePayload {
    std::uint64_t requestId = 0;
    std::uint32_t questId = 0;
    bool success = false;
    std::uint8_t resultCode = 0; // QuestResultCode
    std::uint64_t serverTime = 0;
};

// QuestAbandonRequest(314)（指令四十三）。
struct QuestAbandonRequestPayload {
    std::uint64_t requestId = 0;
    std::uint32_t questId = 0;
};

// QuestAbandonResponse(315)。
struct QuestAbandonResponsePayload {
    std::uint64_t requestId = 0;
    std::uint32_t questId = 0;
    bool success = false;
    std::uint8_t resultCode = 0; // QuestResultCode
    std::uint64_t serverTime = 0;
};

// QuestProgressUpdated(316)（指令三十：只发本人）。
struct QuestProgressUpdatedPayload {
    std::uint32_t questId = 0;
    std::uint32_t objectiveId = 0;
    std::uint32_t current = 0;
    std::uint32_t required = 0;
    std::uint8_t questState = 0; // QuestState
    std::uint64_t serverTime = 0;
};

// QuestStateChanged(317)（指令三十一：只发本人）。
struct QuestStateChangedPayload {
    std::uint32_t questId = 0;
    std::uint8_t oldState = 0; // QuestState
    std::uint8_t newState = 0;
    std::uint64_t serverTime = 0;
};

// QuestSnapshot(318)（指令四十五：进世界下发 + 每 10s 本人纠偏）。
struct QuestSnapshotObjectiveData {
    std::uint32_t objectiveId = 0;
    std::uint32_t current = 0;
    std::uint32_t required = 0;
};

struct QuestSnapshotEntryData {
    std::uint32_t questId = 0;
    std::uint8_t state = 0; // QuestState
    std::vector<QuestSnapshotObjectiveData> objectives;
};

struct QuestSnapshotPayload {
    std::uint64_t characterId = 0;
    std::vector<QuestSnapshotEntryData> quests;
    std::uint64_t serverTime = 0;
};

// QuestRewardGranted(319)（指令四十一：只发本人）。
struct QuestRewardGrantedPayload {
    std::uint32_t questId = 0;
    std::uint32_t exp = 0;
    std::uint32_t gold = 0;
    std::uint32_t itemDefinitionId = 0; // 0 = 无
    std::uint32_t itemQuantity = 0;
    std::uint32_t newLevel = 1;
    std::int64_t newExperience = 0;
    std::int64_t newGold = 0;
    std::uint64_t serverTime = 0;
};

bool EncodeQuestAcceptRequest(const QuestAcceptRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeQuestAcceptRequest(const std::uint8_t* data, std::size_t size,
                              QuestAcceptRequestPayload& out, std::string& error);
bool EncodeQuestAcceptResponse(const QuestAcceptResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeQuestAcceptResponse(const std::uint8_t* data, std::size_t size,
                               QuestAcceptResponsePayload& out, std::string& error);
bool EncodeQuestTurnInRequest(const QuestTurnInRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeQuestTurnInRequest(const std::uint8_t* data, std::size_t size,
                              QuestTurnInRequestPayload& out, std::string& error);
bool EncodeQuestTurnInResponse(const QuestTurnInResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeQuestTurnInResponse(const std::uint8_t* data, std::size_t size,
                               QuestTurnInResponsePayload& out, std::string& error);
bool EncodeQuestAbandonRequest(const QuestAbandonRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeQuestAbandonRequest(const std::uint8_t* data, std::size_t size,
                               QuestAbandonRequestPayload& out, std::string& error);
bool EncodeQuestAbandonResponse(const QuestAbandonResponsePayload& p,
                                std::vector<std::uint8_t>& out);
bool DecodeQuestAbandonResponse(const std::uint8_t* data, std::size_t size,
                                QuestAbandonResponsePayload& out, std::string& error);
bool EncodeQuestProgressUpdated(const QuestProgressUpdatedPayload& p,
                                std::vector<std::uint8_t>& out);
bool DecodeQuestProgressUpdated(const std::uint8_t* data, std::size_t size,
                                QuestProgressUpdatedPayload& out, std::string& error);
bool EncodeQuestStateChanged(const QuestStateChangedPayload& p, std::vector<std::uint8_t>& out);
bool DecodeQuestStateChanged(const std::uint8_t* data, std::size_t size,
                             QuestStateChangedPayload& out, std::string& error);
bool EncodeQuestSnapshot(const QuestSnapshotPayload& p, std::vector<std::uint8_t>& out);
bool DecodeQuestSnapshot(const std::uint8_t* data, std::size_t size, QuestSnapshotPayload& out,
                         std::string& error);
bool EncodeQuestRewardGranted(const QuestRewardGrantedPayload& p, std::vector<std::uint8_t>& out);
bool DecodeQuestRewardGranted(const std::uint8_t* data, std::size_t size,
                              QuestRewardGrantedPayload& out, std::string& error);

} // namespace legend::world
