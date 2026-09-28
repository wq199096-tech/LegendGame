#include "Shared/Status/StatusEffectProtocol.h"

#include "Shared/Status/StatusEffectTypes.h"
#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {
namespace {

// 与 SkillProtocol.cpp 相同模式：Encode 失败清空 out；Decode 必须完整消费
//（指令八十二），Snapshot count 超上限拒绝（指令五十六）。
template <typename Fn>
bool EncodePayload(std::vector<std::uint8_t>& out, Fn&& fill) {
    out.clear();
    legend::network::ByteWriter writer(out);
    if (!fill(writer)) {
        out.clear();
        return false;
    }
    return true;
}

template <typename Fn>
bool DecodePayload(const std::uint8_t* data, std::size_t size, std::string& error, Fn&& fill) {
    legend::network::ByteReader reader(data, size);
    fill(reader);
    if (!reader.IsValid()) {
        error = "malformed status payload";
        return false;
    }
    if (reader.Remaining() != 0) {
        error = "malformed status payload (trailing bytes)";
        return false;
    }
    return true;
}

} // namespace

bool EncodeStatusEffectApplied(const StatusEffectAppliedPayload& p,
                               std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.instanceId);
        w.WriteUInt32(p.effectId);
        w.WriteUInt8(p.targetType);
        w.WriteUInt64(p.targetEntityId);
        w.WriteUInt8(p.sourceType);
        w.WriteUInt64(p.sourceEntityId);
        w.WriteUInt32(p.sourceSkillId);
        w.WriteUInt8(p.stacks);
        w.WriteUInt32(p.durationMs);
        w.WriteUInt32(p.remainingMs);
        w.WriteUInt64(p.serverTime);
        return true;
    });
}

bool DecodeStatusEffectApplied(const std::uint8_t* data, std::size_t size,
                               StatusEffectAppliedPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.instanceId = r.ReadUInt64();
        out.effectId = r.ReadUInt32();
        out.targetType = r.ReadUInt8();
        out.targetEntityId = r.ReadUInt64();
        out.sourceType = r.ReadUInt8();
        out.sourceEntityId = r.ReadUInt64();
        out.sourceSkillId = r.ReadUInt32();
        out.stacks = r.ReadUInt8();
        out.durationMs = r.ReadUInt32();
        out.remainingMs = r.ReadUInt32();
        out.serverTime = r.ReadUInt64();
    });
}

bool EncodeStatusEffectUpdated(const StatusEffectUpdatedPayload& p,
                               std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.instanceId);
        w.WriteUInt32(p.effectId);
        w.WriteUInt8(p.targetType);
        w.WriteUInt64(p.targetEntityId);
        w.WriteUInt8(p.stacks);
        w.WriteUInt32(p.remainingMs);
        w.WriteUInt64(p.serverTime);
        return true;
    });
}

bool DecodeStatusEffectUpdated(const std::uint8_t* data, std::size_t size,
                               StatusEffectUpdatedPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.instanceId = r.ReadUInt64();
        out.effectId = r.ReadUInt32();
        out.targetType = r.ReadUInt8();
        out.targetEntityId = r.ReadUInt64();
        out.stacks = r.ReadUInt8();
        out.remainingMs = r.ReadUInt32();
        out.serverTime = r.ReadUInt64();
    });
}

bool EncodeStatusEffectRemoved(const StatusEffectRemovedPayload& p,
                               std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.instanceId);
        w.WriteUInt32(p.effectId);
        w.WriteUInt8(p.targetType);
        w.WriteUInt64(p.targetEntityId);
        w.WriteUInt8(p.reason);
        w.WriteUInt64(p.serverTime);
        return true;
    });
}

bool DecodeStatusEffectRemoved(const std::uint8_t* data, std::size_t size,
                               StatusEffectRemovedPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.instanceId = r.ReadUInt64();
        out.effectId = r.ReadUInt32();
        out.targetType = r.ReadUInt8();
        out.targetEntityId = r.ReadUInt64();
        out.reason = r.ReadUInt8();
        out.serverTime = r.ReadUInt64();
    });
}

bool EncodeStatusEffectSnapshot(const StatusEffectSnapshotPayload& p,
                                std::vector<std::uint8_t>& out) {
    // 指令五十六：Encode 超过 32 拒绝。
    if (p.effects.size() > kStatusEffectMaxSnapshotCount) {
        out.clear();
        return false;
    }
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt8(p.targetType);
        w.WriteUInt64(p.targetEntityId);
        w.WriteUInt64(p.serverTime);
        w.WriteUInt16(static_cast<std::uint16_t>(p.effects.size()));
        for (const auto& effect : p.effects) {
            w.WriteUInt64(effect.instanceId);
            w.WriteUInt32(effect.effectId);
            w.WriteUInt8(effect.stacks);
            w.WriteUInt32(effect.remainingMs);
        }
        return true;
    });
}

bool DecodeStatusEffectSnapshot(const std::uint8_t* data, std::size_t size,
                                StatusEffectSnapshotPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.targetType = r.ReadUInt8();
        out.targetEntityId = r.ReadUInt64();
        out.serverTime = r.ReadUInt64();
        const std::uint16_t count = r.ReadUInt16();
        if (count > kStatusEffectMaxSnapshotCount) {
            // 指令五十六/一百二十七：count 超限 -> Decode 失败。
            r.Invalidate();
            return;
        }
        out.effects.reserve(count);
        for (std::uint16_t i = 0; i < count; ++i) {
            StatusEffectSnapshotEntry effect;
            effect.instanceId = r.ReadUInt64();
            effect.effectId = r.ReadUInt32();
            effect.stacks = r.ReadUInt8();
            effect.remainingMs = r.ReadUInt32();
            if (!r.IsValid()) {
                return; // 指令一百二十九：count 大于 payload -> 失败
            }
            out.effects.push_back(effect);
        }
    });
}

} // namespace legend::world
