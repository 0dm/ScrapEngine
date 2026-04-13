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

        bool kvTableBegin(const char* id, float labelWeight = 0.52f) {
            if (!ImGui::BeginTable(id, 2, ImGuiTableFlags_SizingStretchProp)) {
                return false;
            }
            ImGui::TableSetupColumn("k", ImGuiTableColumnFlags_WidthStretch, labelWeight);
            ImGui::TableSetupColumn("v", ImGuiTableColumnFlags_WidthStretch, 1.0f - labelWeight);
            return true;
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
        ImGuiIO& io = ImGui::GetIO();
        const bool compactLayout = io.DisplaySize.x <= 520.0f;
        if (compactLayout) {
            ImGui::SetNextWindowPos(ImVec2(8.0f, 8.0f), ImGuiCond_Always);
            ImGui::SetNextWindowSize(
                ImVec2(io.DisplaySize.x - 16.0f, std::min(io.DisplaySize.y * 0.58f, 300.0f)),
                ImGuiCond_Always);
        } else {
            ImGui::SetNextWindowSize(ImVec2(420.f, 430.f), ImGuiCond_FirstUseEver);
        }
        if (ImGui::Begin("Diagnostics")) {
            ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(6.0f, 3.0f));

            if (g_appDebugStats) {
                const AppDebugStats& s = *g_appDebugStats;

                ImGui::SeparatorText("Scene");
                if (kvTableBegin("scene")) {
                    if (s.sceneLabel[0] != '\0') {
                        kvRow("Scene", s.sceneLabel);
                    }
                    kvRowFmt("Entities", "%u active / %u total", s.activeEntityCount,
                             s.entityCount);
                    kvRowFmt("Mesh primitives", "%u", s.meshPrimitiveCount);
                    kvRowFmt("Lights", "%u", s.gpuLightCount);
                    kvRowFmt("Physics tick", "%.0f Hz", s.physicsTickHz);
                    kvRowFmt("Physics lag", "%.3f ms",
                             static_cast<double>(s.physicsAccumulatorSec) * 1000.0);
                    ImGui::EndTable();
                }

                if (!compactLayout ||
                    ImGui::CollapsingHeader("Rendering", ImGuiTreeNodeFlags_DefaultOpen)) {
                    if (compactLayout) {
                        if (kvTableBegin("render_compact")) {
                            kvRowFmt("Drawable", "%u x %u px", s.drawableWidthPx,
                                     s.drawableHeightPx);
                            kvRowFmt("Scale", "%.2f", static_cast<double>(s.contentScale));
                            kvRowFmt("Drawables", "%u", s.metalDrawableCount);
                            kvRowStatus("PBR", s.metalPbrReady);
                            kvRowStatus("IBL", s.metalIblLoaded, "Loaded", "Fallback");
                            kvRow("Present", s.uncappedPresentation ? "Uncapped" : "Synced");
                            ImGui::EndTable();
                        }
                    } else {
                        ImGui::SeparatorText("Rendering");
                        if (kvTableBegin("render")) {
                            kvRowFmt("Drawable", "%u x %u px", s.drawableWidthPx,
                                     s.drawableHeightPx);
                            kvRowFmt("Scale", "%.2f", static_cast<double>(s.contentScale));
                            kvRowFmt("Drawables", "%u", s.metalDrawableCount);
                            kvRowStatus("PBR", s.metalPbrReady);
                            kvRowStatus("IBL", s.metalIblLoaded, "Loaded", "Fallback");
                            kvRow("Present", s.uncappedPresentation ? "Uncapped" : "Synced");
                            ImGui::EndTable();
                        }
                    }
                }

                if (!compactLayout || ImGui::CollapsingHeader("Input")) {
                    if (compactLayout) {
                        if (kvTableBegin("input_compact")) {
                            kvRowStatus("Window focus", s.appWindowKey, "Focused", "Background");
                            kvRow("Pointer mode", s.cursorCaptured ? "Captured" : "Free");
                            kvRowFmt("Pointer", "%.1f, %.1f", static_cast<double>(s.pointerXPts),
                                     static_cast<double>(s.pointerYPts));
                            kvRow("Dragging body", yesNo(s.draggingRigidBody));
                            if (s.worldDragImGuiUnblockFramesRemaining > 0 ||
                                s.pendingImGuiFocusClear) {
                                kvRowFmt("Unblock frames", "%d",
                                         s.worldDragImGuiUnblockFramesRemaining);
                                kvRow("Focus clear", yesNo(s.pendingImGuiFocusClear));
                            }
                            ImGui::EndTable();
                        }
                    } else {
                        ImGui::SeparatorText("Input");
                        if (kvTableBegin("input")) {
                            kvRowStatus("Window focus", s.appWindowKey, "Focused", "Background");
                            kvRow("Pointer mode", s.cursorCaptured ? "Captured" : "Free");
                            kvRowFmt("Pointer", "%.1f, %.1f", static_cast<double>(s.pointerXPts),
                                     static_cast<double>(s.pointerYPts));
                            kvRow("Dragging body", yesNo(s.draggingRigidBody));
                            if (s.worldDragImGuiUnblockFramesRemaining > 0 ||
                                s.pendingImGuiFocusClear) {
                                kvRowFmt("Unblock frames", "%d",
                                         s.worldDragImGuiUnblockFramesRemaining);
                                kvRow("Focus clear", yesNo(s.pendingImGuiFocusClear));
                            }
                            ImGui::EndTable();
                        }
                    }
                }
            }

            if (!compactLayout || ImGui::CollapsingHeader("GPU")) {
                if (compactLayout) {
                    if (kvTableBegin("gpu_compact")) {
                        const std::string& device = scrap::gpu::metal::defaultMetalDeviceNameUtf8();
                        kvRow("Backend", "Metal");
                        kvRow("Device", device.c_str());
                        ImGui::EndTable();
                    }
                } else {
                    ImGui::SeparatorText("GPU");
                    if (kvTableBegin("gpu")) {
                        const std::string& device = scrap::gpu::metal::defaultMetalDeviceNameUtf8();
                        kvRow("Backend", "Metal");
                        kvRow("Device", device.c_str());
                        ImGui::EndTable();
                    }
                }
            }

            if (!compactLayout && ImGui::CollapsingHeader("ImGui Input State")) {
                if (kvTableBegin("imgui_io")) {
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
            }

            ImGui::PopStyleVar();
        }
        ImGui::End();
    }

} // namespace scrap::ui
