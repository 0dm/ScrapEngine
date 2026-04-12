cmake_minimum_required(VERSION 3.30)

project(SauceEngine LANGUAGES CXX OBJCXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(SAUCE_IOS FALSE)
if(CMAKE_SYSTEM_NAME STREQUAL "iOS")
  set(SAUCE_IOS TRUE)
endif()

set(SAUCE_MACOS FALSE)
if(APPLE AND NOT SAUCE_IOS)
  set(SAUCE_MACOS TRUE)
endif()

if(NOT APPLE)
  message(FATAL_ERROR "SauceEngine platform support is currently implemented for Apple targets only (macOS and iOS).")
endif()

enable_testing()

if(WIN32)
  set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON)
  set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
  set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/lib)
  set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${PROJECT_BINARY_DIR}/bin)
endif()

function(configure_macos_app_icon target icon_path bundle_name bundle_identifier)
  if(NOT SAUCE_MACOS OR NOT EXISTS "${icon_path}")
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

function(configure_windows_app_icon target icon_path)
  if(NOT WIN32 OR NOT EXISTS "${icon_path}")
    return()
  endif()

  set(resource_file "${CMAKE_CURRENT_BINARY_DIR}/${target}_icon.rc")
  set(APP_ICON_PATH "${icon_path}")
  configure_file("${PROJECT_SOURCE_DIR}/cmake/AppIcon.rc.in" "${resource_file}" @ONLY)
  target_sources(${target} PRIVATE "${resource_file}")
endfunction()

function(configure_macos_cli_symlink target cli_path)
  if(NOT SAUCE_MACOS)
    return()
  endif()

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E rm -f "${cli_path}"
    COMMAND ${CMAKE_COMMAND} -E create_symlink "$<TARGET_FILE:${target}>" "${cli_path}"
    VERBATIM
  )
endfunction()

function(sauce_detect_vulkan_sdk)
  if(DEFINED SAUCE_VULKAN_SDK AND EXISTS "${SAUCE_VULKAN_SDK}")
    set(_detected_sdk "${SAUCE_VULKAN_SDK}")
  elseif(DEFINED ENV{VULKAN_SDK} AND EXISTS "$ENV{VULKAN_SDK}")
    set(_detected_sdk "$ENV{VULKAN_SDK}")
  else()
    file(GLOB _sdk_candidates LIST_DIRECTORIES TRUE "$ENV{HOME}/VulkanSDK/*")
    list(SORT _sdk_candidates COMPARE NATURAL ORDER DESCENDING)
    foreach(_candidate IN LISTS _sdk_candidates)
      if(EXISTS "${_candidate}/macOS/include/vulkan/vulkan.h")
        set(_detected_sdk "${_candidate}")
        break()
      endif()
    endforeach()
  endif()

  if(DEFINED _detected_sdk)
    set(SAUCE_VULKAN_SDK "${_detected_sdk}" CACHE PATH "Path to the Vulkan SDK root")
  endif()
endfunction()

function(sauce_configure_target target)
  target_include_directories(${target} PRIVATE
    ${PROJECT_SOURCE_DIR}/include
    ${TINYGLTF_INCLUDE_DIRS}
  )

  target_compile_definitions(${target} PRIVATE VK_USE_PLATFORM_METAL_EXT)

  if(NOT WIN32)
    target_compile_options(${target} PRIVATE ${SAUCE_WARNINGS})
  endif()

  if(APPLE)
    target_link_options(${target} PRIVATE "LINKER:-no_warn_duplicate_libraries")
  endif()
endfunction()

function(sauce_link_platform_frameworks target)
  if(SAUCE_IOS)
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

function(sauce_copy_shaders target)
  add_dependencies(${target} shaders)

  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${PROJECT_SOURCE_DIR}/shaders $<TARGET_FILE_DIR:${target}>/shaders
  )
endfunction()

function(sauce_copy_runtime_assets target)
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_directory
    ${PROJECT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:${target}>/assets
  )
endfunction()

file(GLOB_RECURSE APP_SOURCES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/app/*.cpp"
)
list(FILTER APP_SOURCES EXCLUDE REGEX "/SauceEngineApp\\.cpp$")
list(FILTER APP_SOURCES EXCLUDE REGEX "/ImGuiRenderer\\.cpp$")

file(GLOB_RECURSE PHYSICS_SOURCES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/physics/*.cpp"
)

file(GLOB_RECURSE MODELING_SOURCES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/app/modeling/*.cpp"
)

file(GLOB_RECURSE EDITOR_SOURCES CONFIGURE_DEPENDS
  "${PROJECT_SOURCE_DIR}/src/editor/*.cpp"
)

set(SAUCE_WARNINGS -Wall -Wpedantic -Wextra -Wreorder-init-list -g -O2)

sauce_detect_vulkan_sdk()

if(DEFINED VCPKG_INSTALLED_DIR)
  if(DEFINED VCPKG_TARGET_TRIPLET)
    set(_sauce_vcpkg_triplet_root "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}")
  elseif(SAUCE_IOS)
    set(_sauce_vcpkg_triplet_root "${VCPKG_INSTALLED_DIR}/arm64-ios")
  elseif(SAUCE_MACOS)
    set(_sauce_vcpkg_triplet_root "${VCPKG_INSTALLED_DIR}/arm64-osx")
  endif()

  if(DEFINED _sauce_vcpkg_triplet_root AND EXISTS "${_sauce_vcpkg_triplet_root}")
    list(APPEND CMAKE_PREFIX_PATH
      "${_sauce_vcpkg_triplet_root}"
      "${_sauce_vcpkg_triplet_root}/share"
    )
  endif()
endif()

if(SAUCE_VULKAN_SDK)
  if(NOT Vulkan_INCLUDE_DIR AND EXISTS "${SAUCE_VULKAN_SDK}/macOS/include/vulkan/vulkan.h")
    set(Vulkan_INCLUDE_DIR "${SAUCE_VULKAN_SDK}/macOS/include" CACHE PATH "Path to Vulkan headers")
  endif()

  if(SAUCE_IOS)
    set(_default_vulkan_library "${SAUCE_VULKAN_SDK}/iOS/lib/vulkan.framework/vulkan")
  else()
    set(_default_vulkan_library "${SAUCE_VULKAN_SDK}/macOS/lib/libvulkan.dylib")
  endif()

  if(NOT Vulkan_LIBRARY AND EXISTS "${_default_vulkan_library}")
    set(Vulkan_LIBRARY "${_default_vulkan_library}" CACHE FILEPATH "Path to Vulkan loader library")
  endif()
endif()

find_package(Vulkan REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)
find_path(TINYGLTF_INCLUDE_DIRS "tiny_gltf.h")

if(SAUCE_MACOS)
  find_package(Boost REQUIRED COMPONENTS program_options)
endif()

find_library(FOUNDATION_FRAMEWORK Foundation REQUIRED)
find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
find_library(METAL_FRAMEWORK Metal REQUIRED)
if(SAUCE_IOS)
  find_library(UIKIT_FRAMEWORK UIKit REQUIRED)
else()
  find_library(APPKIT_FRAMEWORK AppKit REQUIRED)
endif()

if(NOT TINYGLTF_INCLUDE_DIRS)
  message(FATAL_ERROR "Failed to locate tinygltf headers.")
endif()

set(SAUCE_IMGUI_SOURCES
  third_party/imgui/imgui.cpp
  third_party/imgui/imgui_demo.cpp
  third_party/imgui/imgui_draw.cpp
  third_party/imgui/imgui_tables.cpp
  third_party/imgui/imgui_widgets.cpp
  third_party/imgui/backends/imgui_impl_vulkan.cpp
  third_party/imgui/misc/cpp/imgui_stdlib.cpp
)

add_library(sauce_imgui STATIC ${SAUCE_IMGUI_SOURCES})
add_library(imgui::imgui ALIAS sauce_imgui)

target_include_directories(sauce_imgui
  PUBLIC
    ${PROJECT_SOURCE_DIR}/third_party/imgui
    ${PROJECT_SOURCE_DIR}/third_party/imgui/backends
    ${PROJECT_SOURCE_DIR}/third_party/imgui/misc/cpp
)

target_link_libraries(sauce_imgui PUBLIC Vulkan::Vulkan)
sauce_configure_target(sauce_imgui)

set(SAUCE_RENDER_SURFACE_SOURCES
  src/app/RenderSurface.mm
)

set(SAUCE_MACOS_PLATFORM_SOURCES
  src/app/platform/PlatformWindow.mm
)

add_subdirectory(${PROJECT_SOURCE_DIR}/shaders)
if(SAUCE_MACOS)
  add_subdirectory(${PROJECT_SOURCE_DIR}/src/launcher)
endif()

set(EXEC_NAME SauceEngine)
if(SAUCE_IOS)
  add_executable(${EXEC_NAME} src/ios/main.mm)
else()
  add_executable(${EXEC_NAME} src/main.cpp)
endif()

target_sources(${EXEC_NAME} PRIVATE
  ${APP_SOURCES}
  src/app/SauceEngineApp.cpp
  src/app/ImGuiRenderer.cpp
  ${PHYSICS_SOURCES}
  ${SAUCE_RENDER_SURFACE_SOURCES}
)

if(SAUCE_MACOS)
  target_sources(${EXEC_NAME} PRIVATE ${SAUCE_MACOS_PLATFORM_SOURCES})
endif()

target_link_libraries(${EXEC_NAME}
  PUBLIC Vulkan::Vulkan
  PRIVATE imgui::imgui nlohmann_json::nlohmann_json
)

if(SAUCE_MACOS)
  target_link_libraries(${EXEC_NAME}
    PUBLIC Boost::program_options
    PRIVATE launcherLib
  )
endif()

sauce_configure_target(${EXEC_NAME})
sauce_link_platform_frameworks(${EXEC_NAME})

if(SAUCE_IOS)
  set_target_properties(${EXEC_NAME} PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_BUNDLE_NAME "SauceEngine"
    MACOSX_BUNDLE_GUI_IDENTIFIER "com.sauceengine.ios"
    XCODE_ATTRIBUTE_PRODUCT_BUNDLE_IDENTIFIER "com.sauceengine.ios"
    XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED "NO"
    XCODE_ATTRIBUTE_CODE_SIGNING_REQUIRED "NO"
    XCODE_ATTRIBUTE_CODE_SIGN_IDENTITY ""
    XCODE_ATTRIBUTE_TARGETED_DEVICE_FAMILY "1,2"
  )
endif()

sauce_copy_shaders(${EXEC_NAME})
sauce_copy_runtime_assets(${EXEC_NAME})

if(SAUCE_MACOS)
  configure_macos_app_icon(${EXEC_NAME}
    "${PROJECT_SOURCE_DIR}/assets/sauceengine.icns"
    "SauceEngine"
    "com.sauceengine.app"
  )
  configure_windows_app_icon(${EXEC_NAME}
    "${PROJECT_SOURCE_DIR}/assets/sauceengine.ico"
  )
  configure_macos_cli_symlink(${EXEC_NAME}
    "${PROJECT_BINARY_DIR}/SauceEngine"
  )
endif()

if(SAUCE_MACOS)
  add_executable(modeltest src/modeltest.cpp)

  target_sources(modeltest PRIVATE
    src/app/components/MeshRendererComponent.cpp
    ${MODELING_SOURCES}
  )

  target_link_libraries(modeltest PUBLIC Vulkan::Vulkan)
  sauce_configure_target(modeltest)

  add_executable(modelviewer src/modelviewer.cpp)

  target_sources(modelviewer PRIVATE
    src/app/ModelViewerRenderer.cpp
    src/app/ImGuiRenderer.cpp
    src/app/components/MeshRendererComponent.cpp
    src/app/components/RigidBodyComponent.cpp
    ${MODELING_SOURCES}
    ${SAUCE_RENDER_SURFACE_SOURCES}
    ${SAUCE_MACOS_PLATFORM_SOURCES}
  )

  target_link_libraries(modelviewer
    PUBLIC Vulkan::Vulkan
    PRIVATE imgui::imgui
  )

  sauce_configure_target(modelviewer)
  sauce_link_platform_frameworks(modelviewer)
  sauce_copy_shaders(modelviewer)
  sauce_copy_runtime_assets(modelviewer)

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

  target_link_libraries(jenga_scene_builder PUBLIC Vulkan::Vulkan)
  sauce_configure_target(jenga_scene_builder)

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

  target_link_libraries(xpbd_cloth_harness PUBLIC Vulkan::Vulkan)
  sauce_configure_target(xpbd_cloth_harness)
  add_test(NAME xpbd_cloth_harness COMMAND xpbd_cloth_harness)

  add_executable(cloth_scene_smoke src/cloth_scene_smoke.cpp)

  target_sources(cloth_scene_smoke PRIVATE ${APP_SOURCES} ${PHYSICS_SOURCES})
  target_link_libraries(cloth_scene_smoke
    PUBLIC Vulkan::Vulkan
    PRIVATE nlohmann_json::nlohmann_json
  )
  sauce_configure_target(cloth_scene_smoke)
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

  target_link_libraries(xpbd_rigid_harness PUBLIC Vulkan::Vulkan)
  sauce_configure_target(xpbd_rigid_harness)

  add_test(NAME xpbd_stack_stability COMMAND xpbd_rigid_harness --test-stack-stability
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )
  add_test(NAME xpbd_piecewise_transfer COMMAND xpbd_rigid_harness --test-piecewise-transfer
    WORKING_DIRECTORY ${PROJECT_SOURCE_DIR}
  )

  add_executable(sauceeditor ${EDITOR_SOURCES})

  target_sources(sauceeditor PRIVATE
    ${APP_SOURCES}
    src/app/ImGuiRenderer.cpp
    ${PHYSICS_SOURCES}
    ${SAUCE_RENDER_SURFACE_SOURCES}
    ${SAUCE_MACOS_PLATFORM_SOURCES}
  )

  target_link_libraries(sauceeditor
    PUBLIC Vulkan::Vulkan
    PRIVATE imgui::imgui nlohmann_json::nlohmann_json
  )

  sauce_configure_target(sauceeditor)
  sauce_link_platform_frameworks(sauceeditor)

  if(TARGET editorGridVert)
    add_dependencies(sauceeditor editorGridVert editorGridFrag editorUnlitVert editorUnlitFrag)
  endif()

  sauce_copy_shaders(sauceeditor)
  sauce_copy_runtime_assets(sauceeditor)

  configure_macos_app_icon(sauceeditor
    "${PROJECT_SOURCE_DIR}/assets/sauceeditor.icns"
    "SauceEditor"
    "com.sauceengine.editor"
  )
  configure_windows_app_icon(sauceeditor
    "${PROJECT_SOURCE_DIR}/assets/sauceeditor.ico"
  )
  configure_macos_cli_symlink(sauceeditor
    "${PROJECT_BINARY_DIR}/sauceeditor"
  )
endif()
