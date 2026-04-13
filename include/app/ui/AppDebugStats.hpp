#pragma once

#include <cstdint>

namespace scrap::ui {

    /// Filled each frame by ScrapEngineApp; read by DebugStatsWindow.
    struct AppDebugStats {
        float frameDeltaSec = 0.f;
        float physicsAccumulatorSec = 0.f;
        double physicsTickHz = 128.0;
        std::uint32_t entityCount = 0;
        std::uint32_t activeEntityCount = 0;
        std::uint32_t meshPrimitiveCount = 0;
        std::uint32_t gpuLightCount = 0;
        std::uint32_t drawableWidthPx = 0;
        std::uint32_t drawableHeightPx = 0;
        float contentScale = 1.f;
        bool launcherActive = false;
        bool uncappedPresentation = false;
        std::uint32_t metalDrawableCount = 0;
        bool metalPbrReady = false;
        bool metalIblLoaded = false;
        char sceneLabel[160] = {};
        // Input / focus (filled after processInput in the same tick)
        bool cursorCaptured = false;
        bool appWindowKey = true;
        bool pendingImGuiFocusClear = false;
        int worldDragImGuiUnblockFramesRemaining = 0;
        bool draggingRigidBody = false;
        float pointerXPts = 0.f;
        float pointerYPts = 0.f;
    };

    void setAppDebugStatsSource(const AppDebugStats* stats);

} // namespace scrap::ui
