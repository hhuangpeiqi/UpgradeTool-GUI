if(NOT DEFINED APP OR NOT DEFINED DESTINATION)
  message(FATAL_ERROR "APP and DESTINATION are required")
endif()
file(GET_RUNTIME_DEPENDENCIES
  EXECUTABLES "${APP}"
  DIRECTORIES "${DESTINATION}" ${SEARCH_DIRS}
  RESOLVED_DEPENDENCIES_VAR dependencies
  UNRESOLVED_DEPENDENCIES_VAR unresolved
  PRE_EXCLUDE_REGEXES "[Aa][Pp][Ii]-[Mm][Ss]-.*" "[Ee][Xx][Tt]-[Mm][Ss]-.*"
  POST_EXCLUDE_REGEXES "[Ww][Ii][Nn][Dd][Oo][Ww][Ss]/[Ss][Yy][Ss][Tt][Ee][Mm]32/.*")
if(unresolved)
  message(FATAL_ERROR "Missing runtime dependencies: ${unresolved}")
endif()
foreach(dependency IN LISTS dependencies)
  get_filename_component(directory "${dependency}" DIRECTORY)
  if(NOT directory STREQUAL DESTINATION)
    file(COPY "${dependency}" DESTINATION "${DESTINATION}")
  endif()
endforeach()
