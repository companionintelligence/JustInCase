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

# Clone llama.cpp
RUN git clone https://github.com/ggerganov/llama.cpp.git

# Install FAISS
RUN wget https://github.com/facebookresearch/faiss/archive/v1.7.4.tar.gz && \
    tar -xzf v1.7.4.tar.gz && \
    cd faiss-1.7.4 && \
    cmake -B build -DFAISS_ENABLE_GPU=OFF -DFAISS_ENABLE_PYTHON=OFF -DBUILD_TESTING=OFF . && \
    cmake --build build -- -j$(nproc) && \
    cmake --install build

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
