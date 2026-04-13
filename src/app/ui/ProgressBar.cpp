#include <app/ui/components/ProgressBar.hpp>
#include <imgui.h>

namespace scrap::ui {
    ProgressBar::ProgressBar(const std::string& name, float fraction)
        : ImGuiComponent(name), fraction{fraction} {
    }

    ProgressBar::~ProgressBar() = default;

    void ProgressBar::render() {
        if (enabled) {
            ImGui::ProgressBar(this->fraction);
        }
    }
} // namespace scrap::ui
