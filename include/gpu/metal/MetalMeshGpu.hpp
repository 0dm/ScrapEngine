#pragma once

#include <cstdint>

namespace scrap::gpu::metal {

    /// Owns Metal buffer handles (as void* to avoid ObjC in headers). Filled by upload helpers in .mm.
    struct MetalMeshGpu {
        void* vertexBuffer = nullptr; // id<MTLBuffer>
        void* indexBuffer = nullptr;  // id<MTLBuffer>
        uint32_t indexCount = 0;
    };

    void releaseMetalMeshGpu(MetalMeshGpu& mesh);

} // namespace scrap::gpu::metal
