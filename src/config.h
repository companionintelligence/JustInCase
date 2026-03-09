#pragma once

#include <string>
#include <cstdlib>
#include <iostream>

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

// ── Database ─────────────────────────────────────────────────────────
const std::string DB_PATH = "data/jic.db";

// ── Model paths (overridable via environment) ────────────────────────
inline std::string get_llm_model_path() {
    const char* f = getenv("LLM_GGUF_FILE");
    return std::string("./gguf_models/") +
           (f ? f : "Llama-3.2-3B-Instruct-Q4_K_M.gguf");
}

inline std::string get_embedding_model_path() {
    const char* f = getenv("EMBEDDING_GGUF_FILE");
    return std::string("./gguf_models/") +
           (f ? f : "nomic-embed-text-v1.5.Q4_K_M.gguf");
}

inline std::string get_llm_model_name() {
    const char* n = getenv("LLM_MODEL");
    return n ? n : "llama3.2:3b";
}

inline std::string get_embedding_model_name() {
    const char* n = getenv("EMBEDDING_MODEL");
    return n ? n : "nomic-embed-text";
}
