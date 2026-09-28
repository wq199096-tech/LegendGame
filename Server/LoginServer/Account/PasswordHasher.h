#pragma once

#include <cstddef>
#include <string>

namespace legend::account {

// 阶段10 指令十三/十四/三十/三十一：libsodium 封装。
// - 密码：Argon2id（crypto_pwhash_str，hash 内含 salt + 参数，禁止 MD5/SHA/自制）
// - Session Token / SelectionTicket：randombytes_buf（系统 CSPRNG，禁止 rand()/mt19937）
// - 数据库只存 token/ticket 的 SHA-256 hex（高熵输入，SHA-256 足够且非密码场景）
namespace PasswordHasher {

// Argon2id 哈希（encoded 格式：$argon2id$v=19$m=...,t=...,p=...$salt$hash）。
bool Hash(const std::string& password, std::string& outHash, std::string& error);

// 校验。返回 false 时 error 非空表示哈希格式/计算错误，error 为空表示密码不匹配。
bool Verify(const std::string& password, const std::string& storedHash, std::string& error);

} // namespace PasswordHasher

// 生成 256-bit CSPRNG token 的 hex（64 字符）。默认 32 字节 = 256 bit（指令三十）。
std::string GenerateTokenHex(std::size_t bytes = 32);

// SHA-256 hex（64 字符）。用于 session_token_hash / ticketHash（不用于密码）。
std::string Sha256Hex(const std::string& input);

// 进程级 libsodium 初始化（幂等，首次调用时执行）。
void EnsureSodiumInit();

} // namespace legend::account
