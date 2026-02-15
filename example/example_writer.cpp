#include <iostream>
#include <string>
#include <unistd.h>  // Для sleep
#include "../SharedMemory/include/BaseMemory.hpp"
#include <thread>

using namespace std;

int main() {
    Result res;
    BaseMemory writer("/writer_queue1");
    
    // Создаем соединение для себя
    res = writer.createConnection();
    if (!res.result) {
        cout << res.message << endl;
        return 1;
    }
    res = writer.openConnection("/reader_queue");
    // Открываем соединение с читателем
    if (!res.result) {
        cout << res.message << endl;
        return 1;
    }
    
    cout << "Writer started. Type messages (type 'exit' to quit):" << endl;
    
    string input;
    while (true) {
        res = writer.sendMessage("Hello");
        if (res.result == 1) {
            cout << "Message send" << endl;
        }
        else {
            cout << "Send fail" << endl;
        }
        res = writer.readOrNotMess();
        if (res.result == 0) {
            cout << res.message << endl;
        }
        this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    writer.deleteConnection();
    return 0;
}