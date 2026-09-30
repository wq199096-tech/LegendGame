#pragma once

// ---------------------------------------------------------------------------
// 阶段25：客户端章节展示目录（Display-Only）。
// 只加载 Data/Game/chapters.json 的【展示字段】（章节标题/finalQuestId）——
// Chapter Complete 判定由服务器权威的 QuestState::Completed 事件触发，
// 本目录只决定"哪个任务对应哪章标题"。缺失文件不致命（fallback 无章节提示）。
// 与 ItemDisplayCatalog 同纪律：直读 JSON，Client 不链接 Server Registry。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace legend::ui {

struct ChapterDisplay {
    std::uint32_t chapterId = 0;
    std::string title;
    std::uint32_t finalQuestId = 0;
    std::vector<std::uint32_t> questIds;
};

class ChapterDisplayCatalog {
public:
    // dataRoot = 含 Game/ 子目录的 Data 目录；chapters.json 缺失 -> false（调用方
    // 降级为空目录）；存在但非法 -> false + error。
    bool Load(const std::string& dataRoot, std::string& error);

    // 该任务是否为某章的 finalQuest（是 -> 填充 outTitle 并返回 true）。
    bool ChapterCompletion(std::uint32_t questId, std::string& outTitle) const;
    std::size_t Count() const { return m_chapters.size(); }
    void Clear() { m_chapters.clear(); }

private:
    std::map<std::uint32_t, ChapterDisplay> m_chapters; // chapterId -> chapter
    std::map<std::uint32_t, std::uint32_t> m_finalQuestToChapter;
};

} // namespace legend::ui
