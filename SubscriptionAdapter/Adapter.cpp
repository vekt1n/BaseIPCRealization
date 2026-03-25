#include "../SharedMemory/include/BaseMemory.hpp"
#include <iostream>
#include <map>
#include <sstream>
#include <set>
#include <string.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <signal.h>

using namespace std;

map<string, set<string>> subscription;
atomic<bool> running(true);

void signal_handler(int) {
    running = false;
}

int main() {
    BaseMemory MessBus("/adapter");

    Result res = MessBus.createConnection();
    if (!res.result) {
        cout << "Failed to create adapter connection: " << res.message << endl;
        return 1;
    }
    cout << "Adapter started successfully" << endl;

    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    while (running) {
        if (MessBus.hasMessage()) {
            Message mess;
            res = MessBus.getMessage(mess);
            
            if (!res.result) {
                cerr << "Error getting message: " << res.message << endl;
                continue;
            }
            
            cout << "Received message:" << endl;
            cout << "  Message: " << mess.message << endl;
            cout << "  Tag: " << mess.tag << endl;
            cout << "  Sender: " << mess.sender << endl;
            
            if (mess.tag == "direct") {
                istringstream iss(mess.message);
                string cmd, topic;
                iss >> cmd;
                iss >> topic;
                
                if (cmd == "sub_to") {
                    subscription[topic].insert(mess.sender);
                    cout << mess.sender << " subscribed to " << topic << endl;
                }
                else if (cmd == "unsub_to") {
                    subscription[topic].erase(mess.sender);
                    cout << mess.sender << " unsubscribed from " << topic << endl;
                }
                else {
                    cerr << "Unknown command: " << cmd << endl;
                }
            }
            else if (!mess.tag.empty()) {
                auto it = subscription.find(mess.tag);
                if (it != subscription.end()) {
                    for (const string& subscriber : it->second) {
                        res = MessBus.openConnection(subscriber.c_str());
                        if (res.result) {
                            MessBus.sendMessage(mess);
                            MessBus.closeConnection();
                            cout << "Sent notification to " << subscriber << endl;
                        } else {
                            cerr << "Failed to publish to " << subscriber << ": " << res.message << endl;
                        }
                    }
                }
                cout << "Notification sent to " << subscription[mess.tag].size() << " subscribers" << endl;
            }
            else {
                cerr << "Message has no tag" << endl;
            }
        }
        this_thread::sleep_for(chrono::microseconds(100));
    }

    cout << "Shutting down adapter..." << endl;
    MessBus.deleteConnection();
    cout << "Adapter stopped" << endl;
    
    return 0;
}