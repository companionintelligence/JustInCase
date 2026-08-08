#pragma once

// Hybrid vector + full-text search index backed by SQLite.
//
// Uses:
//   - sqlite-vec  (vec0 virtual table) for approximate nearest-neighbour search
//   - FTS5        (built-in)           for BM25 lexical search
//   - Reciprocal Rank Fusion (RRF)     to merge the two ranked lists
//
// The entire index lives in a single file (data/jic.db) — no external
// servers, perfect for the offline appliance use-case.

#include <string>
#include <vector>
#include <map>
#include <iostream>
#include <sstream>
#include <mutex>
#include <algorithm>
#include <cctype>
#include <sqlite3.h>
#include "sqlite-vec.h"
#include "types.h"
#include "config.h"

class SQLiteVecIndex {
public:
    struct SearchResult {
        int    id;
        std::string filename;
        std::string text;
        float  score;       // RRF score (higher = better)
    };

    SQLiteVecIndex() = default;
    ~SQLiteVecIndex() { if (db_) sqlite3_close(db_); }

    // ── Open / create the database ────────────────────────────────────
    bool open(const std::string& db_path) {
        // Register sqlite-vec before opening (amalgamation build)
        sqlite3_auto_extension((void(*)(void))sqlite3_vec_init);

        int rc = sqlite3_open(db_path.c_str(), &db_);
        if (rc != SQLITE_OK) {
            std::cerr << "SQLite: cannot open " << db_path << ": "
                      << sqlite3_errmsg(db_) << std::endl;
            return false;
        }

        // Performance pragmas
        exec("PRAGMA journal_mode = WAL");
        exec("PRAGMA synchronous  = NORMAL");
        exec("PRAGMA cache_size   = -64000"); // 64 MB page cache

        // ── Schema ───────────────────────────────────────────────────
        exec(R"(
            CREATE TABLE IF NOT EXISTS chunks (
                id          INTEGER PRIMARY KEY AUTOINCREMENT,
                filename    TEXT    NOT NULL,
                chunk_text  TEXT    NOT NULL,
                page_number INTEGER DEFAULT -1,
                chunk_index INTEGER DEFAULT -1,
                created_at  TEXT    DEFAULT (datetime('now'))
            )
        )");

        exec("CREATE VIRTUAL TABLE IF NOT EXISTS vec_chunks USING vec0("
             "chunk_id INTEGER PRIMARY KEY, "
             "embedding float[" + std::to_string(EMBEDDING_DIM) + "]"
             ")");

        exec(R"(
            CREATE VIRTUAL TABLE IF NOT EXISTS chunks_fts USING fts5(
                filename, chunk_text,
                content   = chunks,
                content_rowid = id
            )
        )");

        // Keep FTS in sync with the chunks table
        exec(R"(
            CREATE TRIGGER IF NOT EXISTS chunks_ai AFTER INSERT ON chunks
            BEGIN
                INSERT INTO chunks_fts(rowid, filename, chunk_text)
                VALUES (new.id, new.filename, new.chunk_text);
            END
        )");

        // The matching DELETE trigger. `chunks_fts` is an EXTERNAL-CONTENT
        // FTS5 table (content=chunks), which means it keeps its own copy of
        // the terms and does NOT notice rows leaving the content table.
        // Deleting a chunk without this leaves the term still indexed: BM25
        // goes on matching text that no longer exists and returns a rowid
        // that resolves to nothing, so a removed document keeps influencing
        // retrieval and can still be cited.
        //
        // The `('delete', ...)` command form is FTS5's required way to
        // retract a row, and it must be given the OLD column values —
        // passing anything else corrupts the index rather than fixing it.
        exec(R"(
            CREATE TRIGGER IF NOT EXISTS chunks_ad AFTER DELETE ON chunks
            BEGIN
                INSERT INTO chunks_fts(chunks_fts, rowid, filename, chunk_text)
                VALUES ('delete', old.id, old.filename, old.chunk_text);
            END
        )");

        exec(R"(
            CREATE TABLE IF NOT EXISTS processed_files (
                filename     TEXT PRIMARY KEY,
                processed_at TEXT DEFAULT (datetime('now')),
                num_chunks   INTEGER DEFAULT 0
            )
        )");

        exec(R"(
            CREATE TABLE IF NOT EXISTS index_meta (
                key   TEXT PRIMARY KEY,
                value TEXT NOT NULL
            )
        )");

        // ── What built this index ───────────────────────────────────
        //
        // `vec_chunks` is created float[EMBEDDING_DIM] with IF NOT EXISTS,
        // so an index built by an earlier model keeps its ORIGINAL width
        // forever — changing the constant and rebuilding the binary does
        // not migrate it. Worse, vectors from a different model are the
        // same width but a different SPACE: cosine distance between them
        // is meaningless, and nothing about that is visible at query time.
        //
        // Recording the width and the model name is what makes the
        // mismatch detectable at all. It is only reported, never enforced
        // here — deleting somebody's index is not this function's call.
        const std::string want_dim = std::to_string(EMBEDDING_DIM);
        const std::string want_model = get_embedding_model_name();
        const std::string had_dim   = meta_get("embedding_dim");
        const std::string had_model = meta_get("embedding_model");

        if (had_dim.empty()) {
            meta_set("embedding_dim", want_dim);
            meta_set("embedding_model", want_model);
        } else if (had_dim != want_dim || (!had_model.empty() && had_model != want_model)) {
            std::cerr
                << "\n  !! INDEX / MODEL MISMATCH — retrieval will be wrong, silently.\n"
                << "     index built with : " << had_model << " (" << had_dim << "-dim)\n"
                << "     now running      : " << want_model << " (" << want_dim << "-dim)\n"
                << "     Embeddings from a different model are not comparable even at the\n"
                << "     same width. Delete the jic-data volume and re-index:\n"
                << "       docker compose down && docker volume rm jic_jic-data\n"
                << std::endl;
            meta_set("mismatch", had_model + " (" + had_dim + ") != " +
                                 want_model + " (" + want_dim + ")");
        } else {
            meta_set("mismatch", "");
        }

        std::cout << "SQLite index opened: " << db_path
                  << "  [" << want_model << ", " << want_dim << "-dim]" << std::endl;
        return true;
    }

    /// A row from index_meta, or "" when absent. Never throws.
    std::string meta_get(const std::string& key) {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db_, "SELECT value FROM index_meta WHERE key = ?", -1, &s, nullptr)
                != SQLITE_OK)
            return "";
        sqlite3_bind_text(s, 1, key.c_str(), -1, SQLITE_TRANSIENT);
        std::string out;
        if (sqlite3_step(s) == SQLITE_ROW) {
            const char* v = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
            if (v) out = v;
        }
        sqlite3_finalize(s);
        return out;
    }

    void meta_set(const std::string& key, const std::string& value) {
        sqlite3_stmt* s = nullptr;
        if (sqlite3_prepare_v2(db_,
                "INSERT INTO index_meta(key, value) VALUES(?, ?) "
                "ON CONFLICT(key) DO UPDATE SET value = excluded.value",
                -1, &s, nullptr) != SQLITE_OK)
            return;
        sqlite3_bind_text(s, 1, key.c_str(),   -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, value.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_step(s);
        sqlite3_finalize(s);
    }

    // ── Insert ────────────────────────────────────────────────────────

    int add_chunk(const std::string& filename,
                  const std::string& text,
                  const std::vector<float>& embedding,
                  int page_number = -1,
                  int chunk_index = -1) {
        std::lock_guard<std::mutex> lock(mu_);

        // 1) metadata
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db_,
            "INSERT INTO chunks (filename, chunk_text, page_number, chunk_index) "
            "VALUES (?, ?, ?, ?)", -1, &s, nullptr);
        sqlite3_bind_text(s, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_text(s, 2, text.c_str(),     -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (s, 3, page_number);
        sqlite3_bind_int (s, 4, chunk_index);
        sqlite3_step(s);
        sqlite3_finalize(s);

        int chunk_id = static_cast<int>(sqlite3_last_insert_rowid(db_));

        // 2) embedding
        sqlite3_prepare_v2(db_,
            "INSERT INTO vec_chunks (chunk_id, embedding) VALUES (?, ?)",
            -1, &s, nullptr);
        sqlite3_bind_int (s, 1, chunk_id);
        sqlite3_bind_blob(s, 2, embedding.data(),
                          static_cast<int>(embedding.size() * sizeof(float)),
                          SQLITE_TRANSIENT);
        sqlite3_step(s);
        sqlite3_finalize(s);

        return chunk_id;
    }

    void add_batch(const std::vector<Document>& docs,
                   const std::vector<std::vector<float>>& embeddings) {
        std::lock_guard<std::mutex> lock(mu_);
        exec("BEGIN TRANSACTION");

        for (size_t i = 0; i < docs.size(); i++) {
            sqlite3_stmt* s = nullptr;

            sqlite3_prepare_v2(db_,
                "INSERT INTO chunks (filename, chunk_text, page_number, chunk_index) "
                "VALUES (?, ?, ?, ?)", -1, &s, nullptr);
            sqlite3_bind_text(s, 1, docs[i].filename.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_bind_text(s, 2, docs[i].text.c_str(),     -1, SQLITE_TRANSIENT);
            sqlite3_bind_int (s, 3, docs[i].page_number);
            sqlite3_bind_int (s, 4, docs[i].chunk_index);
            sqlite3_step(s);
            sqlite3_finalize(s);

            int cid = static_cast<int>(sqlite3_last_insert_rowid(db_));

            sqlite3_prepare_v2(db_,
                "INSERT INTO vec_chunks (chunk_id, embedding) VALUES (?, ?)",
                -1, &s, nullptr);
            sqlite3_bind_int (s, 1, cid);
            sqlite3_bind_blob(s, 2, embeddings[i].data(),
                              static_cast<int>(embeddings[i].size() * sizeof(float)),
                              SQLITE_TRANSIENT);
            sqlite3_step(s);
            sqlite3_finalize(s);
        }

        exec("COMMIT");
    }

    // ── Search ────────────────────────────────────────────────────────

    // Hybrid search: vector + BM25, merged with Reciprocal Rank Fusion.
    std::vector<SearchResult> hybrid_search(
            const std::vector<float>& query_embedding,
            const std::string& query_text,
            int top_k      = 5,
            int candidates = 20) {
        std::lock_guard<std::mutex> lock(mu_);

        const float rrf_k = 60.0f;
        std::map<int, float> rrf_scores;
        std::map<int, std::pair<std::string, std::string>> meta; // id → (file, text)

        // ── 1. Vector search ─────────────────────────────────────────
        {
            sqlite3_stmt* s = nullptr;
            sqlite3_prepare_v2(db_,
                "SELECT chunk_id, distance "
                "FROM vec_chunks "
                "WHERE embedding MATCH ? "
                "ORDER BY distance LIMIT ?",
                -1, &s, nullptr);
            sqlite3_bind_blob(s, 1, query_embedding.data(),
                              static_cast<int>(query_embedding.size() * sizeof(float)),
                              SQLITE_TRANSIENT);
            sqlite3_bind_int(s, 2, candidates);

            int rank = 1;
            while (sqlite3_step(s) == SQLITE_ROW) {
                int id = sqlite3_column_int(s, 0);
                rrf_scores[id] += 1.0f / (rrf_k + rank);
                rank++;
            }
            sqlite3_finalize(s);
        }

        // ── 2. BM25 full-text search ────────────────────────────────
        {
            std::string fts = make_fts_query(query_text);
            if (!fts.empty()) {
                sqlite3_stmt* s = nullptr;
                sqlite3_prepare_v2(db_,
                    "SELECT rowid, rank "
                    "FROM chunks_fts "
                    "WHERE chunks_fts MATCH ? "
                    "ORDER BY rank LIMIT ?",
                    -1, &s, nullptr);
                sqlite3_bind_text(s, 1, fts.c_str(), -1, SQLITE_TRANSIENT);
                sqlite3_bind_int (s, 2, candidates);

                int rank = 1;
                while (sqlite3_step(s) == SQLITE_ROW) {
                    int id = sqlite3_column_int(s, 0);
                    rrf_scores[id] += 1.0f / (rrf_k + rank);
                    rank++;
                }
                sqlite3_finalize(s);
            }
        }

        // ── 3. Fetch metadata for all candidate IDs ─────────────────
        for (auto& [id, _] : rrf_scores) {
            sqlite3_stmt* s = nullptr;
            sqlite3_prepare_v2(db_,
                "SELECT filename, chunk_text FROM chunks WHERE id = ?",
                -1, &s, nullptr);
            sqlite3_bind_int(s, 1, id);
            if (sqlite3_step(s) == SQLITE_ROW) {
                const char* fn = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
                const char* tx = reinterpret_cast<const char*>(sqlite3_column_text(s, 1));
                meta[id] = {fn ? fn : "", tx ? tx : ""};
            }
            sqlite3_finalize(s);
        }

        // ── 4. Sort by RRF score, return top_k ──────────────────────
        std::vector<std::pair<int, float>> scored(rrf_scores.begin(), rrf_scores.end());
        std::sort(scored.begin(), scored.end(),
                  [](auto& a, auto& b) { return a.second > b.second; });

        std::vector<SearchResult> results;
        for (int i = 0; i < std::min(top_k, static_cast<int>(scored.size())); i++) {
            int id = scored[i].first;
            auto& [fn, tx] = meta[id];
            results.push_back({id, fn, tx, scored[i].second});
        }
        return results;
    }

    // Vector-only search (fallback when query text is too short for BM25)
    std::vector<SearchResult> vector_search(
            const std::vector<float>& query_embedding, int top_k = 5) {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<SearchResult> results;

        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db_,
            "SELECT chunk_id, distance "
            "FROM vec_chunks "
            "WHERE embedding MATCH ? "
            "ORDER BY distance LIMIT ?",
            -1, &s, nullptr);
        sqlite3_bind_blob(s, 1, query_embedding.data(),
                          static_cast<int>(query_embedding.size() * sizeof(float)),
                          SQLITE_TRANSIENT);
        sqlite3_bind_int(s, 2, top_k);

        while (sqlite3_step(s) == SQLITE_ROW) {
            int   id   = sqlite3_column_int   (s, 0);
            float dist = static_cast<float>(sqlite3_column_double(s, 1));

            // Fetch metadata
            sqlite3_stmt* m = nullptr;
            sqlite3_prepare_v2(db_,
                "SELECT filename, chunk_text FROM chunks WHERE id = ?",
                -1, &m, nullptr);
            sqlite3_bind_int(m, 1, id);
            if (sqlite3_step(m) == SQLITE_ROW) {
                const char* fn = reinterpret_cast<const char*>(sqlite3_column_text(m, 0));
                const char* tx = reinterpret_cast<const char*>(sqlite3_column_text(m, 1));
                results.push_back({id, fn ? fn : "", tx ? tx : "",
                                   1.0f / (1.0f + dist)});
            }
            sqlite3_finalize(m);
        }
        sqlite3_finalize(s);
        return results;
    }

    // ── Bookkeeping ──────────────────────────────────────────────────

    bool is_file_processed(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mu_);
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db_,
            "SELECT 1 FROM processed_files WHERE filename = ?",
            -1, &s, nullptr);
        sqlite3_bind_text(s, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
        bool exists = (sqlite3_step(s) == SQLITE_ROW);
        sqlite3_finalize(s);
        return exists;
    }

    void mark_file_processed(const std::string& filename, int num_chunks) {
        std::lock_guard<std::mutex> lock(mu_);
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db_,
            "INSERT OR REPLACE INTO processed_files (filename, num_chunks) "
            "VALUES (?, ?)", -1, &s, nullptr);
        sqlite3_bind_text(s, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
        sqlite3_bind_int (s, 2, num_chunks);
        sqlite3_step(s);
        sqlite3_finalize(s);
    }

    // One row per file the ingestion worker has seen, for the library view.
    struct LibraryEntry {
        std::string filename;
        int         num_chunks;
        std::string processed_at;
    };

    std::vector<LibraryEntry> list_processed_files() {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<LibraryEntry> entries;
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db_,
            "SELECT filename, num_chunks, processed_at "
            "FROM processed_files ORDER BY filename",
            -1, &s, nullptr);
        while (sqlite3_step(s) == SQLITE_ROW) {
            const char* fn = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
            const char* at = reinterpret_cast<const char*>(sqlite3_column_text(s, 2));
            entries.push_back({fn ? fn : "",
                               sqlite3_column_int(s, 1),
                               at ? at : ""});
        }
        sqlite3_finalize(s);
        return entries;
    }

    /**
     * Remove every trace of one document: its vectors, its chunks (which
     * retracts its FTS terms through the chunks_ad trigger) and its
     * processed_files row.
     *
     * Until this existed the index was APPEND-ONLY. Deleting a PDF from the
     * sources volume left its chunks behind forever, so `/query` went on
     * retrieving from a document that is not there and citing it — and the
     * citation link 404s, because the file it points at is gone. On an
     * appliance whose entire promise is "answers cite their sources", a
     * citation to a document the user deleted is the worst possible failure:
     * it looks exactly like a working one.
     *
     * Returns the number of chunks removed.
     *
     * ORDER MATTERS. vec_chunks is keyed by chunk id and those ids are
     * resolved from `chunks`, so the vectors must go FIRST — deleting the
     * chunks first would strand every vector with no way left to find it.
     */
    int remove_file(const std::string& filename) {
        std::lock_guard<std::mutex> lock(mu_);

        int removed = 0;
        {
            sqlite3_stmt* s = nullptr;
            sqlite3_prepare_v2(db_,
                "SELECT COUNT(*) FROM chunks WHERE filename = ?", -1, &s, nullptr);
            sqlite3_bind_text(s, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
            if (sqlite3_step(s) == SQLITE_ROW) removed = sqlite3_column_int(s, 0);
            sqlite3_finalize(s);
        }
        if (removed == 0) return 0;

        exec("BEGIN");

        // 1. Vectors, while `chunks` can still resolve their ids.
        {
            sqlite3_stmt* s = nullptr;
            sqlite3_prepare_v2(db_,
                "DELETE FROM vec_chunks WHERE chunk_id IN "
                "(SELECT id FROM chunks WHERE filename = ?)", -1, &s, nullptr);
            sqlite3_bind_text(s, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(s);
            sqlite3_finalize(s);
        }

        // 2. Chunks — the chunks_ad trigger retracts the FTS terms.
        {
            sqlite3_stmt* s = nullptr;
            sqlite3_prepare_v2(db_, "DELETE FROM chunks WHERE filename = ?", -1, &s, nullptr);
            sqlite3_bind_text(s, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(s);
            sqlite3_finalize(s);
        }

        // 3. The library listing.
        {
            sqlite3_stmt* s = nullptr;
            sqlite3_prepare_v2(db_, "DELETE FROM processed_files WHERE filename = ?", -1, &s, nullptr);
            sqlite3_bind_text(s, 1, filename.c_str(), -1, SQLITE_TRANSIENT);
            sqlite3_step(s);
            sqlite3_finalize(s);
        }

        exec("COMMIT");
        return removed;
    }

    /** Every filename the index believes it holds. */
    std::vector<std::string> indexed_filenames() {
        std::lock_guard<std::mutex> lock(mu_);
        std::vector<std::string> out;
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db_, "SELECT filename FROM processed_files", -1, &s, nullptr);
        while (sqlite3_step(s) == SQLITE_ROW) {
            const char* fn = reinterpret_cast<const char*>(sqlite3_column_text(s, 0));
            if (fn) out.push_back(fn);
        }
        sqlite3_finalize(s);
        return out;
    }

    int chunk_count() {
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db_, "SELECT COUNT(*) FROM chunks", -1, &s, nullptr);
        int n = 0;
        if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int(s, 0);
        sqlite3_finalize(s);
        return n;
    }

    int processed_file_count() {
        sqlite3_stmt* s = nullptr;
        sqlite3_prepare_v2(db_,
            "SELECT COUNT(*) FROM processed_files", -1, &s, nullptr);
        int n = 0;
        if (sqlite3_step(s) == SQLITE_ROW) n = sqlite3_column_int(s, 0);
        sqlite3_finalize(s);
        return n;
    }

private:
    sqlite3*    db_ = nullptr;
    std::mutex  mu_;

    bool exec(const std::string& sql) {
        char* err = nullptr;
        int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err);
        if (rc != SQLITE_OK) {
            std::cerr << "SQLite error: " << (err ? err : "unknown")
                      << "\n  SQL: " << sql.substr(0, 200) << std::endl;
            sqlite3_free(err);
            return false;
        }
        return true;
    }

    // Turn a natural-language query into an FTS5 query (words joined with OR).
    std::string make_fts_query(const std::string& text) {
        std::istringstream iss(text);
        std::string word;
        std::vector<std::string> words;
        while (iss >> word) {
            std::string clean;
            for (char c : word)
                if (std::isalnum(static_cast<unsigned char>(c))) clean += c;
            if (clean.length() >= 2) words.push_back(clean);
        }
        if (words.empty()) return "";
        std::ostringstream q;
        for (size_t i = 0; i < words.size(); i++) {
            if (i) q << " OR ";
            q << words[i];
        }
        return q.str();
    }
};
