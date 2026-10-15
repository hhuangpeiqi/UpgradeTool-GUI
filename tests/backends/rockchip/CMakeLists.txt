foreach(name test_rkdisk test_erase test_firmware test_engine test_download)
  upgrade_test(${name} LIBRARIES upgrade_rockchip upgrade_platform usb1 LABELS backend rockchip)
  target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/backends/rockchip/legacy)
endforeach()
