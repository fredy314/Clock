#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <esp_log.h>
#include <esp_timer.h>

struct LogEntry {
    int64_t timestamp; // in microseconds
    std::string message;
};

class LogManager {
public:
    static void init();
    static std::string getLogsJson(int seconds = 5);

private:
    static int vprintf_callback(const char *fmt, va_list args);
    static void addLog(const char* message);

    static std::vector<LogEntry> _logs;
    static std::mutex _mutex;
    static const size_t MAX_LOGS = 50;
    static vprintf_like_t _old_vprintf;
};
