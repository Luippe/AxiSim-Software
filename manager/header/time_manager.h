#pragma once
#include <chrono>

using Clock = std::chrono::steady_clock;

Clock::time_point startTimer();

float endTimer(Clock::time_point start);