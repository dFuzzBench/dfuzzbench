#!/bin/bash
# Build the Track A AFLGo images local/dfuzzbench-aflgo:base and :<project>.
# Usage (any cwd): bash source/aflgo/build_images.sh [project_name]

set -e
SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== Building AFLGo base image ==="
docker build -t local/dfuzzbench-aflgo:base -f "$SCRIPT_DIR/Dockerfile.base" "$SCRIPT_DIR"

PROJECTS="cmark libpng exiv2 guetzli md4c cpp-httplib varnish wamr clib libbpf"

if [ -n "${1:-}" ]; then
    PROJECTS="$1"
fi

for project in $PROJECTS; do
    echo ""
    echo "=== Building AFLGo image for $project ==="
    docker build -t "local/dfuzzbench-aflgo:$project" \
        -f "$SCRIPT_DIR/projects/$project/Dockerfile" \
        "$SCRIPT_DIR/projects/$project/"
done

echo ""
echo "=== All images built ==="
docker images local/dfuzzbench-aflgo
