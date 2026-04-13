#pragma once

#include <chrono>
#include <format>
#include <fstream>
#include <mutex>
#include <string>
#include <utility>

namespace scrap {

    class Log {
      public:
        static void init(const std::string& filepath = "scrapengine.log");
        static void shutdown();

        static void setPalantirMode(bool enabled);
        static bool isPalantirMode();

        template <typename... Args>
        static void info(const std::string& category, std::format_string<Args...> fmt,
                         Args&&... args) {
            write(category, std::format(fmt, std::forward<Args>(args)...));
        }

        template <typename... Args>
        static void verbose(const std::string& category, std::format_string<Args...> fmt,
                            Args&&... args) {
            if (!palantirMode)
                return;
            write(category, std::format(fmt, std::forward<Args>(args)...));
        }

      private:
        static void write(const std::string& category, const std::string& message);

        static std::ofstream logFile;
        static std::mutex logMutex;
        static bool palantirMode;
        static bool initialized;
    };

} // namespace scrap

#ifdef NDEBUG
#define SCRAP_LOG(category, ...)
#define SCRAP_LOG_VERBOSE(category, ...)
#else
#define SCRAP_LOG(category, ...) scrap::Log::info(category, __VA_ARGS__)
#define SCRAP_LOG_VERBOSE(category, ...) scrap::Log::verbose(category, __VA_ARGS__)
#endif
