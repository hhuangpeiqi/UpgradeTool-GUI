if(NOT DEFINED VENDOR)
  set(VENDOR rockchip)
endif()
set(extra_args)
if(DEFINED FIRMWARE)
  list(APPEND extra_args --firmware "${FIRMWARE}")
endif()
file(MAKE_DIRECTORY "${OUTPUT_DIR}")
execute_process(
  COMMAND "${CMAKE_COMMAND}" -E env
    "QT_QPA_PLATFORM=offscreen" "QT_QUICK_BACKEND=software"
    "XDG_CONFIG_HOME=${OUTPUT_DIR}/config" "XDG_STATE_HOME=${OUTPUT_DIR}/state"
    "${APP}" --vendor "${VENDOR}" --tab "${TAB}" ${extra_args} --screenshot "${OUTPUT_DIR}/page.png"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE errors TIMEOUT 15)
if(NOT result EQUAL 0)
  message(FATAL_ERROR "GUI exited with ${result}: ${output}${errors}")
endif()
if(errors MATCHES "ReferenceError|TypeError|is not a type|failed to load")
  message(FATAL_ERROR "QML error: ${errors}")
endif()
if(NOT EXISTS "${OUTPUT_DIR}/page.png")
  message(FATAL_ERROR "GUI did not produce a screenshot")
endif()
file(SIZE "${OUTPUT_DIR}/page.png" size)
if(size LESS 1000)
  message(FATAL_ERROR "GUI screenshot is empty or invalid")
endif()
