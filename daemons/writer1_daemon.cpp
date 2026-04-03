#include "../SharedMemory/include/BaseMemory.hpp"
#include "../SharedMemory/include/Daemonizer.hpp"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <ctime>
#include <thread>
#include <chrono>

volatile sig_atomic_t writer1_running = 1;

void writer1_signal_handler(int sig) {
    (void)sig;
    writer1_running = 0;
}

int main(int argc, char* argv[]) {
    bool foreground_mode = false;
    std::string pid_file = "/tmp/writer1_daemon.pid";
    int interval = 3;
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
            std::cout << "Writer1 Daemon - publishes messages via Adapter\n"
                      << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --foreground       Run in foreground (not as daemon)\n"
                      << "  --pid-file <file>  Set PID file path\n"
                      << "  --interval <sec>   Message interval (default: 3)\n"
                      << "  --tag <tag>        Publish tag (default: data)\n"
                      << "  --help             Show this help\n";
            return 0;
        }
    }
    
    // Демонизируем если нужно
    if (!foreground_mode) {
        if (!Daemonizer::daemonize(pid_file)) {
            std::cerr << "Failed to daemonize writer1!" << std::endl;
            return 1;
        }
    } else {
        std::cout << "Writer1 daemon running in foreground mode\n";
        Daemonizer::writePidFile(pid_file);
    }
    
    // Настраиваем обработчики сигналов
    Daemonizer::setupSignalHandlers();
    signal(SIGTERM, writer1_signal_handler);
    signal(SIGINT, writer1_signal_handler);
    
    // Создаем свою очередь
    BaseMemory writer("/writer_daemon_queue1");
    Result res = writer.createConnection();
    if (!res.result) {
        std::cerr << "Failed to create writer1 queue: " << res.message << std::endl;
        Daemonizer::cleanupPidFile(pid_file);
        return 1;
    }
    
    if (foreground_mode) {
        std::cout << "Writer1 daemon started (PID: " << getpid() << ")\n"
                  << "Publishing to tag '" << publish_tag << "' every " 
                  << interval << " seconds\n"
                  << "Press Ctrl+C to exit\n";
    }
    
    int message_counter = 0;
    time_t start_time = time(nullptr);
    
    // Главный цикл
    while (writer1_running) {
        std::stringstream msg;
        time_t current_time = time(nullptr);
        int uptime = static_cast<int>(current_time - start_time);
        
        msg << "Message #" << message_counter++ 
            << " from Writer1 (uptime: " << uptime << "s)";
        
        std::string message_str = msg.str();
        
        // Публикуем сообщение через Adapter с тегом
        res = writer.publishMessage(message_str, publish_tag);
        
        if (!res.result && foreground_mode) {
            std::cerr << "Failed to publish message: " << res.message << std::endl;
        } else if (foreground_mode) {
            std::cout << "Published [" << publish_tag << "]: " << message_str << std::endl;
        }
        
        // Также отправляем лог
        writer.publishMessage(message_str, "log");
        
        // Ждем указанный интервал
        for (int i = 0; i < interval && writer1_running; ++i) {
            sleep(1);
        }
    }
    
    // Отправляем финальное сообщение
    writer.publishMessage("Writer1 shutting down", publish_tag);
    writer.publishMessage("Writer1 shutting down", "log");
    
    if (foreground_mode) {
        std::cout << "Writer1 daemon stopped\n";
    }
    
    Daemonizer::cleanupPidFile(pid_file);
    return 0;
}
