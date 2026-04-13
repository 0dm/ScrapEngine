#pragma once

#include <app/Camera.hpp>
#include <app/Entity.hpp>
#include <app/components/LightComponent.hpp>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include <physics/XPBD.hpp>

namespace scrap {

    namespace modeling {
        class ModelNode;
        class Model;
    } // namespace modeling

    class ScrapEngineApp;

    namespace editor {
        class EditorApp;
    }

    class Scene {
      public:
        Scene(const scrap::CameraCreateInfo& cameraCreateInfo, const std::string& filename = "") {
            pCamera = std::make_unique<scrap::Camera>(cameraCreateInfo);
        }

        const std::vector<scrap::Entity>& getEntities() const {
            return entities;
        }

        /**
   * Adds an entity to the scene
   */
        void addEntity(scrap::Entity&& entity);

        /**
   * Gets an entity by name (returns nullptr if not found)
   */
        scrap::Entity* getEntity(const std::string& name);

        /**
   * Loads a GLTF model and creates entities with components
   * @param filePath - path to the GLTF file
   * @param preserveHierarchy - if true, creates entity tree; if false, flattens to single level
   */
        void loadGLTFModel(const std::string& filePath, bool preserveHierarchy = true);

        /**
   * Saves the entire scene to a GLTF/GLB file
   * @return true on success
   */
        bool saveToFile(const std::string& filePath) const;

        /**
   * Loads a GLTF/GLB file as the entire scene (clears existing entities)
   * @return true on success
   */
        bool loadFromFile(const std::string& filePath);

        const std::string& getCurrentFilePath() const {
            return currentFilePath;
        }
        void setCurrentFilePath(const std::string& path) {
            currentFilePath = path;
        }
        bool hasFilePath() const {
            return !currentFilePath.empty();
        }

        /**
   * Returns a const ref to camera
   */
        const scrap::Camera& getCameraRO() const noexcept {
            return *pCamera;
        }

        std::vector<scrap::Entity>& getEntitiesMut() {
            return entities;
        }

        /**
   * Collects GPULight data from all active entities that have a LightComponent.
   * Each light's world position is taken from the entity's TransformComponent.
   * Returns a reference to an internal buffer (valid until the next call).
   */
        const std::vector<GPULight>& collectGPULights();

      private:
        std::vector<scrap::Entity> entities;

        std::unique_ptr<scrap::Camera> pCamera;

        std::string currentFilePath;
        std::vector<GPULight> gpuLightBuffer;

        // Helper functions for GLTF loading
        void loadGLTFNodeHierarchy(
            std::shared_ptr<modeling::ModelNode> node,
            std::unordered_map<modeling::ModelNode*, Entity*>& nodeToEntityMap,
            const std::string& filePath);
        void loadGLTFFlattened(std::shared_ptr<modeling::Model> model, const std::string& filePath);

        /**
   * Returns a non-const ref to camera
   */
        scrap::Camera& getCameraRW() noexcept {
            return *pCamera;
        }

        friend class scrap::
            ScrapEngineApp; // This gives the app class full access to entities and camera for user interaction
        friend class scrap::editor::EditorApp;
    };

} // namespace scrap
