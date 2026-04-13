#ifndef OPTION_PARSER_HPP
#define OPTION_PARSER_HPP

#include <array>
#include <string>

#include <launcher/AppOptionsDefaults.hpp>

#include <boost/program_options/options_description.hpp>
#include <boost/program_options/parsers.hpp>
#include <boost/program_options/positional_options.hpp>
#include <boost/program_options/variables_map.hpp>

struct AppOptions {
    unsigned int scr_width, scr_height;
    double tickrate;
    double model_rotate_x_degrees;
    double model_rotate_y_degrees;
    double model_rotate_z_degrees;
    std::string scene_file;
    bool camera_collide;
    std::string ibl_file;
    std::string polyhaven_model_id;
    std::string polyhaven_model_resolution;
    std::string polyhaven_hdri_id;
    std::string polyhaven_hdri_resolution;
    bool model_rotation_provided;
    bool skip_launcher;
    bool help;

    std::array<double, 3> defaultModelRotationDegrees() const {
        return {AppOptionsDefaults::DEFAULT_MODEL_ROTATE_X_DEGREES,
                AppOptionsDefaults::DEFAULT_MODEL_ROTATE_Y_DEGREES,
                AppOptionsDefaults::DEFAULT_MODEL_ROTATE_Z_DEGREES};
    }

    AppOptions(int argc, const char* argv[]);
    AppOptions()
        : scr_width(AppOptionsDefaults::DEFAULT_SCR_WIDTH),
          scr_height(AppOptionsDefaults::DEFAULT_SCR_HEIGHT),
          tickrate(AppOptionsDefaults::DEFAULT_TICKRATE),
          model_rotate_x_degrees(AppOptionsDefaults::DEFAULT_MODEL_ROTATE_X_DEGREES),
          model_rotate_y_degrees(AppOptionsDefaults::DEFAULT_MODEL_ROTATE_Y_DEGREES),
          model_rotate_z_degrees(AppOptionsDefaults::DEFAULT_MODEL_ROTATE_Z_DEGREES), scene_file(),
          camera_collide(false), ibl_file(), polyhaven_model_id(), polyhaven_model_resolution("2k"),
          polyhaven_hdri_id(), polyhaven_hdri_resolution("4k"), model_rotation_provided(false),
          skip_launcher(false), help(false), desc("Allowed options") {
    }

    boost::program_options::options_description getHelpMessage() const;

  private:
    boost::program_options::options_description desc;
};

#endif
