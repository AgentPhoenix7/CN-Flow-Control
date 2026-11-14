CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Werror -Iinclude
BUILD_DIR := build
SRC_DIR := src

COMMON_SRCS := $(SRC_DIR)/crc32.cpp $(SRC_DIR)/frame.cpp $(SRC_DIR)/channel.cpp \
               $(SRC_DIR)/socket.cpp $(SRC_DIR)/wire.cpp $(SRC_DIR)/timer.cpp \
               $(SRC_DIR)/stop_and_wait.cpp $(SRC_DIR)/go_back_n.cpp $(SRC_DIR)/selective_repeat.cpp
COMMON_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(COMMON_SRCS))

.PHONY: all clean test test-integration

all: $(BUILD_DIR)/sender $(BUILD_DIR)/receiver

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -MMD -MP -c $< -o $@

-include $(wildcard $(BUILD_DIR)/*.d)

$(BUILD_DIR)/frame_test: tests/frame_test.cpp $(BUILD_DIR)/frame.o $(BUILD_DIR)/crc32.o
	$(CXX) $(CXXFLAGS) $^ -o $@

test: $(BUILD_DIR)/frame_test
	./$(BUILD_DIR)/frame_test

test-integration: all
	python3 tests/test_transfers.py

$(BUILD_DIR)/sender: $(COMMON_OBJS) $(BUILD_DIR)/sender_main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR)/receiver: $(COMMON_OBJS) $(BUILD_DIR)/receiver_main.o
	$(CXX) $(CXXFLAGS) $^ -o $@

$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

clean:
	rm -rf $(BUILD_DIR)

.PHONY: generate-inputs experiments plots report results

generate-inputs:
	python3 tools/generate_inputs.py

experiments: all generate-inputs
	python3 tools/run_experiments.py

plots:
	python3 tools/plot_results.py

report:
	python3 tools/generate_report_tables.py
	set -e; for f in results/plots/*.svg; do \
		if command -v inkscape >/dev/null 2>&1; then \
			inkscape --export-type=pdf --export-filename="report/figures/$$(basename "$$f" .svg).pdf" "$$f"; \
		else \
			rsvg-convert -f pdf -o "report/figures/$$(basename "$$f" .svg).pdf" "$$f"; \
		fi; \
	done
	cd report && pdflatex -interaction=nonstopmode report.tex && pdflatex -interaction=nonstopmode report.tex

results: experiments plots report
