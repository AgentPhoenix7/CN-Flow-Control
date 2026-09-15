CXX := g++
CXXFLAGS := -std=c++17 -Wall -Wextra -Wpedantic -Werror -Iinclude
BUILD_DIR := build
SRC_DIR := src

COMMON_SRCS := $(SRC_DIR)/crc16.cpp $(SRC_DIR)/frame.cpp $(SRC_DIR)/channel.cpp \
               $(SRC_DIR)/socket.cpp $(SRC_DIR)/wire.cpp $(SRC_DIR)/timer.cpp \
               $(SRC_DIR)/stop_and_wait.cpp $(SRC_DIR)/go_back_n.cpp $(SRC_DIR)/selective_repeat.cpp
COMMON_OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(BUILD_DIR)/%.o,$(COMMON_SRCS))

.PHONY: all clean

all: $(BUILD_DIR)/sender $(BUILD_DIR)/receiver

$(BUILD_DIR)/%.o: $(SRC_DIR)/%.cpp | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

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
	for f in results/plots/*.svg; do \
		inkscape --export-type=pdf --export-filename="report/figures/$$(basename "$$f" .svg).pdf" "$$f"; \
	done
	cd report && pdflatex -interaction=nonstopmode report.tex && pdflatex -interaction=nonstopmode report.tex

results: experiments plots report
