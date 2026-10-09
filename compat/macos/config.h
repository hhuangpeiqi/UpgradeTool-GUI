/* libusb configuration for Apple's POSIX / Darwin backend. */
#ifndef LIBUSB_COMPAT_CONFIG_H
#define LIBUSB_COMPAT_CONFIG_H
#define PLATFORM_POSIX 1
#define HAVE_SYS_TIME_H 1
#define HAVE_SYSLOG 1
#define HAVE_NFDS_T 1
#define HAVE_PTHREAD_THREADID_NP 1
#define HAVE_IOKIT_USB_IOUSBHOSTFAMILYDEFINITIONS_H 1
#define ENABLE_LOGGING 1
#define ENABLE_DEBUG_LOGGING 1
#define USE_SYSTEM_LOGGING_FACILITY 1
#define DEFAULT_VISIBILITY __attribute__((visibility("default")))
#define PRINTF_FORMAT(a, b) __attribute__((format(printf, a, b)))
#endif
