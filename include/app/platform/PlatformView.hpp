#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <imgui.h>

namespace scrap::platform {

    struct FramebufferExtent {
        std::uint32_t width = 0;
        std::uint32_t height = 0;
    };

    enum class Key : std::size_t {
        Escape = 0,
        GraveAccent,
        DeleteKey,
        F,
        E,
        R,
        O,
        N,
        P,
        Space,
        LeftShift,
        RightShift,
        LeftControl,
        RightControl,
        W,
        A,
        S,
        D,
        Count,
    };

    enum class MouseButton : std::size_t {
        Left = 0,
        Right,
        Middle,
        Count,
    };

    struct MouseButtonState {
        bool down = false;
        bool pressed = false;
        bool released = false;
    };

    struct PointerState {
        float x = 0.0f;
        float y = 0.0f;
        float deltaX = 0.0f;
        float deltaY = 0.0f;
        float scrollX = 0.0f;
        float scrollY = 0.0f;
        std::array<MouseButtonState, static_cast<std::size_t>(MouseButton::Count)> buttons{};
    };

    struct InputState {
        std::array<bool, static_cast<std::size_t>(Key::Count)> keys{};
        std::array<bool, static_cast<std::size_t>(Key::Count)> keyPressed{};
        std::array<bool, static_cast<std::size_t>(Key::Count)> keyReleased{};
        PointerState pointer{};
        std::vector<std::string> droppedPaths;
        bool controlDown = false;
        bool shiftDown = false;
        bool altDown = false;
        bool superDown = false;
        bool framebufferResized = false;
        bool toggleCursorCaptureRequested = false;
        bool closeRequested = false;
    };

    inline constexpr std::size_t keyIndex(Key key) {
        return static_cast<std::size_t>(key);
    }

    class PlatformView {
      public:
        virtual ~PlatformView() = default;

        virtual void* getMetalLayerHandle() const = 0;
        virtual FramebufferExtent getFramebufferExtent() const = 0;
        virtual float getContentScaleFactor() const = 0;
        virtual void pumpEvents() = 0;
        virtual bool shouldClose() const = 0;
        virtual void requestClose() = 0;
        virtual void setWindowTitle(const std::string& title) = 0;
        virtual void setWindowSize(uint32_t width, uint32_t height) = 0;
        virtual InputState consumeInputState() = 0;
        virtual void prepareImGuiFrame(ImGuiIO& io, float deltaTime) = 0;
        virtual void setCursorCaptured(bool captured) = 0;
    };

    struct PlatformWindowCreateInfo {
        std::string title;
        uint32_t width = 1280;
        uint32_t height = 720;
        bool resizable = true;
        bool acceptFileDrops = false;
    };

    class PlatformWindow;

    std::unique_ptr<PlatformWindow> createPlatformWindow(
        const PlatformWindowCreateInfo& createInfo);

} // namespace scrap::platform
