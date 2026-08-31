CXX := g++
CPPFLAGS := -Iinclude
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Werror

BUILD_DIR := build
SOURCES := $(wildcard src/*.cpp)
OBJECTS := $(patsubst src/%.cpp,$(BUILD_DIR)/%.o,$(SOURCES))

CHECKSUM_TEST := $(BUILD_DIR)/test_checksum
CHECKSUM_TEST_SOURCES := src/checksum.cpp tests/test_checksum.cpp

.PHONY: all test_checksum clean

all: $(OBJECTS)

test_checksum: $(CHECKSUM_TEST)
	./$(CHECKSUM_TEST)

$(CHECKSUM_TEST): $(CHECKSUM_TEST_SOURCES) include/checksum.hpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(CHECKSUM_TEST_SOURCES) -o $@

$(BUILD_DIR)/%.o: src/%.cpp | $(BUILD_DIR)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR):
	mkdir -p $@

clean:
	rm -f $(OBJECTS) $(CHECKSUM_TEST)
