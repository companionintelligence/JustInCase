# Unit tests for dependency-free headers (no llama.cpp / MuPDF / SQLite).
# Usage: make -C tests/unit
#
#   test_text_utils       — chunking/text helpers
#   test_telemetry        — telemetry gate + redaction primitives (no deps)
#   test_telemetry_scrub  — the before_send/on_crash body. Needs nlohmann/json,
#                           which this repo fetches at build time rather than
#                           vendoring (same pinned version as the Dockerfile).

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O1
SRC_DIR   = ../../src
DEPS_DIR  = .deps
JSON_HPP  = $(DEPS_DIR)/nlohmann/json.hpp
JSON_URL  = https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp

all: run

test_text_utils: test_text_utils.cpp $(SRC_DIR)/text_utils.h $(SRC_DIR)/config.h
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -o $@ test_text_utils.cpp

test_telemetry: test_telemetry.cpp $(SRC_DIR)/telemetry_redact.h $(SRC_DIR)/telemetry_settings.h
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -o $@ test_telemetry.cpp

$(JSON_HPP):
	@mkdir -p $(DEPS_DIR)/nlohmann
	@echo "Fetching nlohmann/json.hpp for the scrubber tests..."
	@curl -fsSL -o $@ $(JSON_URL) || { rm -f $@; \
	  echo "ERROR: could not download nlohmann/json.hpp (offline?)."; \
	  echo "       Only the scrubber tests need it; run 'make test_telemetry'"; \
	  echo "       and 'make test_text_utils' to run the rest offline."; \
	  exit 1; }

test_telemetry_scrub: test_telemetry_scrub.cpp $(JSON_HPP) \
                      $(SRC_DIR)/telemetry_scrub.h $(SRC_DIR)/telemetry_redact.h
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -I$(DEPS_DIR) -o $@ test_telemetry_scrub.cpp

run: test_text_utils test_telemetry test_telemetry_scrub
	./test_text_utils
	./test_telemetry
	./test_telemetry_scrub

clean:
	rm -f test_text_utils test_telemetry test_telemetry_scrub
	rm -rf $(DEPS_DIR)

.PHONY: all run clean
