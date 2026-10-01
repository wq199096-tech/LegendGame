#pragma once

// ---------------------------------------------------------------------------
// Stage26 指令三十：ClientLoginStore —— 本地登录信息持久化（savedata/client_login.json）。
// 只存账号名（方便下次默认填入）；密码/Token 绝不落盘（指令三十）。
// ---------------------------------------------------------------------------

#include <string>

namespace legend::flow {

class ClientLoginStore {
public:
    // 读取上次登录账号名（文件不存在/损坏返回空串）。
    static std::string LoadLastAccountName();
    // 保存账号名（空串 = 清除）。失败静默（不阻塞登录流程）。
    static void SaveLastAccountName(const std::string& name);
};

} // namespace legend::flow
