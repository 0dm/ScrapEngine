#pragma once

#include <app/ui/ImGuiComponent.hpp>
#include <string>

namespace scrap::ui {
    class ProgressBar : public ImGuiComponent {
      public:
        explicit ProgressBar(const std::string& name, float fraction);
        ~ProgressBar() override;

        void render() override;

      private:
        float fraction;
    };
} // namespace scrap::ui
