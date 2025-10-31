#!/bin/bash
# macOS Docker optimization script

echo "🍎 macOS Docker Optimization"
echo "=========================="

# Check current Docker settings
echo "Current Docker CPU allocation:"
docker info --format '{{.NCPU}}' 2>/dev/null || echo "Unable to detect"

echo ""
echo "📋 Optimization checklist:"
echo ""
echo "1. Open Docker Desktop"
echo "2. Go to Settings (⚙️) > Resources > Advanced"
echo "3. Set these values:"
echo "   - CPUs: Maximum available (drag slider all the way right)"
echo "   - Memory: At least 8 GB"
echo "   - Swap: 2-4 GB"
echo "   - Disk image size: Ensure you have enough space"
echo ""
echo "4. Click 'Apply & Restart'"
echo ""
echo "5. Additional optimizations:"
echo "   - Disable 'Use Rosetta for x86/amd64 emulation' if not needed"
echo "   - Enable 'Use virtualization framework' if available"
echo "   - Enable 'VirtioFS' for better file sharing performance"
echo ""
echo "Press Enter when Docker Desktop has been restarted..."
read

# Verify new settings
echo ""
echo "New Docker CPU allocation:"
docker info --format '{{.NCPU}}' 2>/dev/null || echo "Unable to detect"

# Set environment for maximum performance
export DOCKER_BUILDKIT=1
export BUILDKIT_PROGRESS=plain
export COMPOSE_DOCKER_CLI_BUILD=1
export DOCKER_DEFAULT_PLATFORM=linux/arm64

echo ""
echo "✅ Environment configured for maximum performance"
echo ""
echo "Now run: ./fast-build.sh"
