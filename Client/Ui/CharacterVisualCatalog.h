#pragma once

// ---------------------------------------------------------------------------
// Stage26 指令十一：CharacterVisualCatalog —— 角色造型（visualId 1~3）与
// visual_entities.json 实体名的映射 + 界面展示名（全简体中文）。
// 服务器持久化 visualId（Migration 6）；客户端只做展示映射，不做第二套规则。
// 纯数据层（LegendClientUi）。
// ---------------------------------------------------------------------------

#include <cstdint>
#include <cstddef>

namespace legend::ui {

constexpr std::uint16_t kCharacterVisualIdMin = 1;
constexpr std::uint16_t kCharacterVisualIdMax = 3;
constexpr std::size_t kMaxCharactersPerAccountUi = 4; // 与服务器权威上限一致（仅展示提示用）

// visualId -> visual_entities.json 实体名（非法值返回 "player_warrior" 兜底）。
const char* CharacterVisualEntityName(std::uint16_t visualId);

// visualId -> 界面造型名（造型一/造型二/造型三）。
const char* CharacterVisualDisplayName(std::uint16_t visualId);

// 性别 -> 界面名（男/女；非法值返回 "男" 兜底）。
const char* CharacterGenderDisplayName(std::uint16_t gender);

} // namespace legend::ui
