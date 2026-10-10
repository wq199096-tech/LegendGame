#pragma once

// ---------------------------------------------------------------------------
// Stage27 中文化专项：PlayerFacingText —— 玩家可见枚举值的简体中文映射
//（统一显示层，禁止在渲染代码里散落硬编码映射；内部 ID/枚举值保持英文不动）。
// 适用范围：items.json type/equipSlot、quests.json objectives type、
// statuses.json type、maps.json type 等会被客户端直接显示的 JSON 枚举值。
// 纯函数，PlayerFacingLocalizationChecks 可脱离窗口验证。
// ---------------------------------------------------------------------------

#include <string>

namespace legend::ui {

// items.json "type"：Weapon/Armor/Material/Consumable/Quest/None。
const char* ItemTypeName(const std::string& type);
// items.json "equipSlot"：Weapon/Armor/None。
const char* EquipSlotName(const std::string& slot);
// quests.json objectives "type"：KillMonster/CollectItem/ReachLevel/ReachArea。
// 语义为动词前缀：击败 史莱姆 0/5、收集 史莱姆核心 0/3、等级达到 3、前往 远古遗迹。
const char* QuestObjectiveVerb(const std::string& type);
// statuses.json "type"：Buff/Debuff。
const char* StatusKindName(const std::string& type);
// maps.json "type"：Town/Field。
const char* MapTypeName(const std::string& type);
// 职业名（classId 1/2/3；HUD/角色面板兜底显示用）。
const char* ClassName(int classId);
// 判断字符串是否包含 CJK 码点（中文化审计/测试辅助：玩家可见名必须含中文）。
bool ContainsCjk(const std::string& text);

} // namespace legend::ui
