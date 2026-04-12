#pragma once

#define VULKAN_HPP_NO_STRUCT_CONSTRUCTORS
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>
#include <vulkan/vulkan_raii.hpp>

#include <imgui.h>

#include <array>
#include <cstdlib>
#include <string>
#include <vector>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <app/BufferUtils.hpp>
#include <app/GraphicsPipeline.hpp>
#include <app/Scene.hpp>
#include <app/Instance.hpp>
#include <app/PhysicalDevice.hpp>
#include <app/LogicalDevice.hpp>
#include <app/Renderer.hpp>
#include <app/RenderSurface.hpp>
#include <app/SwapChain.hpp>
#include <app/ImGuiRenderer.hpp>
#include <app/platform/PlatformView.hpp>
#include <app/ui/ImGuiComponentManager.hpp>
#include <app/ui/components/HelloWorldWindow.hpp>
#include <app/ui/components/DebugStatsWindow.hpp>
#include <app/ui/components/BulletText.hpp>
#include <app/ui/components/Button.hpp>
#include <app/ui/components/Checkbox.hpp>
#include <app/ui/components/Image.hpp>
#include <app/ui/components/ImageButton.hpp>
#include <app/ui/components/LabelText.hpp>
#include <app/ui/components/RadioButton.hpp>
#include <app/ui/components/Text.hpp>
#include <app/ui/components/TextColored.hpp>
#include <app/ui/components/TextWrapped.hpp>
#include <launcher/AppLauncher.hpp>

#ifdef NDEBUG
constexpr bool enableValidationLayers = false;
#else
constexpr bool enableValidationLayers = true;
#endif

namespace sauce {

class RigidBodyComponent;

class SauceEngineApp {
public:
  SauceEngineApp(); // Constructor to initialize pImGuiComponentManager
  void initialize(platform::PlatformView& platformView, uint32_t width, uint32_t height);
  void tick(float deltaTime);
  void shutdown();

  ~SauceEngineApp();

private:
  platform::PlatformView* platformView = nullptr;

  double deltaUpdate = 0.0;
  float frameDelta = 1.0f / 60.0f;

  float lastX = 0.0f;
  float lastY = 0.0f;
  bool firstMouse = true;
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

  std::unique_ptr<sauce::Instance> pInstance;

  std::unique_ptr<sauce::RenderSurface> pRenderSurface;

  sauce::PhysicalDevice physicalDevice = nullptr;
  sauce::LogicalDevice logicalDevice = nullptr;

  std::unique_ptr<sauce::Renderer> pRenderer;

  std::unique_ptr<sauce::Scene> pScene;
  std::unique_ptr<physics::XPBDSolver> pSolver;

  std::unique_ptr<sauce::ImGuiRenderer> pImGuiRenderer;

  std::unique_ptr<sauce::ui::ImGuiComponentManager> pImGuiComponentManager;
  std::function<void(sauce::ui::ImGuiComponentManager&)> pCustomUIBuilder;

  void initVulkan();
  void processInput(float deltaTime);

  void buildExampleUI();
  bool finalizeLauncherLaunch(const sauce::launcher::LaunchRequest& request);
  bool loadConfiguredScene();
  bool resolveConfiguredRemoteAssets(std::string& errorMessage);
  void setCursorCapture(bool captured);

  void beginDrag(double mouseX, double mouseY);
  void updateDrag(double mouseX, double mouseY);
  void endDrag();
  void logDragTraceSnapshot(const char* timing, float physicsDt);

  void uploadMeshGPUResources();
  void frameLoadedSceneCamera();
  void setupSceneRenderer();
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
  void recordSceneCommandBuffer(vk::raii::CommandBuffer& cmd, uint32_t imageIndex);

public:
  sauce::ui::ImGuiComponentManager& getImGuiManager() { return *pImGuiComponentManager; }
  void setCustomUIBuilder(std::function<void(sauce::ui::ImGuiComponentManager&)> builder);
  void setSceneFile(const std::string& path) { sceneFile = path; }
  void setCameraCollisionEnabled(bool enabled) { cameraCollisionEnabled = enabled; }
  void setIBLFile(const std::string& path) { iblFile = path; }
  void setPhysicsTickRate(double hz);
  void setModelRotationDegrees(const std::array<float, 3>& degrees, bool explicitOverride = true) {
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
      static_cast<float>(AppOptions::DEFAULT_MODEL_ROTATE_X_DEGREES),
      static_cast<float>(AppOptions::DEFAULT_MODEL_ROTATE_Y_DEGREES),
      static_cast<float>(AppOptions::DEFAULT_MODEL_ROTATE_Z_DEGREES)};
  bool modelRotationExplicit = false;
  std::string polyHavenModelId;
  std::string polyHavenModelResolution = "2k";
  std::string polyHavenHdriId;
  std::string polyHavenHdriResolution = "4k";
  bool launcherEnabled = false;
  bool launcherActive = false;
  std::unique_ptr<sauce::launcher::AppLauncher> pLauncher;
  double physicsTickRate = 128.0;
  uint32_t width = 0;
  uint32_t height = 0;
};

} // namespace sauce
