#pragma once

#include <cstdint>
#include <memory>
#include <string>

namespace scrap::gpu::metal {

    /// GPU resources for image-based lighting (matches Vulkan IBL pipeline outputs).
    struct MetalIBLMaps {
        void* envMapArray = nullptr;     // id<MTLTexture> 2D array, 6 faces, RGBA16Float, 512²
        void* irradianceArray = nullptr; // 2D array, 6 faces, RGBA16Float, 32²
        void* prefilterArray = nullptr;  // 2D array, 6 faces, RGBA16Float, mips (128 base)
        void* brdfLUT = nullptr;         // id<MTLTexture> 2D RG16Float 512²
        void* samplerState = nullptr;    // id<MTLSamplerState>
    };

    void releaseMetalIBLMaps(MetalIBLMaps& maps);

    void deleteMetalIBLMaps(MetalIBLMaps* maps);

    using MetalIBLMapsPtr = std::unique_ptr<MetalIBLMaps, void (*)(MetalIBLMaps*)>;

    /// Loads an HDR file (.hdr), builds cubemap + irradiance + prefiltered specular + BRDF LUT on the GPU.
    MetalIBLMapsPtr generateMetalIBLMaps(void* mtlDevice, const std::string& hdrPath);

} // namespace scrap::gpu::metal
