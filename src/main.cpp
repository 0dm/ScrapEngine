#include <iostream>
#include <chrono>
#include <filesystem>
#include <functional>
#include <vector>

#include <launcher/optionParser.hpp>
#include <app/Log.hpp>
#include <app/SauceEngineApp.hpp>
#include <app/platform/PlatformWindow.hpp>
#include <app/ui/ImGuiComponentManager.hpp>
#include <app/ui/components/Button.hpp>
#include <app/ui/components/CustomTooltip.hpp>
#include <app/ui/components/PlotHistogram.hpp>
#include <app/ui/components/PlotLines.hpp>
#include <app/ui/components/ProgressBar.hpp>
#include <app/ui/components/Tooltip.hpp>

namespace {

/// Resolve relative paths against the shell's current directory before any code (e.g. macOS
/// createPlatformWindow) changes the process working directory to the bundle's MacOS folder.
std::string anchorUserFilePath(const std::string& path) {
  if (path.empty()) {
    return path;
  }
  namespace fs = std::filesystem;
  const fs::path p(path);
  if (p.is_absolute()) {
    return p.lexically_normal().string();
  }
  return fs::absolute(p).lexically_normal().string();
}

} // namespace

int main(int argc, const char *argv[]) {
  sauce::Log::init();
  const AppOptions ops(argc, argv);

  if (ops.help) {
    std::cout << "Usage: " << argv[0] << " <options> [scene_file]" << std::endl;
    std::cout << ops.getHelpMessage() << std::endl;
    sauce::Log::shutdown();
    return 1;
  }

  const std::string scenePath = anchorUserFilePath(ops.scene_file);
  const std::string iblPath = anchorUserFilePath(ops.ibl_file);

  sauce::SauceEngineApp mainApp;
  auto window = sauce::platform::createPlatformWindow({
      .title = "SauceEngine",
      .width = ops.scr_width,
      .height = ops.scr_height,
      .resizable = true,
      .acceptFileDrops = false,
  });
  try {
    if (!scenePath.empty()) {
      mainApp.setSceneFile(scenePath);
    }
    mainApp.setCameraCollisionEnabled(ops.camera_collide);
    if (!iblPath.empty()) {
      mainApp.setIBLFile(iblPath);
    }
    mainApp.setPhysicsTickRate(ops.tickrate);
    mainApp.initialize(*window, ops.scr_width, ops.scr_height);

    auto lastFrameTime = std::chrono::steady_clock::now();
    while (!window->shouldClose()) {
      window->pumpEvents();
      const auto currentFrameTime = std::chrono::steady_clock::now();
      const float deltaTime =
          std::chrono::duration<float>(currentFrameTime - lastFrameTime).count();
      lastFrameTime = currentFrameTime;
      mainApp.tick(deltaTime);
    }
    mainApp.shutdown();
  } catch (std::exception& e) {
    mainApp.shutdown();
    sauce::Log::shutdown();
    std::cerr << e.what() << std::endl;
    return EXIT_FAILURE;
  }
  sauce::Log::shutdown();
  return EXIT_SUCCESS;
}
