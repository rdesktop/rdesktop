AC_PREREQ([2.69])
AC_INIT([libnlav2], [2.0], [sedwards@users.noreturn.github.com])
AC_CONFIG_SRCDIR([nlav2.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL

# Detect Host OS for shared library suffix and linking flags
AC_CANONICAL_HOST
case "$host_os" in
  darwin*)
    PLATFORM=macos
    WINPR_TIMEZONE_FILE=/var/db/zoneinfo
    PLATFORM_DEFS="-D_DARWIN_C_SOURCE"
    SHLIB_EXT="dylib"
    SHLIB_FLAGS="-dynamiclib -Wl,-undefined,dynamic_lookup"
    LIBS="$LIBS -framework CoreFoundation -framework Carbon -framework Foundation"
    ;;
  *)
    PLATFORM=linux
    WINPR_TIMEZONE_FILE=/etc/timezone
    PLATFORM_DEFS="-D_DEFAULT_SOURCE"
    SHLIB_EXT="so"
    SHLIB_FLAGS="-shared -fPIC"
    ;;
esac

AC_SUBST([PLATFORM])
AC_SUBST([WINPR_TIMEZONE_FILE])
AC_SUBST([PLATFORM_DEFS])
AC_SUBST([SHLIB_EXT])
AC_SUBST([SHLIB_FLAGS])

# Checks for libraries.
# Check for local static OpenSSL
AC_MSG_CHECKING([for local static OpenSSL])
if test -f "$srcdir/deps/openssl-3.0.15/libcrypto.a"; then
    AC_MSG_RESULT([yes])
    OPENSSL_CFLAGS="-I\$(srcdir)/deps/openssl-3.0.15/include"
    OPENSSL_LIBS="\$(srcdir)/deps/openssl-3.0.15/libssl.a \$(srcdir)/deps/openssl-3.0.15/libcrypto.a"
    AC_DEFINE([WITH_OPENSSL], [1], [Define if OpenSSL is available])
else
    AC_MSG_RESULT([no])
    AC_MSG_ERROR([Local static OpenSSL not found at deps/openssl-3.0.15/libcrypto.a])
fi

AC_SUBST([OPENSSL_CFLAGS])
AC_SUBST([OPENSSL_LIBS])

# Check for pthreads
AC_CHECK_LIB([pthread], [pthread_create], [
    PTHREAD_LIBS="-lpthread"
], [
    AC_MSG_ERROR([pthread library not found])
])
AC_SUBST([PTHREAD_LIBS])

# Check for dl (dynamic loader)
AC_CHECK_LIB([dl], [dlopen], [
    DL_LIBS="-ldl"
], [
    DL_LIBS=""
])
AC_SUBST([DL_LIBS])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stddef.h stdlib.h string.h sys/socket.h unistd.h openssl/evp.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_SIZE_T
AC_TYPE_SSIZE_T
AC_TYPE_UINT16_T
AC_TYPE_UINT32_T
AC_TYPE_UINT64_T
AC_TYPE_UINT8_T

# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset socket strcasecmp strdup strerror])

AC_CONFIG_FILES([Makefile])
AC_OUTPUT
