#pragma once

namespace scrap::gpu {

    enum class BackendKind {
        Metal,
    };

    inline constexpr BackendKind kActiveBackend = BackendKind::Metal;

} // namespace scrap::gpu
