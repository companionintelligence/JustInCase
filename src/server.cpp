// ── JIC Server ───────────────────────────────────────────────────────
//
// Serves the web UI and handles /query (RAG) and /status endpoints.
// Uses SQLite hybrid search (vector + BM25 via RRF) for retrieval.

#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <thread>
#include <filesystem>
#include <cstring>
#include <chrono>
#include <mutex>
#include <atomic>
#include <signal.h>

#include "httplib.h"
#include "llama.h"
#include "nlohmann/json.hpp"
#include "config.h"
#include "types.h"
#include "text_utils.h"
#include "embeddings.h"
#include "llm.h"
#include "sqlite_vec_index.h"

namespace fs = std::filesystem;
using json = nlohmann::json;

// ═════════════════════════════════════════════════════════════════════
// Global state
// ═════════════════════════════════════════════════════════════════════

static SQLiteVecIndex*      g_index      = nullptr;
static EmbeddingGenerator*  g_embeddings = nullptr;
static LLMGenerator*        g_llm        = nullptr;

// Cached status — avoids re-reading the entire DB on every request
static std::atomic<int>     g_chunk_count{0};
static std::atomic<int>     g_file_count{0};

static void refresh_counts() {
    if (g_index) {
        g_chunk_count.store(g_index->chunk_count());
        g_file_count.store(g_index->processed_file_count());
    }
}

// Conversation history
struct ConversationHistory {
    std::vector<std::pair<std::string, std::string>> messages;
    std::chrono::system_clock::time_point last_activity;
};
static std::map<std::string, ConversationHistory> g_conversations;
static std::mutex g_conv_mutex;

// ═════════════════════════════════════════════════════════════════════
// /query handler
// ═════════════════════════════════════════════════════════════════════

static void handle_query(const httplib::Request& req, httplib::Response& res) {
    try {
        auto rj = json::parse(req.body);
        std::string query = rj["query"];

        std::string conv_id = "default";
        if (rj.contains("conversation_id") && !rj["conversation_id"].is_null())
            conv_id = rj["conversation_id"];

        bool use_context = true;
        if (rj.contains("use_context") && rj["use_context"].is_boolean())
            use_context = rj["use_context"];

        json response;
        response["conversation_id"] = conv_id;

        // ── Retrieve conversation history ───────────────────────────
        std::vector<std::pair<std::string, std::string>> history;
        {
            std::lock_guard<std::mutex> lock(g_conv_mutex);
            auto& conv = g_conversations[conv_id];
            history = conv.messages;

            // Prune stale conversations
            auto now = std::chrono::system_clock::now();
            for (auto it = g_conversations.begin(); it != g_conversations.end(); ) {
                auto age = std::chrono::duration_cast<std::chrono::hours>(
                        now - it->second.last_activity).count();
                it = (age > 1) ? g_conversations.erase(it) : std::next(it);
            }
        }

        // ── Build context from documents ────────────────────────────
        std::string context;
        json matches = json::array();

        if (use_context && g_chunk_count.load() > 0) {
            auto q_emb = g_embeddings->get_embedding(query);

            auto results = g_index->hybrid_search(
                    q_emb, query, MAX_CONTEXT_CHUNKS, SEARCH_CANDIDATES);

            std::set<std::string> seen_files;
            for (int i = 0; i < static_cast<int>(results.size()); i++) {
                auto& r = results[i];
                context += "[REFERENCE " + std::to_string(i + 1)
                        + " from " + r.filename + "]\n"
                        + r.text + "\n"
                        + "[END REFERENCE " + std::to_string(i + 1) + "]\n\n";

                if (seen_files.insert(r.filename).second) {
                    matches.push_back({
                        {"filename", r.filename},
                        {"text", r.text.substr(0, 250) + "..."},
                        {"score", r.score}
                    });
                }
            }
        }

        // ── Assemble prompt ─────────────────────────────────────────
        std::string prompt;

        if (!context.empty()) {
            if (context.length() > 6000) {
                context = context.substr(0, 6000)
                        + "\n[REMAINING CONTENT TRUNCATED]\n";
            }
            prompt += "REFERENCE MATERIALS:\n" + context + "\n"
                   +  "Using the above references, answer the following question.\n\n";
        }

        if (!history.empty()) {
            prompt += "Previous conversation:\n";
            size_t start = history.size() > 10 ? history.size() - 10 : 0;
            for (size_t i = start; i < history.size(); i++)
                prompt += history[i].first + ": " + history[i].second + "\n";
            prompt += "\n";
        }

        prompt += "User: " + query + "\n\nAssistant:";

        // ── Generate ────────────────────────────────────────────────
        std::string answer = g_llm->generate(prompt);

        // ── Update conversation history ─────────────────────────────
        {
            std::lock_guard<std::mutex> lock(g_conv_mutex);
            auto& conv = g_conversations[conv_id];
            conv.messages.push_back({"User", query});
            conv.messages.push_back({"Assistant", answer});
            conv.last_activity = std::chrono::system_clock::now();
            if (conv.messages.size() > 20)
                conv.messages.erase(conv.messages.begin(),
                                    conv.messages.begin() + 2);
        }

        response["answer"]  = answer;
        response["matches"] = matches;
        res.set_content(response.dump(), "application/json");

    } catch (const std::exception& e) {
        std::cerr << "query error: " << e.what() << std::endl;
        res.status = 500;
        res.set_content(json({{"error", e.what()}}).dump(), "application/json");
    }
}

// ═════════════════════════════════════════════════════════════════════
// /status handler (uses cached counts)
// ═════════════════════════════════════════════════════════════════════

static void handle_status(const httplib::Request&, httplib::Response& res) {
    json status;
    status["documents_indexed"] = g_chunk_count.load();
    status["files_processed"]   = g_file_count.load();
    res.set_content(status.dump(), "application/json");
}

// ═════════════════════════════════════════════════════════════════════
// Background thread: refresh cached counts every 10 s
// ═════════════════════════════════════════════════════════════════════

static void count_refresher() {
    while (true) {
        refresh_counts();
        std::this_thread::sleep_for(std::chrono::seconds(10));
    }
}

// ═════════════════════════════════════════════════════════════════════
// main
// ═════════════════════════════════════════════════════════════════════

int main() {
    std::cout << "═══════════════════════════════════════════" << std::endl;
    std::cout << "  JIC Server  (SQLite hybrid search)      " << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;

    signal(SIGPIPE, SIG_IGN);

    // ── llama backend ────────────────────────────────────────────────
    llama_backend_init();
    llama_log_set([](enum ggml_log_level level, const char* text, void*) {
        if (level >= GGML_LOG_LEVEL_ERROR) fprintf(stderr, "%s", text);
    }, nullptr);
    ggml_backend_load_all();

    // ── Models ───────────────────────────────────────────────────────
    std::cout << "Loading models..." << std::endl;

    g_embeddings = new EmbeddingGenerator();
    if (!g_embeddings->init()) { std::cerr << "Embeddings init failed" << std::endl; return 1; }

    g_llm = new LLMGenerator();
    if (!g_llm->init()) { std::cerr << "LLM init failed" << std::endl; return 1; }

    // ── SQLite index ─────────────────────────────────────────────────
    fs::create_directories("data");
    g_index = new SQLiteVecIndex();
    if (!g_index->open(DB_PATH)) { std::cerr << "DB open failed" << std::endl; return 1; }

    refresh_counts();
    std::cout << "Index: " << g_chunk_count.load() << " chunks, "
              << g_file_count.load() << " files" << std::endl;

    // Background count refresher
    std::thread(count_refresher).detach();

    // ── HTTP server ──────────────────────────────────────────────────
    httplib::Server svr;

    // Security headers and CORS on every response
    svr.set_default_headers({
        {"X-Frame-Options",           "DENY"},
        {"X-Content-Type-Options",    "nosniff"},
        {"X-XSS-Protection",          "1; mode=block"},
        {"Referrer-Policy",           "strict-origin-when-cross-origin"},
        {"Access-Control-Allow-Origin",  "*"},
        {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"},
        {"Access-Control-Allow-Headers", "Content-Type"}
    });

    // Serve static files from public/
    svr.set_mount_point("/", "public");

    // API routes
    svr.Post("/query",  handle_query);
    svr.Get("/status",  handle_status);
    svr.Options(".*", [](const httplib::Request&, httplib::Response& res) {
        res.status = 200;
    });

    std::cout << "Listening on port " << PORT << std::endl;
    svr.listen("0.0.0.0", PORT);

    delete g_embeddings;
    delete g_llm;
    delete g_index;
    llama_backend_free();
    return 0;
}
