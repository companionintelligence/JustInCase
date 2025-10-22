#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <map>
#include <thread>
#include <mutex>
#include <chrono>
#include <filesystem>
#include <algorithm>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include <curl/curl.h>
#include "llama.h"
#include "nlohmann/json.hpp"

using json = nlohmann::json;
namespace fs = std::filesystem;

// Global configuration
const int PORT = 8080;
const int EMBEDDING_DIM = 384;
const int CHUNK_SIZE = 500;
const int CHUNK_OVERLAP = 50;
const int MAX_CONTEXT_CHUNKS = 3;
const int SEARCH_TOP_K = 5;
const std::string LLAMA_MODEL_PATH = "./gguf_models/Llama-3.2-1B-Instruct-Q4_K_S.gguf";
const std::string NOMIC_MODEL_PATH = "./gguf_models/nomic-embed-text-v1.5.Q4_K_M.gguf";
const std::string TIKA_URL = "http://tika:9998/tika";

// CURL callback for response data
size_t curl_write_callback(void* contents, size_t size, size_t nmemb, std::string* userp) {
    userp->append((char*)contents, size * nmemb);
    return size * nmemb;
}

// Extract text from file using Tika
std::string extract_text_with_tika(const std::string& filepath) {
    CURL* curl = curl_easy_init();
    if (!curl) {
        return "";
    }
    
    std::string response;
    
    // Read file content
    std::ifstream file(filepath, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string file_content = buffer.str();
    
    // Set up CURL
    curl_easy_setopt(curl, CURLOPT_URL, TIKA_URL.c_str());
    curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, "PUT");
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, file_content.c_str());
    curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, file_content.size());
    
    // Headers
    struct curl_slist* headers = NULL;
    headers = curl_slist_append(headers, "Accept: text/plain");
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    
    // Response handling
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    
    // Perform request
    CURLcode res = curl_easy_perform(curl);
    
    // Cleanup
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    
    if (res != CURLE_OK) {
        std::cerr << "CURL error: " << curl_easy_strerror(res) << std::endl;
        return "";
    }
    
    return response;
}

// Global state
struct Document {
    std::string filename;
    std::string text;
};

struct IngestionStatus {
    bool in_progress = false;
    int total_files = 0;
    int files_processed = 0;
    std::string current_file;
    std::chrono::steady_clock::time_point start_time;
    std::chrono::steady_clock::time_point last_update;
};

// Simple vector index implementation
class SimpleVectorIndex {
private:
    std::vector<std::vector<float>> embeddings;
    int dimension;
    
public:
    SimpleVectorIndex(int dim) : dimension(dim) {}
    
    void add(const std::vector<float>& embedding) {
        embeddings.push_back(embedding);
    }
    
    void add_batch(int n, const float* data) {
        for (int i = 0; i < n; i++) {
            std::vector<float> embedding(data + i * dimension, data + (i + 1) * dimension);
            embeddings.push_back(embedding);
        }
    }
    
    std::vector<std::pair<int, float>> search(const std::vector<float>& query, int k) {
        std::vector<std::pair<float, int>> distances;
        
        for (size_t i = 0; i < embeddings.size(); i++) {
            float dist = 0;
            for (int j = 0; j < dimension; j++) {
                float diff = query[j] - embeddings[i][j];
                dist += diff * diff;
            }
            distances.push_back({dist, i});
        }
        
        std::sort(distances.begin(), distances.end());
        
        std::vector<std::pair<int, float>> results;
        for (int i = 0; i < k && i < distances.size(); i++) {
            results.push_back({distances[i].second, distances[i].first});
        }
        
        return results;
    }
    
    size_t size() const { return embeddings.size(); }
    
    void clear() { embeddings.clear(); }
    
    void save(const std::string& path) {
        std::ofstream file(path, std::ios::binary);
        int n = embeddings.size();
        file.write((char*)&n, sizeof(n));
        file.write((char*)&dimension, sizeof(dimension));
        for (const auto& emb : embeddings) {
            file.write((char*)emb.data(), dimension * sizeof(float));
        }
        std::cout << "Saved " << n << " embeddings to " << path << std::endl;
    }
    
    void load(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::cout << "No existing index found at " << path << std::endl;
            return;
        }
        
        int n;
        file.read((char*)&n, sizeof(n));
        file.read((char*)&dimension, sizeof(dimension));
        
        embeddings.clear();
        embeddings.reserve(n);
        
        for (int i = 0; i < n; i++) {
            std::vector<float> emb(dimension);
            file.read((char*)emb.data(), dimension * sizeof(float));
            embeddings.push_back(emb);
        }
        std::cout << "Loaded " << n << " embeddings from " << path << std::endl;
    }
};

// Global variables
std::mutex index_mutex;
SimpleVectorIndex* vector_index = nullptr;
std::vector<Document> documents;
IngestionStatus ingestion_status;
llama_model* llm_model = nullptr;
llama_model* embedding_model = nullptr;
llama_context* llm_ctx = nullptr;
llama_context* embedding_ctx = nullptr;

// HTTP response builder
std::string build_http_response(int status_code, const std::string& content_type, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " OK\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    response << "Access-Control-Allow-Origin: *\r\n";
    response << "Access-Control-Allow-Methods: GET, POST, OPTIONS\r\n";
    response << "Access-Control-Allow-Headers: Content-Type\r\n";
    response << "\r\n";
    response << body;
    return response.str();
}

// Parse HTTP request
struct HttpRequest {
    std::string method;
    std::string path;
    std::map<std::string, std::string> headers;
    std::string body;
};

HttpRequest parse_http_request(const std::string& request) {
    HttpRequest req;
    std::istringstream stream(request);
    std::string line;
    
    // Parse request line
    std::getline(stream, line);
    std::istringstream request_line(line);
    request_line >> req.method >> req.path;
    
    // Parse headers
    while (std::getline(stream, line) && line != "\r") {
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 2);
            value.erase(value.find_last_not_of("\r\n") + 1);
            req.headers[key] = value;
        }
    }
    
    // Parse body
    std::string remaining((std::istreambuf_iterator<char>(stream)), std::istreambuf_iterator<char>());
    req.body = remaining;
    
    return req;
}

// Initialize models
bool init_models() {
    // Initialize LLM
    llama_backend_init(false);
    
    llama_model_params model_params = llama_model_default_params();
    llm_model = llama_load_model_from_file(LLAMA_MODEL_PATH.c_str(), model_params);
    if (!llm_model) {
        std::cerr << "Failed to load LLM model" << std::endl;
        return false;
    }
    
    llama_context_params ctx_params = llama_context_default_params();
    ctx_params.n_ctx = 2048;
    ctx_params.n_threads = 4;
    llm_ctx = llama_new_context_with_model(llm_model, ctx_params);
    
    // Initialize embedding model
    embedding_model = llama_load_model_from_file(NOMIC_MODEL_PATH.c_str(), model_params);
    if (!embedding_model) {
        std::cerr << "Failed to load embedding model" << std::endl;
        return false;
    }
    
    ctx_params.n_ctx = 8192;
    ctx_params.embeddings = true;
    embedding_ctx = llama_new_context_with_model(embedding_model, ctx_params);
    
    return true;
}

// Get embedding for text
std::vector<float> get_embedding(const std::string& text) {
    // Tokenize
    std::vector<llama_token> tokens(text.length() + 1);
    int n_tokens = llama_tokenize(embedding_model, text.c_str(), text.length(), tokens.data(), tokens.size(), true, false);
    tokens.resize(n_tokens);
    
    // Evaluate
    llama_batch batch = llama_batch_init(512, 0, 1);
    for (int i = 0; i < n_tokens; i++) {
        llama_batch_add(batch, tokens[i], i, {0}, false);
    }
    batch.logits[batch.n_tokens - 1] = true;
    
    if (llama_decode(embedding_ctx, batch) != 0) {
        std::cerr << "Failed to eval embedding" << std::endl;
        llama_batch_free(batch);
        return std::vector<float>(EMBEDDING_DIM, 0.0f);
    }
    
    // Get embeddings
    const float* embeddings = llama_get_embeddings(embedding_ctx);
    std::vector<float> result(embeddings, embeddings + EMBEDDING_DIM);
    
    llama_batch_free(batch);
    return result;
}

// Generate LLM response
std::string generate_llm_response(const std::string& prompt) {
    // Tokenize prompt
    std::vector<llama_token> tokens(prompt.length() + 1);
    int n_tokens = llama_tokenize(llm_model, prompt.c_str(), prompt.length(), tokens.data(), tokens.size(), true, false);
    tokens.resize(n_tokens);
    
    // Generate response
    std::string response;
    llama_batch batch = llama_batch_init(512, 0, 1);
    
    // Process prompt
    for (int i = 0; i < n_tokens; i++) {
        llama_batch_add(batch, tokens[i], i, {0}, false);
    }
    batch.logits[batch.n_tokens - 1] = true;
    
    if (llama_decode(llm_ctx, batch) != 0) {
        llama_batch_free(batch);
        return "Error: Failed to process prompt";
    }
    
    // Generate tokens
    int n_cur = batch.n_tokens;
    int n_decode = 0;
    const int n_max_tokens = 1024;
    
    while (n_decode < n_max_tokens) {
        // Sample next token
        auto logits = llama_get_logits_ith(llm_ctx, batch.n_tokens - 1);
        std::vector<llama_token_data> candidates;
        candidates.reserve(llama_n_vocab(llm_model));
        
        for (llama_token token_id = 0; token_id < llama_n_vocab(llm_model); token_id++) {
            candidates.emplace_back(llama_token_data{token_id, logits[token_id], 0.0f});
        }
        
        llama_token_data_array candidates_p = { candidates.data(), candidates.size(), false };
        
        // Sample
        llama_token new_token_id = llama_sample_token_greedy(llm_ctx, &candidates_p);
        
        // Check for EOS
        if (new_token_id == llama_token_eos(llm_model)) {
            break;
        }
        
        // Add token to response
        char buf[256];
        int n = llama_token_to_piece(llm_model, new_token_id, buf, sizeof(buf));
        if (n > 0) {
            response.append(buf, n);
        }
        
        // Add token to batch for next iteration
        llama_batch_clear(batch);
        llama_batch_add(batch, new_token_id, n_cur, {0}, true);
        n_cur++;
        
        if (llama_decode(llm_ctx, batch) != 0) {
            break;
        }
        
        n_decode++;
    }
    
    llama_batch_free(batch);
    return response;
}

// Split text into chunks
std::vector<std::string> split_text(const std::string& text) {
    std::vector<std::string> chunks;
    size_t start = 0;
    
    while (start < text.length()) {
        size_t end = std::min(start + CHUNK_SIZE, text.length());
        
        // Try to break at sentence boundaries
        if (end < text.length()) {
            size_t period = text.rfind(". ", end);
            if (period != std::string::npos && period > start + CHUNK_SIZE - 100) {
                end = period + 2;
            }
        }
        
        chunks.push_back(text.substr(start, end - start));
        start = end - CHUNK_OVERLAP;
    }
    
    return chunks;
}

// Load index from disk
void load_index() {
    std::lock_guard<std::mutex> lock(index_mutex);
    
    vector_index = new SimpleVectorIndex(EMBEDDING_DIM);
    
    if (fs::exists("data/index.bin") && fs::exists("data/metadata.jsonl")) {
        vector_index->load("data/index.bin");
        
        std::ifstream metadata_file("data/metadata.jsonl");
        std::string line;
        documents.clear();
        while (std::getline(metadata_file, line)) {
            auto j = json::parse(line);
            documents.push_back({j["filename"], j["text"]});
        }
        
        std::cout << "Loaded " << documents.size() << " documents from index" << std::endl;
    } else {
        std::cout << "Created new index" << std::endl;
    }
}

// Save index to disk
void save_index() {
    std::lock_guard<std::mutex> lock(index_mutex);
    
    fs::create_directories("data");
    vector_index->save("data/index.bin");
    
    std::ofstream metadata_file("data/metadata.jsonl");
    for (const auto& doc : documents) {
        json j;
        j["filename"] = doc.filename;
        j["text"] = doc.text;
        metadata_file << j.dump() << std::endl;
    }
}

// Background ingestion thread
void background_ingestion() {
    std::set<std::string> processed_files;
    
    // Load processed files list
    if (fs::exists("data/processed_files.txt")) {
        std::ifstream pf("data/processed_files.txt");
        std::string line;
        while (std::getline(pf, line)) {
            processed_files.insert(line);
        }
    }
    
    while (true) {
        std::vector<std::pair<std::string, std::string>> files_to_process;
        
        // Find new files
        if (fs::exists("sources")) {
            for (const auto& entry : fs::recursive_directory_iterator("sources")) {
                if (entry.is_regular_file()) {
                    std::string ext = entry.path().extension();
                    if (ext == ".txt" || ext == ".pdf") {
                        std::string rel_path = fs::relative(entry.path(), "sources").string();
                        if (processed_files.find(rel_path) == processed_files.end()) {
                            files_to_process.push_back({entry.path().string(), rel_path});
                        }
                    }
                }
            }
        }
        
        if (!files_to_process.empty()) {
            ingestion_status.in_progress = true;
            ingestion_status.total_files = files_to_process.size();
            ingestion_status.files_processed = 0;
            ingestion_status.start_time = std::chrono::steady_clock::now();
            
            std::vector<float> new_embeddings;
            std::vector<Document> new_docs;
            
            for (const auto& [full_path, rel_path] : files_to_process) {
                ingestion_status.current_file = rel_path;
                
                try {
                    std::string text;
                    
                    if (full_path.ends_with(".txt")) {
                        std::ifstream file(full_path);
                        std::stringstream buffer;
                        buffer << file.rdbuf();
                        text = buffer.str();
                    } else if (full_path.ends_with(".pdf")) {
                        // Use Tika service for PDF extraction
                        text = extract_text_with_tika(full_path);
                        if (text.empty()) {
                            std::cerr << "Failed to extract text from PDF: " << rel_path << std::endl;
                            continue;
                        }
                    } else {
                        continue;
                    }
                    
                    // Split into chunks
                    auto chunks = split_text(text);
                    
                    for (const auto& chunk : chunks) {
                        if (chunk.length() > 100) {
                            auto embedding = get_embedding(chunk);
                            new_embeddings.insert(new_embeddings.end(), embedding.begin(), embedding.end());
                            new_docs.push_back({rel_path, chunk});
                        }
                    }
                    
                    ingestion_status.files_processed++;
                    ingestion_status.last_update = std::chrono::steady_clock::now();
                    processed_files.insert(rel_path);
                    
                } catch (const std::exception& e) {
                    std::cerr << "Error processing " << rel_path << ": " << e.what() << std::endl;
                }
            }
            
            // Update index
            if (!new_embeddings.empty()) {
                std::lock_guard<std::mutex> lock(index_mutex);
                int n_vectors = new_embeddings.size() / EMBEDDING_DIM;
                vector_index->add_batch(n_vectors, new_embeddings.data());
                documents.insert(documents.end(), new_docs.begin(), new_docs.end());
                save_index();
                
                // Save processed files
                std::ofstream pf("data/processed_files.txt");
                for (const auto& f : processed_files) {
                    pf << f << std::endl;
                }
            }
            
            ingestion_status.in_progress = false;
        }
        
        std::this_thread::sleep_for(std::chrono::seconds(30));
    }
}

// Handle static file serving
std::string serve_static_file(const std::string& path) {
    std::string file_path = path;
    if (file_path == "/") {
        file_path = "/index.html";
    }
    
    // Remove leading slash
    if (!file_path.empty() && file_path[0] == '/') {
        file_path = file_path.substr(1);
    }
    
    // Prepend public/ directory
    file_path = "public/" + file_path;
    
    // Security check - prevent directory traversal
    if (file_path.find("..") != std::string::npos) {
        return build_http_response(403, "text/plain", "Forbidden");
    }
    
    // Check if file exists
    if (!fs::exists(file_path)) {
        return build_http_response(404, "text/plain", "Not Found");
    }
    
    // Read file
    std::ifstream file(file_path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    // Determine content type
    std::string content_type = "text/plain";
    if (file_path.ends_with(".html")) content_type = "text/html";
    else if (file_path.ends_with(".css")) content_type = "text/css";
    else if (file_path.ends_with(".js")) content_type = "application/javascript";
    else if (file_path.ends_with(".json")) content_type = "application/json";
    
    return build_http_response(200, content_type, content);
}

// Handle query endpoint
std::string handle_query(const std::string& body) {
    try {
        auto request_json = json::parse(body);
        std::string query = request_json["query"];
        
        json response;
        
        if (documents.empty()) {
            // No documents indexed, use LLM directly
            std::string prompt = "Question: " + query + "\n\nAnswer:";
            std::string answer = generate_llm_response(prompt);
            
            response["answer"] = answer;
            response["matches"] = json::array();
            response["status"]["documents_indexed"] = 0;
            response["status"]["ingestion"]["in_progress"] = ingestion_status.in_progress;
            
            return build_http_response(200, "application/json", response.dump());
        }
        
        // Get query embedding
        auto query_embedding = get_embedding(query);
        
        // Search in vector index
        std::vector<std::pair<int, float>> results;
        
        {
            std::lock_guard<std::mutex> lock(index_mutex);
            results = vector_index->search(query_embedding, SEARCH_TOP_K);
        }
        
        // Build context from top matches
        std::string context;
        json matches = json::array();
        int chunks_used = 0;
        
        for (const auto& [idx, dist] : results) {
            if (chunks_used >= MAX_CONTEXT_CHUNKS) break;
            if (idx >= 0 && idx < documents.size()) {
                context += documents[idx].text + "\n\n";
                matches.push_back({
                    {"filename", documents[idx].filename},
                    {"text", documents[idx].text}
                });
                chunks_used++;
            }
        }
        
        // Generate response with context
        std::string prompt = "Context: " + context + "\n\nQuestion: " + query + "\n\nAnswer:";
        std::string answer = generate_llm_response(prompt);
        
        response["answer"] = answer;
        response["matches"] = matches;
        response["status"]["documents_indexed"] = documents.size();
        response["status"]["ingestion"]["in_progress"] = ingestion_status.in_progress;
        
        return build_http_response(200, "application/json", response.dump());
        
    } catch (const std::exception& e) {
        json error_response;
        error_response["error"] = e.what();
        return build_http_response(500, "application/json", error_response.dump());
    }
}

// Handle status endpoint
std::string handle_status() {
    json status;
    status["documents_indexed"] = documents.size();
    status["ingestion"]["in_progress"] = ingestion_status.in_progress;
    status["ingestion"]["total_files"] = ingestion_status.total_files;
    status["ingestion"]["files_processed"] = ingestion_status.files_processed;
    status["ingestion"]["current_file"] = ingestion_status.current_file;
    
    if (ingestion_status.in_progress && ingestion_status.total_files > 0) {
        status["progress_percent"] = (ingestion_status.files_processed * 100) / ingestion_status.total_files;
    } else {
        status["progress_percent"] = documents.empty() ? 0 : 100;
    }
    
    return build_http_response(200, "application/json", status.dump());
}

// Handle client connection
void handle_client(int client_socket) {
    char buffer[4096] = {0};
    int valread = read(client_socket, buffer, 4096);
    
    if (valread > 0) {
        std::string request_str(buffer);
        HttpRequest request = parse_http_request(request_str);
        
        std::string response;
        
        if (request.method == "OPTIONS") {
            response = build_http_response(200, "text/plain", "");
        } else if (request.method == "POST" && request.path == "/query") {
            response = handle_query(request.body);
        } else if (request.method == "GET" && request.path == "/status") {
            response = handle_status();
        } else if (request.method == "GET") {
            response = serve_static_file(request.path);
        } else {
            response = build_http_response(404, "text/plain", "Not Found");
        }
        
        send(client_socket, response.c_str(), response.length(), 0);
    }
    
    close(client_socket);
}

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
    
    // Initialize models
    std::cout << "Initializing models..." << std::endl;
    if (!init_models()) {
        std::cerr << "Failed to initialize models" << std::endl;
        return 1;
    }
    
    // Load existing index
    load_index();
    
    // Start background ingestion thread
    std::thread ingestion_thread(background_ingestion);
    ingestion_thread.detach();
    
    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }
    
    // Allow socket reuse
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT, &opt, sizeof(opt))) {
        std::cerr << "Setsockopt failed" << std::endl;
        return 1;
    }
    
    // Bind socket
    struct sockaddr_in address;
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = INADDR_ANY;
    address.sin_port = htons(PORT);
    
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        std::cerr << "Bind failed" << std::endl;
        return 1;
    }
    
    // Listen for connections
    if (listen(server_fd, 3) < 0) {
        std::cerr << "Listen failed" << std::endl;
        return 1;
    }
    
    std::cout << "Server listening on port " << PORT << std::endl;
    
    // Accept connections
    while (true) {
        int addrlen = sizeof(address);
        int client_socket = accept(server_fd, (struct sockaddr *)&address, (socklen_t*)&addrlen);
        
        if (client_socket < 0) {
            std::cerr << "Accept failed" << std::endl;
            continue;
        }
        
        // Handle client in a new thread
        std::thread client_thread(handle_client, client_socket);
        client_thread.detach();
    }
    
    // Cleanup
    llama_free(llm_ctx);
    llama_free(embedding_ctx);
    llama_free_model(llm_model);
    llama_free_model(embedding_model);
    llama_backend_free();
    curl_global_cleanup();
    
    return 0;
}
