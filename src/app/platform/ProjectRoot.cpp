#include <app/platform/ProjectRoot.hpp>

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#endif

#include <filesystem>
#include <string>

namespace scrap::platform {

    std::filesystem::path executablePath() {
#if defined(__APPLE__)
        uint32_t size = 0;
        _NSGetExecutablePath(nullptr, &size);
        if (size == 0) {
            return {};
        }
        std::string buffer(size, '\0');
        if (_NSGetExecutablePath(buffer.data(), &size) != 0) {
            return {};
        }
        std::error_code ec;
        return std::filesystem::weakly_canonical(std::filesystem::path(buffer.c_str()), ec);
#else
        return {};
#endif
    }

    std::filesystem::path findProjectRootNearExecutable() {
        const auto exe = executablePath();
        if (exe.empty()) {
            return {};
        }
        std::filesystem::path dir = exe.parent_path();
        for (int i = 0; i < 20; ++i) {
            if (dir.empty()) {
                break;
            }
            std::error_code ec;
            const auto testSceneDir = dir / "testScene";
            const auto assetsDir = dir / "assets";
            if (std::filesystem::is_directory(testSceneDir, ec) &&
                std::filesystem::is_directory(assetsDir, ec)) {
                std::error_code ec2;
                return std::filesystem::weakly_canonical(dir, ec2);
            }
            const auto parent = dir.parent_path();
            if (parent == dir) {
                break;
            }
            dir = parent;
        }
        return {};
    }

} // namespace scrap::platform
