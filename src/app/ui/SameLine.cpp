#include <app/ui/components/SameLine.hpp>

namespace scrap::ui {

    SameLine::SameLine(const std::string& name, float offsetFromStartX, float spacing)
        : ImGuiComponent(name), offsetFromStartX(offsetFromStartX), spacing(spacing) {
    }

    void SameLine::render() {
        if (!enabled)
            return;

        ImGui::SameLine(offsetFromStartX, spacing);
    }

} // namespace scrap::ui
