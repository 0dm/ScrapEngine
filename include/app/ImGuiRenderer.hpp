#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_raii.hpp>

#include <app/LogicalDevice.hpp>
#include <app/PhysicalDevice.hpp>
#include <app/platform/PlatformView.hpp>

struct ImGuiContext;

namespace sauce {

struct SwapChain;

struct ImGuiRendererCreateInfo {
  const vk::raii::Instance& instance;
  const sauce::PhysicalDevice& physicalDevice;
  const sauce::LogicalDevice& logicalDevice;
  uint32_t queueFamilyIndex;
  sauce::platform::PlatformView& platformView;
  const vk::raii::Queue& queue;
  const vk::raii::CommandPool& commandPool;
  const sauce::SwapChain& swapChain;
  uint32_t imageCount;
  vk::Format swapChainFormat;
  vk::Format depthFormat;
};

class ImGuiRenderer {
public:
  explicit ImGuiRenderer(const ImGuiRendererCreateInfo& createInfo);
  ~ImGuiRenderer();

  void newFrame(float deltaTime);
  void render(const vk::raii::CommandBuffer& commandBuffer, uint32_t imageIndex);

private:
  void createDescriptorPool(const sauce::LogicalDevice& logicalDevice);
  void initImGui(const ImGuiRendererCreateInfo& createInfo);
  void uploadFonts(const ImGuiRendererCreateInfo& createInfo);

  ImGuiContext* imguiContext = nullptr;
  vk::raii::DescriptorPool imguiDescriptorPool = nullptr;
  sauce::platform::PlatformView* platformView = nullptr;
  const vk::raii::Device* device = nullptr;
};

} // namespace sauce
