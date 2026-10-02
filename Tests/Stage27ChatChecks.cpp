// ---------------------------------------------------------------------------
// Stage27ChatChecks.cpp —— Stage27 玩家身份同步与聊天系统验收
//（Stage27 指令四十：PlayerIdentity / RemoteAppearance / ChatCodec /
//  ChatValidation / ChatNearby / ChatWorld / ChatWhisper / ChatRateLimit /
//  ChatReplay / ChatSecurity；并入 LegendWorldTests，不新增第四个测试 exe）。
// 限流测试注入小窗口（指令三十五：不靠长 sleep；生产默认值不变）。
// ---------------------------------------------------------------------------

#include "Tests/WorldTestHarness.h"

#include "Client/Ui/ChatModel.h"
#include "Client/Ui/ChatUiTheme.h"
#include "Client/Ui/CharacterVisualCatalog.h"
#include "Client/WorldNetwork/RemotePlayerEntity.h"
#include "Server/WorldServer/OnlinePlayerDirectory.h"
#include "Shared/Chat/ChatProtocol.h"

namespace worldtest {
namespace {

using legend::client::ChatEntry;
using legend::client::ChatModel;

// 真实等待（WaitUntil 的谓词恒真会立即返回；负向断言需要给网络事件到达留时间）。
inline void TestSleep(int ms) {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

// ===========================================================================
// A. 纯逻辑：ChatCodec（指令十三：Remaining()==0 / 截断拒绝）
// ===========================================================================

void RunStage27CodecChecks() {
    {
        chat::ChatSendRequestPayload p;
        p.requestId = 12345;
        p.channel = static_cast<std::uint8_t>(chat::ChatChannel::Whisper);
        p.targetName = "界面英雄";
        p.text = "你好世界 Hello 123";
        std::vector<std::uint8_t> bytes;
        chat::ChatSendRequestPayload out;
        std::string error;
        const bool ok = chat::EncodeChatSendRequest(p, bytes) &&
                        chat::DecodeChatSendRequest(bytes.data(), bytes.size(), out, error) &&
                        out.requestId == 12345 &&
                        out.channel == static_cast<std::uint8_t>(chat::ChatChannel::Whisper) &&
                        out.targetName == "界面英雄" && out.text == p.text;
        Check("ChatCodecChecks: ChatSendRequest roundtrip (UTF-8 CJK)", ok);
    }
    {
        chat::ChatSendResponsePayload p;
        p.requestId = 77;
        p.success = false;
        p.errorCode = static_cast<std::uint16_t>(chat::ChatErrorCode::RateLimited);
        p.message = "发言过于频繁，请稍后再试";
        std::vector<std::uint8_t> bytes;
        chat::ChatSendResponsePayload out;
        std::string error;
        const bool ok = chat::EncodeChatSendResponse(p, bytes) &&
                        chat::DecodeChatSendResponse(bytes.data(), bytes.size(), out, error) &&
                        !out.success &&
                        out.errorCode == static_cast<std::uint16_t>(chat::ChatErrorCode::RateLimited) &&
                        out.message == p.message;
        Check("ChatCodecChecks: ChatSendResponse roundtrip", ok);
    }
    {
        chat::ChatMessageEventPayload p;
        p.messageId = 9;
        p.channel = static_cast<std::uint8_t>(chat::ChatChannel::World);
        p.senderCharacterId = 42;
        p.senderName = "界面英雄";
        p.text = "有人组队吗";
        p.timestamp = 1727856000000ULL;
        std::vector<std::uint8_t> bytes;
        chat::ChatMessageEventPayload out;
        std::string error;
        const bool ok = chat::EncodeChatMessageEvent(p, bytes) &&
                        chat::DecodeChatMessageEvent(bytes.data(), bytes.size(), out, error) &&
                        out.messageId == 9 &&
                        out.senderCharacterId == 42 && out.senderName == "界面英雄" &&
                        out.text == "有人组队吗" && out.timestamp == p.timestamp;
        Check("ChatCodecChecks: ChatMessageEvent roundtrip", ok);
    }
    {
        // 截断 payload 拒绝。
        chat::ChatSendRequestPayload p;
        p.requestId = 1;
        p.text = "abc";
        std::vector<std::uint8_t> bytes;
        chat::EncodeChatSendRequest(p, bytes);
        bytes.pop_back();
        chat::ChatSendRequestPayload out;
        std::string error;
        Check("ChatCodecChecks: truncated payload rejected",
              !chat::DecodeChatSendRequest(bytes.data(), bytes.size(), out, error));
    }
    {
        // 多余字节拒绝（Remaining()==0 纪律）。
        chat::ChatSendRequestPayload p;
        p.requestId = 2;
        p.text = "abc";
        std::vector<std::uint8_t> bytes;
        chat::EncodeChatSendRequest(p, bytes);
        bytes.push_back(0x00);
        chat::ChatSendRequestPayload out;
        std::string error;
        Check("ChatCodecChecks: trailing bytes rejected",
              !chat::DecodeChatSendRequest(bytes.data(), bytes.size(), out, error) &&
              error.find("trailing") != std::string::npos);
    }
    {
        // 恶意长度前缀（超剩余字节）拒绝。
        std::vector<std::uint8_t> bytes{0x00, 0x01, 0xFF, 0xFF, 0x41};
        chat::ChatSendRequestPayload out;
        std::string error;
        Check("ChatCodecChecks: hostile length prefix rejected",
              !chat::DecodeChatSendRequest(bytes.data(), bytes.size(), out, error));
    }
}

// ===========================================================================
// B. 纯逻辑：文本/频道校验（指令十四/二十七）+ 错误文案 + 命令解析（指令二十六）
// ===========================================================================

void RunStage27ValidationChecks() {
    using chat::ChatErrorCode;
    Check("ChatValidationChecks: chinese text allowed", chat::IsValidChatText("你好世界", 120));
    Check("ChatValidationChecks: ascii text allowed", chat::IsValidChatText("Hello 123!", 120));
    Check("ChatValidationChecks: empty rejected", !chat::IsValidChatText("", 120));
    Check("ChatValidationChecks: whitespace-only rejected", !chat::IsValidChatText("   ", 120));
    Check("ChatValidationChecks: newline rejected", !chat::IsValidChatText(std::string("ab\nc"), 120));
    Check("ChatValidationChecks: carriage return rejected",
          !chat::IsValidChatText(std::string("ab\rc"), 120));
    Check("ChatValidationChecks: tab rejected", !chat::IsValidChatText(std::string("a\tb"), 120));
    Check("ChatValidationChecks: NUL rejected", !chat::IsValidChatText(std::string("a\0b", 3), 120));
    Check("ChatValidationChecks: ANSI escape rejected",
          !chat::IsValidChatText(std::string("\x1B[31mred"), 120));
    Check("ChatValidationChecks: C1 control rejected",
          !chat::IsValidChatText(std::string("a\x85" "b"), 120));
    Check("ChatValidationChecks: invalid utf8 rejected",
          !chat::IsValidChatText(std::string("\xC3\x28hi"), 120));
    Check("ChatValidationChecks: overlong utf8 rejected",
          !chat::IsValidChatText(std::string("\xC0\x80"), 120));
    Check("ChatValidationChecks: surrogate rejected",
          !chat::IsValidChatText(std::string("\xED\xA0\x80"), 120));
    {
        // 120 码点边界（BMP 中文每字 3 字节 = 360 字节 <= 480 上限）。
        std::string ok120;
        for (int i = 0; i < 120; ++i) {
            ok120 += "界";
        }
        std::string bad121 = ok120 + "界";
        Check("ChatValidationChecks: 120 codepoints allowed", chat::IsValidChatText(ok120, 120));
        Check("ChatValidationChecks: 121 codepoints rejected", !chat::IsValidChatText(bad121, 120));
    }
    Check("ChatValidationChecks: channel value whitelist",
          chat::IsValidChatChannelValue(1) && chat::IsValidChatChannelValue(4) &&
          !chat::IsValidChatChannelValue(0) && !chat::IsValidChatChannelValue(5) &&
          !chat::IsValidChatChannelValue(99));
    Check("ChatValidationChecks: System not client-sendable",
          !chat::IsClientSendableChannel(static_cast<std::uint8_t>(chat::ChatChannel::System)) &&
          chat::IsClientSendableChannel(static_cast<std::uint8_t>(chat::ChatChannel::Nearby)));
    Check("ChatValidationChecks: rate limited user text",
          std::string(chat::ChatErrorUserText(static_cast<std::uint16_t>(
              ChatErrorCode::RateLimited))) == "发言过于频繁，请稍后再试");
    Check("ChatValidationChecks: offline user text",
          std::string(chat::ChatErrorUserText(static_cast<std::uint16_t>(
              ChatErrorCode::TargetOffline))) == "该玩家当前不在线");
    Check("ChatValidationChecks: whisper target name = character name rules",
          chat::IsValidChatTargetName("界面英雄") &&
          !chat::IsValidChatTargetName("A") && // < 2 码点
          !chat::IsValidChatTargetName("名字!!"));
    {
        // 指令二十六：命令解析。
        chat::ChatChannel channel = chat::ChatChannel::Nearby;
        std::string target;
        std::string text;
        const bool n = chat::ParseChatCommand("/n 你好", channel, target, text) &&
                       channel == chat::ChatChannel::Nearby && text == "你好";
        const bool w = chat::ParseChatCommand("/world 大家好", channel, target, text) &&
                       channel == chat::ChatChannel::World && text == "大家好";
        const bool whisper = chat::ParseChatCommand("/w 界面英雄 早上好", channel, target, text) &&
                             channel == chat::ChatChannel::Whisper &&
                             target == "界面英雄" && text == "早上好";
        const bool sys = !chat::ParseChatCommand("/s 作弊", channel, target, text);
        const bool plain = !chat::ParseChatCommand("普通发言", channel, target, text);
        const bool emptyCmd = !chat::ParseChatCommand("/w 界面英雄", channel, target, text);
        Check("ChatValidationChecks: /n command", n);
        Check("ChatValidationChecks: /world command", w);
        Check("ChatValidationChecks: /w command", whisper);
        Check("ChatValidationChecks: /s command not supported", sys);
        Check("ChatValidationChecks: plain text not a command", plain);
        Check("ChatValidationChecks: /w without text rejected", emptyCmd);
    }
}

// ===========================================================================
// C. 纯逻辑：限流窗口（指令十五；时间点注入，无 sleep）
// ===========================================================================

void RunStage27RateLimitPureChecks() {
    using chat::ChatTimePoint;
    const ChatTimePoint t0 = chat::SteadyClock::now();
    std::deque<ChatTimePoint> times;
    // 窗口 1000ms 内最多 2 条。
    times = {t0 - std::chrono::milliseconds(100), t0 - std::chrono::milliseconds(50)};
    Check("ChatRateLimitPureChecks: 2 in window -> third blocked",
          !chat::IsChatRateAllowed(t0, times, 1000, 2));
    times = {t0 - std::chrono::milliseconds(1500), t0 - std::chrono::milliseconds(1400)};
    Check("ChatRateLimitPureChecks: outside window -> allowed",
          chat::IsChatRateAllowed(t0, times, 1000, 2));
    times = {t0};
    Check("ChatRateLimitPureChecks: 1 in window max 1 -> blocked",
          !chat::IsChatRateAllowed(t0, times, 3000, 1));
    times = {};
    Check("ChatRateLimitPureChecks: empty history allowed",
          chat::IsChatRateAllowed(t0, times, 3000, 1));
    // 指令四十六：窗口/条数 <= 0 显式放宽（仅测试），生产由配置校验拒绝。
    times = {t0};
    Check("ChatRateLimitPureChecks: zero window test-relaxed",
          chat::IsChatRateAllowed(t0, times, 0, 0));
}

// ===========================================================================
// D. 纯逻辑：ChatModel（指令十九~二十六）
// ===========================================================================

void RunStage27ChatModelChecks() {
    {
        ChatModel model;
        ChatEntry nearby;
        nearby.channel = 1;
        nearby.senderName = "玩家甲";
        nearby.text = "你好";
        ChatEntry world;
        world.channel = 2;
        world.senderName = "玩家乙";
        world.text = "大家好";
        ChatEntry system;
        system.channel = 4;
        system.text = "欢迎进入LegendGame";
        model.AddMessage(nearby);
        model.AddMessage(world);
        model.AddMessage(system);
        const auto all = model.VisibleMessages(0);
        const auto onlyWorld = model.VisibleMessages(2);
        const auto onlySystem = model.VisibleMessages(4);
        const bool ok = all.size() == 3 && onlyWorld.size() == 1 &&
                        onlyWorld.front()->text == "大家好" && onlySystem.size() == 1 &&
                        onlySystem.front()->text == "欢迎进入LegendGame";
        Check("ChatModelChecks: tab filtering", ok);
    }
    {
        // FIFO：每频道 200 / 综合 500。
        ChatModel model;
        for (int i = 0; i < 205; ++i) {
            ChatEntry e;
            e.channel = 1;
            e.senderName = "玩家甲";
            e.text = "m" + std::to_string(i);
            model.AddMessage(e);
        }
        const auto nearby = model.VisibleMessages(1);
        const auto all = model.VisibleMessages(0);
        const bool ok = nearby.size() == 200 &&
                        nearby.front()->text == "m5" && nearby.back()->text == "m204" &&
                        all.size() == 205;
        Check("ChatModelChecks: per-channel FIFO 200", ok);
    }
    {
        ChatModel model;
        for (int i = 0; i < 505; ++i) {
            ChatEntry e;
            e.channel = 2;
            e.text = "w" + std::to_string(i);
            model.AddMessage(e);
        }
        const auto all = model.VisibleMessages(0);
        Check("ChatModelChecks: combined FIFO 500", all.size() == 500 &&
                                                        all.front()->text == "w5");
    }
    {
        ChatModel model;
        model.OpenInput(static_cast<std::uint8_t>(chat::ChatChannel::Whisper), "界面英雄");
        Check("ChatModelChecks: open input whisper target",
              model.InputOpen() &&
              model.DraftChannel() == static_cast<std::uint8_t>(chat::ChatChannel::Whisper) &&
              model.WhisperTarget() == "界面英雄");
        // 提交文本吸收（码点安全）。
        model.FeedTextInput({"你好"}, false);
        Check("ChatModelChecks: feed committed text", model.Draft() == "你好");
        model.FeedTextInput({}, true); // 退格删除一个码点
        Check("ChatModelChecks: backspace removes one codepoint", model.Draft() == "你");
        model.FeedTextInput({}, true);
        Check("ChatModelChecks: backspace to empty", model.Draft().empty());
        model.CloseInput();
        Check("ChatModelChecks: close clears draft",
              !model.InputOpen() && model.Draft().empty() && model.WhisperTarget().empty());
    }
    {
        ChatModel model;
        model.OpenInput(1);
        model.FeedTextInput({"/w 界面英雄 早上好"}, false);
        chat::ChatChannel channel = chat::ChatChannel::Nearby;
        std::string target;
        std::string text;
        const bool ok = model.ParseCommand(channel, target, text) &&
                        channel == chat::ChatChannel::Whisper && target == "界面英雄" &&
                        text == "早上好";
        Check("ChatModelChecks: /w command via model", ok);
    }
    {
        // 输入历史：↑↓（指令二十五）。
        ChatModel model;
        model.OpenInput(2);
        model.FeedTextInput({"第一条"}, false);
        model.CommitDraftToHistory();
        model.FeedTextInput({"第二条"}, false);
        model.CommitDraftToHistory();
        model.FeedTextInput({"草稿中"}, false);
        model.NavigateHistory(+1); // ↑ -> 第二条
        Check("ChatModelChecks: up shows latest history", model.Draft() == "第二条");
        model.NavigateHistory(+1); // ↑ -> 第一条
        Check("ChatModelChecks: up again shows older", model.Draft() == "第一条");
        model.NavigateHistory(-1); // ↓ -> 第二条
        Check("ChatModelChecks: down shows newer", model.Draft() == "第二条");
        model.NavigateHistory(-1); // ↓ -> 回到草稿
        Check("ChatModelChecks: down restores draft", model.Draft() == "草稿中");
    }
    {
        // 历史 20 条 FIFO + 相邻去重（指令二十五）。
        ChatModel model;
        model.OpenInput(1);
        for (int i = 0; i < 25; ++i) {
            model.FeedTextInput({"m" + std::to_string(i)}, false);
            model.CommitDraftToHistory();
        }
        // 25 条提交后历史保留最近 20 条（m5..m24）：↑ 20 次应回到最旧 m5。
        for (int i = 0; i < 20; ++i) {
            model.NavigateHistory(+1);
        }
        const bool capped = model.Draft() == "m5";
        model.NavigateHistory(+1); // 超出更早历史：停留在 m5
        Check("ChatModelChecks: input history capped at 20",
              capped && model.Draft() == "m5");
        model.NavigateHistory(-1);
        model.FeedTextInput({"dup"}, false);
        model.CommitDraftToHistory();
        model.FeedTextInput({"dup"}, false);
        model.CommitDraftToHistory();
        Check("ChatModelChecks: draft cleared after commit", model.Draft().empty());
    }
}

// ===========================================================================
// E. 纯逻辑：PlayerIdentity + OnlinePlayerDirectory（指令二十九/三十）
// ===========================================================================

void RunStage27PlayerIdentityChecks() {
    {
        world::PlayerSession player(11, 22, 33, "界面英雄", 1, 1, 5, 1, 10.0f, 20.0f, 3);
        const bool ok = player.VisualId() == 3 && player.Direction() == 0 &&
                        player.CharacterName() == "界面英雄" && player.Level() == 5;
        player.SetDirection(5);
        Check("PlayerIdentityChecks: session visualId + direction", ok && player.Direction() == 5);
    }
    {
        world::OnlinePlayerDirectory directory;
        auto a = std::make_shared<world::PlayerSession>(1, 10, 100, "玩家甲", 1, 1, 1, 1, 0, 0, 1);
        auto b = std::make_shared<world::PlayerSession>(2, 11, 101, "玩家乙", 2, 1, 1, 1, 0, 0, 2);
        directory.Add(a);
        directory.Add(b);
        bool ok = directory.Size() == 2 &&
                  directory.FindByCharacterId(100) == a &&
                  directory.FindByCharacterId(101) == b &&
                  directory.FindByCharacterName("玩家甲") == a &&
                  directory.FindByCharacterName("玩家乙") == b &&
                  directory.FindByCharacterName("不在线") == nullptr;
        directory.RemoveByCharacterId(100);
        ok = ok && directory.Size() == 1 &&
             directory.FindByCharacterId(100) == nullptr &&
             directory.FindByCharacterName("玩家甲") == nullptr &&
             directory.FindByCharacterName("玩家乙") == b;
        Check("PlayerIdentityChecks: online directory O(1) lookup + removal", ok);
    }
    Check("RemoteAppearanceChecks: visual catalog mapping",
          std::string(legend::ui::CharacterVisualEntityName(1)) == "player_warrior" &&
          std::string(legend::ui::CharacterVisualEntityName(2)) == "player_mage" &&
          std::string(legend::ui::CharacterVisualEntityName(3)) == "player_taoist");
    {
        // RemotePlayerEntity 携带服务器权威 visualId（指令三十一）。
        legend::client::RemotePlayerEntity entity;
        world::PlayerSpawnPayload spawn;
        spawn.characterId = 9;
        spawn.name = "玩家乙";
        spawn.visualId = 2;
        spawn.direction = 4;
        spawn.level = 6;
        entity.ApplySpawn(spawn);
        Check("RemoteAppearanceChecks: entity stores server visualId/level",
              entity.VisualId() == 2 && entity.Direction() == 4 && entity.Level() == 6);
    }
}

// ===========================================================================
// F. E2E：内联真实服务器（身份同步 + 四频道 + 限流 + 防重放 + 安全拒绝）
// ===========================================================================

struct Stage27Seed {
    std::uint64_t accountId = 0;
    std::uint64_t characterId = 0;
    std::string name;
};

bool SeedStage27Character(Database& db, AccountService& accounts, CharacterService& characters,
                          const std::string& username, const std::string& charName,
                          std::uint16_t visualId, std::uint16_t mapId, float x, float y,
                          Stage27Seed& out) {
    auto registered = accounts.Register(db, username, "Stage27Pass!");
    if (!registered.success) {
        return false;
    }
    auto created = characters.Create(db, registered.value, charName, 1, 1, visualId);
    if (!created.success) {
        return false;
    }
    out.accountId = registered.value;
    out.characterId = created.value.characterId;
    out.name = charName;
    return account::CharacterRepository::UpdateWorldPosition(db, out.characterId, mapId, x, y,
                                                             account::UnixNow())
        .success;
}

bool EnterCharacter(WorldTestServers& servers, const Stage27Seed& seed, WorldTestClient& client) {
    const std::string ticket = servers.login->Tickets().Create(seed.accountId, seed.characterId, 60.0);
    return !ticket.empty() && client.ConnectAndEnter(ticket, 8000);
}

int CountChatMessages(WorldTestClient& client, const std::string& text) {
    int n = 0;
    for (const auto& e : client.recorded[WorldTestClient::IndexOf(
             WorldNetworkEvent::Type::ChatMessageEvent)]) {
        if (e.chat.text == text) {
            ++n;
        }
    }
    return n;
}

bool WaitChatMessage(WorldTestClient& client, const std::string& text, WorldNetworkEvent& out,
                     int timeoutMs = 3000) {
    const bool got = WaitUntil(
        [&] {
            client.DrainEvents();
            return CountChatMessages(client, text) > 0;
        },
        timeoutMs);
    if (!got) {
        return false;
    }
    // 注意：不消费（peek）——后续 CountChatMessages 负向断言依赖队列完整。
    for (const auto& e : client.recorded[WorldTestClient::IndexOf(
             WorldNetworkEvent::Type::ChatMessageEvent)]) {
        if (e.chat.text == text) {
            out = e;
            return true;
        }
    }
    return false;
}

bool WaitChatResponse(WorldTestClient& client, std::uint64_t requestId, WorldNetworkEvent& out,
                      int timeoutMs = 3000) {
    const int idx = WorldTestClient::IndexOf(WorldNetworkEvent::Type::ChatSendResponseEvent);
    const bool got = WaitUntil(
        [&] {
            client.DrainEvents();
            for (const auto& e : client.recorded[idx]) {
                if (e.chat.requestId == requestId) {
                    return true;
                }
            }
            return false;
        },
        timeoutMs);
    if (!got) {
        return false;
    }
    auto& queue = client.recorded[idx];
    for (auto it = queue.begin(); it != queue.end(); ++it) {
        if (it->chat.requestId == requestId) {
            out = *it;
            queue.erase(it);
            return true;
        }
    }
    return false;
}

constexpr std::uint8_t kNearby = static_cast<std::uint8_t>(chat::ChatChannel::Nearby);
constexpr std::uint8_t kWorld = static_cast<std::uint8_t>(chat::ChatChannel::World);
constexpr std::uint8_t kWhisper = static_cast<std::uint8_t>(chat::ChatChannel::Whisper);
constexpr std::uint8_t kSystem = static_cast<std::uint8_t>(chat::ChatChannel::System);

void RunStage27IdentityAndChatE2E(WorldTestServers& servers) {
    // 数据种子：A(造型1 @(0,0)) / B(造型2 @(30,0)) / C(造型3 @(1900,1900) 超范围) /
    // D(Map2 @(100,100)) / E(离线，私聊目标)。
    Database db;
    std::string dbError;
    Check("Stage27E2E: temp db open", db.Open(servers.dbPath, dbError));
    AccountService accounts;
    CharacterService characters;
    Stage27Seed seedA;
    Stage27Seed seedB;
    Stage27Seed seedC;
    Stage27Seed seedD;
    Stage27Seed seedOffline;
    Check("Stage27E2E: seed A", SeedStage27Character(db, accounts, characters, "stage27_a",
                                                     "玩家甲", 1, 1, 0.0f, 0.0f, seedA));
    Check("Stage27E2E: seed B", SeedStage27Character(db, accounts, characters, "stage27_b",
                                                     "玩家乙", 2, 1, 30.0f, 0.0f, seedB));
    Check("Stage27E2E: seed C (far)", SeedStage27Character(db, accounts, characters, "stage27_c",
                                                           "玩家丙", 3, 1, 1900.0f, 1900.0f, seedC));
    Check("Stage27E2E: seed D (map2)", SeedStage27Character(db, accounts, characters, "stage27_d",
                                                            "玩家丁", 1, 2, 100.0f, 100.0f, seedD));
    Check("Stage27E2E: seed offline E", SeedStage27Character(db, accounts, characters, "stage27_e",
                                                             "玩家戊", 1, 1, 0.0f, 0.0f, seedOffline));

    // 进世界（A/B/C/D；E 不进）。
    WorldTestClient clientA;
    WorldTestClient clientB;
    WorldTestClient clientC;
    WorldTestClient clientD;
    const bool enterA = EnterCharacter(servers, seedA, clientA);
    const bool enterB = EnterCharacter(servers, seedB, clientB);
    const bool enterC = EnterCharacter(servers, seedC, clientC);
    const bool enterD = EnterCharacter(servers, seedD, clientD);
    Check("Stage27E2E: A/B/C/D entered world", enterA && enterB && enterC && enterD);
    if (!(enterA && enterB && enterC && enterD)) {
        return;
    }

    // ---- PlayerIdentity E2E：EnterWorldSuccess 携带服务器权威 visualId ----
    Check("Stage27E2E: A enter response visualId=1 (server authoritative)",
          clientA.LastEnterSuccess().visualId == 1);
    Check("Stage27E2E: B enter response visualId=2",
          clientB.LastEnterSuccess().visualId == 2);

    // ---- RemoteAppearance E2E：A 看到的 B 是造型2；B 看到的 A 是造型1 ----
    WorldNetworkEvent spawnOfB;
    bool sawB = false;
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
        while (std::chrono::steady_clock::now() < deadline) {
            clientA.DrainEvents();
            for (const auto& e :
                 clientA.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::PlayerSpawn)]) {
                if (e.characterId == seedB.characterId) {
                    spawnOfB = e;
                    sawB = true;
                    break;
                }
            }
            if (sawB) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    Check("Stage27E2E: A received B spawn with visualId=2 + name + level",
          sawB && spawnOfB.visualId == 2 && spawnOfB.characterName == "玩家乙" &&
          spawnOfB.level >= 1);
    WorldNetworkEvent spawnOfA;
    bool sawA = false;
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(4000);
        while (std::chrono::steady_clock::now() < deadline) {
            clientB.DrainEvents();
            for (const auto& e :
                 clientB.recorded[WorldTestClient::IndexOf(WorldNetworkEvent::Type::PlayerSpawn)]) {
                if (e.characterId == seedA.characterId) {
                    spawnOfA = e;
                    sawA = true;
                    break;
                }
            }
            if (sawA) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    Check("Stage27E2E: B received A spawn with visualId=1",
          sawA && spawnOfA.visualId == 1 && spawnOfA.characterName == "玩家甲");

    // ---- System 频道：进入世界的欢迎消息（仅服务器产生）----
    WorldNetworkEvent welcome;
    bool gotWelcome = false;
    {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
        while (std::chrono::steady_clock::now() < deadline) {
            clientA.DrainEvents();
            for (auto& queue = clientA.recorded[WorldTestClient::IndexOf(
                     WorldNetworkEvent::Type::ChatMessageEvent)];
                 !queue.empty();) {
                if (queue.front().chat.channel == kSystem) {
                    welcome = queue.front();
                    queue.pop_front();
                    gotWelcome = true;
                    break;
                }
                queue.pop_front();
            }
            if (gotWelcome) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }
    Check("Stage27E2E: system welcome message on enter",
          gotWelcome && welcome.chat.text == "欢迎进入LegendGame" &&
          welcome.chat.senderName.empty() && welcome.chat.senderCharacterId == 0);

    // ---- Nearby：A 发 -> A/B 收；C(超范围)/D(跨地图) 不收（指令九/三十四）----
    clientA.controller.SendChat(kNearby, "", "附近你好");
    WorldNetworkEvent nearbyEcho;
    const bool aGot = WaitChatMessage(clientA, "附近你好", nearbyEcho);
    const bool bGot = WaitChatMessage(clientB, "附近你好", nearbyEcho);
    TestSleep(400); // 给超范围/跨地图投递留时间（负向断言）
    clientC.DrainEvents();
    clientD.DrainEvents();
    const bool ok = aGot && bGot && nearbyEcho.chat.channel == kNearby &&
                    nearbyEcho.chat.senderName == "玩家甲" &&
                    CountChatMessages(clientC, "附近你好") == 0 &&
                    CountChatMessages(clientD, "附近你好") == 0;
    Check("Stage27E2E: nearby broadcast (range + cross-map isolated)", ok);

    // ---- World：A 发 -> A/B/C/D 全收（指令十）----
    clientA.controller.SendChat(kWorld, "", "世界你好");
    const bool cGotWorld = WaitChatMessage(clientC, "世界你好", nearbyEcho);
    const bool dGotWorld = WaitChatMessage(clientD, "世界你好", nearbyEcho);
    const bool bGotWorld = WaitChatMessage(clientB, "世界你好", nearbyEcho);
    Check("Stage27E2E: world broadcast to all maps", cGotWorld && dGotWorld && bGotWorld);

    // ---- Whisper：A -> B；只有 A/B 收到（指令十一）----
    clientA.controller.SendChat(kWhisper, "玩家乙", "私下聊聊");
    WorldNetworkEvent whisperEvent;
    const bool bGotWhisper = WaitChatMessage(clientB, "私下聊聊", whisperEvent);
    const bool aGotWhisper = WaitChatMessage(clientA, "私下聊聊", whisperEvent);
    TestSleep(400); // 负向断言：C/D 不收
    clientC.DrainEvents();
    clientD.DrainEvents();
    Check("Stage27E2E: whisper only to target + sender",
          bGotWhisper && aGotWhisper && whisperEvent.chat.channel == kWhisper &&
          whisperEvent.chat.targetName == "玩家乙" &&
          CountChatMessages(clientC, "私下聊聊") == 0 &&
          CountChatMessages(clientD, "私下聊聊") == 0);

    // ---- Whisper 离线目标 -> "该玩家当前不在线"（指令十一）----
    const std::uint64_t offlineReq = 9101;
    clientA.controller.SendChatForTest(offlineReq, kWhisper, "玩家戊", "在吗");
    WorldNetworkEvent offlineResp;
    bool gotOfflineResp = WaitChatResponse(clientA, offlineReq, offlineResp);
    Check("Stage27E2E: whisper offline -> TargetOffline + user text",
          gotOfflineResp && !offlineResp.chat.success &&
          offlineResp.chat.errorCode ==
              static_cast<std::uint16_t>(chat::ChatErrorCode::TargetOffline) &&
          offlineResp.chat.errorMessage == "该玩家当前不在线");
    TestSleep(300);
    clientA.DrainEvents();
    Check("Stage27E2E: whisper offline not broadcast", CountChatMessages(clientA, "在吗") == 0);

    // ---- Security：伪造 System 频道拒绝（指令十二）----
    const std::uint64_t sysReq = 9102;
    clientA.controller.SendChatForTest(sysReq, kSystem, "", "假装系统");
    WorldNetworkEvent sysResp;
    const bool sysRejected = WaitChatResponse(clientA, sysReq, sysResp) &&
                             !sysResp.chat.success &&
                             sysResp.chat.errorCode ==
                                 static_cast<std::uint16_t>(chat::ChatErrorCode::SystemChannelForbidden);
    WaitUntil([] { return true; }, 0);
    TestSleep(300);
    clientB.DrainEvents();
    Check("Stage27E2E: forged System message rejected + not broadcast",
          sysRejected && CountChatMessages(clientB, "假装系统") == 0);

    // ---- Security：非法 channel 拒绝（指令十七）----
    const std::uint64_t badChReq = 9103;
    clientA.controller.SendChatForTest(badChReq, 99, "", "非法频道");
    WorldNetworkEvent badChResp;
    Check("Stage27E2E: invalid channel rejected",
          WaitChatResponse(clientA, badChReq, badChResp) && !badChResp.chat.success &&
          badChResp.chat.errorCode ==
              static_cast<std::uint16_t>(chat::ChatErrorCode::InvalidChannel));

    // ---- Security：非法 UTF-8 / 控制字符 / 空 / 纯空格（指令十四/二十七）----
    const std::uint64_t badUtf8Req = 9104;
    clientA.controller.SendChatForTest(badUtf8Req, kNearby, "", std::string("\xC3\x28hi"));
    WorldNetworkEvent badUtf8Resp;
    const bool badUtf8 = WaitChatResponse(clientA, badUtf8Req, badUtf8Resp) &&
                         !badUtf8Resp.chat.success &&
                         badUtf8Resp.chat.errorCode ==
                             static_cast<std::uint16_t>(chat::ChatErrorCode::InvalidUtf8);
    const std::uint64_t ctrlReq = 9105;
    clientA.controller.SendChatForTest(ctrlReq, kNearby, "", std::string("ab\nc"));
    WorldNetworkEvent ctrlResp;
    const bool ctrl = WaitChatResponse(clientA, ctrlReq, ctrlResp) && !ctrlResp.chat.success;
    const std::uint64_t emptyReq = 9106;
    clientA.controller.SendChatForTest(emptyReq, kNearby, "", "");
    WorldNetworkEvent emptyResp;
    const bool empty = WaitChatResponse(clientA, emptyReq, emptyResp) && !emptyResp.chat.success;
    const std::uint64_t blankReq = 9107;
    clientA.controller.SendChatForTest(blankReq, kNearby, "", "   ");
    WorldNetworkEvent blankResp;
    const bool blank = WaitChatResponse(clientA, blankReq, blankResp) && !blankResp.chat.success;
    const std::uint64_t longReq = 9108;
    {
        std::string longText;
        for (int i = 0; i < 121; ++i) {
            longText += "界";
        }
        clientA.controller.SendChatForTest(longReq, kNearby, "", longText);
    }
    WorldNetworkEvent longResp;
    const bool tooLong = WaitChatResponse(clientA, longReq, longResp) && !longResp.chat.success &&
                         longResp.chat.errorCode ==
                             static_cast<std::uint16_t>(chat::ChatErrorCode::TextTooLong);
    Check("Stage27E2E: malformed content rejected (utf8/control/empty/blank/overlong)",
          badUtf8 && ctrl && empty && blank && tooLong);

    // ---- RateLimit：注入窗口（world 400ms/1 条）——第二连发被拒（指令三十五）----
    const std::uint64_t rateReq1 = 9201;
    clientA.controller.SendChatForTest(rateReq1, kWorld, "", "限流第一条");
    WorldNetworkEvent rateResp1;
    const bool rate1 = WaitChatResponse(clientA, rateReq1, rateResp1) && rateResp1.chat.success;
    const std::uint64_t rateReq2 = 9202;
    clientA.controller.SendChatForTest(rateReq2, kWorld, "", "限流第二条");
    WorldNetworkEvent rateResp2;
    const bool rate2 = WaitChatResponse(clientA, rateReq2, rateResp2) && !rateResp2.chat.success &&
                       rateResp2.chat.errorCode ==
                           static_cast<std::uint16_t>(chat::ChatErrorCode::RateLimited);
    Check("Stage27E2E: world rate limit rejects immediate resend", rate1 && rate2);
    TestSleep(450); // 窗口 400ms 过后（真实短等待，非长 sleep）
    const std::uint64_t rateReq3 = 9203;
    clientA.controller.SendChatForTest(rateReq3, kWorld, "", "限流第三条");
    WorldNetworkEvent rateResp3;
    Check("Stage27E2E: after window the same channel recovers",
          WaitChatResponse(clientA, rateReq3, rateResp3) && rateResp3.chat.success);

    // ---- Replay：重复 requestId 不重复广播（指令十六）----
    const std::uint64_t replayReq = 9301;
    clientA.controller.SendChatForTest(replayReq, kNearby, "", "重放测试");
    WorldNetworkEvent replayEcho;
    Check("Stage27E2E: replay probe first send", WaitChatMessage(clientA, "重放测试", replayEcho));
    clientA.controller.SendChatForTest(replayReq, kNearby, "", "重放测试");
    WorldNetworkEvent replayResp;
    const bool replayIdempotent = WaitChatResponse(clientA, replayReq, replayResp) &&
                                  replayResp.chat.success; // 幂等成功
    TestSleep(400);
    clientA.DrainEvents();
    clientB.DrainEvents();
    Check("Stage27E2E: duplicate requestId not broadcast twice",
          replayIdempotent && CountChatMessages(clientA, "重放测试") == 1 &&
          CountChatMessages(clientB, "重放测试") == 1);

    // ---- 重登后 visualId 仍一致（指令三十二：重登一致）----
    clientA.Disconnect();
    TestSleep(400); // 服务器异步移除在线会话
    WorldTestClient clientA2;
    const bool reenterA = EnterCharacter(servers, seedA, clientA2);
    Check("Stage27E2E: A re-enter visualId=1 persisted", reenterA &&
                                                            clientA2.LastEnterSuccess().visualId == 1);
    if (reenterA) {
        clientA2.Disconnect();
    }
    clientB.Disconnect();
    clientC.Disconnect();
    clientD.Disconnect();
    TestSleep(200);
}

} // namespace（anonymous）

// Stage27 指令四十：套件入口（WorldChecks.cpp main 调度）。
int RunStage27ChatChecks(WorldTestServers& servers) {
    RunStage27CodecChecks();
    RunStage27ValidationChecks();
    RunStage27RateLimitPureChecks();
    RunStage27ChatModelChecks();
    RunStage27PlayerIdentityChecks();
    RunStage27IdentityAndChatE2E(servers);
    return 0;
}

} // namespace worldtest
