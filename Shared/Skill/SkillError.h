#pragma once

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段15 指令十九/九十四：Mana 与 Cooldown 消耗边界说明（SkillError.h 承载规则
// 文档 + 结果码命名）。
//
// - Mana 在"施法被服务器正式接受"时扣除：Instant 验证成功后立即扣；Cast-Time
//   开始施法时扣。之后因 Moved/Dead/TargetInvalid 取消，阶段15 一律不返还。
// - Cooldown 在服务器正式接受施法时启动（不等命中），取消不返还。
// - 所有失败路径（UnknownSkill/InvalidTarget/TargetDead/CasterDead/OutOfRange/
//   Cooldown/NotEnoughMana/AlreadyCasting/DuplicateRequest）不扣 Mana、不启动 CD。
// - currentMana 永不为负（服务器下限 0）。
// ---------------------------------------------------------------------------

} // namespace legend::world
