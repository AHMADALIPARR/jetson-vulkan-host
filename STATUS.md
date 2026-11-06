# STATUS

## State (2026-11-06)

The host is written and builds end-to-end from the vendored tree:

- `src/device_probe.cpp` — BUILD-PLAN piece 1: physical-device and
  queue-family probe through the loader, with a clean refusal
  (exit 3) when no driver/ICD is present.
- `src/vkhost.cpp` — the host itself: the SPIR-V gate and SHA-256
  module registry (piece 4), the buffer manager on host-visible
  coherent memory with the ownership table (pieces 2, 3, 8), the
  ternary dot-product compute workload with its exact-match CPU
  gate (piece 5), and the offscreen render half with the PureBasic
  handoff file (piece 7). The queue seam (piece 6) remains a seam:
  the GPU-queue opcode stream from the archive runtime is not wired
  to the host yet — that work belongs with Stage 2 of the archive
  build, on the runtime side.
- `shaders/` — GLSL sources plus the sealed SPIR-V modules and
  `registry.txt` (their SHA-256 hashes). The build recompiles them
  with the vendored glslang and validates every module with the
  vendored spirv-val; a module whose bytes change is a different
  registry entry, not a silent replacement.
- `build-jetson.sh` — the one-command Jetson build + smoke test
  (-j2, receipts saved under `build/host/receipts/`).

## Verified (2026-11-06, this workspace — x86-64, no Vulkan ICD)

- The vendored SPIRV-Tools and glslang build from `vendor/` as
  pinned (AGENTS.md rule 1).
- The full CMake build compiles and links `device_probe` and
  `vkhost` with `-Wall -Wextra`, and compiles all three shader
  modules through glslang + spirv-val.
- `vkhost selftest` passes 10/10: SHA-256 against the FIPS vectors;
  the ownership graph refuses an illegal state skip and accepts the
  legal path; the ternary-dot CPU reference vectors; the gate
  admitting all three sealed modules by hash.
- The gate refuses: a module with one flipped byte fails admission.
- Build-produced modules are byte-identical to the sealed modules
  in `shaders/` (registry match) — glslang output is deterministic
  across these builds.
- `device_probe` and `vkhost compute` on a machine with no Vulkan
  ICD refuse cleanly with exit 3 and the driver's own reason — no
  crash, no hang.

## Not yet run

- **GPU execution.** This workspace has no Vulkan device, so the
  compute dispatch and the offscreen render have not run on silicon
  yet. The staged run is `sh build-jetson.sh` on the Jetson (Orin,
  JetPack ICD), which executes the probe, the compute workload
  under its exact-match gate, and the render half, and saves the
  receipts. Until that run's receipts exist, the GPU claims of this
  repo are: the code builds, the modules validate, the gates are
  wired — nothing more.
- The Pascal-to-SPIR-V lowering remains the owner's end of Stage 6;
  glslang is the bootstrap path until it lands.

## Repository state

Working name `jetson-vulkan-host`. Local only; not pushed. The
sovereign license in `LICENSE` governs the owner's layer; `vendor/`
subtrees remain under their upstream licenses (see SOURCES.md).
Two distribution forms: this git tree (full, including the Silk.NET
.NET path under `vendor/`), and a C++ source tarball without
`.git`, `build/`, or the Silk.NET tree for moving the host onto a
busy Jetson with minimal footprint.
