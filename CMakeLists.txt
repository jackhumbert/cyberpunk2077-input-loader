cmake_minimum_required(VERSION 3.24)
# third-party submodules (pugixml, detours, spdlog) still declare <3.5 minimums; CMake 4 refuses those without this
set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/cyberpunk_cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/red4ext.sdk/cmake")

include(ConfigureVersionFromGit)
configure_version_from_git()

project(input_loader VERSION ${GIT_VERSION_MAJOR}.${GIT_VERSION_MINOR}.${GIT_VERSION_PATCH})

include(CyberpunkMod)

configure_mod(
  NAME "Input Loader"
  AUTHOR "Jack Humbert"
  URL "https://github.com/jackhumbert/cyberpunk2077-input-loader"
  LICENSE "Licensed under the MIT license. See the license.md in the root project for details."
)

# find_package(Detours)
find_package(Spdlog)
find_package(Pugixml)

configure_red4ext(src)

target_include_directories(${MOD_SLUG}.dll PUBLIC ${MOD_SOURCE_DIR}/include)

target_link_libraries(${MOD_SLUG}.dll
  PUBLIC
    # Detours
    spdlog
    pugixml
    bcrypt
) 

target_compile_definitions(${MOD_SLUG}.dll PRIVATE DLLDIR_EX)

configure_mod_file(input_loader.ini engine/config/platform/pc/input_loader.ini)
configure_folder_file(readme.md)
configure_folder_file(license.md)
# configure_uninstall()
configure_release(${MOD_SLUG}_${MOD_VERSION_STR}.zip)
configure_install()