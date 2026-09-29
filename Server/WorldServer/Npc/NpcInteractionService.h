#pragma once

#include "Server/WorldServer/Npc/NpcEntity.h"
#include "Shared/Dialogue/DialogueProtocol.h"
#include "Shared/Npc/NpcProtocol.h"
#include "Shared/Quest/QuestTypes.h"

#include <chrono>
#include <cstdint>
#include <vector>

namespace legend::world {

class PlayerSession;
class NpcEntity;
class NpcDefinition;

// ---------------------------------------------------------------------------
// 阶段20 指令十九/二十二/二十七/三十一/三十二：NpcInteractionService —— NPC 交互
// 纯逻辑（验证/菜单生成/Marker 计算）。WorldServer 负责编排（会话存储/网络广播）。
// ---------------------------------------------------------------------------
class NpcInteractionService {
public:
    // 指令十九：交互验证链（Player 存在/Alive 由调用方先行；此处 NPC 侧 + 距离）。
    // 服务器权威距离（距离 <= interactionRange）。
    static NpcResultCode ValidateInteraction(const PlayerSession& player, const NpcEntity& npc,
                                             std::chrono::steady_clock::time_point now);

    // 指令二十二：Dialogue Session 有效性（存在 + TTL（可配置，默认 30s）+ NPC 仍
    // active + 同图 + 距离仍合法）。
    static NpcResultCode ValidateDialogueSession(const PlayerSession& player,
                                                 std::uint64_t dialogueSessionId,
                                                 const NpcEntity& npc, double ttlSeconds,
                                                 std::chrono::steady_clock::time_point now);

    // 指令二十七：Village Elder 等任务 NPC 菜单按玩家任务状态动态生成
    //（不显示不满足前置的 Accept；ReadyToTurnIn 才显示 TurnIn）。
    static DialoguePayload BuildQuestDialogue(const PlayerSession& player, const NpcEntity& npc,
                                              std::uint64_t dialogueSessionId);

    // 通用菜单（Merchant/Teleporter/MultiFunction：Shop/Teleport/Quest/Close）。
    static DialoguePayload BuildDialogue(const PlayerSession& player, const NpcEntity& npc,
                                         std::uint64_t dialogueSessionId);

    // 指令三十一/三十二：NpcQuestMarker —— per-player 计算。
    // ReadyToTurnIn > Available > InProgress > None（仅统计与该 NPC 关联的任务）。
    static NpcQuestMarker ComputeQuestMarker(const PlayerSession& player,
                                             const NpcDefinition& npc);
};

} // namespace legend::world
