#pragma once

namespace scrap::gpu::metal {

    /// Anchor types for the future native Metal backend (MTLDevice, command queue, swapchain).
    /// `MetalToolchainProbe.mm` already compiles Metal/ObjC++ alongside Vulkan; extend here when
    /// `SCRAP_GPU_BACKEND_METAL=ON` is ready.

    struct MetalDevicePlaceholder {};

} // namespace scrap::gpu::metal
