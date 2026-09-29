#pragma once

// ---------------------------------------------------------------------------
// 阶段22 22.14/22.15 → 阶段23 复用：原子写文件 + 轮换备份（header-only，
// WorldDataJson / GameDataJson 共用——同一保存语义，防两套实现漂移）。
// ---------------------------------------------------------------------------
#include <filesystem>
#include <fstream>
#include <iterator>
#include <nlohmann/json.hpp>
#include <set>
#include <string>

namespace legend::data {

namespace fs = std::filesystem;

// 22.15：保存前轮换备份（<dir>/.backup/<stem>.<1..10>.json，每文件保留 10 份）。
inline void BackupFileForRotate(const std::string& dir, const char* name) {
    std::error_code ec;
    const fs::path src = fs::path(dir) / name;
    if (!fs::exists(src, ec)) {
        return;
    }
    const fs::path backupDir = fs::path(dir) / ".backup";
    fs::create_directories(backupDir, ec);
    if (ec) {
        return; // 备份失败不阻塞保存（WriteAtomicFile 才是数据安全线）
    }
    std::set<int> used;
    for (const auto& entry : fs::directory_iterator(backupDir, ec)) {
        const std::string fileName = entry.path().filename().string();
        const std::string stem = fs::path(name).stem().string();
        const std::string prefix = stem + ".";
        if (fileName.rfind(prefix, 0) != 0) {
            continue;
        }
        const std::string tail = fileName.substr(prefix.size());
        const auto dot = tail.find('.');
        if (dot == std::string::npos) {
            continue;
        }
        try {
            used.insert(std::stoi(tail.substr(0, dot)));
        } catch (...) {
            // 非法备份名忽略
        }
    }
    int slot = 1;
    for (; slot <= 10; ++slot) {
        if (used.count(slot) == 0) {
            break;
        }
    }
    if (slot > 10) {
        slot = 1; // 满 10 份：覆盖最旧槽位
    }
    const std::string backupName =
        fs::path(name).stem().string() + "." + std::to_string(slot) + ".json";
    fs::copy_file(src, backupDir / backupName, fs::copy_options::overwrite_existing, ec);
}

// 22.14：serialize → temp → reparse 验证 → rename replace。
inline bool WriteAtomicFile(const std::string& dir, const char* name,
                            const nlohmann::json& value, std::string& error) {
    using nlohmann::json;
    const std::string text = value.dump(2) + "\n";
    json check = json::parse(text, nullptr, false);
    if (check.is_discarded()) {
        error = std::string(name) + ": reparse failed after serialize (internal bug)";
        return false;
    }
    const fs::path finalPath = fs::path(dir) / name;
    const fs::path tmpPath = fs::path(dir) / (std::string(name) + ".tmp");
    {
        std::ofstream file(tmpPath, std::ios::binary | std::ios::trunc);
        if (!file) {
            error = std::string(name) + ": cannot create temp file '" + tmpPath.string() + "'";
            return false;
        }
        file << text;
        file.flush();
        if (!file) {
            error = std::string(name) + ": failed writing temp file";
            return false;
        }
    }
    std::error_code ec;
    fs::rename(tmpPath, finalPath, ec);
    if (ec) {
        // Windows 个别文件系统 rename 覆盖失败 → 降级 remove+rename。
        ec.clear();
        fs::remove(finalPath, ec);
        ec.clear();
        fs::rename(tmpPath, finalPath, ec);
        if (ec) {
            error = std::string(name) + ": replace failed: " + ec.message();
            fs::remove(tmpPath, ec);
            return false;
        }
    }
    return true;
}

} // namespace legend::data
