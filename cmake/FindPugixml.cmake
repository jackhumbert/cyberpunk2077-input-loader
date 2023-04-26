set(CMAKE_INSTALL_PREFIX ${PROJECT_BINARY_DIR}/install)
add_subdirectory(${MOD_SOURCE_DIR}/deps/pugixml)
target_compile_definitions(pugixml-static PUBLIC
  _ITERATOR_DEBUG_LEVEL=0
)