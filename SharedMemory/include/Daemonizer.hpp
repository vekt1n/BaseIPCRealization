#ifndef DAEMONIZER_HPP
#define DAEMONIZER_HPP

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <stdlib.h>
#include <signal.h>
#include <fcntl.h>
#include <string>
#include <iostream>
#include <fstream>
#include <functional>
#include <cerrno>
#include <cstring>
#include <pwd.h>  // Для getpwnam

// Внешняя переменная для отслеживания состояния
extern volatile sig_atomic_t daemon_running;

class Daemonizer {
public:
    // Основные методы
    static bool daemonize(const std::string& pid_file = "");
    static void setupSignalHandlers();
    static void signalHandler(int sig);
    
    // Управление PID файлами
    static bool isDaemonRunning(const std::string& pid_file);
    static void writePidFile(const std::string& pid_file);
    static void cleanupPidFile(const std::string& pid_file);
    static bool createPidDirectory(const std::string& pid_dir);
    
    // Управление пользователями
    static bool switchToUser(const std::string& username);
    
    // Управление логами
    static void reopenLogFiles();
    
    // Основной цикл демона
    static void daemonLoop(std::function<void()> main_loop, 
                          std::function<void()> cleanup = nullptr);
    
    // Вспомогательные методы
    static pid_t getDaemonPid(const std::string& pid_file) {
        std::ifstream pidfile(pid_file);
        if (!pidfile.is_open()) {
            return -1;
        }
        pid_t pid;
        pidfile >> pid;
        return pid;
    }
    
    static bool stopDaemon(const std::string& pid_file) {
        pid_t pid = getDaemonPid(pid_file);
        if (pid <= 0) {
            return false;
        }
        
        if (kill(pid, SIGTERM) == 0) {
            // Ждем завершения
            for (int i = 0; i < 10; ++i) {
                if (kill(pid, 0) != 0) {
                    cleanupPidFile(pid_file);
                    return true;
                }
                sleep(1);
            }
            // Если процесс не завершился, отправляем SIGKILL
            kill(pid, SIGKILL);
            cleanupPidFile(pid_file);
            return true;
        }
        
        return false;
    }
    
    static bool reloadDaemon(const std::string& pid_file) {
        pid_t pid = getDaemonPid(pid_file);
        if (pid <= 0) {
            return false;
        }
        return (kill(pid, SIGHUP) == 0);
    }
};

#endif // DAEMONIZER_HPP