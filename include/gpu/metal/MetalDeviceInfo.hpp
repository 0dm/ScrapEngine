#pragma once

#include <string>

namespace scrap::gpu::metal {

    /// UTF-8 name of `MTLCreateSystemDefaultDevice()`, or a fallback string.
    std::string defaultMetalDeviceNameUtf8();

} // namespace scrap::gpu::metal
