#pragma once

#include <app/modeling/PropertyValue.hpp>
#include <editor/panels/EditorPanel.hpp>
#include <unordered_map>

namespace scrap {

    class Entity;

    namespace modeling {
        class Mesh;
        class Material;
    } // namespace modeling

} // namespace scrap

namespace scrap::editor {

    class InspectorPanel : public EditorPanel {
      public:
        InspectorPanel(EditorApp& app);
        void render() override;

      private:
        void drawTransformSection(scrap::Entity& entity);
        void drawMeshRendererSection(scrap::Entity& entity);
        void drawClothSection(scrap::Entity& entity);
        void drawMetadataSection(
            const std::string& label,
            const std::unordered_map<std::string, scrap::modeling::PropertyValue>& metadata);
    };

} // namespace scrap::editor
