#pragma once

#include <string>

namespace legend::admin {

// Stage25.6 Server Management GUI：六个正式服务器的统一角色定义。
// 所有服务器共用一套 Admin UI 框架（ServerAdminApp），仅角色不同。
enum class ServerRole {
    Login,
    Character,
    Gateway,
    World,
    Db,
    Log,
};

// Config/servers.json 中的服务键名
inline const char* ServiceKey(ServerRole role) {
    switch (role) {
        case ServerRole::Login:     return "login";
        case ServerRole::Character: return "character";
        case ServerRole::Gateway:   return "gateway";
        case ServerRole::World:     return "world";
        case ServerRole::Db:        return "db";
        case ServerRole::Log:       return "log";
    }
    return "unknown";
}

// 简体中文服务名（GUI 全中文显示）
inline const char* ServiceCnName(ServerRole role) {
    switch (role) {
        case ServerRole::Login:     return "登录服务器";
        case ServerRole::Character: return "角色服务器";
        case ServerRole::Gateway:   return "网关服务器";
        case ServerRole::World:     return "世界服务器";
        case ServerRole::Db:        return "数据库服务器";
        case ServerRole::Log:       return "日志服务器";
    }
    return "未知服务";
}

// 统一窗口标题（固定格式，供 Studio 服务器中心 FindWindowW 定位打开管理窗口）
inline std::wstring WindowTitle(ServerRole role) {
    std::wstring cn;
    switch (role) {
        case ServerRole::Login:     cn = L"登录服务器"; break;
        case ServerRole::Character: cn = L"角色服务器"; break;
        case ServerRole::Gateway:   cn = L"网关服务器"; break;
        case ServerRole::World:     cn = L"世界服务器"; break;
        case ServerRole::Db:        cn = L"数据库服务器"; break;
        case ServerRole::Log:       cn = L"日志服务器"; break;
    }
    return L"LegendGame " + cn + L" 管理台";
}

} // namespace legend::admin
