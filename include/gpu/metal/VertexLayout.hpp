#pragma once

#include <glm/glm.hpp>

namespace scrap::gpu::metal {

    /// Interleaved vertex layout for Metal passes (matches engine mesh CPU layout).
    struct Vertex {
        glm::vec3 position;
        glm::vec3 normal;
        glm::vec2 texCoords;
        glm::vec3 color;
        glm::vec4 tangent;
    };

} // namespace scrap::gpu::metal
