#include "../include/BaseMemory.hpp"
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

// Конструктор
BaseMemory::BaseMemory(const char* name) 
    : this_shm_fd(-1), this_queue(nullptr), 
      send_shm_fd(-1), send_queue(nullptr) {
    strncpy(shm_name, name, sizeof(shm_name) - 1);
    shm_name[sizeof(shm_name) - 1] = '\0';
}

bool BaseMemory::createConnection() {
    // Создаем или открываем shared memory
    this_shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (this_shm_fd == -1) {
        perror("shm_open");
        return false;
    }
    
    // Устанавливаем размер
    if (ftruncate(this_shm_fd, SHM_SIZE) == -1) {
        close(this_shm_fd);
        shm_unlink(shm_name);
        perror("ftruncate");
        return false;
    }
    
    // Отображаем в память
    this_queue = static_cast<SharedQueue*>(
        mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, this_shm_fd, 0)
    );
    
    if (this_queue == MAP_FAILED) {
        close(this_shm_fd);
        shm_unlink(shm_name);
        perror("mmap");
        return false;
    }

    std::lock_guard<std::mutex> lock(init_mutex);
    if (!(this_queue->initialized)) {
        this_queue->write_index = 0;
        this_queue->read_index = 0;
        this_queue->message_count = 0;
        this_queue->k = 0;
        
        // Инициализируем POSIX мьютекс для межпроцессной синхронизации писателей
        pthread_mutexattr_t mutex_attr;
        pthread_mutexattr_init(&mutex_attr);
        pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&this_queue->write_mutex, &mutex_attr);
        pthread_mutexattr_destroy(&mutex_attr);
        
        this_queue->initialized = true;
    }

    return true;
}

BaseMemory::~BaseMemory() {
    deleteConnection();
}

bool BaseMemory::sendMessage(const char* message) {
    if (!send_queue || !message) {
        return false;
    }

    // Блокируем для синхронизации писателей
    send_queue->send_mutex.lock();
    // Проверяем, есть ли место в очереди
    if (send_queue->message_count >= MAX_MESSAGES) {
        std::cout << "Очередь переполнена, сообщение отброшено" << std::endl;
        pthread_mutex_unlock(&send_queue->write_mutex);
        return false;
    }

    // Запоминаем текущую позицию для записи
    int current_write_index = send_queue->write_index;
    
    // Копируем сообщение с безопасным ограничением длины
    size_t len = strlen(message);
    size_t copy_len = (len < MESSAGE_SIZE - 1) ? len : MESSAGE_SIZE - 1;
    memcpy(send_queue->buffer[current_write_index], message, copy_len);
    send_queue->buffer[current_write_index][copy_len] = '\0';

    // Обновляем индекс записи (только для писателей)
    send_queue->write_index = (current_write_index + 1) % MAX_MESSAGES;

    // Атомарно увеличиваем счетчик сообщений
    // memory_order_seq_cst для полной синхронизации между писателями и читателем
    send_queue->message_count.fetch_add(1, std::memory_order_seq_cst);
    send_queue->send_mutex.unlock();

    return true;
}

bool BaseMemory::openConnection(const char* name) {
    send_shm_fd = shm_open(name, O_RDWR, 0666);
    if (send_shm_fd == -1) {
        perror("shm_open");
        return false;
    }

    // Отображаем в память
    send_queue = static_cast<SharedQueue*>(
        mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, send_shm_fd, 0)
    );
    
    if (send_queue == MAP_FAILED) {
        close(send_shm_fd);
        perror("mmap");
        return false;
    }
    
    // Проверяем инициализацию
    if (!send_queue->initialized) {
        munmap(send_queue, SHM_SIZE);
        close(send_shm_fd);
        std::cerr << "Shared memory not initialized by other process" << std::endl;
        return false;
    }
    
    return true;
}

bool BaseMemory::closeConnection() {
    bool success = true;

    if (send_queue != nullptr) {
        if (munmap(send_queue, SHM_SIZE) == -1) {
            perror("munmap failed");
            success = false;
        }
        send_queue = nullptr;
    }
    
    if (send_shm_fd != -1) {
        if (close(send_shm_fd) == -1) {
            perror("close failed");
            success = false;
        }
        send_shm_fd = -1;
    }

    return success;
}

// Версия getMessage - читатель ОДИН, синхронизация не нужна
bool BaseMemory::getMessage(char* buffer, size_t buffer_size) {
    if (!this_queue || !buffer || buffer_size < MESSAGE_SIZE) {
        return false;
    }
    
    // Проверяем, есть ли сообщения (атомарная операция)
    // memory_order_seq_cst для гарантии порядка операций
    if (this_queue->message_count.load(std::memory_order_seq_cst) <= 0) {
        buffer[0] = '\0';
        return false;
    }
    
    // Запоминаем текущую позицию для чтения
    int current_read_index = this_queue->read_index;
    
    // Копируем сообщение
    memcpy(buffer, this_queue->buffer[current_read_index], MESSAGE_SIZE);
    
    // Обновляем индекс чтения (только для читателя)
    this_queue->read_index = (current_read_index + 1) % MAX_MESSAGES;
    
    // Атомарно уменьшаем счетчик сообщений
    this_queue->message_count.fetch_sub(1, std::memory_order_seq_cst);
    
    return true;
}

// Альтернативная версия getMessage, которая возвращает строку
std::string BaseMemory::getMessage() {
    std::string message;
    char buffer[MESSAGE_SIZE];
    if (getMessage(buffer, sizeof(buffer))) {
        message = buffer;
    }
    return message;
}

bool BaseMemory::deleteConnection() {
    bool success = true;
    
    // Уничтожаем мьютекс перед удалением памяти
    if (this_queue != nullptr && this_queue->initialized) {
        pthread_mutex_destroy(&this_queue->write_mutex);
    }
    
    // Освобождаем свою очередь
    if (this_queue != nullptr) {
        if (munmap(this_queue, SHM_SIZE) == -1) {
            perror("munmap failed");
            success = false;
        }
        this_queue = nullptr;
    }
    
    if (this_shm_fd != -1) {
        if (close(this_shm_fd) == -1) {
            perror("close failed");
            success = false;
        }
        this_shm_fd = -1;
    }
    
    // Удаляем разделяемую память
    if (shm_unlink(shm_name) == -1) {
        // Это нормально, если память уже удалена другим процессом
        if (errno != ENOENT) {
            perror("shm_unlink failed");
            success = false;
        }
    }
    
    // Также закрываем соединение для отправки, если оно открыто
    closeConnection();
    
    return success;
}

bool BaseMemory::hasMessage() {
    if (!this_queue) {
        return false;
    }
    // Атомарная проверка счетчика
    return this_queue->message_count.load(std::memory_order_seq_cst) > 0;
}


int BaseMemory::getK() {
    return send_queue->k;
}

bool BaseMemory::sumK() {
    ++send_queue->k;
    return 1;
}
