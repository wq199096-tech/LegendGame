#include "Client/Ui/PlayerFacingErrorCatalog.h"

namespace legend::ui {

const char* PlayerFacingAccountErrorText(std::uint16_t code) {
    // 与 Shared/Account/AccountError.h 的 AccountErrorCode 对齐。
    switch (code) {
        case 0: return "";
        case 1: return "账号名不符合要求";
        case 2: return "密码不符合要求";
        case 3: return "该账号名已被注册";
        case 4: return "服务器数据存储暂时不可用，请稍后重试";
        case 5: return "服务器内部错误，请稍后重试";
        case 10: return "账号名或密码错误";
        case 11: return "账号已被停用";
        case 12: return "账号已被封禁";
        case 13: return "尝试次数过多，请稍后再试";
        case 20: return "登录状态已失效，请重新登录";
        case 30: return "角色名不符合要求（2~12 个汉字、字母或数字）";
        case 31: return "该角色名已被占用";
        case 32: return "角色数量已达上限（最多 4 个）";
        case 33: return "角色不存在";
        case 34: return "不能删除其他账号的角色";
        case 40: return "上一次请求还在处理中，请稍候";
        case 41: return "请求超时，请重试";
        case 42: return "服务器繁忙，请稍后重试";
        default: return "操作失败，请稍后重试";
    }
}

} // namespace legend::ui
