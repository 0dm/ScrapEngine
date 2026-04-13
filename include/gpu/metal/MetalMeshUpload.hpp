#pragma once

#include <scrap/modeling/Mesh.hpp>

namespace scrap::gpu::metal {

    struct MetalMeshGpu;

    /// Builds shared Metal buffers from CPU mesh. `mtlDevice` is `id<MTLDevice>`.
    bool uploadMeshToMetal(void* mtlDevice, const scrap::modeling::Mesh& src, MetalMeshGpu& out);

    /// Updates existing Metal buffers when vertex/index counts match; otherwise re-uploads like `uploadMeshToMetal`.
    bool syncMetalMeshWithCpuMesh(void* mtlDevice, const scrap::modeling::Mesh& src,
                                  MetalMeshGpu& inOut);

} // namespace scrap::gpu::metal
