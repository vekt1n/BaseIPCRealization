#include "Daemonizer.hpp"
#include <iostream>
#include <fstream>
#include <csignal>
#include <sys/file.h>  // Для flock
#include <cstring>

// Глобальная переменная для отслеживания состояния демона
volatile sig_atomic_t daemon_running = 1;

void Daemonizer::signalHandler(int sig) {
    switch(sig) {
        case SIGTERM:
        case SIGINT:
        case SIGQUIT:
            daemon_running = 0;
            break;
        case SIGHUP:
            // Перезагрузка конфигурации
            break;
        default:
            break;
    }
}

bool Daemonizer::daemonize(const std::string& pid_file) {
    // Уже демон? Проверяем родительский процесс
    if (getppid() == 1) {
        return true;  // Уже демон
    }
    
    // Первый fork
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "First fork failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Если это родительский процесс - завершаем его
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    // Создаем новую сессию
    pid_t sid = setsid();
    if (sid < 0) {
        std::cerr << "Failed to create new session: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Игнорируем некоторые сигналы
    signal(SIGCHLD, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    
    // Второй fork для гарантии, что процесс не станет лидером сессии
    pid = fork();
    if (pid < 0) {
        std::cerr << "Second fork failed: " << strerror(errno) << std::endl;
        return false;
    }
    
    if (pid > 0) {
        exit(EXIT_SUCCESS);
    }
    
    // Устанавливаем маску создания файлов
    umask(0);
    
    // Меняем рабочую директорию на корневую
    if (chdir("/") < 0) {
        std::cerr << "Failed to change directory to /: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Закрываем стандартные файловые дескрипторы
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    
    // Перенаправляем стандартные дескрипторы в /dev/null
    int null_fd = open("/dev/null", O_RDWR);
    if (null_fd < 0) {
        return false;
    }
    
    // Дублируем дескрипторы
    if (dup2(null_fd, STDIN_FILENO) < 0) {
        close(null_fd);
        return false;
    }
    
    if (dup2(null_fd, STDOUT_FILENO) < 0) {
        close(null_fd);
        return false;
    }
    
    if (dup2(null_fd, STDERR_FILENO) < 0) {
        close(null_fd);
        return false;
    }
    
    close(null_fd);
    
    // Записываем PID в файл, если указан
    if (!pid_file.empty()) {
        writePidFile(pid_file);
    }
    
    return true;
}

void Daemonizer::setupSignalHandlers() {
    struct sigaction new_action;
    
    // Настраиваем обработчик для SIGTERM
    new_action.sa_handler = signalHandler;
    sigemptyset(&new_action.sa_mask);
    new_action.sa_flags = 0;
    sigaction(SIGTERM, &new_action, NULL);
    
    // Настраиваем обработчик для SIGINT
    sigaction(SIGINT, &new_action, NULL);
    
    // Настраиваем обработчик для SIGQUIT
    sigaction(SIGQUIT, &new_action, NULL);
    
    // Игнорируем SIGPIPE (чтобы не завершаться при закрытии каналов)
    signal(SIGPIPE, SIG_IGN);
    
    // Настраиваем обработчик для SIGHUP (перезагрузка конфигурации)
    sigaction(SIGHUP, &new_action, NULL);
}

bool Daemonizer::isDaemonRunning(const std::string& pid_file) {
    if (pid_file.empty()) {
        return false;
    }
    
    std::ifstream pidfile(pid_file);
    if (!pidfile.is_open()) {
        return false;  // Файл не существует
    }
    
    pid_t pid;
    pidfile >> pid;
    pidfile.close();
    
    // Проверяем, существует ли процесс с таким PID
    if (pid <= 0) {
        return false;
    }
    
    // Отправляем сигнал 0 для проверки существования процесса
    if (kill(pid, 0) == 0) {
        return true;  // Процесс существует
    }
    
    // Процесс не существует, удаляем старый PID файл
    if (errno == ESRCH) {
        unlink(pid_file.c_str());
    }
    
    return false;
}

void Daemonizer::writePidFile(const std::string& pid_file) {
    // Создаем PID файл с блокировкой
    int fd = open(pid_file.c_str(), O_CREAT | O_RDWR, 0644);
    if (fd < 0) {
        std::cerr << "Cannot create PID file: " << pid_file 
                  << " Error: " << strerror(errno) << std::endl;
        return;
    }
    
    // Блокируем файл
    if (flock(fd, LOCK_EX | LOCK_NB) < 0) {
        close(fd);
        std::cerr << "Another instance is already running!" << std::endl;
        exit(EXIT_FAILURE);
    }
    
    // Очищаем файл
    ftruncate(fd, 0);
    
    // Записываем PID
    std::string pid_str = std::to_string(getpid());
    write(fd, pid_str.c_str(), pid_str.length());
    
    // Сохраняем блокировку
    flock(fd, LOCK_UN);
    close(fd);
}

void Daemonizer::cleanupPidFile(const std::string& pid_file) {
    if (!pid_file.empty()) {
        unlink(pid_file.c_str());
    }
}

void Daemonizer::reopenLogFiles() {
    // Этот метод может быть использован для ротации логов
    // При получении SIGHUP демоны могут переоткрыть файлы логов
}

bool Daemonizer::switchToUser(const std::string& username) {
    if (username.empty()) {
        return true;  // Не нужно менять пользователя
    }
    
    // Получаем информацию о пользователе
    struct passwd* pw = getpwnam(username.c_str());
    if (!pw) {
        std::cerr << "User " << username << " not found!" << std::endl;
        return false;
    }
    
    // Меняем группу
    if (setgid(pw->pw_gid) != 0) {
        std::cerr << "Failed to set group id: " << strerror(errno) << std::endl;
        return false;
    }
    
    // Меняем пользователя
    if (setuid(pw->pw_uid) != 0) {
        std::cerr << "Failed to set user id: " << strerror(errno) << std::endl;
        return false;
    }
    
    return true;
}

void Daemonizer::daemonLoop(std::function<void()> main_loop, 
                           std::function<void()> cleanup) {
    try {
        // Основной цикл демона
        while (daemon_running) {
            main_loop();
            sleep(1);  // Небольшая пауза, чтобы не нагружать CPU
        }
    } catch (const std::exception& e) {
        std::cerr << "Daemon exception: " << e.what() << std::endl;
    }
    
    // Выполняем очистку
    if (cleanup) {
        cleanup();
    }
}

bool Daemonizer::createPidDirectory(const std::string& pid_dir) {
    struct stat st;
    
    // Проверяем, существует ли директория
    if (stat(pid_dir.c_str(), &st) != 0) {
        // Директория не существует, создаем
        if (mkdir(pid_dir.c_str(), 0755) != 0) {
            std::cerr << "Failed to create PID directory: " << pid_dir 
                      << " Error: " << strerror(errno) << std::endl;
            return false;
        }
    } else if (!S_ISDIR(st.st_mode)) {
        // Существует, но не является директорией
        std::cerr << "PID path is not a directory: " << pid_dir << std::endl;
        return false;
    }
    
    return true;
}