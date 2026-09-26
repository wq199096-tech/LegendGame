#pragma once

namespace legend {

// 基于高性能计数器的时间测量工具
class Timer {
public:
    Timer();
    void Reset();
    double GetElapsedSeconds() const;

private:
    long long m_startCounter = 0;
    double m_secondsPerCount = 0.0;
};

} // namespace legend
