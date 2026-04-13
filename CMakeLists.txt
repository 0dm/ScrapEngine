cmake_minimum_required(VERSION 3.30)

project(ScrapEngine LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SCRAP_IOS FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(SCRAP_IOS TRUE)
endif()

set(SCRAP_MACOS FALSE)
if(APPLE AND NOT SCRAP_IOS)
  set(SCRAP_MACOS TRUE)
endif()

if(NOT APPLE)
  message(FATAL_ERROR "ScrapEngine platform support is currently implemented for Apple targets only (macOS and iOS).")
endif()

enable_testing()

function(configure_macos_app_icon target icon_path bundle_name bundle_identifier)
  if(NOT SCRAP_MACOS OR NOT EXISTS "${icon_path}")
    return()
  endif()

  get_filename_component(icon_name "${icon_path}" NAME)
  set_source_files_properties("${icon_path}" PROPERTIES
    MACOSX_PACKAGE_LOCATION "Resources"
  )

  target_sources(${target} PRIVATE "${icon_path}")
  set_target_properties(${target} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "${bundle_name}"
    MACOSX_BUNDLE_GUI_IDENTIFIER "${bundle_identifier}"
    MACOSX_BUNDLE_ICON_FILE "${icon_name}"
  )
endfunction()

function(configure_macos_cli_symlink target cli_path)
  if(NOT SCRAP_MACOS)
    return()
  endif()

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f "${cli_path}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_FILE:${target}>" "${cli_path}"
    VERBATIM
  )
endfunction()

function(scrap_configure_target target)
  target_include_directories(${target} PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${TINYGLTF_INCLUDE_DIRS}
  )

  target_compile_options(${target} PRIVATE ${SCRAP_WARNINGS})

  if(APPLE)
    target_link_options(${target} PRIVATE "LINKER:-no_warn_duplicate_libraries")
  endif()
endfunction()

function(scrap_link_platform_frameworks target)
  if(SCRAP_IOS)
    target_link_libraries(${target} PRIVATE
      ${FOUNDATION_FRAMEWORK}
      ${UIKIT_FRAMEWORK}
      ${QUARTZCORE_FRAMEWORK}
      ${METAL_FRAMEWORK}
    )
  else()
    target_link_libraries(${target} PRIVATE
      ${FOUNDATION_FRAMEWORK}
      ${APPKIT_FRAMEWORK}
      ${QUARTZCORE_FRAMEWORK}
      ${METAL_FRAMEWORK}
    )
  endif()
endfunction()

function(scrap_copy_shaders target)
  add_dependencies(${target} shaders)

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${PROJECT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:${target}>/shaders
  )
endfunction()

function(scrap_copy_runtime_assets target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${target}>/assets
  )
endfunction()

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/app/*.cpp"
)
list(FILTER APP_SOURCES EXCLUDE REGEX "/ScrapEngineApp\\.cpp$")
list(FILTER APP_SOURCES EXCLUDE REGEX "/ImGuiRenderer\\.cpp$")

file(GLOB_RECURSE PHYSICS_SOURCES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/physics/*.cpp"
)

file(GLOB_RECURSE MODELING_SOURCES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/app/modeling/*.cpp"
)

set(SCRAP_WARNINGS -Wall -Wpedantic -Wextra -Wreorder-init-list -g -O2)

if(DEFINED VCPKG_INSTALLED_DIR)
  if(DEFINED VCPKG_TARGET_TRIPLET)
    set(_scrap_vcpkg_triplet_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
  elseif(SCRAP_IOS)
    set(_scrap_vcpkg_triplet_root "${VCPKG_INSTALLED_DIR}/arm64-ios")
  elseif(SCRAP_MACOS)
    set(_scrap_vcpkg_triplet_root "${VCPKG_INSTALLED_DIR}/arm64-osx")
  endif()

  if(DEFINED _scrap_vcpkg_triplet_root AND EXISTS "${_scrap_vcpkg_triplet_root}")
    list(APPEND CMAKE_PREFIX_PATH
      "${_scrap_vcpkg_triplet_root}"
      "${_scrap_vcpkg_triplet_root}/share"
    )
  endif()
endif()

find_package(nlohmann_json CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")

if(SCRAP_MACOS)
  find_package(Boost REQUIRED COMPONENTS program_options)
endif()

find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(METAL_FRAMEWORK Metal REQUIRED)
if(SCRAP_IOS)
  find_library(UIKIT_FRAMEWORK UIKit REQUIRED)
else()
  find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
endif()

if(NOT TINYGLTF_INCLUDE_DIRS)
  message(FATAL_ERROR "Failed to locate tinygltf headers.")
endif()

set(SCRAP_IMGUI_CORE_SOURCES
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_demo.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/misc/cpp/imgui_stdlib.cpp
)

add_library(scrap_imgui_core STATIC ${SCRAP_IMGUI_CORE_SOURCES})

set(_scrap_imgui_include_dirs
  ${PROJECT_SOURCE_DIR}/third_party/imgui
  ${PROJECT_SOURCE_DIR}/third_party/imgui/backends
  ${PROJECT_SOURCE_DIR}/third_party/imgui/misc/cpp
)

target_include_directories(scrap_imgui_core PUBLIC ${_scrap_imgui_include_dirs})
scrap_configure_target(scrap_imgui_core)

set(GPU_METAL_SOURCES
  src/gpu/metal/LayerPresenter.mm
  src/gpu/metal/MetalMeshUpload.mm
  src/gpu/metal/MetalIBLResources.mm
  src/gpu/metal/MetalPBRRenderer.mm
  src/gpu/metal/MetalDeviceInfo.mm
)

set(SCRAP_MACOS_PLATFORM_SOURCES
  src/app/platform/PlatformWindow.mm
)

add_subdirectory(${PROJECT_SOURCE_DIR}/shaders)
if(SCRAP_MACOS)
  add_subdirectory(${PROJECT_SOURCE_DIR}/src/launcher)
endif()

set(EXEC_NAME ScrapEngine)
if(SCRAP_IOS)
  add_executable(${EXEC_NAME} src/ios/main.mm)
else()
  add_executable(${EXEC_NAME} src/main.cpp)
endif()

target_sources(${EXEC_NAME} PRIVATE
  ${APP_SOURCES}
  src/app/ScrapEngineApp.cpp
  ${PHYSICS_SOURCES}
  ${GPU_METAL_SOURCES}
  third_party/imgui/backends/imgui_impl_metal.mm
)

if(SCRAP_MACOS)
  target_sources(${EXEC_NAME} PRIVATE ${SCRAP_MACOS_PLATFORM_SOURCES})
endif()

target_link_libraries(${EXEC_NAME}
  PRIVATE scrap_imgui_core nlohmann_json::nlohmann_json
)

if(SCRAP_MACOS)
  target_link_libraries(${EXEC_NAME}
    PUBLIC Boost::program_options
    PRIVATE scrap_launcher
  )
endif()

scrap_configure_target(${EXEC_NAME})
scrap_link_platform_frameworks(${EXEC_NAME})

if(SCRAP_IOS)
  set_target_properties(${EXEC_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "ScrapEngine"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.scrapengine.ios"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.scrapengine.ios"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )
endif()

scrap_copy_shaders(${EXEC_NAME})
scrap_copy_runtime_assets(${EXEC_NAME})

if(SCRAP_MACOS)
  set(_scrap_main_icns "${PROJECT_SOURCE_DIR}/assets/scrapengine.icns")

  configure_macos_app_icon(${EXEC_NAME}
    "${_scrap_main_icns}"
    "ScrapEngine"
    "com.scrapengine.app"
  )
  configure_macos_cli_symlink(${EXEC_NAME}
    "${PROJECT_BINARY_DIR}/ScrapEngine"
  )
endif()

if(SCRAP_MACOS)
  add_executable(modeltest src/modeltest.cpp)

  target_sources(modeltest PRIVATE
    src/app/components/MeshRendererComponent.cpp
    ${MODELING_SOURCES}
  )

  scrap_configure_target(modeltest)

  add_executable(jenga_scene_builder
    src/jenga_scene_builder.cpp
    src/app/platform/ProjectRoot.cpp
    src/app/Scene.cpp
    src/app/components/ClothComponent.cpp
    src/app/components/TransformComponent.cpp
    src/app/components/MeshRendererComponent.cpp
    src/app/components/PointLightComponent.cpp
    src/app/components/DirectionalLightComponent.cpp
    src/app/components/SpotLightComponent.cpp
    src/app/modeling/GLTFLoader.cpp
    src/app/modeling/GLTFExporter.cpp
    src/app/modeling/Model.cpp
    src/app/modeling/ModelNode.cpp
    src/app/modeling/Mesh.cpp
    src/app/modeling/Material.cpp
    src/app/modeling/Texture.cpp
    src/app/modeling/TextureCache.cpp
    src/app/modeling/Transform.cpp
    src/physics/Cloth.cpp
  )

  scrap_configure_target(jenga_scene_builder)

  add_custom_target(generate_jenga_scene
    COMMAND $<TARGET_FILE:jenga_scene_builder>
    DEPENDS jenga_scene_builder
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )

  add_executable(xpbd_cloth_harness
    src/xpbd_cloth_harness.cpp
    src/app/components/ClothComponent.cpp
    src/app/components/MeshRendererComponent.cpp
    src/app/components/TransformComponent.cpp
    src/app/modeling/Mesh.cpp
    src/app/modeling/Transform.cpp
    src/physics/Cloth.cpp
    src/physics/SphereCollider.cpp
    src/physics/XPBD.cpp
  )

  scrap_configure_target(xpbd_cloth_harness)
  add_test(NAME xpbd_cloth_harness COMMAND xpbd_cloth_harness)

  add_executable(cloth_scene_smoke src/cloth_scene_smoke.cpp)

  target_sources(cloth_scene_smoke PRIVATE
    ${APP_SOURCES}
    ${PHYSICS_SOURCES}
    src/gpu/metal/MetalDeviceInfo.mm
  )
  target_link_libraries(cloth_scene_smoke
    PRIVATE scrap_imgui_core nlohmann_json::nlohmann_json
  )
  scrap_configure_target(cloth_scene_smoke)
  scrap_link_platform_frameworks(cloth_scene_smoke)
  add_test(NAME cloth_scene_smoke COMMAND cloth_scene_smoke)

  add_executable(xpbd_rigid_harness
    src/xpbd_rigid_harness.cpp
    src/app/platform/ProjectRoot.cpp
    src/app/Scene.cpp
    src/app/PhysicsDemoSetup.cpp
    src/app/components/ClothComponent.cpp
    src/app/components/TransformComponent.cpp
    src/app/components/MeshRendererComponent.cpp
    src/app/components/RigidBodyComponent.cpp
    src/app/components/PointLightComponent.cpp
    src/app/components/DirectionalLightComponent.cpp
    src/app/components/SpotLightComponent.cpp
    src/app/modeling/GLTFLoader.cpp
    src/app/modeling/GLTFExporter.cpp
    src/app/modeling/Model.cpp
    src/app/modeling/ModelNode.cpp
    src/app/modeling/Mesh.cpp
    src/app/modeling/Material.cpp
    src/app/modeling/Texture.cpp
    src/app/modeling/TextureCache.cpp
    src/app/modeling/Transform.cpp
    src/physics/Cloth.cpp
    src/physics/SphereBVH.cpp
    src/physics/SphereCollider.cpp
    src/physics/XPBD.cpp
    src/physics/XPBDContacts.cpp
    src/physics/XPBDRigidSolve.cpp
  )

  scrap_configure_target(xpbd_rigid_harness)

  add_test(NAME xpbd_stack_stability COMMAND xpbd_rigid_harness --test-stack-stability
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
  add_test(NAME xpbd_piecewise_transfer COMMAND xpbd_rigid_harness --test-piecewise-transfer
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
endif()

find_program(SCRAP_CLANG_FORMAT clang-format)
if(SCRAP_CLANG_FORMAT)
  add_custom_target(format
    COMMAND bash "${CMAKE_CURRENT_SOURCE_DIR}/cmake/clang-format-run.bash"
    WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    COMMENT "clang-format (in-place, excludes third_party/)"
    VERBATIM
  )
else()
  add_custom_target(format
    COMMAND "${CMAKE_COMMAND}" -E echo "clang-format not found in PATH; install LLVM and add clang-format to PATH."
    COMMAND bash -c "exit 1"
    VERBATIM
  )
endif()
