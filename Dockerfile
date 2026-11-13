# ══════════════════════════════════════════════════════════════════════
# JIC Dockerfile — multi-stage build
#
# Stage 1: Download & build llama.cpp (pinned tag)
# Stage 2: Build MuPDF
# Stage 3: Build sentry-native (OPT-IN — see JIC_SENTRY below)
# Stage 4: Build sqlite-vec + our application
# Stage 5: Slim runtime image
# ══════════════════════════════════════════════════════════════════════

# ── Pinned versions ──────────────────────────────────────────────────
# llama.cpp is pinned to b6591: the newest tag BEFORE common/ grew a
# cpp-httplib model downloader (b6593 = PR #16185, made unconditional when
# libcurl support was removed in PR #18828). JIC links libcommon.a with
# --whole-archive and bundles GGUF models locally, so any in-tree HTTP
# downloader becomes an undefined-reference at the final static link
# (httplib::Client / curl_easy_*). At b6591 the downloader is curl-only and
# compiles out entirely with -DLLAMA_CURL=OFF. See issue #10.
ARG LLAMA_CPP_TAG=b6591
ARG MUPDF_TAG=1.27.2
ARG SENTRY_NATIVE_VERSION=0.16.1

# Opt-out error reporting is optional at build time. JIC_SENTRY=0 (the default)
# downloads nothing, links nothing, and produces binaries with no Sentry code
# in them at all — src/telemetry.h compiles to no-ops. Build a reporting-capable
# image with:  docker build --build-arg JIC_SENTRY=1 .
# The DSN is NOT baked in here; it is supplied at runtime via SENTRY_DSN.
ARG JIC_SENTRY=0

# GPU offload for llama.cpp. `off` (the default) is byte-for-byte the CPU-only
# build this image has always produced — no new packages, no new layers.
#
#   docker build --build-arg JIC_GPU=vulkan .
#
# VULKAN, NOT CUDA, is the one bundled. CUDA and ROCm each need a vendor
# toolchain that only exists in a different base image (nvidia/cuda:*-devel,
# rocm/dev-ubuntu-*), so supporting them here would mean templating FROM and
# doubling the build matrix. Vulkan installs from Ubuntu's own archive and
# covers NVIDIA, AMD and Intel with one artifact — the right trade for an
# appliance that does not know what silicon it will land on. Project NOMAD
# solves the same problem by swapping the whole Ollama image per vendor,
# which it can do because it ships no inference code of its own.
#
# Setting JIC_N_GPU_LAYERS on a CPU-only image is NOT an error and NOT a
# warning from llama.cpp — it is silently ignored. `/status` therefore reports
# the backend that was actually compiled in, so the difference is visible.
ARG JIC_GPU=off

# ═══════════════════════ Stage 1: llama.cpp ══════════════════════════
FROM ubuntu:24.04 AS llama-builder

ARG LLAMA_CPP_TAG
ARG JIC_GPU
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake git ca-certificates libcurl4-openssl-dev \
    --no-install-recommends && rm -rf /var/lib/apt/lists/*

# Vulkan SDK bits, only when asked for. glslc (glslang-tools) is required:
# ggml compiles its Vulkan kernels at build time and the cmake configure step
# fails without it.
RUN if [ "${JIC_GPU}" = "vulkan" ]; then \
        apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
            libvulkan-dev glslang-tools --no-install-recommends && \
        rm -rf /var/lib/apt/lists/*; \
    fi

WORKDIR /build
RUN case "${JIC_GPU}" in \
      off|"")  GPU_FLAGS="" ;; \
      vulkan)  GPU_FLAGS="-DGGML_VULKAN=ON" ;; \
      cuda)    GPU_FLAGS="-DGGML_CUDA=ON" ;; \
      hip)     GPU_FLAGS="-DGGML_HIP=ON" ;; \
      *)       echo "JIC_GPU must be one of: off vulkan cuda hip (got '${JIC_GPU}')" >&2; exit 1 ;; \
    esac && \
    echo "GPU backend: ${JIC_GPU:-off}  ${GPU_FLAGS}" && \
    git clone --depth 1 --recurse-submodules --shallow-submodules --branch ${LLAMA_CPP_TAG} \
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
        ${GPU_FLAGS} \
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
RUN git clone --depth 1 --recurse-submodules --shallow-submodules --branch ${MUPDF_TAG} \
        --recurse-submodules --shallow-submodules \
        https://github.com/ArtifexSoftware/mupdf.git && \
    cd mupdf && \
    make -j$(nproc) \
        HAVE_X11=no HAVE_GLUT=no HAVE_CURL=no \
        HAVE_LEPTONICA=no HAVE_TESSERACT=no \
        shared=no prefix=/mupdf-install install

# ═════════════════ Stage 3: sentry-native (opt-in) ═══════════════════
# The stage always exists so `COPY --from=sentry-builder` resolves, but with
# JIC_SENTRY=0 it only creates an empty directory: no apt, no download, no
# build. Nothing new is contacted at runtime either — sentry-native uses the
# curl transport, and libcurl4 is already in the runtime image.
#
# Backend is inproc on purpose. crashpad would additionally require shipping a
# `crashpad_handler` executable in the runtime image and would upload
# minidumps; see the rationale at the top of src/telemetry.h.
FROM ubuntu:24.04 AS sentry-builder

ARG JIC_SENTRY
ARG SENTRY_NATIVE_VERSION
RUN mkdir -p /sentry-install && \
    if [ "$JIC_SENTRY" = "1" ]; then \
        apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
            build-essential cmake wget unzip ca-certificates \
            libcurl4-openssl-dev zlib1g-dev \
            --no-install-recommends && rm -rf /var/lib/apt/lists/* && \
        wget -O /tmp/sentry-native.zip \
            "https://github.com/getsentry/sentry-native/releases/download/${SENTRY_NATIVE_VERSION}/sentry-native.zip" && \
        mkdir -p /build/sentry-native && \
        unzip -q /tmp/sentry-native.zip -d /build/sentry-native && \
        rm /tmp/sentry-native.zip && \
        cmake -S /build/sentry-native -B /build/sentry-build \
            -DCMAKE_BUILD_TYPE=RelWithDebInfo \
            -DCMAKE_INSTALL_PREFIX=/sentry-install \
            -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
            -DSENTRY_BACKEND=inproc \
            -DSENTRY_TRANSPORT=curl \
            -DSENTRY_BUILD_SHARED_LIBS=OFF \
            -DSENTRY_BUILD_TESTS=OFF \
            -DSENTRY_BUILD_EXAMPLES=OFF && \
        cmake --build /build/sentry-build --parallel "$(nproc)" --target install ; \
    fi

# ═══════════════════════ Stage 4: App build ══════════════════════════
FROM ubuntu:24.04 AS app-builder

# libcurl4-openssl-dev is only needed when linking sentry-native (curl
# transport). It is installed unconditionally so the layer is identical in
# both build modes; it never reaches the runtime image.
RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    build-essential cmake git wget unzip ca-certificates \
    libopenblas-dev libsqlite3-dev libcurl4-openssl-dev \
    --no-install-recommends && rm -rf /var/lib/apt/lists/*

WORKDIR /build

# Pre-built libraries from earlier stages
COPY --from=llama-builder /llama-install /llama-install
COPY --from=llama-builder /build/llama.cpp/ggml/src /build/llama.cpp/ggml/src
COPY --from=mupdf-builder /mupdf-install /mupdf-install
COPY --from=sentry-builder /sentry-install /sentry-install

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
ARG JIC_SENTRY
ARG JIC_GPU
# The Vulkan loader is linked dynamically by CMakeLists (find_library(vulkan)),
# so the dev package has to exist in THIS stage too, not only where ggml's
# kernels were compiled.
RUN if [ "${JIC_GPU}" = "vulkan" ]; then \
        apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
            libvulkan-dev --no-install-recommends && rm -rf /var/lib/apt/lists/*; \
    fi
RUN if [ "$JIC_SENTRY" = "1" ]; then JIC_SENTRY_FLAG=ON; else JIC_SENTRY_FLAG=OFF; fi && \
    cmake -B build \
        -DCMAKE_BUILD_TYPE=Release \
        -DCMAKE_CXX_FLAGS="-O3" \
        -DCMAKE_VERBOSE_MAKEFILE=ON \
        -DJIC_SENTRY=$JIC_SENTRY_FLAG \
        -DJIC_SENTRY_PREFIX=/sentry-install \
        -DJIC_GPU="${JIC_GPU:-off}" \
        . && \
    cmake --build build -- -j$(nproc)

# ═══════════════════════ Stage 5: Runtime ════════════════════════════
FROM ubuntu:24.04

LABEL org.opencontainers.image.title="JIC — Just In Case" \
      org.opencontainers.image.description="Offline emergency knowledge assistant (RAG over local PDFs with llama.cpp)" \
      org.opencontainers.image.source="https://github.com/companionintelligence/JustInCase"

RUN apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
    libopenblas0 libgomp1 libcurl4 curl ca-certificates \
    --no-install-recommends && rm -rf /var/lib/apt/lists/*

# Vulkan runtime, only for a Vulkan image. mesa-vulkan-drivers supplies the
# AMD/Intel ICDs; an NVIDIA card uses the ICD from the host driver, which
# arrives with the device passthrough rather than from apt.
ARG JIC_GPU
RUN if [ "${JIC_GPU}" = "vulkan" ]; then \
        apt-get update && DEBIAN_FRONTEND=noninteractive apt-get install -y \
            libvulkan1 mesa-vulkan-drivers --no-install-recommends && \
        rm -rf /var/lib/apt/lists/*; \
    fi

# Non-root runtime user — the appliance never needs root
RUN groupadd -g 10001 jic && \
    useradd -r -u 10001 -g jic -d /app -s /usr/sbin/nologin jic

WORKDIR /app

COPY --from=app-builder /build/build/jic-server    /app/
COPY --from=app-builder /build/build/jic-ingestion /app/

# Web UI only. Knowledge content is NOT baked into the image — it lives
# in the jic-sources volume (.dockerignore excludes public/sources/).
COPY public/ ./public/

# Content fetcher + curated manifest, used by the compose "fetch" profile
COPY helper-scripts/fetch-source-data.sh /app/bin/fetch-sources
COPY sources.yaml /app/sources.yaml

RUN chmod 755 /app/bin/fetch-sources && \
    mkdir -p data gguf_models public/sources && \
    chown -R jic:jic /app

USER jic

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=60s --retries=3 \
    CMD curl -fsS http://localhost:8080/status >/dev/null || exit 1

CMD ["./jic-server"]
