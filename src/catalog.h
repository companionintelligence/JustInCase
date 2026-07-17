#pragma once

// ── Catalog parser ───────────────────────────────────────────────────
// Reads the curated source manifest (sources.yaml) into memory. This is a
// deliberately minimal line parser for the flat schema that manifest uses —
// NOT a general YAML parser — and mirrors the awk parser in
// helper-scripts/fetch-source-data.sh so the two never diverge. The manifest
// is a trusted, image-baked file, so the parser optimises for clarity.

#include <string>
#include <vector>
#include <fstream>
#include "config.h"

struct CatalogItem {
    std::string url, filename, category, collection, title, license, size, sha256;
};

struct CatalogCollection {
    std::string id, name, description;
};

struct Catalog {
    std::vector<CatalogCollection> collections;
    std::vector<CatalogItem>       items;

    const CatalogCollection* find_collection(const std::string& id) const {
        for (const auto& c : collections) if (c.id == id) return &c;
        return nullptr;
    }
};

// Clean a raw value that follows the first ':' on a line. A double-quoted
// value is returned verbatim (so a legitimate '#' inside it — e.g. a license
// like "PD (Project Gutenberg #17093)" — is preserved). An unquoted value has
// a trailing " # comment" stripped and is trimmed.
inline std::string catalog_clean_value(std::string v) {
    size_t b = v.find_first_not_of(" \t");
    if (b == std::string::npos) return "";
    v = v.substr(b);

    if (v.front() == '"') {
        size_t end = v.find('"', 1);
        if (end != std::string::npos) return v.substr(1, end - 1);
        // Unterminated quote — fall through to best-effort unquoted handling.
    }

    size_t h = v.find(" #");
    if (h != std::string::npos) v = v.substr(0, h);
    size_t e = v.find_last_not_of(" \t\r");
    return (e == std::string::npos) ? "" : v.substr(0, e + 1);
}

inline Catalog parse_catalog(const std::string& path) {
    Catalog cat;
    std::ifstream in(path);
    if (!in) return cat;

    enum Section { NONE, COLLECTIONS, SOURCES } sec = NONE;
    CatalogItem item;       bool have_item = false;
    CatalogCollection col;  bool have_col  = false;

    auto flush_item = [&] {
        if (have_item) cat.items.push_back(item);
        item = CatalogItem(); have_item = false;
    };
    auto flush_col = [&] {
        if (have_col) cat.collections.push_back(col);
        col = CatalogCollection(); have_col = false;
    };

    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t b = line.find_first_not_of(" \t");
        if (b == std::string::npos) continue;             // blank
        std::string t = line.substr(b);
        if (t[0] == '#') continue;                        // comment

        // Top-level keys (no indentation) switch sections.
        if (b == 0) {
            if (t.rfind("collections:", 0) == 0) { flush_col(); flush_item(); sec = COLLECTIONS; }
            else if (t.rfind("sources:", 0) == 0) { flush_col(); flush_item(); sec = SOURCES; }
            else { flush_col(); flush_item(); sec = NONE; }
            continue;
        }

        // Indented "key: value" (optionally list-prefixed with "- ").
        bool dash = false;
        std::string s = t;
        if (s.rfind("- ", 0) == 0) { dash = true; s = s.substr(2); }
        size_t colon = s.find(':');
        if (colon == std::string::npos) continue;
        std::string key = s.substr(0, colon);
        std::string val = catalog_clean_value(s.substr(colon + 1));

        if (sec == COLLECTIONS) {
            if (dash && key == "id")        { flush_col(); col.id = val; have_col = true; }
            else if (key == "name")         col.name        = val;
            else if (key == "description")  col.description = val;
        } else if (sec == SOURCES) {
            if (dash && key == "url")       { flush_item(); item.url = val; have_item = true; }
            else if (key == "filename")     item.filename   = val;
            else if (key == "category")     item.category   = val;
            else if (key == "collection")   item.collection = val;
            else if (key == "title")        item.title      = val;
            else if (key == "license")      item.license    = val;
            else if (key == "size")         item.size       = val;
            else if (key == "sha256")       item.sha256     = val;
        }
    }
    flush_col();
    flush_item();
    return cat;
}
