#include "BaseMemory.hpp"
#include <cstring>
#include <iostream>
#include <mutex>
#include <string>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/stat.h>

BaseMemory::BaseMemory(const char* name) 
    : this_shm_fd(-1), this_queue(nullptr), 
      send_shm_fd(-1), send_queue(nullptr) {
    strncpy(shm_name, name, sizeof(shm_name) - 1);
    shm_name[sizeof(shm_name) - 1] = '\0';
    checkMessages.read_index = 0;
    checkMessages.write_index = 0;
    checkMessages.message_count = 0;
}

Result BaseMemory::createConnection() {
    this_shm_fd = shm_open(shm_name, O_CREAT | O_RDWR, 0666);
    if (this_shm_fd == -1) {
        perror("shm_open");
        return Result(false, "error shm_open");
    }
    
    if (ftruncate(this_shm_fd, SHM_SIZE) == -1) {
        close(this_shm_fd);
        shm_unlink(shm_name);
        perror("ftruncate");
        return Result(false, "error ftruncate");
    }
    
    this_queue = static_cast<SharedQueue*>(
        mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, this_shm_fd, 0)
    );
    
    if (this_queue == MAP_FAILED) {
        close(this_shm_fd);
        shm_unlink(shm_name);
        perror("mmap");        
        return Result(false, "error mmap");
    }

    std::lock_guard<std::mutex> lock(init_mutex);
    if (!this_queue->initialized) {
        this_queue->write_index = 0;
        this_queue->read_index = 0;
        this_queue->message_count = 0;
        this_queue->k = 0;
        
        pthread_mutexattr_t mutex_attr;
        pthread_mutexattr_init(&mutex_attr);
        pthread_mutexattr_setpshared(&mutex_attr, PTHREAD_PROCESS_SHARED);
        pthread_mutex_init(&this_queue->write_mutex, &mutex_attr);
        pthread_mutexattr_destroy(&mutex_attr);
        
        this_queue->initialized = true;
    }

    return SuccessResult;
}

BaseMemory::~BaseMemory() {
    deleteConnection();
}

Result BaseMemory::sendMessage(const Message& message) {
    if (!send_queue) {
        return Result(false, "Has no send_queue");
    }
    
    if (message.message.empty()) {
        return Result(false, "Message is empty");
    }

    pthread_mutex_lock(&send_queue->write_mutex);
    
    if (send_queue->message_count.load(std::memory_order_acquire) >= MAX_MESSAGES) {
        pthread_mutex_unlock(&send_queue->write_mutex);
        return Result(false, "No space for message");
    }

    int current_write_index = send_queue->write_index;
    
    strncpy(send_queue->buffer[current_write_index].message, 
            message.message.c_str(), MESSAGE_SIZE - 1);
    send_queue->buffer[current_write_index].message[MESSAGE_SIZE - 1] = '\0';
    
    strncpy(send_queue->buffer[current_write_index].sender, 
            message.sender.c_str(), 255);
    send_queue->buffer[current_write_index].sender[255] = '\0';
    
    strncpy(send_queue->buffer[current_write_index].tag, 
            message.tag.c_str(), 255);
    send_queue->buffer[current_write_index].tag[255] = '\0';
    
    send_queue->buffer[current_write_index].is_read = false;
    
    if (checkMessages.write_index < MAX_MESSAGES) {
        checkMessages.buffer[checkMessages.write_index] = &send_queue->buffer[current_write_index];
        strncpy(checkMessages.reader[checkMessages.write_index], send_shm_name, 255);
        checkMessages.time[checkMessages.write_index] = std::chrono::steady_clock::now();
    }

    send_queue->write_index = (current_write_index + 1) % MAX_MESSAGES;
    checkMessages.write_index = (checkMessages.write_index + 1) % MAX_MESSAGES;
    send_queue->message_count.fetch_add(1, std::memory_order_release);
    checkMessages.message_count.fetch_add(1, std::memory_order_release);
    
    pthread_mutex_unlock(&send_queue->write_mutex);

    return SuccessResult;
}

Result BaseMemory::sendMessage(const char* message) {
    if (!send_queue || !message) {
        return Result(false, "Has no send_queue or message is null");
    }
    
    Message msg;
    msg.message = message;
    msg.sender = shm_name;
    msg.tag = "direct";
    
    return sendMessage(msg);
}

Result BaseMemory::sendMessage(const std::string message) {
    return sendMessage(message.c_str());
}

Result BaseMemory::openConnection(const char* name) {
    send_shm_fd = shm_open(name, O_RDWR, 0666);
    if (send_shm_fd == -1) {
        perror("shm_open");
        return Result(false, "error shm_open");
    }
    
    strncpy(send_shm_name, name, 255);
    send_shm_name[255] = '\0';

    send_queue = static_cast<SharedQueue*>(
        mmap(0, SHM_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, send_shm_fd, 0)
    );
    
    if (send_queue == MAP_FAILED) {
        close(send_shm_fd);
        perror("mmap");
        return Result(false, "error mmap");
    }
    
    if (!send_queue->initialized) {
        munmap(send_queue, SHM_SIZE);
        close(send_shm_fd);
        return Result(false, "Shared memory not initialized by other process");
    }
    
    return SuccessResult;
}

Result BaseMemory::closeConnection() {
    Result success = SuccessResult;

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
            if (!success.result) success.message += ", close failed";
            else success.message = "close failed";
            success.result = false;
        }
        send_shm_fd = -1;
    }
    
    memset(send_shm_name, 0, sizeof(send_shm_name));
    
    return success;
}

Result BaseMemory::getMessage(Message& buffer) {
    if (!this_queue) {
        return Result(false, "Has no this_queue");
    }
    
    if (this_queue->message_count.load(std::memory_order_acquire) <= 0) {
        return Result(false, "No messages");
    }
    
    int current_read_index = this_queue->read_index;
    
    buffer.message = this_queue->buffer[current_read_index].message;
    buffer.sender = this_queue->buffer[current_read_index].sender;
    buffer.tag = this_queue->buffer[current_read_index].tag;
    this_queue->buffer[current_read_index].is_read = true;
    
    this_queue->read_index = (current_read_index + 1) % MAX_MESSAGES;
    this_queue->message_count.fetch_sub(1, std::memory_order_release);
    
    return SuccessResult;
}

Result BaseMemory::deleteConnection() {
    Result success = SuccessResult;
    
    if (this_queue != nullptr && this_queue->initialized) {
        pthread_mutex_destroy(&this_queue->write_mutex);
    }
    
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
            if (!success.result) success.message += ", close failed";
            else success.message = "close failed";
            success.result = false;
        }
        this_shm_fd = -1;
    }
    
    if (shm_unlink(shm_name) == -1) {
        if (errno != ENOENT) {
            perror("shm_unlink failed");
            if (!success.result) success.message += ", shm_unlink failed";
            else success.message = "shm_unlink failed";
            success.result = false;
        }
    }
    
    closeConnection();
    
    return success;
}

bool BaseMemory::hasMessage() {
    if (!this_queue) return false;
    return this_queue->message_count.load(std::memory_order_acquire) > 0;
}

bool BaseMemory::hasSpace() {
    if (!send_queue) return false;
    return send_queue->message_count.load(std::memory_order_acquire) < MAX_MESSAGES;
}

Result BaseMemory::readOrNotMess() {
    std::chrono::steady_clock::time_point now = std::chrono::steady_clock::now();
    
    if (checkMessages.message_count.load(std::memory_order_acquire) > 0) {
        int current_read = checkMessages.read_index;
        
        if (std::chrono::duration_cast<std::chrono::seconds>(now - checkMessages.time[current_read]) 
            >= std::chrono::seconds(5)) {
            
            if (checkMessages.buffer[current_read] && 
                !checkMessages.buffer[current_read]->is_read) {
                std::string mess = checkMessages.reader[current_read];
                mess += " did not read message: ";
                mess += checkMessages.buffer[current_read]->message;
                return Result(false, mess);
            }
            
            checkMessages.read_index = (checkMessages.read_index + 1) % MAX_MESSAGES;
            checkMessages.message_count.fetch_sub(1, std::memory_order_release);
        }
    }
    
    return SuccessResult;
}

Result BaseMemory::sendMessage(const char* send_for, const char* message) {
    if (!send_for || !message) {
        return Result(false, "Invalid parameters");
    }
    
    Result res = openConnection(send_for);
    if (!res.result) {
        return res;
    }
    
    if (!hasSpace()) {
        closeConnection();
        return Result(false, "Has No Enough space");
    }
    
    res = sendMessage(message);
    if (!res.result) {
        closeConnection();
        return res;
    }
    
    closeConnection();
    return SuccessResult;
}

Result BaseMemory::sendMessage(const std::string send_for, const char* message) {
    return sendMessage(send_for.c_str(), message);
}

Result BaseMemory::sendMessage(const char* send_for, const std::string message) {
    return sendMessage(send_for, message.c_str());
}

Result BaseMemory::sendMessage(const std::string send_for, const std::string message) {
    return sendMessage(send_for.c_str(), message.c_str());
}

Result BaseMemory::publishMessage(const char* message, const char* tag) {
    if (!message || !tag) {
        return Result(false, "Invalid parameters");
    }
    
    Message mess;
    mess.message = message;
    mess.sender = shm_name;
    mess.tag = tag;
    
    Result res = openConnection("/adapter");
    if (!res.result) {
        return res;
    }

    if (send_queue->message_count.load(std::memory_order_acquire) >= MAX_MESSAGES) {
        closeConnection();
        return Result(false, "Has No Enough Space");
    }

    res = sendMessage(mess);
    if (!res.result) {
        closeConnection();
        return res;
    }

    closeConnection();
    return SuccessResult;
}

Result BaseMemory::publishMessage(const std::string message, const char* tag) {
    return publishMessage(message.c_str(), tag);
}

Result BaseMemory::publishMessage(const char* message, const std::string tag) {
    return publishMessage(message, tag.c_str());
}

Result BaseMemory::publishMessage(const std::string message, const std::string tag) {
    return publishMessage(message.c_str(), tag.c_str());
}