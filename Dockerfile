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

# Second stage: Build llama.cpp library (cached separately)
FROM --platform=linux/arm64 ubuntu:24.04 AS llama-builder

# Install build dependencies
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential \
    cmake \
    --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Copy llama.cpp from first stage
COPY --from=llama-downloader /build/llama.cpp ./llama.cpp

# Build ONLY llama.cpp as a static library
# This layer will be cached unless llama.cpp source changes
RUN cd llama.cpp && \
    cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DLLAMA_BUILD_TESTS=OFF \
        -DLLAMA_BUILD_EXAMPLES=OFF \
        -DLLAMA_BUILD_SERVER=OFF \
        -DLLAMA_STATIC=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DGGML_CCACHE=OFF \
        -DLLAMA_CURL=OFF \
        . && \
    cmake --build build -- -j$(nproc) && \
    # Create a package of just what we need
    mkdir -p /llama-install/lib /llama-install/include && \
    # Find and copy all static libraries
    find build -name "*.a" -exec cp {} /llama-install/lib/ \; && \
    # Copy headers
    cp -r include/* /llama-install/include/ && \
    cp -r ggml/include/* /llama-install/include/ && \
    # List what we copied for debugging
    echo "Libraries copied:" && ls -la /llama-install/lib/ && \
    echo "Headers copied:" && ls -la /llama-install/include/

# Third stage: Build our server (this is the only part that rebuilds when server.cpp changes)
FROM --platform=linux/arm64 ubuntu:24.04 AS app-builder

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

# Copy pre-built llama.cpp libraries and headers
COPY --from=llama-builder /llama-install /llama-install
COPY --from=llama-builder /build/llama.cpp/ggml/src /build/llama.cpp/ggml/src

# Download nlohmann/json as a single header (much faster than git clone)
RUN mkdir -p include/nlohmann && \
    wget --no-check-certificate -O include/nlohmann/json.hpp \
    https://github.com/nlohmann/json/releases/download/v3.11.2/json.hpp && \
    echo "Downloaded nlohmann/json.hpp"

# Copy ONLY our application files
COPY server.cpp CMakeLists.txt ./

# Create a simplified CMakeLists.txt for faster builds
RUN cat > CMakeLists-fast.txt << 'EOF'
cmake_minimum_required(VERSION 3.16)
project(jic-server)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(Threads REQUIRED)

# Add pre-built llama library
add_library(llama STATIC IMPORTED)
set_target_properties(llama PROPERTIES
    IMPORTED_LOCATION /llama-install/lib/libllama.a
    INTERFACE_INCLUDE_DIRECTORIES /llama-install/include
)

# Add ggml library
add_library(ggml STATIC IMPORTED)
set_target_properties(ggml PROPERTIES
    IMPORTED_LOCATION /llama-install/lib/libggml.a
)

# Add nlohmann/json
include(FetchContent)
set(FETCHCONTENT_TRY_FIND_PACKAGE_MODE NEVER)
FetchContent_Declare(json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
    GIT_SHALLOW TRUE
    GIT_CONFIG "http.sslVerify=false"
)
FetchContent_MakeAvailable(json)

# Main executable
add_executable(jic-server server.cpp)

# Link libraries
target_link_libraries(jic-server 
    PRIVATE 
    llama
    ggml
    nlohmann_json::nlohmann_json
    Threads::Threads
    curl
    openblas
)

# Include directories
target_include_directories(jic-server PRIVATE 
    /llama-install/include
)
EOF

# Build our server using pre-built llama
RUN echo "=== Starting server build ===" && \
    echo "CPU cores available: $(nproc)" && \
    echo "System info:" && \
    uname -a && \
    echo "Memory available:" && \
    free -h && \
    echo "=== Configuring CMake ===" && \
    cmake -B build \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_CXX_FLAGS="-O3 -march=armv8-a" \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DCMAKE_RULE_MESSAGES=ON \
    -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
    . && \
    echo "=== CMake configuration complete, starting compilation ===" && \
    echo "Building with $(nproc) parallel jobs" && \
    cmake --build build --verbose -- -j$(nproc) VERBOSE=1 V=1

# Runtime image
FROM --platform=linux/arm64 ubuntu:24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libopenblas0 \
    libcurl4 \
    --no-install-recommends \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /app

# Copy the built binary
COPY --from=app-builder /build/build/jic-server /app/

# Copy web files from public directory
COPY public/ ./public/

# Create directories
RUN mkdir -p data sources gguf_models

EXPOSE 8080

CMD ["./jic-server"]
