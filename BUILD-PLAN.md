# BUILD-PLAN — the Vulkan end, in dependency order

The fetch list is satisfied: everything open-source this stage
needs is vendored under `vendor/` (manifest in `SOURCES.md`).
What remains is the write list. The Jetson side also needs Free
Pascal for ARM64 and gcc (JetPack) — toolchains, installed on the
box, not vendored here.

1. **Device probe** — a throwaway program that opens the Orin GPU
   through the loader and prints its properties and queue families.
   Proves the driver path before anything is built on it.
2. **The host daemon** (C++, Vulkan-Hpp from `vendor/`) — instance,
   device, compute queue, command pool, descriptor layout for
   storage buffers; a control channel the BEAM harness drives (Unix
   socket, line protocol — the ace-sovereign harness pattern).
3. **The buffer manager** — host-visible, host-coherent pools on
   unified memory, plus the ownership table: every buffer has an
   owning layer, a state, and a transfer log; the GPU touches a
   buffer only under a recorded transfer.
4. **The SPIR-V gate + module registry** — `spirv-val` (built from
   `vendor/SPIRV-Tools`) in the load path; modules admitted by
   hash, once, into a registry mapping hash → pipeline.
5. **First workload** — the owner's ternary kernel as a compute
   shader, dispatched over a buffer. Gate: GPU results equal the
   Pascal CPU reference exactly (integer kernels — no tolerances).
   The shader arrives by the owner's Pascal-to-SPIR-V lowering, or
   by the glslang bootstrap (`vendor/glslang`) while the lowering
   matures; both end at a validated SPIR-V module.
6. **The queue seam** — the opcode table for the archive runtime's
   GPU queue (the mv6502 runtime drains writes to $C100–$CFFF as an
   ordered command stream): buffer create/copy, pipeline bind,
   dispatch, present; a BEAM-side translator turns the drained
   stream into host-daemon commands, deterministically.
7. **Render half** — Vulkan renders offscreen into a shared buffer;
   presentation is PureBasic's (the owner's ruling: PureBasic
   provides the graphics — his existing stack, the Voxel800 line).
   This repo builds no swapchain or display code; its render job
   ends at a completed frame in memory with a recorded handoff.
8. **Sync hardening** — fences/timeline semaphores wired to the
   ownership table; no implicit waits.

Not needed: CUDA toolkit, cuDNN, Nsight, vendor SDK accounts,
cloud services of any kind.
