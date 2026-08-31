CXX := g++
CPPFLAGS := -Iinclude
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Werror

BUILD_DIR := build
SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

CHECKSUM_TEST := $(BUILD_DIR)/test_checksum
CHECKSUM_TEST_SOURCES := src/checksum.cpp tests/test_checksum.cpp
CONFIG_TEST := $(BUILD_DIR)/test_config
CONFIG_TEST_SOURCES := tests/test_config.cpp
CRC_TEST := $(BUILD_DIR)/test_crc
CRC_TEST_SOURCES := src/crc.cpp tests/test_crc.cpp
ERROR_INJECTION_TEST := $(BUILD_DIR)/test_error_injection
ERROR_INJECTION_TEST_SOURCES := src/error_injection.cpp tests/test_error_injection.cpp
FRAME_TEST := $(BUILD_DIR)/test_frame
FRAME_TEST_SOURCES := src/checksum.cpp src/crc.cpp src/frame.cpp tests/test_frame.cpp
RECORD_TEST := $(BUILD_DIR)/test_record
RECORD_TEST_SOURCES := src/record.cpp tests/test_record.cpp
CHANNEL_TEST := $(BUILD_DIR)/test_channel
CHANNEL_TEST_SOURCES := src/error_injection.cpp src/channel.cpp tests/test_channel.cpp
TIMER_TEST := $(BUILD_DIR)/test_timer
TIMER_TEST_SOURCES := src/timer.cpp tests/test_timer.cpp
METRICS_TEST := $(BUILD_DIR)/test_metrics
METRICS_TEST_SOURCES := src/metrics.cpp tests/test_metrics.cpp
SOCKET_TEST := $(BUILD_DIR)/test_socket
SOCKET_TEST_SOURCES := src/record.cpp src/socket.cpp tests/test_socket.cpp
STOP_AND_WAIT_TEST := $(BUILD_DIR)/test_stop_and_wait
STOP_AND_WAIT_TEST_SOURCES := src/stop_and_wait.cpp tests/test_stop_and_wait.cpp
GO_BACK_N_TEST := $(BUILD_DIR)/test_go_back_n
GO_BACK_N_TEST_SOURCES := src/go_back_n.cpp tests/test_go_back_n.cpp
SELECTIVE_REPEAT_TEST := $(BUILD_DIR)/test_selective_repeat
SELECTIVE_REPEAT_TEST_SOURCES := src/selective_repeat.cpp tests/test_selective_repeat.cpp

.PHONY: all test check_config_header test_checksum test_crc test_error_injection test_frame test_record test_channel test_timer test_metrics test_socket test_stop_and_wait test_go_back_n test_selective_repeat clean

all: $(OBJECTS)

test: check_config_header test_checksum test_crc test_error_injection test_frame test_record test_channel test_timer test_metrics test_socket test_stop_and_wait test_go_back_n test_selective_repeat

check_config_header: $(CONFIG_TEST)
	./$(CONFIG_TEST)

test_checksum: $(CHECKSUM_TEST)
	./$(CHECKSUM_TEST)

test_crc: $(CRC_TEST)
	./$(CRC_TEST)

test_error_injection: $(ERROR_INJECTION_TEST)
	./$(ERROR_INJECTION_TEST)

test_frame: $(FRAME_TEST)
	./$(FRAME_TEST)

test_record: $(RECORD_TEST)
	./$(RECORD_TEST)

test_channel: $(CHANNEL_TEST)
	./$(CHANNEL_TEST)

test_timer: $(TIMER_TEST)
	./$(TIMER_TEST)

test_metrics: $(METRICS_TEST)
	./$(METRICS_TEST)

test_socket: $(SOCKET_TEST)
	./$(SOCKET_TEST)

test_stop_and_wait: $(STOP_AND_WAIT_TEST)
	./$(STOP_AND_WAIT_TEST)

test_go_back_n: $(GO_BACK_N_TEST)
	./$(GO_BACK_N_TEST)

test_selective_repeat: $(SELECTIVE_REPEAT_TEST)
	./$(SELECTIVE_REPEAT_TEST)

$(CHECKSUM_TEST): $(CHECKSUM_TEST_SOURCES) include/checksum.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CHECKSUM_TEST_SOURCES) -o $@

$(CONFIG_TEST): $(CONFIG_TEST_SOURCES) include/config.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CONFIG_TEST_SOURCES) -o $@

$(CRC_TEST): $(CRC_TEST_SOURCES) include/crc.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CRC_TEST_SOURCES) -o $@

$(ERROR_INJECTION_TEST): $(ERROR_INJECTION_TEST_SOURCES) include/error_injection.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ERROR_INJECTION_TEST_SOURCES) -o $@

$(FRAME_TEST): $(FRAME_TEST_SOURCES) include/frame.hpp include/config.hpp include/checksum.hpp include/crc.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(FRAME_TEST_SOURCES) -o $@

$(RECORD_TEST): $(RECORD_TEST_SOURCES) include/record.hpp include/frame.hpp include/config.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(RECORD_TEST_SOURCES) -o $@

$(CHANNEL_TEST): $(CHANNEL_TEST_SOURCES) include/channel.hpp include/error_injection.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CHANNEL_TEST_SOURCES) -o $@

$(TIMER_TEST): $(TIMER_TEST_SOURCES) include/timer.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(TIMER_TEST_SOURCES) -o $@

$(METRICS_TEST): $(METRICS_TEST_SOURCES) include/metrics.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(METRICS_TEST_SOURCES) -o $@

$(SOCKET_TEST): $(SOCKET_TEST_SOURCES) include/socket.hpp include/record.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOCKET_TEST_SOURCES) -o $@

$(STOP_AND_WAIT_TEST): $(STOP_AND_WAIT_TEST_SOURCES) include/stop_and_wait.hpp include/protocol.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(STOP_AND_WAIT_TEST_SOURCES) -o $@

$(GO_BACK_N_TEST): $(GO_BACK_N_TEST_SOURCES) include/go_back_n.hpp include/protocol.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(GO_BACK_N_TEST_SOURCES) -o $@

$(SELECTIVE_REPEAT_TEST): $(SELECTIVE_REPEAT_TEST_SOURCES) include/selective_repeat.hpp include/protocol.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SELECTIVE_REPEAT_TEST_SOURCES) -o $@

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -f $(OBJECTS) $(CHECKSUM_TEST) $(CONFIG_TEST) $(CRC_TEST) $(ERROR_INJECTION_TEST) $(FRAME_TEST) $(RECORD_TEST) $(CHANNEL_TEST) $(TIMER_TEST) $(METRICS_TEST) $(SOCKET_TEST) $(STOP_AND_WAIT_TEST) $(GO_BACK_N_TEST) $(SELECTIVE_REPEAT_TEST)
