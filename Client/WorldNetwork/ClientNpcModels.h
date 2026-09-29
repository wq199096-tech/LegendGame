#pragma once

#include "Shared/Dialogue/DialogueProtocol.h"
#include "Shared/Shop/ShopProtocol.h"
#include "Shared/Teleport/TeleportProtocol.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::client {

// ---------------------------------------------------------------------------
// 阶段20 指令七十九：ClientDialogueModel / ClientShopModel —— 客户端对话/商店
// 镜像（全部来自服务器事件：NpcInteractResponse/DialoguePayload/ShopOpenResponse）。
// Client 不决定内容（指令二）；仅展示 + 发回用户选择。
// ---------------------------------------------------------------------------
class ClientDialogueModel {
public:
    void Clear() { m_payload = world::DialoguePayload{}; }
    bool Active() const { return m_payload.dialogueSessionId != 0 && !m_payload.options.empty(); }
    std::uint64_t SessionId() const { return m_payload.dialogueSessionId; }
    std::uint64_t NpcEntityId() const { return m_payload.npcEntityId; }
    const std::string& Title() const { return m_payload.title; }
    const std::string& Text() const { return m_payload.text; }
    const std::vector<world::DialogueOptionData>& Options() const { return m_payload.options; }

    // DialoguePayload(325)：全量覆盖（options 为空 = 关闭指令）。
    void Apply(const world::DialoguePayload& payload) { m_payload = payload; }
    // 指令八十：数字键 1~9 选择 Option（返回是否发出）。
    bool FindOptionByIndex(std::size_t oneBased, world::DialogueOptionData& out) const {
        if (oneBased == 0 || oneBased > m_payload.options.size()) {
            return false;
        }
        out = m_payload.options[oneBased - 1];
        return true;
    }

private:
    world::DialoguePayload m_payload;
};

class ClientShopModel {
public:
    void Clear() {
        m_sessionId = 0;
        m_shopId = 0;
        m_npcEntityId = 0;
        m_entries.clear();
    }
    bool Active() const { return m_sessionId != 0; }
    std::uint64_t SessionId() const { return m_sessionId; }
    std::uint32_t ShopId() const { return m_shopId; }
    std::uint64_t NpcEntityId() const { return m_npcEntityId; }
    const std::vector<world::ShopEntryData>& Entries() const { return m_entries; }

    // ShopOpenResponse(328)：全量覆盖。
    void Apply(const world::ShopOpenResponsePayload& payload) {
        m_sessionId = payload.shopSessionId;
        m_shopId = payload.shopId;
        m_npcEntityId = payload.npcEntityId;
        m_entries = payload.entries;
    }
    // 指令八十一：数字键选择条目。
    bool FindEntryByIndex(std::size_t oneBased, world::ShopEntryData& out) const {
        if (oneBased == 0 || oneBased > m_entries.size()) {
            return false;
        }
        out = m_entries[oneBased - 1];
        return true;
    }

private:
    std::uint64_t m_sessionId = 0;
    std::uint32_t m_shopId = 0;
    std::uint64_t m_npcEntityId = 0;
    std::vector<world::ShopEntryData> m_entries;
};

} // namespace legend::client
