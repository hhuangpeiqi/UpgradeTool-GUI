upgrade_test(generate_allwinner_fixtures SOURCES Generate.cpp
  LIBRARIES upgrade_allwinner upgrade_platform
  ARGUMENTS "${CMAKE_BINARY_DIR}/allwinner-fixtures"
  FIXTURES_SETUP allwinner_firmware LABELS fixture allwinner)
