foreach(vendor rockchip allwinner qualcomm)
  if(vendor STREQUAL "rockchip")
    set(prefix test_gui_page)
  else()
    set(prefix test_${vendor}_page)
  endif()
  foreach(tab RANGE 0 2)
    set(firmware_argument)
    if(vendor STREQUAL "allwinner")
      set(firmware_argument "-DFIRMWARE=${CMAKE_BINARY_DIR}/allwinner-fixtures/normal.img")
    endif()
    add_test(NAME ${prefix}_${tab} COMMAND ${CMAKE_COMMAND}
      -DAPP=$<TARGET_FILE:upgrade_tool_gui> -DTAB=${tab} -DVENDOR=${vendor}
      -DQPA_PLATFORM=${UPGRADE_TOOL_TEST_QPA_PLATFORM}
      ${firmware_argument} -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/${vendor}-${tab}
      -P ${CMAKE_CURRENT_SOURCE_DIR}/gui_smoke.cmake)
    set_tests_properties(${prefix}_${tab} PROPERTIES TIMEOUT 30 LABELS "gui;smoke;${vendor}"
      ENVIRONMENT "${test_gui_environment}")
    if(vendor STREQUAL "allwinner")
      set_tests_properties(${prefix}_${tab} PROPERTIES FIXTURES_REQUIRED allwinner_firmware)
    endif()
  endforeach()
endforeach()
