#ifndef WINPR_CONFIG_H
#define WINPR_CONFIG_H

/* Include files */
#define WINPR_HAVE_FCNTL_H
#define WINPR_HAVE_UNISTD_H
#define WINPR_HAVE_INTTYPES_H
#define WINPR_HAVE_STDBOOL_H
#define WINPR_HAVE_AIO_H
#ifdef __APPLE__
#define WINPR_HAVE_SYS_FILIO_H
#endif
#define WINPR_HAVE_SYS_SELECT_H
#ifdef __APPLE__
#define WINPR_HAVE_SYS_SOCKIO_H
#endif
/* #undef WINPR_HAVE_SYS_EVENTFD_H */
/* #undef WINPR_HAVE_SYS_TIMERFD_H */
#define WINPR_HAVE_TM_GMTOFF
#define WINPR_HAVE_AIO_H
#define WINPR_HAVE_POLL_H
#define WINPR_HAVE_SYSLOG_H
/* #undef WINPR_HAVE_JOURNALD_H */
#ifdef __linux__
#define WINPR_HAVE_PTHREAD_MUTEX_TIMEDLOCK
#endif
#define WINPR_HAVE_EXECINFO_H
#define WINPR_HAVE_GETLOGIN_R
#define WINPR_HAVE_GETPWUID_R
#define WINPR_HAVE_STRNDUP
#ifdef __APPLE__
#define WINPR_HAVE_STRNSTR
#endif
#define WINPR_HAVE_UNWIND_H
#define WINPR_HAVE_SSIZE_T
/* #undef WINPR_HAVE_WIN_SSIZE_T */
/* #undef WINPR_WITH_PNG */

#define WINPR_HAVE_STRERROR_R /** @since version 3.3.0 */

/* #undef WITH_EVENTFD_READ_WRITE */

/* #undef WITH_NATIVE_SSPI */
/* #undef WITH_INTERNAL_RC4 */
/* #undef WITH_INTERNAL_MD4 */
/* #undef WITH_INTERNAL_MD5 */

/* #undef WITH_WINPR_JSON */

/* #undef WITH_DEBUG_NTLM */
/* #undef WITH_DEBUG_THREADS */
/* #undef WITH_DEBUG_EVENTS */
/* #undef WITH_DEBUG_MUTEX */

/* #undef WINPR_UTILS_IMAGE_DIBv5 */
/* #undef WINPR_UTILS_IMAGE_WEBP */
/* #undef WINPR_UTILS_IMAGE_PNG */
/* #undef WINPR_UTILS_IMAGE_JPEG */

#if !defined(WITHOUT_WINPR_3x_DEPRECATED)
/* #undef WITHOUT_WINPR_3x_DEPRECATED */
#endif
#if !defined(WITH_WINPR_DEPRECATED)
/* #undef WITH_WINPR_DEPRECATED */
#endif

#endif /* WINPR_CONFIG_H */
