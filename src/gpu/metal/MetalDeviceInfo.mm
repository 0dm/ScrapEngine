#import <Metal/Metal.h>

#include <gpu/metal/MetalDeviceInfo.hpp>

#include <string>

namespace scrap::gpu::metal {

    std::string defaultMetalDeviceNameUtf8() {
        id<MTLDevice> d = MTLCreateSystemDefaultDevice();
        if (d == nil) {
            return "Metal (no device)";
        }
        NSString* n = d.name;
        if (n == nil || n.length == 0) {
            return "Metal";
        }
        return std::string(n.UTF8String);
    }

} // namespace scrap::gpu::metal
