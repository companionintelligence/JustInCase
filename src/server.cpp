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

static SQLiteVecIndex*      g_index      = nullptr;   // set once before listen()

// The models are loaded by a background thread (see model_loader) while request
// handlers may already be running on cpp-httplib's worker pool. These pointers
// only ever transition null → loaded exactly once and never revert, so plain
// acquire/release atomics are a correct, lock-free way to publish them: a reader
// sees either null (→ 503 / no context) or a fully-constructed object.
static std::atomic<EmbeddingGenerator*> g_embeddings{nullptr};
static std::atomic<LLMGenerator*>       g_llm{nullptr};

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
    // /status stay reachable, but /query needs the LLM. Load the pointer
    // once here and use that local for the rest of the request (it cannot
    // revert to null), so there is no check-then-use race with the loader.
    LLMGenerator* llm = g_llm.load(std::memory_order_acquire);
    if (!llm) {
        // Point at the *resolved* model directory (honours JIC_GGUF_DIR), the
        // same value /status reports, rather than a hardcoded path.
        send_error(res, 503,
                   "Language model not loaded yet — it loads automatically once "
                   "the GGUF files are present in " + resolved_gguf_dir() +
                   ". The document library stays browsable in the meantime.");
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

        EmbeddingGenerator* emb = g_embeddings.load(std::memory_order_acquire);
        if (use_context && emb && g_chunk_count.load() > 0) {
            auto q_emb = emb->get_embedding(query);

            // Empty = the query could not be embedded; skip vector search
            // rather than feeding a bad vector into the index (answer the
            // question without retrieved context).
            if (!q_emb.empty()) {
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
        std::string answer = llm->generate(prompt);

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

// Cheap, never-throwing existence check for /status (the file may sit on a
// missing or read-only mount; error_code overload keeps this safe to poll).
static bool model_file_present(const std::string& path) {
    std::error_code ec;
    return fs::exists(path, ec) && !ec;
}

static void handle_status(const httplib::Request&, httplib::Response& res) {
    auto uptime = std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::steady_clock::now() - g_start_time).count();

    json status;
    status["version"]            = JIC_VERSION;
    status["uptime_seconds"]     = uptime;
    status["documents_indexed"]  = g_chunk_count.load();
    status["files_processed"]    = g_file_count.load();
    status["llm_loaded"]         = (g_llm.load() != nullptr);
    status["embeddings_loaded"]  = (g_embeddings.load() != nullptr);
    status["llm_model"]          = get_llm_model_name();
    status["embedding_model"]    = get_embedding_model_name();
    // Diagnostics: where the server is actually looking for models, and
    // whether the GGUF files are present there — so "LLM missing" is
    // self-explanatory (wrong mount path vs. files not yet provisioned).
    status["gguf_dir"]               = resolved_gguf_dir();
    status["llm_gguf_present"]       = model_file_present(get_llm_model_path());
    status["embedding_gguf_present"] = model_file_present(get_embedding_model_path());
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
// Background thread: load the GGUF models as soon as they are available
//
// Models are large, host-provisioned files that may not be present when the
// container starts (fresh install, the CI publish gate's empty gguf_models/,
// or files dropped into the volume later). Rather than fail or block startup,
// the server comes up immediately in degraded mode and this thread keeps
// trying to load each model until it succeeds — so provisioning the GGUF
// files "just works" without a restart, exactly as the ingestion service
// auto-indexes new documents.
// ═════════════════════════════════════════════════════════════════════

// A model is safe to load only once its file exists AND has stopped changing
// (mtime older than the settle window): this avoids mmap-ing a file that is
// still being copied in, which could later fault with SIGBUS on access.
static bool model_file_ready(const std::string& path) {
    std::error_code ec;
    if (!fs::exists(path, ec) || ec) return false;
    auto mtime = fs::last_write_time(path, ec);
    if (ec) return false;
    return (fs::file_time_type::clock::now() - mtime)
               > std::chrono::seconds(FILE_SETTLE_SECONDS);
}

static void model_loader() {
    bool warned_emb = false,      warned_llm = false;       // "waiting for file"
    bool warned_emb_fail = false, warned_llm_fail = false;  // "present but won't load"

    while (g_running.load()) {
        // Load each model independently and only into an *empty* slot — never
        // reload over an already-loaded pointer (that would leak a multi-GB
        // model on every scan). Publish with a release store so a concurrent
        // handler that acquire-loads a non-null pointer sees a fully-built
        // object. On failure, delete the local and leave the slot null.
        if (g_running.load() &&
            g_embeddings.load(std::memory_order_acquire) == nullptr) {
            const std::string path = get_embedding_model_path();
            if (model_file_ready(path)) {
                auto* e = new EmbeddingGenerator();
                if (e->init()) {
                    g_embeddings.store(e, std::memory_order_release);
                    std::cout << "Embedding model loaded — semantic search enabled ("
                              << describe_model_path(path) << ")" << std::endl;
                } else {
                    delete e;   // frees any partial state; retry next scan
                    if (!warned_emb_fail) {   // don't re-log every scan
                        std::cerr << "Embedding model present but failed to load "
                                     "(corrupt or incompatible?) — "
                                  << describe_model_path(path)
                                  << "; will keep retrying" << std::endl;
                        warned_emb_fail = true;
                    }
                }
            } else if (!warned_emb) {
                std::cout << "Waiting for embedding model — "
                          << describe_model_path(path) << std::endl;
                warned_emb = true;
            }
        }

        // Re-check g_running between the two loads: a model load can take
        // seconds and cannot be interrupted mid-flight, so a SIGTERM that
        // arrives during the embedding load must not then kick off the (much
        // larger) LLM load and blow past Docker's stop grace.
        if (g_running.load() &&
            g_llm.load(std::memory_order_acquire) == nullptr) {
            const std::string path = get_llm_model_path();
            if (model_file_ready(path)) {
                auto* l = new LLMGenerator();
                if (l->init()) {
                    g_llm.store(l, std::memory_order_release);
                    std::cout << "LLM loaded — /query now available ("
                              << describe_model_path(path) << ")" << std::endl;
                } else {
                    delete l;
                    if (!warned_llm_fail) {   // don't re-log every scan
                        std::cerr << "LLM present but failed to load "
                                     "(corrupt or incompatible?) — "
                                  << describe_model_path(path)
                                  << "; will keep retrying" << std::endl;
                        warned_llm_fail = true;
                    }
                }
            } else if (!warned_llm) {
                std::cout << "Waiting for LLM — "
                          << describe_model_path(path) << std::endl;
                warned_llm = true;
            }
        }

        // Nothing left to do once both are up.
        if (g_embeddings.load(std::memory_order_acquire) != nullptr &&
            g_llm.load(std::memory_order_acquire) != nullptr)
            break;

        // Interruptible wait: poll g_running every 250 ms so shutdown is prompt.
        const int wait = get_scan_interval_sec();
        for (int i = 0; i < wait * 4 && g_running.load(); i++)
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
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

    // ── Models (loaded in the background) ─────────────────────────────
    // Missing GGUF models must not kill or stall the server: the CI publish
    // gate (and a fresh install before models are provisioned) runs the
    // container with an empty gguf_models/. The server serves the UI and
    // /status immediately; the model_loader thread loads the models as soon
    // as they appear — no restart required — and /query answers 503 until
    // then. Reporting the resolved directory up front makes a mis-pointed
    // bind mount obvious in the logs instead of a silent degraded run.
    std::cout << "Model directory: " << resolved_gguf_dir()
              << " — models load in the background as they become available"
              << std::endl;

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

    // Background workers (joined on shutdown): count refresher + model loader.
    // The loader is started before listen() so the UI/degraded mode is served
    // immediately while models load; its first iteration attempts a load right
    // away, so models already present at boot come up promptly.
    std::thread refresher(count_refresher);
    std::thread loader(model_loader);

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
    loader.join();   // finishes any in-flight load, then exits (see model_loader)

    // Safe to delete now: listen() has joined every request-handler worker,
    // and the loader thread is joined, so nothing else touches these pointers.
    delete g_embeddings.load();
    delete g_llm.load();
    delete g_index;
    llama_backend_free();
    std::cout << "Bye." << std::endl;
    return 0;
}
