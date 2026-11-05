# AGENTS.md — law of this repository

1. **Vendor means source.** Dependencies live in `vendor/` as
   extracted source with pinned commits in SOURCES.md. A path, a
   URL, or a package-manager line is not a dependency satisfied.
2. **Upstream stays upstream.** Vendor subtrees keep their licenses
   and their bytes; local changes to a vendored tree are recorded
   in SOURCES.md or not made. The owner's layer is written outside
   `vendor/`.
3. **Verify before commit.** The SPIR-V gate is the doctrine in
   miniature: no module executes that has not been validated, and
   no claim lands in STATUS.md that has not been run.
4. **CUDA-free means CUDA-free.** Nothing in this repo links,
   loads, or shells out to a CUDA component.
