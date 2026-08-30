# Unit tests for dependency-free headers (no llama.cpp / MuPDF / SQLite).
# Usage: make -C tests/unit
#
#   test_text_utils       — chunking/text helpers
#   test_telemetry        — telemetry gate + redaction primitives (no deps)
#   test_telemetry_scrub  — the before_send/on_crash body. Needs nlohmann/json,
#                           which this repo fetches at build time rather than
#                           vendoring (same pinned version as the Dockerfile).
#   test_kiwix_parse      — the kiwix-serve response parsers. Needs cpp-httplib
#                           for the same reason (kiwix_client.h includes it),
#                           at the same pinned version as the Dockerfile.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -O1
SRC_DIR   = ../../src
DEPS_DIR  = .deps
JSON_HPP  = $(DEPS_DIR)/nlohmann/json.hpp
JSON_URL  = https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp
HTTPLIB_H = $(DEPS_DIR)/httplib.h
HTTPLIB_URL = https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h

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

$(HTTPLIB_H):
	@mkdir -p $(DEPS_DIR)
	@echo "Fetching cpp-httplib for the kiwix parser tests..."
	@curl -fsSL -o $@ $(HTTPLIB_URL) || { rm -f $@; \
	  echo "ERROR: could not download httplib.h (offline?)."; \
	  echo "       Only the kiwix parser tests need it."; \
	  exit 1; }

test_kiwix_parse: test_kiwix_parse.cpp $(HTTPLIB_H) $(SRC_DIR)/kiwix_client.h \
                  $(SRC_DIR)/text_utils.h $(SRC_DIR)/config.h
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -I$(DEPS_DIR) -o $@ test_kiwix_parse.cpp

test_telemetry_scrub: test_telemetry_scrub.cpp $(JSON_HPP) \
                      $(SRC_DIR)/telemetry_scrub.h $(SRC_DIR)/telemetry_redact.h
	$(CXX) $(CXXFLAGS) -I$(SRC_DIR) -I$(DEPS_DIR) -o $@ test_telemetry_scrub.cpp

run: test_text_utils test_telemetry test_telemetry_scrub test_kiwix_parse
	./test_text_utils
	./test_telemetry
	./test_telemetry_scrub
	./test_kiwix_parse

clean:
	rm -f test_text_utils test_telemetry test_telemetry_scrub test_kiwix_parse
	rm -rf $(DEPS_DIR)

.PHONY: all run clean
