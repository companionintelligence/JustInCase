// ── JIC Server ───────────────────────────────────────────────────────
//
// Serves the web UI and handles /query (RAG), /status and /api/library.
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
#include <csignal>
#include <cctype>

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

static httplib::Server*     g_server     = nullptr;
static std::atomic<bool>    g_running{true};

static const auto g_start_time = std::chrono::steady_clock::now();

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

// Drop conversations idle for over an hour, and bound the map so a
// client minting fresh conversation IDs cannot grow memory unbounded.
static void prune_conversations_locked() {
    auto now = std::chrono::system_clock::now();
    for (auto it = g_conversations.begin(); it != g_conversations.end(); ) {
        auto age = std::chrono::duration_cast<std::chrono::hours>(
                now - it->second.last_activity).count();
        it = (age >= 1) ? g_conversations.erase(it) : std::next(it);
    }
    while (g_conversations.size() > MAX_CONVERSATIONS) {
        auto oldest = g_conversations.begin();
        for (auto it = g_conversations.begin(); it != g_conversations.end(); ++it)
            if (it->second.last_activity < oldest->second.last_activity)
                oldest = it;
        g_conversations.erase(oldest);
    }
}

// ═════════════════════════════════════════════════════════════════════
// Request validation
// ═════════════════════════════════════════════════════════════════════

static bool valid_conversation_id(const std::string& id) {
    if (id.empty() || id.size() > MAX_CONV_ID_CHARS) return false;
    for (char c : id) {
        if (!std::isalnum(static_cast<unsigned char>(c)) &&
            c != '_' && c != '-') return false;
    }
    return true;
}

static void send_error(httplib::Response& res, int status, const std::string& msg) {
    res.status = status;
    res.set_content(json({{"error", msg}}).dump(), "application/json");
}

// ═════════════════════════════════════════════════════════════════════
// /query handler
// ═════════════════════════════════════════════════════════════════════

static void handle_query(const httplib::Request& req, httplib::Response& res) {
    // Degraded mode: the server runs without GGUF models so the UI and
    // /status stay reachable, but /query needs the LLM.
    if (!g_llm) {
        send_error(res, 503,
                   "LLM model not loaded. Place the GGUF models in "
                   "gguf_models/ and restart the container.");
        return;
    }

    // ── Parse & validate input (client errors → 400, not 500) ───────
    json rj;
    try {
        rj = json::parse(req.body);
    } catch (const std::exception&) {
        send_error(res, 400, "Request body must be valid JSON");
        return;
    }

    if (!rj.contains("query") || !rj["query"].is_string()) {
        send_error(res, 400, "Missing required string field: query");
        return;
    }
    std::string query = rj["query"];
    if (query.empty() || query.size() > MAX_QUERY_CHARS) {
        send_error(res, 400, "query must be between 1 and " +
                   std::to_string(MAX_QUERY_CHARS) + " characters");
        return;
    }

    std::string conv_id = "default";
    if (rj.contains("conversation_id") && rj["conversation_id"].is_string()) {
        conv_id = rj["conversation_id"];
        if (!valid_conversation_id(conv_id)) {
            send_error(res, 400, "conversation_id may only contain "
                       "letters, digits, '_' and '-' (max 128 chars)");
            return;
        }
    }

    bool use_context = true;
    if (rj.contains("use_context") && rj["use_context"].is_boolean())
        use_context = rj["use_context"];

    try {
        json response;
        response["conversation_id"] = conv_id;

        // ── Retrieve conversation history ───────────────────────────
        std::vector<std::pair<std::string, std::string>> history;
        {
            std::lock_guard<std::mutex> lock(g_conv_mutex);
            prune_conversations_locked();
            auto it = g_conversations.find(conv_id);
            if (it != g_conversations.end()) history = it->second.messages;
        }

        // ── Build context from documents ────────────────────────────
        std::string context;
        json matches = json::array();

        if (use_context && g_embeddings && g_chunk_count.load() > 0) {
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
        send_error(res, 500, "Internal error while answering the query");
    }
}

// ═════════════════════════════════════════════════════════════════════
// /status handler (uses cached counts)
// ═════════════════════════════════════════════════════════════════════

static void handle_status(const httplib::Request&, httplib::Response& res) {
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - g_start_time).count();

    json status;
    status["version"]            = JIC_VERSION;
    status["uptime_seconds"]     = uptime;
    status["documents_indexed"]  = g_chunk_count.load();
    status["files_processed"]    = g_file_count.load();
    status["llm_loaded"]         = (g_llm != nullptr);
    status["embeddings_loaded"]  = (g_embeddings != nullptr);
    status["llm_model"]          = get_llm_model_name();
    status["embedding_model"]    = get_embedding_model_name();
    res.set_content(status.dump(), "application/json");
}

// ═════════════════════════════════════════════════════════════════════
// /api/library handler — what is actually in the knowledge base
// ═════════════════════════════════════════════════════════════════════

static void handle_library(const httplib::Request&, httplib::Response& res) {
    json files = json::array();
    int total_chunks = 0;

    if (g_index) {
        const fs::path sources_root = get_sources_dir();
        for (const auto& e : g_index->list_processed_files()) {
            std::error_code ec;
            auto size = fs::file_size(sources_root / e.filename, ec);

            // Category = first path component ("100_Survival/x.pdf")
            std::string category = "Uncategorized";
            auto slash = e.filename.find('/');
            if (slash != std::string::npos && slash > 0)
                category = e.filename.substr(0, slash);

            files.push_back({
                {"filename",   e.filename},
                {"category",   category},
                {"chunks",     e.num_chunks},
                {"indexed_at", e.processed_at},
                {"size_bytes", ec ? 0 : static_cast<long long>(size)},
                {"status",     e.num_chunks > 0 ? "indexed" : "skipped"}
            });
            total_chunks += e.num_chunks;
        }
    }

    json out;
    out["files"]        = files;
    out["total_files"]  = files.size();
    out["total_chunks"] = total_chunks;
    res.set_content(out.dump(), "application/json");
}

// ═════════════════════════════════════════════════════════════════════
// Background thread: refresh cached counts every 10 s
// ═════════════════════════════════════════════════════════════════════

static void count_refresher() {
    int tick = 0;
    while (g_running.load()) {
        if (tick % 20 == 0) refresh_counts();   // every 10 s
        tick++;
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
}

// ═════════════════════════════════════════════════════════════════════
// main
// ═════════════════════════════════════════════════════════════════════

static void handle_shutdown_signal(int) {
    g_running.store(false);
    if (g_server) g_server->stop();
}

int main() {
    std::cout << "═══════════════════════════════════════════" << std::endl;
    std::cout << "  JIC Server " << JIC_VERSION
              << "  (SQLite hybrid search)" << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;

    std::signal(SIGPIPE, SIG_IGN);
    std::signal(SIGINT,  handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);

    // ── llama backend ────────────────────────────────────────────────
    llama_backend_init();
    llama_log_set([](enum ggml_log_level level, const char* text, void*) {
        if (level >= GGML_LOG_LEVEL_ERROR) fprintf(stderr, "%s", text);
    }, nullptr);
    ggml_backend_load_all();

    // ── Models ───────────────────────────────────────────────────────
    std::cout << "Loading models..." << std::endl;

    // Missing GGUF models must not kill the server: the CI publish gate (and
    // a fresh install before models are provisioned) runs the container with
    // an empty gguf_models/. Serve the UI and /status regardless; /query
    // answers 503 until the models are in place.
    g_embeddings = new EmbeddingGenerator();
    if (!g_embeddings->init()) {
        std::cerr << "Embeddings init failed — continuing in degraded mode "
                     "(semantic search disabled)" << std::endl;
        delete g_embeddings;
        g_embeddings = nullptr;
    }

    g_llm = new LLMGenerator();
    if (!g_llm->init()) {
        std::cerr << "LLM init failed — continuing in degraded mode "
                     "(/query returns 503)" << std::endl;
        delete g_llm;
        g_llm = nullptr;
    }

    // ── SQLite index ─────────────────────────────────────────────────
    const std::string db_path = get_db_path();
    fs::create_directories(fs::path(db_path).parent_path());
    g_index = new SQLiteVecIndex();
    if (!g_index->open(db_path)) {
        std::cerr << "DB open failed: " << db_path << std::endl;
        return 1;
    }

    refresh_counts();
    std::cout << "Index: " << g_chunk_count.load() << " chunks, "
              << g_file_count.load() << " files" << std::endl;

    // Background count refresher (joined on shutdown)
    std::thread refresher(count_refresher);

    // ── HTTP server ──────────────────────────────────────────────────
    httplib::Server svr;
    g_server = &svr;

    // Reject oversized request bodies before buffering them
    svr.set_payload_max_length(MAX_REQUEST_BODY);
    svr.set_read_timeout(15, 0);
    svr.set_write_timeout(60, 0);

    // Security headers on every response
    httplib::Headers default_headers = {
        {"X-Frame-Options",        "DENY"},
        {"X-Content-Type-Options", "nosniff"},
        {"Referrer-Policy",        "strict-origin-when-cross-origin"}
    };

    // Cross-origin access is opt-in (JIC_CORS_ORIGIN); the bundled UI is
    // same-origin and needs nothing.
    const std::string cors_origin = get_cors_origin();
    if (!cors_origin.empty()) {
        default_headers.emplace("Access-Control-Allow-Origin",  cors_origin);
        default_headers.emplace("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        default_headers.emplace("Access-Control-Allow-Headers", "Content-Type");
        std::cout << "CORS enabled for origin: " << cors_origin << std::endl;
    }
    svr.set_default_headers(default_headers);

    // CSP for HTML only — a strict policy here would break in-browser PDF
    // viewing of /sources/ files in some browsers.
    svr.set_post_routing_handler([](const httplib::Request&, httplib::Response& res) {
        auto ct = res.get_header_value("Content-Type");
        if (ct.find("text/html") != std::string::npos) {
            res.set_header("Content-Security-Policy",
                "default-src 'self'; script-src 'self'; style-src 'self'; "
                "img-src 'self' data:; connect-src 'self'; font-src 'self'; "
                "object-src 'none'; base-uri 'self'; form-action 'self'; "
                "frame-ancestors 'none'");
        }
    });

    // Serve static files from public/
    svr.set_mount_point("/", "public");

    // API routes
    svr.Post("/query",       handle_query);
    svr.Get ("/status",      handle_status);
    svr.Get ("/api/library", handle_library);
    svr.Options(".*", [&cors_origin](const httplib::Request&, httplib::Response& res) {
        res.status = cors_origin.empty() ? 405 : 204;
    });

    std::cout << "Listening on port " << PORT << std::endl;
    svr.listen("0.0.0.0", PORT);

    // ── Shutdown (SIGTERM/SIGINT → svr.stop() → fall through) ───────
    std::cout << "Shutting down..." << std::endl;
    g_running.store(false);
    refresher.join();

    delete g_embeddings;
    delete g_llm;
    delete g_index;
    llama_backend_free();
    std::cout << "Bye." << std::endl;
    return 0;
}
