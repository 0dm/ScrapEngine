#include <app/RenderSurface.hpp>

#include <stdexcept>

#import <QuartzCore/CAMetalLayer.h>

namespace sauce {

RenderSurface::RenderSurface(const sauce::Instance& instance, void* metalLayerHandle) {
  if (metalLayerHandle == nullptr) {
    throw std::runtime_error("Failed to create Metal surface: CAMetalLayer handle was null");
  }

  auto* createMetalSurface = reinterpret_cast<PFN_vkCreateMetalSurfaceEXT>(
      vkGetInstanceProcAddr(static_cast<VkInstance>(**instance), "vkCreateMetalSurfaceEXT"));
  if (createMetalSurface == nullptr) {
    throw std::runtime_error("vkCreateMetalSurfaceEXT is unavailable on this Vulkan loader");
  }

  VkMetalSurfaceCreateInfoEXT createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_METAL_SURFACE_CREATE_INFO_EXT;
  createInfo.pLayer = (__bridge CAMetalLayer*)metalLayerHandle;

  VkSurfaceKHR cSurface = VK_NULL_HANDLE;
  if (VkResult res =
          createMetalSurface(static_cast<VkInstance>(**instance), &createInfo, nullptr, &cSurface);
      res != VK_SUCCESS) {
    throw std::runtime_error(
        "Failed to create Metal surface! Error code: " + std::to_string(res));
  }

  surface = { *instance, cSurface };
}

} // namespace sauce
