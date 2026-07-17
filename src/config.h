#pragma once

#include <string>
#include <cstdlib>
#include <iostream>
#include <filesystem>
#include <system_error>

// ── Version ──────────────────────────────────────────────────────────
#define JIC_VERSION "0.3.0"

// ── Network ──────────────────────────────────────────────────────────
const int PORT = 8080;

// ── Embeddings ───────────────────────────────────────────────────────
const int EMBEDDING_DIM = 768;  // nomic-embed-text-v1.5 → 768 dimensions

// ── Chunking ─────────────────────────────────────────────────────────
const int CHUNK_SIZE    = 1500; // characters per chunk (target)
const int CHUNK_OVERLAP = 200;  // overlap between adjacent chunks

// ── Retrieval ────────────────────────────────────────────────────────
const int MAX_CONTEXT_CHUNKS = 5;   // chunks sent to the LLM
const int SEARCH_CANDIDATES  = 30;  // candidates pulled before re-ranking

// ── LLM ──────────────────────────────────────────────────────────────
const int LLM_CONTEXT_SIZE   = 8192;  // n_ctx — token window
const int LLM_MAX_TOKENS     = 1024;  // max tokens in a generated response
const int LLM_BATCH_SIZE     = 512;

// ── Request limits ───────────────────────────────────────────────────
const size_t MAX_REQUEST_BODY  = 1 * 1024 * 1024; // 1 MB JSON body cap
const size_t MAX_QUERY_CHARS   = 8000;            // longest accepted question
const size_t MAX_CONV_ID_CHARS = 128;
const size_t MAX_CONVERSATIONS = 200;             // bounded history map

// ── Ingestion ────────────────────────────────────────────────────────
const size_t MAX_DOCUMENT_CHARS = 8u * 1000u * 1000u; // per-document text cap
const int    FILE_SETTLE_SECONDS = 10; // skip files modified more recently

// ── Environment helpers ──────────────────────────────────────────────
inline std::string env_or(const char* key, const std::string& fallback) {
    const char* v = getenv(key);
    return (v && *v) ? std::string(v) : fallback;
}

inline int env_or_int(const char* key, int fallback) {
    const char* v = getenv(key);
    if (!v || !*v) return fallback;
    try { return std::stoi(v); } catch (...) { return fallback; }
}

// ── Paths (overridable via environment) ──────────────────────────────
inline std::string get_db_path() {
    return env_or("JIC_DB_PATH", "data/jic.db");
}

inline std::string get_sources_dir() {
    return env_or("JIC_SOURCES_DIR", "public/sources");
}

inline int get_scan_interval_sec() {
    int s = env_or_int("JIC_SCAN_INTERVAL_SEC", 30);
    return s < 5 ? 5 : s;
}

// Cross-origin access is disabled unless explicitly configured.
// Set JIC_CORS_ORIGIN to an origin (or "*") to allow API calls from
// other web origins.
inline std::string get_cors_origin() {
    return env_or("JIC_CORS_ORIGIN", "");
}

// ── Model paths (overridable via environment) ────────────────────────
// The directory that holds the GGUF model files. Overridable so a deploy
// whose bind mount lands somewhere other than ./gguf_models can point the
// server at the real location instead of silently running degraded.
inline std::string get_gguf_dir() {
    return env_or("JIC_GGUF_DIR", "./gguf_models");
}

inline std::string get_llm_model_path() {
    return get_gguf_dir() + "/" +
           env_or("LLM_GGUF_FILE", "Llama-3.2-3B-Instruct-Q4_K_M.gguf");
}

inline std::string get_embedding_model_path() {
    return get_gguf_dir() + "/" +
           env_or("EMBEDDING_GGUF_FILE", "nomic-embed-text-v1.5.Q4_K_M.gguf");
}

// Absolute, resolved form of the model directory — computed once (cwd is
// stable at /app in the container). Never throws: a missing directory is a
// normal state (the whole point of degraded mode), so this must be safe to
// call from the /status handler on every poll.
inline std::string resolved_gguf_dir() {
    static const std::string cached = [] {
        std::error_code ec;
        auto abs = std::filesystem::weakly_canonical(get_gguf_dir(), ec);
        return ec ? get_gguf_dir() : abs.string();
    }();
    return cached;
}

// Human-readable diagnostic for logs when a model fails to load: the exact
// resolved path, whether the file is actually there (and its size), and if
// not, what the directory *does* contain — so "LLM missing" stops being a
// mystery. Error-code-safe throughout; never throws.
inline std::string describe_model_path(const std::string& path) {
    namespace fs = std::filesystem;
    std::error_code ec;
    const fs::path p(path);

    auto abs = fs::weakly_canonical(p, ec);
    std::string out = "resolved='" + (ec ? path : abs.string()) + "'";
    ec.clear();

    if (fs::exists(p, ec) && !ec) {
        auto sz = fs::file_size(p, ec);
        out += ec ? " [present, size unknown]"
                  : " [present, " + std::to_string(sz) + " bytes]";
        return out;
    }

    out += " [MISSING]";
    const fs::path dir = p.parent_path();
    ec.clear();
    auto dir_abs = fs::weakly_canonical(dir, ec);
    const std::string dir_shown = ec ? dir.string() : dir_abs.string();
    ec.clear();

    if (fs::is_directory(dir, ec) && !ec) {
        out += "; dir '" + dir_shown + "' contains: ";
        std::error_code it_ec;
        // Iterate with the error_code increment(): directory_iterator's
        // operator++ (used by range-for) THROWS on a mid-scan failure, which
        // would break this helper's never-throws contract and, in the loader
        // thread, terminate the process. increment(ec) reports instead.
        fs::directory_iterator it(dir, it_ec), end;
        if (it_ec) {
            // e.g. the dir exists but is not readable by this uid — exactly
            // the kind of thing that looks like "no models" but isn't.
            out += "(unreadable: " + it_ec.message() + ")";
        } else {
            int n = 0;
            for (; !it_ec && it != end; it.increment(it_ec)) {
                if (n++) out += ", ";
                out += it->path().filename().string();
                if (n >= 20) { out += ", …"; break; }
            }
            if (n == 0) out += "(empty)";
        }
    } else if (ec) {
        // is_directory() itself failed (e.g. the parent is not traversable):
        // report the actual reason instead of mislabelling it "does not exist".
        out += "; dir '" + dir_shown + "' is unreadable: " + ec.message();
    } else {
        out += "; dir '" + dir_shown + "' does not exist";
    }
    return out;
}

inline std::string get_llm_model_name() {
    return env_or("LLM_MODEL", "llama3.2:3b");
}

inline std::string get_embedding_model_name() {
    return env_or("EMBEDDING_MODEL", "nomic-embed-text");
}
