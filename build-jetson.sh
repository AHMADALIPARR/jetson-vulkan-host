#!/bin/sh
# build-jetson.sh — build and smoke-test the Stage 6 host on the Jetson.
#
# Gentle by design: the box is small and busy, so compilation runs at
# -j2 and the smoke tests allocate kilobytes. Run from the repo root
# of either form (the git checkout or the source tarball).
#
#   sh build-jetson.sh
#
# Produces: build/host/{device_probe,vkhost}, shaders under
# build/host/shaders/, and receipts in build/host/receipts/.
set -eu
cd "$(dirname "$0")"

JOBS="${JOBS:-2}"
BUILD=build/host

echo "== preflight"
if [ ! -e /usr/share/vulkan/icd.d/nvidia_icd.json ] && ! ls /usr/share/vulkan/icd.d/*.json >/dev/null 2>&1; then
    echo "WARN: no Vulkan ICD found under /usr/share/vulkan/icd.d — JetPack's ICD is expected."
fi
command -v cmake >/dev/null || { echo "FAIL: cmake not installed"; exit 1; }
command -v g++ >/dev/null || { echo "FAIL: g++ not installed"; exit 1; }
free -m | head -2 || true

echo "== configure"
cmake -S . -B "$BUILD" -DCMAKE_BUILD_TYPE=Release

echo "== build (-j$JOBS)"
cmake --build "$BUILD" -j"$JOBS"

echo "== gate check: shader modules compile + validate + register"
cat "$BUILD/shaders/registry.txt"

mkdir -p "$BUILD/receipts"
cd "$BUILD"

echo "== selftest (CPU-side; validates the sealed modules through the gate)"
./vkhost selftest shaders/ternary_dot.comp.spv shaders/fullscreen.vert.spv shaders/gradient.frag.spv \
    | tee receipts/selftest.txt

echo "== device probe"
./device_probe | tee receipts/probe.txt

echo "== compute workload (GPU must equal CPU reference exactly)"
./vkhost compute | tee receipts/compute.txt

echo "== render half (offscreen frame + handoff)"
./vkhost render | tee receipts/render.txt
sha256sum frame.rgba frame.handoff | tee receipts/frame.sha256

echo "== done. Receipts in $BUILD/receipts/"
