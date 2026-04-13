#pragma once

#include <editor/EditorCamera.hpp>
#include <editor/gizmos/Gizmo.hpp>
#include <editor/gizmos/RotateGizmo.hpp>
#include <editor/gizmos/ScaleGizmo.hpp>
#include <editor/gizmos/TranslateGizmo.hpp>

#include <app/modeling/Mesh.hpp>
#include <gpu/vulkan/GraphicsPipeline.hpp>
#include <gpu/vulkan/LogicalDevice.hpp>
#include <gpu/vulkan/PhysicalDevice.hpp>
#include <gpu/vulkan/Renderer.hpp>

#include <memory>

namespace scrap::editor {

    class GizmoRenderer {
      public:
        GizmoRenderer(const scrap::PhysicalDevice& physicalDevice,
                      const scrap::LogicalDevice& logicalDevice,
                      const vk::raii::DescriptorSetLayout& descriptorSetLayout,
                      vk::Format colorFormat, scrap::Renderer& renderer);

        void setActiveGizmo(GizmoType type);
        GizmoType getActiveGizmoType() const {
            return activeType;
        }
        Gizmo* getActiveGizmo() {
            return activeGizmo.get();
        }

        void render(vk::raii::CommandBuffer& cmd, const vk::raii::DescriptorSet& descriptorSet,
                    const glm::vec3& entityPosition, const EditorCamera& camera, float aspect);

        static constexpr float SCALE_FACTOR = 0.15f;

      private:
        void uploadMesh();

        const scrap::PhysicalDevice* pPhysicalDevice;
        const scrap::LogicalDevice* pLogicalDevice;
        scrap::Renderer* pRenderer;

        std::unique_ptr<scrap::GraphicsPipeline> pipeline;
        std::unique_ptr<scrap::modeling::Mesh> mesh;

        GizmoType activeType = GizmoType::Translate;
        std::unique_ptr<Gizmo> activeGizmo;

        bool meshDirty = true;
    };

} // namespace scrap::editor
