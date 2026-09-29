#pragma once

#include <cstddef>
#include <cstdint>

namespace legend::world {

// ---------------------------------------------------------------------------
// 阶段20：Shop 公共类型。
// ---------------------------------------------------------------------------

// Shop entries 上限（指令九十）：Decode >64 拒绝。
inline constexpr std::size_t kMaxShopEntries = 64;

// Shop 结果码（指令四十二~五十六）。
enum class ShopResultCode : std::uint8_t {
    Success = 0,
    MalformedRequest = 1,
    NotInWorld = 2,
    Dead = 3,
    SessionNotFound = 4,
    SessionExpired = 5,
    NpcGone = 6,
    WrongMap = 7,
    TooFar = 8,
    ShopNotFound = 9,
    ItemNotInShop = 10,
    CannotBuy = 11,
    CannotSell = 12,
    InvalidQuantity = 13,
    NotEnoughGold = 14,
    InventoryFull = 15,
    ItemNotFound = 16, // 背包无此 instanceId / 数量不足
    EquippedItem = 17, // 装备中不能直接卖（指令五十一）
    DuplicateRequest = 18,
    InternalError = 19,
};

inline const char* ShopResultCodeName(std::uint8_t code) {
    switch (static_cast<ShopResultCode>(code)) {
        case ShopResultCode::Success: return "Success";
        case ShopResultCode::MalformedRequest: return "MalformedRequest";
        case ShopResultCode::NotInWorld: return "NotInWorld";
        case ShopResultCode::Dead: return "Dead";
        case ShopResultCode::SessionNotFound: return "SessionNotFound";
        case ShopResultCode::SessionExpired: return "SessionExpired";
        case ShopResultCode::NpcGone: return "NpcGone";
        case ShopResultCode::WrongMap: return "WrongMap";
        case ShopResultCode::TooFar: return "TooFar";
        case ShopResultCode::ShopNotFound: return "ShopNotFound";
        case ShopResultCode::ItemNotInShop: return "ItemNotInShop";
        case ShopResultCode::CannotBuy: return "CannotBuy";
        case ShopResultCode::CannotSell: return "CannotSell";
        case ShopResultCode::InvalidQuantity: return "InvalidQuantity";
        case ShopResultCode::NotEnoughGold: return "NotEnoughGold";
        case ShopResultCode::InventoryFull: return "InventoryFull";
        case ShopResultCode::ItemNotFound: return "ItemNotFound";
        case ShopResultCode::EquippedItem: return "EquippedItem";
        case ShopResultCode::DuplicateRequest: return "DuplicateRequest";
        case ShopResultCode::InternalError: return "InternalError";
    }
    return "Unknown";
}

} // namespace legend::world
