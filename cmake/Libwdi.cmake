# Windows 10/11 use the inbox WinUSB driver; no legacy WDK coinstallers are shipped.
set(WDI_SOURCE "${CMAKE_SOURCE_DIR}/third_party/libwdi/libwdi")
set(WDI_BUILD "${CMAKE_BINARY_DIR}/libwdi-inbox")
if(NOT EXISTS "${WDI_SOURCE}/libwdi.c")
  message(FATAL_ERROR "libwdi submodule missing: run git submodule update --init --recursive")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WDI_SOURCE}/libwdi.c")
file(MAKE_DIRECTORY "${WDI_BUILD}")
file(READ "${WDI_SOURCE}/libwdi.c" _wdi)
function(wdi_replace before after)
  string(FIND "${_wdi}" "${before}" _position)
  if(_position LESS 0)
    message(FATAL_ERROR "libwdi inbox adaptation no longer applies; review the pinned upstream revision")
  endif()
  string(REPLACE "${before}" "${after}" _wdi "${_wdi}")
  set(_wdi "${_wdi}" PARENT_SCOPE)
endfunction()
wdi_replace("\tint res, r;" "\tint res = 0, r;")
wdi_replace("\tfor (res=0; res<nb_resources; res++) {\n\t\t// Identify the WinUSB"
  "\tif (driver_type == WDI_WINUSB) {\n\t\tGetSystemDirectoryA(filename, MAX_PATH);\n\t\tsafe_strcat(filename, MAX_PATH, \"\\\\drivers\\\\winusb.sys\");\n\t\tgoto read_version;\n\t}\n\tfor (res=0; res<nb_resources; res++) {\n\t\t// Identify the WinUSB")
wdi_replace("\t// Read the version\n\twfilename" "read_version:\n\t// Read the version of the installed inbox driver, without redistributing it.\n\twfilename")
wdi_replace("unixtime_to_msfiletime((time_t)resource[res].creation_time)"
  "unixtime_to_msfiletime(driver_type == WDI_WINUSB ? (time_t)1577836800 : (time_t)resource[res].creation_time)")
wdi_replace("\tDeleteFileU(filename);\n\nout:" "\tif (driver_type != WDI_WINUSB) DeleteFileU(filename);\n\nout:")
file(CONFIGURE OUTPUT "${WDI_BUILD}/libwdi.c" CONTENT "${_wdi}" @ONLY)
file(CONFIGURE OUTPUT "${WDI_BUILD}/config.h" CONTENT "#define OPT_M64 1\n#define WDK_DIR \"inbox\"\n#define WDF_VER 1011\n#define ENABLE_LOGGING 1\n#define INCLUDE_DEBUG_LOGGING 1\n" @ONLY)

set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${WDI_SOURCE}/installer.c")
file(READ "${WDI_SOURCE}/installer.c" _wdi)
wdi_replace("HANDLE pipe_handle = INVALID_HANDLE_VALUE;"
  "static BOOL upgrade_reboot_required = FALSE;\nHANDLE pipe_handle = INVALID_HANDLE_VALUE;")
wdi_replace("UpdateDriverForPlugAndPlayDevicesU(NULL, hardware_id, path, INSTALLFLAG_FORCE, NULL)"
  "UpdateDriverForPlugAndPlayDevicesU(NULL, hardware_id, path, INSTALLFLAG_FORCE, &upgrade_reboot_required)")
wdi_replace("\t\tsend_status(IC_SET_TIMEOUT_DEFAULT);\n\t\tif (b == TRUE)"
  "\t\tDWORD upgrade_error = b ? ERROR_SUCCESS : GetLastError();\n\t\tsend_status(IC_SET_TIMEOUT_DEFAULT);\n\t\tif (b == TRUE)")
wdi_replace("\t\tret = process_error(GetLastError(), path);"
  "\t\tret = process_error(upgrade_error, path);")
wdi_replace("case ERROR_SUCCESS_REBOOT_REQUIRED:\n"
  "case ERROR_SUCCESS_REBOOT_REQUIRED:\n\t\tupgrade_reboot_required = TRUE;\n")
wdi_replace("out:\n\t// Report any error status code"
  "out:\n\t{\n\t\tFILE *status = fopenU(\"upgrade-install-status.json\", \"wb\");\n\t\tif (status != NULL) {\n\t\t\tfprintf(status, \"{\\\"rebootRequired\\\":%s}\", upgrade_reboot_required ? \"true\" : \"false\");\n\t\t\tfclose(status);\n\t\t}\n\t}\n\t// Report any error status code")
file(CONFIGURE OUTPUT "${WDI_BUILD}/installer.c" CONTENT "${_wdi}" @ONLY)
add_executable(wdi_installer "${WDI_BUILD}/installer.c")
set_target_properties(wdi_installer PROPERTIES AUTOMOC OFF OUTPUT_NAME installer_x64 RUNTIME_OUTPUT_DIRECTORY "${WDI_BUILD}")
target_include_directories(wdi_installer PRIVATE "${WDI_BUILD}" "${WDI_SOURCE}")
target_link_options(wdi_installer PRIVATE -static)
target_link_libraries(wdi_installer PRIVATE setupapi newdev ole32)

add_custom_command(OUTPUT "${WDI_BUILD}/embedded.h"
  COMMAND "${CMAKE_COMMAND}" "-DSOURCE=${WDI_SOURCE}" "-DOUTPUT=${WDI_BUILD}/embedded.h"
    "-DINSTALLER=$<TARGET_FILE:wdi_installer>" -P "${CMAKE_SOURCE_DIR}/cmake/EmbedWinusb.cmake"
  DEPENDS wdi_installer "${CMAKE_SOURCE_DIR}/cmake/EmbedWinusb.cmake"
    "${CMAKE_SOURCE_DIR}/scripts/windows/winusb.inf.in"
  VERBATIM)
add_library(wdi SHARED "${WDI_BUILD}/libwdi.c" "${WDI_BUILD}/embedded.h"
  "${WDI_SOURCE}/logging.c" "${WDI_SOURCE}/tokenizer.c" "${WDI_SOURCE}/vid_data.c"
  "${WDI_SOURCE}/pki.c" "${WDI_SOURCE}/libwdi_dlg.c" "${WDI_SOURCE}/libwdi.rc")
target_include_directories(wdi PRIVATE "${WDI_BUILD}" "${WDI_SOURCE}")
target_compile_definitions(wdi PRIVATE LIBWDI_DLL_EXPORT)
target_link_options(wdi PRIVATE -static-libgcc)
target_link_libraries(wdi PRIVATE setupapi ole32 ntdll)
set_target_properties(wdi PROPERTIES AUTOMOC OFF PREFIX "" OUTPUT_NAME libwdi)
target_include_directories(wdi INTERFACE "${WDI_SOURCE}")
install(TARGETS wdi RUNTIME DESTINATION bin)
install(FILES "${CMAKE_SOURCE_DIR}/third_party/libwdi/COPYING" "${CMAKE_SOURCE_DIR}/third_party/libwdi/COPYING-LGPL"
  DESTINATION share/doc/upgrade-tool/libwdi)
