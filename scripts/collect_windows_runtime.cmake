if(NOT DEFINED APP OR NOT DEFINED DESTINATION)
  message(FATAL_ERROR "APP and DESTINATION are required")
endif()
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${APP}"
  DIRECTORIES "${DESTINATION}" ${SEARCH_DIRS}
  RESOLVED_DEPENDENCIES_VAR dependencies
  UNRESOLVED_DEPENDENCIES_VAR unresolved
  PRE_EXCLUDE_REGEXES "[Aa][Pp][Ii]-[Mm][Ss]-.*" "[Ee][Xx][Tt]-[Mm][Ss]-.*")
if(unresolved)
  message(FATAL_ERROR "Missing runtime dependencies: ${unresolved}")
endif()
file(TO_CMAKE_PATH "${DESTINATION}" destination_path)
foreach(dependency IN LISTS dependencies)
  # Windows dependency paths can mix backslashes and forward slashes.
  file(TO_CMAKE_PATH "${dependency}" dependency)
  if(dependency MATCHES "[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Tt][Ee][Mm]32/.*")
    continue()
  endif()
  get_filename_component(directory "${dependency}" DIRECTORY)
  if(NOT directory STREQUAL destination_path)
    file(COPY "${dependency}" DESTINATION "${DESTINATION}")
  endif()
endforeach()
