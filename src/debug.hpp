#pragma once

#include <iostream>
#include <string>
#include <chrono>

inline long long getTimestampMs() {
    static auto startTime = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - startTime).count();
}

#define DEBUG_LOG(msg) do { std::cerr << "[" << getTimestampMs() << "ms] " << msg << std::endl; std::cerr.flush(); } while(0)
#define DEBUG_LOG_FUNC() do { std::cerr << "[" << getTimestampMs() << "ms] Entering: " << __PRETTY_FUNCTION__ << std::endl; std::cerr.flush(); } while(0)

// Scoped timer that logs if operation exceeds threshold
class ScopedTimer {
public:
    ScopedTimer(const char* name, int thresholdMs = 50)
        : m_name(name), m_thresholdMs(thresholdMs), m_start(std::chrono::steady_clock::now()) {}

    ~ScopedTimer() {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - m_start).count();
        if (elapsed >= m_thresholdMs) {
            std::cerr << "[" << getTimestampMs() << "ms] SLOW: " << m_name << " took " << elapsed << "ms" << std::endl;
            std::cerr.flush();
        }
    }
private:
    const char* m_name;
    int m_thresholdMs;
    std::chrono::steady_clock::time_point m_start;
};

#define SCOPED_TIMER(name) ScopedTimer _timer_##__LINE__(name)
#define SCOPED_TIMER_THRESHOLD(name, ms) ScopedTimer _timer_##__LINE__(name, ms)
