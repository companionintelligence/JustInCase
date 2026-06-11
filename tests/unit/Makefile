# Unit tests for dependency-free headers (no llama.cpp / MuPDF / SQLite).
# Usage: make -C tests/unit

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O1
SRC_DIR   = ../../src

all: run

test_text_utils: test_text_utils.cpp $(SRC_DIR)/text_utils.h $(SRC_DIR)/config.h
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -o $@ test_text_utils.cpp

run: test_text_utils
	./test_text_utils

clean:
	rm -f test_text_utils

.PHONY: all run clean
