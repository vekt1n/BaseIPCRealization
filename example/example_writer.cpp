#include <iostream>
#include <string>
#include <unistd.h>  // Для sleep
#include "../SharedMemory/include/BaseMemory.hpp"

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
        cout << "> ";
        getline(cin, input);
        
        if (input == "exit") {
            writer.sendMessage("exit");
            break;
        }
        res = writer.sendMessage(input.c_str());
        if (!res.result) {
            cout << res.message << endl;
        } else {
            cout << "Sent: " << input << endl;
        }
    }
    
    writer.deleteConnection();
    return 0;
}