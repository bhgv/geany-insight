dnl Process this file with autoconf to produce a configure script.

AC_PREREQ([2.60])
AC_INIT([Geany], [2.2.0],
		[https://github.com/geany/geany/issues])
AC_CONFIG_SRCDIR([src/geany.h])
AC_CONFIG_AUX_DIR([build-aux])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([1.11 -Wall parallel-tests subdir-objects])
AC_CONFIG_HEADERS([config.h])

# Silence "AC_LINK_IFELSE before AC_USE_SYSTEM_EXTENSIONS" warnings
# Note: Enables _GNU_SOURCE on Linux
AC_USE_SYSTEM_EXTENSIONS

AM_GNU_GETTEXT_VERSION([0.19.8])
AM_GNU_GETTEXT([external])

m4_ifdef([AM_SILENT_RULES],[AM_SILENT_RULES([yes])])

GEANY_PREFIX
GEANY_DOCDIR

GEANY_STATUS_ADD([Install Geany in], [${prefix}])
if test -n "${build}" -a -n "${target}"; then
	GEANY_STATUS_ADD([Building Geany on], [${build}])
	GEANY_STATUS_ADD([Building Geany for], [${target}])
fi

m4_ifdef([AM_PROG_AR],[AM_PROG_AR])
LT_INIT([disable-static])
AC_PROG_CC
AM_PROG_CC_C_O
AC_PROG_CXX
GEANY_PROG_CXX
AX_CXX_COMPILE_STDCXX_17

AC_PROG_INSTALL
AC_PROG_LN_S

if test "$ac_cv_prog_cc_c99" = "no"; then
	AC_MSG_ERROR([Need a C99 compiler])
fi

# autoscan start

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdlib.h sys/time.h errno.h limits.h])

# Checks for dependencies needed by ctags
AC_CHECK_HEADERS([dirent.h fnmatch.h direct.h io.h sys/dir.h])
AC_DEFINE([HAVE_STDBOOL_H], [1], [whether or not to use <stdbool.h>.])
AC_CANONICAL_HOST
AS_CASE([${host_os}],
        [darwin*], [dnl Using builtin regex on macOS for performance reasons
                    have_regcomp=no],
        [AC_CHECK_FUNC([regcomp],
                       [have_regcomp=yes],
                       [have_regcomp=no
                        dnl various stuff for ctags/gnu_regex/
                        AC_CHECK_HEADERS([langinfo.h locale.h libintl.h wctype.h wchar.h])
                        AC_CHECK_FUNCS([memcpy isblank wcrtomb mbrtowc wcscoll])
                        AC_FUNC_ALLOCA])])
AM_CONDITIONAL([USE_BUNDLED_REGEX], [test "xno" = "x$have_regcomp"])
AC_CHECK_FUNC([fnmatch], [have_fnmatch=yes], [have_fnmatch=no])
AM_CONDITIONAL([USE_BUNDLED_FNMATCH], [test "xno" = "x$have_fnmatch"])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
AC_STRUCT_TM

# Checks for library functions.
AC_CHECK_FUNCS([realpath])

# Function checks for u-ctags
AC_CHECK_FUNCS([strerror strstr asprintf])
AC_CHECK_FUNCS([mkstemp tempnam], [break])
AC_CHECK_FUNCS([strcasecmp stricmp], [break])
AC_CHECK_FUNCS([strncasecmp strnicmp], [break])
AC_CHECK_FUNCS([truncate ftruncate chsize], [break])
# non-functions checks for u-ctags.  Not that we really need those as we don't
# use u-ctags's main, but the corresponding macros have to be defined to
# something, so simply perform the actual checks.
AC_CHECK_DECLS([__environ],,,[[#include <unistd.h>]])
AC_CHECK_DECLS([_NSGetEnviron],,,[[#include <crt_externs.h>]])

# autoscan end


# check for VCS revision
GEANY_CHECK_REVISION([dnl force debug mode for a VCS working copy
					  CFLAGS="-g -DGEANY_DEBUG $CFLAGS"])

GEANY_CHECK_GTK
dnl Require compiler semantics unless a minimal build is explicitly requested.
AC_ARG_WITH([clang],AS_HELP_STRING([--with-clang@<:@=auto/yes/no/PREFIX@:>@],[use native Clang C++ semantic analysis]),[],[with_clang=yes])
AC_ARG_WITH([libclang],AS_HELP_STRING([--with-libclang],[deprecated alias for --with-clang; never links libclang]),[with_clang=$with_libclang])
AS_IF([test "x$with_clang" != xno],[
 AC_PATH_PROGS([LLVM_CONFIG],[llvm-config llvm-config-22 llvm-config-21 llvm-config-20 llvm-config-19 llvm-config-18 llvm-config-17 llvm-config-16 llvm-config-15 llvm-config-14])
 clang_cppflags="";clang_ldflags=""
 AS_CASE([$with_clang],[auto|yes],[AS_IF([test -n "$LLVM_CONFIG"],[clang_cppflags="-I`$LLVM_CONFIG --includedir`";clang_ldflags="-L`$LLVM_CONFIG --libdir`"])],[clang_cppflags="-I$with_clang/include";clang_ldflags="-L$with_clang/lib"])
 clang_save_cppflags="$CPPFLAGS";clang_save_ldflags="$LDFLAGS";clang_save_libs="$LIBS";clang_save_cxxflags="$CXXFLAGS"
 CPPFLAGS="$CPPFLAGS $clang_cppflags";LDFLAGS="$LDFLAGS $clang_ldflags";LIBS="$LIBS -lclang-cpp -lLLVM";CXXFLAGS="$CXXFLAGS -fno-rtti"
 AC_LANG_PUSH([C++])
 AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <clang/Tooling/Tooling.h>]],[[auto ast=clang::tooling::buildASTFromCode("int value;");]])],[clang_library=yes],[clang_library=no])
 AC_LANG_POP([C++])
 CPPFLAGS="$clang_save_cppflags";LDFLAGS="$clang_save_ldflags";LIBS="$clang_save_libs";CXXFLAGS="$clang_save_cxxflags"
 AS_IF([test x$clang_library = xyes],[AC_DEFINE([HAVE_CLANG_CPP],[1],[Use native Clang C++ semantics])
 GTK_CFLAGS="$GTK_CFLAGS $clang_cppflags";GTK_LIBS="$GTK_LIBS $clang_ldflags -lclang-cpp -lLLVM"],[AS_IF([test x$with_clang != xauto],[AC_MSG_ERROR([Native Clang C++ headers/libraries unavailable. Build external/llvm-project or install Clang development headers, libclang-cpp-dev and llvm-dev. Minimal builds use --without-clang.])])])
])
GEANY_CHECK_GTK_FUNCS([g_strv_equal])

# --disable-deprecated switch for GTK purification
AC_ARG_ENABLE([deprecated],
		[AS_HELP_STRING([--disable-deprecated], [Disable deprecated GTK functions.])],
		[GTK_CFLAGS="$GTK_CFLAGS -DG_DISABLE_DEPRECATED -DGTK_DISABLE_DEPRECATED"],
		[])


# Check for binary relocation support
GEANY_CHECK_BINRELOC
# CTags source compatibility (we actually use GRegex instead of POSIX regcomp)
AC_DEFINE([HAVE_REGCOMP], [1], [Should always be 1, required for CTags.])
# Plugins support
GEANY_CHECK_PLUGINS
# check for mingw specific settings
GEANY_CHECK_MINGW

GEANY_CHECK_SOCKET
GEANY_CHECK_VTE
GEANY_CHECK_MAC_INTEGRATION
GEANY_CHECK_THE_FORCE dnl hehe

AC_SUBST([GETTEXT_PACKAGE],[$PACKAGE])
AC_DEFINE_UNQUOTED([GETTEXT_PACKAGE], ["$PACKAGE"], [Gettext package.])

# double eval since datarootdir is usually defined as ${prefix}/share
AM_COND_IF([MINGW],
  [pkgdatadir='${prefix}/data'],
  [pkgdatadir='${datarootdir}/geany'])
AM_COND_IF([MINGW],[LIBS="$LIBS -liconv"])
AC_SUBST([GEANY_DATA_DIR], [$(eval echo $(eval echo $pkgdatadir))])
AC_SUBST([pkgdatadir])

# The default Python command.  On Windows, use the `py` launcher by default
AC_ARG_WITH([python-command],
			[AS_HELP_STRING([--with-python-command],
							[the default Python command [defaults to "py" on Windows and "python" otherwise]])],
			[with_python_command=$withval],
			[with_python_command=auto])
AS_IF([test "x$with_python_command" = xauto],
	  [AM_COND_IF([MINGW], [with_python_command=py], [with_python_command=python])])
AC_SUBST([PYTHON_COMMAND], [$with_python_command])

# Documentation tools
GEANY_CHECK_DOCUTILS
GEANY_CHECK_DOXYGEN
GEANY_CHECK_GTKDOC_HEADER

# libgeany
GEANY_LIB_INIT

# Output
AC_CONFIG_FILES([
		Makefile
		icons/Makefile
		icons/16x16/Makefile
		icons/24x24/Makefile
		icons/32x32/Makefile
		icons/48x48/Makefile
		icons/scalable/Makefile
		icons/tango/Makefile
		icons/tango/16x16/Makefile
		icons/tango/24x24/Makefile
		icons/tango/32x32/Makefile
		icons/tango/48x48/Makefile
		icons/tango/scalable/Makefile
		ctags/Makefile
		scintilla/Makefile
		src/Makefile
		src/tagmanager/Makefile
		plugins/Makefile
		po/Makefile.in
		data/Makefile
		data/filedefs/filetypes.python
		doc/Makefile
		doc/geany.1
		geany.pc
		geany.nsi
		tests/Makefile
		tests/ctags/Makefile
])
AC_OUTPUT

# Summary
GEANY_STATUS_OUTPUT
echo ""
echo "Configuration is done OK."
echo ""
