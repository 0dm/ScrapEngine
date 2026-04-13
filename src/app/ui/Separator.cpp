#include <app/ui/components/Separator.hpp>

namespace scrap::ui {

    Separator::Separator(const std::string& name) : ImGuiComponent(name) {
    }

    void Separator::render() {
        if (!enabled)
            return;

        ImGui::Separator();
    }

} // namespace scrap::ui
