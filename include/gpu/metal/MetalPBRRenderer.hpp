#pragma once

#include <gpu/metal/MetalIBLResources.hpp>
#include <gpu/metal/MetalMeshGpu.hpp>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace scrap {
    struct GPULight;
    namespace modeling {
        class Material;
    }
} // namespace scrap

namespace scrap::gpu::metal {

    /// One mesh instance for the Metal PBR forward pass (matches Vulkan scene draw bindings).
    struct MetalDrawablePBR {
        MetalMeshGpu mesh{};
        glm::mat4 model{1.0f};
        scrap::modeling::Material* material = nullptr;
        bool doubleSided = false;
    };

    /// HDR IBL + Cook-Torrance PBR + skybox (parallels `Renderer` + `shader_pbr` / `skybox` on Vulkan).
    class MetalPBRRenderer {
      public:
        MetalPBRRenderer();
        ~MetalPBRRenderer();

        MetalPBRRenderer(const MetalPBRRenderer&) = delete;
        MetalPBRRenderer& operator=(const MetalPBRRenderer&) = delete;

        void init(void* mtlDevice, std::uint32_t colorPixelFormatRaw);
        void shutdown();

        /// Active IBL (non-owning). Pass nullptr to use neutral gray fallbacks.
        void setIBL(const MetalIBLMaps* maps);

        bool ready() const;

        void draw(void* mtlRenderEncoder, float viewportWidth, float viewportHeight,
                  const glm::mat4& view, const glm::mat4& proj, const glm::vec3& cameraPos,
                  const scrap::GPULight* lights, std::uint32_t lightCount,
                  const MetalDrawablePBR* drawables, std::size_t drawableCount);

      private:
        struct Impl;
        std::unique_ptr<Impl> pImpl;
    };

} // namespace scrap::gpu::metal
