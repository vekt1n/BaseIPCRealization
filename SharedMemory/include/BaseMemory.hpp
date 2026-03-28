#ifndef BASE_MEMORY_HPP
#define BASE_MEMORY_HPP

#include <atomic>
#include <mutex>
#include <pthread.h>
#include <string>
#include <chrono>   

#define SHM_SIZE 60000
#define MAX_MESSAGES 10
#define MESSAGE_SIZE 256

struct Message {
    std::string message;
    std::string sender;
    std::string tag;
};

struct Result {
    bool result;
    std::string message;

    Result(bool res, std::string mess)
        : result(res),
          message(mess) {}
    Result()
        : result(false),
          message("") {}
};

class BaseMemory {
private:
    struct MessageForShm {
        char message[MESSAGE_SIZE];
        char sender[256];
        char tag[256];
        bool is_read;
    };

    struct SharedQueue {
        std::atomic<int> message_count;
        int write_index;
        int read_index;
        int k;
        bool initialized;
        MessageForShm buffer[MAX_MESSAGES];
        pthread_mutex_t write_mutex;
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
    std::mutex init_mutex;
    CheckMessage checkMessages;

    Result SuccessResult = {true, "Success"};

public:
    BaseMemory(const char* name);
    ~BaseMemory();
    
    Result createConnection();
    Result openConnection(const char* name);
    Result closeConnection();
    Result deleteConnection();
    Result sendMessage(const Message& message);
    
    Result sendMessage(const char* message);
    Result sendMessage(const std::string message);
    Result sendMessage(const char* send_for, const char* message);
    Result sendMessage(const std::string send_for, const char* message);
    Result sendMessage(const char* send_for, const std::string message);
    Result sendMessage(const std::string send_for, const std::string message);
    Result publishMessage(const char* message, const char* tag);
    Result publishMessage(const std::string message, const char* tag);
    Result publishMessage(const char* message, const std::string tag);
    Result publishMessage(const std::string message, const std::string tag);
    Result getMessage(Message& buffer);
    bool hasMessage();
    bool hasSpace();
    Result readOrNotMess();
};

#endif // BASE_MEMORY_HPP