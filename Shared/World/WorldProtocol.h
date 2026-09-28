#pragma once

#include "Shared/World/WorldError.h"
#include "Shared/World/WorldTypes.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace legend::world {

// 阶段11 指令十六：Client <-> WorldServer 协议 payload。
// 所有 Decode 必须 reader.IsValid() && reader.Remaining()==0（指令六十三），
// 继续复用阶段9 PacketCodec / kMaxPacketPayload（指令六十四）。

// WorldClientHello(200) / WorldServerHello(201)：字节布局与阶段9
// ClientHelloPayload / ServerHelloPayload 完全一致，直接复用
// legend::network::Encode/DecodeClientHello / ServerHello（MessageId 区分处理）。

// 阶段11 指令十九：EnterWorldRequest —— 只带 selectionTicket（+requestId），
// 绝不带 accountId/characterId（指令十五：身份只能来自 Ticket 消费结果）。
struct EnterWorldRequestPayload {
    std::uint64_t requestId = 0;
    std::string selectionTicket; // <= kSelectionTicketMaxLength，超长 = Malformed
};

struct EnterWorldResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint64_t accountId = 0;
    std::uint64_t characterId = 0;
    std::string characterName;
    std::uint16_t classId = 0;
    std::uint16_t gender = 0;
    std::uint32_t level = 1;
    std::uint16_t mapId = 1;
    float positionX = 0.0f;
    float positionY = 0.0f;
    std::uint64_t serverTime = 0;
    // 阶段14 指令十七：进入世界返回玩家 HP。
    std::uint32_t currentHp = 100;
    std::uint32_t maxHp = 100;
    bool alive = true;
    // 阶段15 指令六十九：进入世界返回玩家 Mana（阶段15 不持久化，恢复 100/100）。
    std::uint32_t currentMana = 100;
    std::uint32_t maxMana = 100;
    std::uint16_t errorCode = 0; // WorldErrorCode
    std::string message;
};

// 阶段11 指令七十九：客户端退出世界。
struct WorldDisconnectNoticePayload {
    std::string reason; // 与阶段9 DisconnectNoticePayload 同布局
};

// 阶段11 指令三十四：移动输入 —— 只发方向，禁止绝对坐标（指令三十五）。
struct PlayerMoveInputPayload {
    std::uint32_t inputSequence = 0;
    float directionX = 0.0f;
    float directionY = 0.0f;
    float deltaTime = 0.0f;
};

// 阶段11 指令四十二：100ms 权威位置快照。
struct PlayerPositionSnapshotPayload {
    std::uint64_t characterId = 0;
    float positionX = 0.0f;
    float positionY = 0.0f;
    std::uint32_t lastProcessedInputSequence = 0;
    std::uint64_t serverTime = 0;
};

// ---------------------------------------------------------------------------
// 阶段12 指令二十二~二十五：AOI 多玩家同步 payload
// ---------------------------------------------------------------------------

// 指令二十二：PlayerSpawn(230)。阶段14 指令十六：增加 HP 字段。
struct PlayerSpawnPayload {
    std::uint64_t characterId = 0;
    std::string name;
    std::uint16_t classId = 0;
    std::uint16_t gender = 0;
    std::uint32_t level = 1;
    std::uint16_t mapId = 1;
    float positionX = 0.0f;
    float positionY = 0.0f;
    std::uint64_t serverTime = 0;
    std::uint32_t currentHp = 100;
    std::uint32_t maxHp = 100;
    bool alive = true;
};

// 指令二十三：PlayerDespawn(231)，reason = PlayerDespawnReason。
struct PlayerDespawnPayload {
    std::uint64_t characterId = 0;
    std::uint8_t reason = 0;
};

// 指令二十四：RemotePlayerSnapshot(232)（阶段12 服务器只发 batch，保留单条协议）。
// 阶段14 指令九十二：RemotePlayer batch 不加 HP——HP 走 CombatEvent + HealthSnapshot。
struct RemotePlayerSnapshotPayload {
    std::uint64_t characterId = 0;
    float positionX = 0.0f;
    float positionY = 0.0f;
    std::uint32_t lastProcessedInputSequence = 0;
    std::uint64_t serverTime = 0;
};

// 指令二十五：RemotePlayerBatchSnapshot(233) 推荐形态。
struct RemotePlayerBatchEntry {
    std::uint64_t characterId = 0;
    float positionX = 0.0f;
    float positionY = 0.0f;
    std::uint32_t lastProcessedInputSequence = 0;
};

struct RemotePlayerBatchSnapshotPayload {
    std::uint64_t serverTime = 0;
    std::vector<RemotePlayerBatchEntry> players; // count <= kRemoteBatchMaxPlayers
};

// 阶段11 指令十二/十三：WorldServer -> LoginServer Ticket 一次性消费。
struct ConsumeSelectionTicketRequestPayload {
    std::uint64_t requestId = 0;
    std::string selectionTicket;
};

struct ConsumeSelectionTicketResponsePayload {
    std::uint64_t requestId = 0;
    bool success = false;
    std::uint64_t accountId = 0;
    std::uint64_t characterId = 0;
    std::uint16_t errorCode = 0;
    std::string message;
};

// WorldErrorResponse(2550)：与阶段9 ErrorResponsePayload 同布局 {errorCode, message}。

bool EncodeEnterWorldRequest(const EnterWorldRequestPayload& p, std::vector<std::uint8_t>& out);
bool DecodeEnterWorldRequest(const std::uint8_t* data, std::size_t size,
                             EnterWorldRequestPayload& out, std::string& error);
bool EncodeEnterWorldResponse(const EnterWorldResponsePayload& p, std::vector<std::uint8_t>& out);
bool DecodeEnterWorldResponse(const std::uint8_t* data, std::size_t size,
                              EnterWorldResponsePayload& out, std::string& error);
bool EncodeWorldDisconnectNotice(const WorldDisconnectNoticePayload& p,
                                 std::vector<std::uint8_t>& out);
bool DecodeWorldDisconnectNotice(const std::uint8_t* data, std::size_t size,
                                 WorldDisconnectNoticePayload& out, std::string& error);
bool EncodePlayerMoveInput(const PlayerMoveInputPayload& p, std::vector<std::uint8_t>& out);
bool DecodePlayerMoveInput(const std::uint8_t* data, std::size_t size,
                           PlayerMoveInputPayload& out, std::string& error);
bool EncodePlayerPositionSnapshot(const PlayerPositionSnapshotPayload& p,
                                  std::vector<std::uint8_t>& out);
bool DecodePlayerPositionSnapshot(const std::uint8_t* data, std::size_t size,
                                  PlayerPositionSnapshotPayload& out, std::string& error);
bool EncodePlayerSpawn(const PlayerSpawnPayload& p, std::vector<std::uint8_t>& out);
bool DecodePlayerSpawn(const std::uint8_t* data, std::size_t size, PlayerSpawnPayload& out,
                       std::string& error);
bool EncodePlayerDespawn(const PlayerDespawnPayload& p, std::vector<std::uint8_t>& out);
bool DecodePlayerDespawn(const std::uint8_t* data, std::size_t size, PlayerDespawnPayload& out,
                         std::string& error);
bool EncodeRemotePlayerSnapshot(const RemotePlayerSnapshotPayload& p,
                                std::vector<std::uint8_t>& out);
bool DecodeRemotePlayerSnapshot(const std::uint8_t* data, std::size_t size,
                                RemotePlayerSnapshotPayload& out, std::string& error);
bool EncodeRemotePlayerBatchSnapshot(const RemotePlayerBatchSnapshotPayload& p,
                                     std::vector<std::uint8_t>& out);
bool DecodeRemotePlayerBatchSnapshot(const std::uint8_t* data, std::size_t size,
                                     RemotePlayerBatchSnapshotPayload& out, std::string& error);
bool EncodeConsumeSelectionTicketRequest(const ConsumeSelectionTicketRequestPayload& p,
                                         std::vector<std::uint8_t>& out);
bool DecodeConsumeSelectionTicketRequest(const std::uint8_t* data, std::size_t size,
                                         ConsumeSelectionTicketRequestPayload& out,
                                         std::string& error);
bool EncodeConsumeSelectionTicketResponse(const ConsumeSelectionTicketResponsePayload& p,
                                          std::vector<std::uint8_t>& out);
bool DecodeConsumeSelectionTicketResponse(const std::uint8_t* data, std::size_t size,
                                          ConsumeSelectionTicketResponsePayload& out,
                                          std::string& error);

} // namespace legend::world
