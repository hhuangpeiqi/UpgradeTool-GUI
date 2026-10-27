set(CMAKE_AUTOMOC ON)

add_library(upgrade_gui_backend STATIC
  WindowsUsbDriver.cpp
  WindowsUsbDriver.h
  DriverRecovery.cpp
  DriverRecovery.h
  UsbAccess.cpp
  UsbAccess.h
  backend/DriverController.cpp
  backend/DriverController.h
  ApplicationLifecycle.cpp
  ApplicationLifecycle.h
  ProcessAdapter.cpp
  ProcessAdapter.h
  MessageCatalog.cpp
  OperationSpec.cpp
  OperationSpec.h
  QtObserver.cpp
  QtObserver.h
  InspectionQueue.cpp
  InspectionQueue.h
  AllwinnerSwitchMonitor.cpp
  AllwinnerSwitchMonitor.h
  EngineWorker.cpp
  EngineWorker.h
  EdlRunner.cpp
  EdlRunner.h
  EdlDeviceSelection.h
  EdlTransport.cpp
  AdbService.cpp
  AdbService.h
  AdbRunner.cpp
  AdbRunner.h
  AdbTiming.h
  AdbExecutable.cpp
  AdbExecutable.h
  backend/DeviceModel.cpp
  backend/DeviceModel.h
  backend/TaskController.cpp
  backend/TaskController.h
  backend/RockchipController.cpp
  backend/RockchipController.h
  backend/AllwinnerController.cpp
  backend/AllwinnerController.h
  backend/QualcommController.cpp
  backend/QualcommController.h
  backend/AppController.cpp
  backend/AppController.h
  backend/LogModel.cpp
  backend/LogModel.h
  backend/ImageEntryModel.cpp
  backend/ImageEntryModel.h
  backend/KeyValueModel.cpp
  backend/KeyValueModel.h
  backend/ConfigStore.cpp
  backend/ConfigStore.h
  backend/DirModel.cpp
  backend/DirModel.h
  backend/FileLocations.cpp
  backend/FileLocations.h
  backend/ThemeManager.cpp
  backend/ThemeManager.h
  backend/LanguageManager.cpp
  backend/LanguageManager.h
  backend/Retranslatable.h
)

target_include_directories(upgrade_gui_backend PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/backends/rockchip/legacy)
target_link_libraries(upgrade_gui_backend PUBLIC upgradetool upgrade_platform usb1 Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::SerialPort)
if(WIN32)
  include(${CMAKE_SOURCE_DIR}/scripts/rockusb_driver_manifest.cmake)
  list(LENGTH ROCKUSB_DRIVER_FILES ROCKUSB_DRIVER_FILE_COUNT)
  set(ROCKUSB_DRIVER_CPP_FILES "")
  foreach(entry IN LISTS ROCKUSB_DRIVER_FILES)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 relative)
    list(GET fields 1 checksum)
    string(APPEND ROCKUSB_DRIVER_CPP_FILES "    {\"${relative}\", \"${checksum}\"},\n")
  endforeach()
  configure_file(${CMAKE_SOURCE_DIR}/application/RockusbDriverPackage.h.in
    ${CMAKE_BINARY_DIR}/generated/application/RockusbDriverPackage.h @ONLY)
  include(${CMAKE_SOURCE_DIR}/scripts/qualcomm_driver_manifest.cmake)
  configure_file(${CMAKE_SOURCE_DIR}/application/QualcommDriverPackage.h.in
    ${CMAKE_BINARY_DIR}/generated/application/QualcommDriverPackage.h @ONLY)
  include(${CMAKE_SOURCE_DIR}/scripts/allwinner_driver_manifest.cmake)
  list(LENGTH ALLWINNER_DRIVER_FILES ALLWINNER_DRIVER_FILE_COUNT)
  set(ALLWINNER_DRIVER_CPP_FILES "")
  foreach(entry IN LISTS ALLWINNER_DRIVER_FILES)
    string(REPLACE "|" ";" fields "${entry}")
    list(GET fields 0 relative)
    list(GET fields 1 checksum)
    string(APPEND ALLWINNER_DRIVER_CPP_FILES "    {\"${relative}\", \"${checksum}\"},\n")
  endforeach()
  configure_file(${CMAKE_SOURCE_DIR}/application/AllwinnerDriverPackage.h.in
    ${CMAKE_BINARY_DIR}/generated/application/AllwinnerDriverPackage.h @ONLY)
  target_link_libraries(upgrade_gui_backend PUBLIC cfgmgr32 winusb shell32 ole32)
  qt_add_executable(upgrade_driver_installer ${CMAKE_SOURCE_DIR}/scripts/windows/driver_installer.cpp
    ${CMAKE_SOURCE_DIR}/scripts/windows/DriverInstall.cpp
    ${CMAKE_SOURCE_DIR}/scripts/windows/AllwinnerDriverInstall.cpp
    ${CMAKE_SOURCE_DIR}/scripts/windows/QualcommDriverInstall.cpp
    ${CMAKE_SOURCE_DIR}/scripts/windows/RockusbDriverInstall.cpp WindowsUsbDriver.cpp DriverRecovery.cpp)
  target_include_directories(upgrade_driver_installer PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_BINARY_DIR}/generated)
  target_link_libraries(upgrade_driver_installer PRIVATE upgrade_platform Qt6::Core setupapi cfgmgr32 winusb shell32 ole32)
  if(UPGRADE_TOOL_ROCKCHIP)
    target_link_libraries(upgrade_driver_installer PRIVATE upgrade_rockchip_usb)
  endif()
  if(UPGRADE_TOOL_ALLWINNER)
    target_link_libraries(upgrade_driver_installer PRIVATE upgrade_allwinner)
  endif()
  set_target_properties(upgrade_driver_installer PROPERTIES WIN32_EXECUTABLE TRUE)
  install(TARGETS upgrade_driver_installer RUNTIME DESTINATION bin)
endif()
qt_add_executable(upgrade_tool_gui main.cpp)
if(WIN32)
  add_dependencies(upgrade_tool_gui upgrade_driver_installer)
  enable_language(RC)
  target_sources(upgrade_tool_gui PRIVATE ${CMAKE_SOURCE_DIR}/scripts/windows/app_icon.rc)
  target_include_directories(upgrade_tool_gui PRIVATE ${CMAKE_SOURCE_DIR}/scripts/windows)
  set_target_properties(upgrade_tool_gui PROPERTIES WIN32_EXECUTABLE TRUE)
endif()

set(_qml_root ${CMAKE_CURRENT_SOURCE_DIR}/qml)

set(_qml_files
  dialogs/DriverDialog.qml
  components/QualcommWorkspace.qml
  components/QualcommToolPanel.qml
  components/QualcommPathRow.qml
  components/QualcommOptions.qml
  components/QualcommProgrammer.qml
  components/QualcommComboBox.qml
  components/SegmentedControl.qml
  components/QualcommXmlList.qml
  pages/QualcommFlashPage.qml
  pages/QualcommPartitionsPage.qml
  pages/QualcommToolsPage.qml
  Main.qml
  Theme.qml
  Icons.qml
  components/PrimaryButton.qml
  components/SecondaryButton.qml
  components/Spinner.qml
  components/AppCheckBox.qml
  components/AppComboBox.qml
  components/CheckSquare.qml
  components/AppTextField.qml
  components/Card.qml
  components/SectionTitle.qml
  components/FormSection.qml
  components/DisclosureSection.qml
  components/AppProgressBar.qml
  components/EmptyState.qml
  components/TitleBar.qml
  components/NavButton.qml
  components/NavAction.qml
  components/FilePathRow.qml
  components/TopBar.qml
  components/DeviceComboBox.qml
  components/StatusBar.qml
  components/LogPanel.qml
  components/Toast.qml
  components/AppDialog.qml
  components/AppFilePicker.qml
  components/EdgeRoundedRect.qml
  components/AppMenu.qml
  components/AppMenuItem.qml
  components/AppMenuSeparator.qml
  components/ImageTable.qml
  components/KeyValueTable.qml
  pages/DownloadImagePage.qml
  components/AllwinnerWorkspace.qml
  components/AllwinnerPartitions.qml
  pages/AllwinnerRawPage.qml
  pages/AllwinnerFirmwarePage.qml
  pages/AllwinnerToolsPage.qml
  dialogs/AllwinnerDeviceDialog.qml
  dialogs/SwitchRecoveryDialog.qml
  pages/FirmwarePage.qml
  pages/AdvancedPage.qml
  dialogs/SettingsDialog.qml
  dialogs/BatchSelectDialog.qml
  dialogs/StorageSelectDialog.qml
  dialogs/EdlSwitchDialog.qml
  dialogs/AdbDeviceDialog.qml
  dialogs/GptDialog.qml
  dialogs/AboutDialog.qml
  dialogs/PermissionDialog.qml
)

set(_icon_files
  icons/download-gray.svg
  icons/download-blue.svg
  icons/chip-gray.svg
  icons/chip-blue.svg
  icons/gear-gray.svg
  icons/gear-blue.svg
  icons/monitor-gray.svg
  icons/more-gray.svg
  icons/refresh-blue.svg
  icons/folder-white.svg
  icons/folder-gray.svg
  icons/file-gray.svg
  icons/search-gray.svg
  icons/play-white.svg
  icons/switch-blue.svg
  icons/trash-gray.svg
  icons/plus-gray.svg
  icons/device-gray.svg
  icons/inbox-gray.svg
  icons/check-white.svg
  icons/caret-gray.svg
  icons/download-gray-dark.svg
  icons/chip-gray-dark.svg
  icons/gear-gray-dark.svg
  icons/monitor-gray-dark.svg
  icons/more-gray-dark.svg
  icons/folder-gray-dark.svg
  icons/file-gray-dark.svg
  icons/search-gray-dark.svg
  icons/trash-gray-dark.svg
  icons/plus-gray-dark.svg
  icons/device-gray-dark.svg
  icons/inbox-gray-dark.svg
  icons/caret-gray-dark.svg
)

set(_qml_abs)
foreach(_f IN LISTS _qml_files)
  get_filename_component(_qml_alias "${_f}" NAME)
  set_source_files_properties(${_qml_root}/${_f}
    PROPERTIES QT_RESOURCE_ALIAS "${_qml_alias}")
  list(APPEND _qml_abs ${_qml_root}/${_f})
endforeach()
unset(_qml_alias)

set(_icon_abs)
foreach(_f IN LISTS _icon_files)
  set_source_files_properties(${_qml_root}/${_f}
    PROPERTIES QT_RESOURCE_ALIAS "${_f}")
  list(APPEND _icon_abs ${_qml_root}/${_f})
endforeach()

set(_app_logo "${CMAKE_SOURCE_DIR}/scripts/upgrade_tool.svg")
set_source_files_properties(${_app_logo}
  PROPERTIES QT_RESOURCE_ALIAS "icons/upgrade_tool.svg")
list(APPEND _icon_abs ${_app_logo})

set_source_files_properties(${_qml_root}/Theme.qml ${_qml_root}/Icons.qml
  PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

if(QT_KNOWN_POLICY_QTP0004)
  qt_policy(SET QTP0004 NEW)
endif()

set(_qml_module_options)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.7)
  list(APPEND _qml_module_options NO_CACHEGEN)
endif()

qt_add_qml_module(upgrade_tool_gui
  URI UpgradeTool
  VERSION 1.0
  RESOURCE_PREFIX /qt/qml
  QML_FILES ${_qml_abs}
  RESOURCES ${_icon_abs}
  ${_qml_module_options}
)

find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
set(_tr_sources
  ${CMAKE_CURRENT_SOURCE_DIR}/MessageCatalog.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/DriverController.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/RockchipController.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/AllwinnerController.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/EdlRunner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/EdlTransport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/UsbAccess.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/EngineWorker.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/AllwinnerSwitchMonitor.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/AdbRunner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/AdbService.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/QtObserver.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/OperationSpec.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/QualcommController.cpp
  ${CMAKE_SOURCE_DIR}/backends/qualcomm/QualcommBackend.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/AppController.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/ConfigStore.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/DirModel.cpp
  ${CMAKE_SOURCE_DIR}/backends/allwinner/AllwinnerBackend.cpp
  ${CMAKE_SOURCE_DIR}/backends/allwinner/Convert.cpp
  ${CMAKE_SOURCE_DIR}/backends/allwinner/Firmware.cpp
  ${_qml_abs}
)
if(Qt6_VERSION VERSION_GREATER_EQUAL 6.7)
  qt_add_translations(TARGETS upgrade_tool_gui
    SOURCES ${_tr_sources}
    INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}
    TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/upgrade_tool_zh_CN.ts
    RESOURCE_PREFIX "/i18n")
else()
  qt_add_translations(upgrade_tool_gui
    SOURCES ${_tr_sources}
    INCLUDE_DIRECTORIES ${CMAKE_SOURCE_DIR}
    TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/upgrade_tool_zh_CN.ts
    RESOURCE_PREFIX "/i18n")
endif()

# The test tree owns test targets; expose the application's resource manifest.
set_property(TARGET upgrade_tool_gui PROPERTY UPGRADE_TEST_QML_FILES "${_qml_abs};${_icon_abs};${CMAKE_CURRENT_BINARY_DIR}/UpgradeTool/qmldir")
set_property(TARGET upgrade_tool_gui PROPERTY UPGRADE_TEST_TRANSLATIONS "${CMAKE_CURRENT_SOURCE_DIR}/i18n/upgrade_tool_zh_CN.ts")

function(upgrade_attach_gui_test_resources target)
  get_target_property(files upgrade_tool_gui UPGRADE_TEST_QML_FILES)
  get_target_property(gui_source upgrade_tool_gui SOURCE_DIR)
  foreach(file IN LISTS files)
    get_source_file_property(alias "${file}" DIRECTORY "${gui_source}" QT_RESOURCE_ALIAS)
    if(NOT alias)
      set(alias qmldir)
    endif()
    set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "${alias}")
  endforeach()
  qt_add_resources(${target} ${target}_assets
    PREFIX "/qt/qml/UpgradeTool" FILES ${files})
endfunction()

function(upgrade_attach_test_translations target)
  get_target_property(translations upgrade_tool_gui UPGRADE_TEST_TRANSLATIONS)
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.7)
    qt_add_translations(TARGETS ${target} TS_FILES ${translations} RESOURCE_PREFIX "/i18n")
  else()
    qt_add_translations(${target} TS_FILES ${translations} RESOURCE_PREFIX "/i18n")
  endif()
endfunction()

target_include_directories(upgrade_tool_gui PRIVATE ${CMAKE_SOURCE_DIR} ${CMAKE_SOURCE_DIR}/backends/rockchip/legacy)
target_link_libraries(upgrade_tool_gui PRIVATE
  upgrade_gui_backend
  Qt6::Core
  Qt6::Gui
  Qt6::Quick
  Qt6::QuickControls2
)

if(APPLE)
  find_package(Qt6 REQUIRED COMPONENTS Svg)
  add_executable(upgrade_tool_icon_renderer EXCLUDE_FROM_ALL
    ${CMAKE_SOURCE_DIR}/scripts/macos/render_icon.cpp)
  target_link_libraries(upgrade_tool_icon_renderer PRIVATE Qt6::Core Qt6::Gui Qt6::Svg)
  set(_app_icon "${CMAKE_CURRENT_BINARY_DIR}/upgrade_tool.icns")
  add_custom_command(OUTPUT "${_app_icon}"
    COMMAND upgrade_tool_icon_renderer "${_app_logo}"
            "${CMAKE_CURRENT_BINARY_DIR}/upgrade_tool.iconset"
    COMMAND /usr/bin/iconutil -c icns
            "${CMAKE_CURRENT_BINARY_DIR}/upgrade_tool.iconset" -o "${_app_icon}"
    DEPENDS upgrade_tool_icon_renderer "${_app_logo}"
    VERBATIM)
  set_source_files_properties("${_app_icon}" PROPERTIES
    GENERATED TRUE MACOSX_PACKAGE_LOCATION Resources)
  set_source_files_properties("${CMAKE_SOURCE_DIR}/config.ini" PROPERTIES
    MACOSX_PACKAGE_LOCATION Resources)
  target_sources(upgrade_tool_gui PRIVATE "${_app_icon}" "${CMAKE_SOURCE_DIR}/config.ini")
  set_target_properties(upgrade_tool_gui PROPERTIES
    MACOSX_BUNDLE TRUE
    MACOSX_BUNDLE_GUI_IDENTIFIER "org.rockchip.upgrade-tool"
    MACOSX_BUNDLE_BUNDLE_NAME "UpgradeTool-GUI"
    MACOSX_BUNDLE_ICON_FILE "upgrade_tool.icns"
    MACOSX_BUNDLE_BUNDLE_VERSION "${UPGRADE_TOOL_VERSION}"
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${UPGRADE_TOOL_VERSION}")
endif()

install(TARGETS upgrade_tool_gui BUNDLE DESTINATION . RUNTIME DESTINATION bin)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  install(FILES ${CMAKE_SOURCE_DIR}/scripts/upgrade_tool.desktop
          DESTINATION share/applications)
  install(FILES ${CMAKE_SOURCE_DIR}/scripts/upgrade_tool.svg
          DESTINATION share/icons/hicolor/scalable/apps)
endif()
