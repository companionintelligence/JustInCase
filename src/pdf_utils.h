#pragma once

// PDF text extraction using MuPDF (replaces Apache Tika)
// MuPDF is a lightweight C library — no JVM, no server, no network round-trips.

#include <string>
#include <vector>
#include <iostream>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <mupdf/fitz.h>

namespace fs = std::filesystem;

// Extract text from a PDF file, returning (page_number, page_text) pairs.
// Page numbers are 1-based.
inline std::vector<std::pair<int, std::string>> extract_pdf_pages(const std::string& filepath) {
    std::vector<std::pair<int, std::string>> pages;

    fz_context* ctx = fz_new_context(NULL, NULL, FZ_STORE_DEFAULT);
    if (!ctx) {
        std::cerr << "MuPDF: failed to create context" << std::endl;
        return pages;
    }

    fz_try(ctx) {
        fz_register_document_handlers(ctx);
        fz_document* doc = fz_open_document(ctx, filepath.c_str());
        int page_count = fz_count_pages(ctx, doc);

        std::cout << "MuPDF: opened " << filepath << " (" << page_count << " pages)" << std::endl;

        for (int i = 0; i < page_count; i++) {
            fz_page* page = fz_load_page(ctx, doc, i);

            // Extract structured text
            fz_stext_page* stext = fz_new_stext_page_from_page(ctx, page, NULL);

            // Render to plain text via an in-memory buffer
            fz_buffer* buf = fz_new_buffer(ctx, 4096);
            fz_output* out = fz_new_output_with_buffer(ctx, buf);
            fz_print_stext_page_as_text(ctx, out, stext);
            fz_close_output(ctx, out);
            fz_drop_output(ctx, out);

            // Pull the bytes out of the buffer
            unsigned char* data = NULL;
            size_t len = fz_buffer_storage(ctx, buf, &data);
            std::string page_text(reinterpret_cast<char*>(data), len);

            // Keep pages with meaningful content
            if (page_text.length() > 10) {
                pages.push_back({i + 1, page_text});
            }

            fz_drop_buffer(ctx, buf);
            fz_drop_stext_page(ctx, stext);
            fz_drop_page(ctx, page);
        }

        fz_drop_document(ctx, doc);
    }
    fz_catch(ctx) {
        std::cerr << "MuPDF error processing " << filepath << ": "
                  << fz_caught_message(ctx) << std::endl;
    }

    fz_drop_context(ctx);
    return pages;
}

// Convenience: extract all text from a PDF as one string
inline std::string extract_pdf_text(const std::string& filepath) {
    auto pages = extract_pdf_pages(filepath);
    std::ostringstream combined;
    for (const auto& [page_num, text] : pages) {
        combined << text;
        if (!text.empty() && text.back() != '\n') {
            combined << "\n";
        }
    }
    return combined.str();
}

// Extract text from a plain text file
inline std::string extract_text_file(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) {
        std::cerr << "Failed to open text file: " << filepath << std::endl;
        return "";
    }
    std::stringstream buf;
    buf << file.rdbuf();
    return buf.str();
}

// Check whether a file is small enough to process
inline bool is_file_processable(const std::string& filepath,
                                size_t max_size = 100 * 1024 * 1024) {
    std::error_code ec;
    auto sz = fs::file_size(filepath, ec);
    if (ec) return false;
    return sz <= max_size;
}
