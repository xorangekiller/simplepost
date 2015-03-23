AC_PREREQ([2.63])
AC_INIT([simplepost], [0.3.0], [xorangekiller@gmail.com])
AC_CONFIG_SRCDIR([src/simplepost.c])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([no-define])

# Check for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_EGREP

# Configure features.
AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug], [disable optimizations and tune for debugging]),
    [case "${enableval}" in \
        yes|true)
            ENABLE_DEBUG=true
            ;;
        no|false)
            ENABLE_DEBUG=false
            ;;
        *)
            AC_MSG_ERROR(bad value ${enableval} for --enable-debug)
            ;;
     esac], [ENABLE_DEBUG=false])
AM_CONDITIONAL([DEBUG], [test x$ENABLE_DEBUG = xtrue])

AC_MSG_CHECKING([whether debug support was requested])
if test "x$ENABLE_DEBUG" = xtrue; then
    AC_MSG_RESULT([yes])
    AS_IF([echo " $CPPFLAGS $AM_CPPFLAGS " | egrep -qs "\s+\-DEBUG\s+"], [], [AM_CPPFLAGS="$AM_CPPFLAGS -DDEBUG"])
    AS_IF([echo " $CFLAGS $AM_CFLAGS " | egrep -qs "\s+\-Wall\s+"], [], [AM_CFLAGS="$AM_CFLAGS -Wall"])
    AS_IF([echo " $CFLAGS $AM_CFLAGS " | egrep -qs "\s+\-Wextra\s+"], [], [AM_CFLAGS="$AM_CFLAGS -Wextra"])
    AS_IF([echo " $CFLAGS $AM_CFLAGS " | egrep -qs "\s+\-(g|ggdb)\s+"], [], [AM_CFLAGS="$AM_CFLAGS -ggdb"])

    AC_SUBST([AM_CPPFLAGS])
    AC_SUBST([AM_CFLAGS])
else
    AC_MSG_RESULT([no])
fi

my_save_cflags="$CFLAGS"
CFLAGS=-fdiagnostics-color=auto
AC_MSG_CHECKING([whether the C compiler supports -fdiagnostics-color=auto])
AC_COMPILE_IFELSE([AC_LANG_PROGRAM([])],
    [AC_MSG_RESULT([yes])]
    [AM_CFLAGS="$AM_CFLAGS -fdiagnostics-color=auto"],
    [AC_MSG_RESULT([no])]
)
CFLAGS="$my_save_cflags"
AC_SUBST([AM_CFLAGS])

# Check for libraries.
AC_CHECK_LIB([magic], [magic_load])
AC_CHECK_LIB([microhttpd], [MHD_get_daemon_info])
AC_CHECK_LIB([pthread], [pthread_self])

# Check for header files.
AC_CHECK_HEADERS([arpa/inet.h  \
                  fcntl.h      \
                  limits.h     \
                  netdb.h      \
                  netinet/in.h \
                  stdlib.h     \
                  string.h     \
                  sys/socket.h \
                  sys/time.h   \
                  unistd.h])

# Check for typedefs, structures, and compiler characteristics.
AC_PROG_CC_C99
AC_TYPE_PID_T
AC_TYPE_SIZE_T

# Check for library functions.
AC_FUNC_MALLOC
AC_FUNC_REALLOC
AC_CHECK_FUNCS([memset  \
                regcomp \
                socket  \
                strchr])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_HEADERS([src/config.h:src/config.in])

AC_OUTPUT([Makefile
           man/Makefile
           src/Makefile])
