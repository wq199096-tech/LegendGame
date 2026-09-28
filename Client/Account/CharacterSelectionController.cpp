#include "Client/Account/CharacterSelectionController.h"

#include "Engine/Debug/Logger.h"

namespace legend::client {

void CharacterSelectionController::RequestSelect(AccountClientController& account,
                                                 std::uint64_t characterId) {
    if (account.SessionToken().empty()) {
        LOG_WARN("[CharacterSelect] no session token; login first");
        return;
    }
    m_characterId = characterId;
    m_pending = true;
    m_selected = false;
    m_ticket.clear();
    account.SendSelectCharacter(account.SessionToken(), characterId);
}

void CharacterSelectionController::Update(AccountClientController& account) {
    if (!m_pending) {
        return;
    }
    if (account.State() == AccountFlowState::CharacterSelected &&
        account.SelectedCharacterId() == m_characterId) {
        m_selected = true;
        m_pending = false;
        m_ticket = account.SelectionTicket();
        return;
    }
    if (account.State() == AccountFlowState::CharacterListReady) {
        // 选择失败（CharacterNotFound/NotOwned 等）-> 控制器已回到列表态
        m_pending = false;
    }
}

} // namespace legend::client
