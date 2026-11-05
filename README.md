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
