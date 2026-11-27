# Vulkan Memory Allocator

Unmodified Vulkan Memory Allocator (VMA) v3.5.0, vendored from [GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator) at tag `v3.5.0`, commit [`0f1a757da579831283413295cc7f3b47eb521c77`](https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/tree/0f1a757da579831283413295cc7f3b47eb521c77).

Downloaded on 2026-11-27 with the owner's approval (FD-24, planning decision 16). VMA is MIT-licensed; preserve the accompanying [LICENSE.txt](LICENSE.txt) when packaging this dependency.

| File | Git blob ID of the upstream bytes |
| --- | --- |
| `include/vk_mem_alloc.h` | `0c9b3279dd5492c01ad4172830f647559d0792c5` |
| `LICENSE.txt` | `be95175d6a296d066ed17617c094422d27174ad5` |

Upstream files:

- [include/vk_mem_alloc.h](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/v3.5.0/include/vk_mem_alloc.h)
- [LICENSE.txt](https://raw.githubusercontent.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator/v3.5.0/LICENSE.txt)

## Engine integration

A private system include of RavenCore, through the `RavenVma` interface target of [`cmake/RavenVma.cmake`](../../cmake/RavenVma.cmake), which also checks the version pin against the header. `Private/Renderer/Vulkan/VulkanMemory.cpp` is the only file that includes it and defines `VMA_IMPLEMENTATION` once; the rest of the renderer allocates through Raven's `VulkanMemory` seam, and no public header exposes VMA types. Replace the copy only with [`scripts/UpdateVma.cmake`](../../scripts/UpdateVma.cmake).
