#include "../include/BaseMemory.hpp"
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

Result SuccessRessult() {
    return Result(1, "Success");
}

// Конструктор
BaseMemory::BaseMemory(const char* name) 
    : this_shm_fd(-1), this_queue(nullptr), 
      send_shm_fd(-1), send_queue(nullptr) {
    strncpy(shm_name, name, sizeof(shm_name) - 1);
    shm_name[sizeof(shm_name) - 1] = '\0';
    checkMessages.read_index = 0;
    checkMessages.write_index = 0;
}

Result BaseMemory::createConnection() {
    // Создаем или открываем shared memory
    this_shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (this_shm_fd == -1) {
        perror("shm_open");
        return Result(0, "error shm_open");
    }
    
    // Устанавливаем размер
    if (ftruncate(this_shm_fd, SHM_SIZE) == -1) {
        close(this_shm_fd);
        shm_unlink(shm_name);
        perror("ftruncate");
        return Result(0, "error ftruncate");

    }
    
    // Отображаем в память
    this_queue = static_cast<SharedQueue*>(
        mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, this_shm_fd, 0)
    );
    
    if (this_queue == MAP_FAILED) {
        close(this_shm_fd);
        shm_unlink(shm_name);
        perror("mmap");        
        return Result(0, "error mmap");
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

    return SuccessRessult();
}

BaseMemory::~BaseMemory() {
    deleteConnection();
}

Result BaseMemory::sendMessage(const char* message) {
    if (!send_queue || !message) {
        return Result(0, "Has no send_queue or message");
    }

    // Блокируем для синхронизации писателей
    send_queue->send_mutex.lock();
    // Проверяем, есть ли место в очереди
    if (send_queue->message_count >= MAX_MESSAGES) {
        pthread_mutex_unlock(&send_queue->write_mutex);
        return Result(0, "Has no space for message. Message missed");
    }

    // Запоминаем текущую позицию для записи
    int current_write_index = send_queue->write_index;
    
    // Копируем сообщение с безопасным ограничением длины
    size_t len = strlen(message);
    size_t copy_len = (len < MESSAGE_SIZE - 1) ? len : MESSAGE_SIZE - 1;
    memcpy(send_queue->buffer[current_write_index].message, message, copy_len);
    send_queue->buffer[current_write_index].message[copy_len] = '\0';
    memcpy(send_queue->buffer[current_write_index].sender, shm_name, strlen(shm_name));
    send_queue->buffer[current_write_index].sender[strlen(shm_name)] = '\0';
    send_queue->buffer[current_write_index].is_read = false;
    checkMessages.buffer[checkMessages.write_index] = &send_queue->buffer[current_write_index];
    memcpy(checkMessages.reader[checkMessages.write_index], send_shm_name, strlen(send_shm_name));
    checkMessages.time[checkMessages.write_index] = std::chrono::steady_clock::now();

    // Обновляем индекс записи (только для писателей)
    send_queue->write_index = (current_write_index + 1) % MAX_MESSAGES;
    checkMessages.write_index = (checkMessages.write_index + 1) % MAX_MESSAGES;

    // Атомарно увеличиваем счетчик сообщений
    // memory_order_seq_cst для полной синхронизации между писателями и читателем
    send_queue->message_count.fetch_add(1, std::memory_order_seq_cst);
    checkMessages.message_count.fetch_add(1, std::memory_order_seq_cst);
    send_queue->send_mutex.unlock();

    return SuccessRessult();
}

Result BaseMemory::openConnection(const char* name) {
    send_shm_fd = shm_open(name, O_RDWR, 0666);
    memcpy(send_shm_name, name, strlen(name));
    send_shm_name[strlen(name)] = '\0';
    if (send_shm_fd == -1) {
        perror("shm_open");
        return Result(0, "error shm_open");
    }

    // Отображаем в память
    send_queue = static_cast<SharedQueue*>(
        mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, send_shm_fd, 0)
    );
    
    if (send_queue == MAP_FAILED) {
        close(send_shm_fd);
        perror("mmap");
        return Result(0, "error mmap");
    }
    
    // Проверяем инициализацию
    if (!send_queue->initialized) {
        munmap(send_queue, SHM_SIZE);
        close(send_shm_fd);
        return Result(0, "Shared memory not initialized by other process");
    }
    
    return SuccessRessult();
}

Result BaseMemory::closeConnection() {
    Result success = SuccessRessult();

    if (send_queue != nullptr) {
        if (munmap(send_queue, SHM_SIZE) == -1) {
            perror("munmap failed");
            success.result = false;
            success.message = "munmap failed ";
        }
        send_queue = nullptr;
    }
    
    if (send_shm_fd != -1) {
        if (close(send_shm_fd) == -1) {
            perror("close failed");
            if (success.result == false) success.message += ", close failed";
            else success.message = "close failed";
            success.result = false;
        }
        send_shm_fd = -1;
    }

    return success;
}

// Версия getMessage - читатель ОДИН, синхронизация не нужна
Result BaseMemory::getMessage(Message& buffer) {
    if (!this_queue) {
        return Result(0, "Has no this_queue");
    }
    
    // Проверяем, есть ли сообщения (атомарная операция)
    // memory_order_seq_cst для гарантии порядка операций
    if (this_queue->message_count.load(std::memory_order_seq_cst) <= 0) {
        buffer.message[0] = '\0';
        buffer.sender[0] = '\0';
        return Result(0, "Has no Messages");
    }
    
    // Запоминаем текущую позицию для чтения
    int current_read_index = this_queue->read_index;
    
    // Копируем сообщение
    memcpy(buffer.message, this_queue->buffer[current_read_index].message, MESSAGE_SIZE);
    memcpy(buffer.sender, this_queue->buffer[current_read_index].sender, MESSAGE_SIZE);
    this_queue->buffer[current_read_index].is_read = true;
    
    // Обновляем индекс чтения (только для читателя)
    this_queue->read_index = (current_read_index + 1) % MAX_MESSAGES;
    
    // Атомарно уменьшаем счетчик сообщений
    this_queue->message_count.fetch_sub(1, std::memory_order_seq_cst);
    
    return SuccessRessult();
}

Result BaseMemory::deleteConnection() {
    Result success = SuccessRessult();
    
    // Уничтожаем мьютекс перед удалением памяти
    if (this_queue != nullptr && this_queue->initialized) {
        pthread_mutex_destroy(&this_queue->write_mutex);
    }
    
    // Освобождаем свою очередь
    if (this_queue != nullptr) {
        if (munmap(this_queue, SHM_SIZE) == -1) {
            perror("munmap failed");
            success.result = false;
            success.message = "munmap failed";
        }
        this_queue = nullptr;
    }
    
    if (this_shm_fd != -1) {
        if (close(this_shm_fd) == -1) {
            perror("close failed");
            if (success.result == false) success.message += ", close failed";
            else success.message = "close failed";
            success.result = false;
        }
        this_shm_fd = -1;
    }
    
    // Удаляем разделяемую память
    if (shm_unlink(shm_name) == -1) {
        // Это нормально, если память уже удалена другим процессом
        if (errno != ENOENT) {
            perror("shm_unlink failed");
            if (success.result == false) success.message += ", shm_unlink failed";
            else success.message = "shm_unlink failed";
            success.result = false;
        }
    }
    
    // Также закрываем соединение для отправки, если оно открыто
    closeConnection();
    
    return success;
}

bool BaseMemory::hasMessage() {
    // Атомарная проверка счетчика
    return this_queue->message_count.load(std::memory_order_seq_cst) > 0;
}

Result BaseMemory::readOrNotMess() {
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    Result res = SuccessRessult();
    if (checkMessages.message_count > 0) {
        if (std::chrono::duration_cast<std::chrono::seconds>(now - checkMessages.time[checkMessages.read_index]) 
            >= std::chrono::seconds(5)) {
                if (checkMessages.buffer[checkMessages.read_index]->is_read == false) {
                    // std::string mess = "Message for \"";
                    // mess += checkMessages.reader[checkMessages.read_index];
                    // mess += "\": \"";
                    // mess += checkMessages.buffer[checkMessages.read_index]->message;
                    // mess += "\" is not read";
                    std::string mess = checkMessages.reader[checkMessages.read_index];
                    res = Result(0,  mess);
                    return res;
                }
            checkMessages.read_index = (checkMessages.read_index + 1) % MAX_MESSAGES;
            checkMessages.message_count.fetch_sub(1, std::memory_order_seq_cst);
        }
    }
    
    return res;
}