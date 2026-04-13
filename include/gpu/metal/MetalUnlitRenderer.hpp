#pragma once

#include <gpu/metal/MetalMeshGpu.hpp>

#include <glm/glm.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace scrap::gpu::metal {

    /// Minimal unlit pass: interleaved `Vertex` layout, per-draw MVP in vertex buffer slot 1.
    class MetalUnlitRenderer {
      public:
        MetalUnlitRenderer();
        ~MetalUnlitRenderer();

        MetalUnlitRenderer(const MetalUnlitRenderer&) = delete;
        MetalUnlitRenderer& operator=(const MetalUnlitRenderer&) = delete;

        void init(void* mtlDevice, std::uint32_t pixelFormatRaw);
        void draw(void* mtlRenderEncoder, float viewportWidth, float viewportHeight,
                  const glm::mat4& viewProj, const std::vector<MetalMeshGpu>& meshes,
                  const std::vector<glm::mat4>& modelMatrices);

        bool ready() const {
            return pImpl != nullptr;
        }

      private:
        struct Impl;
        std::unique_ptr<Impl> pImpl;
    };

} // namespace scrap::gpu::metal
