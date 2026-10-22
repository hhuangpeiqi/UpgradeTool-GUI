upgrade_test(test_platform_io LIBRARIES upgrade_platform LABELS platform)
find_package(Python3 3.10 QUIET COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME test_prepare_adb
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_prepare_adb.py"
      "${CMAKE_COMMAND}" "${CMAKE_SOURCE_DIR}")
  set_tests_properties(test_prepare_adb PROPERTIES TIMEOUT 30 LABELS "platform;packaging")
  add_test(NAME test_prepare_qualcomm_driver
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/test_prepare_qualcomm_driver.py"
      "${CMAKE_COMMAND}" "${CMAKE_SOURCE_DIR}")
  set_tests_properties(test_prepare_qualcomm_driver PROPERTIES TIMEOUT 30 LABELS "platform;packaging;drivers")
endif()
if(WIN32)
  target_link_libraries(test_platform_io PRIVATE shell32)
endif()
if(UNIX)
  upgrade_test(test_platform_path LIBRARIES upgrade_platform LABELS platform)
endif()
