#pragma once

#include "Shared/Quest/QuestTypes.h"

#include <cstdint>
#include <unordered_map>

namespace legend::client {

// 阶段19：Shared 任务类型引入 client 命名空间。
using world::QuestId;
using world::QuestState;

// ---------------------------------------------------------------------------
// 阶段19 指令四十八：ClientQuestState —— 单个任务的客户端镜像状态。
// Client 不是任务真相（指令四十九）：状态/进度只来自 QuestProgressUpdated /
// QuestStateChanged / QuestSnapshot，绝不本地杀怪 +1。
// ---------------------------------------------------------------------------
struct ClientQuestState {
    QuestId questId = 0;
    QuestState state = QuestState::NotAccepted;

    struct ObjectiveProgress {
        std::uint32_t current = 0;
        std::uint32_t required = 0;
    };
    // key = objectiveId。
    std::unordered_map<std::uint32_t, ObjectiveProgress> objectives;
};

} // namespace legend::client
