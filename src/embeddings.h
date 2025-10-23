#pragma once

#include <vector>
#include <string>
#include <mutex>
#include <iostream>
#include "llama.h"
#include "config.h"

class EmbeddingGenerator {
private:
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    std::mutex mutex;
    int n_past = 0;
    
public:
    ~EmbeddingGenerator() {
        if (ctx) llama_free(ctx);
        if (model) llama_model_free(model);
    }
    
    bool init() {
        llama_model_params model_params = llama_model_default_params();
        model = llama_model_load_from_file(NOMIC_MODEL_PATH.c_str(), model_params);
        if (!model) {
            std::cerr << "Failed to load embedding model" << std::endl;
            return false;
        }
        
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 8192;
        ctx_params.n_batch = 2048;
        ctx_params.n_ubatch = 2048;
        ctx_params.embeddings = true;
        ctx_params.pooling_type = LLAMA_POOLING_TYPE_MEAN;
        ctx_params.n_threads = 4;
        ctx_params.n_threads_batch = 4;
        
        ctx = llama_init_from_model(model, ctx_params);
        if (!ctx) {
            std::cerr << "Failed to create embedding context" << std::endl;
            return false;
        }
        
        return true;
    }
    
    std::vector<float> get_embedding(const std::string& text) {
        std::lock_guard<std::mutex> lock(mutex);
        
        std::cout << "Embeddings: Getting embedding for text of length " << text.length() << std::endl;
        
        // Reset context if getting full
        int n_ctx = llama_n_ctx(ctx);
        if (n_past > n_ctx * 0.75) {
            std::cout << "Embeddings: Resetting context (n_past=" << n_past << ", n_ctx=" << n_ctx << ")" << std::endl;
            llama_free(ctx);
            
            llama_context_params ctx_params = llama_context_default_params();
            ctx_params.n_ctx = 8192;
            ctx_params.n_batch = 2048;
            ctx_params.n_ubatch = 2048;
            ctx_params.embeddings = true;
            ctx_params.pooling_type = LLAMA_POOLING_TYPE_MEAN;
            ctx_params.n_threads = 4;
            ctx_params.n_threads_batch = 4;
            
            ctx = llama_init_from_model(model, ctx_params);
            n_past = 0;
            
            if (!ctx) {
                return std::vector<float>(EMBEDDING_DIM, 0.0f);
            }
        }
        
        // Get vocab from model
        const llama_vocab* vocab = llama_model_get_vocab(model);
        
        // Tokenize
        int n_prompt = -llama_tokenize(vocab, text.c_str(), text.size(), NULL, 0, true, true);
        if (n_prompt <= 0) {
            return std::vector<float>(EMBEDDING_DIM, 0.0f);
        }
        
        if (n_prompt > 2048) {
            n_prompt = 2048;
        }
        
        std::vector<llama_token> tokens(n_prompt);
        int actual_tokens = llama_tokenize(vocab, text.c_str(), text.size(), tokens.data(), tokens.size(), true, true);
        if (actual_tokens < 0) {
            return std::vector<float>(EMBEDDING_DIM, 0.0f);
        }
        tokens.resize(actual_tokens);
        
        // Check batch size limit
        if (tokens.size() > 2048) {  // n_batch is 2048 for embeddings
            std::cerr << "Embeddings: Input too long (" << tokens.size() << " tokens), truncating to 2048" << std::endl;
            tokens.resize(2048);
        }
        
        // Prepare batch
        llama_batch batch = llama_batch_get_one(tokens.data(), tokens.size());
        
        // Evaluate
        if (llama_decode(ctx, batch) != 0) {
            return std::vector<float>(EMBEDDING_DIM, 0.0f);
        }
        
        n_past += tokens.size();
        
        // Get embeddings
        const float* embeddings = llama_get_embeddings(ctx);
        if (!embeddings) {
            return std::vector<float>(EMBEDDING_DIM, 0.0f);
        }
        
        int n_embd = llama_model_n_embd(model);
        std::vector<float> result(embeddings, embeddings + std::min(n_embd, EMBEDDING_DIM));
        if (result.size() < EMBEDDING_DIM) {
            result.resize(EMBEDDING_DIM, 0.0f);
        }
        
        return result;
    }
};
