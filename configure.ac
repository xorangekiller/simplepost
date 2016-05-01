AC_PREREQ([2.63])
AC_INIT([simplepost], [0.3.0], [xorangekiller@gmail.com])
AC_CONFIG_SRCDIR([src/simplepost.c])
AC_CONFIG_AUX_DIR([build-aux])
AM_INIT_AUTOMAKE([no-define])

# SP_REPLACE_CFLAG(PATTERN, FLAG)
# -------------------------------------
# Replace the given PATTERN with FLAG in $CFLAGS and $AM_CFLAGS
AC_DEFUN([SP_REPLACE_CFLAG],
    [if test "x$CFLAGS" != 'x'; then CFLAGS="$(eval echo \"$CFLAGS\" | sed -r 's/'"$1"'//g')"; fi;
        if test "x$AM_CFLAGS" != 'x'; then AM_CFLAGS="$(eval echo \"$AM_CFLAGS\" | sed -r 's/'"$1"'//g')"; fi
        AM_CFLAGS="$AM_CFLAGS $2"])

# Save the original flags passed in from the environment, to be used to make
# decisions later.
ORIG_CFLAGS="$CFLAGS"

# Check for programs.
AC_PROG_CC
AC_PROG_CPP
AC_PROG_INSTALL
AC_PROG_EGREP
AC_PROG_SED

# Configure the debug feature.
AC_ARG_ENABLE(debug, AS_HELP_STRING([--enable-debug], [disable optimizations and tune for debugging]),
    [case "${enableval}" in \
        yes|true)
            enable_debug=yes
            ;;
        no|false)
            enable_debug=no
            ;;
        *)
            AC_MSG_ERROR(bad value ${enableval} for --enable-debug)
            ;;
     esac], [enable_debug=no])
AM_CONDITIONAL([DEBUG], [test x$enable_debug = xyes])

AC_MSG_CHECKING([whether debug support was requested])
if test "x$enable_debug" = xyes; then
    AC_MSG_RESULT([yes])
    AS_IF([echo " $CPPFLAGS $AM_CPPFLAGS " | egrep -qs "\s+\-DEBUG\s+"], [], [AM_CPPFLAGS="$AM_CPPFLAGS -DDEBUG"])
    AS_IF([echo " $CFLAGS $AM_CFLAGS " | egrep -qs "\s+\-Wall\s+"], [], [AM_CFLAGS="$AM_CFLAGS -Wall"])
    AS_IF([echo " $CFLAGS $AM_CFLAGS " | egrep -qs "\s+\-Wextra\s+"], [], [AM_CFLAGS="$AM_CFLAGS -Wextra"])
    AS_IF([echo " $ORIG_CFLAGS " | egrep -qs "\s+\-O[[0-9]]+\s+"], [], [SP_REPLACE_CFLAG([\s*\-O[[0-9]]+], [-O0])])
    AS_IF([echo " $ORIG_CFLAGS " | egrep -qs "\s+\-(g[[0-9]]*|ggdb)\s+"], [], [SP_REPLACE_CFLAG([\s*\-(g[[0-9]]*|ggdb)], [-ggdb])])

    AC_SUBST([AM_CPPFLAGS])
    AC_SUBST([AM_CFLAGS])
else
    AC_MSG_RESULT([no])
fi

# Configure automatic coloring on versions of gcc that support it (4.9+).
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

# Configure Doxygen documentation.
DX_DOXYGEN_FEATURE(ON)
DX_DOT_FEATURE(ON)
DX_HTML_FEATURE(ON)
DX_MAN_FEATURE(OFF)
DX_PDF_FEATURE(OFF)
DX_PS_FEATURE(OFF)

my_save_srcdir="$srcdir"
srcdir="../src"
DX_INIT_DOXYGEN([SimplePost], [Doxyfile], [doxygen])
srcdir="$my_save_srcdir"

# Check for libraries.
AC_CHECK_LIB([magic], [magic_load],
    [AC_DEFINE([HAVE_LIBmagic], [1], [Define if you have libmagic.])
        LIBS="-lmagic $LIBS"],
    [AC_MSG_ERROR([libmagic not found.])])
AC_CHECK_LIB([microhttpd], [MHD_get_daemon_info],
    [AC_DEFINE([HAVE_LIBmicrohttpd], [1], [Define if you have libmicrohttpd.])
        LIBS="-lmicrohttpd $LIBS"],
    [AC_MSG_ERROR([libmicrohttpd not found.])])
AC_CHECK_LIB([pthread], [pthread_self],
    [AC_DEFINE([HAVE_LIBpthread], [1], [Define if you have libpthread.])
        LIBS="-lpthread $LIBS"],
    [AC_MSG_ERROR([libpthread not found.])])

# Check for header files.
AC_CHECK_HEADERS([arpa/inet.h  \
                  fcntl.h      \
                  limits.h     \
                  netdb.h      \
                  netinet/in.h \
                  stdlib.h     \
                  string.h     \
                  getopt.h     \
                  stdbool.h    \
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
AC_CHECK_FUNCS([memset      \
                regcomp     \
                socket      \
                strchr      \
                getopt      \
                getopt_long])

AC_CONFIG_HEADERS([config.h])
AC_CONFIG_HEADERS([src/config.h:src/config.in])

AC_OUTPUT([Makefile
           doc/Makefile
           man/Makefile
           src/Makefile])

# Print a summary of the enabled features after configuration.
echo "************************************************************"
echo "$PACKAGE_NAME $PACKAGE_VERSION will be built with following features:"
printf "  %-39s $enable_debug\n" "Debug build:"
printf "  %-39s " "Doxygen documentation:"
if test "$DX_FLAG_doc" = 1; then
    echo "yes"

    printf "  %-39s " "Graphics for Doxygen documentation:"
    AS_IF([test "$DX_FLAG_dot" = 1], [echo "yes"], [echo "no"])

    printf "  %-39s " "Doxygen manual pages:"
    AS_IF([test "$DX_FLAG_man" = 1], [echo "yes"], [echo "no"])

    printf "  %-39s " "Doxygen RTF documentation:"
    AS_IF([test "$DX_FLAG_rtf" = 1], [echo "yes"], [echo "no"])

    printf "  %-39s " "Doxygen XML documentation:"
    AS_IF([test "$DX_FLAG_xml" = 1], [echo "yes"], [echo "no"])

    printf "  %-39s " "Doxygen plain HTML documentation:"
    if test "$DX_FLAG_html" = 1; then
        echo "yes"

        printf "  %-39s " "Doxygen compressed HTML help:"
        if test "$DX_FLAG_chm" = 1; then
            echo "yes"

            printf "  %-39s " "Doxygen seperate HTML index file:"
            AS_IF([test "$DX_FLAG_chi" = 1], [echo "yes"], [echo "no"])
        else
            echo "no"
        fi
    else
        echo "no"
    fi

    printf "  %-39s " "Doxygen PostScript documentation:"
    AS_IF([test "$DX_FLAG_ps" = 1], [echo "yes"], [echo "no"])

    printf "  %-39s " "Doxygen PDF documentation:"
    AS_IF([test "$DX_FLAG_pdf" = 1], [echo "yes"], [echo "no"])
else
    echo "no"
fi
#printf "  %-39s $with_libmagic" "Content-Type support (libmagic):"
#printf "  %-39s $with_libconfig" "Configuration file support (libconfig):"
#printf "  %-39s $use_examples" "Build examples:"
#printf "  %-39s $enable_tests" "Build unit tests:"
echo "************************************************************"
