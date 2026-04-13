add_library(scrap_cli_options STATIC "${CMAKE_CURRENT_LIST_DIR}/optionParser.cpp")

target_include_directories(scrap_cli_options PUBLIC ${PROJECT_SOURCE_DIR}/include)

find_package(Boost REQUIRED COMPONENTS program_options)
target_link_libraries(scrap_cli_options PUBLIC Boost::program_options)

scrap_configure_target(scrap_cli_options)

add_library(scrap_launcher STATIC
  "${CMAKE_CURRENT_LIST_DIR}/AppLauncher.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/LauncherCatalog.cpp"
  "${CMAKE_CURRENT_LIST_DIR}/PolyHavenClient.cpp"
)

target_include_directories(scrap_launcher PRIVATE
  ${PROJECT_SOURCE_DIR}/include
)

find_package(CURL REQUIRED)
target_link_libraries(scrap_launcher PRIVATE CURL::libcurl)

find_package(nlohmann_json CONFIG REQUIRED)
target_link_libraries(scrap_launcher PRIVATE nlohmann_json::nlohmann_json)

target_link_libraries(scrap_launcher
  PUBLIC
    scrap_cli_options
  PRIVATE
    scrap_imgui_core
)

target_link_libraries(scrap_launcher PRIVATE Boost::program_options)

scrap_configure_target(scrap_launcher)
