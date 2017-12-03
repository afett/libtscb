# Process this file with autoconf to produce a configure script.
PACKAGE=libtscb

AC_INIT(src, 0.9.9, hcb@chaoticmind.net)
AC_CONFIG_SRCDIR(.)

VERSION=0.9.9

AC_CANONICAL_HOST
case "$host_os" in
	darwin*) PORTNAME=darwin ;;
	linux*) PORTNAME=linux ;;
	freebsd*) PORTNAME=freebsd ;;
esac
AC_SUBST(PORTNAME)

AC_CONFIG_HEADERS([include/tscb/config:include/tscb/config.in])

AC_PROG_CXX
AC_PROG_INSTALL

# Check for system features
AC_SYS_LARGEFILE
if test "$ac_cv_sys_file_offset_bits" != "" -a "$ac_cv_sys_file_offset_bits" != "no"; then
  CPPFLAGS="$CPPFLAGS -D_FILE_OFFSET_BITS=$ac_cv_sys_file_offset_bits"
fi

# Checks for libraries.
ACX_PTHREAD

# allow to disable asserts
AC_HEADER_ASSERT

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([sys/poll.h sys/event.h sys/epoll.h])

# Checks for library functions.
AC_CHECK_FUNCS([gettimeofday])

AC_CHECK_FUNC(poll,[
AC_DEFINE(HAVE_POLL,1,[Check if poll is defined])
],[])

AC_CHECK_FUNC(epoll_create,[
AC_DEFINE(HAVE_EPOLL,1,[Check if epoll_create is defined])
DISPATCHER_EPOLL=yes
],[])

AC_CHECK_FUNC(epoll_create1,[
AC_DEFINE(HAVE_EPOLL1,1,[Check if epoll_create1 is defined])
DISPATCHER_EPOLL=yes
],[])

AC_CHECK_FUNC(pipe2,[
AC_DEFINE(HAVE_PIPE2,1,[Check if pipe2 is defined])
DISPATCHER_EPOLL=yes
],[])

# Optional features
AC_ARG_WITH(fdsetsize,[  --with-fdsetsize=arg    Override FD_SETSIZE (for the nutters who are running Mac OS <10.3)],[
SELECT_FD_SETSIZE=$withval
])

AC_ARG_ENABLE(shared,[  --enable-shared        Build shared library version of libtscb],[
ENABLE_SHARED=yes
],[])

AC_SUBST(DISPATCHER_EPOLL)
AC_SUBST(SELECT_FD_SETSIZE)
AC_SUBST(VERSION)
AC_SUBST(ENABLE_SHARED)

# Generate output files
AC_CONFIG_FILES([Makefile])
AC_CONFIG_FILES([libtscb.pc])
AC_OUTPUT
