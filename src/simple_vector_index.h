#pragma once

#include <vector>
#include <algorithm>
#include <queue>
#include <fstream>
#include <iostream>

// Simple vector index implementation - preserved for reference
// This is a brute-force nearest neighbor search implementation
class SimpleVectorIndex {
private:
    std::vector<float> embeddings_data;  // Store all embeddings in a single contiguous array
    int dimension;
    int num_vectors;
    
public:
    SimpleVectorIndex(int dim) : dimension(dim), num_vectors(0) {
        // Reserve initial capacity to avoid frequent reallocations
        embeddings_data.reserve(1000 * dimension);
    }
    
    void add(const std::vector<float>& embedding) {
        embeddings_data.insert(embeddings_data.end(), embedding.begin(), embedding.end());
        num_vectors++;
    }
    
    void add_batch(int n, const float* data) {
        // Direct copy is much more efficient
        embeddings_data.insert(embeddings_data.end(), data, data + n * dimension);
        num_vectors += n;
    }
    
    std::vector<std::pair<int, float>> search(const std::vector<float>& query, int k) {
        if (num_vectors == 0) return {};
        
        // Use a min-heap to keep only top k results
        std::priority_queue<std::pair<float, int>> top_k;
        
        // Process embeddings in chunks to improve cache locality
        const int chunk_size = 16;  // Process 16 vectors at a time
        
        for (int base_idx = 0; base_idx < num_vectors; base_idx += chunk_size) {
            int end_idx = std::min(base_idx + chunk_size, num_vectors);
            
            for (int i = base_idx; i < end_idx; i++) {
                float dist = 0;
                const float* embedding = embeddings_data.data() + i * dimension;
                
                // Unroll loop for better performance
                int j = 0;
                for (; j + 4 <= dimension; j += 4) {
                    float d0 = query[j] - embedding[j];
                    float d1 = query[j+1] - embedding[j+1];
                    float d2 = query[j+2] - embedding[j+2];
                    float d3 = query[j+3] - embedding[j+3];
                    dist += d0*d0 + d1*d1 + d2*d2 + d3*d3;
                }
                // Handle remaining dimensions
                for (; j < dimension; j++) {
                    float diff = query[j] - embedding[j];
                    dist += diff * diff;
                }
                
                if (top_k.size() < k) {
                    top_k.push({dist, i});
                } else if (dist < top_k.top().first) {
                    top_k.pop();
                    top_k.push({dist, i});
                }
            }
        }
        
        // Extract results from heap
        std::vector<std::pair<int, float>> results;
        results.reserve(top_k.size());
        while (!top_k.empty()) {
            results.push_back({top_k.top().second, top_k.top().first});
            top_k.pop();
        }
        std::reverse(results.begin(), results.end());
        
        return results;
    }
    
    size_t size() const { return num_vectors; }
    
    void clear() { 
        embeddings_data.clear();
        num_vectors = 0;
    }
    
    void save(const std::string& path) {
        std::ofstream file(path, std::ios::binary);
        file.write((char*)&num_vectors, sizeof(num_vectors));
        file.write((char*)&dimension, sizeof(dimension));
        file.write((char*)embeddings_data.data(), num_vectors * dimension * sizeof(float));
        std::cout << "Saved " << num_vectors << " embeddings to " << path << std::endl;
    }
    
    void load(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            std::cout << "No existing index found at " << path << std::endl;
            return;
        }
        
        file.read((char*)&num_vectors, sizeof(num_vectors));
        file.read((char*)&dimension, sizeof(dimension));
        
        embeddings_data.resize(num_vectors * dimension);
        file.read((char*)embeddings_data.data(), num_vectors * dimension * sizeof(float));
        
        std::cout << "Loaded " << num_vectors << " embeddings from " << path << std::endl;
    }
};
