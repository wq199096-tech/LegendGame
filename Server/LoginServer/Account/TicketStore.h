#pragma once

#include <chrono>
#include <cstdint>
#include <mutex>
#include <string>
#include <unordered_map>

namespace legend::account {

// 阶段10 指令五十一/五十二/五十三：SelectionTicket（内存 TicketStore）。
// - 256-bit 随机字符串，默认 60 秒有效期
// - 一次性：WorldServer 将来只能消费一次（ConsumeTicket）
// - 数据库只存 hash；本阶段无 WorldServer 调用方
class TicketStore {
public:
    // 生成 ticket；ttlSeconds <= 0 仅用于测试短有效期。
    std::string Create(std::uint64_t accountId, std::uint64_t characterId,
                       double ttlSeconds = 60.0);
    // 校验：未消费 + 未过期 + account/character 匹配。
    bool Validate(const std::string& ticket, std::uint64_t accountId,
                  std::uint64_t characterId) const;
    // 一次性消费：成功后同一 ticket 再次 Consume 失败。
    bool Consume(const std::string& ticket, std::uint64_t accountId, std::uint64_t characterId);

private:
    struct TicketEntry {
        std::uint64_t accountId = 0;
        std::uint64_t characterId = 0;
        std::chrono::steady_clock::time_point expiresAt{};
        bool consumed = false;
    };

    void ClearExpiredLocked();

    mutable std::mutex m_mutex;
    std::unordered_map<std::string, TicketEntry> m_entries; // key = ticketHash
};

} // namespace legend::account
