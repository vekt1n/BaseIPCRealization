#include "../include/Logger.h"

#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <thread>
#include <chrono>

Logger::Logger(size_t maxBufferSize, LogLevel minLevel, const std::string& logFilePath)
    : minLevel_(minLevel),
      logFilePath_(logFilePath),
      maxBufferSize_(maxBufferSize),
      writeToFile_(false),
      writeToConsole_(true)
{

}

void Logger::log(LogLevel level, const std::string& module, const std::string& message) {
    if (level < minLevel_) return;

    std::lock_guard<std::mutex> lock(logMutex_);
    if (level < minLevel_) return;

    LogEntry entry;
    entry.timestamp = std::time(nullptr);
    entry.level = level;
    entry.module = module;
    entry.message = message;
    entry.threadID = getThreadIdHash();
    entry.requestId = getCurrentRequestId();

    logBuffer_.push_back(entry);
    enforceBufferLimit();

    if (writeToConsole_) {
        std::cout << formatLogEntry(entry) << std::endl;
    }

    if (writeToFile_) {
        std::ofstream file(logFilePath_, std::ios::app);
        if (file.is_open()) {
            file << formatLogEntry(entry) << std::endl;
            file.close();
        }
    }
}

void Logger::setMinLevel(LogLevel level) {
    std::lock_guard<std::mutex> lock(logMutex_);
    minLevel_ = level;
}

std::vector<LogEntry> Logger::getRecentLogs(size_t count) {
    std::lock_guard<std::mutex> lock(logMutex_);
    if (count >= logBuffer_.size()) {
        return logBuffer_;
    } else {
        return std::vector<LogEntry>(logBuffer_.end() - count, logBuffer_.end());
    }
}

bool Logger::dumpToFile(const std::string& filename) {
    std::lock_guard<std::mutex> lock(logMutex_);
    std::ofstream file(filename);
    if (!file.is_open()) return false;

    for (const auto& entry : logBuffer_) {
        file << formatLogEntry(entry) << std::endl;
    }
    file.close();
    return true;
}

void Logger::clear() {
    std::lock_guard<std::mutex> lock(logMutex_);
    logBuffer_.clear();
}

void Logger::setOutputMode(bool file, bool console) {
    std::lock_guard<std::mutex> lock(logMutex_);
    writeToFile_ = file;
    writeToConsole_ = console;
}

void Logger::setRequestId(const std::string& id) {
    getThreadLocalRequestId() = id;
}

std::string Logger::getRequestId() {
    return getThreadLocalRequestId();
}

// Приватные методы
void Logger::enforceBufferLimit() {
    if (logBuffer_.size() > maxBufferSize_) {
        size_t excess = logBuffer_.size() - maxBufferSize_;
        logBuffer_.erase(logBuffer_.begin(), logBuffer_.begin() + excess);
    }
}

std::string Logger::formatLogEntry(const LogEntry& entry) const {
    std::ostringstream oss;
    std::time_t t = static_cast<std::time_t>(entry.timestamp);
    std::tm* tm = std::localtime(&t);
    oss << std::put_time(tm, "%Y-%m-%d %H:%M:%S") << " ";

    switch (entry.level) {
        case LogLevel::DEBUG:   oss << "[DEBUG] "; break;
        case LogLevel::INFO:    oss << "[INFO]  "; break;
        case LogLevel::WARNING: oss << "[WARN]  "; break;
        case LogLevel::ERROR:   oss << "[ERROR] "; break;
        case LogLevel::FATAL:   oss << "[FATAL] "; break;
    }

    oss << "[" << entry.module << "] ";
    oss << "[Thread:0x" << std::hex << entry.threadID << std::dec << "] ";

    if (!entry.requestId.empty()) {
        oss << "[RequestId:" << entry.requestId << "] ";
    }

    oss << entry.message;
    return oss.str();
}

uint32_t Logger::getThreadIdHash() {
    std::hash<std::thread::id> hasher;
    size_t h = hasher(std::this_thread::get_id());
    return static_cast<uint32_t>(h);
}

std::string& Logger::getThreadLocalRequestId() {
    static thread_local std::string requestId;
    return requestId;
}

std::string Logger::getCurrentRequestId() {
    return getThreadLocalRequestId();
}
