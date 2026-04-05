#include "../SharedMemory/include/BaseMemory.hpp"
#include "../SharedMemory/include/Daemonizer.hpp"
#include "../SharedMemory/include/Logger.h"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <ctime>
#include <thread>
#include <chrono>

volatile sig_atomic_t writer2_running = 1;

void writer2_signal_handler(int sig) {
    (void)sig;
    writer2_running = 0;
}

int main(int argc, char* argv[]) {
    bool foreground_mode = false;
    std::string pid_file = "/tmp/writer2_daemon.pid";
    int interval = 5;
    std::string publish_tag = "data";
    
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--foreground") == 0) {
            foreground_mode = true;
        } else if (strcmp(argv[i], "--pid-file") == 0 && i + 1 < argc) {
            pid_file = argv[++i];
        } else if (strcmp(argv[i], "--interval") == 0 && i + 1 < argc) {
            interval = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--tag") == 0 && i + 1 < argc) {
            publish_tag = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            std::cout << "Writer2 Daemon - publishes messages via subs_bus\n"
                      << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --foreground       Run in foreground (not as daemon)\n"
                      << "  --pid-file <file>  Set PID file path\n"
                      << "  --interval <sec>   Message interval (default: 5)\n"
                      << "  --tag <tag>        Publish tag (default: data)\n"
                      << "  --help             Show this help\n";
            return 0;
        }
    }
    
    // Демонизируем если нужно
    if (!foreground_mode) {
        if (!Daemonizer::daemonize(pid_file)) {
            std::cerr << "Failed to daemonize writer2!" << std::endl;
            return 1;
        }
    } else {
        std::cout << "Writer2 daemon running in foreground mode\n";
        Daemonizer::writePidFile(pid_file);
    }
    
    // Настраиваем обработчики сигналов
    Daemonizer::setupSignalHandlers();
    signal(SIGTERM, writer2_signal_handler);
    signal(SIGINT, writer2_signal_handler);
    
    // Создаём локальный логгер
    Logger logger(500, LogLevel::DEBUG, "/tmp/writer2.log");
    logger.setOutputMode(/*file=*/true, /*console=*/foreground_mode);
    
    // Создаем свою очередь
    BaseMemory writer("/writer_daemon_queue2");
    Result res = writer.createConnection();
    if (!res.result) {
        logger.log(LogLevel::ERROR, "Writer2", "Failed to create writer2 queue: " + res.message);
        Daemonizer::cleanupPidFile(pid_file);
        return 1;
    }
    
    if (foreground_mode) {
        std::cout << "Writer2 daemon started (PID: " << getpid() << ")\n"
                  << "Publishing to tag '" << publish_tag << "' every " 
                  << interval << " seconds\n"
                  << "Press Ctrl+C to exit\n";
    }
    
    int message_counter = 0;
    time_t start_time = time(nullptr);
    
    // Главный цикл
    while (writer2_running) {
        std::stringstream msg;
        time_t current_time = time(nullptr);
        int uptime = static_cast<int>(current_time - start_time);
        
        msg << "Message #" << message_counter++ 
            << " from Writer2 (uptime: " << uptime << "s)";

        std::string message_str = msg.str();
        
        // Публикуем через subs_bus
        res = writer.publishMessage(message_str, publish_tag);
        
        if (!res.result && foreground_mode) {
            std::cerr << "Failed to publish message: " << res.message << std::endl;
            logger.log(LogLevel::ERROR, "Writer2", "Failed to publish: " + res.message);
        } else if (foreground_mode) {
            std::cout << "Published [" << publish_tag << "]: " << message_str << std::endl;
        }
        
        // Логируем локально и отправляем в централизованный логгер
        logger.log(LogLevel::INFO, "Writer2", message_str);
        writer.publishMessage(message_str, "log");
        
        // Ждем указанный интервал
        for (int i = 0; i < interval && writer2_running; ++i) {
            sleep(1);
        }
    }
    
    // Отправляем финальное сообщение
    logger.log(LogLevel::INFO, "Writer2", "Writer2 shutting down");
    writer.publishMessage("Writer2 shutting down", publish_tag);
    writer.publishMessage("Writer2 shutting down", "log");
    
    if (foreground_mode) {
        std::cout << "Writer2 daemon stopped\n";
    }
    
    Daemonizer::cleanupPidFile(pid_file);
    return 0;
}
