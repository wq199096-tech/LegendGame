#include "Server/LoginServer/Account/PasswordHasher.h"

#include <sodium.h>

#include <mutex>
#include <vector>

namespace legend::account {
namespace {

constexpr int kArgon2OpsLimit = crypto_pwhash_OPSLIMIT_INTERACTIVE;
constexpr int kArgon2MemLimit = crypto_pwhash_MEMLIMIT_INTERACTIVE;

std::string ToHex(const unsigned char* data, std::size_t size) {
    std::vector<char> hex(size * 2 + 1);
    sodium_bin2hex(hex.data(), hex.size(), data, size);
    return std::string(hex.data(), size * 2);
}

} // namespace

void EnsureSodiumInit() {
    static std::once_flag flag;
    std::call_once(flag, [] { sodium_init(); });
}

bool PasswordHasher::Hash(const std::string& password, std::string& outHash, std::string& error) {
    EnsureSodiumInit();
    char hashed[crypto_pwhash_STRBYTES];
    if (crypto_pwhash_str(hashed, password.c_str(), password.length(), kArgon2OpsLimit,
                          kArgon2MemLimit) != 0) {
        error = "argon2id hash failed (out of memory?)";
        return false;
    }
    // crypto_pwhash_STRBYTES 内以 '\0' 结尾。
    outHash.assign(hashed);
    return true;
}

bool PasswordHasher::Verify(const std::string& password, const std::string& storedHash,
                            std::string& error) {
    EnsureSodiumInit();
    if (storedHash.size() >= crypto_pwhash_STRBYTES || storedHash.empty()) {
        error = "stored password hash has invalid format";
        return false;
    }
    // 0 = 校验通过；-1 = 密码不匹配或哈希格式非法（统一按不通过处理）。
    if (crypto_pwhash_str_verify(storedHash.c_str(), password.c_str(), password.length()) != 0) {
        return false;
    }
    return true;
}

std::string GenerateTokenHex(std::size_t bytes) {
    EnsureSodiumInit();
    if (bytes == 0) {
        return {};
    }
    std::vector<unsigned char> buffer(bytes);
    randombytes_buf(buffer.data(), buffer.size());
    return ToHex(buffer.data(), buffer.size());
}

std::string Sha256Hex(const std::string& input) {
    EnsureSodiumInit();
    unsigned char digest[crypto_hash_sha256_BYTES];
    crypto_hash_sha256(digest, reinterpret_cast<const unsigned char*>(input.data()), input.size());
    return ToHex(digest, sizeof(digest));
}

} // namespace legend::account
