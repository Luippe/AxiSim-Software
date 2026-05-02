#include "time_manager.h"

// measure time
Clock::time_point startTimer() {
    return Clock::now();
}

float endTimer(Clock::time_point start) {
    return std::chrono::duration<float, std::milli>(Clock::now() - start).count();
}