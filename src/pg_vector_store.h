#pragma once

#include <string>
#include <vector>
#include <sstream>
#include <iostream>
#include <cstring>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <netdb.h>
#include "nlohmann/json.hpp"

using json = nlohmann::json;

struct Document {
    std::string filename;
    std::string text;
};

// PostgreSQL client using raw TCP sockets
class PGVectorStore {
private:
    int sock = -1;
    std::string host;
    int port;
    std::string dbname;
    std::string user;
    std::string password;
    int dimension;
    
    bool send_query(const std::string& query) {
        // For now, we'll use a simple HTTP API approach
        // In production, you'd implement the PostgreSQL wire protocol
        // or use a lightweight PostgreSQL client library
        
        // This is a placeholder - in reality we'd need to implement
        // the PostgreSQL protocol or use a REST API wrapper
        std::cerr << "PGVectorStore: Would execute query: " << query.substr(0, 100) << "..." << std::endl;
        return true;
    }
    
    std::string escape_string(const std::string& str) {
        std::string result;
        for (char c : str) {
            if (c == '\'') result += "''";
            else result += c;
        }
        return result;
    }
    
    std::string vector_to_string(const std::vector<float>& vec) {
        std::ostringstream oss;
        oss << "[";
        for (size_t i = 0; i < vec.size(); i++) {
            if (i > 0) oss << ",";
            oss << vec[i];
        }
        oss << "]";
        return oss.str();
    }
    
public:
    PGVectorStore(const std::string& host, int port, const std::string& dbname, 
                  const std::string& user, const std::string& password, int dimension)
        : host(host), port(port), dbname(dbname), user(user), password(password), dimension(dimension) {}
    
    ~PGVectorStore() {
        disconnect();
    }
    
    bool connect() {
        // For MVP, we'll use HTTP REST API to PostgreSQL
        // This avoids complex PostgreSQL protocol implementation
        std::cout << "PGVectorStore: Connecting to " << host << ":" << port << std::endl;
        
        // Create tables via REST API or admin tool
        std::string create_table = 
            "CREATE TABLE IF NOT EXISTS documents ("
            "id SERIAL PRIMARY KEY, "
            "filename TEXT NOT NULL, "
            "chunk_text TEXT NOT NULL, "
            "embedding vector(" + std::to_string(dimension) + "), "
            "created_at TIMESTAMP DEFAULT CURRENT_TIMESTAMP)";
        
        send_query(create_table);
        
        std::string create_index = 
            "CREATE INDEX IF NOT EXISTS documents_embedding_idx "
            "ON documents USING ivfflat (embedding vector_l2_ops) WITH (lists = 100)";
        
        send_query(create_index);
        
        return true;
    }
    
    void disconnect() {
        if (sock >= 0) {
            close(sock);
            sock = -1;
        }
    }
    
    bool add_document(const std::string& filename, const std::string& text, const std::vector<float>& embedding) {
        std::string query = "INSERT INTO documents (filename, chunk_text, embedding) VALUES ('" +
                           escape_string(filename) + "', '" + 
                           escape_string(text) + "', '" +
                           vector_to_string(embedding) + "'::vector)";
        return send_query(query);
    }
    
    std::vector<std::pair<Document, float>> search(const std::vector<float>& query_vec, int k) {
        std::vector<std::pair<Document, float>> results;
        
        std::string query = "SELECT filename, chunk_text, embedding <-> '" + 
                           vector_to_string(query_vec) + "'::vector as distance " +
                           "FROM documents " +
                           "ORDER BY embedding <-> '" + vector_to_string(query_vec) + "'::vector " +
                           "LIMIT " + std::to_string(k);
        
        // Placeholder - would parse results from PostgreSQL response
        std::cout << "PGVectorStore: Search query would return results" << std::endl;
        
        return results;
    }
    
    size_t size() {
        // Placeholder
        return 0;
    }
    
    bool file_exists(const std::string& filename) {
        // Placeholder
        return false;
    }
};
