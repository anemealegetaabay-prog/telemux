#include "telemux/logging.h"

namespace telemux {

namespace {
LogLevel g_level = LogLevel::kWarn;

const char* level_name(LogLevel level) {
    switch (level) {
        case LogLevel::kDebug: return "DEBUG";
        case LogLevel::kInfo: return "INFO";
        case LogLevel::kWarn: return "WARN";
        case LogLevel::kError: return "ERROR";
    }
    return "?";
}
}  // namespace

void set_log_level(LogLevel level) { g_level = level; }
LogLevel log_level() { return g_level; }

void log_line(LogLevel level, const char* subsystem, const char* message) {
    if (static_cast<int>(level) < static_cast<int>(g_level)) return;
    std::fprintf(stderr, "[%s] %s: %s\n", level_name(level), subsystem, message);
}

}  // namespace telemux
