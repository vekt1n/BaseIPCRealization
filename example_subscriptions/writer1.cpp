#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include "../SharedMemory/include/BaseMemory.hpp"

using namespace std;

int main(int argc, char* argv[]) {
    string my_queue = "/myapp_queue";
    string other_queue = "/otherapp_queue";

    if (argc >= 3) {
        my_queue = argv[1];
        other_queue = argv[2];
    } else {
        cout << "Usage: " << argv[0] << " <my_queue> <other_queue>" << endl;
        cout << "Using default queues: " << my_queue << " and " << other_queue << endl;
    }

    BaseMemory memory("/writer1");
    Result res;

    res = memory.createConnection();
    if (!res.result) {
        cout << "Failed to create connection: " << res.message << endl;
        return 1;
    }
    cout << "Connection created successfully" << endl;

    res = memory.openConnection("/adapter");
    if (!res.result) {
        cout << "Failed to open connection to /adapter: " << res.message << endl;
        memory.deleteConnection();
        return 1;
    }
    cout << "Opened connection to /adapter successfully" << endl;

    atomic<bool> running(true);

    thread receiver([&memory, &running]() {
        while (running) {
            if (memory.hasMessage()) {
                Message msg;
                Result res = memory.getMessage(msg);
                if (res.result) {
                    cout << "\n[RECEIVED from " << msg.sender << "]: " << msg.message << endl;
                    cout << "> ";
                    cout.flush();
                } else {
                    cerr << "Error receiving message: " << res.message << endl;
                }
            }
            this_thread::sleep_for(chrono::milliseconds(10));
        }
    });

    cout << "Dual-thread messenger started." << endl;
    cout << "Type messages and press Enter to send." << endl;
    cout << "Type 'exit' to quit." << endl;
    cout << "> ";
    cout.flush();

    string input;
    while (running) {
        getline(cin, input);
        if (input == "exit") {
            running = false;
            break;
        }
        if (!input.empty()) {
            if (memory.hasSpace()) {
                res = memory.sendMessage(input);
                if (res.result) {
                    cout << "[SENT] " << input << endl;
                } else {
                    cerr << "Send failed: " << res.message << endl;
                }
            } else {
                cerr << "No space in queue, message not sent." << endl;
            }
        }
        cout << "> ";
        cout.flush();
    }

    if (receiver.joinable())
        receiver.join();

    memory.deleteConnection();
    cout << "Exiting." << endl;
    return 0;
}