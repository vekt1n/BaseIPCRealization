#include "../SharedMemory/include/BaseMemory.hpp"
#include "../SharedMemory/include/Daemonizer.hpp"
#include "../SharedMemory/include/Logger.h"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <cstring>
#include <memory>
#include <thread>
#include <chrono>

volatile sig_atomic_t logger_running = 1;

void logger_signal_handler(int sig) {
    (void)sig;
    logger_running = 0;
}

// Маппинг тегов IPC-сообщений на уровни логирования
LogLevel tagToLogLevel(const std::string& tag) {
    if (tag == "error" || tag == "ERROR")   return LogLevel::ERROR;
    if (tag == "fatal" || tag == "FATAL")   return LogLevel::FATAL;
    if (tag == "warn"  || tag == "WARNING") return LogLevel::WARNING;
    if (tag == "debug" || tag == "DEBUG")   return LogLevel::DEBUG;
    // По умолчанию — INFO (в т.ч. для тега "log")
    return LogLevel::INFO;
}

int main(int argc, char* argv[]) {
    bool foreground_mode = false;
    std::string pid_file = "/tmp/logger_daemon.pid";
    std::string log_file = "./shared_memory.log";
    size_t buffer_size = 5000;
    
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--foreground") == 0) {
            foreground_mode = true;
        } else if (strcmp(argv[i], "--pid-file") == 0 && i + 1 < argc) {
            pid_file = argv[++i];
        } else if (strcmp(argv[i], "--log-file") == 0 && i + 1 < argc) {
            log_file = argv[++i];
        } else if (strcmp(argv[i], "--buffer-size") == 0 && i + 1 < argc) {
            buffer_size = static_cast<size_t>(atoi(argv[++i]));
        } else if (strcmp(argv[i], "--help") == 0) {
            std::cout << "Logger Daemon - logs all IPC messages via subscriptions\n"
                      << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --foreground       Run in foreground (not as daemon)\n"
                      << "  --pid-file <file>  Set PID file path\n"
                      << "  --log-file <file>  Set log file path\n"
                      << "  --buffer-size <n>  Set log buffer size (default: 5000)\n"
                      << "  --help             Show this help\n";
            return 0;
        }
    }
    
    // Конвертируем относительный путь лога в абсолютный (до демонизации, пока cwd правильный)
    if (log_file[0] != '/') {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) {
            log_file = std::string(cwd) + "/" + log_file;
        }
    }
    
    // Демонизируем если нужно
    if (!foreground_mode) {
        if (!Daemonizer::daemonize(pid_file)) {
            std::cerr << "Failed to daemonize logger!" << std::endl;
            return 1;
        }
    } else {
        std::cout << "Logger daemon running in foreground mode\n";
        Daemonizer::writePidFile(pid_file);
    }
    
    // Настраиваем обработчики сигналов
    Daemonizer::setupSignalHandlers();
    signal(SIGTERM, logger_signal_handler);
    signal(SIGINT, logger_signal_handler);
    
    // Создаём экземпляр Logger
    Logger logger(buffer_size, LogLevel::DEBUG, log_file);
    
    // Настраиваем вывод: файл всегда, консоль — только в foreground
    logger.setOutputMode(/*file=*/true, /*console=*/foreground_mode);
    
    // Создаем свою очередь для получения сообщений
    BaseMemory logger_queue("/logger_daemon_queue");
    Result res = logger_queue.createConnection();
    if (!res.result) {
        std::cerr << "Failed to create logger queue: " << res.message << std::endl;
        return 1;
    }
    
    // Подписываемся на тег "log" через subscrribtion
    res = logger_queue.openConnection("/subscrribtion_queue");
    if (!res.result) {
        std::cerr << "Failed to connect to subscrribtion: " << res.message << std::endl;
        // Продолжаем работу — можем получать прямые сообщения
    } else {
        // Подписываемся на логи всех уровней
        logger_queue.sendMessage("sub_to log");
        logger_queue.sendMessage("sub_to info");
        logger_queue.sendMessage("sub_to error");
        logger_queue.sendMessage("sub_to fatal");
        logger_queue.sendMessage("sub_to warn");
        logger_queue.sendMessage("sub_to debug");
        logger_queue.closeConnection();
        
        if (foreground_mode) {
            std::cout << "Subscribed to log tags via subscrribtion\n";
        }
    }
    
    if (foreground_mode) {
        std::cout << "Logger daemon started (PID: " << getpid() << ")\n"
                  << "Log file: " << log_file << "\n"
                  << "Buffer size: " << buffer_size << "\n"
                  << "Waiting for log messages...\n"
                  << "Press Ctrl+C to exit\n";
    }
    
    logger.log(LogLevel::INFO, "LOGGER", "Logger daemon started successfully");
    
    // Главный цикл — читаем сообщения из своей очереди
    while (logger_running) {
        if (logger_queue.hasMessage()) {
            Message msg;
            res = logger_queue.getMessage(msg);
            if (res.result) {
                LogLevel level = tagToLogLevel(msg.tag);
                logger.log(level, msg.sender, msg.message);
            }
        }
        usleep(100000); // 100ms
    }
    
    logger.log(LogLevel::INFO, "LOGGER", "Logger daemon stopped");
    
    // Сохраняем буфер в файл при остановке
    logger.dumpToFile(log_file + ".dump");
    
    // Отписываемся при выходе
    res = logger_queue.openConnection("/subscrribtion_queue");
    if (res.result) {
        logger_queue.sendMessage("unsub_to log");
        logger_queue.sendMessage("unsub_to info");
        logger_queue.sendMessage("unsub_to error");
        logger_queue.sendMessage("unsub_to fatal");
        logger_queue.sendMessage("unsub_to warn");
        logger_queue.sendMessage("unsub_to debug");
        logger_queue.closeConnection();
    }
    
    if (foreground_mode) {
        std::cout << "Logger daemon stopped\n";
    }
    
    Daemonizer::cleanupPidFile(pid_file);
    return 0;
}
