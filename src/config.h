#pragma once

#include <string>
#include <cstdlib>
#include <iostream>

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
inline std::string get_llm_model_path() {
    return std::string("./gguf_models/") +
           env_or("LLM_GGUF_FILE", "Llama-3.2-3B-Instruct-Q4_K_M.gguf");
}

inline std::string get_embedding_model_path() {
    return std::string("./gguf_models/") +
           env_or("EMBEDDING_GGUF_FILE", "nomic-embed-text-v1.5.Q4_K_M.gguf");
}

inline std::string get_llm_model_name() {
    return env_or("LLM_MODEL", "llama3.2:3b");
}

inline std::string get_embedding_model_name() {
    return env_or("EMBEDDING_MODEL", "nomic-embed-text");
}
