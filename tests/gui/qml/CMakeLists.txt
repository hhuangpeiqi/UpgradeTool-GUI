find_package(Qt6 REQUIRED COMPONENTS Test QuickTest)
upgrade_test(test_driver_ui LIBRARIES upgrade_gui_backend Qt6::Test TIMEOUT 45
  ARGUMENTS "${CMAKE_BINARY_DIR}/driver-preview" ENVIRONMENT ${test_gui_environment} LABELS gui qml drivers)
upgrade_attach_gui_test_resources(test_driver_ui)
upgrade_attach_test_translations(test_driver_ui)
upgrade_test(test_allwinner_ui LIBRARIES upgrade_gui_backend TIMEOUT 30
  ARGUMENTS "${CMAKE_BINARY_DIR}/allwinner-fixtures/normal.img" "${CMAKE_CURRENT_BINARY_DIR}/allwinner-ui"
  FIXTURES_REQUIRED allwinner_firmware ENVIRONMENT ${test_gui_environment} LABELS gui qml allwinner)
upgrade_test(test_qualcomm_ui
  SOURCES test_qualcomm_ui.cpp test_qualcomm_devices.cpp test_qualcomm_flash.cpp test_qualcomm_tools.cpp test_qualcomm_layout.cpp
  LIBRARIES upgrade_gui_backend Qt6::Test Qt6::QuickTest TIMEOUT 30
  ENVIRONMENT ${test_gui_environment} LABELS gui qml qualcomm)
upgrade_test(test_qml_controls LIBRARIES upgrade_gui_backend Qt6::Test TIMEOUT 30
  ENVIRONMENT ${test_gui_environment} LABELS gui qml controls)
upgrade_test(test_switching_ui LIBRARIES upgrade_gui_backend Qt6::Test TIMEOUT 30
  ENVIRONMENT ${test_gui_environment} LABELS gui qml rockchip allwinner qualcomm)
upgrade_test(test_file_picker LIBRARIES upgrade_gui_backend Qt6::Test TIMEOUT 30
  ENVIRONMENT ${test_gui_environment} LABELS gui qml controls)
foreach(name test_allwinner_ui test_qualcomm_ui test_qml_controls test_switching_ui test_file_picker)
  upgrade_attach_gui_test_resources(${name})
endforeach()
upgrade_attach_test_translations(test_qualcomm_ui)
upgrade_attach_test_translations(test_switching_ui)
upgrade_test(test_maskrom_ui LIBRARIES upgrade_gui_backend Qt6::Test TIMEOUT 30
  ENVIRONMENT ${test_gui_environment} LABELS gui qml rockchip)
upgrade_attach_gui_test_resources(test_maskrom_ui)
upgrade_attach_test_translations(test_maskrom_ui)
