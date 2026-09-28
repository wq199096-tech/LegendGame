#pragma once

#include "Client/Account/AccountClientController.h"

#include <cstdint>
#include <string>

namespace legend::client {

// 阶段10 指令四十九~五十三：CharacterSelectionController——角色选择流程封装。
// 只编排 AccountClientController 的请求与状态，不触碰 GameScene/World。
class CharacterSelectionController {
public:
    // 发送选择请求（token 取自 AccountClientController 会话状态）。
    void RequestSelect(AccountClientController& account, std::uint64_t characterId);

    // 每帧调用：跟随账号状态推进（CharacterSelected 时记录结果）。
    void Update(AccountClientController& account);

    bool IsSelected() const { return m_selected; }
    bool Pending() const { return m_pending; }
    std::uint64_t CharacterId() const { return m_characterId; }
    const std::string& Ticket() const { return m_ticket; }

private:
    bool m_selected = false;
    bool m_pending = false;
    std::uint64_t m_characterId = 0;
    std::string m_ticket;
};

} // namespace legend::client
