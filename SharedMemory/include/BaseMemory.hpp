#ifndef BASE_MEMORY_HPP
#define BASE_MEMORY_HPP

#include <atomic>
#include <mutex>
#include <pthread.h>
#include <string>

#define SHM_SIZE 30000
#define MAX_MESSAGES 100
#define MESSAGE_SIZE 256

struct Message {
    std::string message;
    std::string sender;
};

struct SharedQueue {
    std::atomic<int> message_count;  // Атомарный счетчик сообщений
    int write_index;                 // Индекс для записи (только для писателей)
    int read_index;                  // Индекс для чтения (только для читателя)
    int k;
    bool initialized;                // Флаг инициализации
    char buffer[MAX_MESSAGES][MESSAGE_SIZE];  // Буфер сообщений
    pthread_mutex_t write_mutex;     // Мьютекс для синхронизации писателей
    std::mutex send_mutex;
};

class BaseMemory {
private:
    char shm_name[256];
    int this_shm_fd;
    SharedQueue* this_queue;
    int send_shm_fd;
    SharedQueue* send_queue;
    std::mutex init_mutex;  // Только для локальной синхронизации создания

public:
    BaseMemory(const char* name);
    ~BaseMemory();
    
    bool createConnection();
    bool openConnection(const char* name);
    bool closeConnection();
    bool deleteConnection();
    
    bool sendMessage(const char* message);
    bool getMessage(char* buffer, size_t buffer_size);
    std::string getMessage();  // Новая перегруженная версия
    bool hasMessage();

    int getK();
    bool sumK();
};

#endif // BASE_MEMORY_HPP