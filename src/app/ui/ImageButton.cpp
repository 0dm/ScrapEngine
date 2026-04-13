#include <algorithm>
#include <app/ui/components/ImageButton.hpp>
#include <string>

namespace scrap::ui {

    ImageButton::ImageButton(const std::string& name, ImTextureID texture, const ImVec2& size,
                             Callback onClick)
        : Image(name, texture, size), onClick(onClick) {
    }

    void ImageButton::render() {
        if (!enabled)
            return;
        if (ImGui::ImageButton(name.c_str(), texture, size)) {
            if (onClick) {
                onClick();
            }
        }
    }

} // namespace scrap::ui
