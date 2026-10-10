cmake_minimum_required(VERSION 3.21.1)
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()
project(UpgradeTool-GUI C CXX)

# Keep runnable targets at the build root; multi-config generators append the configuration.
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

file(STRINGS "${CMAKE_SOURCE_DIR}/core/Version.h" UPGRADE_TOOL_VERSION_LINE
     REGEX "TOOL_VERSION[ \t]*=")
string(REGEX MATCH "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _upgrade_tool_ver_match
       "${UPGRADE_TOOL_VERSION_LINE}")
set(UPGRADE_TOOL_VERSION "${CMAKE_MATCH_1}")
if(NOT UPGRADE_TOOL_VERSION)
  message(FATAL_ERROR "Could not parse TOOL_VERSION from core/Version.h")
endif()
unset(UPGRADE_TOOL_VERSION_LINE)
unset(_upgrade_tool_ver_match)

add_compile_options(-Wall -fno-strict-aliasing -D_FILE_OFFSET_BITS=64 -D_LARGE_FILE)

set(LIBUSB_DIR ${CMAKE_SOURCE_DIR}/third_party/libusb)
set(LIBUSB_SOURCES
  ${LIBUSB_DIR}/libusb/core.c
  ${LIBUSB_DIR}/libusb/descriptor.c
  ${LIBUSB_DIR}/libusb/hotplug.c
  ${LIBUSB_DIR}/libusb/io.c
  ${LIBUSB_DIR}/libusb/strerror.c
  ${LIBUSB_DIR}/libusb/sync.c
)
if(WIN32)
  list(APPEND LIBUSB_SOURCES
    ${LIBUSB_DIR}/libusb/os/windows_common.c
    ${LIBUSB_DIR}/libusb/os/windows_winusb.c
    ${LIBUSB_DIR}/libusb/os/windows_usbdk.c
    ${LIBUSB_DIR}/libusb/os/events_windows.c
    ${LIBUSB_DIR}/libusb/os/threads_windows.c
  )
elseif(APPLE)
  list(APPEND LIBUSB_SOURCES
    ${LIBUSB_DIR}/libusb/os/darwin_usb.c
    ${LIBUSB_DIR}/libusb/os/events_posix.c
    ${LIBUSB_DIR}/libusb/os/threads_posix.c
  )
else()
  list(APPEND LIBUSB_SOURCES
    ${LIBUSB_DIR}/libusb/os/linux_usbfs.c
    ${LIBUSB_DIR}/libusb/os/linux_netlink.c
    ${LIBUSB_DIR}/libusb/os/events_posix.c
    ${LIBUSB_DIR}/libusb/os/threads_posix.c
  )
endif()

set(LIBUSB_GEN_DIR ${CMAKE_BINARY_DIR}/libusb-gen)
add_custom_command(
  OUTPUT ${LIBUSB_GEN_DIR}/version_describe.h
  COMMAND ${CMAKE_COMMAND} -E make_directory ${LIBUSB_GEN_DIR}
  COMMAND sh ${LIBUSB_DIR}/build-aux/gen-describe.sh
          ${LIBUSB_DIR} ${LIBUSB_GEN_DIR}/version_describe.h
  DEPENDS ${LIBUSB_DIR}/build-aux/gen-describe.sh
  COMMENT "Generating libusb version_describe.h"
  VERBATIM)
add_custom_target(libusb_version_describe
  DEPENDS ${LIBUSB_GEN_DIR}/version_describe.h)

add_library(usb1 STATIC ${LIBUSB_SOURCES})
add_dependencies(usb1 libusb_version_describe)
target_include_directories(usb1 PUBLIC ${LIBUSB_DIR}/libusb)
target_include_directories(usb1 PRIVATE ${LIBUSB_GEN_DIR})
if(WIN32)
  target_include_directories(usb1 PRIVATE ${CMAKE_SOURCE_DIR}/compat/windows)
  target_compile_definitions(usb1 PRIVATE _WIN32_WINNT=0x0600 _CRT_SECURE_NO_WARNINGS)
  target_link_libraries(usb1 PUBLIC setupapi user32 advapi32 ole32)
elseif(APPLE)
  target_include_directories(usb1 PRIVATE ${CMAKE_SOURCE_DIR}/compat/macos)
  find_library(IOKIT_FRAMEWORK IOKit REQUIRED)
  find_library(COREFOUNDATION_FRAMEWORK CoreFoundation REQUIRED)
  find_library(SECURITY_FRAMEWORK Security REQUIRED)
  target_link_libraries(usb1 PUBLIC
    ${IOKIT_FRAMEWORK} ${COREFOUNDATION_FRAMEWORK} ${SECURITY_FRAMEWORK} objc)
else()
  target_include_directories(usb1 PRIVATE ${CMAKE_SOURCE_DIR}/compat)
  target_compile_definitions(usb1 PRIVATE _GNU_SOURCE=1)
endif()
find_package(Threads REQUIRED)
target_link_libraries(usb1 PUBLIC Threads::Threads)

add_library(efex STATIC
  third_party/libefex/src/efex-common.c
  third_party/libefex/src/efex-fel.c
  third_party/libefex/src/efex-fes.c
  third_party/libefex/src/efex-usb.c
)
set_target_properties(efex PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
target_include_directories(efex PUBLIC third_party/libefex/includes)

add_library(upgradetool STATIC
  lib/crc.cpp
  lib/RKLog.cpp
  lib/RKScan.cpp
  lib/RKComm.cpp
  lib/RKDevice.cpp
  lib/RKBoot.cpp
  lib/RKImage.cpp
  lib/Config.cpp
  lib/Parameter.cpp
  lib/Idb.cpp
  lib/MD5Checksum.cpp
  lib/AndroidDevice.cpp
  lib/RKDisk.cpp
  lib/DeviceFactory.cpp
  lib/Upgrade.cpp
  platform/PlatformPath.cpp
  core/UpgradeEngine.cpp
  core/FirmwareOperations.cpp
  core/FirmwareService.cpp
  core/TaskRunner.cpp
  core/Backend.cpp
  core/allwinner/Firmware.cpp
  core/allwinner/Transport.cpp
  core/allwinner/Convert.cpp
  core/allwinner/AllwinnerBackend.cpp
)
target_include_directories(upgradetool PUBLIC lib ${CMAKE_SOURCE_DIR})
target_link_libraries(upgradetool PUBLIC efex usb1)

option(UPGRADE_TOOL_TESTS "Build the unit tests" OFF)
if(UPGRADE_TOOL_TESTS)
  enable_testing()
endif()

find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2)
target_link_libraries(upgradetool PUBLIC Qt6::Core)
# Qt 6.8's OpenGL wrapper still adds AGL, absent from recent Apple SDKs.
# Adjust only the imported target in this project; leave the Qt installation intact.
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
  get_target_property(_qt_opengl_links WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
  list(FILTER _qt_opengl_links EXCLUDE REGEX "AGL(\\.framework)?$")
  set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY
    INTERFACE_LINK_LIBRARIES "${_qt_opengl_links}")
endif()
add_subdirectory(gui)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  install(FILES scripts/99-rk-rockusb-all.rules scripts/99-allwinner.rules
          DESTINATION share/upgrade_tool)
  install(PROGRAMS scripts/install-udev.sh
          DESTINATION share/upgrade_tool)
endif()
install(FILES config.ini
        DESTINATION share/upgrade_tool)
install(FILES third_party/libefex/LICENSE core/allwinner/LICENSE.OpenixCLI
        DESTINATION share/doc/upgrade-tool/allwinner)
install(FILES README.md
        DESTINATION share/doc/upgrade-tool)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
      "${CMAKE_SOURCE_DIR}/scripts/deb/postinst;${CMAKE_SOURCE_DIR}/scripts/deb/postrm")

  set(CPACK_PACKAGE_NAME "upgrade-tool")
  set(CPACK_PACKAGE_VERSION "${UPGRADE_TOOL_VERSION}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "UpgradeTool-GUI (Qt6 GUI) for Rockchip and Allwinner USB firmware flashing")
  set(CPACK_PACKAGE_CONTACT "UpgradeTool-GUI maintainers")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/rockchip-linux/rkdeveloptool")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/lib/license.txt")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pkexec")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS
      "qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-templates, qml6-module-qtquick-window, qml6-module-qtqml-workerscript, qt6-qpa-plugins, qt6-svg-plugins | libqt6svg6")
  if(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
    find_program(DPKG_EXECUTABLE dpkg)
    if(DPKG_EXECUTABLE)
      execute_process(COMMAND "${DPKG_EXECUTABLE}" --print-architecture
        OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    else()
      # Match CPackDeb's default when dpkg is unavailable.
      set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE i386)
    endif()
  endif()
  set(CPACK_DEBIAN_FILE_NAME
      "UpgradeTool-GUI_${UPGRADE_TOOL_VERSION}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")
  set(CPACK_GENERATOR "DEB")
  include(CPack)
endif()

if(UPGRADE_TOOL_TESTS)
  if(UNIX)
    add_executable(test_platform_path tests/test_platform_path.cpp)
    target_link_libraries(test_platform_path PRIVATE upgradetool)
    add_test(NAME test_platform_path COMMAND test_platform_path)
  endif()

  add_executable(test_rkdisk tests/test_rkdisk.cpp)
  target_link_libraries(test_rkdisk PRIVATE upgradetool)
  add_test(NAME test_rkdisk COMMAND test_rkdisk)

  add_executable(test_erase tests/test_erase.cpp)
  target_link_libraries(test_erase PRIVATE upgradetool)
  add_test(NAME test_erase COMMAND test_erase)

  add_executable(test_firmware tests/test_firmware.cpp)
  target_link_libraries(test_firmware PRIVATE upgradetool)
  add_test(NAME test_firmware COMMAND test_firmware)

  add_executable(test_allwinner tests/test_allwinner.cpp)
  target_link_libraries(test_allwinner PRIVATE upgradetool)
  add_test(NAME test_allwinner COMMAND test_allwinner "${CMAKE_BINARY_DIR}/allwinner-fixtures")
  set_tests_properties(test_allwinner PROPERTIES FIXTURES_SETUP allwinner_firmware)
  add_executable(allwinner_device EXCLUDE_FROM_ALL tests/allwinner_device.cpp)
  target_link_libraries(allwinner_device PRIVATE upgradetool)

  add_executable(test_tasks tests/test_tasks.cpp)
  target_link_libraries(test_tasks PRIVATE upgradetool)
  add_test(NAME test_tasks COMMAND test_tasks)

  add_executable(test_engine tests/test_engine.cpp)
  target_link_libraries(test_engine PRIVATE upgradetool)
  add_test(NAME test_engine COMMAND test_engine)

  add_executable(test_download tests/test_download.cpp)
  target_link_libraries(test_download PRIVATE upgradetool)
  add_test(NAME test_download COMMAND test_download)
endif()
