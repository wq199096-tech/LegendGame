#include "Server/WorldServer/Npc/NpcInteractionService.h"

#include "Server/WorldServer/Npc/NpcRegistry.h"
#include "Server/WorldServer/Npc/ShopService.h"
#include "Server/WorldServer/Npc/TeleportService.h"
#include "Server/WorldServer/PlayerSession.h"
#include "Server/WorldServer/Quest/QuestRegistry.h"
#include "Shared/Npc/NpcError.h"

#include <cmath>

namespace legend::world {

namespace {

// NPC 关联任务 = NpcDefinition.questIds ∪ QuestRegistry 中 start/turnIn 指向该 NPC 的任务。
std::vector<QuestId> RelatedQuests(const NpcDefinition& npc) {
    std::vector<QuestId> quests = npc.questIds;
    for (const auto& definition : QuestRegistry::Instance().AllQuests()) {
        bool related = false;
        for (const QuestId existing : quests) {
            if (existing == definition.questId) {
                related = true;
                break;
            }
        }
        if (!related && (definition.startNpcDefinitionId == npc.npcDefinitionId ||
                         definition.turnInNpcDefinitionId == npc.npcDefinitionId)) {
            quests.push_back(definition.questId);
        }
    }
    return quests;
}

} // namespace

NpcResultCode NpcInteractionService::ValidateInteraction(const PlayerSession& player,
                                                         const NpcEntity& npc,
                                                         std::chrono::steady_clock::time_point now) {
    (void)now;
    if (!npc.Active()) {
        return NpcResultCode::NpcNotActive; // 指令十九：NPC 存在且 active
    }
    if (player.MapId() != npc.MapId()) {
        return NpcResultCode::WrongMap; // 指令十九：same map
    }
    // 指令十九：visibleNpcs 由服务器 AOI 权威维护（防远程作弊）。
    if (player.VisibleNpcs().count(npc.EntityId()) == 0) {
        return NpcResultCode::NotVisible;
    }
    // 指令十九：服务器权威距离 <= interactionRange。
    const float dx = npc.X() - player.PositionX();
    const float dy = npc.Y() - player.PositionY();
    const NpcDefinition* definition = npc.Definition();
    const float range = definition ? definition->interactionRange : kNpcDefaultInteractionRange;
    if (dx * dx + dy * dy > range * range) {
        return NpcResultCode::TooFar;
    }
    return NpcResultCode::Success;
}

NpcResultCode NpcInteractionService::ValidateDialogueSession(
    const PlayerSession& player, std::uint64_t dialogueSessionId, const NpcEntity& npc,
    double ttlSeconds, std::chrono::steady_clock::time_point now) {
    const auto& session = player.DialogueSession();
    if (session.sessionId == 0 || session.sessionId != dialogueSessionId) {
        return NpcResultCode::SessionNotFound; // 指令二十二
    }
    if (session.npcEntityId != npc.EntityId()) {
        return NpcResultCode::SessionNotFound;
    }
    // 指令二十二：超过 TTL（默认 30s）失效。
    const auto elapsed = std::chrono::duration<double>(now - session.openedAt).count();
    if (elapsed > ttlSeconds) {
        return NpcResultCode::SessionExpired;
    }
    if (!npc.Active()) {
        return NpcResultCode::SessionExpired;
    }
    if (player.MapId() != npc.MapId()) {
        return NpcResultCode::SessionExpired;
    }
    // 指令二十二：玩家走远（超出 interactionRange）失效。
    const float dx = npc.X() - player.PositionX();
    const float dy = npc.Y() - player.PositionY();
    const NpcDefinition* definition = npc.Definition();
    const float range = definition ? definition->interactionRange : kNpcDefaultInteractionRange;
    if (dx * dx + dy * dy > range * range) {
        return NpcResultCode::TooFar;
    }
    return NpcResultCode::Success;
}

DialoguePayload NpcInteractionService::BuildQuestDialogue(const PlayerSession& player,
                                                          const NpcEntity& npc,
                                                          std::uint64_t dialogueSessionId) {
    const NpcDefinition* definition = npc.Definition();
    DialoguePayload payload;
    payload.dialogueSessionId = dialogueSessionId;
    payload.npcEntityId = npc.EntityId();
    const DialogueDefinition* dialogue =
        definition ? NpcRegistry::Instance().FindDialogue(definition->dialogueId) : nullptr;
    payload.title = dialogue ? dialogue->title : "NPC";
    payload.text = dialogue ? dialogue->text : "";

    std::uint32_t optionId = 1;
    const auto& registry = QuestRegistry::Instance();
    for (const QuestId questId : RelatedQuests(*definition)) {
        const QuestDefinition* quest = registry.FindQuest(questId);
        if (quest == nullptr) {
            continue;
        }
        const PlayerQuestState* state = player.Quests().Find(questId);
        const bool accepted =
            state != nullptr && (state->state == QuestState::InProgress ||
                                 state->state == QuestState::ReadyToTurnIn);
        const bool completed = player.Quests().IsCompleted(questId);
        // 指令三十：ReadyToTurnIn 才显示 TurnIn。
        if (!completed && state != nullptr && state->state == QuestState::ReadyToTurnIn &&
            quest->turnInNpcDefinitionId == definition->npcDefinitionId) {
            DialogueOptionData option;
            option.optionId = optionId++;
            option.type = static_cast<std::uint8_t>(DialogueOptionType::Quest);
            option.referenceId = questId;
            option.label = "Turn In " + quest->name;
            payload.options.push_back(std::move(option));
            continue;
        }
        // 指令二十七：接取选项只在（未接取/未完成 且 前置满足 且 等级满足 且 本 NPC 是 start）时显示；
        // 不满足前置直接不显示（阶段20 建议）。
        if (!accepted && !completed && quest->startNpcDefinitionId == definition->npcDefinitionId &&
            (quest->prerequisiteQuestId == 0 || player.Quests().IsCompleted(quest->prerequisiteQuestId)) &&
            player.Level() >= quest->minLevel) {
            DialogueOptionData option;
            option.optionId = optionId++;
            option.type = static_cast<std::uint8_t>(DialogueOptionType::Quest);
            option.referenceId = questId;
            option.label = "Accept " + quest->name;
            payload.options.push_back(std::move(option));
        }
    }
    // Shop Option（Merchant）。
    if (definition != nullptr && definition->shopId != 0 &&
        ShopRegistry::Instance().FindShop(definition->shopId) != nullptr) {
        DialogueOptionData option;
        option.optionId = optionId++;
        option.type = static_cast<std::uint8_t>(DialogueOptionType::Shop);
        option.referenceId = definition->shopId;
        option.label = "Browse Shop";
        payload.options.push_back(std::move(option));
    }
    // Teleport Option。
    if (definition != nullptr && definition->teleportId != 0) {
        const TeleportDefinition* teleport =
            TeleportRegistry::Instance().FindTeleport(definition->teleportId);
        if (teleport != nullptr) {
            DialogueOptionData option;
            option.optionId = optionId++;
            option.type = static_cast<std::uint8_t>(DialogueOptionType::Teleport);
            option.referenceId = teleport->teleportId;
            option.label = "Teleport: " + teleport->name +
                           (teleport->goldCost > 0
                                ? " (" + std::to_string(teleport->goldCost) + "G)"
                                : " (Free)");
            payload.options.push_back(std::move(option));
        }
    }
    // Close 常驻。
    DialogueOptionData close;
    close.optionId = optionId++;
    close.type = static_cast<std::uint8_t>(DialogueOptionType::Close);
    close.referenceId = 0;
    close.label = "Close";
    payload.options.push_back(std::move(close));
    return payload;
}

DialoguePayload NpcInteractionService::BuildDialogue(const PlayerSession& player,
                                                     const NpcEntity& npc,
                                                     std::uint64_t dialogueSessionId) {
    // 阶段20：全部 NPC 走同一动态菜单生成（任务状态/商店/传送按定义自动组合）。
    return BuildQuestDialogue(player, npc, dialogueSessionId);
}

NpcQuestMarker NpcInteractionService::ComputeQuestMarker(const PlayerSession& player,
                                                         const NpcDefinition& npc) {
    // 指令三十二：ReadyToTurnIn > Available > InProgress > None。
    const auto& registry = QuestRegistry::Instance();
    NpcQuestMarker marker = NpcQuestMarker::None;
    for (const QuestId questId : RelatedQuests(npc)) {
        const QuestDefinition* quest = registry.FindQuest(questId);
        if (quest == nullptr) {
            continue;
        }
        const PlayerQuestState* state = player.Quests().Find(questId);
        const bool completed = player.Quests().IsCompleted(questId);
        if (state != nullptr && state->state == QuestState::ReadyToTurnIn &&
            quest->turnInNpcDefinitionId == npc.npcDefinitionId) {
            return NpcQuestMarker::ReadyToTurnIn; // 最高优先级
        }
        if (!completed && state == nullptr &&
            quest->startNpcDefinitionId == npc.npcDefinitionId &&
            (quest->prerequisiteQuestId == 0 || player.Quests().IsCompleted(quest->prerequisiteQuestId)) &&
            player.Level() >= quest->minLevel) {
            marker = NpcQuestMarker::Available; // 次优先级
            continue;
        }
        if (state != nullptr && state->state == QuestState::InProgress &&
            marker == NpcQuestMarker::None) {
            marker = NpcQuestMarker::InProgress; // 最低优先级
        }
    }
    return marker;
}

} // namespace legend::world
