#include "Shared/Shop/ShopProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {

namespace {
using legend::network::ByteReader;
using legend::network::ByteWriter;
// ByteWriter/ByteReader 无 Int64 接口——gold 非负（<2^63），用 UInt64 位模式传输。
inline std::uint64_t ToBits(std::int64_t v) { return static_cast<std::uint64_t>(v); }
inline std::int64_t FromBits(std::uint64_t v) { return static_cast<std::int64_t>(v); }
} // namespace

// ShopOpenRequest(327)。
bool EncodeShopOpenRequest(const ShopOpenRequestPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt64(p.dialogueSessionId);
    return true;
}

bool DecodeShopOpenRequest(const std::uint8_t* data, std::size_t size,
                           ShopOpenRequestPayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.dialogueSessionId = r.ReadUInt64();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed ShopOpenRequest payload";
        return false;
    }
    return true;
}

// ShopOpenResponse(328)。entries >64 Encode 截断（指令九十）。
bool EncodeShopOpenResponse(const ShopOpenResponsePayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteBool(p.success);
    w.WriteUInt8(p.resultCode);
    w.WriteUInt64(p.shopSessionId);
    w.WriteUInt32(p.shopId);
    w.WriteUInt64(p.npcEntityId);
    const std::size_t count =
        p.entries.size() < kMaxShopEntries ? p.entries.size() : kMaxShopEntries;
    w.WriteUInt8(static_cast<std::uint8_t>(count));
    for (std::size_t i = 0; i < count; ++i) {
        w.WriteUInt32(p.entries[i].itemDefinitionId);
        w.WriteUInt32(p.entries[i].buyPrice);
        w.WriteUInt32(p.entries[i].sellPrice);
        w.WriteBool(p.entries[i].canBuy);
        w.WriteBool(p.entries[i].canSell);
    }
    return true;
}

bool DecodeShopOpenResponse(const std::uint8_t* data, std::size_t size,
                            ShopOpenResponsePayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.success = r.ReadBool();
    out.resultCode = r.ReadUInt8();
    out.shopSessionId = r.ReadUInt64();
    out.shopId = r.ReadUInt32();
    out.npcEntityId = r.ReadUInt64();
    const std::uint8_t entryCount = r.ReadUInt8();
    if (entryCount > kMaxShopEntries) {
        r.Invalidate();
    }
    out.entries.clear();
    for (std::uint8_t i = 0; i < entryCount && r.IsValid(); ++i) {
        ShopEntryData entry;
        entry.itemDefinitionId = r.ReadUInt32();
        entry.buyPrice = r.ReadUInt32();
        entry.sellPrice = r.ReadUInt32();
        entry.canBuy = r.ReadBool();
        entry.canSell = r.ReadBool();
        out.entries.push_back(entry);
    }
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed ShopOpenResponse payload";
        return false;
    }
    return true;
}

// ShopBuyRequest(329)。
bool EncodeShopBuyRequest(const ShopBuyRequestPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt64(p.shopSessionId);
    w.WriteUInt32(p.itemDefinitionId);
    w.WriteUInt32(p.quantity);
    return true;
}

bool DecodeShopBuyRequest(const std::uint8_t* data, std::size_t size, ShopBuyRequestPayload& out,
                          std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.shopSessionId = r.ReadUInt64();
    out.itemDefinitionId = r.ReadUInt32();
    out.quantity = r.ReadUInt32();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed ShopBuyRequest payload";
        return false;
    }
    return true;
}

// ShopBuyResponse(330)。
bool EncodeShopBuyResponse(const ShopBuyResponsePayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteBool(p.success);
    w.WriteUInt8(p.resultCode);
    w.WriteUInt32(p.itemDefinitionId);
    w.WriteUInt32(p.quantity);
    w.WriteUInt32(p.goldSpent);
    w.WriteUInt64(ToBits(p.newGold));
    return true;
}

bool DecodeShopBuyResponse(const std::uint8_t* data, std::size_t size, ShopBuyResponsePayload& out,
                           std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.success = r.ReadBool();
    out.resultCode = r.ReadUInt8();
    out.itemDefinitionId = r.ReadUInt32();
    out.quantity = r.ReadUInt32();
    out.goldSpent = r.ReadUInt32();
    out.newGold = FromBits(r.ReadUInt64());
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed ShopBuyResponse payload";
        return false;
    }
    return true;
}

// ShopSellRequest(331)。
bool EncodeShopSellRequest(const ShopSellRequestPayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteUInt64(p.shopSessionId);
    w.WriteUInt64(p.inventoryInstanceId);
    w.WriteUInt32(p.quantity);
    return true;
}

bool DecodeShopSellRequest(const std::uint8_t* data, std::size_t size, ShopSellRequestPayload& out,
                           std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.shopSessionId = r.ReadUInt64();
    out.inventoryInstanceId = r.ReadUInt64();
    out.quantity = r.ReadUInt32();
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed ShopSellRequest payload";
        return false;
    }
    return true;
}

// ShopSellResponse(332)。
bool EncodeShopSellResponse(const ShopSellResponsePayload& p, std::vector<std::uint8_t>& out) {
    out.clear();
    ByteWriter w(out);
    w.WriteUInt64(p.requestId);
    w.WriteBool(p.success);
    w.WriteUInt8(p.resultCode);
    w.WriteUInt32(p.itemDefinitionId);
    w.WriteUInt32(p.quantity);
    w.WriteUInt32(p.goldReceived);
    w.WriteUInt64(ToBits(p.newGold));
    return true;
}

bool DecodeShopSellResponse(const std::uint8_t* data, std::size_t size,
                            ShopSellResponsePayload& out, std::string& error) {
    ByteReader r(data, size);
    out.requestId = r.ReadUInt64();
    out.success = r.ReadBool();
    out.resultCode = r.ReadUInt8();
    out.itemDefinitionId = r.ReadUInt32();
    out.quantity = r.ReadUInt32();
    out.goldReceived = r.ReadUInt32();
    out.newGold = FromBits(r.ReadUInt64());
    if (!r.IsValid() || r.Remaining() != 0) {
        error = "malformed ShopSellResponse payload";
        return false;
    }
    return true;
}

} // namespace legend::world
