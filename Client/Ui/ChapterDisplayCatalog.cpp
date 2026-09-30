#include "Client/Ui/ChapterDisplayCatalog.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>

namespace legend::ui {

namespace {

namespace fs = std::filesystem;

using nlohmann::json;

bool ReadJsonFile(const std::string& filePath, json& out, std::string& error) {
    std::ifstream file(filePath, std::ios::binary);
    if (!file) {
        error = filePath + ": file not found";
        return false;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    out = json::parse(text, nullptr, false);
    if (out.is_discarded()) {
        error = filePath + ": invalid JSON syntax";
        return false;
    }
    if (!out.is_object()) {
        error = filePath + ": top-level must be an object";
        return false;
    }
    return true;
}

} // namespace

bool ChapterDisplayCatalog::Load(const std::string& dataRoot, std::string& error) {
    const fs::path path = fs::path(dataRoot) / "Game" / "chapters.json";
    if (!fs::exists(path)) {
        error = path.string() + ": file not found";
        return false;
    }
    json root;
    if (!ReadJsonFile(path.string(), root, error)) {
        return false;
    }
    const auto chaptersIt = root.find("chapters");
    if (chaptersIt == root.end() || !chaptersIt->is_array()) {
        error = "chapters.json: missing/invalid array 'chapters'";
        return false;
    }
    m_chapters.clear();
    m_finalQuestToChapter.clear();
    for (const auto& e : *chaptersIt) {
        ChapterDisplay chapter;
        const auto idIt = e.find("chapterId");
        if (idIt == e.end() || !idIt->is_number_unsigned()) {
            error = "chapters.json: entry missing chapterId";
            return false;
        }
        chapter.chapterId = idIt->get<std::uint32_t>();
        const auto titleIt = e.find("title");
        if (titleIt != e.end() && titleIt->is_string()) {
            chapter.title = titleIt->get<std::string>();
        }
        const auto finalIt = e.find("finalQuestId");
        if (finalIt != e.end() && finalIt->is_number_unsigned()) {
            chapter.finalQuestId = finalIt->get<std::uint32_t>();
        }
        const auto questsIt = e.find("questIds");
        if (questsIt != e.end() && questsIt->is_array()) {
            for (const auto& q : *questsIt) {
                if (q.is_number_unsigned()) {
                    chapter.questIds.push_back(q.get<std::uint32_t>());
                }
            }
        }
        if (chapter.finalQuestId != 0) {
            m_finalQuestToChapter[chapter.finalQuestId] = chapter.chapterId;
        }
        m_chapters[chapter.chapterId] = std::move(chapter);
    }
    return true;
}

bool ChapterDisplayCatalog::ChapterCompletion(std::uint32_t questId, std::string& outTitle) const {
    const auto it = m_finalQuestToChapter.find(questId);
    if (it == m_finalQuestToChapter.end()) {
        return false;
    }
    const auto chapterIt = m_chapters.find(it->second);
    if (chapterIt == m_chapters.end()) {
        return false;
    }
    outTitle = chapterIt->second.title;
    return true;
}

} // namespace legend::ui
