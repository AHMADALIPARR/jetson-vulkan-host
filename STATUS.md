# STATUS

## State (2026-11-04)

- Created on the owner's order ("make that a repository, extract
  the source, not just paths"): the Stage 6 Vulkan end of the Miami
  Vice archive build as its own repo.
- Vendored and verified: the seven Khronos source trees listed in
  `SOURCES.md` (223 MB total, pinned commits, `.git` stripped, key
  files spot-checked present). Nothing has been built from them
  yet — no compilation has run in this repo.
- The write list (BUILD-PLAN.md, 8 pieces) is specification, not
  code: the host daemon, buffer manager, SPIR-V gate, queue seam,
  and render half do not exist yet. The Pascal-to-SPIR-V lowering
  is the owner's end of Stage 6 and lives with his Pascal work.
- Bindings decision stands as C++/Vulkan-Hpp (vendored); the
  owner's C# remark is recorded in the Miami blueprint — Silk.NET
  is not vendored; if he calls C# back in, it gets extracted then.

## Repository state

Working name `jetson-vulkan-host`. Local only; not pushed. The
sovereign license in `LICENSE` governs the owner's layer; `vendor/`
subtrees remain under their upstream licenses (see SOURCES.md).
