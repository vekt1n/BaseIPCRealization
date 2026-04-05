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

std::map <std::string, std::set <std::string>> subscription;
std::atomic<bool> running(true);

void signal_handler(int) {
    running = false;
}

// Simple JSON serializer for map<string, set<string>>
bool saveSubscriptions(std::string subscriptions_file) {
    std::ofstream file(subscriptions_file);
    if (!file.is_open()) {
        std::cerr << "Failed to open subscriptions file for writing: " << subscriptions_file << std::endl;
        return false;
    }

    file << "{\n";
    bool first_topic = true;
    for (const auto& [topic, subscribers] : subscription) {
        if (!first_topic) file << ",\n";
        first_topic = false;
        file << "  \"" << topic << "\": [";
        bool first_sub = true;
        for (const std::string& sub : subscribers) {
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
void loadSubscriptions(std::string subscriptions_file) {
    std::ifstream file(subscriptions_file);
    if (!file.is_open()) return;  // file doesn't exist – start empty

    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();

    subscription.clear();
    // Very naive parser – assumes correct JSON format:
    // { "topic1": ["sub1", "sub2"], "topic2": [] }
    size_t pos = 0;
    while ((pos = content.find('"', pos)) != std::string::npos) {
        size_t start = pos + 1;
        size_t end = content.find('"', start);
        if (end == std::string::npos) break;
        std::string topic = content.substr(start, end - start);
        pos = end + 1;

        // find opening '[' after colon
        size_t colon = content.find(':', pos);
        if (colon == std::string::npos) break;
        size_t bracket = content.find('[', colon);
        if (bracket == std::string::npos) break;
        size_t close_bracket = content.find(']', bracket);
        if (close_bracket == std::string::npos) break;

        std::string subs_str = content.substr(bracket + 1, close_bracket - bracket - 1);
        std::set <std::string> subs;
        size_t sub_pos = 0;
        while ((sub_pos = subs_str.find('"', sub_pos)) != std::string::npos) {
            size_t sub_start = sub_pos + 1;
            size_t sub_end = subs_str.find('"', sub_start);
            if (sub_end == std::string::npos) break;
            std::string subscriber = subs_str.substr(sub_start, sub_end - sub_start);
            subs.insert(subscriber);
            sub_pos = sub_end + 1;
        }
        subscription[topic] = subs;
        pos = close_bracket + 1;
    }
    std::cout << "Loaded " << subscription.size() << " topics from " << subscriptions_file << std::endl;
}

int main(int argc, char* argv[]) {
    bool foreground_mode = false;
    std::string pid_file = "/tmp/adapter.pid";
    std::string subs_file = "./subscriptions_MessBus.json";
    
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--foreground") == 0) {
            foreground_mode = true;
        } else if (strcmp(argv[i], "--pid-file") == 0 && i + 1 < argc) {
            pid_file = argv[++i];
        } else if (strcmp(argv[i], "--sub-file") == 0 && i + 1 < argc) {
            subs_file = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0) {
            std::cout << "Subscription Adapter - message bus with pub/sub\n"
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
            std::cerr << "Failed to daemonize adapter!" << std::endl;
            return 1;
        }
    } else {
        std::cout << "Adapter running in foreground mode\n" << std::endl;
        Daemonizer::writePidFile(pid_file);
    }
    
    Daemonizer::setupSignalHandlers();
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    BaseMemory MessBus("/subscrribtion_queue");
    Result res = MessBus.createConnection();
    if (!res.result) {
        std::cerr << "Failed to create adapter connection: " << res.message << std::endl;
        return 1;
    }
    
    loadSubscriptions(subs_file);  // load saved state
    
    if (foreground_mode) {
        std::cout << "Adapter started successfully (PID: " << getpid() << ")\n"
             << "Press Ctrl+C to exit\n";
    }

    while (running) {
        if (MessBus.hasMessage()) {
            Message mess;
            res = MessBus.getMessage(mess);
            
            if (!res.result) {
                if (foreground_mode) {
                    std::cerr << "Error getting message: " << res.message << std::endl;
                }
                continue;
            }
            
            if (foreground_mode) {
                std::cout << "Received [tag=" << mess.tag << "] [from=" 
                     << mess.sender << "]: " << mess.message << std::endl;
            }
            
            if (mess.tag == "direct") {
                std::istringstream iss(mess.message);
                std::string cmd, topic;
                iss >> cmd >> topic;
                
                if (cmd == "sub_to") {
                    subscription[topic].insert(mess.sender);
                    saveSubscriptions(subs_file);
                    if (foreground_mode) 
                        std::cout << mess.sender << " subscribed to '" << topic << "'" << std::endl;
                }
                else if (cmd == "unsub_to") {
                    auto it = subscription.find(topic);
                    if (it != subscription.end()) {
                        it->second.erase(mess.sender);
                        if (it->second.empty()) subscription.erase(it);
                        saveSubscriptions(subs_file);
                    }
                    if (foreground_mode) 
                        std::cout << mess.sender << " unsubscribed from '" << topic << "'" << std::endl;
                }
                else if (foreground_mode) {
                    std::cerr << "Unknown command: " << cmd << std::endl;
                }
            }
            else if (mess.tag == "info" || mess.tag == "debug" || mess.tag == "log" ||
                     mess.tag == "error" || mess.tag == "fatal" || mess.tag == "warn") {
                res = MessBus.openConnection("/logger_daemon_queue");
                if (res.result) {
                    MessBus.sendMessage(mess);
                    MessBus.closeConnection();
                    if (foreground_mode) 
                        std::cout << "  -> Sent to /logger_daemon_queue" << std::endl;
                } else if (foreground_mode) {
                    std::cerr << "  -> Failed to send to /logger_daemon_queue "
                            << ": " << res.message << std::endl;
                }
            }
            else if (!mess.tag.empty()) {
                auto it = subscription.find(mess.tag);
                int count = 0;
                if (it != subscription.end()) {
                    for (const std::string& subscriber : it->second) {
                        res = MessBus.openConnection(subscriber.c_str());
                        if (res.result) {
                            MessBus.sendMessage(mess);
                            MessBus.closeConnection();
                            count++;
                            if (foreground_mode) 
                                std::cout << "  -> Sent to " << subscriber << std::endl;
                        } else if (foreground_mode) {
                            std::cerr << "  -> Failed to send to " << subscriber 
                                 << ": " << res.message << std::endl;
                        }
                    }
                    std::string tmpMess = "Notified " + std::to_string(count) + 
                                            " subscriber(s) for tag '" + mess.tag + "'";
                    res = MessBus.sendMessage("/logger_daemon_queue", tmpMess);
                    if (res.result) {
                        if (foreground_mode) {
                            std::cout << "  -> Sent to /logger_daemon_queue" << std::endl;
                        }
                    } 
                    else if (foreground_mode) {
                        std::cerr << "  -> Failed to send to /logger_daemon_queue "
                                << ": " << res.message << std::endl;
                    }
                }
                if (foreground_mode) 
                    std::cout << "Notified " << count << " subscriber(s) for tag '" 
                         << mess.tag << "'" << std::endl;
            }
            else if (foreground_mode) {
                std::cerr << "Message has no tag, ignoring" << std::endl;
            }
        }
        std::this_thread::sleep_for(std::chrono::microseconds(100));
    }

    if (foreground_mode) std::cout << "Shutting down adapter..." << std::endl;
    
    saveSubscriptions(subs_file);   // final save
    MessBus.deleteConnection();
    Daemonizer::cleanupPidFile(pid_file);
    
    if (foreground_mode) std::cout << "Adapter stopped" << std::endl;
    return 0;
}