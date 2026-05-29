#pragma once

#include <string>

// Shared document/chunk structure used across ingestion and serving
struct Document {
    std::string filename;
    std::string text;
    int page_number = -1;
    int chunk_index = -1;
};
