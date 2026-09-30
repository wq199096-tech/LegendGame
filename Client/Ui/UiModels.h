#pragma once

// ---------------------------------------------------------------------------
// 阶段25：正式 UI 数据模型层（Display-Only）。
//
// 纪律（与 VisualDataCatalog 相同）：
//   - 全部状态来自服务器事件镜像（WorldClientController 只读）或本地 UI 瞬态；
//     Client 永不推算服务器权威结果（指令二/四十九）。
//   - 本层为纯数据/计时逻辑（不依赖 SDL/OpenGL）——UiModelChecks 可直接测试
//     （指令六十七：Toast queue / Map Banner / BossBar state / Skill cooldown
//     display state / HUD 数据 / Tracker 数据 / Inventory / Shop model）。
// ---------------------------------------------------------------------------

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::ui {

// ---- Toast（指令五十五/五十六：GameToastManager，Info/Success/Warning/Error，
//      最大 5 条同时，自动淡出；服务器常见错误码不能只打印 Console）----
enum class ToastLevel : std::uint8_t { Info = 0, Success = 1, Warning = 2, Error = 3 };

struct Toast {
    ToastLevel level = ToastLevel::Info;
    std::string text;
    float age = 0.0f;
    float life = 3.0f;
    // 淡出透明度（末 0.5s 线性淡出）。
    float Alpha() const {
        const float remain = life - age;
        if (remain <= 0.0f) {
            return 0.0f;
        }
        return remain < 0.5f ? remain / 0.5f : 1.0f;
    }
};

class ToastManager {
public:
    static constexpr std::size_t kMaxToasts = 5;

    // 超过上限时挤掉最旧一条。
    void Push(ToastLevel level, const std::string& text, float lifeSeconds = 3.0f);
    void Update(float deltaTime);
    const std::vector<Toast>& Active() const { return m_toasts; }
    std::size_t Count() const { return m_toasts.size(); }
    void Clear() { m_toasts.clear(); }

private:
    std::vector<Toast> m_toasts;
};

// ---- Map Enter Banner（指令三十六：MapChanged 时屏幕中央显示地图名，
//      淡入-保持-淡出 总时长约 2s）----
class MapBanner {
public:
    void Show(const std::string& mapName);
    void Update(float deltaTime);
    bool Active() const { return !m_text.empty() && m_age < m_total; }
    const std::string& Text() const { return m_text; }
    // 0~1（前 0.4s 淡入，后 0.4s 淡出）。
    float Alpha() const;

private:
    std::string m_text;
    float m_age = 0.0f;
    float m_total = 2.0f;
};

// ---- Boss Bar（指令三十四：Boss 被攻击/进入战斗后顶部显示；离开战斗/远离隐藏）----
class BossBar {
public:
    void ShowBoss(std::uint64_t entityId, const std::string& name, std::uint32_t currentHp,
                  std::uint32_t maxHp);
    void UpdateBoss(std::uint64_t entityId, std::uint32_t currentHp, std::uint32_t maxHp);
    void Hide() { m_visible = false; }
    bool Visible() const { return m_visible; }
    std::uint64_t EntityId() const { return m_entityId; }
    const std::string& Name() const { return m_name; }
    float HpPct() const { return m_maxHp > 0 ? static_cast<float>(m_currentHp) / static_cast<float>(m_maxHp) : 0.0f; }
    std::uint32_t CurrentHp() const { return m_currentHp; }
    std::uint32_t MaxHp() const { return m_maxHp; }

private:
    bool m_visible = false;
    std::uint64_t m_entityId = 0;
    std::string m_name;
    std::uint32_t m_currentHp = 0;
    std::uint32_t m_maxHp = 0;
};

// ---- Skill 槽显示状态（指令二十八/二十九/三十：CD 视觉倒计时/Mana 不足红闪/
//      Cooldown 提示；全部为展示态，判定仍在服务器）----
struct SkillSlotState {
    bool unlocked = false;
    std::string name;
    std::string iconAsset;
    std::uint32_t manaCost = 0;
    float cooldownRemaining = 0.0f; // 秒（展示倒计时）
    float cooldownTotal = 0.0f;
    float errorFlash = 0.0f;        // >0 = 红/暗反馈剩余时间
    // 指令三十：CD 期间点击 -> 服务器返回 Cooldown -> 这里给提示flag。
    bool ShowCooldownHint() const { return cooldownRemaining > 0.0f; }
    bool ManaInsufficient(std::uint32_t currentMana) const {
        return unlocked && currentMana < manaCost;
    }
};

// ---- HUD V2 数据（指令二十七：头像/Name/Lv/HP/Mana/EXP Bar/Gold；
//      指令十五：Attack/Defense 含装备加成，装备后立即变化）----
struct HudModel {
    std::string playerName;
    int classId = 0;
    std::uint32_t level = 1;
    std::uint32_t hp = 0;
    std::uint32_t maxHp = 1;
    std::uint32_t mana = 0;
    std::uint32_t maxMana = 1;
    std::int64_t exp = 0;
    std::int64_t expToNext = 1;
    std::int64_t gold = 0;
    std::uint32_t attack = 0;   // base + equipment
    std::uint32_t defense = 0;  // base + equipment
    float HpPct() const { return maxHp > 0 ? static_cast<float>(hp) / static_cast<float>(maxHp) : 0.0f; }
    float ManaPct() const { return maxMana > 0 ? static_cast<float>(mana) / static_cast<float>(maxMana) : 0.0f; }
    float ExpPct() const { return expToNext > 0 ? static_cast<float>(exp) / static_cast<float>(expToNext) : 0.0f; }
};

// ---- Quest Tracker（指令二十一：右侧最多 3 个 Active Quest，进度全部来自服务器）----
struct QuestTrackerEntry {
    std::uint32_t questId = 0;
    std::string title;
    std::string objectiveText;
    std::uint32_t current = 0;
    std::uint32_t required = 0;
    bool readyToTurnIn = false;
};

struct QuestTrackerModel {
    std::vector<QuestTrackerEntry> entries; // ≤3（InProgress 优先，ReadyToTurnIn 置顶）
    bool HasQuest(std::uint32_t questId) const;
};

// ---- Inventory UI（指令十七：40 格/图标/数量/装备边框/点击 Tooltip；
//      指令十八：双击或右键 Equip（本项目定为：双击 Equip），服务器依旧权威）----
struct InventorySlotView {
    bool filled = false;
    std::uint64_t instanceId = 0;
    std::uint32_t definitionId = 0;
    std::uint32_t quantity = 0;
    std::string iconAsset;
    bool isEquipment = false;
    bool equippedHere = false; // 装备栏展示用（Weapon/Armor 槽）
};

struct ItemTooltip {
    bool visible = false;
    std::string name;
    std::string type;      // Weapon/Armor/Material
    std::string equipSlot; // Weapon/Armor/None
    std::uint32_t attackBonus = 0;
    std::uint32_t defenseBonus = 0;
    std::uint32_t sellPrice = 0;
};

struct InventoryUiModel {
    static constexpr std::size_t kSlots = 40;
    std::array<InventorySlotView, kSlots> slots;
    int selectedIndex = -1;
    ItemTooltip tooltip;
    std::int64_t gold = 0;
    const InventorySlotView& Slot(std::size_t index) const { return slots[index]; }
};

// ---- Shop UI（指令十九：NPC 对话 -> Open Shop -> Shop Window：图标/名称/价格/
//      Buy + 玩家背包 Sell + 当前 Gold；B/S 快捷键保留 Debug）----
struct ShopEntryView {
    std::uint32_t definitionId = 0;
    std::string name;
    std::string iconAsset;
    std::uint32_t buyPrice = 0;
    bool canBuy = true;
};

struct ShopUiModel {
    std::vector<ShopEntryView> entries;
    std::int64_t gold = 0;
    int selectedIndex = -1;
};

// ---- Dialogue UI（指令二十：NPC Name/Dialogue Text/Options；Quest: Accept/
//      Continue/Turn In/Close）----
struct DialogueOptionView {
    std::uint32_t optionId = 0;
    std::uint8_t type = 0; // DialogueOptionType（服务器下发）
    std::string label;
};

struct DialogueUiModel {
    bool active = false;
    std::string npcName;
    std::string text;
    std::vector<DialogueOptionView> options;
};

// ---- Character Panel V1（指令十六：C 键 —— Name/Class/Level/EXP/HP/Mana/
//      Attack/Defense/Gold + Weapon/Armor 装备槽）----
struct EquipSlotView {
    bool filled = false;
    std::string name;
    std::uint32_t bonus = 0;
};

struct CharacterPanelModel {
    std::string name;
    std::string className;
    std::uint32_t level = 1;
    std::int64_t exp = 0;
    std::int64_t expToNext = 1;
    std::uint32_t hp = 0;
    std::uint32_t maxHp = 1;
    std::uint32_t mana = 0;
    std::uint32_t maxMana = 1;
    std::uint32_t attack = 0;
    std::uint32_t defense = 0;
    std::int64_t gold = 0;
    EquipSlotView weapon;
    EquipSlotView armor;
};

// ---- MiniMap V1（指令三十七/三十八：右上小地图；玩家/NPC/Portal/任务 NPC；
//      固定 NPC/Portal 来自当前地图配置，怪物暂不显示；权威原则——只显示
//      Client AOI 已知实体 + 地图静态配置）----
struct MinimapBlip {
    float x = 0.0f;
    float y = 0.0f;
    bool isQuestGiver = false;
};

struct MinimapModel {
    float playerX = 0.0f;
    float playerY = 0.0f;
    float minX = 0.0f;
    float minY = 0.0f;
    float maxX = 1.0f;
    float maxY = 1.0f;
    std::vector<MinimapBlip> npcs;
    std::vector<MinimapBlip> portals;
    // 世界坐标 -> 0~1 归一化（UI 层再乘尺寸）。
    float NormalizeX(float worldX) const;
    float NormalizeY(float worldY) const;
};

// ---- Level Up 反馈（指令二十三：屏幕中央 LEVEL UP! 约 1.5~2s）----
class LevelUpFx {
public:
    void Trigger();
    void Update(float deltaTime);
    bool Active() const { return m_age < m_total; }
    // 0~1（首尾各 0.3s 淡入淡出）。
    float Alpha() const;

private:
    float m_age = 10.0f;
    float m_total = 1.8f;
};

} // namespace legend::ui
