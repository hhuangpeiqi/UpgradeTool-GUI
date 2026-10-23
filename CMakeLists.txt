cmake_minimum_required(VERSION 3.21.1)
if(CMAKE_HOST_APPLE AND NOT DEFINED CMAKE_OSX_DEPLOYMENT_TARGET)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "12.0" CACHE STRING "Minimum macOS version")
endif()
project(UpgradeTool-GUI C CXX)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_EXTENSIONS OFF)
option(UPGRADE_TOOL_GUI "Build the Qt desktop application" ON)
option(UPGRADE_TOOL_ROCKCHIP "Build the Rockchip backend" ON)
option(UPGRADE_TOOL_ALLWINNER "Build the Allwinner backend" ON)
option(UPGRADE_TOOL_QUALCOMM "Build the Qualcomm backend" ON)
option(UPGRADE_TOOL_TESTS "Build the unit tests" OFF)
configure_file(application/BuildFeatures.h.in generated/application/BuildFeatures.h)
add_library(upgrade_domain INTERFACE)
target_include_directories(upgrade_domain INTERFACE ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
target_compile_features(upgrade_domain INTERFACE cxx_std_20)
if(UPGRADE_TOOL_TESTS)
  enable_testing()
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_SOURCE_DIR}/application/Version.h")
file(STRINGS "${CMAKE_SOURCE_DIR}/application/Version.h" UPGRADE_TOOL_VERSION_LINE
     REGEX "TOOL_VERSION[ \t]*=")
string(REGEX MATCH "\"([0-9]+\\.[0-9]+\\.[0-9]+)\"" _upgrade_tool_ver_match
       "${UPGRADE_TOOL_VERSION_LINE}")
set(UPGRADE_TOOL_VERSION "${CMAKE_MATCH_1}")
if(NOT UPGRADE_TOOL_VERSION)
  message(FATAL_ERROR "Could not parse TOOL_VERSION from application/Version.h")
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

add_library(upgrade_platform STATIC platform/PlatformPath.cpp platform/File.cpp platform/Text.cpp platform/Config.cpp platform/FileLog.cpp platform/Process.cpp platform/SerialPort.cpp)
target_include_directories(upgrade_platform PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(upgrade_platform PUBLIC upgrade_domain)
add_library(upgrade_usb STATIC platform/UsbContext.cpp)
target_include_directories(upgrade_usb PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(upgrade_usb PUBLIC upgrade_domain PRIVATE usb1)
add_library(upgrade_application STATIC application/TaskRunner.cpp application/TaskService.cpp application/BackendRegistry.cpp application/ConfigService.cpp)
target_include_directories(upgrade_application PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(upgrade_application PUBLIC upgrade_domain PRIVATE upgrade_platform)

set(UPGRADE_BACKENDS)
if(UPGRADE_TOOL_ROCKCHIP)
  if(WIN32)
    add_library(upgrade_rockchip_usb STATIC backends/rockchip/WindowsRockusb.cpp)
    target_link_libraries(upgrade_rockchip_usb PRIVATE setupapi cfgmgr32)
  else()
    add_library(upgrade_rockchip_usb STATIC backends/rockchip/LibusbTransport.cpp)
    target_link_libraries(upgrade_rockchip_usb PRIVATE usb1)
  endif()
  target_include_directories(upgrade_rockchip_usb PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(upgrade_rockchip_usb PRIVATE upgrade_platform)
  add_library(upgrade_rockchip STATIC
    backends/rockchip/legacy/crc.cpp backends/rockchip/legacy/RKScan.cpp backends/rockchip/legacy/RKComm.cpp backends/rockchip/legacy/RKDevice.cpp
    backends/rockchip/legacy/RKBoot.cpp backends/rockchip/legacy/RKImage.cpp backends/rockchip/legacy/Config.cpp backends/rockchip/legacy/Parameter.cpp backends/rockchip/legacy/Idb.cpp
    backends/rockchip/legacy/MD5Checksum.cpp backends/rockchip/legacy/AndroidDevice.cpp backends/rockchip/legacy/RKDisk.cpp backends/rockchip/legacy/DeviceFactory.cpp backends/rockchip/legacy/Upgrade.cpp
    backends/rockchip/UpgradeEngine.cpp backends/rockchip/FirmwareOperations.cpp
    backends/rockchip/FirmwareService.cpp backends/rockchip/RockchipBackend.cpp
    backends/rockchip/DeviceOperations.cpp backends/rockchip/StorageOperations.cpp
    backends/rockchip/PartitionOperations.cpp backends/rockchip/UpgradeOperations.cpp)
  target_include_directories(upgrade_rockchip PRIVATE backends/rockchip/legacy ${CMAKE_SOURCE_DIR})
  target_link_libraries(upgrade_rockchip PUBLIC upgrade_domain upgrade_rockchip_usb PRIVATE upgrade_platform)
  if(NOT WIN32)
    target_link_libraries(upgrade_rockchip PRIVATE upgrade_usb)
  endif()
  list(APPEND UPGRADE_BACKENDS upgrade_rockchip)
endif()
if(UPGRADE_TOOL_ALLWINNER)
  add_library(efex STATIC third_party/libefex/src/efex-common.c third_party/libefex/src/efex-fel.c third_party/libefex/src/efex-fes.c third_party/libefex/src/efex-usb.c)
  set_target_properties(efex PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES)
  target_include_directories(efex PUBLIC third_party/libefex/includes)
  add_library(upgrade_allwinner STATIC backends/allwinner/Firmware.cpp backends/allwinner/Convert.cpp backends/allwinner/Transport.cpp backends/allwinner/AllwinnerBackend.cpp)
  target_include_directories(upgrade_allwinner PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(upgrade_allwinner PUBLIC upgrade_domain PRIVATE upgrade_platform upgrade_usb efex usb1)
  list(APPEND UPGRADE_BACKENDS upgrade_allwinner)
endif()
if(UPGRADE_TOOL_QUALCOMM)
  include(cmake/Qdl.cmake)
  add_library(upgrade_qualcomm STATIC backends/qualcomm/QualcommBackend.cpp)
  target_include_directories(upgrade_qualcomm PRIVATE ${CMAKE_SOURCE_DIR})
  target_link_libraries(upgrade_qualcomm PUBLIC upgrade_domain PRIVATE upgrade_platform qdl_embedded)
  list(APPEND UPGRADE_BACKENDS upgrade_qualcomm)
endif()
add_library(upgradetool STATIC backends/BackendFactory.cpp)
target_include_directories(upgradetool PRIVATE ${CMAKE_SOURCE_DIR})
target_link_libraries(upgradetool PUBLIC upgrade_domain upgrade_application PRIVATE ${UPGRADE_BACKENDS})

if(UPGRADE_TOOL_GUI)
if(NOT UPGRADE_TOOL_ROCKCHIP AND NOT UPGRADE_TOOL_ALLWINNER AND NOT UPGRADE_TOOL_QUALCOMM)
  message(FATAL_ERROR "The desktop application requires at least one vendor backend")
endif()
find_package(Qt6 REQUIRED COMPONENTS Core Gui Quick QuickControls2 SerialPort)
if(APPLE AND TARGET WrapOpenGL::WrapOpenGL)
  get_target_property(_qt_opengl_links WrapOpenGL::WrapOpenGL INTERFACE_LINK_LIBRARIES)
  list(FILTER _qt_opengl_links EXCLUDE REGEX "AGL(\\.framework)?$")
  set_property(TARGET WrapOpenGL::WrapOpenGL PROPERTY
    INTERFACE_LINK_LIBRARIES "${_qt_opengl_links}")
endif()
add_subdirectory(gui)

endif()

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  install(FILES scripts/99-rk-rockusb-all.rules scripts/99-allwinner.rules scripts/99-qualcomm.rules
          DESTINATION share/upgrade_tool)
  install(PROGRAMS scripts/install-udev.sh
          DESTINATION share/upgrade_tool)
endif()
install(FILES config.ini
        DESTINATION share/upgrade_tool)
if(UPGRADE_TOOL_ALLWINNER)
install(FILES third_party/libefex/LICENSE backends/allwinner/LICENSE.OpenixCLI
        DESTINATION share/doc/upgrade-tool/allwinner)
endif()
if(UPGRADE_TOOL_QUALCOMM)
install(FILES third_party/qdl/LICENSE
        DESTINATION share/doc/upgrade-tool/qualcomm)
install(FILES third_party/libzip/LICENSE
        DESTINATION share/doc/upgrade-tool/qualcomm RENAME LICENSE.libzip)
install(FILES third_party/libxml2/Copyright
        DESTINATION share/doc/upgrade-tool/qualcomm RENAME LICENSE.libxml2)
endif()
install(FILES third_party/libusb/COPYING
        DESTINATION share/doc/upgrade-tool RENAME LICENSE.libusb)
install(FILES README.md README.zh-CN.md
        DESTINATION share/doc/upgrade-tool)

if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  set(CPACK_DEBIAN_PACKAGE_CONTROL_EXTRA
      "${CMAKE_SOURCE_DIR}/scripts/deb/postinst;${CMAKE_SOURCE_DIR}/scripts/deb/postrm")

  set(CPACK_PACKAGE_NAME "upgrade-tool")
  set(CPACK_PACKAGE_VERSION "${UPGRADE_TOOL_VERSION}")
  set(CPACK_PACKAGE_DESCRIPTION_SUMMARY
      "UpgradeTool-GUI (Qt6 GUI) for Rockchip, Allwinner and Qualcomm USB firmware flashing")
  set(CPACK_PACKAGE_CONTACT "UpgradeTool-GUI maintainers")
  set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/rockchip-linux/rkdeveloptool")
  set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_SOURCE_DIR}/backends/rockchip/legacy/license.txt")
  set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
  set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
  set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "pkexec")
  set(CPACK_DEBIAN_PACKAGE_DEPENDS
      "adb, qml6-module-qtquick, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-templates, qml6-module-qtquick-window, qml6-module-qtqml-workerscript, qt6-qpa-plugins, qt6-svg-plugins | libqt6svg6")
  if(NOT CPACK_DEBIAN_PACKAGE_ARCHITECTURE)
    find_program(DPKG_EXECUTABLE dpkg)
    if(DPKG_EXECUTABLE)
      execute_process(COMMAND "${DPKG_EXECUTABLE}" --print-architecture
        OUTPUT_VARIABLE CPACK_DEBIAN_PACKAGE_ARCHITECTURE
        OUTPUT_STRIP_TRAILING_WHITESPACE
        COMMAND_ERROR_IS_FATAL ANY)
    else()
      set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE i386)
    endif()
  endif()
  set(CPACK_DEBIAN_FILE_NAME
      "UpgradeTool-GUI_${UPGRADE_TOOL_VERSION}_${CPACK_DEBIAN_PACKAGE_ARCHITECTURE}.deb")
  set(CPACK_GENERATOR "DEB")
  include(CPack)
endif()

if(UPGRADE_TOOL_TESTS)
  add_subdirectory(tests)
endif()
