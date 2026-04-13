#pragma once

#include <app/Entity.hpp>

namespace scrap {
    class Scene;
}

namespace scrap::editor {

    class SelectionManager {
      public:
        void select(int index) {
            selectedIndex = index;
        }
        void deselect() {
            selectedIndex = -1;
        }
        int getSelectedIndex() const {
            return selectedIndex;
        }
        bool hasSelection() const {
            return selectedIndex >= 0;
        }

        scrap::Entity* getSelectedEntity(scrap::Scene& scene);

      private:
        int selectedIndex = -1;
    };

} // namespace scrap::editor
