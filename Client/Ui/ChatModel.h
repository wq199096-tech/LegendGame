#pragma once

// ---------------------------------------------------------------------------
// Stage27 指令十九~二十六：ChatModel —— 聊天窗口纯数据模型（LegendClientUi，
// 不依赖 SDL/OpenGL；测试可脱离窗口系统验证）。
//   - 历史：每频道 200 条 / 综合 500 条 FIFO（指令二十四；只存内存，不入 SQLite）
//   - 输入：UTF-8 码点安全（复用 FlowTextAppend/FlowTextBackspace）
//   - 命令：/n 附近、/w 玩家名 私聊、/world 世界（指令二十六；/s 禁止）
//   - 输入历史：最近 20 条，↑↓ 切换（指令二十五）
//   - 点击名字 -> 私聊小菜单状态（指令二十三）
// 服务器权威：sender/channel 校验在服务器；本地只做展示与命令预解析。
// ---------------------------------------------------------------------------

#include "Client/Ui/FlowUiModel.h" // FlowTextAppend/FlowTextBackspace
#include "Shared/Chat/ChatConstants.h"

#include <cstdint>
#include <deque>
#include <string>
#include <vector>

namespace legend::client {

struct ChatEntry {
    std::uint8_t channel = static_cast<std::uint8_t>(chat::ChatChannel::System);
    std::string senderName;   // System 消息为空
    std::string targetName;   // 仅私聊
    std::string text;
    std::uint64_t timestamp = 0;
    bool isError = false;     // 本地/服务器错误提示（红色，指令二十一）
};

// 频道 -> 标签下标（0 综合 / 1 附近 / 2 世界 / 3 私聊 / 4 系统）。
inline std::uint8_t ChatTabForChannel(std::uint8_t channel) {
    return channel >= 1 && channel <= 4 ? channel : 0;
}

class ChatModel {
public:
    // ---- 历史（指令二十四：每频道 200 / 综合 500 FIFO）----
    void AddMessage(const ChatEntry& entry);
    const std::deque<ChatEntry>& AllMessages() const { return m_all; }
    // tab: 0=综合 1~4=频道；返回该标签可见消息（综合 = 全部）。
    std::vector<const ChatEntry*> VisibleMessages(std::uint8_t tab) const;

    // ---- 展开/收起（指令二十：默认小尺寸，点击展开）----
    bool Expanded() const { return m_expanded; }
    void SetExpanded(bool expanded) { m_expanded = expanded; }
    void ToggleExpanded() { m_expanded = !m_expanded; }

    // ---- 标签页 ----
    std::uint8_t ActiveTab() const { return m_activeTab; }
    void SetActiveTab(std::uint8_t tab) { m_activeTab = tab; }

    // ---- 滚动（0 = 贴底最新；>0 = 向上翻过的行数）----
    int ScrollOffset() const { return m_scrollOffset; }
    void ScrollBy(int lines) { m_scrollOffset = m_scrollOffset + lines < 0 ? 0 : m_scrollOffset + lines; }
    void ResetScroll() { m_scrollOffset = 0; }

    // ---- 输入（指令二十：Enter 打开 -> Enter 发送 -> Esc 取消）----
    bool InputOpen() const { return m_inputOpen; }
    void OpenInput(std::uint8_t channel, const std::string& whisperTarget = std::string());
    void CloseInput();
    const std::string& Draft() const { return m_draft; }
    std::uint8_t DraftChannel() const { return m_draftChannel; }
    const std::string& WhisperTarget() const { return m_whisperTarget; }
    // IME 提交文本吸收（码点安全；组合串由渲染层拼接，不进 draft）。
    void FeedTextInput(const std::vector<std::string>& utf8Chunks, bool backspace);

    // 指令二十六：命令预解析（/n /w 玩家名 /world；/s 不允许）。
    // 返回 true = draft 已解析出目标频道/私聊目标/正文（draft 不变）。
    // 返回 false = 无命令前缀（用当前频道发送）或命令残缺。
    bool ParseCommand(chat::ChatChannel& outChannel, std::string& outTarget,
                      std::string& outText) const;
    // 发送成功后调用：写输入历史并清空 draft（指令二十五：最近 20 条）。
    void CommitDraftToHistory();
    // ↑↓ 历史切换（在历史中回溯/回到草稿）。返回当前 draft 应显示内容。
    void NavigateHistory(int direction);
    bool HistoryBrowsing() const { return m_historyCursor != m_inputHistory.size(); }

    // ---- 点击名字私聊（指令二十三：弹小菜单 -> 私聊）----
    bool WhisperMenuOpen() const { return m_whisperMenuOpen; }
    const std::string& WhisperMenuTarget() const { return m_whisperMenuTarget; }
    void OpenWhisperMenu(const std::string& senderName) {
        m_whisperMenuOpen = !senderName.empty();
        m_whisperMenuTarget = senderName;
    }
    void CloseWhisperMenu() { m_whisperMenuOpen = false; m_whisperMenuTarget.clear(); }

    // 默认发送频道（Enter 直接打开时使用；第一版默认附近）。
    std::uint8_t DefaultChannel() const { return m_defaultChannel; }
    void SetDefaultChannel(std::uint8_t channel) { m_defaultChannel = channel; }

private:
    std::deque<ChatEntry> m_all;          // 综合（500）
    std::deque<ChatEntry> m_byChannel[5]; // 0 unused, 1~4 频道（200）
    bool m_expanded = false;
    std::uint8_t m_activeTab = 0;         // 综合
    int m_scrollOffset = 0;
    bool m_inputOpen = false;
    std::uint8_t m_draftChannel = static_cast<std::uint8_t>(chat::ChatChannel::Nearby);
    std::string m_whisperTarget;
    std::string m_draft;
    std::vector<std::string> m_inputHistory; // 最近 20 条（指令二十五）
    std::size_t m_historyCursor = 0;         // == size() 表示在草稿
    std::string m_historyDraftBackup;
    bool m_whisperMenuOpen = false;
    std::string m_whisperMenuTarget;
    std::uint8_t m_defaultChannel = static_cast<std::uint8_t>(chat::ChatChannel::Nearby);
};

} // namespace legend::client
