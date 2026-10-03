#!/bin/bash
# Optional: build the OSS-Fuzz base images that the dfuzzbench target images are built on,
# from the copies in data/dfuzzbench/base-images, instead of pulling them from gcr.io.
#
# Usage (run from the agentic/ root):
#     bash scripts/build_images.sh
#
# This produces locally-tagged `gcr.io/oss-fuzz-base/{base-image,base-clang,base-builder,
# base-builder-python,base-runner}` images; base-clang compiles LLVM, which is slow. The
# per-target images `local/dfuzzbench:<tag>` are then built with scripts/build_dfuzzbench_image.sh.
# The T6 (ARVO) setting does not need this script: it runs the public `n132/arvo:<id>-vul` images.
set -eux

cd "$(dirname "$0")/.."          # -> agentic/
B=data/dfuzzbench/base-images

# Order matters: each image is FROM the previous one.
docker build --pull -t gcr.io/oss-fuzz-base/base-image          "$B/base-image"
docker build        -t gcr.io/oss-fuzz-base/base-clang          "$B/base-clang"
docker build        -t gcr.io/oss-fuzz-base/base-builder        "$B/base-builder"
docker build        -t gcr.io/oss-fuzz-base/base-builder-python "$B/base-builder-python"
docker build        -t gcr.io/oss-fuzz-base/base-runner         "$B/base-runner"
