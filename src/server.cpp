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
#include <arpa/inet.h>
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

// Helper function to get reason phrase for status code
std::string get_reason_phrase(int status_code) {
    switch (status_code) {
        case 200: return "OK";
        case 201: return "Created";
        case 202: return "Accepted";
        case 204: return "No Content";
        case 301: return "Moved Permanently";
        case 302: return "Found";
        case 304: return "Not Modified";
        case 400: return "Bad Request";
        case 401: return "Unauthorized";
        case 403: return "Forbidden";
        case 404: return "Not Found";
        case 405: return "Method Not Allowed";
        case 413: return "Request Entity Too Large";
        case 415: return "Unsupported Media Type";
        case 429: return "Too Many Requests";
        case 500: return "Internal Server Error";
        case 501: return "Not Implemented";
        case 502: return "Bad Gateway";
        case 503: return "Service Unavailable";
        default:  return "Unknown";
    }
}

// HTTP response builder with security headers
std::string build_http_response(int status_code, const std::string& content_type, const std::string& body) {
    std::ostringstream response;
    response << "HTTP/1.1 " << status_code << " " << get_reason_phrase(status_code) << "\r\n";
    response << "Content-Type: " << content_type << "\r\n";
    response << "Content-Length: " << body.length() << "\r\n";
    
    // Security headers
    response << "X-Frame-Options: DENY\r\n";
    response << "X-Content-Type-Options: nosniff\r\n";
    response << "X-XSS-Protection: 1; mode=block\r\n";
    response << "Content-Security-Policy: default-src 'self'; script-src 'self' 'unsafe-inline'; style-src 'self' 'unsafe-inline'\r\n";
    response << "Referrer-Policy: strict-origin-when-cross-origin\r\n";
    
    // CORS headers (restrictive configuration)
    response << "Access-Control-Allow-Origin: https://example.com\r\n";
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
    
    // Parse body based on Content-Length with validation
    const size_t MAX_REQUEST_SIZE = 30 * 1024 * 1024; // 30MB
    try {
        if (req.headers.find("Content-Length") != req.headers.end()) {
            long content_length = std::stol(req.headers["Content-Length"]);
            
            // Validate Content-Length
            if (content_length < 0 || static_cast<size_t>(content_length) > MAX_REQUEST_SIZE) {
                std::cerr << "Invalid Content-Length: " << content_length << std::endl;
                return req; // Return request with empty body
            }
            
            req.body.resize(static_cast<size_t>(content_length));
            stream.read(&req.body[0], content_length);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error parsing Content-Length: " << e.what() << std::endl;
        // Return request with empty body
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

// Connection tracking for rate limiting
struct ConnectionTracker {
    std::map<std::string, std::vector<std::chrono::system_clock::time_point>> connections;
    std::mutex tracker_mutex;
    const int MAX_CONNECTIONS_PER_IP = 100;
    const int RATE_LIMIT_WINDOW_SEC = 60;
    const int MAX_REQUESTS_PER_WINDOW = 60;
    
    bool should_allow_connection(const std::string& ip) {
        std::lock_guard<std::mutex> lock(tracker_mutex);
        auto now = std::chrono::system_clock::now();
        auto& timestamps = connections[ip];
        
        // Remove old timestamps outside the window
        timestamps.erase(
            std::remove_if(timestamps.begin(), timestamps.end(),
                [&](const auto& ts) {
                    return std::chrono::duration_cast<std::chrono::seconds>(now - ts).count() > RATE_LIMIT_WINDOW_SEC;
                }),
            timestamps.end()
        );
        
        // Check rate limit
        if (timestamps.size() >= static_cast<size_t>(MAX_REQUESTS_PER_WINDOW)) {
            return false;
        }
        
        // Record this connection
        timestamps.push_back(now);
        return true;
    }
};
ConnectionTracker connection_tracker;

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

// Handle static file serving with path traversal protection
std::string serve_static_file(const std::string& path) {
    try {
        std::string file_path = path;
        if (file_path == "/") {
            file_path = "/index.html";
        }
        
        // Remove leading slash
        if (!file_path.empty() && file_path[0] == '/') {
            file_path = file_path.substr(1);
        }
        
        // Construct full path and validate it's within public/
        fs::path requested_path = fs::path("public") / file_path;
        fs::path base_path = fs::canonical("public");
        
        // Check if requested file exists before canonicalizing
        if (!fs::exists(requested_path)) {
            return build_http_response(404, "text/plain", "Not Found");
        }
        
        // Resolve to canonical path and validate it's within public/
        fs::path canonical_path = fs::canonical(requested_path);
        
        // Check if canonical path starts with base_path (prevent path traversal)
        auto [base_end, path_end] = std::mismatch(
            base_path.begin(), base_path.end(),
            canonical_path.begin(), canonical_path.end()
        );
        
        if (base_end != base_path.end()) {
            return build_http_response(403, "text/plain", "Forbidden");
        }
        
        // Determine content type
        std::string content_type = "text/plain";
        std::string path_str = canonical_path.string();
        if (string_ends_with(path_str, ".html")) content_type = "text/html";
        else if (string_ends_with(path_str, ".css")) content_type = "text/css";
        else if (string_ends_with(path_str, ".js")) content_type = "application/javascript";
        else if (string_ends_with(path_str, ".json")) content_type = "application/json";
        else if (string_ends_with(path_str, ".pdf")) content_type = "application/pdf";
        
        // Read file
        std::ifstream file(canonical_path, std::ios::binary);
        if (!file.is_open()) {
            return build_http_response(403, "text/plain", "Forbidden");
        }
        std::stringstream buffer;
        buffer << file.rdbuf();
        std::string content = buffer.str();
        
        return build_http_response(200, content_type, content);
    } catch (const fs::filesystem_error& e) {
        std::cerr << "Filesystem error in serve_static_file: " << e.what() << std::endl;
        return build_http_response(404, "text/plain", "Not Found");
    } catch (const std::exception& e) {
        std::cerr << "Error in serve_static_file: " << e.what() << std::endl;
        return build_http_response(500, "text/plain", "Internal Server Error");
    }
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
        
        // Check if we should use context
        bool use_context = true;
        if (request_json.contains("use_context") && request_json["use_context"].is_boolean()) {
            use_context = request_json["use_context"];
        }
        std::cout << "Use context: " << (use_context ? "yes" : "no") << std::endl;
        
        json response;
        response["conversation_id"] = conversation_id;
        
        if (documents.empty() || !use_context) {
            std::cout << "Using LLM directly without document context" << std::endl;
            
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
            
            // Build prompt with just conversation history
            std::string prompt = "";
            
            // Add conversation history
            if (!history.empty()) {
                prompt += "Previous conversation:\n";
                // Only include last 6 exchanges (12 messages) to save tokens
                size_t start_idx = history.size() > 12 ? history.size() - 12 : 0;
                for (size_t i = start_idx; i < history.size(); i++) {
                    prompt += history[i].first + ": " + history[i].second + "\n";
                }
                prompt += "\n";
            }
            
            prompt += "User: " + query + "\n\nAssistant:";
            
            std::cout << "Generating LLM response..." << std::endl;
            std::string answer = llm->generate(prompt);
            std::cout << "LLM response generated: " << answer.substr(0, 50) << "..." << std::endl;
            
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
        std::string prompt = "You are a helpful emergency first aid assistant. Provide clear, practical advice.\n\n";
        
        if (!context.empty()) {
            // Limit context size more aggressively to prevent token overflow
            if (context.length() > 800) {
                context = context.substr(0, 800) + "...\n[REMAINING CONTENT TRUNCATED]\n";
                std::cout << "Truncated context to 800 characters" << std::endl;
            }
            prompt += "REFERENCE MATERIALS:\n" + context + "\n";
            prompt += "Based on the above information, please provide helpful advice.\n\n";
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

// Handle client connection with security protections
void handle_client(int client_socket) {
    const size_t MAX_REQUEST_SIZE = 30 * 1024 * 1024; // 30MB
    const int READ_TIMEOUT_SEC = 30;
    
    // Set socket timeout to prevent slowloris attacks
    struct timeval tv;
    tv.tv_sec = READ_TIMEOUT_SEC;
    tv.tv_usec = 0;
    if (setsockopt(client_socket, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        std::cerr << "Failed to set socket timeout" << std::endl;
    }
    
    std::vector<char> buffer(65536);
    int total_read = 0;
    int valread;
    
    // Read request headers first
    while ((valread = read(client_socket, buffer.data() + total_read, buffer.size() - total_read - 1)) > 0) {
        total_read += valread;
        buffer[total_read] = '\0';
        
        // Check size limits to prevent memory exhaustion
        if (static_cast<size_t>(total_read) >= MAX_REQUEST_SIZE) {
            std::cerr << "Request too large" << std::endl;
            std::string error_response = build_http_response(413, "text/plain", "Request Entity Too Large");
            send(client_socket, error_response.c_str(), error_response.length(), 0);
            close(client_socket);
            return;
        }
        
        // Check if we have complete headers
        std::string current(buffer.data(), total_read);
        size_t header_end = current.find("\r\n\r\n");
        if (header_end != std::string::npos) {
            // Parse to get content length
            HttpRequest temp_req = parse_http_request(current);
            
            // If POST request with body, ensure we read it all
            if (temp_req.method == "POST" && temp_req.headers.find("Content-Length") != temp_req.headers.end()) {
                try {
                    long content_length = std::stol(temp_req.headers["Content-Length"]);
                    
                    // Validate content length
                    if (content_length < 0 || static_cast<size_t>(content_length) > MAX_REQUEST_SIZE) {
                        std::cerr << "Invalid Content-Length" << std::endl;
                        std::string error_response = build_http_response(400, "text/plain", "Bad Request");
                        send(client_socket, error_response.c_str(), error_response.length(), 0);
                        close(client_socket);
                        return;
                    }
                    
                    int body_start = header_end + 4;
                    int body_read = total_read - body_start;
                    
                    // Read remaining body if needed
                    while (body_read < content_length) {
                        // Check total size limit
                        if (static_cast<size_t>(total_read) >= MAX_REQUEST_SIZE) {
                            std::cerr << "Request too large" << std::endl;
                            std::string error_response = build_http_response(413, "text/plain", "Request Entity Too Large");
                            send(client_socket, error_response.c_str(), error_response.length(), 0);
                            close(client_socket);
                            return;
                        }
                        
                        valread = read(client_socket, buffer.data() + total_read, buffer.size() - total_read - 1);
                        if (valread <= 0) break;
                        total_read += valread;
                        body_read += valread;
                        buffer[total_read] = '\0';
                    }
                } catch (const std::exception& e) {
                    std::cerr << "Error parsing Content-Length: " << e.what() << std::endl;
                    std::string error_response = build_http_response(400, "text/plain", "Bad Request");
                    send(client_socket, error_response.c_str(), error_response.length(), 0);
                    close(client_socket);
                    return;
                }
            }
            break;
        }
        
        // Resize buffer if needed (with safety check)
        if (total_read >= static_cast<int>(buffer.size()) - 1024) {
            size_t new_size = buffer.size() * 2;
            if (new_size > MAX_REQUEST_SIZE) {
                new_size = MAX_REQUEST_SIZE;
            }
            buffer.resize(new_size);
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
    
    // Listen for connections with increased backlog
    if (listen(server_fd, 128) < 0) {
        std::cerr << "Listen failed" << std::endl;
        return 1;
    }
    
    std::cout << "Server listening on port " << PORT << std::endl;
    
    // Accept connections
    while (true) {
        struct sockaddr_in client_addr;
        int addrlen = sizeof(client_addr);
        int client_socket = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t*)&addrlen);
        
        if (client_socket < 0) {
            std::cerr << "Accept failed" << std::endl;
            continue;
        }
        
        // Get client IP for rate limiting
        char client_ip[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(client_addr.sin_addr), client_ip, INET_ADDRSTRLEN);
        std::string ip_str(client_ip);
        
        // Check rate limit
        if (!connection_tracker.should_allow_connection(ip_str)) {
            std::cerr << "Rate limit exceeded for IP: " << ip_str << std::endl;
            std::string error_response = build_http_response(429, "text/plain", "Too Many Requests");
            send(client_socket, error_response.c_str(), error_response.length(), 0);
            close(client_socket);
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
