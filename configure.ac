AC_INIT(rdesktop, 1.4.0)

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

AC_CHECK_HEADER(sys/select.h, AC_DEFINE(HAVE_SYS_SELECT_H))
AC_CHECK_HEADER(sys/modem.h, AC_DEFINE(HAVE_SYS_MODEM_H))
AC_CHECK_HEADER(sys/filio.h, AC_DEFINE(HAVE_SYS_FILIO_H))
AC_CHECK_HEADER(sys/strtio.h, AC_DEFINE(HAVE_SYS_STRTIO_H))
AC_CHECK_HEADER(locale.h, AC_DEFINE(HAVE_LOCALE_H))
AC_CHECK_HEADER(langinfo.h, AC_DEFINE(HAVE_LANGINFO_H))
AC_CHECK_HEADER(iconv.h, AC_DEFINE(HAVE_ICONV_H))

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
AC_ARG_WITH(libao,
    [  --with-libao=DIR        look for libao at DIR/include, DIR/lib],
    [
    CFLAGS="$CFLAGS -I$withval/include"
    CPPFLAGS="$CPPFLAGS -I$withval/include"
    LIBS="$LIBS -L$withval/lib"
    rpath="$rpath:$withval/lib"
    ]
)

sound="yes"
AC_ARG_WITH(sound,
    [  --with-sound            select sound system ("oss", "sgi", "sun" or "libao") ],
    [ 
    sound="$withval" 
    ])
if test "$sound" = yes; then
    AC_CHECK_HEADER(ao/ao.h, [sound=libao])
    AC_CHECK_HEADER(sys/soundcard.h, [sound=oss])
    AC_CHECK_HEADER(dmedia/audio.h, [sound=sgi])
    AC_CHECK_HEADER(sys/audioio.h, [sound=sun])
fi
if test "$sound" = no; then
    break
elif test "$sound" = oss; then
    SOUNDOBJ="rdpsnd.o rdpsnd_oss.o"
    AC_DEFINE(WITH_RDPSND)
elif test "$sound" = sgi; then
    SOUNDOBJ="rdpsnd.o rdpsnd_sgi.o"
    LDFLAGS="$LDFLAGS -laudio"
    AC_DEFINE(WITH_RDPSND)
elif test "$sound" = sun; then
    SOUNDOBJ="rdpsnd.o rdpsnd_sun.o"
    AC_DEFINE(WITH_RDPSND)
elif test "$sound" = libao; then
    SOUNDOBJ="rdpsnd.o rdpsnd_libao.o"
    LDFLAGS="$LDFLAGS -lao"
    AC_DEFINE(WITH_RDPSND)
else
   AC_MSG_WARN([sound support disabled])
   AC_MSG_WARN([Currently supported systems are Open Sound System (oss), SGI AL (sgi), Sun/BSD (sun) and libao])
fi
AC_SUBST(SOUNDOBJ)

#
# dirfd
#
dnl Find out how to get the file descriptor associated with an open DIR*.
dnl From Jim Meyering

AC_DEFUN([UTILS_FUNC_DIRFD],
[

  AC_HEADER_DIRENT
  dirfd_headers='
#if HAVE_DIRENT_H
# include <dirent.h>
#else /* not HAVE_DIRENT_H */
# define dirent direct
# if HAVE_SYS_NDIR_H
#  include <sys/ndir.h>
# endif /* HAVE_SYS_NDIR_H */
# if HAVE_SYS_DIR_H
#  include <sys/dir.h>
# endif /* HAVE_SYS_DIR_H */
# if HAVE_NDIR_H
#  include <ndir.h>
# endif /* HAVE_NDIR_H */
#endif /* HAVE_DIRENT_H */
'
  AC_CHECK_FUNCS(dirfd)
  AC_CHECK_DECLS([dirfd], , , $dirfd_headers)

  AC_CACHE_CHECK([whether dirfd is a macro],
    jm_cv_func_dirfd_macro,
    [AC_EGREP_CPP([dirent_header_defines_dirfd], [$dirfd_headers
#ifdef dirfd
 dirent_header_defines_dirfd
#endif],
       jm_cv_func_dirfd_macro=yes,
       jm_cv_func_dirfd_macro=no)])

  # Use the replacement only if we have no function, macro,
  # or declaration with that name.
  if test $ac_cv_func_dirfd,$ac_cv_have_decl_dirfd,$jm_cv_func_dirfd_macro \
      = no,no,no; then
    AC_REPLACE_FUNCS([dirfd])
    AC_CACHE_CHECK(
	      [how to get the file descriptor associated with an open DIR*],
		   gl_cv_sys_dir_fd_member_name,
      [
        dirfd_save_CFLAGS=$CFLAGS
	for ac_expr in d_fd dd_fd; do

	  CFLAGS="$CFLAGS -DDIR_FD_MEMBER_NAME=$ac_expr"
	  AC_TRY_COMPILE(
	    [$dirfd_headers
	    ],
	    [DIR *dir_p = opendir("."); (void) dir_p->DIR_FD_MEMBER_NAME;],
	    dir_fd_found=yes
	  )
	  CFLAGS=$dirfd_save_CFLAGS
	  test "$dir_fd_found" = yes && break
	done
	test "$dir_fd_found" = yes || ac_expr=no_such_member

	gl_cv_sys_dir_fd_member_name=$ac_expr
      ]
    )
    if test $gl_cv_sys_dir_fd_member_name != no_such_member; then
      AC_DEFINE_UNQUOTED(DIR_FD_MEMBER_NAME,
	$gl_cv_sys_dir_fd_member_name,
	[the name of the file descriptor member of DIR])
    fi
    AH_VERBATIM(DIR_TO_FD,
		[#ifdef DIR_FD_MEMBER_NAME
# define DIR_TO_FD(Dir_p) ((Dir_p)->DIR_FD_MEMBER_NAME)
#else
# define DIR_TO_FD(Dir_p) -1
#endif
]
    )
  fi
])

UTILS_FUNC_DIRFD

#
# iconv
#

dnl This macros shamelessly stolen from
dnl http://gcc.gnu.org/ml/gcc-bugs/2001-06/msg01398.html.
dnl Written by Bruno Haible.

AC_DEFUN([UTILS_FUNC_ICONV],
[
  dnl Some systems have iconv in libc, some have it in libiconv (OSF/1 and
  dnl those with the standalone portable GNU libiconv installed).

  AC_ARG_WITH([libiconv-prefix],
[  --with-libiconv-prefix=DIR  search for libiconv in DIR/include and DIR/lib], [
    for dir in `echo "$withval" | tr : ' '`; do
      if test -d $dir/include; then CPPFLAGS="$CPPFLAGS -I$dir/include"; fi
      if test -d $dir/lib; then LDFLAGS="$LDFLAGS -L$dir/lib"; fi
    done
   ])

  AC_CACHE_CHECK(for iconv, am_cv_func_iconv, [
    am_cv_func_iconv="no, consider installing GNU libiconv"
    am_cv_lib_iconv=no
    AC_TRY_LINK([#include <stdlib.h>
#include <iconv.h>],
      [iconv_t cd = iconv_open("","");
       iconv(cd,NULL,NULL,NULL,NULL);
       iconv_close(cd);],
      am_cv_func_iconv=yes)
    if test "$am_cv_func_iconv" != yes; then
      am_save_LIBS="$LIBS"
      LIBS="$LIBS -liconv"
      AC_TRY_LINK([#include <stdlib.h>
#include <iconv.h>],
        [iconv_t cd = iconv_open("","");
         iconv(cd,NULL,NULL,NULL,NULL);
         iconv_close(cd);],
        am_cv_lib_iconv=yes
        am_cv_func_iconv=yes)
      LIBS="$am_save_LIBS"
    fi
  ])
  if test "$am_cv_func_iconv" = yes; then
    AC_DEFINE(HAVE_ICONV, 1, [Define if you have the iconv() function.])
    AC_MSG_CHECKING([for iconv declaration])
    AC_CACHE_VAL(am_cv_proto_iconv, [
      AC_TRY_COMPILE([
#include <stdlib.h>
#include <iconv.h>
extern
#ifdef __cplusplus
"C"
#endif
#if defined(__STDC__) || defined(__cplusplus)
size_t iconv (iconv_t cd, char * *inbuf, size_t *inbytesleft, char * *outbuf, size_t *outbytesleft);
#else
size_t iconv();
#endif
], [], am_cv_proto_iconv_arg1="", am_cv_proto_iconv_arg1="const")
      am_cv_proto_iconv="extern size_t iconv (iconv_t cd, $am_cv_proto_iconv_arg1 char * *inbuf, size_t *inbytesleft, char * *outbuf, size_t *outbytesleft);"])
    am_cv_proto_iconv=`echo "[$]am_cv_proto_iconv" | tr -s ' ' | sed -e 's/( /(/'`
    AC_MSG_RESULT([$]{ac_t:-
         }[$]am_cv_proto_iconv)
    AC_DEFINE_UNQUOTED(ICONV_CONST, $am_cv_proto_iconv_arg1,
      [Define as const if the declaration of iconv() needs const.])
  fi
  LIBICONV=
  if test "$am_cv_lib_iconv" = yes; then
    LIBICONV="-liconv"
  fi
  AC_SUBST(LIBICONV)
])

UTILS_FUNC_ICONV
LIBS="$LIBS $LIBICONV"

#
# socklen_t
# from curl

dnl Check for socklen_t: historically on BSD it is an int, and in
dnl POSIX 1g it is a type of its own, but some platforms use different
dnl types for the argument to getsockopt, getpeername, etc.  So we
dnl have to test to find something that will work.
AC_DEFUN([TYPE_SOCKLEN_T],
[
   AC_CHECK_TYPE([socklen_t], ,[
      AC_MSG_CHECKING([for socklen_t equivalent])
      AC_CACHE_VAL([socklen_t_equiv],
      [
         # Systems have either "struct sockaddr *" or
         # "void *" as the second argument to getpeername
         socklen_t_equiv=
         for arg2 in "struct sockaddr" void; do
            for t in int size_t unsigned long "unsigned long"; do
               AC_TRY_COMPILE([
                  #include <sys/types.h>
                  #include <sys/socket.h>

                  int getpeername (int, $arg2 *, $t *);
               ],[
                  $t len;
                  getpeername(0,0,&len);
               ],[
                  socklen_t_equiv="$t"
                  break
               ])
            done
         done

         if test "x$socklen_t_equiv" = x; then
            AC_MSG_ERROR([Cannot find a type to use in place of socklen_t])
         fi
      ])
      AC_MSG_RESULT($socklen_t_equiv)
      AC_DEFINE_UNQUOTED(socklen_t, $socklen_t_equiv,
			[type to use in place of socklen_t if not defined])],
      [#include <sys/types.h>
#include <sys/socket.h>])
])

TYPE_SOCKLEN_T

#
# IPv6
#
AC_ARG_WITH(ipv6,
    [  --with-ipv6             enable IPv6-support],
    [ AC_DEFINE(IPv6) ])


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
*-*-irix6.5*)
    LIBS="$LIBS -L$ssldir/lib32 -lcrypto"
    CFLAGS="$CFLAGS -D__SGI_IRIX__"
    ;;
esac


AC_OUTPUT(Makefile)

dnl Local Variables:
dnl comment-start: "dnl "
dnl comment-end: ""
dnl comment-start-skip: "\\bdnl\\b\\s *"
dnl compile-command: "autoconf"
dnl End:
