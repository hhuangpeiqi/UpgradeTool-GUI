upgrade_test(test_inspections LIBRARIES upgrade_gui_backend TIMEOUT 10 LABELS gui controller rockchip allwinner)
upgrade_test(test_driver_controller LIBRARIES upgrade_gui_backend TIMEOUT 30 LABELS gui controller drivers)
upgrade_test(test_driver_recovery LIBRARIES upgrade_gui_backend TIMEOUT 10 LABELS gui controller drivers)
if(WIN32)
  upgrade_test(test_windows_driver_package SOURCES ../../platform/test_windows_driver_package.cpp
    LIBRARIES upgrade_gui_backend TIMEOUT 20 LABELS platform packaging drivers)
endif()
upgrade_test(test_file_locations LIBRARIES upgrade_gui_backend TIMEOUT 10 LABELS gui controller)
foreach(name test_shared_services test_adb_executable test_adb_runner test_adb_recovery test_edl_runner test_qualcomm_controller)
  upgrade_test(${name} LIBRARIES upgrade_gui_backend TIMEOUT 30 LABELS gui controller)
endforeach()
set_tests_properties(test_adb_recovery PROPERTIES TIMEOUT 60)
set_tests_properties(test_adb_runner PROPERTIES TIMEOUT 90)
set_property(TEST test_edl_runner test_qualcomm_controller APPEND PROPERTY LABELS qualcomm)
upgrade_test(test_edl_handoff LIBRARIES upgrade_gui_backend TIMEOUT 30
  ENVIRONMENT ${test_gui_environment} LABELS gui controller qualcomm)
upgrade_test(test_gui_backend
  SOURCES test_gui_backend.cpp test_gui_devices.cpp test_gui_tasks.cpp test_gui_allwinner.cpp test_gui_language.cpp
  # This combined suite creates many controllers; Windows startup and teardown
  # can consume the former 20-second budget even when every assertion passes.
  LIBRARIES upgrade_gui_backend TIMEOUT 60 ENVIRONMENT ${test_gui_environment}
  LABELS gui controller)
upgrade_attach_test_translations(test_gui_backend)
upgrade_test(test_application_lifecycle LIBRARIES upgrade_gui_backend TIMEOUT 30
  ENVIRONMENT ${test_gui_environment} LABELS gui controller qml)
upgrade_attach_gui_test_resources(test_application_lifecycle)
upgrade_attach_test_translations(test_application_lifecycle)

upgrade_test(test_allwinner_switch LIBRARIES upgrade_gui_backend TIMEOUT 15 LABELS gui controller allwinner)
upgrade_test(test_allwinner_handoff LIBRARIES upgrade_gui_backend TIMEOUT 30
  ENVIRONMENT ${test_gui_environment} LABELS gui controller allwinner drivers)
upgrade_test(test_switch_recovery LIBRARIES upgrade_gui_backend TIMEOUT 30
  ENVIRONMENT ${test_gui_environment} LABELS gui controller rockchip allwinner qualcomm)
