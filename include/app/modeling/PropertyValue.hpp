#pragma once

#include <string>
#include <variant>

namespace scrap {
    namespace modeling {

        // Variant type for storing metadata from GLTF extensions and extras
        using PropertyValue = std::variant<int, bool, float, double, std::string>;

    } // namespace modeling
} // namespace scrap
