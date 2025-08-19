#pragma once

#include <cstdio>

namespace telemux {

enum class LogLevel { kDebug, kInfo, kWarn, kError };

void set_log_level(LogLevel level);
LogLevel log_level();
void log_line(LogLevel level, const char* subsystem, const char* message);

}  // namespace telemux

#define TELEMUX_LOG(level, subsystem, message) \
    ::telemux::log_line(level, subsystem, message)
