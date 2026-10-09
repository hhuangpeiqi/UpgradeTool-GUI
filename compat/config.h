/* Minimal libusb config.h for a Linux/glibc build.
 *
 * The vendored libusb normally gets this from autotools, which is unavailable
 * on this machine. Rather than pull in a full autotools bootstrap, we ship a
 * hand-written config covering the features present on Linux + glibc. libusb
 * is deliberately built without libudev (HAVE_LIBUDEV unset) so it uses the
 * netlink backend.
 */
#ifndef LIBUSB_COMPAT_CONFIG_H
#define LIBUSB_COMPAT_CONFIG_H

#define PLATFORM_POSIX 1

#define HAVE_CLOCK_GETTIME 1
#define HAVE_EVENTFD 1
#define HAVE_TIMERFD 1
#define HAVE_PIPE2 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYSLOG 1
#define HAVE_NFDS_T 1
#define HAVE_ASM_TYPES_H 1
#define HAVE_OS_TIMER 1
#define HAVE_PTHREAD_CONDATTR_SETCLOCK 1
#define HAVE_PTHREAD_SETNAME_NP 1

#define ENABLE_LOGGING 1
#define ENABLE_DEBUG_LOGGING 1
#define USE_SYSTEM_LOGGING_FACILITY 1

#define DEFAULT_VISIBILITY __attribute__((visibility("default")))
#define PRINTF_FORMAT(a, b) __attribute__((format(printf, a, b)))

#endif
