AC_INIT(lockfile.c)
AC_CONFIG_HEADER(autoconf.h)
AC_REVISION($Revision: 1.0 $)dnl

dnl Check for GNU cc
AC_PROG_CC

dnl Set Default CFLAGS
if test "$GCC" = "yes"; then
    CFLAGS="$CFLAGS -Wall -D_GNU_SOURCE"
fi

TARGETS="static"
INSTALL_TARGETS="install_static"

dnl extra argument: --enable-shared
AC_MSG_CHECKING(build shared libs)
AC_ARG_ENABLE(shared,
[  --enable-shared         Build shared libraries],
[ case "$enableval" in
    no)
	SHARED=no
	;;
    *)
	SHARED=yes
	CFLAGS="$CFLAGS -fPIC -D_REENTRANT"
	TARGETS="shared"
	INSTALL_TARGETS="install_shared"
	;;
  esac ],
  SHARED=no
)
AC_SUBST(SHARED)
AC_MSG_RESULT($SHARED)

dnl extra argument: --with-libnfslock
nfslockdir="$libdir"
AC_MSG_CHECKING(build libnfslock)
AC_ARG_WITH(libnfslock,
[  --with-libnfslock[=DIR] Build libnfslock (and install in DIR)],
[ case "$withval" in
    no)
	LIBNFSLOCK=no
	;;
    *)
	test "$withval" != "no" && nfslockdir="$withval"
	LIBNFSLOCK=yes
	TARGETS="$TARGETS nfslib"
	INSTALL_TARGETS="$INSTALL_TARGETS install_nfslib"
	;;
  esac ],
  LIBNFSLOCK=no
)
AC_SUBST(LIBNFSLOCK)
AC_MSG_RESULT($LIBNFSLOCK)

dnl extra argument: --with-mailgroup[=mail]
AC_ARG_WITH(mailgroup,
[  --with-mailgroup[=group] Run setgid (mail) to write to the mailspool],
[ case "$withval" in
    yes)
	MAILGROUP="mail"
	;;
    *)
	MAILGROUP="$withval"
	;;
  esac ]
)
if test "$MAILGROUP" != ""; then
    AC_DEFINE_UNQUOTED(MAILGROUP, "$MAILGROUP")
fi
AC_SUBST(MAILGROUP)

dnl Find ldconfig.
AC_PATH_PROG(LDCONFIG, ldconfig,, $PATH:/sbin:/usr/sbin)

dnl Check for headers
AC_HEADER_STDC

AC_CHECK_HEADERS( \
	getopt.h \
	paths.h \
	sys/param.h
)

AC_CHECK_FUNCS( \
	utime \
	utimes \
)

if test "$ac_cv_header_paths_h" != "yes";
then
    dnl Find out where the mail spool is.
    AC_MSG_CHECKING(location of your mail spool)
    MAILSPOOL=/var/spool/mail
    for dir in /var/mail /var/spool/mail /usr/mail /usr/spool/mail
    do
	if test -d $dir; then
	    MAILSPOOL=$dir
	    break
	fi
    done
    PATHMAILDIR="#define _PATH_MAILDIR \"$MAILSPOOL\""
    AC_MSG_RESULT($MAILSPOOL)
else
    PATHMAILDIR="#include <paths.h>"
fi
AC_SUBST(PATHMAILDIR)

AC_SUBST(TARGETS)
AC_SUBST(INSTALL_TARGETS)
AC_SUBST(nfslockdir)

AC_OUTPUT(\
	./Makefile \
	./maillock.h \
)
