#include "Server/LoginServer/Account/TicketStore.h"

#include "Server/LoginServer/Account/PasswordHasher.h"

namespace legend::account {

std::string TicketStore::Create(std::uint64_t accountId, std::uint64_t characterId,
                                double ttlSeconds) {
    const std::string ticket = GenerateTokenHex(32); // 256-bit（指令五十一）
    if (ticket.empty()) {
        return {};
    }
    TicketEntry entry;
    entry.accountId = accountId;
    entry.characterId = characterId;
    entry.expiresAt = std::chrono::steady_clock::now() +
                      std::chrono::duration_cast<std::chrono::steady_clock::duration>(
                          std::chrono::duration<double>(ttlSeconds));
    entry.consumed = false;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ClearExpiredLocked();
        m_entries.emplace(Sha256Hex(ticket), std::move(entry));
    }
    return ticket;
}

bool TicketStore::Validate(const std::string& ticket, std::uint64_t accountId,
                           std::uint64_t characterId) const {
    if (ticket.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_entries.find(Sha256Hex(ticket));
    if (it == m_entries.end()) {
        return false;
    }
    const TicketEntry& entry = it->second;
    if (entry.consumed) {
        return false;
    }
    if (std::chrono::steady_clock::now() >= entry.expiresAt) {
        return false;
    }
    return entry.accountId == accountId && entry.characterId == characterId;
}

bool TicketStore::Consume(const std::string& ticket, std::uint64_t accountId,
                          std::uint64_t characterId) {
    if (ticket.empty()) {
        return false;
    }
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_entries.find(Sha256Hex(ticket));
    if (it == m_entries.end()) {
        return false;
    }
    TicketEntry& entry = it->second;
    if (entry.consumed || std::chrono::steady_clock::now() >= entry.expiresAt ||
        entry.accountId != accountId || entry.characterId != characterId) {
        return false;
    }
    entry.consumed = true; // 指令五十三：ticket 只能消费一次
    return true;
}

void TicketStore::ClearExpiredLocked() {
    const auto now = std::chrono::steady_clock::now();
    for (auto it = m_entries.begin(); it != m_entries.end();) {
        if (it->second.consumed || now >= it->second.expiresAt) {
            it = m_entries.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace legend::account
