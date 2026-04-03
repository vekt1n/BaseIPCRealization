#include "../SharedMemory/include/BaseMemory.hpp"
#include "../SharedMemory/include/Daemonizer.hpp"
#include <iostream>
#include <csignal>
#include <unistd.h>
#include <cstring>
#include <sstream>
#include <thread>
#include <chrono>

volatile sig_atomic_t reader_running = 1;

void reader_signal_handler(int sig) {
    (void)sig;
    reader_running = 0;
}

int main(int argc, char* argv[]) {
    bool foreground_mode = false;
    std::string pid_file = "/tmp/reader_daemon.pid";
    std::string subscribe_tag = "data";  // Тег по умолчанию для подписки
    
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--foreground") == 0) {
            foreground_mode = true;
        } else if (strcmp(argv[i], "--pid-file") == 0 && i + 1 < argc) {
            pid_file = argv[++i];
        } else if (strcmp(argv[i], "--tag") == 0 && i + 1 < argc) {
            subscribe_tag = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            std::cout << "Reader Daemon - reads messages via subscriptions\n"
                      << "Usage: " << argv[0] << " [options]\n"
                      << "Options:\n"
                      << "  --foreground       Run in foreground (not as daemon)\n"
                      << "  --pid-file <file>  Set PID file path\n"
                      << "  --tag <tag>        Subscribe to tag (default: data)\n"
                      << "  --help             Show this help\n";
            return 0;
        }
    }
    
    // Демонизируем если нужно
    if (!foreground_mode) {
        if (!Daemonizer::daemonize(pid_file)) {
            std::cerr << "Failed to daemonize reader!" << std::endl;
            return 1;
        }
    } else {
        std::cout << "Reader daemon running in foreground mode\n";
        Daemonizer::writePidFile(pid_file);
    }
    
    // Настраиваем обработчики сигналов
    Daemonizer::setupSignalHandlers();
    signal(SIGTERM, reader_signal_handler);
    signal(SIGINT, reader_signal_handler);
    
    // Создаем свою очередь для чтения
    BaseMemory reader("/reader_daemon_queue");
    Result res = reader.createConnection();
    if (!res.result) {
        std::cerr << "Failed to create reader queue: " << res.message << std::endl;
        Daemonizer::cleanupPidFile(pid_file);
        return 1;
    }
    
    // Подписываемся на нужный тег через Adapter
    res = reader.openConnection("/adapter");
    if (!res.result) {
        std::cerr << "Failed to connect to adapter: " << res.message << std::endl;
        Daemonizer::cleanupPidFile(pid_file);
        return 1;
    }
    
    std::string sub_cmd = "sub_to " + subscribe_tag;
    reader.sendMessage(sub_cmd.c_str());
    reader.closeConnection();
    
    if (foreground_mode) {
        std::cout << "Reader daemon started (PID: " << getpid() << ")\n"
                  << "Subscribed to tag: " << subscribe_tag << "\n"
                  << "Waiting for messages...\n"
                  << "Press Ctrl+C to exit\n";
    }
    
    // Главный цикл
    while (reader_running) {
        if (reader.hasMessage()) {
            Message msg;
            res = reader.getMessage(msg);
            if (res.result) {
                if (foreground_mode) {
                    std::cout << "[" << msg.tag << "] [" << msg.sender << "]: " 
                              << msg.message << std::endl;
                }
                
                // Пересылаем в логгер через publishMessage
                reader.publishMessage(
                    ("Received from " + msg.sender + ": " + msg.message),
                    "log"
                );
                
                // Проверяем на команду выхода
                if (msg.message == "exit") {
                    if (foreground_mode) {
                        std::cout << "Received exit command, shutting down...\n";
                    }
                    break;
                }
            }
        }
        usleep(100000); // 100ms
    }
    
    // Отписываемся при выходе
    res = reader.openConnection("/adapter");
    if (res.result) {
        std::string unsub_cmd = "unsub_to " + subscribe_tag;
        reader.sendMessage(unsub_cmd.c_str());
        reader.closeConnection();
    }
    
    if (foreground_mode) {
        std::cout << "Reader daemon stopped\n";
    }
    
    Daemonizer::cleanupPidFile(pid_file);
    return 0;
}
