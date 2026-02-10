#include "../SharedMemory/include/BaseMemory.hpp"
#include <bits/stdc++.h>
#include <ctime>

using namespace std;

int main() {
    BaseMemory reader("/reader_queue");
    reader.createConnection();
    while (true) {
        if (reader.hasMessage()) {
            cout << reader.getMessage() << endl;
        }
        else {
            cout << "Has no Messages" << endl;
        }
        sleep(1);
    }
}