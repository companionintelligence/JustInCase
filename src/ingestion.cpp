#include <iostream>
#include <fstream>
#include <set>
#include <thread>
#include <chrono>
#include <curl/curl.h>
#include "llama.h"
#include "config.h"
#include "text_utils.h"
#include "embeddings.h"
#include "simple_vector_index.h"
#include "pg_vector_store.h"

// Wait for Tika service to be ready
bool wait_for_tika(int max_retries = 30) {
    std::cout << "Waiting for Tika service..." << std::endl;
    
    for (int i = 0; i < max_retries; i++) {
        CURL* curl = curl_easy_init();
        if (!curl) continue;
        
        curl_easy_setopt(curl, CURLOPT_URL, "http://tika:9998");
        curl_easy_setopt(curl, CURLOPT_NOBODY, 1L);
        curl_easy_setopt(curl, CURLOPT_TIMEOUT, 2L);
        
        CURLcode res = curl_easy_perform(curl);
        curl_easy_cleanup(curl);
        
        if (res == CURLE_OK) {
            std::cout << "Tika service is ready!" << std::endl;
            return true;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    std::cerr << "Tika service failed to start" << std::endl;
    return false;
}

int main() {
    // Initialize CURL
    curl_global_init(CURL_GLOBAL_DEFAULT);
    
    // Wait for Tika
    if (!wait_for_tika()) {
        std::cerr << "Cannot proceed without Tika service" << std::endl;
        return 1;
    }
    
    // Initialize llama backend
    llama_backend_init();
    llama_log_set([](enum ggml_log_level level, const char * text, void *) {
        if (level >= GGML_LOG_LEVEL_ERROR) {
            fprintf(stderr, "%s", text);
        }
    }, nullptr);
    
    // Initialize embedding generator
    EmbeddingGenerator embeddings;
    if (!embeddings.init()) {
        std::cerr << "Failed to initialize embeddings" << std::endl;
        return 1;
    }
    
    // For now, use SimpleVectorIndex - later we'll switch to PGVectorStore
    SimpleVectorIndex index(EMBEDDING_DIM);
    std::vector<Document> documents;
    
    // Load existing index
    if (fs::exists("data/index.bin") && fs::exists("data/metadata.jsonl")) {
        index.load("data/index.bin");
        
        std::ifstream metadata_file("data/metadata.jsonl");
        std::string line;
        while (std::getline(metadata_file, line)) {
            auto j = nlohmann::json::parse(line);
            documents.push_back({j["filename"], j["text"]});
        }
        
        std::cout << "Loaded " << documents.size() << " documents from index" << std::endl;
    }
    
    // Load processed files list
    std::set<std::string> processed_files;
    if (fs::exists("data/processed_files.txt")) {
        std::ifstream pf("data/processed_files.txt");
        std::string line;
        while (std::getline(pf, line)) {
            line.erase(0, line.find_first_not_of(" \t\r\n"));
            line.erase(line.find_last_not_of(" \t\r\n") + 1);
            if (!line.empty()) {
                processed_files.insert(line);
            }
        }
        pf.close();
        std::cout << "Loaded " << processed_files.size() << " processed files from tracking" << std::endl;
    }
    
    // Main ingestion loop
    while (true) {
        std::vector<std::pair<std::string, std::string>> files_to_process;
        
        // Find new files
        if (fs::exists("public/sources")) {
            try {
                for (const auto& entry : fs::recursive_directory_iterator("public/sources")) {
                    if (entry.is_regular_file()) {
                        std::string ext = entry.path().extension();
                        if (ext == ".txt" || ext == ".pdf") {
                            std::string rel_path = fs::relative(entry.path(), "public/sources").string();
                            if (processed_files.find(rel_path) == processed_files.end()) {
                                files_to_process.push_back({entry.path().string(), rel_path});
                            }
                        }
                    }
                }
            } catch (const std::exception& e) {
                std::cerr << "Error scanning public/sources directory: " << e.what() << std::endl;
            }
        }
        
        if (!files_to_process.empty()) {
            std::cout << "Found " << files_to_process.size() << " new files to process" << std::endl;
            
            for (const auto& [full_path, rel_path] : files_to_process) {
                try {
                    std::cout << "Processing: " << rel_path << std::endl;
                    
                    std::string text;
                    if (string_ends_with(full_path, ".txt")) {
                        std::ifstream file(full_path);
                        std::stringstream buffer;
                        buffer << file.rdbuf();
                        text = buffer.str();
                    } else if (string_ends_with(full_path, ".pdf")) {
                        text = extract_text_with_tika(full_path);
                        if (text.empty()) {
                            std::cerr << "Failed to extract text from PDF: " << rel_path << std::endl;
                            processed_files.insert(rel_path);
                            continue;
                        }
                        
                        // Truncate very large texts
                        const size_t MAX_TEXT_LENGTH = 500000;
                        if (text.length() > MAX_TEXT_LENGTH) {
                            std::cout << "Text too large, truncating from " << text.length() 
                                      << " to " << MAX_TEXT_LENGTH << " characters" << std::endl;
                            text = text.substr(0, MAX_TEXT_LENGTH);
                        }
                    }
                    
                    // Split into chunks
                    auto chunks = split_text(text);
                    std::cout << "Split into " << chunks.size() << " chunks" << std::endl;
                    
                    // Process chunks in batches
                    const int BATCH_SIZE = 50;
                    std::vector<float> batch_embeddings;
                    std::vector<Document> batch_docs;
                    
                    int chunk_count = 0;
                    for (const auto& chunk : chunks) {
                        if (chunk.length() > 100) {
                            std::cout << "Processing chunk " << ++chunk_count << "/" << chunks.size() << std::endl;
                            
                            auto embedding = embeddings.get_embedding(chunk);
                            if (embedding.size() == EMBEDDING_DIM) {
                                batch_embeddings.insert(batch_embeddings.end(), embedding.begin(), embedding.end());
                                batch_docs.push_back({rel_path, chunk});
                                
                                // Flush batch if full
                                if (batch_docs.size() >= BATCH_SIZE) {
                                    index.add_batch(batch_docs.size(), batch_embeddings.data());
                                    documents.insert(documents.end(), batch_docs.begin(), batch_docs.end());
                                    
                                    batch_embeddings.clear();
                                    batch_docs.clear();
                                    
                                    // Save periodically
                                    fs::create_directories("data");
                                    index.save("data/index.bin");
                                    
                                    std::ofstream metadata_file("data/metadata.jsonl");
                                    for (const auto& doc : documents) {
                                        nlohmann::json j;
                                        j["filename"] = doc.filename;
                                        j["text"] = doc.text;
                                        metadata_file << j.dump() << std::endl;
                                    }
                                    
                                    std::this_thread::sleep_for(std::chrono::milliseconds(500));
                                }
                            }
                        }
                    }
                    
                    // Flush remaining embeddings
                    if (!batch_embeddings.empty()) {
                        index.add_batch(batch_docs.size(), batch_embeddings.data());
                        documents.insert(documents.end(), batch_docs.begin(), batch_docs.end());
                    }
                    
                    processed_files.insert(rel_path);
                    
                    // Save progress
                    std::ofstream pf("data/processed_files.txt");
                    for (const auto& f : processed_files) {
                        pf << f << std::endl;
                    }
                    pf.flush();
                    pf.close();
                    
                    // Save index
                    index.save("data/index.bin");
                    std::ofstream metadata_file("data/metadata.jsonl");
                    for (const auto& doc : documents) {
                        nlohmann::json j;
                        j["filename"] = doc.filename;
                        j["text"] = doc.text;
                        metadata_file << j.dump() << std::endl;
                    }
                    
                    std::cout << "Completed processing: " << rel_path << std::endl;
                    
                } catch (const std::exception& e) {
                    std::cerr << "Error processing " << rel_path << ": " << e.what() << std::endl;
                    processed_files.insert(rel_path);
                }
            }
            
            std::cout << "Ingestion batch complete. Total documents: " << documents.size() << std::endl;
        }
        
        // Sleep before next scan
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
    
    // Cleanup
    llama_backend_free();
    curl_global_cleanup();
    
    return 0;
}
