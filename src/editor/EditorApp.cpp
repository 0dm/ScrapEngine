#include <app/components/MeshRendererComponent.hpp>
#include <app/components/TransformComponent.hpp>
#include <app/modeling/GLTFLoader.hpp>
#include <app/modeling/Material.hpp>
#include <app/modeling/Model.hpp>
#include <app/ui/components/SettingsWindow.hpp>
#include <editor/AABB.hpp>
#include <editor/EditorApp.hpp>
#include <editor/gizmos/GizmoRenderer.hpp>
#include <editor/panels/AssetBrowserPanel.hpp>
#include <editor/panels/InspectorPanel.hpp>
#include <editor/panels/SceneHierarchyPanel.hpp>
#include <editor/panels/ViewportPanel.hpp>
#include <gpu/Backend.hpp>
#include <gpu/vulkan/GraphicsPipeline.hpp>

#include <cmath>
#include <csignal>
#include <cstring>
#include <editor/zip_file.hpp>
#include <filesystem>
#include <fstream>
#include <glm/gtc/quaternion.hpp>
#include <imgui.h>
#include <imgui_internal.h>
#include <iostream>
#include <mach-o/dyld.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace scrap::editor {

    static_assert(scrap::gpu::kActiveBackend == scrap::gpu::BackendKind::Vulkan,
                  "Editor stays on gpu/vulkan.");

    namespace {

        bool isKeyDown(const platform::InputState& input, platform::Key key) {
            return input.keys[platform::keyIndex(key)];
        }

        bool isKeyPressed(const platform::InputState& input, platform::Key key) {
            return input.keyPressed[platform::keyIndex(key)];
        }

        const platform::MouseButtonState& getMouseButton(const platform::InputState& input,
                                                         platform::MouseButton button) {
            return input.pointer.buttons[static_cast<std::size_t>(button)];
        }

        std::filesystem::path getCurrentExecutablePath() {
            uint32_t size = 0;
            _NSGetExecutablePath(nullptr, &size);
            std::string buffer(size, '\0');
            if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
                return {};
            }
            return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()));
        }

        std::filesystem::path getEngineExecutablePath() {
            const std::filesystem::path editorPath = getCurrentExecutablePath();
            if (editorPath.empty()) {
                return {};
            }

            return editorPath.parent_path() / "ScrapEngine";
        }

    } // namespace

    static constexpr uint32_t EDITOR_WIDTH = 1920;
    static constexpr uint32_t EDITOR_HEIGHT = 1080;

    EditorApp::EditorApp() {
        scrap::Log::init();
        settingsManager.load();
        scrap::Log::setPalantirMode(settingsManager.get().palantirMode);
        SCRAP_LOG("Editor", "ScrapEditor starting up");

        settingsManager.setOnChangeCallback(
            [this](const scrap::EditorSettings& s) { applySettings(s); });
    }

    EditorApp::~EditorApp() {
        SCRAP_LOG("Editor", "ScrapEditor shutting down");

        // Stop play mode if active
        if (playModeActive) {
            stopPlayMode();
        }

        if (pRenderer) {
            logicalDevice->waitIdle();
        }

        settingsWindow.reset();
        hierarchyPanel.reset();
        inspectorPanel.reset();
        viewportPanel.reset();
        assetBrowserPanel.reset();

        pGizmoRenderer.reset();
        pGridPipeline.reset();
        pUnlitPipeline.reset();
        pLitPipeline.reset();
        pOffscreenFB.reset();

        pImGuiRenderer.reset();
        // Materials own descriptor sets from Renderer's pool — tear down Scene before Renderer.
        pScene.reset();
        pRenderer.reset();
        // Same static layout as main app: if Renderer construction fails after Material::initDescriptorSetLayout,
        // ~Renderer never runs; always clear while the device is still valid.
        modeling::Material::cleanup();

        scrap::Log::shutdown();
    }

    void EditorApp::run() {
        initWindow();
        initVulkan();
        initEditor();

        settingsWindow = std::make_unique<scrap::ui::SettingsWindow>(settingsManager);
        applySettings(settingsManager.get());

        mainLoop();
    }

    void EditorApp::initWindow() {
        window = scrap::platform::createPlatformWindow({
            .title = "ScrapEditor",
            .width = EDITOR_WIDTH,
            .height = EDITOR_HEIGHT,
            .resizable = true,
            .acceptFileDrops = true,
        });
        window->setCursorCaptured(false);
    }

    void EditorApp::initVulkan() {
        pInstance = std::make_unique<scrap::Instance>();

        pRenderSurface =
            std::make_unique<scrap::RenderSurface>(*pInstance, window->getMetalLayerHandle());

        physicalDevice = {*pInstance};
        logicalDevice = {physicalDevice, *pRenderSurface};

        scrap::CameraCreateInfo cameraCreateInfo{
            .scrWidth = static_cast<float>(EDITOR_WIDTH),
            .scrHeight = static_cast<float>(EDITOR_HEIGHT),
        };

        pScene = std::make_unique<scrap::Scene>(cameraCreateInfo);

        scrap::RendererCreateInfo rendererCreateInfo{
            .physicalDevice = physicalDevice,
            .logicalDevice = logicalDevice,
            .renderSurface = *pRenderSurface,
            .platformView = *window,
        };

        pRenderer = std::make_unique<scrap::Renderer>(rendererCreateInfo);

        scrap::ImGuiRendererCreateInfo imguiCreateInfo{
            .instance = **pInstance,
            .physicalDevice = physicalDevice,
            .logicalDevice = logicalDevice,
            .queueFamilyIndex = logicalDevice.getQueueIndex(),
            .platformView = *window,
            .queue = pRenderer->getQueue(),
            .commandPool = pRenderer->getCommandPool(),
            .swapChain = pRenderer->getSwapChain(),
            .imageCount = static_cast<uint32_t>(pRenderer->getSwapChain().getImages().size()),
            .swapChainFormat = pRenderer->getSwapChain().getSurfaceFormat().format,
            .depthFormat = scrap::GraphicsPipeline::findDepthFormat(physicalDevice),
        };

        pImGuiRenderer = std::make_unique<scrap::ImGuiRenderer>(imguiCreateInfo);

        // Enable docking
        ImGuiIO& io = ImGui::GetIO();
        io.ConfigFlags |= ImGuiConfigFlags_DockingEnable;

        setupEditorTheme();

        // Create offscreen framebuffer for viewport rendering
        pOffscreenFB =
            std::make_unique<OffscreenFramebuffer>(physicalDevice, logicalDevice, 800, 600);

        // Create grid pipeline (no vertex input, alpha blending, no culling, no depth write)
        scrap::GraphicsPipelineConfig gridConfig{
            .physicalDevice = physicalDevice,
            .logicalDevice = logicalDevice,
            .descriptorSetLayouts = {*pRenderer->getDescriptorSetLayout0()},
            .colorFormat = OffscreenFramebuffer::COLOR_FORMAT,
            .hasVertexInput = false,
            .enableBlending = true,
            .enableCulling = false,
            .depthWrite = false,
            .hasPushConstants = false,
            .pushConstantSize = 0,
        };
        pGridPipeline = std::make_unique<scrap::GraphicsPipeline>(
            physicalDevice, logicalDevice, gridConfig.descriptorSetLayouts,
            OffscreenFramebuffer::COLOR_FORMAT, "shaders/editor_grid.vert.spv",
            "shaders/editor_grid.frag.spv", gridConfig);

        // Create unlit pipeline (vertex input, no blending, culling, depth write, push constants)
        scrap::GraphicsPipelineConfig unlitConfig{
            .physicalDevice = physicalDevice,
            .logicalDevice = logicalDevice,
            .descriptorSetLayouts = {*pRenderer->getDescriptorSetLayout0()},
            .colorFormat = OffscreenFramebuffer::COLOR_FORMAT,
            .hasVertexInput = true,
            .enableBlending = false,
            .enableCulling = true,
            .depthWrite = true,
            .hasPushConstants = true,
            .pushConstantSize = sizeof(MeshPushConstants),
        };
        pUnlitPipeline = std::make_unique<scrap::GraphicsPipeline>(
            physicalDevice, logicalDevice, unlitConfig.descriptorSetLayouts,
            OffscreenFramebuffer::COLOR_FORMAT, "shaders/editor_unlit.vert.spv",
            "shaders/editor_unlit.frag.spv", unlitConfig);

        // Create lit pipeline (same config, PBR shaders)
        scrap::GraphicsPipelineConfig litConfig{
            .physicalDevice = physicalDevice,
            .logicalDevice = logicalDevice,
            .descriptorSetLayouts = {*pRenderer->getDescriptorSetLayout0()},
            .colorFormat = OffscreenFramebuffer::COLOR_FORMAT,
            .hasVertexInput = true,
            .enableBlending = true,
            .enableCulling = true,
            .depthWrite = true,
            .hasPushConstants = true,
            .pushConstantSize = sizeof(MeshPushConstants),
        };
        pLitPipeline = std::make_unique<scrap::GraphicsPipeline>(
            physicalDevice, logicalDevice, litConfig.descriptorSetLayouts,
            OffscreenFramebuffer::COLOR_FORMAT, "shaders/editor_lit.vert.spv",
            "shaders/editor_lit.frag.spv", litConfig);

        // Create gizmo renderer
        pGizmoRenderer = std::make_unique<GizmoRenderer>(
            physicalDevice, logicalDevice, pRenderer->getDescriptorSetLayout0(),
            OffscreenFramebuffer::COLOR_FORMAT, *pRenderer);

        // Set up custom command buffer recording for the editor
        pRenderer->setCommandBufferRecorder(
            [this](vk::raii::CommandBuffer& cmd, uint32_t imageIndex) {
                recordEditorCommandBuffer(cmd, imageIndex);
            });
    }

    void EditorApp::setupEditorTheme() {
        ImGuiStyle& style = ImGui::GetStyle();

        // Rounding
        style.WindowRounding = 4.0f;
        style.ChildRounding = 2.0f;
        style.FrameRounding = 3.0f;
        style.PopupRounding = 3.0f;
        style.ScrollbarRounding = 3.0f;
        style.GrabRounding = 2.0f;
        style.TabRounding = 3.0f;

        // Spacing
        style.WindowPadding = ImVec2(8, 8);
        style.FramePadding = ImVec2(6, 4);
        style.ItemSpacing = ImVec2(8, 4);
        style.ItemInnerSpacing = ImVec2(4, 4);
        style.IndentSpacing = 16.0f;

        // Borders
        style.WindowBorderSize = 1.0f;
        style.ChildBorderSize = 1.0f;
        style.FrameBorderSize = 0.0f;
        style.TabBorderSize = 0.0f;

        style.ScrollbarSize = 12.0f;
        style.GrabMinSize = 8.0f;

        ImVec4* colors = style.Colors;

        // Background
        colors[ImGuiCol_WindowBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
        colors[ImGuiCol_ChildBg] = ImVec4(0.12f, 0.12f, 0.14f, 1.00f);
        colors[ImGuiCol_PopupBg] = ImVec4(0.10f, 0.10f, 0.12f, 0.96f);

        // Borders
        colors[ImGuiCol_Border] = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
        colors[ImGuiCol_BorderShadow] = ImVec4(0.00f, 0.00f, 0.00f, 0.00f);

        // Frame (input fields, checkboxes)
        colors[ImGuiCol_FrameBg] = ImVec4(0.18f, 0.18f, 0.22f, 1.00f);
        colors[ImGuiCol_FrameBgHovered] = ImVec4(0.24f, 0.24f, 0.30f, 1.00f);
        colors[ImGuiCol_FrameBgActive] = ImVec4(0.30f, 0.30f, 0.38f, 1.00f);

        // Title bar
        colors[ImGuiCol_TitleBg] = ImVec4(0.09f, 0.09f, 0.11f, 1.00f);
        colors[ImGuiCol_TitleBgActive] = ImVec4(0.12f, 0.12f, 0.16f, 1.00f);
        colors[ImGuiCol_TitleBgCollapsed] = ImVec4(0.09f, 0.09f, 0.11f, 0.75f);

        // Menu bar
        colors[ImGuiCol_MenuBarBg] = ImVec4(0.14f, 0.14f, 0.16f, 1.00f);

        // Scrollbar
        colors[ImGuiCol_ScrollbarBg] = ImVec4(0.10f, 0.10f, 0.12f, 1.00f);
        colors[ImGuiCol_ScrollbarGrab] = ImVec4(0.28f, 0.28f, 0.32f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabHovered] = ImVec4(0.36f, 0.36f, 0.42f, 1.00f);
        colors[ImGuiCol_ScrollbarGrabActive] = ImVec4(0.44f, 0.44f, 0.52f, 1.00f);

        // Buttons
        colors[ImGuiCol_Button] = ImVec4(0.22f, 0.22f, 0.28f, 1.00f);
        colors[ImGuiCol_ButtonHovered] = ImVec4(0.32f, 0.32f, 0.42f, 1.00f);
        colors[ImGuiCol_ButtonActive] = ImVec4(0.38f, 0.38f, 0.50f, 1.00f);

        // Headers (collapsing headers, tree nodes)
        colors[ImGuiCol_Header] = ImVec4(0.20f, 0.20f, 0.26f, 1.00f);
        colors[ImGuiCol_HeaderHovered] = ImVec4(0.28f, 0.28f, 0.38f, 1.00f);
        colors[ImGuiCol_HeaderActive] = ImVec4(0.34f, 0.34f, 0.46f, 1.00f);

        // Separator
        colors[ImGuiCol_Separator] = ImVec4(0.22f, 0.22f, 0.26f, 1.00f);
        colors[ImGuiCol_SeparatorHovered] = ImVec4(0.40f, 0.55f, 0.80f, 0.78f);
        colors[ImGuiCol_SeparatorActive] = ImVec4(0.40f, 0.55f, 0.80f, 1.00f);

        // Resize grip
        colors[ImGuiCol_ResizeGrip] = ImVec4(0.30f, 0.30f, 0.40f, 0.30f);
        colors[ImGuiCol_ResizeGripHovered] = ImVec4(0.40f, 0.55f, 0.80f, 0.67f);
        colors[ImGuiCol_ResizeGripActive] = ImVec4(0.40f, 0.55f, 0.80f, 0.95f);

        // Tabs
        colors[ImGuiCol_Tab] = ImVec4(0.14f, 0.14f, 0.18f, 1.00f);
        colors[ImGuiCol_TabHovered] = ImVec4(0.28f, 0.28f, 0.38f, 1.00f);
        colors[ImGuiCol_TabSelected] = ImVec4(0.22f, 0.22f, 0.30f, 1.00f);
        colors[ImGuiCol_TabDimmed] = ImVec4(0.10f, 0.10f, 0.14f, 1.00f);
        colors[ImGuiCol_TabDimmedSelected] = ImVec4(0.16f, 0.16f, 0.22f, 1.00f);

        // Docking
        colors[ImGuiCol_DockingPreview] = ImVec4(0.40f, 0.55f, 0.80f, 0.70f);
        colors[ImGuiCol_DockingEmptyBg] = ImVec4(0.08f, 0.08f, 0.10f, 1.00f);

        // Text
        colors[ImGuiCol_Text] = ImVec4(0.88f, 0.88f, 0.90f, 1.00f);
        colors[ImGuiCol_TextDisabled] = ImVec4(0.46f, 0.46f, 0.50f, 1.00f);

        // Selection / Highlight
        colors[ImGuiCol_CheckMark] = ImVec4(0.45f, 0.65f, 0.95f, 1.00f);
        colors[ImGuiCol_SliderGrab] = ImVec4(0.40f, 0.55f, 0.80f, 1.00f);
        colors[ImGuiCol_SliderGrabActive] = ImVec4(0.50f, 0.65f, 0.90f, 1.00f);

        // Drag drop
        colors[ImGuiCol_DragDropTarget] = ImVec4(0.45f, 0.65f, 0.95f, 0.90f);

        // Nav
        colors[ImGuiCol_NavHighlight] = ImVec4(0.40f, 0.55f, 0.80f, 1.00f);
    }

    void EditorApp::initEditor() {
        editorCamera.setScreenSize(EDITOR_WIDTH, EDITOR_HEIGHT);

        hierarchyPanel = std::make_unique<SceneHierarchyPanel>(*this);
        inspectorPanel = std::make_unique<InspectorPanel>(*this);
        viewportPanel = std::make_unique<ViewportPanel>(*this);
        assetBrowserPanel = std::make_unique<AssetBrowserPanel>(*this);

        // Seed scene with a few test entities
        scrap::Entity cameraEntity("Main Camera");
        cameraEntity.addComponent<TransformComponent>();
        pScene->addEntity(std::move(cameraEntity));

        scrap::Entity lightEntity("Directional Light");
        lightEntity.addComponent<TransformComponent>();
        pScene->addEntity(std::move(lightEntity));

        // Load initial scene file if specified via command line
        if (!initialSceneFile.empty()) {
            openScene(initialSceneFile);
        } else {
            setStatusMessage("ScrapEditor ready");
        }
    }

    void EditorApp::importGLTFToScene(const std::string& path) {
        try {
            size_t before = pScene->getEntities().size();
            pScene->loadGLTFModel(path, true);
            size_t added = pScene->getEntities().size() - before;
            setStatusMessage("Imported " + std::filesystem::path(path).filename().string() + " (" +
                             std::to_string(added) + " entities)");
        } catch (const std::exception& e) {
            setStatusMessage("Import failed: " + std::string(e.what()));
        }
    }

    void EditorApp::replaceModelOnComponent(MeshRendererComponent& mrc, const std::string& path) {
        try {
            scrap::modeling::GLTFLoader loader;
            auto model = loader.loadModel(path);
            if (!model) {
                setStatusMessage("Failed to load: " +
                                 std::filesystem::path(path).filename().string());
                return;
            }
            auto pairs = model->getAllMeshMaterialPairs();
            if (pairs.empty()) {
                setStatusMessage("No meshes in: " +
                                 std::filesystem::path(path).filename().string());
                return;
            }

            logicalDevice->waitIdle();
            mrc.setMesh(pairs[0].mesh);
            mrc.setMaterial(pairs[0].material);
            mrc.setModelPath(path);
            setStatusMessage("Changed model to: " +
                             std::filesystem::path(path).filename().string());
        } catch (const std::exception& e) {
            setStatusMessage("Model change failed: " + std::string(e.what()));
        }
    }

    void EditorApp::clearModelOnComponent(MeshRendererComponent& mrc) {
        logicalDevice->waitIdle();
        mrc.setMesh(nullptr);
        mrc.setMaterial(nullptr);
        mrc.setModelPath("");
        setStatusMessage("Cleared mesh");
    }

    void EditorApp::openScene(const std::string& path) {
        try {
            logicalDevice->waitIdle();
            selectionManager.deselect();
            if (pScene->loadFromFile(path)) {
                setStatusMessage("Opened: " + std::filesystem::path(path).filename().string());
            } else {
                setStatusMessage("Failed to open scene: " + path);
            }
        } catch (const std::exception& e) {
            setStatusMessage("Open failed: " + std::string(e.what()));
        }
    }

    void EditorApp::saveScene() {
        if (pScene->hasFilePath()) {
            saveSceneAs(pScene->getCurrentFilePath());
        } else {
            // Open Save As dialog
            std::string defaultPath =
                (std::filesystem::current_path() / "assets" / "scene.gltf").string();
            std::strncpy(dialogPathBuf, defaultPath.c_str(), sizeof(dialogPathBuf) - 1);
            dialogPathBuf[sizeof(dialogPathBuf) - 1] = '\0';
            showSaveAsDialog = true;
        }
    }

    void EditorApp::saveSceneAs(const std::string& path) {
        try {
            if (pScene->saveToFile(path)) {
                setStatusMessage("Saved: " + std::filesystem::path(path).filename().string());
            } else {
                setStatusMessage("Failed to save scene: " + path);
            }
        } catch (const std::exception& e) {
            setStatusMessage("Save failed: " + std::string(e.what()));
        }
    }

    void EditorApp::uploadMeshGPUResources() {
        for (auto& entity : pScene->getEntitiesMut()) {
            auto mrcs = entity.getComponents<MeshRendererComponent>();
            for (auto* mrc : mrcs) {
                auto mesh = mrc->getMesh();
                if (!mesh || !mesh->isValid())
                    continue;
                if (!mesh->hasGPUData()) {
                    auto& physDev = const_cast<vk::raii::PhysicalDevice&>(*physicalDevice);
                    auto& cmdPool = const_cast<vk::raii::CommandPool&>(pRenderer->getCommandPool());
                    auto& queue = const_cast<vk::raii::Queue&>(pRenderer->getQueue());
                    mesh->initVulkanResources(logicalDevice, physDev, cmdPool, queue);
                }
            }
        }
    }

    void EditorApp::pickEntityAtScreen(float windowX, float windowY) {
        auto* vp = static_cast<ViewportPanel*>(viewportPanel.get());
        if (!vp)
            return;

        ImVec2 vpPos = vp->getViewportScreenPos();
        ImVec2 vpSize = vp->getViewportSize();
        if (vpSize.x <= 0 || vpSize.y <= 0)
            return;

        float localX = windowX - vpPos.x;
        float localY = windowY - vpPos.y;

        // Check if click is within viewport bounds
        if (localX < 0 || localY < 0 || localX > vpSize.x || localY > vpSize.y)
            return;

        Ray ray = editorCamera.screenToWorldRay(localX, localY, vpSize.x, vpSize.y);

        int bestIdx = -1;
        float bestDist = std::numeric_limits<float>::max();

        auto& entities = pScene->getEntitiesMut();
        for (int i = 0; i < static_cast<int>(entities.size()); ++i) {
            auto& entity = entities[i];
            if (!entity.getActive())
                continue;

            auto mrcs = entity.getComponents<MeshRendererComponent>();
            if (mrcs.empty())
                continue;

            glm::mat4 modelMatrix = glm::mat4(1.0f);
            auto* tc = entity.getComponent<TransformComponent>();
            if (tc) {
                modelMatrix = tc->getLocalMatrix();
            }

            for (auto* mrc : mrcs) {
                auto mesh = mrc->getMesh();
                if (!mesh || !mesh->isValid())
                    continue;

                AABB localAABB = AABB::fromVertices(mesh->getVertices());
                AABB worldAABB = localAABB.transformed(modelMatrix);

                float t = 0.0f;
                if (rayIntersectsAABB(ray.origin, ray.direction, worldAABB, t)) {
                    if (t < bestDist) {
                        bestDist = t;
                        bestIdx = i;
                    }
                }
            }
        }

        if (bestIdx >= 0) {
            selectionManager.select(bestIdx);
            setStatusMessage("Selected: " + entities[bestIdx].get_name());
        } else {
            selectionManager.deselect();
        }
    }

    void EditorApp::recordEditorCommandBuffer(vk::raii::CommandBuffer& cmd, uint32_t imageIndex) {
        cmd.begin({});

        // Update UBO with camera matrices - use EditorCamera directly (bypasses Scene Camera)
        float aspect = static_cast<float>(pOffscreenFB->getWidth()) /
                       static_cast<float>(pOffscreenFB->getHeight());
        scrap::UniformBufferObject ubo{
            .model = glm::mat4(1.0f), // identity for grid
            .view = editorCamera.getViewMatrix(),
            .proj = editorCamera.getProjectionMatrix(aspect),
            .cameraPos = editorCamera.getPosition(),
        };
        ubo.proj[1][1] *= -1; // Vulkan Y-flip
        memcpy(pRenderer->getCurrentUniformBufferMapped(), &ubo, sizeof(ubo));

        // ========================
        // PASS 1: Render scene to offscreen framebuffer
        // ========================

        // Transition offscreen color image -> ColorAttachmentOptimal
        pRenderer->transitionImageLayout(
            cmd, *pOffscreenFB->getColorImage(), vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, {},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::ImageAspectFlagBits::eColor);

        // Transition offscreen depth image -> DepthAttachmentOptimal
        pRenderer->transitionImageLayout(cmd, *pOffscreenFB->getDepthImage(),
                                         vk::ImageLayout::eUndefined,
                                         vk::ImageLayout::eDepthAttachmentOptimal, {},
                                         vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                                         vk::PipelineStageFlagBits2::eTopOfPipe,
                                         vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                             vk::PipelineStageFlagBits2::eLateFragmentTests,
                                         vk::ImageAspectFlagBits::eDepth);

        vk::ClearValue clearColor = vk::ClearColorValue{0.12f, 0.12f, 0.15f, 1.0f};
        vk::ClearValue clearDepth = vk::ClearDepthStencilValue(1.0f, 0);

        vk::RenderingAttachmentInfo offscreenColorAttachment{
            .imageView = pOffscreenFB->getColorImageView(),
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearColor,
        };

        vk::RenderingAttachmentInfo offscreenDepthAttachment{
            .imageView = pOffscreenFB->getDepthImageView(),
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = clearDepth,
        };

        vk::Extent2D offscreenExtent{pOffscreenFB->getWidth(), pOffscreenFB->getHeight()};

        vk::RenderingInfo offscreenRenderingInfo{
            .renderArea = {.offset = {0, 0}, .extent = offscreenExtent},
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &offscreenColorAttachment,
            .pDepthAttachment = &offscreenDepthAttachment,
        };

        cmd.beginRendering(offscreenRenderingInfo);

        cmd.setViewport(0, vk::Viewport(0.0f, 0.0f, static_cast<float>(offscreenExtent.width),
                                        static_cast<float>(offscreenExtent.height), 0.0f, 1.0f));
        cmd.setScissor(0, vk::Rect2D(vk::Offset2D(0, 0), offscreenExtent));

        // Draw grid
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **pGridPipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, pGridPipeline->getLayout(), 0,
                               {*pRenderer->getCurrentDescriptorSet()}, nullptr);
        cmd.draw(6, 1, 0, 0); // Fullscreen quad (6 vertices, no vertex buffer)

        // Draw scene meshes with active pipeline (unlit or lit based on viewport mode)
        auto* activePipeline =
            (viewportMode == ViewportMode::Lit) ? pLitPipeline.get() : pUnlitPipeline.get();
        cmd.bindPipeline(vk::PipelineBindPoint::eGraphics, **activePipeline);
        cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, activePipeline->getLayout(), 0,
                               {*pRenderer->getCurrentDescriptorSet()}, nullptr);

        for (auto& entity : pScene->getEntitiesMut()) {
            if (!entity.getActive())
                continue;

            auto mrcs = entity.getComponents<MeshRendererComponent>();
            if (mrcs.empty())
                continue;

            // Get transform for this entity
            glm::mat4 modelMatrix = glm::mat4(1.0f);
            auto* tc = entity.getComponent<TransformComponent>();
            if (tc) {
                modelMatrix = tc->getLocalMatrix();
            }

            for (auto* mrc : mrcs) {
                auto mesh = mrc->getMesh();
                if (!mesh || !mesh->hasGPUData())
                    continue;

                // Build push constants with material data
                MeshPushConstants pushData{};
                pushData.model = modelMatrix;
                pushData.baseColor = glm::vec4(0.7f, 0.7f, 0.7f, 1.0f); // default grey
                pushData.metallic = 0.0f;
                pushData.roughness = 0.5f;

                auto material = mrc->getMaterial();
                if (material) {
                    auto& props = material->getProperties();
                    pushData.baseColor = props.baseColorFactor;
                    pushData.metallic = props.metallicFactor;
                    pushData.roughness = props.roughnessFactor;
                }

                cmd.pushConstants<MeshPushConstants>(activePipeline->getLayout(),
                                                     vk::ShaderStageFlagBits::eVertex |
                                                         vk::ShaderStageFlagBits::eFragment,
                                                     0, pushData);

                auto& cmdRef = const_cast<vk::raii::CommandBuffer&>(cmd);
                mesh->bind(cmdRef);
                mesh->draw(cmdRef);
            }
        }

        // Draw gizmo for selected entity
        if (selectionManager.hasSelection() && pGizmoRenderer) {
            auto* entity = selectionManager.getSelectedEntity(*pScene);
            if (entity) {
                auto* tc = entity->getComponent<TransformComponent>();
                if (tc) {
                    pGizmoRenderer->render(cmd, pRenderer->getCurrentDescriptorSet(),
                                           tc->getTranslation(), editorCamera, aspect);
                }
            }
        }

        cmd.endRendering();

        // Transition offscreen color image -> ShaderReadOnlyOptimal for ImGui sampling
        pRenderer->transitionImageLayout(
            cmd, *pOffscreenFB->getColorImage(), vk::ImageLayout::eColorAttachmentOptimal,
            vk::ImageLayout::eShaderReadOnlyOptimal, vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::AccessFlagBits2::eShaderRead, vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eFragmentShader, vk::ImageAspectFlagBits::eColor);

        // ========================
        // PASS 2: Render ImGui to swapchain
        // ========================

        // Transition swapchain image -> ColorAttachmentOptimal
        pRenderer->transitionImageLayout(
            cmd, pRenderer->getSwapChain().getImages()[imageIndex], vk::ImageLayout::eUndefined,
            vk::ImageLayout::eColorAttachmentOptimal, {},
            vk::AccessFlagBits2::eColorAttachmentWrite,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eColorAttachmentOutput, vk::ImageAspectFlagBits::eColor);

        // Transition renderer's depth image for the ImGui pass (ImGui pipeline expects depth format)
        pRenderer->transitionImageLayout(cmd, *pRenderer->getDepthImage(),
                                         vk::ImageLayout::eUndefined,
                                         vk::ImageLayout::eDepthAttachmentOptimal, {},
                                         vk::AccessFlagBits2::eDepthStencilAttachmentWrite,
                                         vk::PipelineStageFlagBits2::eTopOfPipe,
                                         vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                                             vk::PipelineStageFlagBits2::eLateFragmentTests,
                                         vk::ImageAspectFlagBits::eDepth);

        vk::ClearValue swapClearColor = vk::ClearColorValue{0.0f, 0.0f, 0.0f, 1.0f};
        vk::ClearValue swapClearDepth = vk::ClearDepthStencilValue(1.0f, 0);

        vk::RenderingAttachmentInfo swapColorAttachment{
            .imageView = pRenderer->getSwapChain().getImageViews()[imageIndex],
            .imageLayout = vk::ImageLayout::eColorAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eStore,
            .clearValue = swapClearColor,
        };

        vk::RenderingAttachmentInfo swapDepthAttachment{
            .imageView = pRenderer->getDepthImageView(),
            .imageLayout = vk::ImageLayout::eDepthAttachmentOptimal,
            .loadOp = vk::AttachmentLoadOp::eClear,
            .storeOp = vk::AttachmentStoreOp::eDontCare,
            .clearValue = swapClearDepth,
        };

        vk::RenderingInfo swapRenderingInfo{
            .renderArea =
                {
                    .offset = {0, 0},
                    .extent = pRenderer->getSwapChain().getExtent(),
                },
            .layerCount = 1,
            .colorAttachmentCount = 1,
            .pColorAttachments = &swapColorAttachment,
            .pDepthAttachment = &swapDepthAttachment,
        };

        cmd.beginRendering(swapRenderingInfo);

        // Render ImGui (which includes the viewport panel showing the offscreen texture)
        if (pImGuiRenderer) {
            pImGuiRenderer->render(cmd, imageIndex);
        }

        cmd.endRendering();

        // Transition swapchain image -> PresentSrcKHR
        pRenderer->transitionImageLayout(
            cmd, pRenderer->getSwapChain().getImages()[imageIndex],
            vk::ImageLayout::eColorAttachmentOptimal, vk::ImageLayout::ePresentSrcKHR,
            vk::AccessFlagBits2::eColorAttachmentWrite, {},
            vk::PipelineStageFlagBits2::eColorAttachmentOutput,
            vk::PipelineStageFlagBits2::eBottomOfPipe, vk::ImageAspectFlagBits::eColor);

        cmd.end();
    }

    void EditorApp::mainLoop() {
        while (!window->shouldClose()) {
            auto currentFrameTime = std::chrono::steady_clock::now();
            deltaTime = std::chrono::duration<float>(currentFrameTime - lastFrameTime).count();
            lastFrameTime = currentFrameTime;

            window->pumpEvents();
            const auto input = window->consumeInputState();
            if (input.framebufferResized && pRenderer) {
                pRenderer->setFramebufferResized();
            }

            processInput(input);

            editorCamera.update(deltaTime);
            editorCamera.syncToSceneCamera(pScene->getCameraRW());

            // Handle viewport resize
            if (viewportPanel) {
                auto* vp = static_cast<ViewportPanel*>(viewportPanel.get());
                if (vp->viewportSizeChanged()) {
                    ImVec2 size = vp->getViewportSize();
                    uint32_t w = static_cast<uint32_t>(size.x);
                    uint32_t h = static_cast<uint32_t>(size.y);
                    if (w > 0 && h > 0) {
                        logicalDevice->waitIdle();
                        pOffscreenFB->resize(w, h);
                        editorCamera.setScreenSize(static_cast<float>(w), static_cast<float>(h));
                    }
                    vp->clearSizeChanged();
                }
            }

            // Check if play mode process has exited on its own
            if (playModeActive && playProcessPid > 0) {
                int status;
                pid_t result = waitpid(playProcessPid, &status, WNOHANG);
                if (result != 0) {
                    // Process has exited
                    playProcessPid = -1;
                    playModeActive = false;
                    if (!playModeTempFile.empty()) {
                        std::error_code ec2;
                        std::filesystem::remove(playModeTempFile, ec2);
                        playModeTempFile.clear();
                    }
                    setStatusMessage("Play mode ended");
                }
            }

            // Upload mesh GPU resources for any newly imported models
            uploadMeshGPUResources();

            pImGuiRenderer->newFrame(deltaTime);
            buildEditorUI();

            pRenderer->drawFrame(logicalDevice, *pScene, pImGuiRenderer.get());
        }

        logicalDevice->waitIdle();
    }

    void EditorApp::processInput(const scrap::platform::InputState& input) {
        handleDroppedFiles(input);
        handleKeyboardShortcuts(input);
        handlePointerInput(input);

        // Always allow fly mode WASD when in fly mode (cursor is captured)
        if (editorCamera.getMode() == EditorCamera::Mode::Fly) {
            if (isKeyDown(input, platform::Key::W))
                editorCamera.flyMoveForward(deltaTime);
            if (isKeyDown(input, platform::Key::S))
                editorCamera.flyMoveBackward(deltaTime);
            if (isKeyDown(input, platform::Key::A))
                editorCamera.flyMoveLeft(deltaTime);
            if (isKeyDown(input, platform::Key::D))
                editorCamera.flyMoveRight(deltaTime);
            return;
        }

        // In orbit mode, only process input when viewport hovered
        if (!viewportHovered || ImGui::GetIO().WantCaptureKeyboard) {
            return;
        }
    }

    void EditorApp::handleDroppedFiles(const scrap::platform::InputState& input) {
        if (!assetBrowserPanel) {
            return;
        }

        for (const auto& path : input.droppedPaths) {
            assetBrowserPanel->handleFileDrop(path);
        }
    }

    void EditorApp::handleKeyboardShortcuts(const scrap::platform::InputState& input) {
        const bool ctrl = input.controlDown;
        const bool shift = input.shiftDown;

        if (ctrl && isKeyPressed(input, platform::Key::S) && shift) {
            std::string defaultPath =
                pScene->hasFilePath()
                    ? pScene->getCurrentFilePath()
                    : (std::filesystem::current_path() / "assets" / "scene.gltf").string();
            std::strncpy(dialogPathBuf, defaultPath.c_str(), sizeof(dialogPathBuf) - 1);
            dialogPathBuf[sizeof(dialogPathBuf) - 1] = '\0';
            showSaveAsDialog = true;
            return;
        }
        if (ctrl && isKeyPressed(input, platform::Key::S)) {
            saveScene();
            return;
        }
        if (ctrl && isKeyPressed(input, platform::Key::O)) {
            std::string defaultPath =
                pScene->hasFilePath()
                    ? pScene->getCurrentFilePath()
                    : (std::filesystem::current_path() / "assets" / "scene.gltf").string();
            std::strncpy(dialogPathBuf, defaultPath.c_str(), sizeof(dialogPathBuf) - 1);
            dialogPathBuf[sizeof(dialogPathBuf) - 1] = '\0';
            showOpenDialog = true;
            return;
        }
        if (ctrl && isKeyPressed(input, platform::Key::N)) {
            logicalDevice->waitIdle();
            pScene->getEntitiesMut().clear();
            pScene->setCurrentFilePath("");
            selectionManager.deselect();
            setStatusMessage("New scene created");
            return;
        }
        if (ctrl && isKeyPressed(input, platform::Key::D)) {
            selectionManager.deselect();
            return;
        }
        if (ctrl && isKeyPressed(input, platform::Key::P)) {
            if (playModeActive) {
                stopPlayMode();
            } else {
                startPlayMode();
            }
            return;
        }

        if (ImGui::GetIO().WantCaptureKeyboard) {
            return;
        }

        if (isKeyPressed(input, platform::Key::Escape)) {
            if (editorCamera.getMode() == EditorCamera::Mode::Fly) {
                editorCamera.endFlyMode();
                rightMouseDown = false;
                window->setCursorCaptured(false);
                return;
            }
            window->requestClose();
        }

        if (isKeyPressed(input, platform::Key::F)) {
            auto* entity = selectionManager.getSelectedEntity(*pScene);
            if (entity) {
                auto* tc = entity->getComponent<TransformComponent>();
                if (tc) {
                    editorCamera.focusOn(tc->getTranslation());
                    setStatusMessage("Focused on: " + entity->get_name());
                }
            }
        }

        if (isKeyPressed(input, platform::Key::DeleteKey)) {
            int idx = selectionManager.getSelectedIndex();
            auto& entities = pScene->getEntitiesMut();
            if (idx >= 0 && idx < static_cast<int>(entities.size())) {
                std::string name = entities[idx].get_name();
                logicalDevice->waitIdle();
                entities.erase(entities.begin() + idx);
                selectionManager.deselect();
                setStatusMessage("Deleted: " + name);
            }
        }

        if (editorCamera.getMode() != EditorCamera::Mode::Fly && !ctrl) {
            if (isKeyPressed(input, platform::Key::W)) {
                activeGizmoMode = GizmoType::Translate;
                if (pGizmoRenderer)
                    pGizmoRenderer->setActiveGizmo(GizmoType::Translate);
                setStatusMessage("Gizmo: Translate");
            }
            if (isKeyPressed(input, platform::Key::E)) {
                activeGizmoMode = GizmoType::Rotate;
                if (pGizmoRenderer)
                    pGizmoRenderer->setActiveGizmo(GizmoType::Rotate);
                setStatusMessage("Gizmo: Rotate");
            }
            if (isKeyPressed(input, platform::Key::R)) {
                activeGizmoMode = GizmoType::Scale;
                if (pGizmoRenderer)
                    pGizmoRenderer->setActiveGizmo(GizmoType::Scale);
                setStatusMessage("Gizmo: Scale");
            }
        }
    }

    void EditorApp::handlePointerInput(const scrap::platform::InputState& input) {
        const auto& leftMouse = getMouseButton(input, platform::MouseButton::Left);
        const auto& rightMouse = getMouseButton(input, platform::MouseButton::Right);
        const auto& middleMouse = getMouseButton(input, platform::MouseButton::Middle);

        const float xpos = input.pointer.x;
        const float ypos = input.pointer.y;
        const float deltaX = input.pointer.deltaX;
        const float deltaY = -input.pointer.deltaY;

        bool guardPress = false;
        if (leftMouse.pressed || rightMouse.pressed || middleMouse.pressed) {
            const bool imguiWants = ImGui::GetIO().WantCaptureMouse;
            const bool inFlyMode = editorCamera.getMode() == EditorCamera::Mode::Fly;
            guardPress = imguiWants && !viewportHovered && !inFlyMode;
        }

        if (rightMouse.pressed && !guardPress) {
            rightMouseDown = true;
            lastMouseX = xpos;
            lastMouseY = ypos;
            if (viewportHovered) {
                editorCamera.beginFlyMode();
                window->setCursorCaptured(true);
            }
        }
        if (rightMouse.released) {
            rightMouseDown = false;
            if (editorCamera.getMode() == EditorCamera::Mode::Fly) {
                editorCamera.endFlyMode();
                window->setCursorCaptured(false);
            }
        }

        if (middleMouse.pressed && !guardPress) {
            middleMouseDown = true;
            lastMouseX = xpos;
            lastMouseY = ypos;
        }
        if (middleMouse.released) {
            middleMouseDown = false;
        }

        if (leftMouse.pressed && !guardPress) {
            leftMouseDown = true;
            lastMouseX = xpos;
            lastMouseY = ypos;
            mousePressX = xpos;
            mousePressY = ypos;

            if (viewportHovered && selectionManager.hasSelection() && pGizmoRenderer) {
                auto* gizmo = pGizmoRenderer->getActiveGizmo();
                auto* entity = selectionManager.getSelectedEntity(*pScene);
                if (gizmo && entity) {
                    auto* tc = entity->getComponent<TransformComponent>();
                    if (tc) {
                        auto* vp = static_cast<ViewportPanel*>(viewportPanel.get());
                        ImVec2 vpPos = vp->getViewportScreenPos();
                        ImVec2 vpSize = vp->getViewportSize();
                        const float localX = xpos - vpPos.x;
                        const float localY = ypos - vpPos.y;
                        Ray ray = editorCamera.screenToWorldRay(localX, localY, vpSize.x, vpSize.y);

                        float dist = glm::length(editorCamera.getPosition() - tc->getTranslation());
                        float gizmoScale = dist * GizmoRenderer::SCALE_FACTOR;
                        GizmoAxis hitAxis = gizmo->hitTest(ray, tc->getTranslation(),
                                                           tc->getRotation(), gizmoScale);
                        if (hitAxis != GizmoAxis::None) {
                            gizmo->beginInteraction(hitAxis, ray, tc->getTranslation(),
                                                    tc->getRotation());
                            gizmoInteracting = true;
                        }
                    }
                }
            }
        }
        if (leftMouse.released) {
            if (gizmoInteracting && pGizmoRenderer) {
                if (auto* gizmo = pGizmoRenderer->getActiveGizmo()) {
                    gizmo->endInteraction();
                }
                gizmoInteracting = false;
            } else {
                float dx = xpos - mousePressX;
                float dy = ypos - mousePressY;
                if (std::sqrt(dx * dx + dy * dy) < 3.0f) {
                    pickEntityAtScreen(xpos, ypos);
                }
            }
            leftMouseDown = false;
        }

        if (input.pointer.scrollY != 0.0f && viewportHovered) {
            editorCamera.zoom(input.pointer.scrollY);
        }

        if (deltaX == 0.0f && deltaY == 0.0f) {
            return;
        }

        lastMouseX = xpos;
        lastMouseY = ypos;

        if (rightMouseDown && editorCamera.getMode() == EditorCamera::Mode::Fly) {
            editorCamera.flyMouseLook(deltaX, deltaY);
            return;
        }

        if (gizmoInteracting && leftMouseDown && pGizmoRenderer) {
            auto* gizmo = pGizmoRenderer->getActiveGizmo();
            auto* entity = selectionManager.getSelectedEntity(*pScene);
            if (gizmo && entity) {
                auto* tc = entity->getComponent<TransformComponent>();
                if (tc) {
                    auto* vp = static_cast<ViewportPanel*>(viewportPanel.get());
                    ImVec2 vpPos = vp->getViewportScreenPos();
                    ImVec2 vpSize = vp->getViewportSize();
                    const float localX = xpos - vpPos.x;
                    const float localY = ypos - vpPos.y;
                    Ray ray = editorCamera.screenToWorldRay(localX, localY, vpSize.x, vpSize.y);

                    glm::vec3 delta =
                        gizmo->updateInteraction(ray, tc->getTranslation(), tc->getRotation());

                    GizmoType type = pGizmoRenderer->getActiveGizmoType();
                    if (type == GizmoType::Translate) {
                        tc->setTranslation(tc->getTranslation() + delta);
                    } else if (type == GizmoType::Rotate) {
                        float angle = glm::length(delta);
                        if (angle > 1e-6f) {
                            glm::vec3 axis = delta / angle;
                            glm::quat rot = glm::angleAxis(angle, axis);
                            tc->setRotation(rot * tc->getRotation());
                        }
                    } else if (type == GizmoType::Scale) {
                        tc->setScale(tc->getScale() + delta);
                    }
                }
            }
            return;
        }

        if (!viewportHovered) {
            return;
        }

        if (leftMouseDown && editorCamera.getMode() == EditorCamera::Mode::Orbit) {
            editorCamera.orbit(deltaX * 0.3f, deltaY * 0.3f);
        } else if (middleMouseDown) {
            editorCamera.pan(deltaX, deltaY);
        }
    }

    void EditorApp::buildEditorUI() {
        // Create full-screen dockspace
        ImGuiViewport* vp = ImGui::GetMainViewport();
        ImGui::SetNextWindowPos(vp->WorkPos);
        ImGui::SetNextWindowSize(vp->WorkSize);
        ImGui::SetNextWindowViewport(vp->ID);

        ImGuiWindowFlags windowFlags = ImGuiWindowFlags_MenuBar | ImGuiWindowFlags_NoDocking;
        windowFlags |= ImGuiWindowFlags_NoTitleBar | ImGuiWindowFlags_NoCollapse;
        windowFlags |= ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove;
        windowFlags |= ImGuiWindowFlags_NoBringToFrontOnFocus | ImGuiWindowFlags_NoNavFocus;
        windowFlags |= ImGuiWindowFlags_NoBackground;

        ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0.0f, 0.0f));

        ImGui::Begin("DockSpace", nullptr, windowFlags);
        ImGui::PopStyleVar(3);

        ImGuiID dockspaceId = ImGui::GetID("EditorDockSpace");

        if (firstFrame) {
            setupDefaultDockLayout(dockspaceId);
            firstFrame = false;
        }

        ImGui::DockSpace(dockspaceId, ImVec2(0.0f, 0.0f), ImGuiDockNodeFlags_None);

        // Menu bar
        if (ImGui::BeginMenuBar()) {
            if (ImGui::BeginMenu("File")) {
                if (ImGui::MenuItem("New Scene", "Ctrl+N")) {
                    logicalDevice->waitIdle();
                    pScene->getEntitiesMut().clear();
                    pScene->setCurrentFilePath("");
                    selectionManager.deselect();
                    setStatusMessage("New scene created");
                }
                if (ImGui::MenuItem("Open Scene...", "Ctrl+O")) {
                    std::string defaultPath =
                        pScene->hasFilePath()
                            ? pScene->getCurrentFilePath()
                            : (std::filesystem::current_path() / "assets" / "scene.gltf").string();
                    std::strncpy(dialogPathBuf, defaultPath.c_str(), sizeof(dialogPathBuf) - 1);
                    dialogPathBuf[sizeof(dialogPathBuf) - 1] = '\0';
                    showOpenDialog = true;
                }
                ImGui::Separator();
                if (ImGui::MenuItem("Save Scene", "Ctrl+S")) {
                    saveScene();
                }
                if (ImGui::MenuItem("Save Scene As...", "Ctrl+Shift+S")) {
                    std::string defaultPath =
                        pScene->hasFilePath()
                            ? pScene->getCurrentFilePath()
                            : (std::filesystem::current_path() / "assets" / "scene.gltf").string();
                    std::strncpy(dialogPathBuf, defaultPath.c_str(), sizeof(dialogPathBuf) - 1);
                    dialogPathBuf[sizeof(dialogPathBuf) - 1] = '\0';
                    showSaveAsDialog = true;
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Export Scene as ZIP...")) {
                    std::string defaultPath =
                        (std::filesystem::current_path() / "scene_export.zip").string();
                    std::strncpy(dialogPathBuf, defaultPath.c_str(), sizeof(dialogPathBuf) - 1);
                    dialogPathBuf[sizeof(dialogPathBuf) - 1] = '\0';
                    showExportZipDialog = true;
                }

                if (ImGui::MenuItem("Import Scene from ZIP...")) {
                    std::string defaultPath =
                        (std::filesystem::current_path() / "scene_import.zip").string();
                    std::strncpy(dialogPathBuf, defaultPath.c_str(), sizeof(dialogPathBuf) - 1);
                    dialogPathBuf[sizeof(dialogPathBuf) - 1] = '\0';
                    showImportZipDialog = true;
                }

                ImGui::Separator();

                if (ImGui::MenuItem("Exit", "Esc")) {
                    window->requestClose();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Edit")) {
                if (ImGui::MenuItem("Deselect All", "Ctrl+D")) {
                    selectionManager.deselect();
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("View")) {
                ImGui::MenuItem("Hierarchy", nullptr, &showHierarchy);
                ImGui::MenuItem("Inspector", nullptr, &showInspector);
                ImGui::MenuItem("Viewport", nullptr, &showViewport);
                ImGui::MenuItem("Asset Browser", nullptr, &showAssetBrowser);
                ImGui::MenuItem("Settings", nullptr, &showSettings);
                ImGui::Separator();
                if (ImGui::MenuItem("Reset Layout")) {
                    firstFrame = true; // re-trigger layout on next frame
                }
                ImGui::EndMenu();
            }
            if (ImGui::BeginMenu("Entity")) {
                if (ImGui::BeginMenu("Create")) {
                    if (ImGui::MenuItem("Empty Entity")) {
                        createEmptyEntity();
                    }
                    if (ImGui::MenuItem("Box")) {
                        createBoxEntity();
                    }
                    if (ImGui::MenuItem("Ball")) {
                        createBallEntity();
                    }
                    ImGui::EndMenu();
                }
                ImGui::EndMenu();
            }

            // Play mode controls - centered in menu bar
            {
                float menuBarWidth = ImGui::GetWindowWidth();
                float buttonWidth = 80.0f;
                float totalWidth = playModeActive ? (buttonWidth * 1 + 8.0f) : buttonWidth;
                float cursorX = (menuBarWidth - totalWidth) * 0.5f;

                // Ensure we don't overlap menus
                if (cursorX < ImGui::GetCursorPosX())
                    cursorX = ImGui::GetCursorPosX() + 20.0f;

                ImGui::SetCursorPosX(cursorX);

                if (!playModeActive) {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.15f, 0.55f, 0.15f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                          ImVec4(0.20f, 0.70f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.10f, 0.45f, 0.10f, 1.0f));
                    if (ImGui::Button("Play", ImVec2(buttonWidth, 0))) {
                        startPlayMode();
                    }
                    ImGui::PopStyleColor(3);
                } else {
                    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.70f, 0.15f, 0.15f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonHovered,
                                          ImVec4(0.85f, 0.20f, 0.20f, 1.0f));
                    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.55f, 0.10f, 0.10f, 1.0f));
                    if (ImGui::Button("Stop", ImVec2(buttonWidth, 0))) {
                        stopPlayMode();
                    }
                    ImGui::PopStyleColor(3);
                }
            }

            ImGui::EndMenuBar();
        }

        ImGui::End();

        // Render panels
        if (showHierarchy && hierarchyPanel) {
            hierarchyPanel->render();
        }
        if (showInspector && inspectorPanel) {
            inspectorPanel->render();
        }
        if (showViewport && viewportPanel) {
            viewportPanel->render();
            viewportHovered = static_cast<ViewportPanel*>(viewportPanel.get())->isViewportHovered();
            viewportFocused = static_cast<ViewportPanel*>(viewportPanel.get())->isViewportFocused();
        }
        if (showAssetBrowser && assetBrowserPanel) {
            assetBrowserPanel->render();
        }
        if (showSettings && settingsWindow) {
            settingsWindow->setEnabled(true);
            settingsWindow->render();
            // Sync back: if user closed the window via its X button
            if (!settingsWindow->isEnabled()) {
                showSettings = false;
            }
        }

        // Status bar at bottom
        {
            if (statusTimer > 0.0f) {
                statusTimer -= deltaTime;
            }

            float statusBarHeight = 24.0f;
            ImGui::SetNextWindowPos(
                ImVec2(vp->WorkPos.x, vp->WorkPos.y + vp->WorkSize.y - statusBarHeight));
            ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x, statusBarHeight));
            ImGuiWindowFlags statusFlags =
                ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoInputs |
                ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings |
                ImGuiWindowFlags_NoDocking | ImGuiWindowFlags_NoFocusOnAppearing;

            ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(8, 3));
            ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0.10f, 0.10f, 0.12f, 1.00f));
            ImGui::Begin("##StatusBar", nullptr, statusFlags);
            ImGui::PopStyleColor();
            ImGui::PopStyleVar();

            if (statusTimer > 0.0f && !statusMessage.empty()) {
                ImGui::Text("%s", statusMessage.c_str());
            } else {
                int entityCount = static_cast<int>(pScene->getEntities().size());
                auto& cam = editorCamera;
                glm::vec3 camPos = cam.getPosition();
                ImGui::Text("Entities: %d | Camera: (%.1f, %.1f, %.1f) | %s", entityCount, camPos.x,
                            camPos.y, camPos.z,
                            cam.getMode() == EditorCamera::Mode::Orbit ? "Orbit" : "Fly");
            }

            ImGui::End();
        }

        // Open Scene dialog
        if (showOpenDialog) {
            ImGui::OpenPopup("Open Scene");
            showOpenDialog = false;
        }
        if (ImGui::BeginPopupModal("Open Scene", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("File path:");
            ImGui::SetNextItemWidth(400);
            ImGui::InputText("##openpath", dialogPathBuf, sizeof(dialogPathBuf));
            ImGui::Spacing();
            if (ImGui::Button("Open", ImVec2(120, 0))) {
                openScene(dialogPathBuf);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }

        // Export Scene as ZIP dialog
        if (showExportZipDialog) {
            ImGui::OpenPopup("Export Scene as ZIP");
            showExportZipDialog = false;
        }

        if (ImGui::BeginPopupModal("Export Scene as ZIP", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("ZIP file path:");
            ImGui::SetNextItemWidth(400);
            ImGui::InputText("##exportzip", dialogPathBuf, sizeof(dialogPathBuf));

            ImGui::Spacing();

            if (ImGui::Button("Export", ImVec2(120, 0))) {
                std::string zipPath = dialogPathBuf;

                // Collect asset paths (basic version)
                std::vector<std::string> assets;
                for (auto& entity : pScene->getEntities()) {
                    auto mrcs = entity.getComponents<MeshRendererComponent>();
                    for (auto* mrc : mrcs) {
                        if (!mrc->getModelPath().empty()) {
                            assets.push_back(mrc->getModelPath());
                        }
                    }
                }

                if (saveSceneToZip(zipPath, pScene->getCurrentFilePath(), assets)) {
                    setStatusMessage("Exported scene to ZIP");
                    ImGui::CloseCurrentPopup();
                }
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }
        // Import Scene from ZIP dialog
        if (showImportZipDialog) {
            ImGui::OpenPopup("Import Scene from ZIP");
            showImportZipDialog = false;
        }

        if (ImGui::BeginPopupModal("Import Scene from ZIP", nullptr,
                                   ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("ZIP file path:");
            ImGui::SetNextItemWidth(400);
            ImGui::InputText("##importzip", dialogPathBuf, sizeof(dialogPathBuf));

            ImGui::Spacing();

            if (ImGui::Button("Import", ImVec2(120, 0))) {
                loadSceneFromZip(dialogPathBuf);
                setStatusMessage("Imported scene from ZIP");
                ImGui::CloseCurrentPopup();
            }

            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }

            ImGui::EndPopup();
        }

        // Save Scene As dialog
        if (showSaveAsDialog) {
            ImGui::OpenPopup("Save Scene As");
            showSaveAsDialog = false;
        }
        if (ImGui::BeginPopupModal("Save Scene As", nullptr, ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::Text("File path (.gltf or .glb):");
            ImGui::SetNextItemWidth(400);
            ImGui::InputText("##savepath", dialogPathBuf, sizeof(dialogPathBuf));
            ImGui::Spacing();
            if (ImGui::Button("Save", ImVec2(120, 0))) {
                saveSceneAs(dialogPathBuf);
                ImGui::CloseCurrentPopup();
            }
            ImGui::SameLine();
            if (ImGui::Button("Cancel", ImVec2(120, 0))) {
                ImGui::CloseCurrentPopup();
            }
            ImGui::EndPopup();
        }
    }

    void EditorApp::setupDefaultDockLayout(ImGuiID dockspaceId) {
        ImGui::DockBuilderRemoveNode(dockspaceId);
        ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);

        ImGuiViewport* viewport = ImGui::GetMainViewport();
        ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);

        ImGuiID dockLeft, dockCenter;
        ImGui::DockBuilderSplitNode(dockspaceId, ImGuiDir_Left, 0.20f, &dockLeft, &dockCenter);

        ImGuiID dockRight, dockCenterRemaining;
        ImGui::DockBuilderSplitNode(dockCenter, ImGuiDir_Right, 0.25f, &dockRight,
                                    &dockCenterRemaining);

        ImGuiID dockBottom, dockViewport;
        ImGui::DockBuilderSplitNode(dockCenterRemaining, ImGuiDir_Down, 0.28f, &dockBottom,
                                    &dockViewport);

        ImGui::DockBuilderDockWindow("Hierarchy", dockLeft);
        ImGui::DockBuilderDockWindow("Inspector", dockRight);
        ImGui::DockBuilderDockWindow("Asset Browser", dockBottom);
        ImGui::DockBuilderDockWindow("Viewport", dockViewport);

        ImGui::DockBuilderFinish(dockspaceId);
    }

    void EditorApp::createEmptyEntity() {
        scrap::Entity e("Empty Entity");
        e.addComponent<TransformComponent>();
        pScene->addEntity(std::move(e));

        selectionManager.select(int(pScene->getEntities().size()) - 1);
        setStatusMessage("Created Empty Entity");
    }

    void EditorApp::createBoxEntity() {
        size_t before = pScene->getEntities().size();
        pScene->loadGLTFModel("assets/models/Cube.gltf", true);
        size_t after = pScene->getEntities().size();

        if (after > before) {
            selectionManager.select(int(after - 1));
            setStatusMessage("Created Box");
        }
    }

    void EditorApp::createBallEntity() {
        size_t before = pScene->getEntities().size();
        pScene->loadGLTFModel("assets/models/sphere.gltf", true);

        size_t after = pScene->getEntities().size();

        if (after > before) {
            selectionManager.select(int(after - 1));
            setStatusMessage("Created Ball");
        }
    }

    bool EditorApp::zipFolder(const std::filesystem::path& src, const std::filesystem::path& dst) {
        if (!std::filesystem::exists(src)) {
            setStatusMessage("Cannot zip folder: source path does not exist.");
            return false;
        }

        miniz_cpp::zip_file zip;

        for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
            if (!entry.is_regular_file())
                continue;

            std::filesystem::path rel = std::filesystem::relative(entry.path(), src);

            std::string data = loadFileToString(entry.path().string());
            if (!data.empty()) {
                zip.writestr(rel.string(), data);
            }
        }

        zip.save(dst.string());
        return true;
    }

    std::string EditorApp::loadFileToString(const std::string& path) {
        std::ifstream file(path, std::ios::binary);
        if (!file) {
            return {};
        }

        return std::string(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());
    }

    bool EditorApp::saveSceneToZip(const std::string& zipPath, const std::string& scenePath,
                                   const std::vector<std::string>& assetPaths) {
        if (zipPath.empty()) {
            setStatusMessage("Cannot export: no output path specified.");
            return false;
        }

        // If the scene has been saved to disk, use that file directly.
        // Otherwise, save to a temp file so we can still export.
        std::string sceneFile = scenePath;
        bool usedTmp = false;

        if (sceneFile.empty() || !std::filesystem::exists(sceneFile)) {
            auto tmpDir = std::filesystem::temp_directory_path() / "scrap_export";
            std::filesystem::create_directories(tmpDir);
            sceneFile = (tmpDir / "scene.gltf").string();

            if (!pScene->saveToFile(sceneFile)) {
                setStatusMessage("Cannot export: failed to save scene to temp file.");
                return false;
            }
            usedTmp = true;
        }

        miniz_cpp::zip_file zip;

        std::string sceneData = loadFileToString(sceneFile);
        zip.writestr("scene.gltf", sceneData);

        for (const auto& asset : assetPaths) {
            std::string data = loadFileToString(asset);
            if (!data.empty()) {
                zip.writestr(asset, data);
            }
        }

        zip.save(zipPath);

        if (usedTmp) {
            std::filesystem::remove_all(std::filesystem::temp_directory_path() / "scrap_export");
        }

        return true;
    }

    void EditorApp::loadSceneFromZip(const std::string& zipPath) {
        miniz_cpp::zip_file zip;
        zip.load(zipPath);

        std::string extractDir = "temp_scene_extract";
        std::filesystem::create_directories(extractDir);

        zip.extractall(extractDir);

        openScene(extractDir + "/scene.gltf");
    }

    bool EditorApp::unzipToFolder(const std::filesystem::path& zipPath,
                                  const std::filesystem::path& outDir) {
        miniz_cpp::zip_file zip;
        zip.load(zipPath.string());
        zip.extractall(outDir.string());
        return true;
    }

    void EditorApp::applySettings(const scrap::EditorSettings& s) {
        ImGui::GetIO().FontGlobalScale = s.imguiScale;

        scrap::Log::setPalantirMode(s.palantirMode);

        editorCamera.setMouseSensitivity(s.mouseSensitivity);
        editorCamera.setFlySpeed(s.cameraSpeed);
        editorCamera.setFOV(s.fieldOfView);

        if (!s.workingDirectory.empty() && s.workingDirectory != lastWorkingDirectory) {
            std::error_code ec;
            std::filesystem::current_path(s.workingDirectory, ec);
            if (ec) {
                SCRAP_LOG("Settings", "Failed to set working directory to '{}': {}",
                          s.workingDirectory, ec.message());
            } else {
                lastWorkingDirectory = s.workingDirectory;
                SCRAP_LOG_VERBOSE("Settings", "Working directory set to '{}'",
                                  std::filesystem::current_path().string());
            }
        }

        SCRAP_LOG_VERBOSE(
            "Settings",
            "Settings applied (scale={:.2f}, sensitivity={:.2f}, speed={:.1f}, fov={:.0f})",
            s.imguiScale, s.mouseSensitivity, s.cameraSpeed, s.fieldOfView);
    }

    void EditorApp::startPlayMode() {
        if (playModeActive || !pScene)
            return;

        namespace fs = std::filesystem;
        fs::path tempDir = fs::temp_directory_path() / "scrapengine_play";
        std::error_code ec;
        fs::create_directories(tempDir, ec);
        if (ec) {
            setStatusMessage("Play: Failed to create temp directory");
            SCRAP_LOG("Play", "Failed to create temp dir: {}", ec.message());
            return;
        }

        playModeTempFile = (tempDir / "play_scene.glb").string();

        const std::string originalPath = pScene->getCurrentFilePath();
        if (!pScene->saveToFile(playModeTempFile)) {
            setStatusMessage("Play: Failed to save scene");
            SCRAP_LOG("Play", "Failed to save scene to temp file");
            return;
        }
        pScene->setCurrentFilePath(originalPath);

        const fs::path engineExe = getEngineExecutablePath();
        if (engineExe.empty() || !fs::exists(engineExe)) {
            setStatusMessage("Play: Could not locate ScrapEngine executable");
            SCRAP_LOG("Play", "Could not locate ScrapEngine executable");
            return;
        }

        SCRAP_LOG("Play", "Starting play mode: {} {}", engineExe.string(), playModeTempFile);

        pid_t pid = fork();
        if (pid == 0) {
            execl(engineExe.c_str(), engineExe.c_str(), playModeTempFile.c_str(), nullptr);
            _exit(1);
        } else if (pid > 0) {
            playProcessPid = pid;
            playModeActive = true;
            setStatusMessage("Play mode started");
            SCRAP_LOG("Play", "ScrapEngine launched with PID {}", pid);
        } else {
            setStatusMessage("Play: Failed to launch engine");
            SCRAP_LOG("Play", "fork() failed");
        }
    }

    void EditorApp::stopPlayMode() {
        if (!playModeActive)
            return;

        if (playProcessPid > 0) {
            SCRAP_LOG("Play", "Stopping ScrapEngine (PID {})", playProcessPid);
            kill(playProcessPid, SIGTERM);

            int tries = 0;
            pid_t result;
            do {
                result = waitpid(playProcessPid, nullptr, WNOHANG);
                if (result == 0)
                    usleep(100000);
            } while (result == 0 && ++tries < 50);

            if (result == 0) {
                kill(playProcessPid, SIGKILL);
                waitpid(playProcessPid, nullptr, 0);
            }

            playProcessPid = -1;
        }

        if (!playModeTempFile.empty()) {
            std::error_code ec;
            std::filesystem::remove(playModeTempFile, ec);
            playModeTempFile.clear();
        }

        playModeActive = false;
        setStatusMessage("Play mode stopped");
    }

} // namespace scrap::editor
