#include <app/ui/components/DebugStatsWindow.hpp>

#include <app/ui/AppDebugStats.hpp>

#include <cstdarg>
#include <cstdio>
#include <string>

#include <gpu/metal/MetalDeviceInfo.hpp>
#include <imgui.h>

namespace scrap::ui {

    namespace {

        const AppDebugStats* g_appDebugStats = nullptr;

        void kvTableBegin(const char* id, float labelWeight = 0.52f) {
            const ImGuiTableFlags flags = ImGuiTableFlags_SizingStretchProp;
            if (ImGui::BeginTable(id, 2, flags)) {
                ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, labelWeight);
                ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 1.0f - labelWeight);
            }
        }

        void kvRow(const char* label, const char* value) {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(value);
        }

        void kvRowFmt(const char* label, const char* fmt, ...) {
            char buf[256];
            va_list args;
            va_start(args, fmt);
            std::vsnprintf(buf, sizeof(buf), fmt, args);
            va_end(args);
            kvRow(label, buf);
        }

        void kvRowStatus(const char* label, bool ok, const char* okText = "Ready",
                         const char* badText = "No") {
            ImGui::TableNextRow();
            ImGui::TableSetColumnIndex(0);
            ImGui::AlignTextToFramePadding();
            ImGui::TextUnformatted(label);
            ImGui::TableSetColumnIndex(1);
            ImGui::AlignTextToFramePadding();
            ImGui::TextColored(ok ? ImVec4(0.45f, 0.78f, 0.52f, 1.0f)
                                  : ImVec4(0.82f, 0.48f, 0.48f, 1.0f),
                               "%s", ok ? okText : badText);
        }

        const char* yesNo(bool v) {
            return v ? "Yes" : "No";
        }

    } // namespace

    void setAppDebugStatsSource(const AppDebugStats* stats) {
        g_appDebugStats = stats;
    }

    void DebugStatsWindow::render() {
        ImGui::SetNextWindowSize(ImVec2(420.f, 430.f), ImGuiCond_FirstUseEver);
        ImGui::Begin("Diagnostics");

        ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 3.0f));

        ImGuiIO& io = ImGui::GetIO();

        if (g_appDebugStats) {
            const AppDebugStats& s = *g_appDebugStats;

            ImGui::SeparatorText("Scene");
            kvTableBegin("scene");
            if (s.sceneLabel[0] != '\0') {
                kvRow("Scene", s.sceneLabel);
            }
            kvRowFmt("Entities", "%u active / %u total", s.activeEntityCount, s.entityCount);
            kvRowFmt("Mesh primitives", "%u", s.meshPrimitiveCount);
            kvRowFmt("Lights", "%u", s.gpuLightCount);
            kvRowFmt("Physics tick", "%.0f Hz", s.physicsTickHz);
            kvRowFmt("Physics lag", "%.3f ms",
                     static_cast<double>(s.physicsAccumulatorSec) * 1000.0);
            ImGui::EndTable();

            ImGui::SeparatorText("Rendering");
            kvTableBegin("render");
            kvRowFmt("Drawable", "%u x %u px", s.drawableWidthPx, s.drawableHeightPx);
            kvRowFmt("Content scale", "%.2f", static_cast<double>(s.contentScale));
            kvRowFmt("Drawables", "%u", s.metalDrawableCount);
            kvRowStatus("PBR pipeline", s.metalPbrReady);
            kvRowStatus("IBL", s.metalIblLoaded, "Loaded", "Fallback");
            kvRow("Presentation", s.uncappedPresentation ? "Uncapped" : "Display-synced");
            kvRow("Launcher", s.launcherActive ? "Visible" : "Hidden");
            ImGui::EndTable();

            ImGui::SeparatorText("Input");
            kvTableBegin("input");
            kvRowStatus("Window focus", s.appWindowKey, "Focused", "Background");
            kvRow("Pointer mode", s.cursorCaptured ? "Captured" : "Free");
            kvRowFmt("Pointer (points)", "%.1f, %.1f", static_cast<double>(s.pointerXPts),
                     static_cast<double>(s.pointerYPts));
            kvRow("Dragging body", yesNo(s.draggingRigidBody));
            if (s.worldDragImGuiUnblockFramesRemaining > 0 || s.pendingImGuiFocusClear) {
                kvRowFmt("Input unblock frames", "%d", s.worldDragImGuiUnblockFramesRemaining);
                kvRow("Focus clear pending", yesNo(s.pendingImGuiFocusClear));
            }
            ImGui::EndTable();
        }

        ImGui::SeparatorText("GPU");
        kvTableBegin("gpu");
        {
            const std::string& device = scrap::gpu::metal::defaultMetalDeviceNameUtf8();
            kvRow("Backend", "Metal");
            kvRow("Device", device.c_str());
        }
        ImGui::EndTable();

        if (ImGui::CollapsingHeader("ImGui Input State")) {
            kvTableBegin("imgui_io");
            kvRow("Capture mouse", yesNo(io.WantCaptureMouse));
            kvRow("Capture keyboard", yesNo(io.WantCaptureKeyboard));
            kvRow("App focus lost", yesNo(io.AppFocusLost));
            kvRowFmt("Mouse position", "%.1f, %.1f", static_cast<double>(io.MousePos.x),
                     static_cast<double>(io.MousePos.y));
            kvRow("Window hovered",
                  yesNo(ImGui::IsWindowHovered(ImGuiHoveredFlags_AnyWindow |
                                               ImGuiHoveredFlags_RootAndChildWindows)));
            kvRow("Window focused",
                  yesNo(ImGui::IsWindowFocused(ImGuiFocusedFlags_AnyWindow)));
            ImGui::EndTable();
        }

        ImGui::PopStyleVar();
        ImGui::End();
    }

} // namespace scrap::ui
