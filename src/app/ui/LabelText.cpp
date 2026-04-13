#include <algorithm>
#include <app/ui/components/LabelText.hpp>
#include <string>

namespace scrap::ui {

    LabelText::LabelText(const std::string& name, const std::string& label, const std::string& text)
        : Text(name, text), label(label) {
    }

    void LabelText::render() {
        if (!enabled)
            return;
        ImGui::LabelText(label.c_str(), "%s", text.c_str());
    }

} // namespace scrap::ui
