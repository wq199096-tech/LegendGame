#include "Engine/Core/Timer.h"

#include <SDL3/SDL.h>

namespace legend {

Timer::Timer() {
    const long long frequency = static_cast<long long>(SDL_GetPerformanceFrequency());
    m_secondsPerCount = 1.0 / static_cast<double>(frequency > 0 ? frequency : 1);
    Reset();
}

void Timer::Reset() {
    m_startCounter = static_cast<long long>(SDL_GetPerformanceCounter());
}

double Timer::GetElapsedSeconds() const {
    const long long now = static_cast<long long>(SDL_GetPerformanceCounter());
    return static_cast<double>(now - m_startCounter) * m_secondsPerCount;
}

} // namespace legend
