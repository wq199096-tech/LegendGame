#include "Shared/InternalProtocol/PersistenceMessages.h"

#include <cstdio>

int RunPersistenceRpcChecks() {
    using namespace legend::internal;
    int failures = 0;
    const auto check = [&](const char* name, bool ok) {
        std::printf("[%s] %s\n", ok ? "PASS" : "FAIL", name);
        if (!ok) ++failures;
    };
    CharacterCreateCommand command{42, "RpcHero", 1, 2};
    std::vector<std::uint8_t> bytes;
    std::string error;
    CharacterCreateCommand decoded;
    check("PersistenceRpcChecks: typed create command",
          EncodeCharacterCreateCommand(command, bytes) &&
              DecodeCharacterCreateCommand(bytes.data(), bytes.size(), decoded, error) &&
              decoded.accountId == 42 && decoded.name == "RpcHero");
    std::vector<legend::account::CharacterSummary> list{{7, "Hero", 1, 1, 3, 2, 99}};
    std::vector<legend::account::CharacterSummary> decodedList;
    check("PersistenceRpcChecks: typed character list",
          EncodeCharacterList(list, bytes) &&
              DecodeCharacterList(bytes.data(), bytes.size(), decodedList, error) &&
              decodedList.size() == 1 && decodedList[0].mapId == 2);

    // ---- 阶段25.5：World 持久化协议编解码（严格解码 + 截断拒绝） ----
    WorldCharacterRow characterRow{9, 42, "RpcWorldHero", 1, 1, 4, 320, 77, 2, 123.5f, 456.25f,
                                   false};
    WorldCharacterRow decodedRow;
    check("PersistenceRpcChecks: world character row roundtrip",
          EncodeWorldCharacterRow(characterRow, bytes) &&
              DecodeWorldCharacterRow(bytes.data(), bytes.size(), decodedRow, error) &&
              decodedRow.id == 9 && decodedRow.name == "RpcWorldHero" &&
              decodedRow.gold == 77 && decodedRow.mapId == 2 && !decodedRow.deleted);

    WorldInventoryList inventoryList{42, {{1, 3001, 8, 1001, 1700000000},
                                          {2, 3002, 1, 7, 1700000001}}};
    WorldInventoryList decodedInventory;
    check("PersistenceRpcChecks: world inventory roundtrip",
          EncodeWorldInventoryList(inventoryList, bytes) &&
              DecodeWorldInventoryList(bytes.data(), bytes.size(), decodedInventory, error) &&
              decodedInventory.items.size() == 2 && decodedInventory.items[0].slotIndex == 1001 &&
              decodedInventory.items[1].quantity == 1);

    WorldQuestStateList questList{{{4001, 2, 1700000000, 0, 0}}, {{4001, 1, 3}, {4001, 2, 0}}};
    WorldQuestStateList decodedQuests;
    check("PersistenceRpcChecks: world quest list roundtrip",
          EncodeWorldQuestStateList(questList, bytes) &&
              DecodeWorldQuestStateList(bytes.data(), bytes.size(), decodedQuests, error) &&
              decodedQuests.quests.size() == 1 && decodedQuests.objectives.size() == 2 &&
              decodedQuests.objectives[0].progress == 3);

    WorldItemInsert insert{42, 3001, 3, 0, 1700000000, false, 0, 0};
    WorldItemInsert decodedInsert;
    check("PersistenceRpcChecks: world item insert roundtrip",
          EncodeWorldItemInsert(insert, bytes) &&
              DecodeWorldItemInsert(bytes.data(), bytes.size(), decodedInsert, error) &&
              decodedInsert.definitionId == 3001 && decodedInsert.quantity == 3 &&
              !decodedInsert.mergedIntoStack);

    WorldShopBuy shopBuy{42, 50, 3002, 1, 7, false, 0, 0};
    WorldShopBuy decodedShopBuy;
    check("PersistenceRpcChecks: world shop buy roundtrip",
          EncodeWorldShopBuy(shopBuy, bytes) &&
              DecodeWorldShopBuy(bytes.data(), bytes.size(), decodedShopBuy, error) &&
              decodedShopBuy.newGold == 50 && decodedShopBuy.itemDefinitionId == 3002);

    check("PersistenceRpcChecks: truncated inventory rejected",
          EncodeWorldInventoryList(inventoryList, bytes) &&
              !DecodeWorldInventoryList(bytes.data(), bytes.size() - 4, decodedInventory, error));
    check("PersistenceRpcChecks: truncated quest list rejected",
          EncodeWorldQuestStateList(questList, bytes) &&
              !DecodeWorldQuestStateList(bytes.data(), bytes.size() - 2, decodedQuests, error));
    return failures;
}
