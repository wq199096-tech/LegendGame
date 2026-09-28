#include "Shared/World/WorldProtocol.h"

#include "Shared/Network/ByteReader.h"
#include "Shared/Network/ByteWriter.h"

namespace legend::world {
namespace {

// 与 Shared/Network/Protocol.cpp 相同的模式：Encode 失败清空 out（不产生半包），
// Decode 必须完整消费（剩余字节 = malformed，指令六十三）。
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
        error = "malformed world payload";
        return false;
    }
    if (reader.Remaining() != 0) {
        error = "malformed world payload (trailing bytes)";
        return false;
    }
    return true;
}

} // namespace

bool EncodeEnterWorldRequest(const EnterWorldRequestPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        return w.WriteString(p.selectionTicket);
    });
}

bool DecodeEnterWorldRequest(const std::uint8_t* data, std::size_t size,
                             EnterWorldRequestPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        (void)(r.ReadString(out.selectionTicket));
    });
}

bool EncodeEnterWorldResponse(const EnterWorldResponsePayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteBool(p.success);
        w.WriteUInt64(p.accountId);
        w.WriteUInt64(p.characterId);
        w.WriteUInt16(p.classId);
        w.WriteUInt16(p.gender);
        w.WriteUInt32(p.level);
        w.WriteUInt16(p.mapId);
        w.WriteFloat(p.positionX);
        w.WriteFloat(p.positionY);
        w.WriteUInt64(p.serverTime);
        // 阶段14 指令十七：玩家 HP
        w.WriteUInt32(p.currentHp);
        w.WriteUInt32(p.maxHp);
        w.WriteBool(p.alive);
        // 阶段15 指令六十九：玩家 Mana
        w.WriteUInt32(p.currentMana);
        w.WriteUInt32(p.maxMana);
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.characterName) && w.WriteString(p.message);
    });
}

bool DecodeEnterWorldResponse(const std::uint8_t* data, std::size_t size,
                              EnterWorldResponsePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.success = r.ReadBool();
        out.accountId = r.ReadUInt64();
        out.characterId = r.ReadUInt64();
        out.classId = r.ReadUInt16();
        out.gender = r.ReadUInt16();
        out.level = r.ReadUInt32();
        out.mapId = r.ReadUInt16();
        out.positionX = r.ReadFloat();
        out.positionY = r.ReadFloat();
        out.serverTime = r.ReadUInt64();
        out.currentHp = r.ReadUInt32();
        out.maxHp = r.ReadUInt32();
        out.alive = r.ReadBool();
        out.currentMana = r.ReadUInt32();
        out.maxMana = r.ReadUInt32();
        out.errorCode = r.ReadUInt16();
        (void)(r.ReadString(out.characterName) && r.ReadString(out.message));
    });
}

bool EncodeWorldDisconnectNotice(const WorldDisconnectNoticePayload& p,
                                 std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        return w.WriteString(p.reason);
    });
}

bool DecodeWorldDisconnectNotice(const std::uint8_t* data, std::size_t size,
                                 WorldDisconnectNoticePayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        (void)(r.ReadString(out.reason));
    });
}

bool EncodePlayerMoveInput(const PlayerMoveInputPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt32(p.inputSequence);
        w.WriteFloat(p.directionX);
        w.WriteFloat(p.directionY);
        w.WriteFloat(p.deltaTime);
        return true;
    });
}

bool DecodePlayerMoveInput(const std::uint8_t* data, std::size_t size,
                           PlayerMoveInputPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.inputSequence = r.ReadUInt32();
        out.directionX = r.ReadFloat();
        out.directionY = r.ReadFloat();
        out.deltaTime = r.ReadFloat();
    });
}

bool EncodePlayerPositionSnapshot(const PlayerPositionSnapshotPayload& p,
                                  std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.characterId);
        w.WriteFloat(p.positionX);
        w.WriteFloat(p.positionY);
        w.WriteUInt32(p.lastProcessedInputSequence);
        w.WriteUInt64(p.serverTime);
        return true;
    });
}

bool DecodePlayerPositionSnapshot(const std::uint8_t* data, std::size_t size,
                                  PlayerPositionSnapshotPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.characterId = r.ReadUInt64();
        out.positionX = r.ReadFloat();
        out.positionY = r.ReadFloat();
        out.lastProcessedInputSequence = r.ReadUInt32();
        out.serverTime = r.ReadUInt64();
    });
}

// ---------------------------------------------------------------------------
// 阶段12：AOI 多玩家同步（指令二十二~二十五/一百零二~一百零四）
// ---------------------------------------------------------------------------

bool EncodePlayerSpawn(const PlayerSpawnPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.characterId);
        w.WriteUInt16(p.classId);
        w.WriteUInt16(p.gender);
        w.WriteUInt32(p.level);
        w.WriteUInt16(p.mapId);
        w.WriteFloat(p.positionX);
        w.WriteFloat(p.positionY);
        w.WriteUInt64(p.serverTime);
        // 阶段14 指令十六：HP 字段
        w.WriteUInt32(p.currentHp);
        w.WriteUInt32(p.maxHp);
        w.WriteBool(p.alive);
        return w.WriteString(p.name);
    });
}

bool DecodePlayerSpawn(const std::uint8_t* data, std::size_t size, PlayerSpawnPayload& out,
                       std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.characterId = r.ReadUInt64();
        out.classId = r.ReadUInt16();
        out.gender = r.ReadUInt16();
        out.level = r.ReadUInt32();
        out.mapId = r.ReadUInt16();
        out.positionX = r.ReadFloat();
        out.positionY = r.ReadFloat();
        out.serverTime = r.ReadUInt64();
        out.currentHp = r.ReadUInt32();
        out.maxHp = r.ReadUInt32();
        out.alive = r.ReadBool();
        (void)(r.ReadString(out.name));
    });
}

bool EncodePlayerDespawn(const PlayerDespawnPayload& p, std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.characterId);
        w.WriteUInt8(p.reason);
        return true;
    });
}

bool DecodePlayerDespawn(const std::uint8_t* data, std::size_t size, PlayerDespawnPayload& out,
                         std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.characterId = r.ReadUInt64();
        out.reason = r.ReadUInt8();
    });
}

bool EncodeRemotePlayerSnapshot(const RemotePlayerSnapshotPayload& p,
                                std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.characterId);
        w.WriteFloat(p.positionX);
        w.WriteFloat(p.positionY);
        w.WriteUInt32(p.lastProcessedInputSequence);
        w.WriteUInt64(p.serverTime);
        return true;
    });
}

bool DecodeRemotePlayerSnapshot(const std::uint8_t* data, std::size_t size,
                                RemotePlayerSnapshotPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.characterId = r.ReadUInt64();
        out.positionX = r.ReadFloat();
        out.positionY = r.ReadFloat();
        out.lastProcessedInputSequence = r.ReadUInt32();
        out.serverTime = r.ReadUInt64();
    });
}

bool EncodeRemotePlayerBatchSnapshot(const RemotePlayerBatchSnapshotPayload& p,
                                     std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.serverTime);
        w.WriteUInt16(static_cast<std::uint16_t>(p.players.size()));
        for (const auto& entry : p.players) {
            w.WriteUInt64(entry.characterId);
            w.WriteFloat(entry.positionX);
            w.WriteFloat(entry.positionY);
            w.WriteUInt32(entry.lastProcessedInputSequence);
        }
        return true;
    });
}

bool DecodeRemotePlayerBatchSnapshot(const std::uint8_t* data, std::size_t size,
                                     RemotePlayerBatchSnapshotPayload& out, std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.serverTime = r.ReadUInt64();
        const std::uint16_t count = r.ReadUInt16();
        // 指令一百零四：count > 128 拒绝；指令一百零三：count 超剩余 payload 拒绝。
        if (count > kRemoteBatchMaxPlayers) {
            r.Invalidate();
            return;
        }
        out.players.reserve(count);
        for (std::uint16_t i = 0; i < count; ++i) {
            RemotePlayerBatchEntry entry;
            entry.characterId = r.ReadUInt64();
            entry.positionX = r.ReadFloat();
            entry.positionY = r.ReadFloat();
            entry.lastProcessedInputSequence = r.ReadUInt32();
            out.players.push_back(entry);
        }
    });
}

bool EncodeConsumeSelectionTicketRequest(const ConsumeSelectionTicketRequestPayload& p,
                                         std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        return w.WriteString(p.selectionTicket);
    });
}

bool DecodeConsumeSelectionTicketRequest(const std::uint8_t* data, std::size_t size,
                                         ConsumeSelectionTicketRequestPayload& out,
                                         std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        (void)(r.ReadString(out.selectionTicket));
    });
}

bool EncodeConsumeSelectionTicketResponse(const ConsumeSelectionTicketResponsePayload& p,
                                          std::vector<std::uint8_t>& out) {
    return EncodePayload(out, [&](legend::network::ByteWriter& w) {
        w.WriteUInt64(p.requestId);
        w.WriteBool(p.success);
        w.WriteUInt64(p.accountId);
        w.WriteUInt64(p.characterId);
        w.WriteUInt16(p.errorCode);
        return w.WriteString(p.message);
    });
}

bool DecodeConsumeSelectionTicketResponse(const std::uint8_t* data, std::size_t size,
                                          ConsumeSelectionTicketResponsePayload& out,
                                          std::string& error) {
    return DecodePayload(data, size, error, [&](legend::network::ByteReader& r) {
        out.requestId = r.ReadUInt64();
        out.success = r.ReadBool();
        out.accountId = r.ReadUInt64();
        out.characterId = r.ReadUInt64();
        out.errorCode = r.ReadUInt16();
        (void)(r.ReadString(out.message));
    });
}

} // namespace legend::world
