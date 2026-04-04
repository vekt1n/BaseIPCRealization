#ifndef LOGGER_H
#define LOGGER_H

#include <string>
#include <vector>
#include <mutex>
#include <cstdint>
#include <ctime>

enum class LogLevel {
    DEBUG,
    INFO,
    WARNING,
    ERROR,
    FATAL
};

struct LogEntry {
    size_t timestamp;
    LogLevel level;
    std::string module;
    std::string message;
    uint32_t threadID;
    std::string requestId;
};

class Logger {
public:
    Logger(size_t maxBufferSize = 1000,
           LogLevel minLevel = LogLevel::DEBUG,
           const std::string& logFilePath = "log.txt");
    ~Logger() = default;

    void log(LogLevel level, const std::string& module, const std::string& message);
    void setMinLevel(LogLevel level);
    std::vector<LogEntry> getRecentLogs(size_t count);
    bool dumpToFile(const std::string& filename);
    void clear();
    void setOutputMode(bool file, bool console);

    // Управление requestId для текущего потока (thread-local)
    static void setRequestId(const std::string& id);
    static std::string getRequestId();

private:
    void enforceBufferLimit();
    std::string formatLogEntry(const LogEntry& entry) const;

    static uint32_t getThreadIdHash();
    static std::string& getThreadLocalRequestId();
    static std::string getCurrentRequestId();

private:
    std::vector<LogEntry> logBuffer_;
    LogLevel minLevel_;
    std::string logFilePath_;
    std::mutex logMutex_;
    uint32_t maxBufferSize_;
    bool writeToFile_;
    bool writeToConsole_;
};

#endif // LOGGER_H
