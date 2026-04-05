CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -pthread -O2
LDFLAGS = -lrt -lpthread

SRC_DIR = ./SharedMemory/src
BUILD_DIR = ./build
INCLUDE_DIR = ./SharedMemory/include
DAEMONS_DIR = ./daemons

# Исходные файлы
SHAREDMEM_SRC = $(SRC_DIR)/BaseMemory.cpp
DAEMONIZER_SRC = $(SRC_DIR)/Daemonizer.cpp
LOGGER_SRC = $(SRC_DIR)/Logger.cpp

# Демоны
ADAPTER_SRC = ./SubscriptionAdapter/Adapter.cpp
READER_DAEMON_SRC = $(DAEMONS_DIR)/reader_daemon.cpp
WRITER1_DAEMON_SRC = $(DAEMONS_DIR)/writer1_daemon.cpp
WRITER2_DAEMON_SRC = $(DAEMONS_DIR)/writer2_daemon.cpp
LOGGER_DAEMON_SRC = $(DAEMONS_DIR)/logger_daemon.cpp

# Примеры (простые - прямой IPC)
EXAMPLE_READER_SRC = ./example/example_reader.cpp
EXAMPLE_WRITER1_SRC = ./example/example_writer.cpp
EXAMPLE_WRITER2_SRC = ./example/example_writer2.cpp

# Примеры с подписками
EXAMPLE_SUB_DIR = ./example_subscriptions

all: daemons examples examples_sub tools

# Создаем build директорию
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

# ========================
# Демоны (с Daemonizer)
# ========================
daemons: $(BUILD_DIR) adapter reader_daemon writer1_daemon writer2_daemon logger_daemon

adapter: $(ADAPTER_SRC) $(SHAREDMEM_SRC) $(DAEMONIZER_SRC)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) \
		$(ADAPTER_SRC) $(SHAREDMEM_SRC) $(DAEMONIZER_SRC) \
		-o $(BUILD_DIR)/adapter $(LDFLAGS)

reader_daemon: $(READER_DAEMON_SRC) $(SHAREDMEM_SRC) $(DAEMONIZER_SRC) $(LOGGER_SRC)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) \
		$(READER_DAEMON_SRC) $(SHAREDMEM_SRC) $(DAEMONIZER_SRC) $(LOGGER_SRC) \
		-o $(BUILD_DIR)/reader_daemon $(LDFLAGS)

writer1_daemon: $(WRITER1_DAEMON_SRC) $(SHAREDMEM_SRC) $(DAEMONIZER_SRC) $(LOGGER_SRC)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) \
		$(WRITER1_DAEMON_SRC) $(SHAREDMEM_SRC) $(DAEMONIZER_SRC) $(LOGGER_SRC) \
		-o $(BUILD_DIR)/writer1_daemon $(LDFLAGS)

writer2_daemon: $(WRITER2_DAEMON_SRC) $(SHAREDMEM_SRC) $(DAEMONIZER_SRC) $(LOGGER_SRC)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) \
		$(WRITER2_DAEMON_SRC) $(SHAREDMEM_SRC) $(DAEMONIZER_SRC) $(LOGGER_SRC) \
		-o $(BUILD_DIR)/writer2_daemon $(LDFLAGS)

logger_daemon: $(LOGGER_DAEMON_SRC) $(SHAREDMEM_SRC) $(DAEMONIZER_SRC) $(LOGGER_SRC)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) \
		$(LOGGER_DAEMON_SRC) $(SHAREDMEM_SRC) $(DAEMONIZER_SRC) $(LOGGER_SRC) \
		-o $(BUILD_DIR)/logger_daemon $(LDFLAGS)

# ========================
# Примеры (без демонизации)
# ========================
examples: $(BUILD_DIR) reader_example writer1_example writer2_example

reader_example: $(EXAMPLE_READER_SRC) $(SHAREDMEM_SRC)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $(EXAMPLE_READER_SRC) $(SHAREDMEM_SRC) \
		-o $(BUILD_DIR)/reader_example $(LDFLAGS)

writer1_example: $(EXAMPLE_WRITER1_SRC) $(SHAREDMEM_SRC)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $(EXAMPLE_WRITER1_SRC) $(SHAREDMEM_SRC) \
		-o $(BUILD_DIR)/writer1_example $(LDFLAGS)

writer2_example: $(EXAMPLE_WRITER2_SRC) $(SHAREDMEM_SRC)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $(EXAMPLE_WRITER2_SRC) $(SHAREDMEM_SRC) \
		-o $(BUILD_DIR)/writer2_example $(LDFLAGS)

# ========================
# Примеры с подписками
# ========================
examples_sub: $(BUILD_DIR) sub_writer1 sub_writer2 sub_writer3

sub_writer1: $(EXAMPLE_SUB_DIR)/writer1.cpp $(SHAREDMEM_SRC)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $(EXAMPLE_SUB_DIR)/writer1.cpp $(SHAREDMEM_SRC) \
		-o $(BUILD_DIR)/sub_writer1 $(LDFLAGS)

sub_writer2: $(EXAMPLE_SUB_DIR)/writer2.cpp $(SHAREDMEM_SRC)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $(EXAMPLE_SUB_DIR)/writer2.cpp $(SHAREDMEM_SRC) \
		-o $(BUILD_DIR)/sub_writer2 $(LDFLAGS)

sub_writer3: $(EXAMPLE_SUB_DIR)/writer3.cpp $(SHAREDMEM_SRC)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $(EXAMPLE_SUB_DIR)/writer3.cpp $(SHAREDMEM_SRC) \
		-o $(BUILD_DIR)/sub_writer3 $(LDFLAGS)

# ========================
# Утилиты
# ========================
tools: ipc_manager

ipc_manager: $(DAEMONS_DIR)/ipc_manager.cpp
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $(DAEMONS_DIR)/ipc_manager.cpp \
		-o $(BUILD_DIR)/ipc_manager $(LDFLAGS)

# ========================
# Очистка
# ========================
clean:
	rm -rf $(BUILD_DIR)
	rm -f /var/run/*_daemon.pid 2>/dev/null || true

free_mem:
	rm -f /dev/shm/reader_queue 2>/dev/null || true
	rm -f /dev/shm/writer_queue1 2>/dev/null || true
	rm -f /dev/shm/writer_queue2 2>/dev/null || true
	rm -f /dev/shm/logger_queue 2>/dev/null || true
	rm -f /dev/shm/adapter 2>/dev/null || true

clean_all: clean free_mem

# ========================
# Запуск в foreground (для отладки)
# ========================
run_adapter_fg: ./build/adapter
	$(BUILD_DIR)/adapter --foreground

run_reader_daemon_fg: ./build/reader_daemon
	$(BUILD_DIR)/reader_daemon --foreground

run_writer1_daemon_fg: ./build/writer1_daemon
	$(BUILD_DIR)/writer1_daemon --foreground

run_writer2_daemon_fg: ./build/writer2_daemon
	$(BUILD_DIR)/writer2_daemon --foreground

run_logger_daemon_fg: ./build/logger_daemon
	$(BUILD_DIR)/logger_daemon --foreground --log-file ./test.log

# Запуск примеров
run_reader_example: ./build/reader_example
	$(BUILD_DIR)/reader_example

run_writer1_example: ./build/writer1_example
	$(BUILD_DIR)/writer1_example

run_writer2_example: ./build/writer2_example
	$(BUILD_DIR)/writer2_example

# Установка
install_dirs:
	mkdir -p /var/log
	mkdir -p /var/run
	touch /var/log/shared_memory.log
	chmod 666 /var/log/shared_memory.log

.PHONY: all daemons examples examples_sub tools clean free_mem install_dirs clean_all \
        run_adapter_fg run_reader_daemon_fg run_writer1_daemon_fg run_writer2_daemon_fg run_logger_daemon_fg \
        run_reader_example run_writer1_example run_writer2_example
