/* Minimal libusb config.h for a Windows build.
 *
 * The vendored libusb normally gets this from autotools, which we do not run
 * on Windows either. This hand-written config mirrors third_party/libusb/msvc
 * but is written so it also compiles under MinGW (which does not define
 * _MSC_VER), because the CMake build may be driven by either toolchain.
 *
 * NOT VALIDATED ON A REAL WINDOWS HOST. The Linux build is the verified one;
 * see README (P7) before claiming Windows support.
 */
#ifndef LIBUSB_COMPAT_WINDOWS_CONFIG_H
#define LIBUSB_COMPAT_WINDOWS_CONFIG_H

#define PLATFORM_WINDOWS 1

#if defined(_MSC_VER)
/* Visual Studio 2013 or later is required. */
#if (_MSC_VER < 1800)
#error "Visual Studio 2013 or later is required."
#endif
/* Visual Studio 2013/2015 do not support __func__. */
#if (_MSC_VER <= 1900)
#define __func__ __FUNCTION__
#endif
/* Visual Studio 2015 and later defines timespec. */
#if (_MSC_VER >= 1900)
#define _TIMESPEC_DEFINED 1
#endif
#pragma warning(disable:4127)
#pragma warning(disable:4200)
#pragma warning(disable:4201)
#pragma warning(disable:4324)
#pragma warning(disable:4996)
#endif

/* Define to the attribute for default visibility. */
#define DEFAULT_VISIBILITY /**/

/* Define to 1 to enable message logging. */
#define ENABLE_LOGGING 1

/* Define to the attribute for enabling parameter checks on printf-like
   functions. */
#define PRINTF_FORMAT(a, b) /**/

#endif
