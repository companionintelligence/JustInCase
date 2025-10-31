#pragma once

#include <string>
#include <cstdlib>  // for getenv()

// Global configuration
const int PORT = 8080;
const int EMBEDDING_DIM = 768;  // nomic-embed-text-v1.5 uses 768 dimensions
const int CHUNK_SIZE = 2000;  // Larger chunks for more complete information
const int CHUNK_OVERLAP = 200;  // Slightly more overlap
const int MAX_CONTEXT_CHUNKS = 3;  // Use up to 3 chunks for context to keep it focused
const int SEARCH_TOP_K = 10;  // Search for top 10, but only use top 3

const std::string LLAMA_MODEL_PATH = "./gguf_models/Qwen2.5-VL-7B-Instruct-Q4_K_M.gguf";
const std::string NOMIC_MODEL_PATH = "./gguf_models/nomic-embed-text-v1.5.Q4_K_M.gguf";

// Model configuration from environment
inline std::string get_llm_model_path() {
    const char* file = getenv("LLM_GGUF_FILE");
    return std::string("./gguf_models/") + (file ? file : "Qwen2.5-VL-7B-Instruct-Q4_K_M.gguf");
}

inline std::string get_llm_mmproj_path() {
    const char* file = getenv("LLM_MMPROJ_FILE");
    return file ? std::string("./gguf_models/") + file : "";
}

inline std::string get_embedding_model_path() {
    const char* file = getenv("EMBEDDING_GGUF_FILE");
    return std::string("./gguf_models/") + (file ? file : "nomic-embed-text-v1.5.Q4_K_M.gguf");
}

inline std::string get_llm_model_name() {
    const char* name = getenv("LLM_MODEL");
    return name ? name : "qwen2.5-vl:7b";
}

inline std::string get_embedding_model_name() {
    const char* name = getenv("EMBEDDING_MODEL");
    return name ? name : "nomic-embed-text";
}
const std::string TIKA_URL = "http://tika:9998/tika";

// PostgreSQL configuration from environment
inline std::string get_pg_host() { 
    const char* h = getenv("POSTGRES_HOST");
    return h ? h : "postgres";
}

inline int get_pg_port() {
    const char* p = getenv("POSTGRES_PORT");
    return p ? std::stoi(p) : 5432;
}

inline std::string get_pg_db() {
    const char* d = getenv("POSTGRES_DB");
    return d ? d : "jic_db";
}

inline std::string get_pg_user() {
    const char* u = getenv("POSTGRES_USER");
    return u ? u : "jic";
}

inline std::string get_pg_password() {
    const char* p = getenv("POSTGRES_PASSWORD");
    return p ? p : "jic_password";
}
