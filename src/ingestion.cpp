// ── JIC Ingestion Service ────────────────────────────────────────────
//
// Continuously scans the sources directory for new PDFs and text files,
// extracts text with MuPDF, splits into semantic chunks, generates
// embeddings, and stores everything in SQLite (vec + FTS5).
//
// No external services required — Tika is gone, curl is gone.

#include <iostream>
#include <fstream>
#include <sstream>
#include <set>
#include <thread>
#include <chrono>
#include <atomic>
#include <csignal>
#include <filesystem>

#include "llama.h"
#include "nlohmann/json.hpp"
#include "config.h"
#include "types.h"
#include "text_utils.h"
#include "pdf_utils.h"
#include "embeddings.h"
#include "sqlite_vec_index.h"

namespace fs = std::filesystem;

static std::atomic<bool> g_running{true};

static void handle_shutdown_signal(int) { g_running.store(false); }

// Files that were modified moments ago may still be mid-download (the
// content fetcher writes *.part then renames, but users also copy files
// straight into the volume) — let them settle before ingesting.
static bool file_is_settled(const fs::path& p) {
    std::error_code ec;
    auto mtime = fs::last_write_time(p, ec);
    if (ec) return false;
    auto age = fs::file_time_type::clock::now() - mtime;
    return age > std::chrono::seconds(FILE_SETTLE_SECONDS);
}

// Sleep in short slices so SIGTERM interrupts promptly.
static void interruptible_sleep(int seconds) {
    for (int i = 0; i < seconds * 4 && g_running.load(); i++)
        std::this_thread::sleep_for(std::chrono::milliseconds(250));
}

int main() {
    std::cout << "═══════════════════════════════════════════" << std::endl;
    std::cout << "  JIC Ingestion " << JIC_VERSION
              << " (MuPDF + SQLite)" << std::endl;
    std::cout << "═══════════════════════════════════════════" << std::endl;

    std::signal(SIGINT,  handle_shutdown_signal);
    std::signal(SIGTERM, handle_shutdown_signal);

    // ── Initialise llama backend ─────────────────────────────────────
    llama_backend_init();
    llama_log_set([](enum ggml_log_level level, const char* text, void*) {
        if (level >= GGML_LOG_LEVEL_ERROR) fprintf(stderr, "%s", text);
    }, nullptr);

    // ── Embedding model ──────────────────────────────────────────────
    EmbeddingGenerator embeddings;
    if (!embeddings.init()) {
        std::cerr << "Failed to initialise embedding model" << std::endl;
        return 1;
    }

    // ── SQLite index ─────────────────────────────────────────────────
    const std::string db_path = get_db_path();
    fs::create_directories(fs::path(db_path).parent_path());
    SQLiteVecIndex index;
    if (!index.open(db_path)) {
        std::cerr << "Failed to open database: " << db_path << std::endl;
        return 1;
    }

    const std::string sources_dir = get_sources_dir();
    const int scan_interval = get_scan_interval_sec();

    std::cout << "Existing index: " << index.chunk_count() << " chunks, "
              << index.processed_file_count() << " files" << std::endl;
    std::cout << "Watching " << sources_dir << " every "
              << scan_interval << "s" << std::endl;

    // ── Main ingestion loop ──────────────────────────────────────────
    while (g_running.load()) {
        std::vector<std::pair<std::string, std::string>> files_to_process;

        // Discover new files
        if (fs::exists(sources_dir)) {
            try {
                for (const auto& entry : fs::recursive_directory_iterator(sources_dir)) {
                    if (!entry.is_regular_file()) continue;

                    // Skip hidden files and in-flight downloads
                    std::string name = entry.path().filename().string();
                    if (!name.empty() && name[0] == '.') continue;

                    std::string ext = entry.path().extension().string();
                    for (auto& c : ext) c = std::tolower(static_cast<unsigned char>(c));
                    if (ext != ".pdf" && ext != ".txt") continue;

                    std::string rel = fs::relative(entry.path(), sources_dir).string();
                    if (index.is_file_processed(rel)) continue;

                    if (!file_is_settled(entry.path())) continue; // retry next scan

                    if (is_file_processable(entry.path().string())) {
                        files_to_process.push_back({entry.path().string(), rel});
                    } else {
                        std::cerr << "Skipping (too large): " << rel << std::endl;
                        index.mark_file_processed(rel, 0);
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Error scanning sources: " << e.what() << std::endl;
            }
        }

        if (!files_to_process.empty()) {
            std::cout << "\nFound " << files_to_process.size()
                      << " new file(s) to process" << std::endl;

            for (const auto& [full_path, rel_path] : files_to_process) {
                if (!g_running.load()) break;
                try {
                    std::cout << "\n── Processing: " << rel_path << std::endl;

                    // ── Extract text ─────────────────────────────────
                    std::string text;
                    if (string_ends_with(full_path, ".pdf")) {
                        text = extract_pdf_text(full_path);
                        if (text.empty()) {
                            std::cerr << "No text extracted from " << rel_path << std::endl;
                            index.mark_file_processed(rel_path, 0);
                            continue;
                        }
                    } else {
                        text = extract_text_file(full_path);
                    }

                    // Cap extremely large texts
                    if (text.length() > MAX_DOCUMENT_CHARS) {
                        std::cout << "Text truncated from " << text.length()
                                  << " to " << MAX_DOCUMENT_CHARS << " chars" << std::endl;
                        text.resize(MAX_DOCUMENT_CHARS);
                    }

                    // ── Chunk ────────────────────────────────────────
                    auto chunks = split_text(text);
                    if (chunks.empty()) {
                        index.mark_file_processed(rel_path, 0);
                        continue;
                    }

                    // ── Embed & store in batches ─────────────────────
                    const int BATCH = 50;
                    std::vector<Document> batch_docs;
                    std::vector<std::vector<float>> batch_embs;
                    int total_stored = 0;

                    for (int ci = 0; ci < static_cast<int>(chunks.size()); ci++) {
                        if (!g_running.load()) break;
                        auto emb = embeddings.get_embedding(chunks[ci]);
                        if (static_cast<int>(emb.size()) != EMBEDDING_DIM) continue;

                        batch_docs.push_back({rel_path, chunks[ci], -1, ci});
                        batch_embs.push_back(std::move(emb));

                        if (static_cast<int>(batch_docs.size()) >= BATCH) {
                            index.add_batch(batch_docs, batch_embs);
                            total_stored += batch_docs.size();
                            batch_docs.clear();
                            batch_embs.clear();

                            std::cout << "  " << total_stored << "/"
                                      << chunks.size() << " chunks stored" << std::endl;

                            // Small pause to let the server breathe
                            std::this_thread::sleep_for(std::chrono::milliseconds(200));
                        }
                    }

                    // Flush remainder
                    if (!batch_docs.empty()) {
                        index.add_batch(batch_docs, batch_embs);
                        total_stored += batch_docs.size();
                    }

                    // Interrupted mid-document: leave it unmarked so the
                    // next run re-ingests it completely.
                    if (!g_running.load() &&
                        total_stored < static_cast<int>(chunks.size())) {
                        std::cout << "  Interrupted — " << rel_path
                                  << " will be re-processed on restart" << std::endl;
                        break;
                    }

                    index.mark_file_processed(rel_path, total_stored);
                    std::cout << "  ✓ " << rel_path << ": " << total_stored
                              << " chunks indexed" << std::endl;

                } catch (const std::exception& e) {
                    std::cerr << "Error processing " << rel_path << ": "
                              << e.what() << std::endl;
                    index.mark_file_processed(rel_path, 0);
                }
            }

            std::cout << "\nIngestion batch complete.  Total chunks: "
                      << index.chunk_count() << std::endl;
        }

        // Sleep before next scan
        interruptible_sleep(scan_interval);
    }

    std::cout << "Ingestion service stopped." << std::endl;
    llama_backend_free();
    return 0;
}
