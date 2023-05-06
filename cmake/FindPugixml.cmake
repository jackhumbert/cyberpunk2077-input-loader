if(NOT TARGET pugixml-static)
  find_package(Git QUIET)

  if(GIT_FOUND AND EXISTS "${MOD_SOURCE_DIR}/.git")
    execute_process(
      COMMAND ${GIT_EXECUTABLE} submodule update --init deps/pugixml
      WORKING_DIRECTORY ${MOD_SOURCE_DIR}
    )
  endif()

  set(CMAKE_INSTALL_PREFIX ${PROJECT_BINARY_DIR}/install)
  add_subdirectory(${MOD_SOURCE_DIR}/deps/pugixml)
  target_compile_definitions(pugixml-static PUBLIC
    _ITERATOR_DEBUG_LEVEL=0
  )
endif()