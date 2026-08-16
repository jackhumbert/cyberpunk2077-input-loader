cmake_minimum_required(VERSION 3.5...3.31)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/cyberpunk_cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/red4ext.sdk/cmake")

project(input_loader VERSION 0.2.4 LANGUAGES CXX)

# cyberpunk_cmake uses the deprecated `add_custom_command(TARGET ... DEPENDS ...)`
# form which CMake 3.28+ rejects via policy CMP0175. The submodule is third-party
# code we don't want to fork, so set the policy to OLD to keep the legacy behavior.
# `CMAKE_POLICY_DEFAULT_CMP0175` is used because it propagates into add_subdirectory
# scopes (spdlog, pugixml, etc.) which might trigger the same policy.
if(POLICY CMP0175)
  set(CMAKE_POLICY_DEFAULT_CMP0175 OLD)
  cmake_policy(SET CMP0175 OLD)
endif()

include(CyberpunkMod)

configure_mod(
  NAME "Input Loader"
  AUTHOR "Jack Humbert"
  URL "https://github.com/jackhumbert/cyberpunk2077-input-loader"
  LICENSE "Licensed under the MIT license. See the license.md in the root project for details."
)

find_package(Spdlog)
find_package(Pugixml)

configure_red4ext(src)

target_include_directories(${MOD_SLUG}.dll PUBLIC ${MOD_SOURCE_DIR}/include)

target_link_libraries(${MOD_SLUG}.dll
  PUBLIC
    spdlog
    pugixml
)

target_compile_definitions(${MOD_SLUG}.dll PRIVATE DLLDIR_EX)

# Mod files installed into the game's directory layout
configure_mod_file(inputUserMappings.xml red4ext/plugins/${MOD_SLUG}/inputUserMappings.xml)
configure_mod_file(input_loader.ini     engine/config/platform/pc/input_loader.ini)
configure_mod_file(inputContexts.xml    r6/cache/inputContexts.xml)
configure_mod_file(inputUserMappings.xml r6/cache/inputUserMappings.xml)
configure_folder_file(readme.md)
configure_folder_file(license.md)

# Produce the install directory (game_dir) + release zip
configure_release(${MOD_SLUG}_${MOD_VERSION_STR}.zip)
configure_install()
