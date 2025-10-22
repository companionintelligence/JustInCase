# First stage: Download and cache llama.cpp
FROM --platform=linux/arm64 ubuntu:24.04 AS llama-downloader
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    git \
    ca-certificates \
    --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build
RUN git config --global http.sslverify false && \
    git clone --depth 1 https://github.com/ggerganov/llama.cpp.git && \
    git config --global http.sslverify true

# Second stage: Build
FROM --platform=linux/arm64 ubuntu:24.04 AS builder

# Install build dependencies
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    cmake \
    git \
    wget \
    ca-certificates \
    libopenblas-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy llama.cpp from first stage
COPY --from=llama-downloader /build/llama.cpp ./llama.cpp

# Copy source files
COPY server.cpp CMakeLists.txt ./

# Build the server with optimizations
RUN cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O3 -march=armv8-a" \
    . && \
    cmake --build build -- -j$(nproc)

# Runtime image
FROM --platform=linux/arm64 ubuntu:24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libopenblas0 \
    libcurl4 \
    --no-install-recommends \
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
