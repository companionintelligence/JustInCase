FROM ubuntu:24.04 AS builder

# Install build dependencies
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    git \
    wget \
    libopenblas-dev \
    libcurl4-openssl-dev \
    libssl-dev \
    --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*

# Install newer CMake
RUN wget https://github.com/Kitware/CMake/releases/download/v3.27.7/cmake-3.27.7-linux-x86_64.sh && \
    chmod +x cmake-3.27.7-linux-x86_64.sh && \
    ./cmake-3.27.7-linux-x86_64.sh --skip-license --prefix=/usr/local && \
    rm cmake-3.27.7-linux-x86_64.sh

WORKDIR /build

# Clone llama.cpp with minimal depth
RUN git clone --depth 1 https://github.com/ggerganov/llama.cpp.git

# Copy source files
COPY server.cpp CMakeLists.txt ./

# Build the server
RUN cmake -B build . && \
    cmake --build build -- -j$(nproc)

# Runtime image
FROM ubuntu:24.04

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
