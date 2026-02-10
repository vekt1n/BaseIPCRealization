#include <iostream>
#include <string>
#include <unistd.h>  // Для sleep
#include "../SharedMemory/include/BaseMemory.hpp"

using namespace std;

int main() {
    BaseMemory writer("/writer_queue2");
    
    // Создаем соединение для себя
    if (!writer.createConnection()) {
        cout << "Failed to create connection" << endl;
        return 1;
    }
    
    // Открываем соединение с читателем
    if (!writer.openConnection("/reader_queue")) {
        cout << "Failed to open connection to reader" << endl;
        return 1;
    }
    
    cout << "Writer started. Type messages (type 'exit' to quit):" << endl;
    
    string input;
    int k = 0;
    std::string message = "";
    while (true) {
        k = writer.getK();
        message = to_string(k);
        const char* buf = message.data();
        writer.sendMessage(buf);
        cout << "Send mess \"" << k << "\"" << endl;
        writer.sumK();
        sleep(1);
    }
    
    writer.deleteConnection();
    return 0;
}