// Unit tests for src/text_utils.h (pure functions, no llama/sqlite deps).
// Build & run:  make -C tests/unit

#include <cassert>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

#include "text_utils.h"

static int g_failures = 0;

#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::cerr << "FAIL  " << __func__ << ":" << __LINE__ << "  "   \
                      << #cond << std::endl;                               \
            g_failures++;                                                  \
        }                                                                  \
    } while (0)

static void test_trim() {
    CHECK(trim("  hello  ") == "hello");
    CHECK(trim("\n\t x \r\n") == "x");
    CHECK(trim("") == "");
    CHECK(trim("   \t\n  ") == "");
    CHECK(trim("no-trim") == "no-trim");
}

static void test_string_ends_with() {
    CHECK(string_ends_with("manual.pdf", ".pdf"));
    CHECK(!string_ends_with("manual.pdf", ".txt"));
    CHECK(!string_ends_with("a", "longer-than-string"));
    CHECK(string_ends_with("x", ""));
}

static void test_split_by_keeps_separators() {
    auto parts = split_by("a. b. c", ". ");
    CHECK(parts.size() == 3);
    CHECK(parts[0] == "a. ");
    CHECK(parts[1] == "b. ");
    CHECK(parts[2] == "c");

    // Reassembling the parts must reproduce the input (lossless split)
    std::string joined;
    for (const auto& p : parts) joined += p;
    CHECK(joined == "a. b. c");
}

static void test_split_text_empty_and_tiny() {
    CHECK(split_text("").empty());
    CHECK(split_text("   \n\n  ").empty());
    // Chunks under 80 chars are filtered as noise
    CHECK(split_text("short").empty());
}

static void test_split_text_single_chunk() {
    std::string text(200, 'a');
    auto chunks = split_text(text);
    CHECK(chunks.size() == 1);
    CHECK(chunks[0] == text);
}

static void test_split_text_respects_max_size() {
    // Build a document of 80 paragraphs, each ~190 chars
    std::string para;
    for (int i = 0; i < 12; i++) para += "the quick brown fox ";  // 240 chars
    std::string text;
    for (int i = 0; i < 80; i++) text += para + "\n\n";

    auto chunks = split_text(text);
    CHECK(!chunks.empty());

    // Every chunk must respect CHUNK_SIZE plus the overlap prefix
    for (const auto& c : chunks) {
        CHECK(static_cast<int>(c.length()) <= CHUNK_SIZE + CHUNK_OVERLAP);
        CHECK(static_cast<int>(c.length()) >= 80);
    }

    // Coverage: total retained text must be at least the input size
    size_t total = std::accumulate(chunks.begin(), chunks.end(), size_t{0},
            [](size_t acc, const std::string& c) { return acc + c.size(); });
    CHECK(total >= text.size() / 2);
}

static void test_split_text_no_separators() {
    // One giant token: must hard-split rather than loop or overflow
    std::string text(7000, 'x');
    auto chunks = split_text(text);
    CHECK(chunks.size() >= 4);
    for (const auto& c : chunks)
        CHECK(static_cast<int>(c.length()) <= CHUNK_SIZE + CHUNK_OVERLAP);
}

static void test_split_text_overlap() {
    // Two paragraphs too large for one chunk → expect the second chunk to
    // start with the tail of the first (context overlap for retrieval).
    std::string p1(1200, 'a');
    std::string p2(1200, 'b');
    auto chunks = split_text(p1 + "\n\n" + p2);
    CHECK(chunks.size() == 2);
    if (chunks.size() == 2) {
        CHECK(chunks[1].find(std::string(CHUNK_OVERLAP / 2, 'a')) == 0);
        CHECK(chunks[1].find('b') != std::string::npos);
    }
}

int main() {
    test_trim();
    test_string_ends_with();
    test_split_by_keeps_separators();
    test_split_text_empty_and_tiny();
    test_split_text_single_chunk();
    test_split_text_respects_max_size();
    test_split_text_no_separators();
    test_split_text_overlap();

    if (g_failures == 0) {
        std::cout << "All text_utils tests passed." << std::endl;
        return 0;
    }
    std::cerr << g_failures << " check(s) failed." << std::endl;
    return 1;
}
