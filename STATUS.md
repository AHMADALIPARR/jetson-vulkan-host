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
- Bindings: both paths are now vendored. C++/Vulkan-Hpp stands;
  the owner called C# in (2026-11-04) and Silk.NET was extracted
  the same night (see SOURCES.md) — the .NET host path (C#, and F#
  on the same bindings) is fully sourced. Which language the host
  daemon is written in is still the owner's call at build time;
  both are buildable from this repo.

## Repository state

Working name `jetson-vulkan-host`. Local only; not pushed. The
sovereign license in `LICENSE` governs the owner's layer; `vendor/`
subtrees remain under their upstream licenses (see SOURCES.md).
