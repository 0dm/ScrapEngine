#include "app/components/TransformComponent.hpp"

namespace scrap {

    TransformComponent::TransformComponent() : transform() {
    }

    TransformComponent::TransformComponent(const modeling::Transform& transform)
        : transform(transform) {
    }

} // namespace scrap
