CXX = g++
CXXFLAGS = -std=c++20 -Wall -Wextra -pthread -O2
LDFLAGS = -lrt -lpthread

SRC_DIR = ./SharedMemory/src
BUILD_DIR = ./example/build
INCLUDE_DIR = ./SharedMemory/include

READER_SRC = ./example/example_reader.cpp
WRITER1_SRC = ./example/example_writer.cpp
WRITER2_SRC = ./example/example_writer2.cpp 
SHAREDMEM_SRC = $(SRC_DIR)/BaseMemory.cpp

all: reader writer1 writer2

reader: $(READER_SRC) $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $(READER_SRC) $(SHAREDMEM_SRC) -o $(BUILD_DIR)/reader $(LDFLAGS)

writer1: $(WRITER1_SRC) $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $(WRITER1_SRC) $(SHAREDMEM_SRC) -o $(BUILD_DIR)/writer1 $(LDFLAGS)

writer2: $(WRITER2_SRC) $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -I$(INCLUDE_DIR) $(WRITER2_SRC) $(SHAREDMEM_SRC) -o $(BUILD_DIR)/writer2 $(LDFLAGS)

clean:
	rm -f $(BUILD_DIR)/reader $(BUILD_DIR)/writer1 $(BUILD_DIR)/writer2

free_mem:
	rm /dev/shm/reader_queue  || true
	rm /dev/shm/writer_queue1  || true
	rm /dev/shm/writer_queue2 || true

run_reader:
	$(BUILD_DIR)/reader

run_writer1:
	$(BUILD_DIR)/writer1

run_writer2:
	$(BUILD_DIR)/writer2

.PHONY: all clean reader writer