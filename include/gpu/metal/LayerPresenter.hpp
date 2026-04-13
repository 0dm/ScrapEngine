#pragma once

#include <cstdint>
#include <functional>
#include <memory>

#include <scrap/MacroKitchen.hpp>
#include <scrap/platform/PlatformView.hpp>

namespace scrap::gpu::metal {

    /// `CAMetalLayer` presenter; optional Dear ImGui via Metal backend.
    class LayerPresenter {
      public:
        LayerPresenter();
        ~LayerPresenter();

        SCRAP_NON_COPYABLE(LayerPresenter)

        void init(void* cametalLayerPtr, uint32_t width, uint32_t height, float contentScale);
        /// Logical points × content scale (initial window size before layout).
        void setDrawableSize(uint32_t width, uint32_t height, float contentScale);
        /// Backing-store pixels — must match `PlatformView::getFramebufferExtent()` width/height (do not scale again).
        void setDrawableSizePixels(uint32_t widthPx, uint32_t heightPx);
        void presentClear(float r, float g, float b, float a);

        /// Call after `ImGui::CreateContext` / `StyleColorsDark`; uses internal `MTLDevice`.
        void initDearImGui();
        void shutdownDearImGui();

        /// `encodeBeforeImGui` runs with color+depth load cleared; use for scene pass before Dear ImGui.
        /// Drawable pixel size matches the current color attachment (use for Metal viewport / projection aspect).
        void renderDearImGuiFrame(
            scrap::platform::PlatformView& view, float deltaTime,
            const std::function<void()>& buildUi,
            const std::function<void(void* mtlRenderEncoder, std::uint32_t drawableWidthPx,
                                     std::uint32_t drawableHeightPx)>& encodeBeforeImGui = {});

        void* mtlDevice() const;
        std::uint32_t drawablePixelFormat() const;

        void shutdown();

      private:
        struct Impl;
        std::unique_ptr<Impl> pImpl;

        void ensureDepthTexture(std::uint32_t w, std::uint32_t h);
    };

} // namespace scrap::gpu::metal
