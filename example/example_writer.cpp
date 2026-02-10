#include <iostream>
#include <string>
#include <unistd.h>  // Для sleep
#include "../SharedMemory/include/BaseMemory.hpp"

using namespace std;

int main() {
    BaseMemory writer("/writer_queue1");
    
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
    while (true) {
        cout << "> ";
        getline(cin, input);
        
        if (input == "exit") {
            writer.sendMessage("exit");
            break;
        }
        
        if (!writer.sendMessage(input.c_str())) {
            cout << "Failed to send message" << endl;
        } else {
            cout << "Sent: " << input << endl;
        }
    }
    
    writer.deleteConnection();
    return 0;
}