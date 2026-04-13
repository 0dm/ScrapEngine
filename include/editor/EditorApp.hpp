#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

#include <imgui.h>

#include <string>
#include <vector>

#include <chrono>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <app/Scene.hpp>
#include <app/platform/PlatformWindow.hpp>
#include <gpu/vulkan/ImGuiRenderer.hpp>
#include <gpu/vulkan/Instance.hpp>
#include <gpu/vulkan/LogicalDevice.hpp>
#include <gpu/vulkan/PhysicalDevice.hpp>
#include <gpu/vulkan/RenderSurface.hpp>
#include <gpu/vulkan/Renderer.hpp>

#include <editor/EditorCamera.hpp>
#include <editor/OffscreenFramebuffer.hpp>
#include <editor/SelectionManager.hpp>
#include <editor/gizmos/Gizmo.hpp>

#include <app/Log.hpp>
#include <app/Settings.hpp>

#include <chrono>
#include <filesystem>
#include <memory>
#include <sys/types.h>

namespace scrap {
    class MeshRendererComponent;
} // namespace scrap

namespace scrap::ui {
    class SettingsWindow;
} // namespace scrap::ui

namespace scrap::editor {

    struct MeshPushConstants {
        glm::mat4 model;     // 64 bytes
        glm::vec4 baseColor; // 16 bytes
        float metallic;      // 4 bytes
        float roughness;     // 4 bytes
        float pad[2];        // 8 bytes alignment
    };
    // Total: 96 bytes (within 128-byte minimum guarantee)

    enum class ViewportMode {
        Unlit,
        Lit
    };

    class EditorPanel;
    class SceneHierarchyPanel;
    class InspectorPanel;
    class ViewportPanel;
    class AssetBrowserPanel;
    class GizmoRenderer;

    class EditorApp {
      public:
        EditorApp();
        ~EditorApp();

        void run();

        scrap::Scene& getScene() {
            return *pScene;
        }
        const scrap::Scene& getScene() const {
            return *pScene;
        }
        SelectionManager& getSelectionManager() {
            return selectionManager;
        }
        EditorCamera& getEditorCamera() {
            return editorCamera;
        }
        float getDeltaTime() const {
            return deltaTime;
        }

        void createEmptyEntity();
        void createBoxEntity();
        void createBallEntity();

        const scrap::PhysicalDevice& getPhysicalDevice() const {
            return physicalDevice;
        }
        const scrap::LogicalDevice& getLogicalDevice() const {
            return logicalDevice;
        }
        scrap::Renderer& getRenderer() {
            return *pRenderer;
        }

        OffscreenFramebuffer* getOffscreenFramebuffer() {
            return pOffscreenFB.get();
        }

        ViewportMode getViewportMode() const {
            return viewportMode;
        }
        void setViewportMode(ViewportMode mode) {
            viewportMode = mode;
        }

        void setStatusMessage(const std::string& msg) {
            statusMessage = msg;
            statusTimer = 5.0f;
        }

        void importGLTFToScene(const std::string& path);
        void replaceModelOnComponent(MeshRendererComponent& mrc, const std::string& path);
        void clearModelOnComponent(MeshRendererComponent& mrc);

        void openScene(const std::string& path);
        void saveScene();
        void saveSceneAs(const std::string& path);

        void setInitialSceneFile(const std::string& path) {
            initialSceneFile = path;
        }

        // Play mode
        void startPlayMode();
        void stopPlayMode();
        bool isInPlayMode() const {
            return playModeActive;
        }

      private:
        void initWindow();
        void initVulkan();
        void initEditor();
        void setupEditorTheme();
        void mainLoop();
        void buildEditorUI();
        void setupDefaultDockLayout(ImGuiID dockspaceId);
        void processInput(const scrap::platform::InputState& input);
        void handleKeyboardShortcuts(const scrap::platform::InputState& input);
        void handlePointerInput(const scrap::platform::InputState& input);
        void handleDroppedFiles(const scrap::platform::InputState& input);

        bool saveSceneToZip(const std::string& zipPath, const std::string& scenePath,
                            const std::vector<std::string>& assetPaths);
        std::string loadFileToString(const std::string& path);
        bool zipFolder(const std::filesystem::path& src, const std::filesystem::path& dst);
        bool unzipToFolder(const std::filesystem::path& zipPath,
                           const std::filesystem::path& outDir);
        void loadSceneFromZip(const std::string& zipPath);

        bool showExportZipDialog = false;
        bool showImportZipDialog = false;

        void recordEditorCommandBuffer(vk::raii::CommandBuffer& cmd, uint32_t imageIndex);
        void uploadMeshGPUResources();
        void pickEntityAtScreen(float windowX, float windowY);
        std::unique_ptr<scrap::platform::PlatformWindow> window;

        std::chrono::steady_clock::time_point lastFrameTime = std::chrono::steady_clock::now();
        float deltaTime = 0.0f;

        float lastMouseX = 0.0f;
        float lastMouseY = 0.0f;
        float mousePressX = 0.0f;
        float mousePressY = 0.0f;
        bool rightMouseDown = false;
        bool middleMouseDown = false;
        bool leftMouseDown = false;

        std::unique_ptr<scrap::Instance> pInstance;
        std::unique_ptr<scrap::RenderSurface> pRenderSurface;
        scrap::PhysicalDevice physicalDevice = nullptr;
        scrap::LogicalDevice logicalDevice = nullptr;

        std::unique_ptr<scrap::Scene> pScene;
        std::unique_ptr<scrap::Renderer> pRenderer;
        std::unique_ptr<scrap::ImGuiRenderer> pImGuiRenderer;

        // Editor rendering resources
        std::unique_ptr<OffscreenFramebuffer> pOffscreenFB;
        std::unique_ptr<scrap::GraphicsPipeline> pGridPipeline;
        std::unique_ptr<scrap::GraphicsPipeline> pUnlitPipeline;
        std::unique_ptr<scrap::GraphicsPipeline> pLitPipeline;
        std::unique_ptr<GizmoRenderer> pGizmoRenderer;

        SelectionManager selectionManager;
        EditorCamera editorCamera;

        std::unique_ptr<SceneHierarchyPanel> hierarchyPanel;
        std::unique_ptr<InspectorPanel> inspectorPanel;
        std::unique_ptr<ViewportPanel> viewportPanel;
        std::unique_ptr<AssetBrowserPanel> assetBrowserPanel;

        bool showHierarchy = true;
        bool showInspector = true;
        bool showViewport = true;
        bool showAssetBrowser = true;
        bool showSettings = false;

        bool firstFrame = true;
        bool viewportHovered = false;
        bool viewportFocused = false;

        ViewportMode viewportMode = ViewportMode::Unlit;
        GizmoType activeGizmoMode = GizmoType::Translate;
        bool gizmoInteracting = false;

        std::string statusMessage;
        float statusTimer = 0.0f;

        // File dialog state
        bool showOpenDialog = false;
        bool showSaveAsDialog = false;
        char dialogPathBuf[512] = {};

        std::string initialSceneFile;

        // Play mode state
        bool playModeActive = false;
        pid_t playProcessPid = -1;
        std::string playModeTempFile;

        scrap::SettingsManager settingsManager;
        std::string lastWorkingDirectory;
        std::unique_ptr<scrap::ui::SettingsWindow> settingsWindow;

        void applySettings(const scrap::EditorSettings& s);
    };

} // namespace scrap::editor
