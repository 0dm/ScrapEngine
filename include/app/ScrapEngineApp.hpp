#pragma once

#include <imgui.h>

#include <array>
#include <cstdlib>
#include <functional>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <string>
#include <vector>

#include <app/Scene.hpp>
#include <app/platform/PlatformView.hpp>
#include <app/ui/AppDebugStats.hpp>
#include <app/ui/ImGuiComponentManager.hpp>
#include <app/ui/components/BulletText.hpp>
#include <app/ui/components/Button.hpp>
#include <app/ui/components/Checkbox.hpp>
#include <app/ui/components/DebugStatsWindow.hpp>
#include <app/ui/components/Image.hpp>
#include <app/ui/components/ImageButton.hpp>
#include <app/ui/components/LabelText.hpp>
#include <app/ui/components/RadioButton.hpp>
#include <app/ui/components/Text.hpp>
#include <app/ui/components/TextColored.hpp>
#include <app/ui/components/TextWrapped.hpp>
#include <gpu/metal/LayerPresenter.hpp>
#include <gpu/metal/MetalIBLResources.hpp>
#include <gpu/metal/MetalPBRRenderer.hpp>
#include <launcher/AppOptionsDefaults.hpp>
#ifndef SCRAP_IOS
#include <launcher/AppLauncher.hpp>
#endif

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

namespace scrap {

    class RigidBodyComponent;

    class ScrapEngineApp {
      public:
        ScrapEngineApp();
        void initialize(platform::PlatformView& platformView, uint32_t width, uint32_t height);
        void tick(float deltaTime);
        void shutdown();

        ~ScrapEngineApp();

      private:
        platform::PlatformView* platformView = nullptr;

        double deltaUpdate = 0.0;
        float frameDelta = 1.0f / 60.0f;

        float lastX = 0.0f;
        float lastY = 0.0f;
        bool firstMouse = true;
        float debugLastPointerXPts = 0.f;
        float debugLastPointerYPts = 0.f;
        bool cursorCaptured = false;
        bool gravePressedLastFrame = false;
        bool demoTriggerPressedLastFrame = false;
        bool spacePressedLastFrame = false;
        bool cameraCollisionEnabled = false;
        bool dropDemoActive = false;
        bool defaultSceneSpinEnabled = false;
        bool defaultSceneSpinActive = false;
        std::string defaultSceneSpinEntityName;
        glm::vec3 defaultSceneSpinAngularVelocity = glm::vec3(1.35f, 1.9f, 0.65f);
        float cameraCollisionRadius = 0.35f;
        float cameraCollisionCooldown = 0.0f;

        Entity* draggedEntity = nullptr;
        glm::vec3 dragPlaneNormal = glm::vec3(0.0f);
        glm::vec3 dragPlanePoint = glm::vec3(0.0f);
        glm::vec3 dragOffset = glm::vec3(0.0f);
        glm::vec3 dragTargetPosition = glm::vec3(0.0f);
        glm::vec3 dragSmoothedVelocity = glm::vec3(0.0f);
        Entity* dragTraceEntity = nullptr;
        int dragTraceStep = 0;
        int dragTraceReleaseStepsRemaining = 0;

        std::unique_ptr<scrap::gpu::metal::LayerPresenter> pMetalPresenter;
        std::vector<scrap::gpu::metal::MetalDrawablePBR> metalDrawables_;
        scrap::gpu::metal::MetalIBLMapsPtr metalIBL_{nullptr,
                                                     scrap::gpu::metal::deleteMetalIBLMaps};
        std::unique_ptr<scrap::gpu::metal::MetalPBRRenderer> pMetalPBRRenderer;

        std::unique_ptr<scrap::Scene> pScene;
        std::unique_ptr<physics::XPBDSolver> pSolver;

        std::unique_ptr<scrap::ui::ImGuiComponentManager> pImGuiComponentManager;
        std::function<void(scrap::ui::ImGuiComponentManager&)> pCustomUIBuilder;

        void initMetal();
        void releaseMetalSceneMeshes();
        void syncMetalMeshesFromScene();
        void processInput(float deltaTime);

        void buildExampleUI();
        void refreshDebugStats(std::uint32_t framebufferWidth, std::uint32_t framebufferHeight);
#ifndef SCRAP_IOS
        bool finalizeLauncherLaunch(const scrap::launcher::LaunchRequest& request);
#endif
        bool loadConfiguredScene();
        bool resolveConfiguredRemoteAssets(std::string& errorMessage);
        void setCursorCapture(bool captured);

        void beginDrag(double mouseX, double mouseY);
        void updateDrag(double mouseX, double mouseY);
        void endDrag();
        void logDragTraceSnapshot(const char* timing, float physicsDt);

        void frameLoadedSceneCamera();
        void setupXPBDSolver();
        void setupDefaultSceneSpin();
        void updateDefaultSceneSpin(float deltaTime);
        void frameCameraToScene();
        bool isPhysicsDemoScene() const;
        Entity* findDefaultSceneSpinEntity();
        RigidBodyComponent* ensureEntityRigidBody(Entity& entity);
        void configureRigidBodyFromEntity(Entity& entity, RigidBodyComponent& rigidBody);
        void applyCameraCollisionPush(const glm::vec3& previousCameraPosition, float deltaTime);
        void startDropDemo();
        void updateDropDemoForces();
        void syncRigidBodiesToTransforms();
        std::vector<RigidBodyComponent*> collectRigidBodies();
        void applyClothImpulse();
        void advancePhysicsSimulation(float deltaTime);

        void syncMetalModelMatricesFromScene();
        void updateClothMeshesMetal();

      public:
        scrap::ui::ImGuiComponentManager& getImGuiManager() {
            return *pImGuiComponentManager;
        }
        void setCustomUIBuilder(std::function<void(scrap::ui::ImGuiComponentManager&)> builder);
        void setSceneFile(const std::string& path) {
            sceneFile = path;
        }
        void setCameraCollisionEnabled(bool enabled) {
            cameraCollisionEnabled = enabled;
        }
        void setIBLFile(const std::string& path) {
            iblFile = path;
        }
        void setPhysicsTickRate(double hz);
        void setModelRotationDegrees(const std::array<float, 3>& degrees,
                                     bool explicitOverride = true) {
            modelRotationDegrees = degrees;
            modelRotationExplicit = explicitOverride;
        }
        void setPolyHavenModelSelection(const std::string& id, const std::string& resolution) {
            polyHavenModelId = id;
            polyHavenModelResolution = resolution;
        }
        void setPolyHavenHdriSelection(const std::string& id, const std::string& resolution) {
            polyHavenHdriId = id;
            polyHavenHdriResolution = resolution;
        }
        void setLauncherEnabled(bool enabled) {
            launcherEnabled = enabled;
            launcherActive = enabled;
        }

      private:
        std::string sceneFile;
        std::string iblFile;
        std::array<float, 3> modelRotationDegrees{
            static_cast<float>(AppOptionsDefaults::DEFAULT_MODEL_ROTATE_X_DEGREES),
            static_cast<float>(AppOptionsDefaults::DEFAULT_MODEL_ROTATE_Y_DEGREES),
            static_cast<float>(AppOptionsDefaults::DEFAULT_MODEL_ROTATE_Z_DEGREES)};
        bool modelRotationExplicit = false;
        std::string polyHavenModelId;
        std::string polyHavenModelResolution = "2k";
        std::string polyHavenHdriId;
        std::string polyHavenHdriResolution = "4k";
        bool launcherEnabled = false;
        bool launcherActive = false;
        bool clearImGuiFocusAfterLauncherHandoff = false;
        int worldDragImGuiUnblockFrames = 0;
#ifndef SCRAP_IOS
        std::unique_ptr<scrap::launcher::AppLauncher> pLauncher;
#endif
        double physicsTickRate = 128.0;
        uint32_t width = 0;
        uint32_t height = 0;
        scrap::ui::AppDebugStats debugStats_{};
    };

} // namespace scrap
