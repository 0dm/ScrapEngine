#pragma once

#include <app/platform/PlatformView.hpp>

namespace scrap::platform {

    /// On macOS, set the process working directory to the folder containing the executable (e.g.
    /// MyApp.app/Contents/MacOS) so bundled shaders and assets resolve when launched from Finder.
    void setWorkingDirectoryToExecutableFolder();

    struct PlatformWindowImpl;

    class PlatformWindow : public PlatformView {
      public:
        ~PlatformWindow() override;
        void* getMetalLayerHandle() const override;
        FramebufferExtent getFramebufferExtent() const override;
        float getContentScaleFactor() const override;
        void pumpEvents() override;
        bool shouldClose() const override;
        void requestClose() override;
        void setWindowTitle(const std::string& title) override;
        void setWindowSize(uint32_t width, uint32_t height) override;
        InputState consumeInputState() override;
        void prepareImGuiFrame(ImGuiIO& io, float deltaTime) override;
        void setCursorCaptured(bool captured) override;

      private:
        explicit PlatformWindow(std::unique_ptr<PlatformWindowImpl> impl);

        std::unique_ptr<PlatformWindowImpl> impl;

        friend std::unique_ptr<PlatformWindow> createPlatformWindow(
            const PlatformWindowCreateInfo& createInfo);
    };

} // namespace scrap::platform
