#include "Client/Flow/ClientLoginStore.h"

#include <nlohmann/json.hpp>

#include <fstream>

namespace legend::flow {

namespace {

// 与 VisualRuntime 的 client_settings.json 同目录约定（savedata/ 相对工作目录）。
constexpr const char* kLoginStorePath = "savedata/client_login.json";

} // namespace

std::string ClientLoginStore::LoadLastAccountName() {
    std::ifstream in(kLoginStorePath);
    if (!in.is_open()) {
        return std::string();
    }
    try {
        const auto root = nlohmann::json::parse(in);
        if (root.is_object()) {
            const auto it = root.find("lastAccountName");
            if (it != root.end() && it->is_string()) {
                return it->get<std::string>();
            }
        }
    } catch (...) {
        // 损坏文件按不存在处理
    }
    return std::string();
}

void ClientLoginStore::SaveLastAccountName(const std::string& name) {
    try {
        nlohmann::json root;
        root["lastAccountName"] = name;
        std::ofstream out(kLoginStorePath, std::ios::trunc);
        if (out.is_open()) {
            out << root.dump(2) << std::endl;
        }
    } catch (...) {
        // 持久化失败不阻塞登录流程
    }
}

} // namespace legend::flow
