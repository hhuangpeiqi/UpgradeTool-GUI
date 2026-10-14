set(CMAKE_AUTOMOC ON)

add_library(upgrade_gui_backend STATIC
  OperationSpec.cpp
  OperationSpec.h
  QtObserver.cpp
  QtObserver.h
  EngineWorker.cpp
  EngineWorker.h
  EdlRunner.cpp
  EdlRunner.h
  EdlTransport.cpp
  AdbRunner.cpp
  AdbRunner.h
  FirmwareEntries.cpp
  FirmwareEntries.h
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
  backend/ThemeManager.cpp
  backend/ThemeManager.h
  backend/LanguageManager.cpp
  backend/LanguageManager.h
  backend/Retranslatable.h
)

target_include_directories(upgrade_gui_backend PUBLIC ${CMAKE_SOURCE_DIR})
target_link_libraries(upgrade_gui_backend PUBLIC upgradetool Qt6::Core Qt6::Gui Qt6::Quick Qt6::QuickControls2 Qt6::SerialPort)
if(WIN32)
  target_link_libraries(upgrade_gui_backend PUBLIC cfgmgr32)
endif()
qt_add_executable(upgrade_tool_gui main.cpp)

if(UPGRADE_TOOL_TESTS)
  add_executable(test_edl_runner ${CMAKE_SOURCE_DIR}/tests/test_edl_runner.cpp)
  target_link_libraries(test_edl_runner PRIVATE upgrade_gui_backend)
  add_test(NAME test_edl_runner COMMAND test_edl_runner)
  set_tests_properties(test_edl_runner PROPERTIES TIMEOUT 30)
  add_executable(test_qualcomm_controller ${CMAKE_SOURCE_DIR}/tests/test_qualcomm_controller.cpp)
  target_link_libraries(test_qualcomm_controller PRIVATE upgrade_gui_backend)
  add_test(NAME test_qualcomm_controller COMMAND test_qualcomm_controller)
  set_tests_properties(test_qualcomm_controller PROPERTIES TIMEOUT 30)
  add_executable(test_adb_runner ${CMAKE_SOURCE_DIR}/tests/test_adb_runner.cpp)
  target_link_libraries(test_adb_runner PRIVATE upgrade_gui_backend)
  add_test(NAME test_adb_runner COMMAND test_adb_runner)
  set_tests_properties(test_adb_runner PROPERTIES TIMEOUT 30)
  add_executable(test_gui_backend ${CMAKE_SOURCE_DIR}/tests/test_gui_backend.cpp)
  target_link_libraries(test_gui_backend PRIVATE upgrade_gui_backend)
  add_test(NAME test_gui_backend COMMAND test_gui_backend)
  foreach(_tab RANGE 0 2)
    add_test(NAME test_gui_page_${_tab}
      COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:upgrade_tool_gui> -DTAB=${_tab}
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/smoke-${_tab}
        -P ${CMAKE_SOURCE_DIR}/tests/gui_smoke.cmake)
  endforeach()
  foreach(_tab RANGE 0 2)
    add_test(NAME test_allwinner_page_${_tab}
      COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:upgrade_tool_gui> -DTAB=${_tab} -DVENDOR=allwinner
        -DFIRMWARE=${CMAKE_BINARY_DIR}/allwinner-fixtures/normal.img
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/allwinner-smoke-${_tab}
        -P ${CMAKE_SOURCE_DIR}/tests/gui_smoke.cmake)
  endforeach()
  set_tests_properties(test_allwinner_page_0 test_allwinner_page_1 test_allwinner_page_2
    PROPERTIES FIXTURES_REQUIRED allwinner_firmware)
  foreach(_tab RANGE 0 2)
    add_test(NAME test_qualcomm_page_${_tab}
      COMMAND ${CMAKE_COMMAND}
        -DAPP=$<TARGET_FILE:upgrade_tool_gui> -DTAB=${_tab} -DVENDOR=qualcomm
        -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/qualcomm-smoke-${_tab}
        -P ${CMAKE_SOURCE_DIR}/tests/gui_smoke.cmake)
  endforeach()
  set_tests_properties(test_gui_backend PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 20)
endif()

set(_qml_root ${CMAKE_CURRENT_SOURCE_DIR}/qml)

set(_qml_files
  components/QualcommWorkspace.qml
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
  ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/EdlRunner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/EdlTransport.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/AdbRunner.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/QtObserver.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/FirmwareEntries.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/OperationSpec.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/QualcommController.cpp
  ${CMAKE_SOURCE_DIR}/core/qualcomm/QualcommBackend.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/AppController.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/ConfigStore.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/backend/DirModel.cpp
  ${CMAKE_SOURCE_DIR}/core/allwinner/AllwinnerBackend.cpp
  ${CMAKE_SOURCE_DIR}/core/allwinner/Convert.cpp
  ${CMAKE_SOURCE_DIR}/core/allwinner/Firmware.cpp
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

if(UPGRADE_TOOL_TESTS)
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.7)
    qt_add_translations(TARGETS test_gui_backend
      TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/upgrade_tool_zh_CN.ts
      RESOURCE_PREFIX "/i18n")
  else()
    qt_add_translations(test_gui_backend
      TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/upgrade_tool_zh_CN.ts
      RESOURCE_PREFIX "/i18n")
  endif()
endif()

if(UPGRADE_TOOL_TESTS)
  add_executable(test_allwinner_ui ${CMAKE_SOURCE_DIR}/tests/test_allwinner_ui.cpp)
  target_link_libraries(test_allwinner_ui PRIVATE upgrade_gui_backend)
  set(_test_qmldir "${CMAKE_CURRENT_BINARY_DIR}/UpgradeTool/qmldir")
  set_source_files_properties("${_test_qmldir}" PROPERTIES QT_RESOURCE_ALIAS qmldir)
  qt_add_resources(test_allwinner_ui allwinner_ui_assets
    PREFIX "/qt/qml/UpgradeTool" FILES ${_qml_abs} ${_icon_abs} "${_test_qmldir}")
  add_executable(test_qualcomm_ui ${CMAKE_SOURCE_DIR}/tests/test_qualcomm_ui.cpp)
  target_link_libraries(test_qualcomm_ui PRIVATE upgrade_gui_backend)
  qt_add_resources(test_qualcomm_ui qualcomm_ui_assets
    PREFIX "/qt/qml/UpgradeTool" FILES ${_qml_abs} ${_icon_abs} "${_test_qmldir}")
  if(Qt6_VERSION VERSION_GREATER_EQUAL 6.7)
    qt_add_translations(TARGETS test_qualcomm_ui
      TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/upgrade_tool_zh_CN.ts RESOURCE_PREFIX "/i18n")
  else()
    qt_add_translations(test_qualcomm_ui
      TS_FILES ${CMAKE_CURRENT_SOURCE_DIR}/i18n/upgrade_tool_zh_CN.ts RESOURCE_PREFIX "/i18n")
  endif()
  add_test(NAME test_qualcomm_ui COMMAND test_qualcomm_ui)
  set_tests_properties(test_qualcomm_ui PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
  add_test(NAME test_allwinner_ui COMMAND test_allwinner_ui
    "${CMAKE_BINARY_DIR}/allwinner-fixtures/normal.img" "${CMAKE_CURRENT_BINARY_DIR}/allwinner-ui")
  set_tests_properties(test_allwinner_ui PROPERTIES FIXTURES_REQUIRED allwinner_firmware
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software" TIMEOUT 30)
endif()

target_include_directories(upgrade_tool_gui PRIVATE ${CMAKE_SOURCE_DIR})
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
