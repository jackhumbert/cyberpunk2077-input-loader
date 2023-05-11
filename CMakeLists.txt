cmake_minimum_required(VERSION 3.24)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/cyberpunk_cmake")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/deps/red4ext.sdk/cmake")

project(input_loader VERSION 0.2.0)

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
) 

target_compile_definitions(${MOD_SLUG}.dll PRIVATE DLLDIR_EX)

configure_mod_file(inputUserMappings.xml red4ext/plugins/${MOD_SLUG}/inputUserMappings.xml)
configure_uninstall_file(engine/config/platform/pc/input_loader.ini)
configure_uninstall_file(r6/cache/inputContexts.xml)
configure_uninstall_file(r6/cache/inputUserMappings.xml)
configure_folder_file(readme.md)
configure_folder_file(license.md)
configure_uninstall()
configure_release(${MOD_SLUG}_${MOD_VERSION_STR}.zip)
configure_install()