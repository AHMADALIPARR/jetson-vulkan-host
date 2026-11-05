# SOURCES — vendored upstream manifest

Extracted 2026-11-04 from the KhronosGroup GitHub organization
(depth-1 snapshots, `.git` stripped): the source itself lives in
`vendor/`, not just paths to it. Each subtree keeps its upstream
LICENSE file and remains under its upstream license (Apache-2.0
family); the sovereign license in this repo's root `LICENSE` governs
the owner's layer, not these trees.

| Tree | Upstream | Commit | Size |
|---|---|---|---|
| Vulkan-Headers | github.com/KhronosGroup/Vulkan-Headers | `9fbc2a782ee3f8a4db64e7d991e23da75e893f44` | 45 MB |
| Vulkan-Loader | github.com/KhronosGroup/Vulkan-Loader | `f82038b3106955365543e434db7b7167aa0fc9f3` | 13 MB |
| Vulkan-Hpp | github.com/KhronosGroup/Vulkan-Hpp | `83ffc5972e157eee9815f75d2cf5e73a7ba93890` | 27 MB |
| SPIRV-Headers | github.com/KhronosGroup/SPIRV-Headers | `6efe5ae6b7c6ad29da3f2cc3f164b9c3d72b78b1` | 4.9 MB |
| SPIRV-Tools | github.com/KhronosGroup/SPIRV-Tools | `b570f00a782a7bf4006964e8284dd8c10c4c549b` | 34 MB |
| glslang | github.com/KhronosGroup/glslang | `9e78d9ffe78a8bc7fa1d3326f2da91d19eba3479` | 60 MB |
| Vulkan-ValidationLayers | github.com/KhronosGroup/Vulkan-ValidationLayers | `615ed222e657ff79d07acb3db69776a09d1842e5` | 69 MB |
| Silk.NET | github.com/dotnet/Silk.NET | `fd0c2871dd08019b9e6a166f13f7bfe44fb4f11a` | 1,011 MB |

Silk.NET (.NET Foundation, MIT — upstream's license, in-tree) is the
.NET binding set the owner called in (2026-11-04: "Yes C# and even
F# can perform well"). The Vulkan binding lives at
`vendor/Silk.NET/src/Vulkan/Silk.NET.Vulkan`. **F# needs no separate
extraction**: F# consumes the same .NET assemblies and compiles to
the same IL, so F# rides this tree — its seat is the host-side
translator and ownership logic. The tree is vendored whole (all of
Silk.NET's API bindings), per the owner's extraction rule.

## Build wiring notes (from the trees themselves)

- SPIRV-Tools expects SPIRV-Headers at `external/spirv-headers`
  inside its tree: place the vendored SPIRV-Headers there at build
  time.
- glslang builds standalone; its SPIR-V backend uses its own
  SPIRV-Headers copy already in-tree.
- Vulkan-ValidationLayers' full build pulls its declared
  dependencies with its own `scripts/update_deps.py` (network at
  build time); the vendored snapshot is the layers' own source.
  The layers are a development tool, not a runtime dependency.

Spot-verified at extraction: `Vulkan-Headers/include/vulkan/vulkan.h`,
`Vulkan-Hpp/vulkan/vulkan.hpp`, `Vulkan-Loader/loader/loader.c`,
`glslang/glslang/Public/ShaderLang.h`,
`SPIRV-Tools/include/spirv-tools/libspirv.h`,
`SPIRV-Headers/include/spirv/1.2/spirv.h` all present.
