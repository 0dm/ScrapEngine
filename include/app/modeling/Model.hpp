#pragma once

#include "app/modeling/Material.hpp"
#include "app/modeling/Mesh.hpp"
#include "app/modeling/ModelNode.hpp"
#include "app/modeling/PropertyValue.hpp"
#include <memory>
#include <unordered_map>
#include <vector>

namespace scrap {
    namespace modeling {

        class Model {
          public:
            Model() = default;

            std::shared_ptr<ModelNode> getRootNode() const {
                return rootNode;
            }
            void setRootNode(std::shared_ptr<ModelNode> root) {
                rootNode = root;
                rebuildFlatLists();
            }

            const std::vector<std::shared_ptr<Mesh>>& getAllMeshes() const {
                return allMeshes;
            }
            const std::vector<std::shared_ptr<Material>>& getAllMaterials() const {
                return allMaterials;
            }

            std::vector<MeshMaterialPair> getAllMeshMaterialPairs() const;

            const std::unordered_map<std::string, PropertyValue>& getMetadata() const {
                return metadata;
            }
            void setMetadata(const std::string& key, const PropertyValue& value) {
                metadata[key] = value;
            }
            bool hasMetadata(const std::string& key) const {
                return metadata.find(key) != metadata.end();
            }

            void rebuildFlatLists() {
                allMeshes.clear();
                allMaterials.clear();
                if (rootNode) {
                    traverseNode(rootNode);
                }
            }

          private:
            std::shared_ptr<ModelNode> rootNode;
            std::vector<std::shared_ptr<Mesh>> allMeshes;
            std::vector<std::shared_ptr<Material>> allMaterials;
            std::unordered_map<std::string, PropertyValue> metadata;

            void traverseNode(std::shared_ptr<ModelNode> node);
            void collectPairsFromNode(std::shared_ptr<ModelNode> node,
                                      std::vector<MeshMaterialPair>& pairs) const;
        };

    } // namespace modeling
} // namespace scrap
