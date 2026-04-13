#include <algorithm>
#include <app/Log.hpp>
#include <app/PhysicsDemoSetup.hpp>
#include <app/ScrapEngineApp.hpp>
#include <app/components/ClothComponent.hpp>
#include <app/components/LightComponent.hpp>
#include <app/components/MeshRendererComponent.hpp>
#include <app/components/RigidBodyComponent.hpp>
#include <app/components/TransformComponent.hpp>
#include <app/modeling/Material.hpp>
#include <app/ui/components/DebugStatsWindow.hpp>
#include <cctype>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <functional>
#include <limits>

#include <physics/XPBD.hpp>

#include <gpu/Backend.hpp>
#include <gpu/metal/MetalMeshUpload.hpp>

static_assert(scrap::gpu::kActiveBackend == scrap::gpu::BackendKind::Metal,
              "ScrapEngine uses Metal.");

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

namespace scrap {

    namespace {

        constexpr const char* kLauncherWindowTitle = "ScrapEngine Launcher";
        constexpr const char* kEngineWindowTitle = "ScrapEngine";

        bool isKeyPressed(const platform::InputState& input, platform::Key key) {
            return input.keys[platform::keyIndex(key)];
        }

        std::string lowercase(std::string value) {
            std::transform(value.begin(), value.end(), value.begin(),
                           [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
            return value;
        }

        float clampMagnitudeScale(const glm::vec3& value, float maxMagnitude) {
            const float lengthSq = glm::length2(value);
            if (lengthSq <= maxMagnitude * maxMagnitude || maxMagnitude <= 0.0f) {
                return 1.0f;
            }
            return maxMagnitude / std::sqrt(lengthSq);
        }

        struct SceneBounds {
            glm::vec3 min = glm::vec3(std::numeric_limits<float>::max());
            glm::vec3 max = glm::vec3(std::numeric_limits<float>::lowest());
            bool valid = false;
        };

        glm::quat integrateAngularVelocity(const glm::quat& orientation,
                                           const glm::vec3& angularVelocity, float deltaTime) {
            const glm::quat spin(0.0f, angularVelocity.x, angularVelocity.y, angularVelocity.z);
            return glm::normalize(orientation + 0.5f * spin * orientation * deltaTime);
        }

        float computeScaledMeshRadius(const modeling::Mesh& mesh, const glm::vec3& centerOfMass,
                                      const glm::vec3& scale) {
            float maxRadiusSq = 0.0f;
            for (const auto& vertex : mesh.getVertices()) {
                const glm::vec3 localOffset = (vertex.position - centerOfMass) * scale;
                maxRadiusSq = std::max(maxRadiusSq, glm::length2(localOffset));
            }

            return std::sqrt(maxRadiusSq);
        }

        void expandSceneBounds(SceneBounds& bounds, const modeling::Mesh& mesh,
                               const glm::mat4& modelMatrix) {
            for (const auto& vertex : mesh.getVertices()) {
                const glm::vec3 worldPosition =
                    glm::vec3(modelMatrix * glm::vec4(vertex.position, 1.0f));
                bounds.min = glm::min(bounds.min, worldPosition);
                bounds.max = glm::max(bounds.max, worldPosition);
                bounds.valid = true;
            }
        }

        struct Ray {
            glm::vec3 origin;
            glm::vec3 direction;
        };

        struct AABB {
            glm::vec3 min;
            glm::vec3 max;
        };

        std::string formatVec3(const glm::vec3& v) {
            return std::format("[{:.4f},{:.4f},{:.4f}]", v.x, v.y, v.z);
        }

        std::string buildAllRigidBodyPositions(Scene& scene) {
            std::string out = "[";
            bool first = true;
            for (auto& entity : scene.getEntitiesMut()) {
                auto* rigidBody = entity.getComponent<RigidBodyComponent>();
                if (!entity.getActive() || !rigidBody) {
                    continue;
                }

                if (!first) {
                    out += ", ";
                }
                first = false;
                out += std::format("{{name:\"{}\", pos:{}, com:{}}}", entity.get_name(),
                                   formatVec3(rigidBody->getPosition()),
                                   formatVec3(rigidBody->getWorldCenterOfMass()));
            }
            out += "]";
            return out;
        }

        struct DragTraceNeighbor {
            Entity* entity = nullptr;
            RigidBodyComponent* rigidBody = nullptr;
            float distance = 0.0f;
            float speed = 0.0f;
        };

        std::vector<DragTraceNeighbor> collectDragTraceNeighbors(Scene& scene, Entity* focusEntity,
                                                                 RigidBodyComponent* focusBody) {
            std::vector<DragTraceNeighbor> neighbors;
            if (!focusEntity || !focusBody) {
                return neighbors;
            }

            const glm::vec3 focusCenter = focusBody->getWorldCenterOfMass();
            for (auto& entity : scene.getEntitiesMut()) {
                if (&entity == focusEntity || !entity.getActive()) {
                    continue;
                }

                auto* rigidBody = entity.getComponent<RigidBodyComponent>();
                if (!rigidBody) {
                    continue;
                }

                const float distance =
                    glm::distance(focusCenter, rigidBody->getWorldCenterOfMass());
                const float speed = glm::length(rigidBody->getVelocity());
                if (distance > 1.2f && speed < 0.2f) {
                    continue;
                }

                neighbors.push_back(DragTraceNeighbor{
                    .entity = &entity,
                    .rigidBody = rigidBody,
                    .distance = distance,
                    .speed = speed,
                });
            }

            std::sort(neighbors.begin(), neighbors.end(),
                      [](const DragTraceNeighbor& a, const DragTraceNeighbor& b) {
                          if (a.distance != b.distance) {
                              return a.distance < b.distance;
                          }
                          return a.speed > b.speed;
                      });

            if (neighbors.size() > 6) {
                neighbors.resize(6);
            }

            return neighbors;
        }

        Ray screenToWorldRay(const Camera& camera, double mouseX, double mouseY,
                             float viewportWidth, float viewportHeight) {
            float ndcX = (2.0f * static_cast<float>(mouseX)) / viewportWidth - 1.0f;
            float ndcY = 1.0f - (2.0f * static_cast<float>(mouseY)) / viewportHeight;

            glm::mat4 proj = camera.getProjectionMatrix();
            glm::mat4 view = camera.getViewMatrix();
            glm::mat4 invProjView = glm::inverse(proj * view);

            glm::vec4 nearPoint = invProjView * glm::vec4(ndcX, ndcY, 0.0f, 1.0f);
            glm::vec4 farPoint = invProjView * glm::vec4(ndcX, ndcY, 1.0f, 1.0f);
            nearPoint /= nearPoint.w;
            farPoint /= farPoint.w;

            return {glm::vec3(nearPoint),
                    glm::normalize(glm::vec3(farPoint) - glm::vec3(nearPoint))};
        }

        Ray pickingRayFromViewPointer(const Camera& camera, double pointerX, double pointerY,
                                      float contentScale) {
            const float vw = camera.getViewportWidth();
            const float vh = camera.getViewportHeight();
            if (vw < 1.0f || vh < 1.0f) {
                return {glm::vec3(0.0f), glm::vec3(0.0f, 0.0f, 1.0f)};
            }
            const float mx = static_cast<float>(pointerX) * contentScale;
            const float my = static_cast<float>(pointerY) * contentScale;
            return screenToWorldRay(camera, static_cast<double>(mx), static_cast<double>(my), vw,
                                    vh);
        }

        bool rayIntersectsAABB(const Ray& ray, const AABB& box, float& tOut) {
            float tmin = 0.0f;
            float tmax = std::numeric_limits<float>::max();
            for (int i = 0; i < 3; ++i) {
                if (std::abs(ray.direction[i]) < 1e-8f) {
                    if (ray.origin[i] < box.min[i] || ray.origin[i] > box.max[i])
                        return false;
                } else {
                    float invD = 1.0f / ray.direction[i];
                    float t1 = (box.min[i] - ray.origin[i]) * invD;
                    float t2 = (box.max[i] - ray.origin[i]) * invD;
                    if (t1 > t2)
                        std::swap(t1, t2);
                    tmin = std::max(tmin, t1);
                    tmax = std::min(tmax, t2);
                    if (tmin > tmax)
                        return false;
                }
            }
            tOut = tmin;
            return true;
        }

        AABB computeWorldAABB(const modeling::Mesh& mesh, const glm::mat4& modelMatrix) {
            AABB result;
            result.min = glm::vec3(std::numeric_limits<float>::max());
            result.max = glm::vec3(std::numeric_limits<float>::lowest());
            for (const auto& v : mesh.getVertices()) {
                glm::vec3 wp = glm::vec3(modelMatrix * glm::vec4(v.position, 1.0f));
                result.min = glm::min(result.min, wp);
                result.max = glm::max(result.max, wp);
            }
            return result;
        }

        SceneBounds computeSceneBounds(const Scene& scene) {
            SceneBounds bounds;

            for (const auto& entity : scene.getEntities()) {
                if (!entity.getActive()) {
                    continue;
                }

                glm::mat4 modelMatrix(1.0f);
                if (const auto* transform = entity.getComponent<TransformComponent>()) {
                    modelMatrix = transform->getLocalMatrix();
                }

                for (const auto* meshRenderer : entity.getComponents<MeshRendererComponent>()) {
                    if (!meshRenderer) {
                        continue;
                    }

                    const auto mesh = meshRenderer->getMesh();
                    if (!mesh || mesh->getVertices().empty()) {
                        continue;
                    }

                    for (const auto& vertex : mesh->getVertices()) {
                        const glm::vec3 worldPos =
                            glm::vec3(modelMatrix * glm::vec4(vertex.position, 1.0f));
                        if (!bounds.valid) {
                            bounds.min = worldPos;
                            bounds.max = worldPos;
                            bounds.valid = true;
                            continue;
                        }

                        bounds.min = glm::min(bounds.min, worldPos);
                        bounds.max = glm::max(bounds.max, worldPos);
                    }
                }
            }

            return bounds;
        }

        std::array<float, 3> defaultModelRotationDegrees() {
            return {static_cast<float>(AppOptionsDefaults::DEFAULT_MODEL_ROTATE_X_DEGREES),
                    static_cast<float>(AppOptionsDefaults::DEFAULT_MODEL_ROTATE_Y_DEGREES),
                    static_cast<float>(AppOptionsDefaults::DEFAULT_MODEL_ROTATE_Z_DEGREES)};
        }

        bool pathLooksLikeAuthoredScene(const std::string& scenePath) {
            if (scenePath.empty()) {
                return false;
            }

            const std::string normalizedPath =
                lowercase(std::filesystem::path(scenePath).generic_string());
            return normalizedPath.find("/testscene/") != std::string::npos ||
                   normalizedPath.ends_with("/testscene.gltf") ||
                   normalizedPath.ends_with("/testscene.glb");
        }

        std::array<float, 3> resolvedModelRotationDegrees(
            const std::string& scenePath, bool explicitOverride,
            const std::array<float, 3>& configuredDegrees) {
            if (explicitOverride) {
                return configuredDegrees;
            }
            return pathLooksLikeAuthoredScene(scenePath) ? std::array<float, 3>{0.0f, 0.0f, 0.0f}
                                                         : configuredDegrees;
        }

        void applyGlobalModelRotation(Scene& scene, const std::array<float, 3>& rotationDegrees) {
            if (std::abs(rotationDegrees[0]) <= 0.001f && std::abs(rotationDegrees[1]) <= 0.001f &&
                std::abs(rotationDegrees[2]) <= 0.001f) {
                return;
            }

            const glm::quat rotateX =
                glm::angleAxis(glm::radians(rotationDegrees[0]), glm::vec3(1.0f, 0.0f, 0.0f));
            const glm::quat rotateY =
                glm::angleAxis(glm::radians(rotationDegrees[1]), glm::vec3(0.0f, 1.0f, 0.0f));
            const glm::quat rotateZ =
                glm::angleAxis(glm::radians(rotationDegrees[2]), glm::vec3(0.0f, 0.0f, 1.0f));
            const glm::quat rotation = glm::normalize(rotateZ * rotateY * rotateX);

            for (auto& entity : scene.getEntitiesMut()) {
                if (auto* transform = entity.getComponent<TransformComponent>()) {
                    transform->setTranslation(rotation * transform->getTranslation());
                    transform->setRotation(rotation * transform->getRotation());
                }

                if (auto* rigidBody = entity.getComponent<RigidBodyComponent>()) {
                    rigidBody->setPosition(rotation * rigidBody->getPosition());
                    rigidBody->setOrientation(rotation * rigidBody->getOrientation());
                    rigidBody->setVelocity(rotation * rigidBody->getVelocity());
                    rigidBody->setAngularVelocity(rotation * rigidBody->getAngularVelocity());
                }
            }
        }

    } // namespace

    ScrapEngineApp::ScrapEngineApp() {
        pImGuiComponentManager = std::make_unique<scrap::ui::ImGuiComponentManager>();
#if !defined(SCRAP_IOS)
        pLauncher = std::make_unique<scrap::launcher::AppLauncher>();
#endif
    }

    void ScrapEngineApp::setPhysicsTickRate(double hz) {
        constexpr double kMinHz = 10.0;
        constexpr double kMaxHz = 500.0;
        physicsTickRate = std::clamp(hz, kMinHz, kMaxHz);
    }

    ScrapEngineApp::~ScrapEngineApp() {
        shutdown();
    }

    void ScrapEngineApp::initialize(platform::PlatformView& platformView, uint32_t width,
                                    uint32_t height) {
        this->platformView = &platformView;
        this->width = width;
        this->height = height;
#if defined(SCRAP_IOS)
        launcherActive = false;
        launcherEnabled = false;
#endif
        setCursorCapture(false);
        initMetal();

#if !defined(SCRAP_IOS)
        if (launcherActive) {
            platformView.setWindowTitle(kLauncherWindowTitle);
            pLauncher->initialize(width, height, sceneFile, iblFile, polyHavenModelId,
                                  polyHavenModelResolution, polyHavenHdriId,
                                  polyHavenHdriResolution);
        } else
#endif
        {
            std::string remoteError;
            if (!resolveConfiguredRemoteAssets(remoteError)) {
                throw std::runtime_error(remoteError);
            }
            if (!loadConfiguredScene()) {
                throw std::runtime_error("Failed to load scene: " + sceneFile);
            }
        }

        if (pCustomUIBuilder) {
            pCustomUIBuilder(*pImGuiComponentManager);
        }

        scrap::ui::setAppDebugStatsSource(&debugStats_);

        platformView.activateWindowForInput();

        if (!launcherActive && ImGui::GetCurrentContext() != nullptr) {
            clearImGuiFocusAfterLauncherHandoff = true;
            worldDragImGuiUnblockFrames = 12;
        }
    }

    void ScrapEngineApp::setCustomUIBuilder(
        std::function<void(scrap::ui::ImGuiComponentManager&)> builder) {
        pCustomUIBuilder = std::move(builder);
    }

    void ScrapEngineApp::initMetal() {
        if (!platformView) {
            throw std::runtime_error("Platform view must be set before Metal initialization");
        }
        pMetalPresenter = std::make_unique<scrap::gpu::metal::LayerPresenter>();
        pMetalPresenter->init(platformView->getMetalLayerHandle(), width, height,
                              platformView->getContentScaleFactor());

        scrap::CameraCreateInfo cameraCreateInfo{
            .scrWidth = static_cast<float>(width),
            .scrHeight = static_cast<float>(height),
        };
        pScene = std::make_unique<scrap::Scene>(cameraCreateInfo);
        pSolver = std::make_unique<physics::XPBDSolver>();
        setupXPBDSolver();

        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        ImGuiIO& io = ImGui::GetIO();
        io.BackendPlatformName = "scrap_platform";

        pMetalPresenter->initDearImGui();

        pImGuiComponentManager->addComponent(std::make_unique<scrap::ui::DebugStatsWindow>());

        pMetalPBRRenderer = std::make_unique<scrap::gpu::metal::MetalPBRRenderer>();
        pMetalPBRRenderer->init(pMetalPresenter->mtlDevice(),
                                pMetalPresenter->drawablePixelFormat());
    }

    void ScrapEngineApp::processInput(float deltaTime) {
        if (!platformView) {
            return;
        }

        const platform::InputState input = platformView->consumeInputState();
        debugLastPointerXPts = input.pointer.x;
        debugLastPointerYPts = input.pointer.y;
        if (input.framebufferResized && pMetalPresenter) {
            const platform::FramebufferExtent extent = platformView->getFramebufferExtent();
            pMetalPresenter->setDrawableSizePixels(extent.width, extent.height);
            if (pScene && extent.width > 0 && extent.height > 0) {
                pScene->getCameraRW().setViewportSize(static_cast<float>(extent.width),
                                                      static_cast<float>(extent.height));
            }
        }

        if (input.toggleCursorCaptureRequested ||
            (isKeyPressed(input, platform::Key::GraveAccent) && !gravePressedLastFrame)) {
            setCursorCapture(!cursorCaptured);
        }
        gravePressedLastFrame = isKeyPressed(input, platform::Key::GraveAccent);

        if (launcherActive) {
            endDrag();
            return;
        }

        const bool worldDragBypassImGui = worldDragImGuiUnblockFrames > 0;
        if (worldDragImGuiUnblockFrames > 0) {
            worldDragImGuiUnblockFrames--;
        }

        if (cursorCaptured) {
            endDrag();
        }

        const bool demoTriggerPressed = isKeyPressed(input, platform::Key::F);
        if (demoTriggerPressed && !demoTriggerPressedLastFrame && pScene) {
            startDropDemo();
        }
        demoTriggerPressedLastFrame = demoTriggerPressed;

        const bool spacePressed = isKeyPressed(input, platform::Key::Space);
        if (spacePressed && !spacePressedLastFrame) {
            applyClothImpulse();
        }
        spacePressedLastFrame = spacePressed;

        if (!pScene) {
            return;
        }

        if (cameraCollisionCooldown > 0.0f) {
            cameraCollisionCooldown = std::max(0.0f, cameraCollisionCooldown - deltaTime);
        }

        auto& camera = pScene->getCameraRW();
        const glm::vec3 previousCameraPosition = camera.getPos();
        const bool cameraCollisionActive =
            cameraCollisionEnabled && cameraCollisionCooldown <= 0.0f;

        if (!cursorCaptured) {
            const auto& leftMouse =
                input.pointer.buttons[static_cast<std::size_t>(platform::MouseButton::Left)];
            if (leftMouse.pressed &&
                (worldDragBypassImGui || !ImGui::GetIO().WantCaptureMouse)) {
                beginDrag(input.pointer.x, input.pointer.y);
            }
            if (draggedEntity && (leftMouse.down || leftMouse.released ||
                                  input.pointer.deltaX != 0.0f || input.pointer.deltaY != 0.0f)) {
                updateDrag(input.pointer.x, input.pointer.y);
            }
            if (leftMouse.released) {
                endDrag();
            }

            if (cameraCollisionActive) {
                applyCameraCollisionPush(previousCameraPosition, deltaTime);
            }
            return;
        }

        const bool sprintHeld = isKeyPressed(input, platform::Key::LeftShift) ||
                                isKeyPressed(input, platform::Key::RightShift);
        const float baseMovementSpeed = camera.getMovementSpeed();
        camera.setMovementSpeed(sprintHeld ? baseMovementSpeed * 3.0f : baseMovementSpeed);

        if (input.pointer.deltaX != 0.0f || input.pointer.deltaY != 0.0f) {
            camera.processMouseMovement(input.pointer.deltaX, -input.pointer.deltaY);
        }

        if (isKeyPressed(input, platform::Key::W))
            camera.processDirection(Camera::Movement::FORWARD, deltaTime);
        if (isKeyPressed(input, platform::Key::S))
            camera.processDirection(Camera::Movement::BACKWARD, deltaTime);
        if (isKeyPressed(input, platform::Key::A))
            camera.processDirection(Camera::Movement::LEFT, deltaTime);
        if (isKeyPressed(input, platform::Key::D))
            camera.processDirection(Camera::Movement::RIGHT, deltaTime);

        camera.setMovementSpeed(baseMovementSpeed);

        if (cameraCollisionActive) {
            applyCameraCollisionPush(previousCameraPosition, deltaTime);
        }
    }

    void ScrapEngineApp::updateDefaultSceneSpin(float deltaTime) {
        if (!defaultSceneSpinEnabled || !pScene || deltaTime <= 0.0f || isPhysicsDemoScene()) {
            return;
        }

        if (!defaultSceneSpinActive || defaultSceneSpinEntityName.empty()) {
            Entity* candidate = findDefaultSceneSpinEntity();
            if (!candidate) {
                defaultSceneSpinActive = false;
                defaultSceneSpinEntityName.clear();
                return;
            }
            defaultSceneSpinActive = true;
            defaultSceneSpinEntityName = candidate->get_name();
        }

        for (auto& entity : pScene->getEntitiesMut()) {
            if (entity.get_name() != defaultSceneSpinEntityName) {
                continue;
            }

            auto* transform = entity.getComponent<TransformComponent>();
            if (!transform) {
                return;
            }

            const glm::quat currentOrientation = transform->getRotation();
            const glm::quat nextOrientation = integrateAngularVelocity(
                currentOrientation, defaultSceneSpinAngularVelocity, deltaTime);

            transform->setRotation(nextOrientation);
            return;
        }
    }

    void ScrapEngineApp::frameCameraToScene() {
        if (!pScene) {
            return;
        }

        SceneBounds bounds;
        for (const auto& entity : pScene->getEntities()) {
            if (!entity.getActive()) {
                continue;
            }

            const auto* meshRenderer = entity.getComponent<MeshRendererComponent>();
            const auto* transform = entity.getComponent<TransformComponent>();
            if (!meshRenderer || !meshRenderer->getMesh() || !transform) {
                continue;
            }

            expandSceneBounds(bounds, *meshRenderer->getMesh(), transform->getLocalMatrix());
        }

        if (!bounds.valid) {
            return;
        }

        const glm::vec3 center = 0.5f * (bounds.min + bounds.max);
        const glm::vec3 extents = bounds.max - bounds.min;
        float radius = 0.5f * glm::length(extents);
        if (radius < 0.5f) {
            radius = 0.5f;
        }

        auto& camera = pScene->getCameraRW();
        const float halfFovRadians = glm::radians(camera.getFOV() * 0.5f);
        const float distance = std::max(radius / std::tan(halfFovRadians), radius * 1.2f) * 1.35f;
        const glm::vec3 viewDirection = glm::normalize(glm::vec3(1.0f, 1.0f, 0.7f));
        camera.lookAt(center + viewDirection * distance, center, glm::vec3(0.0f, 0.0f, 1.0f));
    }

    bool ScrapEngineApp::isPhysicsDemoScene() const {
        return sceneFile.find("testScene") != std::string::npos ||
               sceneFile.find("jenga_tower") != std::string::npos ||
               sceneFile.find("monkeyball_course") != std::string::npos;
    }

    Entity* ScrapEngineApp::findDefaultSceneSpinEntity() {
        if (!pScene) {
            return nullptr;
        }

        Entity* candidate = nullptr;
        size_t meshEntityCount = 0;
        for (auto& entity : pScene->getEntitiesMut()) {
            if (!entity.getActive()) {
                continue;
            }

            auto* meshRenderer = entity.getComponent<MeshRendererComponent>();
            auto* transform = entity.getComponent<TransformComponent>();
            if (!meshRenderer || !meshRenderer->getMesh() || !transform) {
                continue;
            }

            ++meshEntityCount;
            if (entity.get_name() == "Cube") {
                return &entity;
            }

            if (!candidate) {
                candidate = &entity;
            }
        }

        return meshEntityCount == 1 ? candidate : nullptr;
    }

    RigidBodyComponent* ScrapEngineApp::ensureEntityRigidBody(Entity& entity) {
        return physics_demo::ensureEntityRigidBody(entity);
    }

    void ScrapEngineApp::configureRigidBodyFromEntity(Entity& entity,
                                                      RigidBodyComponent& rigidBody) {
        physics_demo::configureRigidBodyFromEntity(entity, rigidBody);
    }

    void ScrapEngineApp::applyCameraCollisionPush(const glm::vec3& previousCameraPosition,
                                                  float deltaTime) {
        if (!cameraCollisionEnabled || !pScene) {
            return;
        }

        auto& camera = pScene->getCameraRW();
        const glm::vec3 cameraPosition = camera.getPos();
        const glm::vec3 cameraDisplacement = cameraPosition - previousCameraPosition;
        const float displacementLengthSq = glm::length2(cameraDisplacement);
        const float displacementLength =
            displacementLengthSq > 1e-10f ? std::sqrt(displacementLengthSq) : 0.0f;
        const glm::vec3 cameraMoveDirection =
            displacementLength > 1e-5f ? cameraDisplacement / displacementLength : glm::vec3(0.0f);
        const float cameraSpeed = deltaTime > 1e-6f ? displacementLength / deltaTime : 0.0f;

        for (auto& entity : pScene->getEntitiesMut()) {
            if (!entity.getActive()) {
                continue;
            }

            auto* meshRenderer = entity.getComponent<MeshRendererComponent>();
            auto* transform = entity.getComponent<TransformComponent>();
            if (!meshRenderer || !meshRenderer->getMesh() || !transform) {
                continue;
            }

            auto* rigidBody = entity.getComponent<RigidBodyComponent>();
            if (!rigidBody && !dropDemoActive) {
                continue;
            }

            const glm::vec3 centerOfMass =
                rigidBody ? rigidBody->getCenterOfMass()
                          : RigidBodyComponent::meshCenterOfMass(meshRenderer->getMesh());
            const glm::vec3 bodyScale = rigidBody ? rigidBody->getScale() : transform->getScale();
            const float bodyRadius =
                computeScaledMeshRadius(*meshRenderer->getMesh(), centerOfMass, bodyScale);
            if (bodyRadius <= 1e-5f) {
                continue;
            }

            const glm::vec3 bodyCenter =
                rigidBody ? rigidBody->getWorldCenterOfMass()
                          : transform->getTranslation() +
                                transform->getRotation() * (centerOfMass * bodyScale);
            const glm::vec3 delta = bodyCenter - cameraPosition;
            const float distanceSq = glm::length2(delta);
            const float combinedRadius = cameraCollisionRadius + bodyRadius;
            if (distanceSq >= combinedRadius * combinedRadius) {
                continue;
            }

            if (!rigidBody) {
                rigidBody = ensureEntityRigidBody(entity);
            }

            if (!rigidBody || !rigidBody->isCollisionEnabled()) {
                continue;
            }

            const float distance = distanceSq > 1e-10f ? std::sqrt(distanceSq) : 0.0f;
            glm::vec3 contactNormal =
                distance > 1e-5f ? (delta / distance) : glm::vec3(1.0f, 0.0f, 0.0f);
            if (distance <= 1e-5f && displacementLength > 1e-5f) {
                contactNormal = cameraMoveDirection;
            }

            const float penetration = combinedRadius - distance;
            if (rigidBody->isSleeping()) {
                rigidBody->wake();
            }
            if (!rigidBody->isDynamic()) {
                continue;
            }

            constexpr float kPenetrationCorrectionFraction = 0.2f;
            constexpr float kMaxPenetrationCorrection = 0.03f;
            constexpr float kAlignedPushScale = 0.35f;
            constexpr float kMaxAlignedPushSpeed = 0.5f;
            constexpr float kNormalPushScale = 1.2f;
            constexpr float kMaxNormalPushSpeed = 0.2f;
            constexpr float kAngularPushScale = 0.02f;

            const float correctionDistance =
                std::min(penetration * kPenetrationCorrectionFraction, kMaxPenetrationCorrection);
            rigidBody->setWorldCenterOfMass(bodyCenter + contactNormal * correctionDistance);

            glm::vec3 velocity = rigidBody->getVelocity();
            const float inwardSpeed = glm::dot(velocity, -contactNormal);
            if (inwardSpeed > 0.0f) {
                velocity += inwardSpeed * contactNormal;
            }

            if (cameraSpeed > 0.0f) {
                const float alignedComponent =
                    std::max(glm::dot(cameraMoveDirection, contactNormal), 0.0f);
                const float alignedPushSpeed = std::min(
                    alignedComponent * cameraSpeed * kAlignedPushScale, kMaxAlignedPushSpeed);
                velocity += cameraMoveDirection * alignedPushSpeed;
            }

            velocity += contactNormal *
                        std::min(correctionDistance * kNormalPushScale, kMaxNormalPushSpeed);
            rigidBody->setVelocity(velocity);

            if (cameraSpeed > 0.0f) {
                rigidBody->setAngularVelocity(rigidBody->getAngularVelocity() +
                                              glm::cross(cameraMoveDirection, contactNormal) *
                                                  (cameraSpeed * kAngularPushScale));
            }
        }
    }

    // F key: re-run armScene (rigid bodies, collision on, zero velocities). Physics demos also
    // auto-arm on load when the scene path matches isPhysicsDemoScene().
    void ScrapEngineApp::startDropDemo() {
        if (!pScene) {
            return;
        }

        dropDemoActive = false;
        physics_demo::armScene(*pScene);
        dropDemoActive = true;
        cameraCollisionCooldown = 0.2f;
    }

    void ScrapEngineApp::updateDropDemoForces() {
        if (!dropDemoActive || !pScene) {
            return;
        }

        physics_demo::applyForces(*pScene);
    }

    void ScrapEngineApp::applyClothImpulse() {
        if (!pScene) {
            return;
        }

        const Camera& camera = pScene->getCameraRO();
        const glm::vec3 impulseDirection = glm::normalize(camera.getFront());
        constexpr float kImpulseStrength = 2.0f;

        for (auto& entity : pScene->getEntitiesMut()) {
            if (!entity.getActive()) {
                continue;
            }

            for (auto* clothComp : entity.getComponents<ClothComponent>()) {
                physics::ClothData* cloth = clothComp->getClothData();
                if (!cloth || cloth->particles.empty()) {
                    continue;
                }

                glm::vec3 center(0.0f);
                glm::vec3 minPos = cloth->particles.front().position;
                glm::vec3 maxPos = cloth->particles.front().position;
                size_t dynamicCount = 0;

                for (const auto& particle : cloth->particles) {
                    center += particle.position;
                    minPos = glm::min(minPos, particle.position);
                    maxPos = glm::max(maxPos, particle.position);
                    if (!particle.isStatic()) {
                        ++dynamicCount;
                    }
                }

                if (dynamicCount == 0) {
                    continue;
                }

                center /= static_cast<float>(cloth->particles.size());

                const float clothRadius = std::max(0.25f, 0.35f * glm::length(maxPos - minPos));

                for (auto& particle : cloth->particles) {
                    if (particle.isStatic()) {
                        continue;
                    }

                    const float distance = glm::length(particle.position - center);
                    if (distance > clothRadius) {
                        continue;
                    }

                    const float falloff = 1.0f - (distance / clothRadius);
                    const glm::vec3 deltaVelocity = impulseDirection * (kImpulseStrength * falloff);

                    particle.velocity += deltaVelocity;
                    particle.predictedPosition += deltaVelocity * (1.0f / 128.0f);
                }
            }
        }
    }

    void ScrapEngineApp::advancePhysicsSimulation(float deltaTime) {
        if (!pScene || !pSolver) {
            return;
        }

        auto rigidBodies = collectRigidBodies();
        const size_t dynamicRigidBodyCount = static_cast<size_t>(std::count_if(
            rigidBodies.begin(), rigidBodies.end(), [](const RigidBodyComponent* rigidBody) {
                return rigidBody && rigidBody->canBeDynamic() && rigidBody->isCollisionEnabled();
            }));
        const auto solverTuning = physics_demo::selectRigidSolverTuning(dynamicRigidBodyCount);
        pSolver->solverIterations = solverTuning.solverIterations;
        pSolver->rigidSubsteps = solverTuning.rigidSubsteps;
        const float physicsDt = 1.0f / static_cast<float>(physicsTickRate);

        constexpr int kMaxPhysicsStepsPerFrame = 8;
        constexpr float kMaxFrameDtFactor = 3.0f;
        const float clampedFrame = std::min(deltaTime, physicsDt * kMaxFrameDtFactor);
        deltaUpdate += clampedFrame;
        const float maxCarry = physicsDt * static_cast<float>(kMaxPhysicsStepsPerFrame);
        if (deltaUpdate > maxCarry) {
            deltaUpdate = maxCarry;
        }

        if (deltaUpdate > 1.0f) {
            deltaUpdate = physicsDt;
        }

        while (deltaUpdate >= physicsDt) {
            updateDropDemoForces();

            if (draggedEntity) {
                auto* dragRB = draggedEntity->getComponent<RigidBodyComponent>();
                if (dragRB) {
                    pSolver->dragBody = dragRB;
                    if (dragRB->isSleeping()) {
                        dragRB->wake();
                    }

                    const glm::vec3 toTarget = dragTargetPosition - dragRB->getPosition();
                    const glm::vec3 gravDir = pSolver->gravityDirection;
                    const float intentSpeed = glm::length(dragSmoothedVelocity);
                    const float wreckingBlend = glm::smoothstep(1.0f, 4.5f, intentSpeed);
                    const float targetLag = glm::length(toTarget);
                    const float lagBlend = glm::smoothstep(0.08f, 0.45f, targetLag);
                    const glm::vec3 currentVelocity = dragRB->getVelocity();
                    const glm::vec3 desiredVelocity =
                        dragSmoothedVelocity * 0.85f + toTarget * 7.5f;
                    const float verticalSpeed = glm::dot(desiredVelocity, gravDir);
                    const glm::vec3 desiredVerticalVelocity = verticalSpeed * gravDir;
                    const glm::vec3 desiredLateralVelocity =
                        desiredVelocity - desiredVerticalVelocity;

                    constexpr float kGentleLateralSpeed = 2.8f;
                    constexpr float kGentleCatchupLateralSpeed = 5.0f;
                    constexpr float kGentleDownwardSpeed = 1.4f;
                    constexpr float kGentleCatchupDownwardSpeed = 2.4f;
                    constexpr float kGentleUpwardSpeed = 1.8f;
                    constexpr float kGentleCatchupUpwardSpeed = 2.8f;
                    constexpr float kWreckingLateralSpeed = 9.0f;
                    constexpr float kWreckingDownwardSpeed = 6.0f;
                    constexpr float kWreckingUpwardSpeed = 7.0f;
                    const auto blendDragLimit = [&](float gentle, float catchup, float wrecking) {
                        return std::lerp(std::lerp(gentle, catchup, lagBlend), wrecking,
                                         wreckingBlend);
                    };

                    const float lateralSpeedLimit = blendDragLimit(
                        kGentleLateralSpeed, kGentleCatchupLateralSpeed, kWreckingLateralSpeed);
                    const float downwardSpeedLimit = blendDragLimit(
                        kGentleDownwardSpeed, kGentleCatchupDownwardSpeed, kWreckingDownwardSpeed);
                    const float upwardSpeedLimit = blendDragLimit(
                        kGentleUpwardSpeed, kGentleCatchupUpwardSpeed, kWreckingUpwardSpeed);

                    glm::vec3 clampedDesiredLateralVelocity = desiredLateralVelocity;
                    clampedDesiredLateralVelocity *=
                        clampMagnitudeScale(desiredLateralVelocity, lateralSpeedLimit);

                    glm::vec3 clampedDesiredVerticalVelocity = desiredVerticalVelocity;
                    if (verticalSpeed > 0.0f) {
                        clampedDesiredVerticalVelocity *=
                            clampMagnitudeScale(desiredVerticalVelocity, downwardSpeedLimit);
                    } else {
                        clampedDesiredVerticalVelocity *=
                            clampMagnitudeScale(desiredVerticalVelocity, upwardSpeedLimit);
                    }

                    const glm::vec3 clampedDesiredVelocity =
                        clampedDesiredLateralVelocity + clampedDesiredVerticalVelocity;
                    const glm::vec3 desiredAcceleration =
                        (clampedDesiredVelocity - currentVelocity) / std::max(physicsDt, 1e-4f);
                    const float verticalAcceleration = glm::dot(desiredAcceleration, gravDir);
                    const glm::vec3 desiredVerticalAcceleration = verticalAcceleration * gravDir;
                    const glm::vec3 desiredLateralAcceleration =
                        desiredAcceleration - desiredVerticalAcceleration;

                    constexpr float kGentleLateralAcceleration = 22.0f;
                    constexpr float kGentleCatchupLateralAcceleration = 44.0f;
                    constexpr float kGentleDownwardAcceleration = 14.0f;
                    constexpr float kGentleCatchupDownwardAcceleration = 24.0f;
                    constexpr float kGentleUpwardAcceleration = 16.0f;
                    constexpr float kGentleCatchupUpwardAcceleration = 28.0f;
                    constexpr float kWreckingLateralAcceleration = 70.0f;
                    constexpr float kWreckingDownwardAcceleration = 52.0f;
                    constexpr float kWreckingUpwardAcceleration = 58.0f;

                    const float lateralAccelerationLimit = blendDragLimit(
                        kGentleLateralAcceleration, kGentleCatchupLateralAcceleration,
                        kWreckingLateralAcceleration);
                    const float downwardAccelerationLimit = blendDragLimit(
                        kGentleDownwardAcceleration, kGentleCatchupDownwardAcceleration,
                        kWreckingDownwardAcceleration);
                    const float upwardAccelerationLimit =
                        blendDragLimit(kGentleUpwardAcceleration, kGentleCatchupUpwardAcceleration,
                                       kWreckingUpwardAcceleration);

                    glm::vec3 clampedLateralAcceleration = desiredLateralAcceleration;
                    clampedLateralAcceleration *=
                        clampMagnitudeScale(desiredLateralAcceleration, lateralAccelerationLimit);

                    glm::vec3 clampedVerticalAcceleration = desiredVerticalAcceleration;
                    if (verticalAcceleration > 0.0f) {
                        clampedVerticalAcceleration *= clampMagnitudeScale(
                            desiredVerticalAcceleration, downwardAccelerationLimit);
                    } else {
                        clampedVerticalAcceleration *= clampMagnitudeScale(
                            desiredVerticalAcceleration, upwardAccelerationLimit);
                    }

                    const glm::vec3 driveVelocity =
                        currentVelocity +
                        (clampedLateralAcceleration + clampedVerticalAcceleration) * physicsDt;
                    dragRB->setVelocity(driveVelocity);
                    dragRB->setAngularVelocity(dragRB->getAngularVelocity() * 0.15f);
                }
            }

            if (!draggedEntity || !draggedEntity->getComponent<RigidBodyComponent>()) {
                pSolver->dragBody = nullptr;
            }

            const bool traceDragStep = dragTraceEntity && (draggedEntity == dragTraceEntity ||
                                                           dragTraceReleaseStepsRemaining > 0);
            if (traceDragStep) {
                logDragTraceSnapshot("pre", physicsDt);
            }

            pSolver->solvePositions(rigidBodies, physicsDt);
            if (traceDragStep) {
                logDragTraceSnapshot("post", physicsDt);
                ++dragTraceStep;
                if (draggedEntity != dragTraceEntity && dragTraceReleaseStepsRemaining > 0) {
                    --dragTraceReleaseStepsRemaining;
                    if (dragTraceReleaseStepsRemaining == 0) {
                        Log::verbose("drag-trace", "phase=complete entity={}",
                                     dragTraceEntity->get_name());
                        dragTraceEntity = nullptr;
                    }
                }
            }

            syncRigidBodiesToTransforms();
            for (auto& entity : pScene->getEntitiesMut()) {
                if (!entity.getActive()) {
                    continue;
                }

                for (auto* clothComp : entity.getComponents<ClothComponent>()) {
                    clothComp->syncSimulationTransform();

                    physics::ClothData* cloth = clothComp->getClothData();
                    if (cloth && !cloth->empty()) {
                        pSolver->solveCloth(*cloth, clothComp->getSettings(), physicsDt);
                        clothComp->markRuntimeMeshDirty();
                    }
                }
            }

            deltaUpdate -= physicsDt;
        }
    }

    void ScrapEngineApp::tick(float deltaTime) {
        if (!pMetalPresenter || !platformView || !pScene || !pSolver) {
            return;
        }
        frameDelta = std::max(deltaTime, 1e-4f);
        const platform::FramebufferExtent extent = platformView->getFramebufferExtent();
        if (extent.width == 0 || extent.height == 0) {
            return;
        }
        processInput(deltaTime);
        if (!launcherActive) {
            updateDefaultSceneSpin(deltaTime);
            syncRigidBodiesToTransforms();
            advancePhysicsSimulation(deltaTime);
            updateClothMeshesMetal();
            syncMetalModelMatricesFromScene();
        }
#if !defined(SCRAP_IOS)
        else {
            pLauncher->pollBackgroundTasks([this](const scrap::launcher::LaunchRequest& request) {
                return finalizeLauncherLaunch(request);
            });
        }
#endif
        refreshDebugStats(extent.width, extent.height);
        pMetalPresenter->renderDearImGuiFrame(
            *platformView, deltaTime, [this]() { buildExampleUI(); },
            [this](void* enc, std::uint32_t drawableW, std::uint32_t drawableH) {
                if (launcherActive || !pMetalPBRRenderer || !pMetalPBRRenderer->ready() ||
                    !pScene) {
                    return;
                }
                if (metalDrawables_.empty()) {
                    return;
                }
                const float vw =
                    drawableW > 0 ? static_cast<float>(drawableW) : static_cast<float>(width);
                const float vh =
                    drawableH > 0 ? static_cast<float>(drawableH) : static_cast<float>(height);
                if (vw <= 0.0f || vh <= 0.0f) {
                    return;
                }
                pScene->getCameraRW().setViewportSize(vw, vh);
                const scrap::Camera& cam = pScene->getCameraRO();
                const glm::mat4 view = cam.getViewMatrix();
                const glm::mat4 proj = cam.getProjectionMatrix();
                const glm::vec3 cameraPos = cam.getPos();

                auto gpuLights = pScene->collectGPULights();
                if (gpuLights.empty()) {
                    GPULight keyLight{};
                    keyLight.type = static_cast<uint32_t>(LightType::Directional);
                    keyLight.direction = glm::normalize(glm::vec3(1.0f, -1.0f, -1.0f));
                    keyLight.color = glm::vec3(1.0f);
                    keyLight.intensity = 2.5f;
                    gpuLights.push_back(keyLight);

                    GPULight fillLight{};
                    fillLight.type = static_cast<uint32_t>(LightType::Directional);
                    fillLight.direction = -keyLight.direction;
                    fillLight.color = glm::vec3(0.55f, 0.6f, 0.7f);
                    fillLight.intensity = 1.25f;
                    gpuLights.push_back(fillLight);
                }
                constexpr std::uint32_t kMaxLights = 64;
                const std::uint32_t lightCount = static_cast<std::uint32_t>(
                    std::min(gpuLights.size(), static_cast<std::size_t>(kMaxLights)));

                pMetalPBRRenderer->draw(enc, vw, vh, view, proj, cameraPos, gpuLights.data(),
                                        lightCount, metalDrawables_.data(), metalDrawables_.size());
            });
    }

    void ScrapEngineApp::shutdown() {
        scrap::ui::setAppDebugStatsSource(nullptr);
        releaseMetalSceneMeshes();
        if (pMetalPBRRenderer) {
            pMetalPBRRenderer->shutdown();
        }
        pMetalPBRRenderer.reset();
        metalIBL_.reset();
        if (pMetalPresenter) {
            pMetalPresenter->shutdownDearImGui();
        }
        if (ImGui::GetCurrentContext() != nullptr) {
            ImGui::DestroyContext();
        }
        pMetalPresenter.reset();
        modeling::Material::cleanup();
        pSolver.reset();
        pScene.reset();
        platformView = nullptr;
    }

    void ScrapEngineApp::refreshDebugStats(std::uint32_t framebufferWidth,
                                           std::uint32_t framebufferHeight) {
        if (!pScene) {
            return;
        }

        debugStats_.frameDeltaSec = frameDelta;
        debugStats_.physicsAccumulatorSec = static_cast<float>(deltaUpdate);
        debugStats_.physicsTickHz = physicsTickRate;
        debugStats_.drawableWidthPx = framebufferWidth;
        debugStats_.drawableHeightPx = framebufferHeight;
        debugStats_.contentScale = platformView ? platformView->getContentScaleFactor() : 1.f;
        debugStats_.launcherActive = launcherActive;

        debugStats_.entityCount = static_cast<std::uint32_t>(pScene->getEntities().size());
        std::uint32_t active = 0;
        std::uint32_t prim = 0;
        for (const auto& entity : pScene->getEntities()) {
            if (entity.getActive()) {
                ++active;
            }
            for (auto* mrc : entity.getComponents<MeshRendererComponent>()) {
                if (mrc && mrc->getMesh() && !mrc->getMesh()->getVertices().empty()) {
                    ++prim;
                }
            }
        }
        debugStats_.activeEntityCount = active;
        debugStats_.meshPrimitiveCount = prim;
        debugStats_.gpuLightCount = static_cast<std::uint32_t>(pScene->collectGPULights().size());

        const std::string path = pScene->hasFilePath() ? pScene->getCurrentFilePath() : sceneFile;
        if (path.empty()) {
            debugStats_.sceneLabel[0] = '\0';
        } else {
            const std::string fn = std::filesystem::path(path).filename().string();
            std::strncpy(debugStats_.sceneLabel, fn.c_str(), sizeof(debugStats_.sceneLabel) - 1);
            debugStats_.sceneLabel[sizeof(debugStats_.sceneLabel) - 1] = '\0';
        }

        debugStats_.uncappedPresentation = true;
        debugStats_.metalDrawableCount = static_cast<std::uint32_t>(metalDrawables_.size());
        debugStats_.metalPbrReady = pMetalPBRRenderer && pMetalPBRRenderer->ready();
        debugStats_.metalIblLoaded =
            static_cast<bool>(metalIBL_ && metalIBL_->prefilterArray != nullptr);

        debugStats_.cursorCaptured = cursorCaptured;
        debugStats_.appWindowKey = platformView ? platformView->isWindowKey() : true;
        debugStats_.pendingImGuiFocusClear = clearImGuiFocusAfterLauncherHandoff;
        debugStats_.worldDragImGuiUnblockFramesRemaining = worldDragImGuiUnblockFrames;
        debugStats_.draggingRigidBody = draggedEntity != nullptr;
        debugStats_.pointerXPts = debugLastPointerXPts;
        debugStats_.pointerYPts = debugLastPointerYPts;
    }

    void ScrapEngineApp::buildExampleUI() {
        if (ImGui::GetCurrentContext() != nullptr && clearImGuiFocusAfterLauncherHandoff) {
            ImGui::SetWindowFocus(nullptr);
            clearImGuiFocusAfterLauncherHandoff = false;
        }

#if !defined(SCRAP_IOS)
        if (launcherActive) {
            pLauncher->render(
                [this](const scrap::launcher::LaunchRequest& request) {
                    return finalizeLauncherLaunch(request);
                },
                [this]() {
                    if (platformView) {
                        platformView->requestClose();
                    }
                });
            return;
        }
#endif

        pImGuiComponentManager->renderAll();
    }

#if !defined(SCRAP_IOS)
    bool ScrapEngineApp::finalizeLauncherLaunch(const scrap::launcher::LaunchRequest& request) {
        const bool resolutionChanged = request.width != width || request.height != height;

        width = request.width;
        height = request.height;
        modelRotationDegrees = request.modelRotationDegrees;
        modelRotationExplicit = true;
        sceneFile = request.scenePath;
        iblFile = request.iblPath;

        if (!loadConfiguredScene()) {
            return false;
        }

        if (platformView) {
            if (resolutionChanged) {
                platformView->setWindowSize(width, height);

                const platform::FramebufferExtent extent = platformView->getFramebufferExtent();
                if (pScene && extent.width > 0 && extent.height > 0) {
                    pScene->getCameraRW().setViewportSize(static_cast<float>(extent.width),
                                                          static_cast<float>(extent.height));
                }
            }

            platformView->setWindowTitle(kEngineWindowTitle);
            platformView->activateWindowForInput();
        }

        launcherActive = false;
        launcherEnabled = false;
        // Leave pointer free so block picking/drag works; use ` for FPS pointer lock.
        setCursorCapture(false);
        clearImGuiFocusAfterLauncherHandoff = true;
        worldDragImGuiUnblockFrames = 12;
        return true;
    }
#endif

    bool ScrapEngineApp::resolveConfiguredRemoteAssets(std::string& errorMessage) {
#if defined(SCRAP_IOS)
        if (!polyHavenModelId.empty() || !polyHavenHdriId.empty()) {
            errorMessage = "Poly Haven asset download is not available in iOS builds; use bundled "
                           "scene/IBL paths.";
            return false;
        }
        return true;
#else
        if (!polyHavenModelId.empty()) {
            const auto modelDownload = scrap::launcher::downloadPolyHavenModelGltf(
                polyHavenModelId, polyHavenModelResolution);
            if (!modelDownload.errorMessage.empty()) {
                errorMessage = "Poly Haven model download failed: " + modelDownload.errorMessage;
                return false;
            }
            sceneFile = modelDownload.localPath.string();
            if (!modelRotationExplicit) {
                modelRotationDegrees = defaultModelRotationDegrees();
            }
        }

        if (!polyHavenHdriId.empty()) {
            const auto hdriDownload =
                scrap::launcher::downloadPolyHavenHdri(polyHavenHdriId, polyHavenHdriResolution);
            if (!hdriDownload.errorMessage.empty()) {
                errorMessage = "Poly Haven HDRI download failed: " + hdriDownload.errorMessage;
                return false;
            }
            iblFile = hdriDownload.localPath.string();
        }

        return true;
#endif
    }

    bool ScrapEngineApp::loadConfiguredScene() {
        if (!pScene) {
            return false;
        }

        pScene->getEntitiesMut().clear();
        pScene->setCurrentFilePath("");
        dropDemoActive = false;
        draggedEntity = nullptr;
        dragTraceEntity = nullptr;
        dragTraceReleaseStepsRemaining = 0;
        defaultSceneSpinActive = false;
        defaultSceneSpinEntityName.clear();

        const platform::FramebufferExtent extent =
            platformView ? platformView->getFramebufferExtent() : platform::FramebufferExtent{};
        const float viewportWidth =
            extent.width > 0 ? static_cast<float>(extent.width) : static_cast<float>(width);
        const float viewportHeight =
            extent.height > 0 ? static_cast<float>(extent.height) : static_cast<float>(height);
        pScene->getCameraRW().setViewportSize(viewportWidth, viewportHeight);

        defaultSceneSpinEnabled = sceneFile.empty();
        if (sceneFile.empty()) {
            sceneFile = "assets/models/Cube.gltf";
        }

        if (!pScene->loadFromFile(sceneFile)) {
            return false;
        }

        if (!pScene->getEntities().empty()) {
            if (!defaultSceneSpinEnabled) {
                applyGlobalModelRotation(
                    *pScene, resolvedModelRotationDegrees(sceneFile, modelRotationExplicit,
                                                          modelRotationDegrees));
                frameLoadedSceneCamera();
            } else {
                frameCameraToScene();
                setupDefaultSceneSpin();
            }
        }

        if (!pScene->getEntities().empty() && isPhysicsDemoScene()) {
            physics_demo::armScene(*pScene);
            dropDemoActive = true;
        }

        syncMetalMeshesFromScene();
        if (pMetalPBRRenderer && pMetalPresenter && pMetalPresenter->mtlDevice()) {
            metalIBL_.reset();
            pMetalPBRRenderer->setIBL(nullptr);
            if (!iblFile.empty() && std::filesystem::exists(iblFile)) {
                metalIBL_ =
                    scrap::gpu::metal::generateMetalIBLMaps(pMetalPresenter->mtlDevice(), iblFile);
                pMetalPBRRenderer->setIBL(metalIBL_.get());
            }
        }

        return true;
    }

    namespace {

        scrap::gpu::metal::MetalDrawablePBR* metalDrawableForEntity(
            Scene& scene, std::vector<scrap::gpu::metal::MetalDrawablePBR>& drawables,
            MeshRendererComponent& target) {
            std::size_t idx = 0;
            for (auto& entity : scene.getEntitiesMut()) {
                if (!entity.getActive()) {
                    continue;
                }
                for (auto* meshRenderer : entity.getComponents<MeshRendererComponent>()) {
                    if (!meshRenderer || !meshRenderer->getMesh()) {
                        continue;
                    }
                    if (meshRenderer == &target) {
                        return idx < drawables.size() ? &drawables[idx] : nullptr;
                    }
                    ++idx;
                }
            }
            return nullptr;
        }

    } // namespace

    void ScrapEngineApp::releaseMetalSceneMeshes() {
        for (auto& d : metalDrawables_) {
            scrap::gpu::metal::releaseMetalMeshGpu(d.mesh);
        }
        metalDrawables_.clear();
    }

    void ScrapEngineApp::syncMetalMeshesFromScene() {
        releaseMetalSceneMeshes();
        if (!pMetalPresenter || !pScene) {
            return;
        }
        void* dev = pMetalPresenter->mtlDevice();
        if (dev == nullptr) {
            return;
        }
        for (auto& entity : pScene->getEntitiesMut()) {
            if (!entity.getActive()) {
                continue;
            }
            auto* transform = entity.getComponent<TransformComponent>();
            glm::mat4 model(1.0f);
            if (transform) {
                model = transform->getLocalMatrix();
            }
            for (auto* meshRenderer : entity.getComponents<MeshRendererComponent>()) {
                if (!meshRenderer || !meshRenderer->getMesh()) {
                    continue;
                }
                metalDrawables_.push_back({});
                scrap::gpu::metal::MetalDrawablePBR& drawable = metalDrawables_.back();
                if (!scrap::gpu::metal::uploadMeshToMetal(dev, *meshRenderer->getMesh(),
                                                          drawable.mesh)) {
                    metalDrawables_.pop_back();
                } else {
                    drawable.model = model;
                    if (auto mat = meshRenderer->getMaterial()) {
                        drawable.material = mat.get();
                        drawable.doubleSided = mat->getProperties().doubleSided;
                    }
                    // Jenga floor is a single quad (4 verts, 6 indices); viewed obliquely the back face is culled.
                    if (auto mesh = meshRenderer->getMesh();
                        mesh && mesh->getVertexCount() == 4u && mesh->getIndexCount() == 6u) {
                        drawable.doubleSided = true;
                    }
                }
            }
        }
    }

    void ScrapEngineApp::syncMetalModelMatricesFromScene() {
        if (!pScene || metalDrawables_.empty()) {
            return;
        }
        size_t drawCount = 0;
        for (auto& entity : pScene->getEntitiesMut()) {
            if (!entity.getActive()) {
                continue;
            }
            for (auto* meshRenderer : entity.getComponents<MeshRendererComponent>()) {
                if (!meshRenderer || !meshRenderer->getMesh()) {
                    continue;
                }
                ++drawCount;
            }
        }
        if (drawCount != metalDrawables_.size()) {
            syncMetalMeshesFromScene();
            return;
        }
        size_t idx = 0;
        for (auto& entity : pScene->getEntitiesMut()) {
            if (!entity.getActive()) {
                continue;
            }
            auto* transform = entity.getComponent<TransformComponent>();
            const glm::mat4 model = transform ? transform->getLocalMatrix() : glm::mat4(1.0f);
            for (auto* meshRenderer : entity.getComponents<MeshRendererComponent>()) {
                if (!meshRenderer || !meshRenderer->getMesh()) {
                    continue;
                }
                metalDrawables_[idx].model = model;
                ++idx;
            }
        }
    }

    void ScrapEngineApp::updateClothMeshesMetal() {
        if (!pMetalPresenter || !pScene) {
            return;
        }
        void* dev = pMetalPresenter->mtlDevice();
        if (dev == nullptr) {
            return;
        }

        for (auto& entity : pScene->getEntitiesMut()) {
            if (!entity.getActive()) {
                continue;
            }

            for (auto* clothComp : entity.getComponents<ClothComponent>()) {
                auto runtimeMesh = clothComp->getRuntimeMesh();
                if (!runtimeMesh) {
                    continue;
                }

                MeshRendererComponent* clothRenderer = nullptr;
                auto meshRenderers = entity.getComponents<MeshRendererComponent>();
                for (auto* meshRenderer : meshRenderers) {
                    if (meshRenderer && meshRenderer->getMesh() == runtimeMesh) {
                        clothRenderer = meshRenderer;
                        break;
                    }
                }

                bool repointedRuntimeMesh = false;
                if (!clothRenderer && !meshRenderers.empty()) {
                    clothRenderer = meshRenderers.front();
                    if (clothRenderer->getMesh() != runtimeMesh) {
                        clothRenderer->setMesh(runtimeMesh);
                        repointedRuntimeMesh = true;
                    }
                }

                bool regenerateTangents = true;
                if (clothRenderer) {
                    const auto material = clothRenderer->getMaterial();
                    regenerateTangents =
                        material && material->getTexture(modeling::TextureType::Normal);
                }

                const bool runtimeMeshDirty = clothComp->isRuntimeMeshDirty();
                if (runtimeMeshDirty && !clothComp->syncRuntimeMesh(regenerateTangents)) {
                    continue;
                }

                if (!runtimeMesh->isValid()) {
                    continue;
                }

                scrap::gpu::metal::MetalDrawablePBR* drawable =
                    clothRenderer ? metalDrawableForEntity(*pScene, metalDrawables_, *clothRenderer)
                                  : nullptr;
                if (!drawable) {
                    continue;
                }

                if (clothRenderer && drawable->material != clothRenderer->getMaterial().get()) {
                    drawable->material = clothRenderer->getMaterial().get();
                    if (drawable->material) {
                        drawable->doubleSided = drawable->material->getProperties().doubleSided;
                    }
                }

                if (runtimeMeshDirty || repointedRuntimeMesh ||
                    drawable->mesh.vertexBuffer == nullptr) {
                    scrap::gpu::metal::syncMetalMeshWithCpuMesh(dev, *runtimeMesh, drawable->mesh);
                }
            }
        }
    }

    void ScrapEngineApp::setCursorCapture(bool captured) {
        cursorCaptured = captured;
        if (platformView) {
            platformView->setCursorCaptured(captured);
        }
        firstMouse = true;
    }

    void ScrapEngineApp::syncRigidBodiesToTransforms() {
        if (pScene) {
            physics_demo::syncRigidBodiesToTransforms(*pScene);
        }
    }

    std::vector<RigidBodyComponent*> ScrapEngineApp::collectRigidBodies() {
        return pScene ? physics_demo::collectRigidBodies(*pScene)
                      : std::vector<RigidBodyComponent*>{};
    }

    void ScrapEngineApp::frameLoadedSceneCamera() {
        if (!pScene) {
            return;
        }

        const SceneBounds bounds = computeSceneBounds(*pScene);
        if (!bounds.valid) {
            return;
        }

        const glm::vec3 center = 0.5f * (bounds.min + bounds.max);
        const glm::vec3 extents = 0.5f * (bounds.max - bounds.min);
        const float radius = std::max(glm::length(extents), 0.5f);

        auto& camera = pScene->getCameraRW();
        const float halfFovRadians = glm::radians(camera.getFOV() * 0.5f);
        const float minDistance = radius * 2.0f;
        const float fitDistance = radius / std::max(std::tan(halfFovRadians), 0.1f);
        const float distance = std::max(minDistance, fitDistance * 1.2f);

        const glm::vec3 viewDirection = glm::normalize(glm::vec3(1.0f, 1.0f, 0.65f));
        camera.lookAt(center + viewDirection * distance, center, glm::vec3(0.0f, 0.0f, 1.0f));
        firstMouse = true;
    }

    void ScrapEngineApp::setupXPBDSolver() {
        if (!pSolver) {
            return;
        }

        pSolver->solverIterations = 40;
        pSolver->rigidSubsteps = 8;
    }

    void ScrapEngineApp::setupDefaultSceneSpin() {
        defaultSceneSpinActive = false;
        defaultSceneSpinEntityName.clear();
    }

    void ScrapEngineApp::beginDrag(double mouseX, double mouseY) {
        if (!pScene) {
            return;
        }

        const auto& camera = pScene->getCameraRW();
        const float scale = platformView ? platformView->getContentScaleFactor() : 1.0f;
        const Ray ray = pickingRayFromViewPointer(camera, mouseX, mouseY, scale);

        Entity* bestEntity = nullptr;
        float bestT = std::numeric_limits<float>::max();

        for (auto& entity : pScene->getEntitiesMut()) {
            if (!entity.getActive()) {
                continue;
            }
            auto* meshRenderer = entity.getComponent<MeshRendererComponent>();
            auto* transform = entity.getComponent<TransformComponent>();
            auto* rigidBody = entity.getComponent<RigidBodyComponent>();
            if (!meshRenderer || !meshRenderer->getMesh() || !transform || !rigidBody) {
                continue;
            }
            if (!rigidBody->canBeDynamic()) {
                continue;
            }

            const AABB box =
                computeWorldAABB(*meshRenderer->getMesh(), transform->getLocalMatrix());
            float t = 0.0f;
            if (rayIntersectsAABB(ray, box, t) && t < bestT) {
                bestT = t;
                bestEntity = &entity;
            }
        }

        if (!bestEntity) {
            return;
        }

        auto* rigidBody = bestEntity->getComponent<RigidBodyComponent>();

        glm::mat4 view = camera.getViewMatrix();
        dragPlaneNormal = -glm::vec3(view[0][2], view[1][2], view[2][2]);
        dragPlanePoint = rigidBody->getPosition();

        float denom = glm::dot(ray.direction, dragPlaneNormal);
        if (std::abs(denom) < 1e-8f) {
            return;
        }
        float tPlane = glm::dot(dragPlanePoint - ray.origin, dragPlaneNormal) / denom;
        glm::vec3 hitOnPlane = ray.origin + tPlane * ray.direction;

        dragOffset = rigidBody->getPosition() - hitOnPlane;
        dragTargetPosition = rigidBody->getPosition();
        dragSmoothedVelocity = glm::vec3(0.0f);
        draggedEntity = bestEntity;
        dragTraceEntity = bestEntity;
        dragTraceStep = 0;
        dragTraceReleaseStepsRemaining = 0;

        if (rigidBody->isSleeping()) {
            rigidBody->wake();
        }
        rigidBody->setVelocity(glm::vec3(0.0f));
        rigidBody->setAngularVelocity(glm::vec3(0.0f));
        rigidBody->clearAccumulatedForce();

        Log::verbose("drag-trace",
                     "phase=begin entity={} target={} offset={} plane_point={} plane_normal={}",
                     bestEntity->get_name(), formatVec3(dragTargetPosition), formatVec3(dragOffset),
                     formatVec3(dragPlanePoint), formatVec3(dragPlaneNormal));
    }

    void ScrapEngineApp::updateDrag(double mouseX, double mouseY) {
        if (!draggedEntity || !pScene) {
            return;
        }

        auto* rigidBody = draggedEntity->getComponent<RigidBodyComponent>();
        if (!rigidBody) {
            draggedEntity = nullptr;
            return;
        }

        const auto& camera = pScene->getCameraRW();
        const float scale = platformView ? platformView->getContentScaleFactor() : 1.0f;
        const Ray ray = pickingRayFromViewPointer(camera, mouseX, mouseY, scale);

        float denom = glm::dot(ray.direction, dragPlaneNormal);
        if (std::abs(denom) < 1e-8f) {
            return;
        }
        float tPlane = glm::dot(dragPlanePoint - ray.origin, dragPlaneNormal) / denom;
        glm::vec3 hitOnPlane = ray.origin + tPlane * ray.direction;

        glm::vec3 newPos = hitOnPlane + dragOffset;

        const float dt = frameDelta;
        const glm::vec3 frameVel = (newPos - dragTargetPosition) / dt;
        constexpr float kSmoothFactor = 0.3f;
        dragSmoothedVelocity = glm::mix(dragSmoothedVelocity, frameVel, kSmoothFactor);

        dragTargetPosition = newPos;

        Log::verbose("drag-trace", "phase=update entity={} target={} frame_vel={} smooth_vel={}",
                     draggedEntity->get_name(), formatVec3(dragTargetPosition),
                     formatVec3(frameVel), formatVec3(dragSmoothedVelocity));
    }

    void ScrapEngineApp::endDrag() {
        if (!draggedEntity) {
            return;
        }

        auto* rigidBody = draggedEntity->getComponent<RigidBodyComponent>();
        if (rigidBody) {
            constexpr float kMaxThrowSpeed = 4.0f;
            glm::vec3 throwVel = glm::mix(rigidBody->getVelocity(), dragSmoothedVelocity, 0.35f);
            float speed = glm::length(throwVel);
            if (speed > kMaxThrowSpeed) {
                throwVel *= kMaxThrowSpeed / speed;
            }
            rigidBody->wake();
            rigidBody->setVelocity(throwVel);
            dragTraceReleaseStepsRemaining =
                std::max(32, static_cast<int>(std::ceil(physicsTickRate * 1.5)));
            Log::verbose("drag-trace", "phase=end entity={} throw_vel={} release_steps={}",
                         draggedEntity->get_name(), formatVec3(throwVel),
                         dragTraceReleaseStepsRemaining);
        }

        draggedEntity = nullptr;
    }

    void ScrapEngineApp::logDragTraceSnapshot(const char* timing, float physicsDt) {
        if (!pScene || !dragTraceEntity || !Log::isPalantirMode()) {
            return;
        }

        auto* focusBody = dragTraceEntity->getComponent<RigidBodyComponent>();
        if (!focusBody) {
            return;
        }

        const bool dragging = draggedEntity == dragTraceEntity;
        Log::verbose(
            "drag-trace",
            "step={} timing={} dt={:.6f} dragging={} release_steps={} entity={} target={} pos={} "
            "com={} vel={} angvel={} sleeping={} invMass={:.5f}",
            dragTraceStep, timing, physicsDt, dragging ? 1 : 0, dragTraceReleaseStepsRemaining,
            dragTraceEntity->get_name(), formatVec3(dragTargetPosition),
            formatVec3(focusBody->getPosition()), formatVec3(focusBody->getWorldCenterOfMass()),
            formatVec3(focusBody->getVelocity()), formatVec3(focusBody->getAngularVelocity()),
            focusBody->isSleeping() ? 1 : 0, focusBody->getInvMass());

        for (const auto& neighbor :
             collectDragTraceNeighbors(*pScene, dragTraceEntity, focusBody)) {
            Log::verbose("drag-trace",
                         "step={} timing={} neighbor={} dist={:.4f} speed={:.4f} pos={} com={} "
                         "vel={} angvel={} sleeping={}",
                         dragTraceStep, timing, neighbor.entity ? neighbor.entity->get_name() : "?",
                         neighbor.distance, neighbor.speed,
                         formatVec3(neighbor.rigidBody->getPosition()),
                         formatVec3(neighbor.rigidBody->getWorldCenterOfMass()),
                         formatVec3(neighbor.rigidBody->getVelocity()),
                         formatVec3(neighbor.rigidBody->getAngularVelocity()),
                         neighbor.rigidBody->isSleeping() ? 1 : 0);
        }

        Log::verbose("drag-trace", "step={} timing={} all_positions={}", dragTraceStep, timing,
                     buildAllRigidBodyPositions(*pScene));
    }

} // namespace scrap
