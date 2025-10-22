FROM ubuntu:22.04 AS builder

# Install build dependencies
RUN apt-get update && apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    libopenblas-dev \
    libcurl4-openssl-dev \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Download only necessary llama.cpp files
RUN mkdir -p llama.cpp && cd llama.cpp && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/llama.h && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/llama.cpp && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/ggml.h && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/ggml.c && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/ggml-impl.h && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/ggml-alloc.h && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/ggml-alloc.c && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/ggml-backend.h && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/ggml-backend.c && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/ggml-backend-impl.h && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/ggml-quants.h && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/ggml-quants.c && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/unicode.h && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/unicode.cpp && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/unicode-data.h && \
    wget -q https://raw.githubusercontent.com/ggerganov/llama.cpp/master/unicode-data.cpp

# Install FAISS
RUN wget https://github.com/facebookresearch/faiss/archive/v1.7.4.tar.gz && \
    tar -xzf v1.7.4.tar.gz && \
    cd faiss-1.7.4 && \
    cmake -B build -DFAISS_ENABLE_GPU=OFF -DFAISS_ENABLE_PYTHON=OFF -DBUILD_TESTING=OFF . && \
    cmake --build build -- -j$(nproc) && \
    cmake --install build && \
    cd .. && rm -rf faiss-1.7.4 v1.7.4.tar.gz

# Copy source files
COPY server.cpp CMakeLists.txt ./

# Build the server
RUN cmake -B build . && \
    cmake --build build -- -j$(nproc)

# Runtime image
FROM ubuntu:22.04

RUN apt-get update && apt-get install -y \
    libopenblas0 \
    libcurl4 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the built binary
COPY --from=builder /build/build/jic-server /app/

# Copy web files from public directory
COPY public/ ./public/

# Create directories
RUN mkdir -p data sources gguf_models

EXPOSE 8080

CMD ["./jic-server"]
