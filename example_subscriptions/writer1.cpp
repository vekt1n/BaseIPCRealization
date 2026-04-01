#include <iostream>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <cstdlib>
#include "../SharedMemory/include/BaseMemory.hpp"

using namespace std;

int main(int argc, char* argv[]) {
    BaseMemory memory("/writer1");
    Result res;

    res = memory.createConnection();
    if (!res.result) {
        cout << "Failed to create connection: " << res.message << endl;
        return 1;
    }
    cout << "Connection created successfully" << endl;

    atomic<bool> running(true);

    thread receiver([&memory, &running]() {
        while (running) {
            if (memory.hasMessage()) {
                Message msg;
                Result res = memory.getMessage(msg);
                if (res.result) {
                    cout << "\n[RECEIVED from " << msg.sender
                    << " with tag \'" << msg.tag << "]: " << msg.message << endl;
                    cout << "who?> ";
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
    cout << "who?> ";
    cout.flush();
    string send_to;
    string message;
    string input;
    while (running) {
        getline(cin, send_to);
        cout << "mess?> ";
        cout.flush();
        getline(cin, message);
        if (send_to == "exit" || message == "exit") {
            running = false;
            break;
        }
        cout << send_to << ' ' << message << endl;
        cout.flush();
        if (!send_to.empty() && !message.empty()) {
            res = memory.sendMessage(send_to, message);
            if (res.result) {
                cout << "[SENT] " << input << endl; 
            } else {
                cerr << "Send failed: " << res.message << endl;
            }
        }
        cout << "who?> ";
        cout.flush();
    }

    if (receiver.joinable())
        receiver.join();

    memory.deleteConnection();
    cout << "Exiting." << endl;
    return 0;
}