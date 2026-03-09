#pragma once

// Text utilities: recursive semantic chunking.
// Splits text by progressively finer boundaries (paragraphs → sentences →
// words) so that chunks respect natural document structure.

#include <string>
#include <vector>
#include <iostream>
#include "config.h"

// ── Helpers ──────────────────────────────────────────────────────────

inline bool string_ends_with(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// Trim leading / trailing whitespace
inline std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}

// ── Recursive text splitter ──────────────────────────────────────────
// Tries each separator in order; the first one that appears in the text
// is used to divide it.  Pieces are merged back up to max_chunk_size,
// and any piece that is still too large is recursively split with the
// next finer separator.

inline std::vector<std::string> split_by(const std::string& text,
                                         const std::string& sep) {
    std::vector<std::string> parts;
    size_t start = 0;
    while (start < text.length()) {
        size_t pos = text.find(sep, start);
        if (pos == std::string::npos) {
            parts.push_back(text.substr(start));
            break;
        }
        // Keep the separator attached to the piece before it
        parts.push_back(text.substr(start, pos - start + sep.length()));
        start = pos + sep.length();
    }
    return parts;
}

inline std::vector<std::string> recursive_split(
        const std::string& text,
        int max_chunk_size,
        int overlap,
        int sep_idx) {

    static const std::vector<std::string> separators = {
        "\n\n",     // paragraph
        "\n",       // line
        ". ",       // sentence
        "? ",
        "! ",
        "; ",
        ", ",
        " "         // word (last resort)
    };

    // Base case: fits in one chunk
    if (static_cast<int>(text.length()) <= max_chunk_size) {
        std::string t = trim(text);
        if (t.empty()) return {};
        return {t};
    }

    // Find the coarsest separator that actually appears
    int use_idx = sep_idx;
    while (use_idx < static_cast<int>(separators.size()) &&
           text.find(separators[use_idx]) == std::string::npos) {
        use_idx++;
    }

    // If no separator found, hard-split by character count
    if (use_idx >= static_cast<int>(separators.size())) {
        std::vector<std::string> out;
        for (size_t i = 0; i < text.length();
             i += static_cast<size_t>(max_chunk_size - overlap)) {
            std::string piece = text.substr(i, max_chunk_size);
            std::string t = trim(piece);
            if (!t.empty()) out.push_back(t);
        }
        return out;
    }

    auto pieces = split_by(text, separators[use_idx]);

    // Merge small pieces into chunks
    std::vector<std::string> chunks;
    std::string current;

    for (const auto& piece : pieces) {
        if (current.empty()) {
            current = piece;
        } else if (static_cast<int>(current.length() + piece.length()) <= max_chunk_size) {
            current += piece;
        } else {
            std::string t = trim(current);
            if (!t.empty()) chunks.push_back(t);
            current = piece;
        }
    }
    {
        std::string t = trim(current);
        if (!t.empty()) chunks.push_back(t);
    }

    // Recursively split any chunk that is still too large
    std::vector<std::string> final_chunks;
    for (const auto& chunk : chunks) {
        if (static_cast<int>(chunk.length()) > max_chunk_size) {
            auto sub = recursive_split(chunk, max_chunk_size, overlap, use_idx + 1);
            final_chunks.insert(final_chunks.end(), sub.begin(), sub.end());
        } else {
            final_chunks.push_back(chunk);
        }
    }

    // Apply overlap: prepend the tail of the previous chunk
    if (overlap > 0 && final_chunks.size() > 1) {
        std::vector<std::string> overlapped;
        overlapped.push_back(final_chunks[0]);
        for (size_t i = 1; i < final_chunks.size(); i++) {
            const auto& prev = final_chunks[i - 1];
            std::string prefix;
            if (static_cast<int>(prev.length()) > overlap) {
                prefix = prev.substr(prev.length() - overlap);
            } else {
                prefix = prev;
            }
            overlapped.push_back(prefix + final_chunks[i]);
        }
        return overlapped;
    }

    return final_chunks;
}

// Public entry point — matches old split_text() signature
inline std::vector<std::string> split_text(const std::string& text,
                                           int max_chunk = CHUNK_SIZE,
                                           int overlap   = CHUNK_OVERLAP) {
    auto chunks = recursive_split(text, max_chunk, overlap, 0);

    // Drop tiny chunks that would add noise
    std::vector<std::string> filtered;
    for (auto& c : chunks) {
        if (c.length() >= 80) filtered.push_back(std::move(c));
    }

    std::cout << "Chunker: " << text.length() << " chars → "
              << filtered.size() << " chunks" << std::endl;
    return filtered;
}
