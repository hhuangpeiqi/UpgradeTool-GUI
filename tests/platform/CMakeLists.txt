upgrade_test(test_platform_io LIBRARIES upgrade_platform LABELS platform)
if(UNIX)
  upgrade_test(test_platform_path LIBRARIES upgrade_platform LABELS platform)
endif()
