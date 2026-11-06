# Jetson Vulkan Host — the GPU end of the Miami Vice archive

This repository is Stage 6 of the Miami Vice Magazine Archive
Execution Architecture (blueprint in the `miami-vice-archive` repo):
the Vulkan end that runs on the Jetson. Pascal defines the compute
kernels; a Pascal-to-SPIR-V lowering path turns them into SPIR-V;
this layer validates those modules, owns the GPU, and executes them
— CUDA-free at the application level, end to end.

The upstream Vulkan/SPIR-V source this layer builds on is vendored
in `vendor/` (see `SOURCES.md` for the pinned manifest): the actual
source, extracted, so the build never depends on an upstream being
reachable. `BUILD-PLAN.md` is the build-out: the host daemon, the
buffer manager with its ownership table, the SPIR-V gate and module
registry, the queue seam to the runtime, and the render half — in
dependency order, with their gates.

Licensing: the owner's layer in this repository is under the
sovereign license in `LICENSE`. The `vendor/` subtrees are upstream
Khronos code and stay under their own licenses, which ship inside
each subtree.

## The host

Two binaries build from `src/` against the vendored headers:

- `device_probe` opens the GPU through the Vulkan loader and prints
  its properties and queue families. It reports a missing or
  incompatible driver as a clean refusal, never a crash.
- `vkhost` owns the GPU work. Its subcommands:
  - `selftest` runs the CPU-side checks (SHA-256 against the FIPS
    vectors, the ownership transition graph, the ternary-dot CPU
    reference) and admits any SPIR-V modules named on the command
    line through the gate.
  - `compute` dispatches the first workload — the ternary
    dot-product kernel (`shaders/ternary_dot.comp`) — over
    host-visible buffers. The GPU result must equal the CPU
    reference exactly; integer kernels admit no tolerance.
  - `render` draws the offscreen frame (`fullscreen.vert` +
    `gradient.frag`, 320×240 RGBA8) into a host buffer and writes
    the handoff: `frame.rgba` plus `frame.handoff` carrying the
    frame's dimensions and hashes. There is no swapchain and no
    display code in this host; presentation is PureBasic's.
  - `all` runs compute, then render.

Every SPIR-V module enters through the gate — SPIR-V Tools
validation — and is registered by SHA-256 before any pipeline
exists; a corrupted module is refused. The sealed modules live in
`shaders/` beside their GLSL sources, with their hashes in
`shaders/registry.txt`. Every buffer the GPU touches moves through
the ownership table (`src/ownership.h`): one owning layer, one
state, a recorded transfer for every handoff, and illegal
transitions refused rather than applied.

## Building

On the Jetson:

```sh
sh build-jetson.sh
```

The script compiles at `-j2` (the box is small and busy), validates
and registers the shader modules during the build, then runs the
self-test, the device probe, the compute workload, and the render
half, saving every output under `build/host/receipts/`.

Anywhere else, the same build is plain CMake:

```sh
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Release
cmake --build build/host -j4
```

The build compiles the vendored SPIR-V Tools and glslang first;
the GPU subcommands need a Vulkan ICD at run time (JetPack's on the
Jetson) and refuse cleanly without one.
