#pragma once

#include <app/ui/ImGuiComponent.hpp>
#include <string>

namespace scrap::ui {
    class Tooltip : public ImGuiComponent {
      public:
        explicit Tooltip(const std::string& name, std::string text);
        ~Tooltip() override;

        void render() override;

      private:
        std::string text;
    };
} // namespace scrap::ui
