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
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <signal.h>
#include "llama.h"
#include "nlohmann/json.hpp"
#include "config.h"
#include "text_utils.h"
#include "embeddings.h"
#include "llm.h"
#include "simple_vector_index.h"

namespace fs = std::filesystem;

using json = nlohmann::json;

// Document structure (matches pg_vector_store.h)
struct Document {
    std::string filename;
    std::string text;
};

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
    while (std::getline(stream, line) && line != "\r" && !line.empty()) {
        if (line == "\n") break;
        size_t colon_pos = line.find(':');
        if (colon_pos != std::string::npos) {
            std::string key = line.substr(0, colon_pos);
            std::string value = line.substr(colon_pos + 2);
            value.erase(value.find_last_not_of("\r\n") + 1);
            req.headers[key] = value;
        }
    }
    
    // Parse body based on Content-Length
    if (req.headers.find("Content-Length") != req.headers.end()) {
        int content_length = std::stoi(req.headers["Content-Length"]);
        req.body.resize(content_length);
        stream.read(&req.body[0], content_length);
    }
    
    return req;
}

// Global state
SimpleVectorIndex* vector_index = nullptr;
std::vector<Document> documents;
EmbeddingGenerator* embeddings = nullptr;
LLMGenerator* llm = nullptr;

// Conversation history storage
struct ConversationHistory {
    std::vector<std::pair<std::string, std::string>> messages; // role, content pairs
    std::chrono::system_clock::time_point last_activity;
};
std::map<std::string, ConversationHistory> conversations;
std::mutex conversations_mutex;

// Load index from disk
void load_index() {
    if (vector_index) delete vector_index;
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
        std::cout << "No existing index found" << std::endl;
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
    
    // Determine content type
    std::string content_type = "text/plain";
    if (string_ends_with(file_path, ".html")) content_type = "text/html";
    else if (string_ends_with(file_path, ".css")) content_type = "text/css";
    else if (string_ends_with(file_path, ".js")) content_type = "application/javascript";
    else if (string_ends_with(file_path, ".json")) content_type = "application/json";
    else if (string_ends_with(file_path, ".pdf")) content_type = "application/pdf";
    
    // Read file
    std::ifstream file(file_path, std::ios::binary);
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string content = buffer.str();
    
    return build_http_response(200, content_type, content);
}

// Handle query endpoint
std::string handle_query(const std::string& body) {
    try {
        std::cout << "Handling query request" << std::endl;
        auto request_json = json::parse(body);
        std::string query = request_json["query"];
        std::cout << "Query: " << query << std::endl;
        
        // Get or create conversation ID
        std::string conversation_id = "default";
        if (request_json.contains("conversation_id") && !request_json["conversation_id"].is_null()) {
            conversation_id = request_json["conversation_id"];
        }
        
        json response;
        response["conversation_id"] = conversation_id;
        
        if (documents.empty()) {
            std::cout << "No documents indexed, using LLM directly" << std::endl;
            // No documents indexed, use LLM directly
            std::string prompt = "You are an emergency knowledge assistant. Please answer the following question:\n\n";
            prompt += "Question: " + query + "\n\nAnswer:";
            std::cout << "Generating LLM response..." << std::endl;
            std::string answer = llm->generate(prompt);
            std::cout << "LLM response generated: " << answer.substr(0, 50) << "..." << std::endl;
            
            response["answer"] = answer;
            response["matches"] = json::array();
            
            return build_http_response(200, "application/json", response.dump());
        }
        
        // Get query embedding
        std::cout << "Getting query embedding..." << std::endl;
        auto query_embedding = embeddings->get_embedding(query);
        std::cout << "Query embedding size: " << query_embedding.size() << std::endl;
        
        // Search in vector index
        std::cout << "Searching vector index..." << std::endl;
        auto results = vector_index->search(query_embedding, SEARCH_TOP_K);
        std::cout << "Found " << results.size() << " results" << std::endl;
        
        // Build context from top matches
        std::string context;
        json matches = json::array();
        std::set<std::string> used_files; // Track which files we've already cited
        int chunks_used = 0;
        
        // Only use top 3 results maximum for better focus
        int max_results = std::min(3, (int)results.size());
        
        for (int i = 0; i < max_results; i++) {
            const auto& [idx, dist] = results[i];
            if (idx >= 0 && idx < documents.size()) {
                // Add to context for LLM - format it clearly
                context += "[REFERENCE " + std::to_string(i+1) + " from " + documents[idx].filename + "]\n";
                context += documents[idx].text + "\n";
                context += "[END REFERENCE " + std::to_string(i+1) + "]\n\n";
                
                // Only add unique files to matches (avoid duplicate citations)
                if (used_files.find(documents[idx].filename) == used_files.end()) {
                    matches.push_back({
                        {"filename", documents[idx].filename},
                        {"text", documents[idx].text.substr(0, 200) + "..."}, // Truncate for display
                        {"score", 1.0f - (dist / 100.0f)} // Convert distance to similarity score
                    });
                    used_files.insert(documents[idx].filename);
                }
                chunks_used++;
            }
        }
        
        // Get conversation history
        std::vector<std::pair<std::string, std::string>> history;
        {
            std::lock_guard<std::mutex> lock(conversations_mutex);
            auto& conv = conversations[conversation_id];
            history = conv.messages;
            
            // Clean up old conversations (older than 1 hour)
            auto now = std::chrono::system_clock::now();
            for (auto it = conversations.begin(); it != conversations.end(); ) {
                if (std::chrono::duration_cast<std::chrono::hours>(now - it->second.last_activity).count() > 1) {
                    it = conversations.erase(it);
                } else {
                    ++it;
                }
            }
        }
        
        // Build prompt with conversation history
        std::string prompt = "You are an emergency first aid assistant. Your role is to provide clear, actionable first aid advice based on the reference materials provided.\n\n";
        prompt += "IMPORTANT INSTRUCTIONS:\n";
        prompt += "1. DO NOT repeat or quote the reference text verbatim\n";
        prompt += "2. Synthesize the information to directly answer the user's question\n";
        prompt += "3. Provide step-by-step instructions when appropriate\n";
        prompt += "4. Be concise and practical\n";
        prompt += "5. Mention which reference you're using only when citing specific procedures\n\n";
        
        if (!context.empty()) {
            // Limit context size to prevent token overflow
            if (context.length() > 1500) {
                context = context.substr(0, 1500) + "...\n[REMAINING CONTENT TRUNCATED]\n";
                std::cout << "Truncated context to 1500 characters" << std::endl;
            }
            prompt += "REFERENCE MATERIALS:\n" + context + "\n";
            prompt += "Based on the above references, please answer the user's question.\n\n";
        }
        
        // Add conversation history (limit to recent exchanges)
        if (!history.empty()) {
            prompt += "Previous conversation:\n";
            // Only include last 4 exchanges (8 messages) to save tokens
            size_t start_idx = history.size() > 8 ? history.size() - 8 : 0;
            for (size_t i = start_idx; i < history.size(); i++) {
                prompt += history[i].first + ": " + history[i].second + "\n";
            }
            prompt += "\n";
        }
        
        prompt += "User: " + query + "\n\nAssistant:";
        
        std::cout << "Generating LLM response with context..." << std::endl;
        std::cout << "Prompt length: " << prompt.length() << " characters" << std::endl;
        std::string answer = llm->generate(prompt);
        std::cout << "LLM response generated successfully" << std::endl;
        
        // Update conversation history
        {
            std::lock_guard<std::mutex> lock(conversations_mutex);
            auto& conv = conversations[conversation_id];
            conv.messages.push_back({"User", query});
            conv.messages.push_back({"Assistant", answer});
            conv.last_activity = std::chrono::system_clock::now();
            
            // Keep only last 10 exchanges (20 messages)
            if (conv.messages.size() > 20) {
                conv.messages.erase(conv.messages.begin(), conv.messages.begin() + 2);
            }
        }
        
        response["answer"] = answer;
        response["matches"] = matches;
        
        return build_http_response(200, "application/json", response.dump());
        
    } catch (const std::exception& e) {
        std::cerr << "Error in handle_query: " << e.what() << std::endl;
        json error_response;
        error_response["error"] = e.what();
        return build_http_response(500, "application/json", error_response.dump());
    } catch (...) {
        std::cerr << "Unknown error in handle_query" << std::endl;
        json error_response;
        error_response["error"] = "Unknown error occurred";
        return build_http_response(500, "application/json", error_response.dump());
    }
}

// Handle status endpoint
std::string handle_status() {
    json status;
    
    // Reload index to get current count
    load_index();
    
    status["documents_indexed"] = documents.size();
    
    // Since ingestion is separate, we just show the current state
    // The UI will show the document count which updates as ingestion progresses
    
    return build_http_response(200, "application/json", status.dump());
}

// Handle client connection
void handle_client(int client_socket) {
    std::vector<char> buffer(65536);
    int total_read = 0;
    int valread;
    
    // Read request headers first
    while ((valread = read(client_socket, buffer.data() + total_read, buffer.size() - total_read - 1)) > 0) {
        total_read += valread;
        buffer[total_read] = '\0';
        
        // Check if we have complete headers
        std::string current(buffer.data(), total_read);
        size_t header_end = current.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            // Parse to get content length
            HttpRequest temp_req = parse_http_request(current);
            
            // If POST request with body, ensure we read it all
            if (temp_req.method == "POST" && temp_req.headers.find("Content-Length") != temp_req.headers.end()) {
                int content_length = std::stoi(temp_req.headers["Content-Length"]);
                int body_start = header_end + 4;
                int body_read = total_read - body_start;
                
                // Read remaining body if needed
                while (body_read < content_length) {
                    valread = read(client_socket, buffer.data() + total_read, buffer.size() - total_read - 1);
                    if (valread <= 0) break;
                    total_read += valread;
                    body_read += valread;
                    buffer[total_read] = '\0';
                }
            }
            break;
        }
        
        // Resize buffer if needed
        if (total_read >= buffer.size() - 1024) {
            buffer.resize(buffer.size() * 2);
        }
    }
    
    if (total_read > 0) {
        std::string request_str(buffer.data(), total_read);
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

int main() {
    // Initialize llama backend
    llama_backend_init();
    llama_log_set([](enum ggml_log_level level, const char * text, void *) {
        if (level >= GGML_LOG_LEVEL_ERROR) {
            fprintf(stderr, "%s", text);
        }
    }, nullptr);
    
    // Load dynamic backends
    ggml_backend_load_all();
    
    // Initialize models
    std::cout << "Initializing models..." << std::endl;
    
    embeddings = new EmbeddingGenerator();
    if (!embeddings->init()) {
        std::cerr << "Failed to initialize embeddings" << std::endl;
        return 1;
    }
    
    llm = new LLMGenerator();
    if (!llm->init()) {
        std::cerr << "Failed to initialize LLM" << std::endl;
        return 1;
    }
    
    // Load existing index
    load_index();
    
    // Create socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == 0) {
        std::cerr << "Socket creation failed" << std::endl;
        return 1;
    }
    
    // Allow socket reuse
    int opt = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))) {
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
        try {
            std::thread client_thread(handle_client, client_socket);
            client_thread.detach();
        } catch (const std::exception& e) {
            std::cerr << "Error creating client thread: " << e.what() << std::endl;
            close(client_socket);
        }
    }
    
    // Cleanup
    delete embeddings;
    delete llm;
    delete vector_index;
    llama_backend_free();
    
    return 0;
}
