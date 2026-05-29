#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <iostream>
#include <cstring>
#include <algorithm>
#include "llama.h"
#include "config.h"

class LLMGenerator {
private:
    llama_model*   model = nullptr;
    llama_context* ctx   = nullptr;
    std::mutex     mutex;

public:
    ~LLMGenerator() {
        if (ctx)   llama_free(ctx);
        if (model) llama_model_free(model);
    }

    bool init() {
        llama_model_params model_params = llama_model_default_params();
        model = llama_model_load_from_file(get_llm_model_path().c_str(), model_params);
        if (!model) {
            std::cerr << "Failed to load LLM model from "
                      << get_llm_model_path() << std::endl;
            return false;
        }

        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx          = LLM_CONTEXT_SIZE;
        ctx_params.n_batch        = LLM_BATCH_SIZE;
        ctx_params.n_ubatch       = LLM_BATCH_SIZE;
        ctx_params.n_threads      = 4;
        ctx_params.n_threads_batch = 4;

        ctx = llama_init_from_model(model, ctx_params);
        if (!ctx) {
            std::cerr << "Failed to create LLM context" << std::endl;
            return false;
        }
        return true;
    }

    std::string generate(const std::string& prompt) {
        std::lock_guard<std::mutex> lock(mutex);

        std::cout << "LLM: starting generation" << std::endl;

        // Fresh context for each request
        llama_free(ctx);
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx          = LLM_CONTEXT_SIZE;
        ctx_params.n_batch        = LLM_BATCH_SIZE;
        ctx_params.n_ubatch       = LLM_BATCH_SIZE;
        ctx_params.n_threads      = 4;
        ctx_params.n_threads_batch = 4;
        ctx = llama_init_from_model(model, ctx_params);

        if (!ctx) {
            std::cerr << "LLM: failed to recreate context" << std::endl;
            return "Error: failed to recreate LLM context";
        }

        // ── Vocab & chat template ────────────────────────────────────
        const llama_vocab* vocab = llama_model_get_vocab(model);
        const char* tmpl = llama_model_chat_template(model, nullptr);

        // Build chat messages
        std::vector<llama_chat_message> messages;

        const char* system_msg;
        if (prompt.find("REFERENCE MATERIALS:") != std::string::npos) {
            system_msg =
                "You are a knowledgeable emergency-preparedness assistant. "
                "Answer clearly and practically using the reference materials "
                "provided.  Cite the source document when possible.  If the "
                "references don't cover the question, say so honestly.";
        } else {
            system_msg =
                "You are a helpful AI assistant.  Be friendly, concise, "
                "and informative.";
        }
        llama_chat_message sys_msg = {"system", system_msg};
        messages.push_back(sys_msg);

        llama_chat_message user_msg = {"user", prompt.c_str()};
        messages.push_back(user_msg);

        // Apply chat template
        std::vector<char> formatted(4096);
        int flen = llama_chat_apply_template(
                tmpl, messages.data(), messages.size(),
                true, formatted.data(), formatted.size());
        if (flen > static_cast<int>(formatted.size())) {
            formatted.resize(flen);
            flen = llama_chat_apply_template(
                    tmpl, messages.data(), messages.size(),
                    true, formatted.data(), formatted.size());
        }
        if (flen < 0)
            return "Error: failed to apply chat template";

        std::string formatted_prompt(formatted.begin(), formatted.begin() + flen);

        // ── Tokenize ─────────────────────────────────────────────────
        int n_prompt = -llama_tokenize(
                vocab, formatted_prompt.c_str(), formatted_prompt.size(),
                NULL, 0, true, true);
        if (n_prompt <= 0) return "Error: invalid prompt token count";

        std::vector<llama_token> prompt_tokens(n_prompt);
        int actual = llama_tokenize(
                vocab, formatted_prompt.c_str(), formatted_prompt.size(),
                prompt_tokens.data(), prompt_tokens.size(), true, true);
        if (actual < 0) return "Error: tokenization failed";
        prompt_tokens.resize(actual);

        std::cout << "LLM: " << actual << " prompt tokens" << std::endl;

        // Truncate if necessary, leaving room for the response
        int n_ctx = llama_n_ctx(ctx);
        int max_prompt = n_ctx - LLM_MAX_TOKENS;
        if (max_prompt < 256) max_prompt = 256;
        if (static_cast<int>(prompt_tokens.size()) > max_prompt) {
            std::cerr << "LLM: truncating prompt from "
                      << prompt_tokens.size() << " to " << max_prompt << std::endl;
            prompt_tokens.resize(max_prompt);
        }

        // ── Evaluate prompt ──────────────────────────────────────────
        llama_batch batch = llama_batch_get_one(
                prompt_tokens.data(), prompt_tokens.size());
        if (llama_decode(ctx, batch) != 0)
            return "Error: failed to process prompt";

        // ── Sample tokens ────────────────────────────────────────────
        llama_sampler* smpl = llama_sampler_chain_init(
                llama_sampler_chain_default_params());
        llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));
        llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.7f));
        llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));

        std::string response;
        int n_decode = 0;

        try {
            while (n_decode < LLM_MAX_TOKENS) {
                llama_token tok = llama_sampler_sample(smpl, ctx, -1);

                if (llama_vocab_is_eog(vocab, tok)) break;

                char buf[256];
                int n = llama_token_to_piece(vocab, tok, buf, sizeof(buf), 0, true);
                if (n < 0) break;
                response.append(buf, n);

                batch = llama_batch_get_one(&tok, 1);
                if (static_cast<int>(prompt_tokens.size()) + n_decode + 1 >= n_ctx)
                    break;
                if (llama_decode(ctx, batch) != 0) break;

                n_decode++;
            }
        } catch (const std::exception& e) {
            std::cerr << "LLM: exception during generation: "
                      << e.what() << std::endl;
            response = "Error occurred during response generation.";
        }

        llama_sampler_free(smpl);

        if (response.empty())
            return "I'm having trouble generating a response. Please try again.";

        std::cout << "LLM: generated " << response.length()
                  << " chars (" << n_decode << " tokens)" << std::endl;
        return response;
    }
};
