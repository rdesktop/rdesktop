AC_INIT(rdesktop, 1.3.1)

AC_CONFIG_SRCDIR([rdesktop.c])

AC_PROG_CC
if test "$GCC" = yes; then
    CFLAGS="$CFLAGS -Wall"
fi

AC_PROG_INSTALL
AC_LANG_C
AC_HEADER_STDC
AC_C_BIGENDIAN([AC_DEFINE(B_ENDIAN)], [AC_DEFINE(L_ENDIAN)])
AC_PATH_XTRA

AC_SEARCH_LIBS(socket, socket)
AC_SEARCH_LIBS(inet_aton, resolv)

rpath=""

#
# OpenSSL detection borrowed from stunnel
#
checkssldir() { :
    if test -f "$1/include/openssl/ssl.h"; then
        ssldir="$1"
        return 0
    fi
    return 1
}
AC_MSG_CHECKING([for OpenSSL directory])
AC_ARG_WITH(openssl,
    [  --with-openssl=DIR          look for OpenSSL at DIR/include, DIR/lib],
    [
        dnl Check the specified location only
        checkssldir "$withval"
    ],
    [
        dnl Search default locations of OpenSSL library
        for maindir in /usr/local /usr/lib /usr/pkg /usr /var/ssl /opt; do
            for dir in $maindir $maindir/openssl $maindir/ssl; do
                checkssldir $dir && break 2
            done
        done
    ]
)
if test -z "$ssldir"; then
    AC_MSG_RESULT([Not found])
    echo
    echo "Couldn't find your OpenSSL library installation dir"
    echo "Use --with-openssl option to fix this problem"
    echo
    exit 1
fi
AC_MSG_RESULT([$ssldir])
AC_SUBST(ssldir)
AC_DEFINE_UNQUOTED(ssldir, "$ssldir")

dnl Add OpenSSL includes and libraries
CFLAGS="$CFLAGS -I$ssldir/include"
AC_ARG_ENABLE(static-openssl, 
             [  --enable-static-openssl link OpenSSL statically],
             [ 
LIBS="$LIBS $ssldir/lib/libcrypto.a" 
             ],
             [ 
LIBS="$LIBS -L$ssldir/lib -lcrypto" 
rpath="$rpath:$ssldir/lib"
             ])


#
# Alignment
#
AC_MSG_CHECKING([if architecture needs alignment])
AC_TRY_RUN([
#include <stdlib.h>
#include <signal.h>
int main(int argc, char **argv)
{
	unsigned char test[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
	signal(SIGBUS, exit);
	signal(SIGABRT, exit);
	signal(SIGSEGV, exit);
	if (*((unsigned int *)(test + 1)) != 0x55443322 && *((unsigned int *)(test + 1)) != 0x22334455) {
		return 1;
	}
	return 0;
}], 
 [AC_MSG_RESULT(no)], 
 [AC_MSG_RESULT(yes) 
  AC_DEFINE(NEED_ALIGN)], 
 [AC_MSG_RESULT(assuming yes) 
  AC_DEFINE(NEED_ALIGN)])


#
# EGD
#
AC_ARG_WITH(egd-socket,
    [  --with-egd-socket=PATH  look for Entropy Gathering Daemon socket at PATH],
    [EGD_SOCKET="$withval"],
    [EGD_SOCKET="/var/run/egd-pool"]
)
AC_DEFINE_UNQUOTED(EGD_SOCKET, "$EGD_SOCKET")


#
# rdp2vnc
#
vncserverconfig=libvncserver-config
AC_ARG_WITH(libvncserver-config,
    [  --with-libvncserver-config=CMD  use CMD as libvncserver-config],
    [vncserverconfig="$withval"]
)
AC_ARG_WITH(libvncserver,
    [  --with-libvncserver     make rdp2vnc],
    [ 
    VNCINC=`$vncserverconfig --cflags`
    AC_SUBST(VNCINC)
    LDVNC=`$vncserverconfig --libs`
    AC_SUBST(LDVNC)
    VNCLINK=`$vncserverconfig --link`
    AC_SUBST(VNCLINK)
    RDP2VNCTARGET="rdp2vnc"
    AC_SUBST(RDP2VNCTARGET)
    ]
)

#
# sound
#
sound="yes"
AC_ARG_WITH(sound,
    [  --with-sound            select sound system ("oss" or "sun") ],
    [ 
    sound="$withval" 
    ])
if test "$sound" = yes; then
    AC_CHECK_HEADER(sys/soundcard.h, [sound=oss])
    AC_CHECK_HEADER(sys/audioio.h, [sound=sun])
fi
if test "$sound" = no; then
    break
elif test "$sound" = oss; then
    SOUNDOBJ="rdpsnd.o rdpsnd_oss.o"
    AC_DEFINE(WITH_RDPSND)
elif test "$sound" = sun; then
    SOUNDOBJ="rdpsnd.o rdpsnd_sun.o"
    AC_DEFINE(WITH_RDPSND)
else
   AC_MSG_WARN([sound support disabled (no sys/soundcard.h or sys/audioio.h)])
   AC_MSG_WARN([Currently supported systems are Open Sound System (oss) and Sun/BSD (sun)])
fi
AC_SUBST(SOUNDOBJ)

#
# dirfd
#
UTILS_FUNC_DIRFD

#
# debugging
#
AC_ARG_WITH(debug,
    [  --with-debug            enable protocol debugging output],
    [ AC_DEFINE(WITH_DEBUG) ])

AC_ARG_WITH(debug-kbd,
    [  --with-debug-kbd        enable debugging of keyboard handling],
    [ AC_DEFINE(WITH_DEBUG_KBD) ])

AC_ARG_WITH(debug-rdp5,
    [  --with-debug-rdp5       enable debugging of RDP5 code],
    [ AC_DEFINE(WITH_DEBUG_RDP5) ])

AC_ARG_WITH(debug-clipboard,
    [  --with-debug-clipboard  enable debugging of clipboard code],
    [ AC_DEFINE(WITH_DEBUG_CLIPBOARD) ])


#
# target-specific stuff
#
# strip leading colon from rpath
rpath=`echo $rpath |sed 's/^://'`
AC_CANONICAL_HOST
case "$host" in
*-*-solaris*)
    LDFLAGS="$LDFLAGS -R$rpath"
    ;;
*-dec-osf*)
    LDFLAGS="$LDFLAGS -Wl,-rpath,$rpath"
    ;;
*-*-hpux*)
    CFLAGS="$CFLAGS -D_XOPEN_SOURCE_EXTENDED"
    ;;
esac


AC_OUTPUT(Makefile)

dnl Local Variables:
dnl comment-start: "dnl "
dnl comment-end: ""
dnl comment-start-skip: "\\bdnl\\b\\s *"
dnl compile-command: "autoconf"
dnl End:
