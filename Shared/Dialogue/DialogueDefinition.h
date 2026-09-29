#pragma once

#include <cstdint>
#include <string>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令二十三/二十四：Dialogue 静态定义（一层菜单的基础文本；
// Options 由 DialogueService 按玩家任务状态动态生成，不静态配置）。
// 代码硬编码，不入数据库（指令一百零八）。
// ---------------------------------------------------------------------------
struct DialogueDefinition {
    std::uint32_t dialogueId = 0;
    std::string title;
    std::string text;
};

} // namespace legend::world
