# Plain GNU make fallback (CMake is the primary build system).
CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Iinclude
LIB_SRC  := src/sha256.cpp src/hash.cpp src/smt.cpp src/proof.cpp src/json.cpp src/db_file.cpp
TEST_SRC := tests/test_main.cpp tests/test_sha256.cpp tests/test_smt.cpp tests/test_proof.cpp tests/test_db_file.cpp
BIN      := build-make

all: $(BIN)/smt $(BIN)/smt_tests $(BIN)/smt_bench

$(BIN):
	mkdir -p $(BIN)

$(BIN)/smt: $(LIB_SRC) src/cli/main.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BIN)/smt_tests: $(LIB_SRC) $(TEST_SRC) | $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $^

$(BIN)/smt_bench: $(LIB_SRC) bench/bench.cpp | $(BIN)
	$(CXX) $(CXXFLAGS) -o $@ $^

test: $(BIN)/smt_tests
	./$(BIN)/smt_tests

clean:
	rm -rf $(BIN)

.PHONY: all test clean
