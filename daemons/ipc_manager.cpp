#include <iostream>
#include <string>
#include <vector>
#include <map>
#include <cstring>
#include <cstdlib>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fstream>
#include <thread>
#include <chrono>
#include <filesystem>
#include <sstream>

namespace fs = std::filesystem;

struct DaemonInfo {
    std::string name;
    std::string executable;
    std::string pid_file;
    std::string default_args;
    int start_delay;
};

class IPCManager {
private:
    std::map<std::string, DaemonInfo> daemons;
    std::string build_dir;
    
    // Порядок запуска: adapter первый, затем logger, reader, writers
    std::vector<std::string> start_order = {"adapter", "logger", "reader", "writer1", "writer2"};
    // Порядок остановки: обратный
    std::vector<std::string> stop_order = {"writer2", "writer1", "reader", "logger", "adapter"};
    
    bool file_exists(const std::string& path) {
        std::ifstream f(path);
        return f.good();
    }
    
    bool process_exists(pid_t pid) {
        if (pid <= 0) return false;
        return (kill(pid, 0) == 0);
    }
    
    pid_t read_pid_from_file(const std::string& pid_file) {
        if (!file_exists(pid_file)) {
            return -1;
        }
        
        std::ifstream pidfile(pid_file);
        if (!pidfile.is_open()) {
            return -1;
        }
        
        pid_t pid;
        pidfile >> pid;
        pidfile.close();
        
        return pid;
    }
    
    void wait_for_pid_file(const std::string& pid_file, int max_wait_seconds = 5) {
        for (int i = 0; i < max_wait_seconds * 2; i++) {
            if (file_exists(pid_file)) {
                pid_t pid = read_pid_from_file(pid_file);
                if (pid > 0 && process_exists(pid)) {
                    return;
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(500));
        }
    }
    
public:
    IPCManager(const std::string& build_path = "./build") : build_dir(build_path) {
        // Adapter — центральный компонент, запускается первым
        daemons["adapter"] = {
            "Subscription Adapter",
            build_dir + "/adapter",
            "/tmp/adapter.pid",
            "",
            3
        };
        
        daemons["logger"] = {
            "Logger Daemon",
            build_dir + "/logger_daemon",
            "/tmp/logger_daemon.pid",
            "",
            3
        };
        
        daemons["reader"] = {
            "Reader Daemon", 
            build_dir + "/reader_daemon",
            "/tmp/reader_daemon.pid",
            "",
            3
        };
        
        daemons["writer1"] = {
            "Writer 1 Daemon",
            build_dir + "/writer1_daemon",
            "/tmp/writer1_daemon.pid",
            "--interval 3",
            2
        };
        
        daemons["writer2"] = {
            "Writer 2 Daemon",
            build_dir + "/writer2_daemon", 
            "/tmp/writer2_daemon.pid",
            "--interval 5",
            2
        };
        
        fs::create_directories("/tmp");
    }
    
    bool is_running(const std::string& daemon_name) {
        if (daemons.find(daemon_name) == daemons.end()) {
            return false;
        }
        
        pid_t pid = read_pid_from_file(daemons[daemon_name].pid_file);
        return process_exists(pid);
    }
    
    pid_t get_pid(const std::string& daemon_name) {
        if (daemons.find(daemon_name) == daemons.end()) {
            return -1;
        }
        
        return read_pid_from_file(daemons[daemon_name].pid_file);
    }
    
    bool start_daemon(const std::string& daemon_name, bool foreground = false) {
        if (daemons.find(daemon_name) == daemons.end()) {
            std::cerr << "Unknown daemon: " << daemon_name << std::endl;
            return false;
        }
        
        if (is_running(daemon_name)) {
            std::cout << daemon_name << " is already running (PID: " 
                      << get_pid(daemon_name) << ")" << std::endl;
            return true;
        }
        
        DaemonInfo& info = daemons[daemon_name];
        std::string cmd = info.executable;
        
        if (foreground) {
            cmd += " --foreground";
        } else {
            cmd += " --pid-file " + info.pid_file;
        }
        
        if (!info.default_args.empty()) {
            cmd += " " + info.default_args;
        }
        
        std::cout << "Starting " << info.name << "..." << std::endl;
        std::cout << "Command: " << cmd << std::endl;
        
        int result = system(cmd.c_str());
        
        if (result != 0 && !foreground) {
            std::cerr << "Command failed with code: " << result << std::endl;
            return false;
        }
        
        if (!foreground) {
            std::cout << "Waiting for daemon to start..." << std::endl;
            wait_for_pid_file(info.pid_file, info.start_delay);
            
            if (is_running(daemon_name)) {
                std::cout << info.name << " started successfully (PID: " 
                          << get_pid(daemon_name) << ")" << std::endl;
                return true;
            } else {
                std::cerr << "Failed to start " << info.name << std::endl;
                return false;
            }
        }
        
        return true;
    }
    
    bool stop_daemon(const std::string& daemon_name) {
        if (daemons.find(daemon_name) == daemons.end()) {
            std::cerr << "Unknown daemon: " << daemon_name << std::endl;
            return false;
        }
        
        if (!is_running(daemon_name)) {
            std::cout << daemon_name << " is not running" << std::endl;
            fs::remove(daemons[daemon_name].pid_file);
            return true;
        }
        
        pid_t pid = get_pid(daemon_name);
        DaemonInfo& info = daemons[daemon_name];
        
        std::cout << "Stopping " << info.name << " (PID: " << pid << ")..." << std::endl;
        
        if (kill(pid, SIGTERM) == 0) {
            for (int i = 0; i < 10; ++i) {
                if (!is_running(daemon_name)) {
                    std::cout << info.name << " stopped gracefully" << std::endl;
                    fs::remove(info.pid_file);
                    return true;
                }
                std::this_thread::sleep_for(std::chrono::seconds(1));
            }
            
            std::cout << "Force stopping " << info.name << "..." << std::endl;
            kill(pid, SIGKILL);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        fs::remove(info.pid_file);
        std::cout << info.name << " stopped" << std::endl;
        return true;
    }
    
    void show_status(bool detailed = false) {
        std::cout << "IPC Daemons Status\n";
        std::cout << "==================\n";
        
        for (const auto& name : start_order) {
            auto it = daemons.find(name);
            if (it == daemons.end()) continue;
            const DaemonInfo& info = it->second;
            
            if (is_running(name)) {
                pid_t pid = get_pid(name);
                std::cout << "✓ " << info.name << ": RUNNING (PID: " << pid << ")\n";
                
                if (detailed) {
                    std::string cmdline_file = "/proc/" + std::to_string(pid) + "/cmdline";
                    if (fs::exists(cmdline_file)) {
                        std::ifstream cmdline(cmdline_file);
                        std::string cmd;
                        if (std::getline(cmdline, cmd)) {
                            for (char& c : cmd) {
                                if (c == '\0') c = ' ';
                            }
                            std::cout << "    Command: " << cmd << "\n";
                        }
                    }
                    
                    std::string stat_file = "/proc/" + std::to_string(pid) + "/stat";
                    if (fs::exists(stat_file)) {
                        std::ifstream stat(stat_file);
                        std::string line;
                        if (std::getline(stat, line)) {
                            std::istringstream iss(line);
                            std::vector<std::string> tokens;
                            std::string token;
                            while (iss >> token) {
                                tokens.push_back(token);
                            }
                            if (tokens.size() > 21) {
                                long start_time = std::stol(tokens[21]);
                                std::ifstream uptime_file("/proc/uptime");
                                double uptime_seconds;
                                uptime_file >> uptime_seconds;
                                long uptime_ticks = uptime_seconds * sysconf(_SC_CLK_TCK);
                                long process_ticks = uptime_ticks - start_time;
                                double process_seconds = process_ticks / (double)sysconf(_SC_CLK_TCK);
                                
                                std::cout << "    Uptime: " << static_cast<int>(process_seconds) << " seconds\n";
                            }
                        }
                    }
                }
            } else {
                std::cout << "✗ " << info.name << ": STOPPED\n";
                
                if (detailed && fs::exists(info.pid_file)) {
                    std::cout << "    Warning: Stale PID file exists: " << info.pid_file << "\n";
                }
            }
        }
    }
    
    void start_all() {
        std::cout << "Starting all IPC daemons...\n";
        std::cout << "===========================\n";
        
        bool all_started = true;
        
        for (const auto& name : start_order) {
            if (!start_daemon(name)) {
                std::cerr << "Failed to start " << name << "!" << std::endl;
                all_started = false;
            }
            std::this_thread::sleep_for(std::chrono::seconds(daemons[name].start_delay));
        }
        
        if (all_started) {
            std::cout << "\nAll daemons started successfully!\n";
        } else {
            std::cout << "\nSome daemons failed to start!\n";
        }
        
        show_status();
    }
    
    void stop_all() {
        std::cout << "Stopping all IPC daemons...\n";
        std::cout << "==========================\n";
        
        for (const auto& name : stop_order) {
            stop_daemon(name);
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        // Очищаем shared memory
        std::cout << "\nCleaning up shared memory...\n";
        system("rm -f /dev/shm/*queue* 2>/dev/null");
        system("rm -f /dev/shm/adapter 2>/dev/null");
        
        std::cout << "\nAll daemons stopped!\n";
    }
    
    void show_help() {
        std::cout << "IPC Manager - Control IPC daemons with Subscription Adapter\n";
        std::cout << "Usage: ipc_manager <command> [daemon] [options]\n";
        std::cout << "\nCommands:\n";
        std::cout << "  start [daemon]    Start daemon (or all if no daemon specified)\n";
        std::cout << "  stop [daemon]     Stop daemon (or all if no daemon specified)\n";
        std::cout << "  restart <daemon>  Restart specific daemon\n";
        std::cout << "  status [-d]       Show status of all daemons (-d for details)\n";
        std::cout << "  cleanup           Clean up shared memory and PID files\n";
        std::cout << "  help              Show this help\n";
        std::cout << "\nDaemons: adapter, logger, reader, writer1, writer2, all\n";
        std::cout << "\nExamples:\n";
        std::cout << "  ipc_manager start           # Start all daemons\n";
        std::cout << "  ipc_manager stop             # Stop all daemons\n";
        std::cout << "  ipc_manager start adapter    # Start only adapter\n";
        std::cout << "  ipc_manager status           # Show status\n";
        std::cout << "  ipc_manager status -d        # Show detailed status\n";
    }
    
    void cleanup() {
        std::cout << "Cleaning up system...\n";
        
        stop_all();
        
        for (const auto& pair : daemons) {
            fs::remove(pair.second.pid_file);
        }
        
        system("rm -f /tmp/*_daemon*.pid 2>/dev/null");
        system("rm -f /tmp/adapter.pid 2>/dev/null");
        system("rm -f /tmp/*.err 2>/dev/null");
        system("rm -f /dev/shm/*queue* 2>/dev/null");
        system("rm -f /dev/shm/adapter 2>/dev/null");
        system("rm -f /dev/shm/sem.* 2>/dev/null");
        
        std::cout << "Cleanup completed!\n";
    }
};

int main(int argc, char* argv[]) {
    IPCManager manager;
    
    if (argc < 2) {
        manager.show_help();
        return 1;
    }
    
    std::string command = argv[1];
    
    if (command == "start") {
        if (argc >= 3) {
            std::string daemon = argv[2];
            if (daemon == "all") {
                manager.start_all();
            } else {
                manager.start_daemon(daemon);
            }
        } else {
            manager.start_all();
        }
    } else if (command == "stop") {
        if (argc >= 3) {
            std::string daemon = argv[2];
            if (daemon == "all") {
                manager.stop_all();
            } else {
                manager.stop_daemon(daemon);
            }
        } else {
            manager.stop_all();
        }
    } else if (command == "restart") {
        if (argc >= 3) {
            std::string daemon = argv[2];
            manager.stop_daemon(daemon);
            std::this_thread::sleep_for(std::chrono::seconds(2));
            manager.start_daemon(daemon);
        } else {
            std::cerr << "Please specify daemon to restart\n";
        }
    } else if (command == "status") {
        bool detailed = false;
        if (argc >= 3 && std::string(argv[2]) == "-d") {
            detailed = true;
        }
        manager.show_status(detailed);
    } else if (command == "cleanup") {
        manager.cleanup();
    } else if (command == "help" || command == "--help") {
        manager.show_help();
    } else {
        std::cout << "Unknown command: " << command << "\n";
        manager.show_help();
        return 1;
    }
    
    return 0;
}
