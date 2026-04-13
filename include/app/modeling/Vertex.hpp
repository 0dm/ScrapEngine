#pragma once

#include <glm/glm.hpp>

namespace scrap {

/// Interleaved vertex layout (CPU + Metal); matches `scrap::gpu::metal::Vertex`.
struct Vertex {
  glm::vec3 position;
  glm::vec3 normal;
  glm::vec2 texCoords;
  glm::vec3 color;
  glm::vec4 tangent;
};

} // namespace scrap
