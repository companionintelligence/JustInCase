#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <iostream>
#include <algorithm>
#include "llama.h"
#include "config.h"

class EmbeddingGenerator {
private:
    llama_model*   model = nullptr;
    llama_context* ctx   = nullptr;
    std::mutex     mutex;
    int n_past = 0;

    void reset_context() {
        if (ctx) llama_free(ctx);

        llama_context_params p = llama_context_default_params();
        p.n_ctx          = 8192;
        p.n_batch        = 2048;
        p.n_ubatch       = 2048;
        p.embeddings     = true;
        p.pooling_type   = LLAMA_POOLING_TYPE_MEAN;
        p.n_threads      = 4;
        p.n_threads_batch = 4;

        ctx = llama_init_from_model(model, p);
        n_past = 0;
    }

public:
    ~EmbeddingGenerator() {
        if (ctx)   llama_free(ctx);
        if (model) llama_model_free(model);
    }

    bool init() {
        // Idempotent: the retry paths (server loader, ingestion wait-loop) may
        // call this repeatedly. Free any partial state from a prior failed
        // attempt so a reload never leaks a model/context.
        if (ctx)   { llama_free(ctx);         ctx   = nullptr; }
        if (model) { llama_model_free(model); model = nullptr; }

        llama_model_params mp = llama_model_default_params();
        model = llama_model_load_from_file(
                get_embedding_model_path().c_str(), mp);
        if (!model) {
            std::cerr << "Failed to load embedding model from "
                      << get_embedding_model_path() << std::endl;
            return false;
        }
        reset_context();
        return ctx != nullptr;
    }

    // Returns an EMBEDDING_DIM-length vector on success, or an EMPTY vector if
    // the text could not be embedded (context / tokenise / decode failure).
    // Callers MUST treat empty as "no embedding" and skip it — never store a
    // zero vector as if it were real, which silently poisons the search index.
    std::vector<float> get_embedding(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex);

        // Defensive: if a prior reset failed and left ctx null, don't call into
        // llama with a null context (crash). Try once to rebuild it first.
        if (!ctx) {
            reset_context();
            if (!ctx) return {};
        }

        // Reset context if it's getting full
        int n_ctx = llama_n_ctx(ctx);
        if (n_past > n_ctx * 3 / 4) {
            reset_context();
            if (!ctx) return {};   // could not embed → empty; caller skips it
        }

        const llama_vocab* vocab = llama_model_get_vocab(model);

        // Tokenize
        int n_prompt = -llama_tokenize(
                vocab, text.c_str(), text.size(), NULL, 0, true, true);
        if (n_prompt <= 0)
            return {};   // could not embed → empty; caller skips it
        if (n_prompt > 2048) n_prompt = 2048;

        std::vector<llama_token> tokens(n_prompt);
        int actual = llama_tokenize(
                vocab, text.c_str(), text.size(),
                tokens.data(), tokens.size(), true, true);
        if (actual < 0)
            return {};   // could not embed → empty; caller skips it
        tokens.resize(actual);
        if (tokens.size() > 2048) tokens.resize(2048);

        llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
        if (llama_decode(ctx, batch) != 0)
            return {};   // could not embed → empty; caller skips it

        n_past += tokens.size();

        const float* emb = llama_get_embeddings(ctx);
        if (!emb)
            return {};   // could not embed → empty; caller skips it

        int n_embd = llama_model_n_embd(model);
        std::vector<float> result(emb, emb + std::min(n_embd, EMBEDDING_DIM));
        if (static_cast<int>(result.size()) < EMBEDDING_DIM)
            result.resize(EMBEDDING_DIM, 0.0f);

        return result;
    }
};
