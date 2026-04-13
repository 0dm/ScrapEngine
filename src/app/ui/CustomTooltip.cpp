#include <app/ui/components/CustomTooltip.hpp>
#include <imgui.h>

namespace scrap::ui {
    CustomTooltip::CustomTooltip(const std::string& name, std::function<void()> renderFn)
        : ImGuiComponent(name), renderFn{std::move(renderFn)} {
    }

    CustomTooltip::~CustomTooltip() = default;

    void CustomTooltip::render() {
        if (enabled && ImGui::IsItemHovered()) {
            ImGui::BeginTooltip();
            this->renderFn();
            ImGui::EndTooltip();
        }
    }
} // namespace scrap::ui
