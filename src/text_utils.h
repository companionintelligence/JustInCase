#pragma once

#include <string>
#include <vector>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <iostream>
#include <curl/curl.h>
#include "config.h"

namespace fs = std::filesystem;

// Helper function for C++17 (ends_with is C++20)
inline bool string_ends_with(const std::string& str, const std::string& suffix) {
    if (suffix.size() > str.size()) return false;
    return str.compare(str.size() - suffix.size(), suffix.size(), suffix) == 0;
}

// CURL callback for response data
size_t tika_write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Extract text from file using Tika
std::string extract_text_with_tika(const std::string& filepath) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        std::cerr << "Failed to initialize CURL for Tika" << std::endl;
        return "";
    }
    
    std::string response;
    
    // Check file size first
    std::error_code ec;
    auto file_size = fs::file_size(filepath, ec);
    if (ec) {
        std::cerr << "Failed to get file size: " << filepath << std::endl;
        curl_easy_cleanup(curl);
        return "";
    }
    
    // Skip very large files
    const size_t MAX_FILE_SIZE = 50 * 1024 * 1024; // 50MB
    if (file_size > MAX_FILE_SIZE) {
        std::cerr << "File too large for processing: " << filepath << " (" << file_size << " bytes)" << std::endl;
        curl_easy_cleanup(curl);
        return "";
    }
    
    // Read file content
    std::ifstream file(filepath, std::ios::binary);
    if (!file) {
        std::cerr << "Failed to open file: " << filepath << std::endl;
        curl_easy_cleanup(curl);
        return "";
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string file_content = buffer.str();
    
    // Set up CURL with timeout
    curl_easy_setopt(curl, CURLOPT_URL, TIKA_URL.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, file_content.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, file_content.size());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 30L); // 30 second timeout
    
    // Headers
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: text/plain");
    
    // Determine content type based on file extension
    std::string content_type = "application/octet-stream";
    if (string_ends_with(filepath, ".pdf")) {
        content_type = "application/pdf";
    } else if (string_ends_with(filepath, ".txt")) {
        content_type = "text/plain";
    } else if (string_ends_with(filepath, ".html") || string_ends_with(filepath, ".htm")) {
        content_type = "text/html";
    }
    
    std::string content_type_header = "Content-Type: " + content_type;
    headers = curl_slist_append(headers, content_type_header.c_str());
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Response handling
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, tika_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    // Get HTTP response code
    long http_code = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
        return "";
    }
    
    if (http_code != 200) {
        std::cerr << "Tika HTTP error: " << http_code << " for file: " << filepath << std::endl;
        std::cerr << "Response: " << response.substr(0, 200) << "..." << std::endl;
        return "";
    }
    
    std::cout << "Successfully extracted " << response.length() << " characters from " << filepath << std::endl;
    return response;
}

// Split text into chunks
std::vector<std::string> split_text(const std::string& text) {
    std::vector<std::string> chunks;
    
    // Reserve space to avoid reallocations
    size_t estimated_chunks = (text.length() / (CHUNK_SIZE - CHUNK_OVERLAP)) + 1;
    chunks.reserve(estimated_chunks);
    
    size_t start = 0;
    
    while (start < text.length()) {
        size_t end = std::min(start + CHUNK_SIZE, text.length());
        
        // Try to break at sentence boundaries
        if (end < text.length()) {
            size_t period = text.rfind(". ", end);
            if (period != std::string::npos && period > start + CHUNK_SIZE - 100) {
                end = period + 2;
            }
        }
        
        chunks.push_back(text.substr(start, end - start));
        start = end - CHUNK_OVERLAP;
        
        // Prevent infinite loop if overlap is too large
        if (start <= chunks.size() * CHUNK_OVERLAP) {
            start = chunks.size() * (CHUNK_SIZE - CHUNK_OVERLAP);
        }
    }
    
    std::cout << "Created " << chunks.size() << " chunks from " << text.length() << " characters" << std::endl;
    
    return chunks;
}
