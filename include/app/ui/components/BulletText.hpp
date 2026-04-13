#pragma once

#include <app/ui/components/Text.hpp>

namespace scrap::ui {
    class BulletText : public Text {
      public:
        BulletText(const std::string& name, const std::string& text);

        void render() override;
    };

} // namespace scrap::ui
