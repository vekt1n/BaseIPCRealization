#include "../SharedMemory/include/BaseMemory.hpp"
#include "../SharedMemory/include/Daemonizer.hpp"
#include <iostream>
#include <map>
#include <sstream>
#include <set>
#include <string.h>
#include <chrono>
#include <thread>
#include <atomic>
#include <signal.h>
#include <unistd.h>

using namespace std;

map<string, set<string>> subscription;
atomic<bool> running(true);

void signal_handler(int) {
    running = false;
}

int main(int argc, char* argv[]) {
    bool foreground_mode = false;
    string pid_file = "/tmp/adapter.pid";
    
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--foreground") == 0) {
            foreground_mode = true;
        } else if (strcmp(argv[i], "--pid-file") == 0 && i + 1 < argc) {
            pid_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            cout << "Subscription Adapter - message bus with pub/sub\n"
                 << "Usage: " << argv[0] << " [options]\n"
                 << "Options:\n"
                 << "  --foreground       Run in foreground (not as daemon)\n"
                 << "  --pid-file <file>  Set PID file path\n"
                 << "  --help             Show this help\n";
            return 0;
        }
    }
    
    // Демонизируем если нужно
    if (!foreground_mode) {
        if (!Daemonizer::daemonize(pid_file)) {
            cerr << "Failed to daemonize adapter!" << endl;
            return 1;
        }
    } else {
        cout << "Adapter running in foreground mode\n";
        Daemonizer::writePidFile(pid_file);
    }
    
    // Настраиваем обработчики сигналов
    Daemonizer::setupSignalHandlers();
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    BaseMemory MessBus("/adapter");
    Result res = MessBus.createConnection();
    if (!res.result) {
        cerr << "Failed to create adapter connection: " << res.message << endl;
        return 1;
    }
    
    if (foreground_mode) {
        cout << "Adapter started successfully (PID: " << getpid() << ")\n"
             << "Press Ctrl+C to exit\n";
    }

    while (running) {
        if (MessBus.hasMessage()) {
            Message mess;
            res = MessBus.getMessage(mess);
            
            if (!res.result) {
                if (foreground_mode) {
                    cerr << "Error getting message: " << res.message << endl;
                }
                continue;
            }
            
            if (foreground_mode) {
                cout << "Received [tag=" << mess.tag << "] [from=" 
                     << mess.sender << "]: " << mess.message << endl;
            }
            
            if (mess.tag == "direct") {
                // Команды подписки/отписки
                istringstream iss(mess.message);
                string cmd, topic;
                iss >> cmd;
                iss >> topic;
                
                if (cmd == "sub_to") {
                    subscription[topic].insert(mess.sender);
                    if (foreground_mode) {
                        cout << mess.sender << " subscribed to '" << topic << "'" << endl;
                    }
                }
                else if (cmd == "unsub_to") {
                    subscription[topic].erase(mess.sender);
                    if (foreground_mode) {
                        cout << mess.sender << " unsubscribed from '" << topic << "'" << endl;
                    }
                }
                else {
                    if (foreground_mode) {
                        cerr << "Unknown command: " << cmd << endl;
                    }
                }
            }
            else if (!mess.tag.empty()) {
                // Рассылка по подписчикам тега
                auto it = subscription.find(mess.tag);
                int count = 0;
                if (it != subscription.end()) {
                    for (const string& subscriber : it->second) {
                        res = MessBus.openConnection(subscriber.c_str());
                        if (res.result) {
                            MessBus.sendMessage(mess);
                            MessBus.closeConnection();
                            count++;
                            if (foreground_mode) {
                                cout << "  -> Sent to " << subscriber << endl;
                            }
                        } else {
                            if (foreground_mode) {
                                cerr << "  -> Failed to send to " << subscriber 
                                     << ": " << res.message << endl;
                            }
                        }
                    }
                }
                if (foreground_mode) {
                    cout << "Notified " << count << " subscriber(s) for tag '" 
                         << mess.tag << "'" << endl;
                }
            }
            else {
                if (foreground_mode) {
                    cerr << "Message has no tag, ignoring" << endl;
                }
            }
        }
        this_thread::sleep_for(chrono::microseconds(100));
    }

    if (foreground_mode) {
        cout << "Shutting down adapter..." << endl;
    }
    
    MessBus.deleteConnection();
    Daemonizer::cleanupPidFile(pid_file);
    
    if (foreground_mode) {
        cout << "Adapter stopped" << endl;
    }
    
    return 0;
}
