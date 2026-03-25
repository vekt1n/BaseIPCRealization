#include "../SharedMemory/include/BaseMemory.hpp"
#include <bits/stdc++.h>
#include <ctime>

using namespace std;

int main() {
    BaseMemory reader("/reader_queue");
    reader.createConnection();
    Result res;
    while (true) {
        Message message;
        if (reader.hasMessage()) {
            res = reader.getMessage(message);
            if (res.result)
                cout << "Mess from " << message.sender << ": " << message.message << endl;
            else 
                cout << "error: " << res.message << endl;
        }
        sleep(1);
    }
}