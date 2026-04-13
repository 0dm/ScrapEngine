#pragma once

#include "app/modeling/PropertyValue.hpp"
#include "app/modeling/Vertex.hpp"
#include <memory>
#include <unordered_map>
#include <vector>

namespace scrap {
    namespace modeling {

        class Mesh {
          public:
            Mesh() = default;
            Mesh(const std::vector<scrap::Vertex>& vertices, const std::vector<uint32_t>& indices);
            ~Mesh();

            const std::vector<scrap::Vertex>& getVertices() const {
                return vertices;
            }
            std::vector<scrap::Vertex>& getVerticesMutable() {
                return vertices;
            }
            const std::vector<uint32_t>& getIndices() const {
                return indices;
            }

            size_t getVertexCount() const {
                return vertices.size();
            }
            size_t getIndexCount() const {
                return indices.size();
            }

            /// Always false: no embedded GPU buffers (Metal uploads from CPU mesh data).
            bool hasGPUData() const {
                return false;
            }

            bool isValid() const;

            const std::unordered_map<std::string, PropertyValue>& getMetadata() const {
                return metadata;
            }
            void setMetadata(const std::string& key, const PropertyValue& value);
            bool hasMetadata(const std::string& key) const;

            void generateNormals();
            void generateTangents();
            void setDynamicVertexBuffer(bool dynamic) {
                dynamicVertexBuffer = dynamic;
            }

          private:
            std::vector<scrap::Vertex> vertices;
            std::vector<uint32_t> indices;
            std::unordered_map<std::string, PropertyValue> metadata;
            bool dynamicVertexBuffer = false;
        };

    } // namespace modeling
} // namespace scrap
