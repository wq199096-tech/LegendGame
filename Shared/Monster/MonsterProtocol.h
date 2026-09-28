#pragma once

#include "Shared/Monster/MonsterTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// 阶段13 指令十七~二十二/六十三/六十四：Monster 协议 payload。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令六十三），
// 复用阶段9 PacketCodec / kMaxPacketPayload；batch count>128 拒绝（指令六十四）。

// MonsterSpawn(240)（指令十八）。阶段14 指令十五：增加 HP 字段。
struct MonsterSpawnPayload {
    std::uint64_t entityId = 0;
    std::uint32_t monsterTypeId = 0;
    std::string name;
    std::uint32_t level = 1;
    std::uint16_t mapId = 1;
    float positionX = 0.0f;
    float positionY = 0.0f;
    std::uint8_t state = 0; // MonsterState
    std::uint64_t serverTime = 0;
    std::uint32_t currentHp = 0;
    std::uint32_t maxHp = 0;
    bool alive = true;
};

// MonsterDespawn(241)（指令十九），reason = MonsterDespawnReason。
struct MonsterDespawnPayload {
    std::uint64_t entityId = 0;
    std::uint8_t reason = 0;
};

// MonsterSnapshotEntry（指令二十）。阶段14 指令九十一：增加 HP 字段。
struct MonsterSnapshotEntry {
    std::uint64_t entityId = 0;
    float positionX = 0.0f;
    float positionY = 0.0f;
    std::uint8_t state = 0;
    std::uint64_t targetCharacterId = 0;
    std::uint32_t currentHp = 0;
    std::uint32_t maxHp = 0;
    bool alive = true;
};

// MonsterBatchSnapshot(242)（指令二十一/二十二）。
struct MonsterBatchSnapshotPayload {
    std::uint64_t serverTime = 0;
    std::vector<MonsterSnapshotEntry> monsters; // count <= kMonsterBatchMaxMonsters
};

bool EncodeMonsterSpawn(const MonsterSpawnPayload& p, std::vector<std::uint8_t>& out);
bool DecodeMonsterSpawn(const std::uint8_t* data, std::size_t size, MonsterSpawnPayload& out,
                        std::string& error);
bool EncodeMonsterDespawn(const MonsterDespawnPayload& p, std::vector<std::uint8_t>& out);
bool DecodeMonsterDespawn(const std::uint8_t* data, std::size_t size, MonsterDespawnPayload& out,
                          std::string& error);
bool EncodeMonsterBatchSnapshot(const MonsterBatchSnapshotPayload& p,
                                std::vector<std::uint8_t>& out);
bool DecodeMonsterBatchSnapshot(const std::uint8_t* data, std::size_t size,
                                MonsterBatchSnapshotPayload& out, std::string& error);

} // namespace legend::world
