#pragma once

#include <string>

namespace legend {

class Engine;

// 应用层基类：子类提供窗口标题与初始化逻辑，本类负责完整生命周期
class Application {
public:
    virtual ~Application() = default;

    // 运行完整生命周期，返回进程退出码
    int Run();

protected:
    virtual const char* GetWindowTitle() const = 0;
    // 引擎初始化成功后调用；返回 false 表示应用初始化失败
    virtual bool OnInitialize(Engine& engine) = 0;
    // 每帧回调（场景更新前）
    virtual void OnUpdate(Engine& engine, float deltaTime) {
        (void)engine;
        (void)deltaTime;
    }
};

} // namespace legend
