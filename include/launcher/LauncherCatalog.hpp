#pragma once

#include <filesystem>
#include <string>
#include <vector>

struct AppOptions;

namespace scrap::launcher {

    struct AssetEntry {
        std::string label;
        std::string description;
        std::string path;
        std::string group;
        bool builtin = false;
        bool authoredScene = false;
    };

    struct AssetCatalog {
        std::vector<AssetEntry> launchTargets;
        std::vector<AssetEntry> iblMaps;
    };

    AssetCatalog discoverAssetCatalog(
        const std::filesystem::path& rootPath = std::filesystem::current_path());
    std::string buildCommandLinePreview(const AppOptions& options,
                                        const std::string& executableName = "./ScrapEngine");

} // namespace scrap::launcher
