#pragma once

#include <app/ui/ImGuiComponent.hpp>
#include <imgui.h>

namespace scrap::ui {

    class DebugStatsWindow : public ImGuiComponent {
      public:
        DebugStatsWindow() : ImGuiComponent("DebugStatsWindow") {
        }

        void render() override;
    };

} // namespace scrap::ui
