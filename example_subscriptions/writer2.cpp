#include <iostream>
#include <string>
#include <unistd.h>
#include "../SharedMemory/include/BaseMemory.hpp"
#include <thread>

using namespace std;

int main() {
    Result res;
    BaseMemory writer("/writer_queue1");
    
    res = writer.createConnection();
    if (!res.result) {
        cout << "Failed to create connection: " << res.message << endl;
        return 1;
    }
    cout << "Connection created successfully" << endl;
    
    // res = writer.openConnection("/adapter");
    // if (!res.result) {
    //     cout << "Failed to open connection to /adapter: " << res.message << endl;
    //     writer.deleteConnection();
    //     return 1;
    // }
    // cout << "Opened connection to /adapter successfully" << endl;
    
    cout << "Writer started. Sending messages every second..." << endl;
    
    int message_count = 0;
    while (message_count < 5) {
        if (true) {
            // Message mess;
            // mess.message = "Hello from writer2 - message #" + to_string(message_count + 1);
            // mess.tag = "tag1";
            // mess.sender = "writer2";
            
            res = writer.publishMessage("Hello world", "tag1");
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