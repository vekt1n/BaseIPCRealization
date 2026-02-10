#include "../SharedMemory/include/BaseMemory.hpp"
#include <bits/stdc++.h>
#include <ctime>

using namespace std;

int main() {
    BaseMemory reader("/reader_queue");
    reader.createConnection();
    while (true) {
        Message message;
        if (reader.hasMessage()) {
            reader.getMessage(message);
            cout << "Mess from " << message.sender << ": " << message.message << endl;
        }
        sleep(1);
    }
}