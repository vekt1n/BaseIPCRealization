#include "../SharedMemory/include/BaseMemory.hpp"
#include "../SharedMemory/include/Daemonizer.hpp"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <cstring>
#include <fstream>
#include <memory>
#include <ctime>
#include <iomanip>
#include <sstream>
#include <thread>
#include <chrono>

volatile sig_atomic_t logger_running = 1;

void logger_signal_handler(int sig) {
    (void)sig;
    logger_running = 0;
}

std::string get_current_timestamp() {
    auto now = std::chrono::system_clock::now();
    auto in_time_t = std::chrono::system_clock::to_time_t(now);
    
    std::stringstream ss;
    ss << std::put_time(std::localtime(&in_time_t), "%Y-%m-%d %H:%M:%S");
    return ss.str();
}

void write_log_entry(const std::string& log_file, 
                     const std::string& tag, 
                     const std::string& sender, 
                     const std::string& message) {
    std::ofstream logfile(log_file, std::ios::app);
    if (logfile.is_open()) {
        std::string timestamp = get_current_timestamp();
        logfile << "[" << timestamp << "] "
                << "[" << tag << "] "
                << "[" << sender << "] "
                << message << std::endl;
        logfile.close();
    }
}

int main(int argc, char* argv[]) {
    bool foreground_mode = false;
    std::string pid_file = "/tmp/logger_daemon.pid";
    std::string log_file = "./shared_memory.log";
    
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--foreground") == 0) {
            foreground_mode = true;
        } else if (strcmp(argv[i], "--pid-file") == 0 && i + 1 < argc) {
            pid_file = argv[++i];
        } else if (strcmp(argv[i], "--log-file") == 0 && i + 1 < argc) {
            log_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            std::cout << "Logger Daemon - logs all IPC messages via subscriptions\n"
                      << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --foreground   Run in foreground (not as daemon)\n"
                      << "  --pid-file <file>  Set PID file path\n"
                      << "  --log-file <file>  Set log file path\n"
                      << "  --help         Show this help\n";
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
    
    // Создаем свою очередь для получения сообщений
    BaseMemory logger_queue("/logger_queue");
    Result res = logger_queue.createConnection();
    if (!res.result) {
        std::cerr << "Failed to create logger queue: " << res.message << std::endl;
        return 1;
    }
    
    // Подписываемся на тег "log" через Adapter
    res = logger_queue.openConnection("/adapter");
    if (!res.result) {
        std::cerr << "Failed to connect to adapter: " << res.message << std::endl;
        // Продолжаем работу — можем получать прямые сообщения
    } else {
        // Подписываемся на логи
        logger_queue.sendMessage("sub_to log");
        logger_queue.closeConnection();
        
        if (foreground_mode) {
            std::cout << "Subscribed to 'log' tag via Adapter\n";
        }
    }
    
    if (foreground_mode) {
        std::cout << "Logger daemon started (PID: " << getpid() << ")\n"
                  << "Log file: " << log_file << "\n"
                  << "Waiting for log messages...\n"
                  << "Press Ctrl+C to exit\n";
    }
    
    write_log_entry(log_file, "SYSTEM", "LOGGER", "Logger daemon started successfully");
    
    // Главный цикл — читаем сообщения из своей очереди
    while (logger_running) {
        if (logger_queue.hasMessage()) {
            Message msg;
            res = logger_queue.getMessage(msg);
            if (res.result) {
                write_log_entry(log_file, msg.tag, msg.sender, msg.message);
                
                if (foreground_mode) {
                    std::cout << "[" << msg.tag << "] [" << msg.sender << "] " 
                              << msg.message << std::endl;
                }
            }
        }
        usleep(100000); // 100ms
    }
    
    write_log_entry(log_file, "SYSTEM", "LOGGER", "Logger daemon stopped");
    
    // Отписываемся при выходе
    res = logger_queue.openConnection("/adapter");
    if (res.result) {
        logger_queue.sendMessage("unsub_to log");
        logger_queue.closeConnection();
    }
    
    if (foreground_mode) {
        std::cout << "Logger daemon stopped\n";
    }
    
    Daemonizer::cleanupPidFile(pid_file);
    return 0;
}
