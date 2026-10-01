#pragma once

#include <atomic>
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
// 阶段11：ConsumeForWorld 供 WorldServer 经 LoginServer 消费（返回绑定身份）。
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

    // 阶段11 指令十二/十三/十四：World 消费入口（经 LoginServer 内部协议调用）。
    struct ConsumeOutcome {
        bool success = false;
        enum class Failure {
            NotFound,   // ticket 不存在（含已过期清理后）
            Expired,    // 已过期
            Consumed,   // 已被消费（重放）
        } failure = Failure::NotFound;
        std::uint64_t accountId = 0;
        std::uint64_t characterId = 0;
    };
    // 一次性消费：校验未过期 + 未消费，成功返回 ticket 绑定的 accountId/characterId。
    ConsumeOutcome ConsumeForWorld(const std::string& ticket);

    // Stage25.6 服务器管理台只读统计（原子计数，GUI 线程可随时读）
    std::uint64_t IssuedCount() const { return m_issued.load(std::memory_order_relaxed); }
    std::uint64_t ConsumedCount() const { return m_consumed.load(std::memory_order_relaxed); }
    std::size_t LiveCount() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_entries.size();
    }

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
    std::atomic<std::uint64_t> m_issued{0};   // Stage25.6 管理台埋点
    std::atomic<std::uint64_t> m_consumed{0};
};

} // namespace legend::account
