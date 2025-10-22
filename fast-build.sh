#!/bin/bash
# Fast build script with progress monitoring

echo "🚀 Fast build script for JIC server"
echo "=================================="

# Detect available CPU cores
CORES=$(nproc 2>/dev/null || sysctl -n hw.ncpu 2>/dev/null || echo 4)
echo "💻 Detected $CORES CPU cores"

# Calculate optimal parallel jobs (for CPU-bound compilation, use all cores)
PARALLEL_JOBS=$CORES
echo "🔧 Using $PARALLEL_JOBS parallel jobs (all cores for compilation)"

# Set Docker BuildKit for better performance
export DOCKER_BUILDKIT=1
export BUILDKIT_PROGRESS=plain
export COMPOSE_DOCKER_CLI_BUILD=1

# Additional BuildKit optimizations
export BUILDKIT_INLINE_CACHE=1
export DOCKER_BUILDKIT_INLINE_CACHE=1

# Increase Docker's CPU allocation if on macOS
if [[ "$OSTYPE" == "darwin"* ]]; then
    echo "🍎 macOS detected - ensuring Docker has maximum CPU allocation"
    echo "   Check Docker Desktop > Settings > Resources > CPUs"
    echo "   Recommended: Set to maximum available cores ($CORES)"
    echo ""
    
    # Check if Docker is using all cores
    DOCKER_CPUS=$(docker info --format '{{.NCPU}}' 2>/dev/null || echo "unknown")
    if [[ "$DOCKER_CPUS" != "unknown" ]] && [[ "$DOCKER_CPUS" -lt "$CORES" ]]; then
        echo "⚠️  WARNING: Docker is only using $DOCKER_CPUS cores out of $CORES available"
        echo "   Please increase CPU allocation in Docker Desktop settings"
        echo ""
    fi
fi

# Check available memory
if command -v free >/dev/null 2>&1; then
    MEM_GB=$(free -g | awk '/^Mem:/{print $2}')
    echo "💾 Available memory: ${MEM_GB}GB"
elif [[ "$OSTYPE" == "darwin"* ]]; then
    MEM_GB=$(( $(sysctl -n hw.memsize) / 1024 / 1024 / 1024 ))
    echo "💾 Available memory: ${MEM_GB}GB"
fi

# Clean build with maximum parallelization
echo ""
echo "🔨 Starting optimized build..."
echo "   - Using BuildKit with inline cache"
echo "   - Parallel jobs: $PARALLEL_JOBS"
echo "   - CPU cores: $CORES"
echo ""

# Set make parallelization
export MAKEFLAGS="-j${PARALLEL_JOBS}"

# Time the build
START_TIME=$(date +%s)

# Build with verbose output and optimizations
docker compose build \
    --no-cache \
    --progress=plain \
    --parallel \
    --build-arg MAKEFLAGS="-j${PARALLEL_JOBS}"

BUILD_EXIT_CODE=$?

END_TIME=$(date +%s)
DURATION=$((END_TIME - START_TIME))
MINUTES=$((DURATION / 60))
SECONDS=$((DURATION % 60))

if [ $BUILD_EXIT_CODE -eq 0 ]; then
    echo ""
    echo "✅ Build completed successfully in ${MINUTES}m ${SECONDS}s!"
    echo ""
    echo "📊 Build statistics:"
    echo "   - Total time: ${DURATION} seconds"
    echo "   - CPU cores used: $CORES"
    echo "   - Parallel jobs: $PARALLEL_JOBS"
    echo ""
    echo "To run the server:"
    echo "  docker compose up"
else
    echo ""
    echo "❌ Build failed after ${MINUTES}m ${SECONDS}s"
    echo ""
    echo "💡 Troubleshooting tips:"
    echo "   1. Check Docker Desktop CPU/memory allocation"
    echo "   2. Try building with fewer parallel jobs:"
    echo "      MAKEFLAGS='-j2' docker compose build"
    echo "   3. Check build logs above for specific errors"
    exit 1
fi
