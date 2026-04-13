#include <app/ui/components/DebugStatsWindow.hpp>

#include <app/ui/AppDebugStats.hpp>

#include <gpu/metal/MetalDeviceInfo.hpp>
#include <imgui.h>

namespace scrap::ui {

    namespace {

        const AppDebugStats* g_appDebugStats = nullptr;

    } // namespace

    void setAppDebugStatsSource(const AppDebugStats* stats) {
        g_appDebugStats = stats;
    }

    void DebugStatsWindow::render() {
        ImGui::SetNextWindowSize(ImVec2(420.f, 340.f), ImGuiCond_FirstUseEver);
        ImGui::Begin("ScrapEngine Debug");

        ImGuiIO& io = ImGui::GetIO();
        ImGui::Text("FPS: %.1f", io.Framerate);
        if (io.Framerate > 0.0f) {
            ImGui::Text("Frame time: %.3f ms", 1000.0f / io.Framerate);
        }
        ImGui::Text("Delta time: %.3f ms", static_cast<double>(io.DeltaTime) * 1000.0);

        if (g_appDebugStats) {
            const AppDebugStats& s = *g_appDebugStats;
            ImGui::Separator();
            ImGui::Text("Frame dt (app): %.3f ms", static_cast<double>(s.frameDeltaSec) * 1000.0);
            ImGui::Text("Physics: %.0f Hz  |  acc: %.3f ms", s.physicsTickHz,
                        static_cast<double>(s.physicsAccumulatorSec) * 1000.0);
            ImGui::Text("Entities: %u active / %u total", s.activeEntityCount, s.entityCount);
            ImGui::Text("Mesh primitives: %u", s.meshPrimitiveCount);
            ImGui::Text("GPU lights: %u", s.gpuLightCount);
            ImGui::Text("Viewport: %ux%u px", s.drawableWidthPx, s.drawableHeightPx);
            ImGui::Text("Content scale: %.2f", static_cast<double>(s.contentScale));
            if (s.sceneLabel[0] != '\0') {
                ImGui::TextWrapped("Scene: %s", s.sceneLabel);
            }
            ImGui::Text("Launcher UI: %s", s.launcherActive ? "yes" : "no");
            ImGui::Text("Present: %s",
                        s.uncappedPresentation ? "uncapped (no display sync)" : "display-synced");
            ImGui::Separator();
            ImGui::Text("Metal drawables: %u", s.metalDrawableCount);
            ImGui::Text("Metal PBR ready: %s", s.metalPbrReady ? "yes" : "no");
            ImGui::Text("IBL loaded: %s", s.metalIblLoaded ? "yes" : "no");
        }

        ImGui::Separator();
        ImGui::Text("Backend: Metal");
        ImGui::Text("Device: %s", scrap::gpu::metal::defaultMetalDeviceNameUtf8().c_str());

        ImGui::End();
    }

} // namespace scrap::ui
