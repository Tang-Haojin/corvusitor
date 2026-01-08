# Corvusitor Makefile
# C++11 standard for compatibility with Verilator

CXX = g++
CXXFLAGS = -std=c++11 -Wall -Wextra -g -I./include

## Source directories
SRC_DIR = src
INCLUDE_DIR = include
TEST_DIR = test
BUILD_DIR = build

## Source files
SRC_FILES = $(SRC_DIR)/module_parser.cpp \
            $(SRC_DIR)/connection_builder.cpp \
            $(SRC_DIR)/code_generator.cpp \
            $(SRC_DIR)/simulator_interface.cpp \
            $(SRC_DIR)/corvus_generator.cpp
OBJ_FILES = $(BUILD_DIR)/module_parser.o \
            $(BUILD_DIR)/connection_builder.o \
            $(BUILD_DIR)/code_generator.o \
            $(BUILD_DIR)/simulator_interface.o \
            $(BUILD_DIR)/corvus_generator.o

## Header files
HEADERS = $(INCLUDE_DIR)/port_info.h \
          $(INCLUDE_DIR)/module_info.h \
          $(INCLUDE_DIR)/module_parser.h \
          $(INCLUDE_DIR)/connection_builder.h \
          $(INCLUDE_DIR)/connection_analysis.h \
          $(INCLUDE_DIR)/code_generator.h \
          $(INCLUDE_DIR)/corvus_generator.h \
          $(INCLUDE_DIR)/simulator_interface.h

## Test programs
TEST_PARSER_BIN = $(BUILD_DIR)/test_parser
TEST_PARSER_SRC = $(TEST_DIR)/test_parser.cpp
TEST_CONN_BIN = $(BUILD_DIR)/test_connection
TEST_CONN_SRC = $(TEST_DIR)/test_connection.cpp
TEST_CODEGEN_BIN = $(BUILD_DIR)/test_code_generator
TEST_CODEGEN_SRC = $(TEST_DIR)/test_code_generator.cpp
TEST_CONN_ANALYSIS_BIN = $(BUILD_DIR)/test_connection_analysis
TEST_CONN_ANALYSIS_SRC = $(TEST_DIR)/test_connection_analysis.cpp
TEST_CORVUS_GEN_BIN = $(BUILD_DIR)/test_corvus_generator
TEST_CORVUS_GEN_SRC = $(TEST_DIR)/test_corvus_generator.cpp

## Default target
.PHONY: all
CORVUSITOR_BIN = $(BUILD_DIR)/corvusitor
MAIN_SRC = $(SRC_DIR)/main.cpp

all: $(TEST_PARSER_BIN) $(TEST_CONN_BIN) $(TEST_CODEGEN_BIN) $(TEST_CONN_ANALYSIS_BIN) $(TEST_CORVUS_GEN_BIN) $(CORVUSITOR_BIN)
## Build main program
$(CORVUSITOR_BIN): $(OBJ_FILES) $(MAIN_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(OBJ_FILES) $(MAIN_SRC) -o $@

## Create build directory
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)

## Build module_parser.o
$(BUILD_DIR)/module_parser.o: $(SRC_DIR)/module_parser.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

## Build connection_builder.o
$(BUILD_DIR)/connection_builder.o: $(SRC_DIR)/connection_builder.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

## Build code_generator.o
$(BUILD_DIR)/code_generator.o: $(SRC_DIR)/code_generator.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

## Build simulator_interface.o
$(BUILD_DIR)/simulator_interface.o: $(SRC_DIR)/simulator_interface.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD_DIR)/corvus_generator.o: $(SRC_DIR)/corvus_generator.cpp $(HEADERS) | $(BUILD_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

## Build test programs
$(TEST_PARSER_BIN): $(OBJ_FILES) $(TEST_PARSER_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(OBJ_FILES) $(TEST_PARSER_SRC) -o $@

## Build connection test program
$(TEST_CONN_BIN): $(OBJ_FILES) $(TEST_CONN_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(OBJ_FILES) $(TEST_CONN_SRC) -o $@

## Build code generator test program
$(TEST_CODEGEN_BIN): $(OBJ_FILES) $(TEST_CODEGEN_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(OBJ_FILES) $(TEST_CODEGEN_SRC) -o $@

$(TEST_CONN_ANALYSIS_BIN): $(OBJ_FILES) $(TEST_CONN_ANALYSIS_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(OBJ_FILES) $(TEST_CONN_ANALYSIS_SRC) -o $@

$(TEST_CORVUS_GEN_BIN): $(OBJ_FILES) $(TEST_CORVUS_GEN_SRC) $(HEADERS)
	$(CXX) $(CXXFLAGS) $(OBJ_FILES) $(TEST_CORVUS_GEN_SRC) -o $@

## Run parser test
.PHONY: test
test: $(TEST_PARSER_BIN)
	./$(TEST_PARSER_BIN)

## Run connection test
.PHONY: test_conn
test_conn: $(TEST_CONN_BIN)
	./$(TEST_CONN_BIN)

## Run connection analysis test
.PHONY: test_conn_analysis
test_conn_analysis: $(TEST_CONN_ANALYSIS_BIN)
	./$(TEST_CONN_ANALYSIS_BIN)

## Run code generator test
.PHONY: test_codegen
test_codegen: $(TEST_CODEGEN_BIN)
	./$(TEST_CODEGEN_BIN)

## Run corvus generator test
.PHONY: test_corvus_gen
test_corvus_gen: $(TEST_CORVUS_GEN_BIN)
	./$(TEST_CORVUS_GEN_BIN)

## Clean build artifacts
.PHONY: clean
clean:
	rm -rf $(BUILD_DIR)

## Show help
.PHONY: help
help:
	@echo "Corvusitor Build System"
	@echo "======================="
	@echo "Targets:"
	@echo "  all           - Build all test programs (default)"
	@echo "  test          - Build and run parser test"
	@echo "  test_conn     - Build and run connection test"
	@echo "  test_codegen  - Build and run code generator test"
	@echo "  clean         - Remove build artifacts"
	@echo "  help          - Show this help message"
