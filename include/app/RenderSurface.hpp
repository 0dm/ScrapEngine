#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

#include <app/Instance.hpp>

namespace sauce {

/**
 * Bridge between Vulkan and the native CAMetalLayer-backed view.
 */
struct RenderSurface {
  RenderSurface(const sauce::Instance& instance, void* metalLayerHandle);

  /**
   * Access the underlying SurfaceKHR (the Vulkan handle type for surfaces)
   */
  const vk::raii::SurfaceKHR& operator*() const & noexcept {
    return surface;
  }

  const vk::raii::SurfaceKHR* operator->() const & noexcept {
    return &surface;
  }

private:
  vk::raii::SurfaceKHR surface = nullptr;
};

}
