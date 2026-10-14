find_package(Git REQUIRED)

if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/libzip/lib/zip.h")
  message(FATAL_ERROR
    "libzip submodule missing: run git submodule update --init --recursive")
endif()

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(_qdl_libzip_off
  BUILD_SHARED_LIBS
  LIBZIP_DO_INSTALL
  BUILD_TOOLS
  BUILD_REGRESS
  BUILD_OSSFUZZ
  BUILD_EXAMPLES
  BUILD_DOC
  ENABLE_BZIP2
  ENABLE_LZMA
  ENABLE_ZSTD
  ENABLE_COMMONCRYPTO
  ENABLE_GNUTLS
  ENABLE_MBEDTLS
  ENABLE_OPENSSL
  ENABLE_WINDOWS_CRYPTO)
foreach(_qdl_libzip_option IN LISTS _qdl_libzip_off)
  set(${_qdl_libzip_option} OFF)
endforeach()
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/libzip"
                 "${CMAKE_BINARY_DIR}/libzip")
foreach(_qdl_libzip_option IN LISTS _qdl_libzip_off)
  unset(${_qdl_libzip_option})
endforeach()
unset(_qdl_libzip_off)
unset(_qdl_libzip_option)
unset(CMAKE_POLICY_DEFAULT_CMP0077)

# libxml2 is linked statically and without ICU: Ubuntu 26.04 renamed the
# package to libxml2-16 and bumped the SONAME to 16, so a dynamically linked
# build from the 22.04 baseline cannot install there any more.
if(NOT EXISTS "${CMAKE_SOURCE_DIR}/third_party/libxml2/CMakeLists.txt")
  message(FATAL_ERROR
    "libxml2 submodule missing: run git submodule update --init --recursive")
endif()

set(CMAKE_POLICY_DEFAULT_CMP0077 NEW)
set(_qdl_libxml2_off
  BUILD_SHARED_LIBS
  LIBXML2_WITH_ICU
  LIBXML2_WITH_LZMA
  LIBXML2_WITH_ZLIB
  LIBXML2_WITH_PYTHON
  LIBXML2_WITH_TESTS
  LIBXML2_WITH_PROGRAMS
  LIBXML2_WITH_DOCS
  LIBXML2_WITH_MODULES
  LIBXML2_WITH_READLINE)
foreach(_qdl_libxml2_option IN LISTS _qdl_libxml2_off)
  set(${_qdl_libxml2_option} OFF)
endforeach()
add_subdirectory("${CMAKE_SOURCE_DIR}/third_party/libxml2"
                 "${CMAKE_BINARY_DIR}/libxml2" EXCLUDE_FROM_ALL)
foreach(_qdl_libxml2_option IN LISTS _qdl_libxml2_off)
  unset(${_qdl_libxml2_option})
endforeach()
unset(_qdl_libxml2_off)
unset(_qdl_libxml2_option)
unset(CMAKE_POLICY_DEFAULT_CMP0077)

set(QDL_UPSTREAM "${CMAKE_SOURCE_DIR}/third_party/qdl")
if(NOT EXISTS "${QDL_UPSTREAM}/src/qdl.c")
  message(FATAL_ERROR "qdl submodule missing: run git submodule update --init --recursive")
endif()
set(QDL_EMBEDDED_DIR "${CMAKE_BINARY_DIR}/qdl-embedded")
file(MAKE_DIRECTORY "${QDL_EMBEDDED_DIR}")
file(COPY "${QDL_UPSTREAM}/src" "${QDL_UPSTREAM}/include" DESTINATION "${QDL_EMBEDDED_DIR}")
set(QDL_PATCH "${CMAKE_SOURCE_DIR}/cmake/patches/qdl-embedded.patch")
execute_process(COMMAND "${GIT_EXECUTABLE}" apply --unsafe-paths "--directory=${QDL_EMBEDDED_DIR}" "${QDL_PATCH}"
  WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}" RESULT_VARIABLE QDL_PATCH_RESULT ERROR_VARIABLE QDL_PATCH_ERROR)
if(NOT QDL_PATCH_RESULT EQUAL 0)
  message(FATAL_ERROR "qdl embedding patch failed: ${QDL_PATCH_ERROR}")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${QDL_PATCH}" "${QDL_UPSTREAM}/src/meson.build")
set(QDL_SOURCES auto qud firehose input_type io patch program programmer read sahara_config sha2 sim ufs usb vip sparse gpt flashmap json contents pathbuf sahara util oscompat file file_zip zipper)
set(QDL_SOURCE_FILES)
foreach(source IN LISTS QDL_SOURCES)
  list(APPEND QDL_SOURCE_FILES "${QDL_EMBEDDED_DIR}/src/${source}.c")
endforeach()
add_library(qdl_embedded STATIC ${QDL_SOURCE_FILES}
  "${CMAKE_SOURCE_DIR}/core/qualcomm/qdl_bridge.c"
  "${CMAKE_SOURCE_DIR}/core/qualcomm/qdl_ux.c")
set_target_properties(qdl_embedded PROPERTIES C_STANDARD 11 C_STANDARD_REQUIRED YES C_EXTENSIONS YES)
target_include_directories(qdl_embedded PRIVATE "${QDL_EMBEDDED_DIR}/include" "${QDL_EMBEDDED_DIR}/src")
target_include_directories(qdl_embedded PUBLIC "${CMAKE_SOURCE_DIR}/core/qualcomm")
target_compile_definitions(qdl_embedded PRIVATE _FILE_OFFSET_BITS=64)
target_link_libraries(qdl_embedded PUBLIC usb1 LibXml2::LibXml2 libzip::zip)
if(WIN32)
  target_compile_definitions(qdl_embedded PRIVATE _WIN32_WINNT=0x0600)
  target_link_libraries(qdl_embedded PRIVATE ws2_32 setupapi cfgmgr32)
endif()
