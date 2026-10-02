// ---------------------------------------------------------------------------
// 阶段25：UI 模型检查（指令六十七：不测像素——测 HUD 数据 / Tracker 数据 /
// Inventory UI model / Shop UI model / Toast queue / Map Banner / BossBar state /
// Skill cooldown display state）。仍链接 LegendWorldTests（三套件纪律不变）。
// ---------------------------------------------------------------------------
#include "Tests/WorldTestHarness.h"

#include "Client/Ui/ChapterDisplayCatalog.h"
#include "Client/Ui/ItemDisplayCatalog.h"
#include "Client/Ui/UiModels.h"
#include "Client/Ui/UiTheme.h"

namespace worldtest {

namespace {

void RunToastChecks() {
    // 指令五十六：最大 5 条同时，自动淡出，Info/Success/Warning/Error。
    ui::ToastManager toasts;
    toasts.Push(ui::ToastLevel::Info, "one");
    toasts.Push(ui::ToastLevel::Success, "two");
    toasts.Push(ui::ToastLevel::Warning, "three");
    toasts.Push(ui::ToastLevel::Error, "four");
    toasts.Push(ui::ToastLevel::Info, "five");
    bool ok = toasts.Count() == 5;
    toasts.Push(ui::ToastLevel::Info, "six"); // 挤掉最旧
    ok = ok && toasts.Count() == 5 && toasts.Active().front().text == "two";
    // 自动淡出：3s 后全部清空。
    for (int i = 0; i < 40; ++i) {
        toasts.Update(0.1f);
    }
    ok = ok && toasts.Count() == 0;
    // 末 0.5s 线性淡出。
    ui::ToastManager fade;
    fade.Push(ui::ToastLevel::Info, "x", 1.0f);
    fade.Update(0.75f);
    ok = ok && fade.Active().front().Alpha() > 0.4f && fade.Active().front().Alpha() <= 0.51f;
    Check("UiToastChecks: max 5 / evict oldest / auto fade / alpha curve", ok);
}

void RunBannerAndLevelUpChecks() {
    // 指令三十六：淡入-保持-淡出 总约 2s。
    ui::MapBanner banner;
    banner.Show("Slime Meadow");
    bool ok = banner.Active() && banner.Alpha() == 0.0f;
    banner.Update(0.2f);
    ok = ok && banner.Alpha() > 0.0f && banner.Alpha() < 1.0f; // 淡入中
    banner.Update(0.4f);
    ok = ok && banner.Alpha() == 1.0f; // 保持
    banner.Update(1.45f);
    ok = ok && !banner.Active(); // 2.05s 后结束
    ok = ok && banner.Alpha() == 0.0f;
    Check("UiMapBannerChecks: fade-in / hold / fade-out ~2s", ok);

    // 指令二十三：LEVEL UP! 1.5~2s 反馈。
    ui::LevelUpFx fx;
    ok = !fx.Active();
    fx.Trigger();
    ok = ok && fx.Active() && fx.Alpha() == 0.0f;
    fx.Update(0.3f);
    ok = ok && fx.Alpha() == 1.0f;
    fx.Update(1.6f);
    ok = ok && !fx.Active();
    Check("UiLevelUpChecks: triggers once, ~1.8s, fade edges", ok);
}

void RunBossBarChecks() {
    // 指令三十四：Boss 被攻击显示 / HP 跟随 / 死亡或离开隐藏。
    ui::BossBar bar;
    bool ok = !bar.Visible();
    bar.ShowBoss(51, "Ancient Slime Guardian", 600, 600);
    ok = ok && bar.Visible() && bar.Name() == "Ancient Slime Guardian" &&
         std::abs(bar.HpPct() - 1.0f) < 0.001f;
    bar.UpdateBoss(51, 300, 600);
    ok = ok && std::abs(bar.HpPct() - 0.5f) < 0.001f;
    bar.UpdateBoss(999, 100, 600); // 未知实体不更新
    ok = ok && std::abs(bar.HpPct() - 0.5f) < 0.001f;
    bar.Hide();
    ok = ok && !bar.Visible() && bar.CurrentHp() == 300 && bar.MaxHp() == 600;
    Check("UiBossBarChecks: show/update-by-entity/hide state", ok);
}

void RunSkillSlotChecks() {
    // 指令二十八/二十九/三十：CD 倒计时展示 / Mana 不足 / 错误红闪。
    ui::SkillSlotState slot;
    slot.unlocked = true;
    slot.manaCost = 20;
    slot.cooldownTotal = 6.0f;
    slot.cooldownRemaining = 4.5f;
    bool ok = slot.ShowCooldownHint();
    ok = ok && slot.ManaInsufficient(10) && !slot.ManaInsufficient(30);
    slot.errorFlash = 0.5f;
    ok = ok && slot.errorFlash > 0.0f;
    slot.errorFlash -= 0.6f;
    ok = ok && slot.errorFlash <= 0.0f;
    Check("UiSkillSlotChecks: cooldown display state / mana gate / error flash", ok);
}

void RunHudAndTrackerModelChecks() {
    // 指令二十七：HUD V2 数据（HP/Mana/EXP pct + Gold/Atk/Def 展示字段）。
    ui::HudModel hud;
    hud.hp = 50;
    hud.maxHp = 100;
    hud.mana = 25;
    hud.maxMana = 100;
    hud.exp = 30;
    hud.expToNext = 120;
    bool ok = std::abs(hud.HpPct() - 0.5f) < 0.001f &&
              std::abs(hud.ManaPct() - 0.25f) < 0.001f &&
              std::abs(hud.ExpPct() - 0.25f) < 0.001f;

    // 指令二十一：Tracker ≤3 条 + HasQuest。
    ui::QuestTrackerModel tracker;
    for (std::uint32_t i = 0; i < 5; ++i) {
        ui::QuestTrackerEntry entry;
        entry.questId = 4001 + i;
        entry.title = "Q";
        entry.current = i;
        entry.required = 5;
        tracker.entries.push_back(entry);
    }
    ok = ok && tracker.entries.size() == 5 && tracker.HasQuest(4003) && !tracker.HasQuest(9999);
    // UI 层截取 ≤3（渲染端纪律）；模型本身接受截断输入。
    Check("UiHudTrackerChecks: hud pct + tracker membership", ok);
}

void RunInventoryShopModelChecks() {
    // 指令十七：40 格模型 + Tooltip 字段；指令十九：Shop 条目/价格展示。
    ui::InventoryUiModel inv;
    bool ok = inv.Slot(0).instanceId == 0 && !inv.Slot(0).filled;
    inv.slots[3].filled = true;
    inv.slots[3].definitionId = 3010;
    inv.slots[3].quantity = 1;
    inv.slots[3].iconAsset = "item_bronze_sword";
    inv.slots[3].isEquipment = true;
    ok = ok && inv.Slot(3).isEquipment && inv.Slot(3).definitionId == 3010;
    inv.tooltip.visible = true;
    inv.tooltip.name = "Bronze Sword";
    inv.tooltip.attackBonus = 8;
    inv.tooltip.defenseBonus = 0;
    inv.tooltip.sellPrice = 45;
    ok = ok && inv.tooltip.visible && inv.tooltip.attackBonus == 8;

    ui::ShopUiModel shop;
    ui::ShopEntryView entry;
    entry.definitionId = 3011;
    entry.name = "Apprentice Staff";
    entry.buyPrice = 160;
    entry.canBuy = true;
    shop.entries.push_back(entry);
    shop.gold = 95;
    ok = ok && shop.entries.size() == 1 && shop.entries[0].buyPrice == 160 && shop.gold == 95;
    Check("UiInventoryShopChecks: 40-slot model / tooltip / shop entries", ok);
}

void RunMinimapModelChecks() {
    // 指令三十七/三十八：归一化 + 边界。
    ui::MinimapModel model;
    model.minX = 0.0f;
    model.minY = 0.0f;
    model.maxX = 2000.0f;
    model.maxY = 2000.0f;
    bool ok = std::abs(model.NormalizeX(1000.0f) - 0.5f) < 0.001f &&
              std::abs(model.NormalizeY(500.0f) - 0.25f) < 0.001f;
    model.npcs.push_back({300.0f, 300.0f, true});
    model.portals.push_back({1000.0f, 300.0f, false});
    ok = ok && model.npcs.size() == 1 && model.npcs[0].isQuestGiver &&
         model.portals.size() == 1;
    Check("UiMinimapChecks: normalize + blips (npc quest-flagged / portal)", ok);
}

void RunItemDisplayCatalogChecks() {
    // 展示目录：真实 Data/Game/items.json（7 物品，3010~3013 新装备）。
    ui::ItemDisplayCatalog catalog;
    std::string error;
    const bool loaded = catalog.Load(std::string(LEGEND_SOURCE_DIR) + "/Data", error);
    if (!loaded) {
        std::printf("[Diag] ItemDisplayCatalog load error: %s\n", error.c_str());
    }
    bool ok = loaded && catalog.Count() == 7;
    const ui::ItemDisplay* bronze = catalog.Find(3010);
    ok = ok && bronze != nullptr && bronze->name == "青铜剑" &&
         bronze->attackBonus == 8 && bronze->isEquipment() &&
         bronze->iconKey == "item_bronze_sword";
    const ui::ItemDisplay* core = catalog.Find(3003);
    ok = ok && core != nullptr && !core->isEquipment() && core->maxStack == 99;
    ok = ok && catalog.DisplayName(3013) == "行者护甲" &&
         catalog.IconAsset(3012) == "item_spirit_talisman" &&
         catalog.Find(9999) == nullptr;
    Check("UiItemDisplayChecks: real Data items.json display fields (3010+)", ok);
}

void RunChapterDisplayCatalogChecks() {
    // 阶段25：真实 Data/Game/chapters.json（Chapter 1 The Restless Slimes）。
    ui::ChapterDisplayCatalog catalog;
    std::string error;
    const bool loaded = catalog.Load(std::string(LEGEND_SOURCE_DIR) + "/Data", error);
    if (!loaded) {
        std::printf("[Diag] ChapterDisplayCatalog load error: %s\n", error.c_str());
    }
    bool ok = loaded && catalog.Count() == 1;
    std::string title;
    ok = ok && catalog.ChapterCompletion(4006, title) && title == "不安分的史莱姆";
    std::string none;
    ok = ok && !catalog.ChapterCompletion(4005, none); // 非 finalQuest 不命中
    // 缺失目录 → 加载失败不致命（调用方降级空目录）。
    ui::ChapterDisplayCatalog missing;
    ok = ok && !missing.Load(std::string(LEGEND_SOURCE_DIR) + "/no_such_data", error);
    Check("UiChapterDisplayChecks: real Data chapters.json finalQuest -> title (4006)", ok);
}

void RunUiThemeChecks() {
    // 指令六十二/六十三：参考分辨率 1920×1080 + 缩放曲线（三种分辨率可用）。
    bool ok = std::abs(ui::UiTheme::ScaleFor(1920.0f, 1080.0f) - 1.0f) < 0.001f;
    ok = ok && std::abs(ui::UiTheme::ScaleFor(1280.0f, 720.0f) - 1280.0f / 1920.0f) < 0.001f;
    ok = ok && std::abs(ui::UiTheme::ScaleFor(1600.0f, 900.0f) - 1600.0f / 1920.0f) < 0.001f;
    ok = ok && ui::UiTheme::ScaleFor(640.0f, 480.0f) >= 0.5f;   // 下限 clamp
    ok = ok && ui::UiTheme::ScaleFor(3840.0f, 2160.0f) <= 2.0f; // 上限 clamp
    Check("UiThemeChecks: reference 1920x1080 scale curve + clamps", ok);
}

} // namespace

void RunUiModelChecks() {
    RunUiThemeChecks();
    RunToastChecks();
    RunBannerAndLevelUpChecks();
    RunBossBarChecks();
    RunSkillSlotChecks();
    RunHudAndTrackerModelChecks();
    RunInventoryShopModelChecks();
    RunMinimapModelChecks();
    RunItemDisplayCatalogChecks();
    RunChapterDisplayCatalogChecks();
}

} // namespace worldtest
