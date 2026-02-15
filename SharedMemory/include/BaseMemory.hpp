#ifndef BASE_MEMORY_HPP
#define BASE_MEMORY_HPP

#include <atomic>
#include <mutex>
#include <pthread.h>
#include <string>
#include <chrono>   

#define SHM_SIZE 30000
#define MAX_MESSAGES 10
#define MESSAGE_SIZE 256

struct Message {
    char message[MESSAGE_SIZE];
    char sender[256];
};

struct Result {
    bool result;
    std::string message;

    Result(bool res, std::string mess)
        : result(res),
          message(mess) {}
    Result()
        : result(0),
          message("") {}
};

Result SuccessRessult();

class BaseMemory {
private:
    struct MessageForShm {
        char message[MESSAGE_SIZE];
        char sender[256];
        bool is_read;
    };

    struct SharedQueue {
        std::atomic<int> message_count;  // Атомарный счетчик сообщений
        int write_index;                 // Индекс для записи (только для писателей)
        int read_index;                  // Индекс для чтения (только для читателя)
        int k;
        bool initialized;                // Флаг инициализации
        MessageForShm buffer[MAX_MESSAGES];    // Буфер сообщений
        pthread_mutex_t write_mutex;     // Мьютекс для синхронизации писателей
        std::mutex send_mutex;
    };

    struct CheckMessage {
        MessageForShm* buffer[MAX_MESSAGES];
        std::chrono::steady_clock::time_point time[MAX_MESSAGES];
        char reader[MAX_MESSAGES][256];
        int write_index;
        int read_index;
        std::atomic<int> message_count;
    };

    char shm_name[256];
    char send_shm_name[256];
    int this_shm_fd;
    SharedQueue* this_queue;
    int send_shm_fd;
    SharedQueue* send_queue;
    std::mutex init_mutex;  // Только для локальной синхронизации создания
    CheckMessage checkMessages;

public:
    BaseMemory(const char* name);
    ~BaseMemory();
    
    Result createConnection();
    Result openConnection(const char* name);
    Result closeConnection();
    Result deleteConnection();
    
    Result sendMessage(const char* message);
    Result getMessage(Message& buffer);
    Message getMessage();  // Новая перегруженная версия
    bool hasMessage();
    Result readOrNotMess();
};

#endif // BASE_MEMORY_HPP