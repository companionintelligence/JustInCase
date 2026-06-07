# ══════════════════════════════════════════════════════════════════════
# JIC Dockerfile — multi-stage build
#
# Stage 1: Download & build llama.cpp (pinned tag)
# Stage 2: Build MuPDF
# Stage 3: Build sqlite-vec + our application
# Stage 4: Slim runtime image
# ══════════════════════════════════════════════════════════════════════

# ── Pinned versions ──────────────────────────────────────────────────
ARG LLAMA_CPP_TAG=b8250
ARG MUPDF_TAG=1.27.2

# ═══════════════════════ Stage 1: llama.cpp ══════════════════════════
FROM ubuntu:24.04 AS llama-builder

ARG LLAMA_CPP_TAG
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake git ca-certificates \
    --no-install-recommends && rm -rf /var/lib/apt/lists/*

WORKDIR /build
RUN git clone --depth 1 --branch ${LLAMA_CPP_TAG} \
        https://github.com/ggml-org/llama.cpp.git && \
    cd llama.cpp && \
    cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
        -DLLAMA_BUILD_TESTS=OFF \
        -DLLAMA_BUILD_EXAMPLES=OFF \
        -DLLAMA_BUILD_SERVER=OFF \
        -DLLAMA_STATIC=ON \
        -DBUILD_SHARED_LIBS=OFF \
        -DGGML_CCACHE=OFF \
        -DLLAMA_CURL=OFF \
        -DGGML_STATIC=ON \
        -DGGML_CPU_BACKEND=ON \
        . && \
    cmake --build build -- -j$(nproc) && \
    mkdir -p /llama-install/lib /llama-install/include && \
    find build -name "*.a" -exec cp {} /llama-install/lib/ \; && \
    cp -r include/* /llama-install/include/ 2>/dev/null || true && \
    cp -r ggml/include/* /llama-install/include/ 2>/dev/null || true && \
    cp -r common/*.h /llama-install/include/ 2>/dev/null || true && \
    cp -r src/*.h /llama-install/include/ 2>/dev/null || true

# ═══════════════════════ Stage 2: MuPDF ══════════════════════════════
FROM ubuntu:24.04 AS mupdf-builder

ARG MUPDF_TAG
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential git ca-certificates \
    --no-install-recommends && rm -rf /var/lib/apt/lists/*

WORKDIR /build
RUN git clone --depth 1 --branch ${MUPDF_TAG} \
        --recurse-submodules --shallow-submodules \
        https://github.com/ArtifexSoftware/mupdf.git && \
    cd mupdf && \
    make -j$(nproc) \
        HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no \
        HAVE_LEPTONICA=no HAVE_TESSERACT=no \
        shared=no prefix=/mupdf-install install

# ═══════════════════════ Stage 3: App build ══════════════════════════
FROM ubuntu:24.04 AS app-builder

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake git wget unzip ca-certificates \
    libopenblas-dev libsqlite3-dev \
    --no-install-recommends && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Pre-built libraries from earlier stages
COPY --from=llama-builder /llama-install /llama-install
COPY --from=llama-builder /build/llama.cpp/ggml/src /build/llama.cpp/ggml/src
COPY --from=mupdf-builder /mupdf-install /mupdf-install

# nlohmann/json (single header)
RUN mkdir -p include/nlohmann && \
    wget -O include/nlohmann/json.hpp \
    https://github.com/nlohmann/json/releases/download/v3.11.3/json.hpp

# cpp-httplib (single header). Pull from the tagged raw tree, not the releases
# asset — v0.18.3 has no httplib.h release asset (the download 404s); the raw
# single-header at the same tag is the canonical source.
RUN wget -O include/httplib.h \
    https://raw.githubusercontent.com/yhirose/cpp-httplib/v0.18.3/httplib.h

# sqlite-vec amalgamation. The tarball holds sqlite-vec.{c,h} at the root with no
# top-level dir, so --strip-components=1 would strip the files themselves and
# extract nothing (CMake then can't find vendor/sqlite-vec.c). Extract as-is.
RUN mkdir -p vendor && \
    wget -O /tmp/sqlite-vec.tar.gz \
        https://github.com/asg017/sqlite-vec/releases/download/v0.1.6/sqlite-vec-0.1.6-amalgamation.tar.gz && \
    tar xzf /tmp/sqlite-vec.tar.gz -C vendor && \
    rm /tmp/sqlite-vec.tar.gz

# SQLite amalgamation (with FTS5 enabled)
RUN wget -O /tmp/sqlite.zip \
        https://www.sqlite.org/2024/sqlite-amalgamation-3470200.zip && \
    cd /tmp && unzip sqlite.zip && \
    cp sqlite-amalgamation-3470200/sqlite3.c  /build/vendor/ && \
    cp sqlite-amalgamation-3470200/sqlite3.h  /build/vendor/ && \
    cp sqlite-amalgamation-3470200/sqlite3ext.h /build/vendor/ && \
    rm -rf /tmp/sqlite*

# Copy application source
COPY src/  ./src/
COPY CMakeLists.txt ./

# Build
RUN cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-O3" \
        -DCMAKE_VERBOSE_MAKEFILE=ON \
        . && \
    cmake --build build -- -j$(nproc)

# ═══════════════════════ Stage 4: Runtime ════════════════════════════
FROM ubuntu:24.04

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libopenblas0 libgomp1 \
    --no-install-recommends && rm -rf /var/lib/apt/lists/*

WORKDIR /app

COPY --from=app-builder /build/build/jic-server    /app/
COPY --from=app-builder /build/build/jic-ingestion /app/
COPY public/ ./public/

RUN mkdir -p data gguf_models

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
    CMD /bin/bash -c 'echo > /dev/tcp/localhost/8080' || exit 1

CMD ["./jic-server"]
