#pragma once

#include <string>
#include <vector>
#include <mutex>
#include <iostream>
#include <cstring>
#include <algorithm>  // for std::string::find
#include "llama.h"
#include "config.h"

class LLMGenerator {
private:
    llama_model* model = nullptr;
    llama_context* ctx = nullptr;
    std::mutex mutex;
    
public:
    ~LLMGenerator() {
        if (ctx) llama_free(ctx);
        if (model) llama_model_free(model);
    }
    
    bool init() {
        llama_model_params model_params = llama_model_default_params();
        model = llama_model_load_from_file(LLAMA_MODEL_PATH.c_str(), model_params);
        if (!model) {
            std::cerr << "Failed to load LLM model" << std::endl;
            return false;
        }
        
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 2048;
        ctx_params.n_batch = 512;
        ctx_params.n_ubatch = 512;  // Set ubatch to match n_batch
        ctx_params.n_threads = 4;
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
        
        std::cout << "LLM: Starting generation" << std::endl;
        
        // Recreate context for each request
        llama_free(ctx);
        llama_context_params ctx_params = llama_context_default_params();
        ctx_params.n_ctx = 2048;
        ctx_params.n_batch = 512;
        ctx_params.n_ubatch = 512;  // Set ubatch to match n_batch
        ctx_params.n_threads = 4;
        ctx_params.n_threads_batch = 4;
        ctx = llama_init_from_model(model, ctx_params);
        
        if (!ctx) {
            std::cerr << "LLM: Failed to recreate context" << std::endl;
            return "Error: Failed to recreate LLM context";
        }
        
        std::cout << "LLM: Context recreated successfully" << std::endl;
        
        // Get vocab from model
        const llama_vocab* vocab = llama_model_get_vocab(model);
        
        // Get chat template
        const char* tmpl = llama_model_chat_template(model, nullptr);
        
        // Parse the prompt to extract conversation history if present
        std::vector<llama_chat_message> messages;
        
        // Create system message
        const char* system_msg = "You are a helpful emergency knowledge assistant. Answer questions based on the provided context. When you use information from the context, mention which document it comes from. Remember information from the conversation history.";
        llama_chat_message sys_msg = {"system", system_msg};
        messages.push_back(sys_msg);
        
        // Check if prompt contains conversation history
        if (prompt.find("Previous conversation:") != std::string::npos) {
            // For now, just pass the whole prompt as a user message
            // In a more sophisticated implementation, we'd parse the history
            llama_chat_message user_msg = {"user", prompt.c_str()};
            messages.push_back(user_msg);
        } else {
            llama_chat_message user_msg = {"user", prompt.c_str()};
            messages.push_back(user_msg);
        }
        
        // Apply chat template
        std::vector<char> formatted(2048);
        int formatted_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(), formatted.size());
        if (formatted_len > (int)formatted.size()) {
            formatted.resize(formatted_len);
            formatted_len = llama_chat_apply_template(tmpl, messages.data(), messages.size(), true, formatted.data(), formatted.size());
        }
        if (formatted_len < 0) {
            return "Error: Failed to apply chat template";
        }
        
        std::string formatted_prompt(formatted.begin(), formatted.begin() + formatted_len);
        
        // Tokenize
        std::cout << "LLM: Tokenizing prompt..." << std::endl;
        int n_prompt = -llama_tokenize(vocab, formatted_prompt.c_str(), formatted_prompt.size(), NULL, 0, true, true);
        std::cout << "LLM: Prompt will use " << n_prompt << " tokens" << std::endl;
        
        if (n_prompt <= 0) {
            std::cerr << "LLM: Invalid token count: " << n_prompt << std::endl;
            return "Error: Invalid prompt token count";
        }
        
        std::vector<llama_token> prompt_tokens(n_prompt);
        int actual_tokens = llama_tokenize(vocab, formatted_prompt.c_str(), formatted_prompt.size(), prompt_tokens.data(), prompt_tokens.size(), true, true);
        if (actual_tokens < 0) {
            std::cerr << "LLM: Tokenization failed with code: " << actual_tokens << std::endl;
            return "Error: Failed to tokenize prompt";
        }
        prompt_tokens.resize(actual_tokens);
        std::cout << "LLM: Actually tokenized " << actual_tokens << " tokens" << std::endl;
        
        // Check context size
        int n_ctx = llama_n_ctx(ctx);
        if (prompt_tokens.size() > n_ctx - 512) {
            prompt_tokens.resize(n_ctx - 512);
        }
        
        // Check batch size limit
        if (prompt_tokens.size() > 512) {  // n_batch is 512
            std::cerr << "LLM: Prompt too long (" << prompt_tokens.size() << " tokens), truncating to 512" << std::endl;
            prompt_tokens.resize(512);
        }
        
        // Prepare batch
        llama_batch batch = llama_batch_get_one(prompt_tokens.data(), prompt_tokens.size());
        
        // Evaluate prompt
        std::cout << "LLM: Evaluating prompt..." << std::endl;
        int decode_result = llama_decode(ctx, batch);
        if (decode_result != 0) {
            std::cerr << "LLM: Decode failed with code: " << decode_result << std::endl;
            return "Error: Failed to process prompt";
        }
        std::cout << "LLM: Prompt evaluated successfully" << std::endl;
        
        // Initialize sampler
        llama_sampler* smpl = llama_sampler_chain_init(llama_sampler_chain_default_params());
        llama_sampler_chain_add(smpl, llama_sampler_init_min_p(0.05f, 1));
        llama_sampler_chain_add(smpl, llama_sampler_init_temp(0.8f));
        llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
        
        // Generate response
        std::string response;
        int n_decode = 0;
        const int n_max_tokens = 1024;
        
        while (n_decode < n_max_tokens) {
            llama_token new_token_id = llama_sampler_sample(smpl, ctx, -1);
            
            if (llama_vocab_is_eog(vocab, new_token_id)) {
                break;
            }
            
            char buf[256];
            int n = llama_token_to_piece(vocab, new_token_id, buf, sizeof(buf), 0, true);
            if (n < 0) break;
            response.append(buf, n);
            
            batch = llama_batch_get_one(&new_token_id, 1);
            
            if (prompt_tokens.size() + n_decode + 1 >= n_ctx) {
                break;
            }
            
            if (llama_decode(ctx, batch) != 0) {
                break;
            }
            
            n_decode++;
        }
        
        llama_sampler_free(smpl);
        return response;
    }
};
