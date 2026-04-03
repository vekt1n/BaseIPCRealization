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
#include <fstream>

using namespace std;

map<string, set<string>> subscription;
atomic<bool> running(true);

void signal_handler(int) {
    running = false;
}

// Simple JSON serializer for map<string, set<string>>
bool saveSubscriptions(string subscriptions_file) {
    ofstream file(subscriptions_file);
    if (!file.is_open()) {
        cerr << "Failed to open subscriptions file for writing: " << subscriptions_file << endl;
        return false;
    }

    file << "{\n";
    bool first_topic = true;
    for (const auto& [topic, subscribers] : subscription) {
        if (!first_topic) file << ",\n";
        first_topic = false;
        file << "  \"" << topic << "\": [";
        bool first_sub = true;
        for (const string& sub : subscribers) {
            if (!first_sub) file << ", ";
            first_sub = false;
            file << "\"" << sub << "\"";
        }
        file << "]";
    }
    file << "\n}\n";
    return true;
}

// Simple JSON deserializer for the format above
void loadSubscriptions(string subscriptions_file) {
    ifstream file(subscriptions_file);
    if (!file.is_open()) return;  // file doesn't exist – start empty

    string content((istreambuf_iterator<char>(file)), istreambuf_iterator<char>());
    file.close();

    subscription.clear();
    // Very naive parser – assumes correct JSON format:
    // { "topic1": ["sub1", "sub2"], "topic2": [] }
    size_t pos = 0;
    while ((pos = content.find('"', pos)) != string::npos) {
        size_t start = pos + 1;
        size_t end = content.find('"', start);
        if (end == string::npos) break;
        string topic = content.substr(start, end - start);
        pos = end + 1;

        // find opening '[' after colon
        size_t colon = content.find(':', pos);
        if (colon == string::npos) break;
        size_t bracket = content.find('[', colon);
        if (bracket == string::npos) break;
        size_t close_bracket = content.find(']', bracket);
        if (close_bracket == string::npos) break;

        string subs_str = content.substr(bracket + 1, close_bracket - bracket - 1);
        set<string> subs;
        size_t sub_pos = 0;
        while ((sub_pos = subs_str.find('"', sub_pos)) != string::npos) {
            size_t sub_start = sub_pos + 1;
            size_t sub_end = subs_str.find('"', sub_start);
            if (sub_end == string::npos) break;
            string subscriber = subs_str.substr(sub_start, sub_end - sub_start);
            subs.insert(subscriber);
            sub_pos = sub_end + 1;
        }
        subscription[topic] = subs;
        pos = close_bracket + 1;
    }
    cout << "Loaded " << subscription.size() << " topics from " << subscriptions_file << endl;
}

int main(int argc, char* argv[]) {
    bool foreground_mode = false;
    string pid_file = "/tmp/adapter.pid";
    string subs_file = "./adapter_subscriptions.json";
    
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--foreground") == 0) {
            foreground_mode = true;
        } else if (strcmp(argv[i], "--pid-file") == 0 && i + 1 < argc) {
            pid_file = argv[++i];
        } else if (strcmp(argv[i], "--sub-file") == 0 && i + 1 < argc) {
            subs_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            cout << "Subscription Adapter - message bus with pub/sub\n"
                 << "Usage: " << argv[0] << " [options]\n"
                 << "Options:\n"
                 << "  --foreground       Run in foreground (not as daemon)\n"
                 << "  --pid-file <file>  Set PID file path\n"
                 << "  --sub-file <file>  Set subscriptions JSON file path\n"
                 << "  --help             Show this help\n";
            return 0;
        }
    }

    if (subs_file[0] != '/') {
        char cwd[4096];
        if (getcwd(cwd, sizeof(cwd))) {
            subs_file = std::string(cwd) + "/" + subs_file;
        }
    }
    
    if (!foreground_mode) {
        if (!Daemonizer::daemonize(pid_file)) {
            cerr << "Failed to daemonize adapter!" << endl;
            return 1;
        }
    } else {
        cout << "Adapter running in foreground mode\n";
        Daemonizer::writePidFile(pid_file);
    }
    
    Daemonizer::setupSignalHandlers();
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    BaseMemory MessBus("/adapter");
    Result res = MessBus.createConnection();
    if (!res.result) {
        cerr << "Failed to create adapter connection: " << res.message << endl;
        return 1;
    }
    
    loadSubscriptions(subs_file);  // load saved state
    
    if (foreground_mode) {
        cout << "Adapter started successfully (PID: " << getpid() << ")\n"
             << "Press Ctrl+C to exit\n";
    }

    while (running) {
        if (MessBus.hasMessage()) {
            Message mess;
            res = MessBus.getMessage(mess);
            
            if (!res.result) {
                if (foreground_mode) cerr << "Error getting message: " << res.message << endl;
                continue;
            }
            
            if (foreground_mode) {
                cout << "Received [tag=" << mess.tag << "] [from=" 
                     << mess.sender << "]: " << mess.message << endl;
            }
            
            if (mess.tag == "direct") {
                istringstream iss(mess.message);
                string cmd, topic;
                iss >> cmd >> topic;
                
                if (cmd == "sub_to") {
                    subscription[topic].insert(mess.sender);
                    saveSubscriptions(subs_file);
                    if (foreground_mode) 
                        cout << mess.sender << " subscribed to '" << topic << "'" << endl;
                }
                else if (cmd == "unsub_to") {
                    auto it = subscription.find(topic);
                    if (it != subscription.end()) {
                        it->second.erase(mess.sender);
                        if (it->second.empty()) subscription.erase(it);
                        saveSubscriptions(subs_file);
                    }
                    if (foreground_mode) 
                        cout << mess.sender << " unsubscribed from '" << topic << "'" << endl;
                }
                else if (foreground_mode) {
                    cerr << "Unknown command: " << cmd << endl;
                }
            }
            else if (!mess.tag.empty()) {
                auto it = subscription.find(mess.tag);
                int count = 0;
                if (it != subscription.end()) {
                    for (const string& subscriber : it->second) {
                        res = MessBus.openConnection(subscriber.c_str());
                        if (res.result) {
                            MessBus.sendMessage(mess);
                            MessBus.closeConnection();
                            count++;
                            if (foreground_mode) 
                                cout << "  -> Sent to " << subscriber << endl;
                        } else if (foreground_mode) {
                            cerr << "  -> Failed to send to " << subscriber 
                                 << ": " << res.message << endl;
                        }
                    }
                }
                if (foreground_mode) 
                    cout << "Notified " << count << " subscriber(s) for tag '" 
                         << mess.tag << "'" << endl;
            }
            else if (foreground_mode) {
                cerr << "Message has no tag, ignoring" << endl;
            }
        }
        this_thread::sleep_for(chrono::microseconds(100));
    }

    if (foreground_mode) cout << "Shutting down adapter..." << endl;
    
    saveSubscriptions(subs_file);   // final save
    MessBus.deleteConnection();
    Daemonizer::cleanupPidFile(pid_file);
    
    if (foreground_mode) cout << "Adapter stopped" << endl;
    return 0;
}