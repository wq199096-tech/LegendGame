#include "Client/Ui/ChatModel.h"

#include <algorithm>

namespace legend::client {

namespace {
constexpr std::size_t kMaxPerChannel = 200; // 指令二十四
constexpr std::size_t kMaxCombined = 500;   // 指令二十四
constexpr std::size_t kMaxInputHistory = 20; // 指令二十五
constexpr std::size_t kChatDraftMaxCodePoints = 160; // 略大于服务器 120 上限，允许超发被拒
} // namespace

void ChatModel::AddMessage(const ChatEntry& entry) {
    m_all.push_back(entry);
    while (m_all.size() > kMaxCombined) {
        m_all.pop_front(); // 综合 FIFO（指令二十四）
    }
    auto& bucket = m_byChannel[ChatTabForChannel(entry.channel)];
    bucket.push_back(entry);
    while (bucket.size() > kMaxPerChannel) {
        bucket.pop_front(); // 频道 FIFO
    }
    // 新消息到达：若在贴底（未向上翻页）保持贴底。
    if (m_scrollOffset == 0) {
        // no-op（贴底语义）
    }
}

std::vector<const ChatEntry*> ChatModel::VisibleMessages(std::uint8_t tab) const {
    std::vector<const ChatEntry*> out;
    if (tab == 0) {
        out.reserve(m_all.size());
        for (const auto& entry : m_all) {
            out.push_back(&entry);
        }
    } else if (tab <= 4) {
        out.reserve(m_byChannel[tab].size());
        for (const auto& entry : m_byChannel[tab]) {
            out.push_back(&entry);
        }
    }
    return out;
}

void ChatModel::OpenInput(std::uint8_t channel, const std::string& whisperTarget) {
    m_inputOpen = true;
    m_draftChannel = channel;
    m_whisperTarget = whisperTarget;
    m_historyCursor = m_inputHistory.size(); // 离开历史浏览态
    m_historyDraftBackup.clear();
}

void ChatModel::CloseInput() {
    m_inputOpen = false;
    m_draft.clear();
    m_whisperTarget.clear();
    m_historyCursor = m_inputHistory.size();
    m_historyDraftBackup.clear();
}

void ChatModel::FeedTextInput(const std::vector<std::string>& utf8Chunks, bool backspace) {
    if (!m_inputOpen) {
        return;
    }
    if (backspace) {
        legend::flow::FlowTextBackspace(m_draft);
    }
    for (const auto& chunk : utf8Chunks) {
        legend::flow::FlowTextAppend(m_draft, chunk, kChatDraftMaxCodePoints);
    }
}

bool ChatModel::ParseCommand(chat::ChatChannel& outChannel, std::string& outTarget,
                             std::string& outText) const {
    std::string text = m_draft;
    chat::ChatChannel channel = static_cast<chat::ChatChannel>(m_draftChannel);
    std::string target = m_whisperTarget;
    // 指令二十六：/n -> 附近；/w 玩家名 -> 私聊；/world -> 世界。
    if (chat::ParseChatCommand(m_draft, channel, target, text)) {
        outChannel = channel;
        outTarget = target;
        outText = text;
        return true;
    }
    outChannel = channel;
    outTarget = target;
    outText = text;
    return m_draft.empty() || m_draft[0] != '/';
}

void ChatModel::CommitDraftToHistory() {
    if (m_draft.empty()) {
        return;
    }
    // 去重相邻重复（不重复入历史）。
    if (m_inputHistory.empty() || m_inputHistory.back() != m_draft) {
        m_inputHistory.push_back(m_draft);
        while (m_inputHistory.size() > kMaxInputHistory) {
            m_inputHistory.erase(m_inputHistory.begin()); // 最近 20 条 FIFO
        }
    }
    m_draft.clear();
    m_historyCursor = m_inputHistory.size();
    m_historyDraftBackup.clear();
}

void ChatModel::NavigateHistory(int direction) {
    if (m_inputHistory.empty()) {
        return;
    }
    if (direction > 0) {
        // ↑：向更早的历史回溯。
        if (m_historyCursor == m_inputHistory.size()) {
            m_historyDraftBackup = m_draft; // 备份当前草稿
        }
        if (m_historyCursor > 0) {
            --m_historyCursor;
            m_draft = m_inputHistory[m_historyCursor];
        }
    } else {
        // ↓：向新方向。
        if (m_historyCursor < m_inputHistory.size()) {
            ++m_historyCursor;
            if (m_historyCursor == m_inputHistory.size()) {
                m_draft = m_historyDraftBackup; // 回到备份草稿
            } else {
                m_draft = m_inputHistory[m_historyCursor];
            }
        }
    }
}

} // namespace legend::client
