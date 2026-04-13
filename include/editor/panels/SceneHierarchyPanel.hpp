#pragma once

#include <editor/panels/EditorPanel.hpp>

namespace scrap::editor {

    class SceneHierarchyPanel : public EditorPanel {
      public:
        SceneHierarchyPanel(EditorApp& app);
        void render() override;
    };

} // namespace scrap::editor
