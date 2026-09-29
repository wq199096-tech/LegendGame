#pragma once

#include "Shared/Shop/ShopTypes.h"

#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20 指令三十九~五十五：Shop 协议。
// MessageId：ShopOpenRequest=327 / ShopOpenResponse=328 / ShopBuyRequest=329 /
// ShopBuyResponse=330 / ShopSellRequest=331 / ShopSellResponse=332。
// Client 绝不能上传价格（指令三十八）：只传 shopSessionId/itemDefinitionId/quantity/
// inventoryInstanceId；entries 上限 64（指令九十）。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令八十九）。
// ---------------------------------------------------------------------------

// ShopOpenRequest(327)（指令三十九：必须经有效 Dialogue Session）。
struct ShopOpenRequestPayload {
    std::uint64_t requestId = 0;
    std::uint64_t dialogueSessionId = 0;
};

struct ShopEntryData {
    std::uint32_t itemDefinitionId = 0;
    std::uint32_t buyPrice = 0;
    std::uint32_t sellPrice = 0;
    bool canBuy = false;
    bool canSell = false;
};

// ShopOpenResponse(328)（指令四十）。
struct ShopOpenResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint8_t resultCode = 0; // ShopResultCode
    std::uint64_t shopSessionId = 0;
    std::uint32_t shopId = 0;
    std::uint64_t npcEntityId = 0;
    std::vector<ShopEntryData> entries;
};

// ShopBuyRequest(329)（指令四十二）。
struct ShopBuyRequestPayload {
    std::uint64_t requestId = 0;
    std::uint64_t shopSessionId = 0;
    std::uint32_t itemDefinitionId = 0;
    std::uint32_t quantity = 1;
};

// ShopBuyResponse(330)（指令四十七）。
struct ShopBuyResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint8_t resultCode = 0; // ShopResultCode
    std::uint32_t itemDefinitionId = 0;
    std::uint32_t quantity = 0;
    std::uint32_t goldSpent = 0;
    std::int64_t newGold = 0;
};

// ShopSellRequest(331)（指令四十九：Client 只发背包 instanceId + quantity）。
struct ShopSellRequestPayload {
    std::uint64_t requestId = 0;
    std::uint64_t shopSessionId = 0;
    std::uint64_t inventoryInstanceId = 0;
    std::uint32_t quantity = 1;
};

// ShopSellResponse(332)（指令五十五）。
struct ShopSellResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint8_t resultCode = 0; // ShopResultCode
    std::uint32_t itemDefinitionId = 0;
    std::uint32_t quantity = 0;
    std::uint32_t goldReceived = 0;
    std::int64_t newGold = 0;
};

bool EncodeShopOpenRequest(const ShopOpenRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeShopOpenRequest(const std::uint8_t* data, std::size_t size,
                           ShopOpenRequestPayload& out, std::string& error);
bool EncodeShopOpenResponse(const ShopOpenResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeShopOpenResponse(const std::uint8_t* data, std::size_t size,
                            ShopOpenResponsePayload& out, std::string& error);
bool EncodeShopBuyRequest(const ShopBuyRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeShopBuyRequest(const std::uint8_t* data, std::size_t size, ShopBuyRequestPayload& out,
                          std::string& error);
bool EncodeShopBuyResponse(const ShopBuyResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeShopBuyResponse(const std::uint8_t* data, std::size_t size, ShopBuyResponsePayload& out,
                           std::string& error);
bool EncodeShopSellRequest(const ShopSellRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeShopSellRequest(const std::uint8_t* data, std::size_t size, ShopSellRequestPayload& out,
                           std::string& error);
bool EncodeShopSellResponse(const ShopSellResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeShopSellResponse(const std::uint8_t* data, std::size_t size,
                            ShopSellResponsePayload& out, std::string& error);

} // namespace legend::world
