#include <app/Scene.hpp>
#include <editor/SelectionManager.hpp>

namespace scrap::editor {

    scrap::Entity* SelectionManager::getSelectedEntity(scrap::Scene& scene) {
        auto& entities = scene.getEntitiesMut();
        if (selectedIndex < 0 || selectedIndex >= static_cast<int>(entities.size())) {
            return nullptr;
        }
        return &entities[selectedIndex];
    }

} // namespace scrap::editor
