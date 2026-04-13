#pragma once

// Defaults shared by CLI parsing, launcher UI, and ScrapEngineApp. No Boost — safe for iOS.
struct AppOptionsDefaults {
    static constexpr unsigned int DEFAULT_SCR_WIDTH = 1280;
    static constexpr unsigned int DEFAULT_SCR_HEIGHT = 720;
    static constexpr double DEFAULT_TICKRATE = 128.0;
    static constexpr double DEFAULT_MODEL_ROTATE_X_DEGREES = 90.0;
    static constexpr double DEFAULT_MODEL_ROTATE_Y_DEGREES = 0.0;
    static constexpr double DEFAULT_MODEL_ROTATE_Z_DEGREES = 0.0;
    static constexpr const char* DEFAULT_MODEL_ROTATION_AXIS = "z";
};
