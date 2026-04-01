#include <iostream>
#include <string>
#include <unistd.h>
#include "../SharedMemory/include/BaseMemory.hpp"
#include <thread>

using namespace std;

int main(int argc, char* argv[]) {
    string mess = "Hello world";
    string tag = "tag1";
    if (argc >= 3) {
        mess = argv[1];
        tag = argv[2];
    }
    Result res;
    BaseMemory writer("/writer3");
    
    res = writer.createConnection();
    if (!res.result) {
        cout << "Failed to create connection: " << res.message << endl;
        return 1;
    }
    cout << "Connection created successfully" << endl;
    
    cout << "Writer started. Sending messages every second..." << endl;
    
    int message_count = 0;
    while (message_count < 5) {
        if (true) {
            res = writer.publishMessage(mess, tag);
            if (res.result) {
                cout << "Message #" << (message_count + 1) << " sent successfully" << endl;
                message_count++;
            } else {
                cout << "Send failed: " << res.message << endl;
            }
        } else {
            cout << "No space in queue, waiting..." << endl;
        }
        
        res = writer.readOrNotMess();
        if (!res.result) {
            cout << "Read notification: " << res.message << endl;
        }
        
        this_thread::sleep_for(std::chrono::seconds(1));
    }
    
    cout << "Finished sending 5 messages" << endl;
    writer.deleteConnection();
    return 0;
}