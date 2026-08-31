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

.PHONY: all test check_config_header test_checksum test_crc test_error_injection clean

all: $(OBJECTS)

test: check_config_header test_checksum test_crc test_error_injection

check_config_header: $(CONFIG_TEST)
	./$(CONFIG_TEST)

test_checksum: $(CHECKSUM_TEST)
	./$(CHECKSUM_TEST)

test_crc: $(CRC_TEST)
	./$(CRC_TEST)

test_error_injection: $(ERROR_INJECTION_TEST)
	./$(ERROR_INJECTION_TEST)

$(CHECKSUM_TEST): $(CHECKSUM_TEST_SOURCES) include/checksum.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CHECKSUM_TEST_SOURCES) -o $@

$(CONFIG_TEST): $(CONFIG_TEST_SOURCES) include/config.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CONFIG_TEST_SOURCES) -o $@

$(CRC_TEST): $(CRC_TEST_SOURCES) include/crc.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CRC_TEST_SOURCES) -o $@

$(ERROR_INJECTION_TEST): $(ERROR_INJECTION_TEST_SOURCES) include/error_injection.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(ERROR_INJECTION_TEST_SOURCES) -o $@

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -f $(OBJECTS) $(CHECKSUM_TEST) $(CONFIG_TEST) $(CRC_TEST) $(ERROR_INJECTION_TEST)
