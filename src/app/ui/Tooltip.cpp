#include <app/ui/components/Tooltip.hpp>
#include <imgui.h>

namespace scrap::ui {
    Tooltip::Tooltip(const std::string& name, std::string text)
        : ImGuiComponent(name), text{std::move(text)} {
    }

    Tooltip::~Tooltip() = default;

    void Tooltip::render() {
        if (enabled && ImGui::IsItemHovered()) {
            ImGui::SetTooltip(this->text.c_str());
        }
    }
} // namespace scrap::ui
