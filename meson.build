project('geany', 'c', 'cpp',
        meson_version: '>= 0.56',
        version: '2.2.0',
        default_options : ['c_std=c11', 'cpp_std=c++17'])

gnome = import('gnome')
pymod = import('python')

cc = meson.get_compiler('c')
cxx = meson.get_compiler('cpp')
prefix = get_option('prefix')

deps_in = [
	['glib-2.0',    '2.56'],
	['gmodule-2.0', '2.56'],
	['gtk+-3.0',    '3.22.0']
]

deps = []
deps_for_pc = ''
foreach dep : deps_in
	deps += [dependency(dep[0], version: '>= ' + dep[1])]
	deps_for_pc += ' ' + dep[0] + ' >= ' + dep[1]
endforeach

glib = deps[0]
gtk = deps[2]

# detect libc
glibc = false
if cc.has_header('gnu/libc-version.h')
	glibc = true
endif

gnu_source = glibc

cdata = configuration_data()
check_headers = [
	'alloca.h',
	'asprintf.h',
	'direct.h',
	'dirent.h',
	'dlfcn.h',
	'errno.h',
	'fcntl.h',
	'fnmatch.h',
	'inttypes.h',
	'io.h',
	'langinfo.h',
	'libintl.h',
	'limits.h',
	'locale.h',
	'memory.h',
	'stdbool.h',
	'sys/dir.h',
	'sys/stat.h',
	'sys/time.h',
	'sys/types.h',
	'unistd.h',
	'wchar.h',
	'wctype.h',
]

check_functions = [
	['alloca', '#include <alloca.h>'],
	['asprintf', '#include <stdio.h>'],
	['chsize', '#include <io.h>'],
	['fnmatch', '#include <fnmatch.h>'],
	['ftruncate', '#include <unistd.h>'],
	['isblank', '#include <ctype.h>'],
	['mbrtowc', '#include <wchar.h>'],
	['memcpy',  '#include <string.h>'],
	['mkstemp', '#include <stdlib.h>'],
	['realpath', '#include <limits.h>\n#include <stdlib.h>'],
	['socket', '#include <sys/socket.h>'],
	# man page says strings.h but we include only string.h and it works
	['strcasecmp', '#include <string.h>'],
	['strncasecmp', '#include <string.h>'],
	['stricmp', '#include <string.h>'],
	['strnicmp', '#include <string.h>'],
	['strerror', '#include <string.h>'],
	['strstr', '#include <string.h>'],
	['tempnam', '#include <stdio.h>'],
	['truncate', '#include <unistd.h>'],
	['wcrtomb', '#include <wchar.h>'],
	['wcscoll', '#include <wchar.h>'],
	['g_strv_equal', '#include <glib.h>']
]

# Using builtin regex on macOS for performance reasons
if (host_machine.system() != 'darwin')
	check_functions += [['regcomp', '#include <regex.h>']]
endif

foreach h : check_headers
	define = 'HAVE_' + h.underscorify().to_upper()
	if cc.has_header(h)
		cdata.set(define, 1)
	endif
endforeach

foreach f : check_functions
	define = 'HAVE_' + f.get(0).underscorify().to_upper()
	ccprefix = '\n'.join([gnu_source ? '#define _GNU_SOURCE' : '', f.get(1)])
	if cc.has_function(f.get(0), prefix: ccprefix, dependencies: deps)
		cdata.set(define, 1)
	endif
endforeach

# From configure.ac:
# non-functions checks for u-ctags.  Not that we really need those as we don't
# use u-ctags's main, but the corresponding macros have to be defined to
# something, so simply perform the actual checks.
#AC_CHECK_DECLS([__environ],,,[[#include <unistd.h>]])
#AC_CHECK_DECLS([_NSGetEnviron],,,[[#include <crt_externs.h>]])
# In meson we cannot as easily test for declarations so defining as 0 is easiest
cdata.set('HAVE_DECL___ENVIRON', 0)
cdata.set('HAVE_DECL__NSGETENVIRON', 0)

cdata.set_quoted('PACKAGE', 'geany')
cdata.set_quoted('PACKAGE_BUGREPORT', 'https://github.com/geany/geany/issues')
cdata.set_quoted('PACKAGE_NAME','Geany')
cdata.set_quoted('PACKAGE_STRING', 'Geany ' + meson.project_version())
cdata.set_quoted('PACKAGE_VERSION', meson.project_version())
cdata.set('GETTEXT_PACKAGE', 'PACKAGE')
cdata.set_quoted('VERSION', meson.project_version())
cdata.set('ENABLE_NLS', 1)
if (host_machine.system() != 'windows' and get_option('vte'))
	cdata.set('HAVE_VTE', 1)
endif
cdata.set('HAVE_PLUGINS', get_option('plugins'))
cdata.set('HAVE_SOCKET', get_option('socket'))
if (host_machine.system() == 'windows')
	cdata.set('GEANY_DATA_DIR', join_paths(prefix, 'data'))
else
	cdata.set('GEANY_DATA_DIR', join_paths(prefix, get_option('datadir'), 'geany'))
endif
cdata.set('GEANY_DOC_DIR', join_paths(prefix, get_option('datadir'), 'doc', 'geany'))
cdata.set('top_srcdir', meson.project_source_root())
cdata.set('top_builddir', meson.project_build_root())

# for geany.pc (adapted from GTK+)
pcconf = cdata
pcconf.set('DEPENDENCIES', deps_for_pc)
pcconf.set('prefix', get_option('prefix'))
pcconf.set('libdir', join_paths('${exec_prefix}', get_option('libdir')))
pcconf.set('includedir', join_paths('${prefix}', get_option('includedir')))
pcconf.set('datarootdir', join_paths('${prefix}', get_option('datadir')))
# actually constant, just match autconf
pcconf.set('exec_prefix', '${prefix}')
pcconf.set('datadir', '${datarootdir}')
pcconf.set('localedir', join_paths('${datarootdir}', 'locale'))

# needed programs
sh = find_program('sh')
cp = find_program('cp')
ln = find_program('ln')
doxygen = find_program('doxygen', required: get_option('api-docs'))
python = pymod.find_installation('python3', modules: doxygen.found() and get_option('gtkdoc') ? ['lxml'] : [])
# These two are truly optional
rst2html = find_program('rst2html', required: get_option('html-docs'))
rst2pdf = find_program('rst2pdf', required: get_option('pdf-docs'))
git = find_program('git', required: false)
revision = '-1'
if git.found()
	ret = run_command(git, 'rev-parse', '--short', '--revs-only', 'HEAD', check: false)
	if ret.returncode() == 0
		revision = ret.stdout().strip()
	endif
endif
cdata.set_quoted('REVISION', revision)

python_command = get_option('python-command')
if python_command == '' or python_command == 'auto'
	if host_machine.system() == 'windows'
		python_command = 'py'
	else
		python_command = 'python'
	endif
endif
cdata.set('PYTHON_COMMAND', python_command)

configure_file(
	output : 'config.h',
	configuration : cdata
)

configure_file(
	input: 'geany.pc.in',
	install: true,
	install_dir: join_paths(prefix, get_option('libdir'), 'pkgconfig'),
	output: 'geany.pc',
	configuration: pcconf
)

configure_file(
	input: 'geany.nsi.in',
	install: false,
	output: 'geany.nsi',
	configuration: {
		'VERSION': meson.project_version(),
		'GTK_VERSION': gtk.version(),
	}
)

# CFLAGS for basic stuff that only depends on libc
basic_cflags  = [ '-DHAVE_CONFIG_H=1', '-O2' ]
if (gnu_source)
	basic_cflags += '-D_GNU_SOURCE'
endif

# CFLAGS common between Geany and bundled plugins
def_cflags = basic_cflags + [
	'-DGTK',
# we're using lots of deprecated stuff with gtk3 (often because there is no proper alternative!)
	'-Wno-deprecated-declarations',
	'-DGDK_DISABLE_DEPRECATION_WARNINGS',
	'-DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_56',
	'-DGEANY_PREFIX="@0@"'.format(get_option('prefix')),
	'-DGEANY_DOCDIR="@0@"'.format(join_paths(prefix, get_option('datadir'), 'doc'))
]
foreach d : [ 'includedir', 'libdir', 'libexecdir', 'datadir', 'localedir' ]
	def_cflags += '-DGEANY_@0@="@1@"'.format(d.underscorify().to_upper(), join_paths(prefix, get_option(d)))
endforeach

# CFLAGS for everything else, i.e. most of Geany
geany_cflags = def_cflags
have_gcc4_visibility = cc.has_argument('-fvisibility=hidden')
geany_cflags += '-DGEANY_PRIVATE'
if host_machine.system() == 'windows'
	geany_cflags += '-DGEANY_EXPORT_SYMBOL=__declspec(dllexport)'
elif have_gcc4_visibility
	geany_cflags += '-fvisibility=hidden'
	geany_cflags += '-DGEANY_EXPORT_SYMBOL=__attribute__((visibility("default")))'
endif
geany_cflags += '-DGEANY_API_SYMBOL=GEANY_EXPORT_SYMBOL'
sci_cflags = geany_cflags
sci_cflags += [ '-std=c++17', '-Wno-non-virtual-dtor', '-DNDEBUG', '-DSCI_LEXER', '-DNO_CXX11_REGEX' ]

iregex = include_directories('ctags/gnu_regex')
ifnmatch = include_directories('ctags/fnmatch')
ictags = include_directories('ctags/main', 'ctags/parsers', 'ctags/dsl', 'ctags/libreadtags')
itagmanager = include_directories('src/tagmanager')
iscintilla = include_directories('scintilla/include', 'scintilla/lexilla/include')
igeany = include_directories('src')

install_headers(
	'scintilla/include/Scintilla.h',
	'scintilla/include/Scintilla.iface',
	'scintilla/include/ScintillaWidget.h',
	'scintilla/include/Sci_Position.h',
	'scintilla/lexilla/include/LexicalStyles.iface',
	'scintilla/lexilla/include/SciLexer.h',
	subdir: 'geany/scintilla'
)

lexilla = static_library('lexilla',
	'scintilla/lexilla/include/Lexilla.h',
	'scintilla/lexilla/include/SciLexer.h',
	'scintilla/lexilla/lexers/LexAbaqus.cxx',
	'scintilla/lexilla/lexers/LexAda.cxx',
	'scintilla/lexilla/lexers/LexAsciidoc.cxx',
	'scintilla/lexilla/lexers/LexAsm.cxx',
	'scintilla/lexilla/lexers/LexAU3.cxx',
	'scintilla/lexilla/lexers/LexBash.cxx',
	'scintilla/lexilla/lexers/LexBasic.cxx',
	'scintilla/lexilla/lexers/LexBatch.cxx',
	'scintilla/lexilla/lexers/LexCaml.cxx',
	'scintilla/lexilla/lexers/LexCIL.cxx',
	'scintilla/lexilla/lexers/LexCmake.cxx',
	'scintilla/lexilla/lexers/LexCOBOL.cxx',
	'scintilla/lexilla/lexers/LexCoffeeScript.cxx',
	'scintilla/lexilla/lexers/LexCPP.cxx',
	'scintilla/lexilla/lexers/LexCSS.cxx',
	'scintilla/lexilla/lexers/LexD.cxx',
	'scintilla/lexilla/lexers/LexDart.cxx',
	'scintilla/lexilla/lexers/LexDiff.cxx',
	'scintilla/lexilla/lexers/LexErlang.cxx',
	'scintilla/lexilla/lexers/LexForth.cxx',
	'scintilla/lexilla/lexers/LexGDScript.cxx',
	'scintilla/lexilla/lexers/LexFortran.cxx',
	'scintilla/lexilla/lexers/LexHaskell.cxx',
	'scintilla/lexilla/lexers/LexHTML.cxx',
	'scintilla/lexilla/lexers/LexJulia.cxx',
	'scintilla/lexilla/lexers/LexLaTeX.cxx',
	'scintilla/lexilla/lexers/LexLisp.cxx',
	'scintilla/lexilla/lexers/LexLua.cxx',
	'scintilla/lexilla/lexers/LexMake.cxx',
	'scintilla/lexilla/lexers/LexMarkdown.cxx',
	'scintilla/lexilla/lexers/LexMatlab.cxx',
	'scintilla/lexilla/lexers/LexNim.cxx',
	'scintilla/lexilla/lexers/LexNix.cxx',
	'scintilla/lexilla/lexers/LexNsis.cxx',
	'scintilla/lexilla/lexers/LexNull.cxx',
	'scintilla/lexilla/lexers/LexPascal.cxx',
	'scintilla/lexilla/lexers/LexPerl.cxx',
	'scintilla/lexilla/lexers/LexPO.cxx',
	'scintilla/lexilla/lexers/LexPowerShell.cxx',
	'scintilla/lexilla/lexers/LexProps.cxx',
	'scintilla/lexilla/lexers/LexPython.cxx',
	'scintilla/lexilla/lexers/LexR.cxx',
	'scintilla/lexilla/lexers/LexRaku.cxx',
	'scintilla/lexilla/lexers/LexRuby.cxx',
	'scintilla/lexilla/lexers/LexRust.cxx',
	'scintilla/lexilla/lexers/LexSINEX.cxx',
	'scintilla/lexilla/lexers/LexSmalltalk.cxx',
	'scintilla/lexilla/lexers/LexSQL.cxx',
	'scintilla/lexilla/lexers/LexTCL.cxx',
	'scintilla/lexilla/lexers/LexTOML.cxx',
	'scintilla/lexilla/lexers/LexTxt2tags.cxx',
	'scintilla/lexilla/lexers/LexVerilog.cxx',
	'scintilla/lexilla/lexers/LexVHDL.cxx',
	'scintilla/lexilla/lexers/LexVisualProlog.cxx',
	'scintilla/lexilla/lexers/LexYAML.cxx',
	'scintilla/lexilla/lexers/LexZig.cxx',
	'scintilla/lexilla/lexlib/Accessor.cxx',
	'scintilla/lexilla/lexlib/Accessor.h',
	'scintilla/lexilla/lexlib/CatalogueModules.h',
	'scintilla/lexilla/lexlib/CharacterCategory.cxx',
	'scintilla/lexilla/lexlib/CharacterCategory.h',
	'scintilla/lexilla/lexlib/CharacterSet.cxx',
	'scintilla/lexilla/lexlib/CharacterSet.h',
	'scintilla/lexilla/lexlib/DefaultLexer.cxx',
	'scintilla/lexilla/lexlib/DefaultLexer.h',
	'scintilla/lexilla/lexlib/InList.cxx',
	'scintilla/lexilla/lexlib/InList.h',
	'scintilla/lexilla/lexlib/LexAccessor.cxx',
	'scintilla/lexilla/lexlib/LexAccessor.h',
	'scintilla/lexilla/lexlib/LexerBase.cxx',
	'scintilla/lexilla/lexlib/LexerBase.h',
	'scintilla/lexilla/lexlib/LexerModule.cxx',
	'scintilla/lexilla/lexlib/LexerModule.h',
	'scintilla/lexilla/lexlib/LexerNoExceptions.cxx',
	'scintilla/lexilla/lexlib/LexerNoExceptions.h',
	'scintilla/lexilla/lexlib/LexerSimple.cxx',
	'scintilla/lexilla/lexlib/LexerSimple.h',
	'scintilla/lexilla/lexlib/OptionSet.h',
	'scintilla/lexilla/lexlib/PropSetSimple.cxx',
	'scintilla/lexilla/lexlib/PropSetSimple.h',
	'scintilla/lexilla/lexlib/SparseState.h',
	'scintilla/lexilla/lexlib/StringCopy.h',
	'scintilla/lexilla/lexlib/StyleContext.cxx',
	'scintilla/lexilla/lexlib/StyleContext.h',
	'scintilla/lexilla/lexlib/SubStyles.h',
	'scintilla/lexilla/lexlib/WordList.cxx',
	'scintilla/lexilla/lexlib/WordList.h',
	'scintilla/lexilla/src/Lexilla.cxx',
	cpp_args: sci_cflags,
	dependencies: deps,
	include_directories: [
		iscintilla,
		include_directories('scintilla/lexilla/lexlib', 'scintilla/lexilla/include')
	]
)
dep_lexilla = declare_dependency(
	link_with: lexilla,
	include_directories: include_directories('scintilla/lexilla/include')
)

scintilla = static_library('scintilla',
	'scintilla/gtk/Converter.h',
	'scintilla/gtk/PlatGTK.cxx',
	'scintilla/gtk/ScintillaGTKAccessible.cxx',
	'scintilla/gtk/ScintillaGTKAccessible.h',
	'scintilla/gtk/ScintillaGTK.cxx',
	'scintilla/gtk/ScintillaGTK.h',
	'scintilla/gtk/scintilla-marshal.c',
	'scintilla/gtk/scintilla-marshal.h',
	'scintilla/gtk/Wrappers.h',
	'scintilla/include/ILexer.h',
	'scintilla/include/ILoader.h',
	'scintilla/include/ScintillaCall.h',
	'scintilla/include/Scintilla.h',
	'scintilla/include/ScintillaMessages.h',
	'scintilla/include/ScintillaStructures.h',
	'scintilla/include/ScintillaTypes.h',
	'scintilla/include/ScintillaWidget.h',
	'scintilla/include/Sci_Position.h',
	'scintilla/src/AutoComplete.cxx',
	'scintilla/src/AutoComplete.h',
	'scintilla/src/CallTip.cxx',
	'scintilla/src/CallTip.h',
	'scintilla/src/CaseConvert.cxx',
	'scintilla/src/CaseConvert.h',
	'scintilla/src/CaseFolder.cxx',
	'scintilla/src/CaseFolder.h',
	'scintilla/src/CellBuffer.cxx',
	'scintilla/src/CellBuffer.h',
	'scintilla/src/ChangeHistory.cxx',
	'scintilla/src/ChangeHistory.h',
	'scintilla/src/CharacterCategoryMap.cxx',
	'scintilla/src/CharacterCategoryMap.h',
	'scintilla/src/CharacterType.cxx',
	'scintilla/src/CharacterType.h',
	'scintilla/src/CharClassify.cxx',
	'scintilla/src/CharClassify.h',
	'scintilla/src/ContractionState.cxx',
	'scintilla/src/ContractionState.h',
	'scintilla/src/DBCS.cxx',
	'scintilla/src/DBCS.h',
	'scintilla/src/Debugging.h',
	'scintilla/src/Decoration.cxx',
	'scintilla/src/Decoration.h',
	'scintilla/src/Document.cxx',
	'scintilla/src/Document.h',
	'scintilla/src/EditModel.cxx',
	'scintilla/src/EditModel.h',
	'scintilla/src/Editor.cxx',
	'scintilla/src/Editor.h',
	'scintilla/src/EditView.cxx',
	'scintilla/src/EditView.h',
	'scintilla/src/ElapsedPeriod.h',
	'scintilla/src/FontQuality.h',
	'scintilla/src/Geometry.cxx',
	'scintilla/src/Geometry.h',
	'scintilla/src/Indicator.cxx',
	'scintilla/src/Indicator.h',
	'scintilla/src/KeyMap.cxx',
	'scintilla/src/KeyMap.h',
	'scintilla/src/LineMarker.cxx',
	'scintilla/src/LineMarker.h',
	'scintilla/src/MarginView.cxx',
	'scintilla/src/MarginView.h',
	'scintilla/src/Partitioning.h',
	'scintilla/src/PerLine.cxx',
	'scintilla/src/PerLine.h',
	'scintilla/src/Platform.h',
	'scintilla/src/PositionCache.cxx',
	'scintilla/src/PositionCache.h',
	'scintilla/src/Position.h',
	'scintilla/src/RESearch.cxx',
	'scintilla/src/RESearch.h',
	'scintilla/src/RunStyles.cxx',
	'scintilla/src/RunStyles.h',
	'scintilla/src/ScintillaBase.cxx',
	'scintilla/src/ScintillaBase.h',
	'scintilla/src/Selection.cxx',
	'scintilla/src/Selection.h',
	'scintilla/src/SparseVector.h',
	'scintilla/src/SplitVector.h',
	'scintilla/src/Style.cxx',
	'scintilla/src/Style.h',
	'scintilla/src/UndoHistory.cxx',
	'scintilla/src/UndoHistory.h',
	'scintilla/src/UniConversion.cxx',
	'scintilla/src/UniConversion.h',
	'scintilla/src/UniqueString.cxx',
	'scintilla/src/UniqueString.h',
	'scintilla/src/ViewStyle.cxx',
	'scintilla/src/ViewStyle.h',
	'scintilla/src/XPM.cxx',
	'scintilla/src/XPM.h',
	cpp_args: sci_cflags,
	dependencies: deps + [ dep_lexilla ],
	include_directories: [
		iscintilla,
		include_directories('scintilla/include', 'scintilla/src')
	]
)
dep_scintilla = declare_dependency(
	link_with: scintilla,
	include_directories: include_directories('scintilla/include')
)

if cdata.get('HAVE_FNMATCH', 0) == 1
	dep_fnmatch = dependency('', required: false)
else
	# use fnmatch bundled with ctags
	fnmatch = static_library('fnmatch',
		'ctags/fnmatch/fnmatch.c',
		'ctags/fnmatch/fnmatch.h',
		include_directories: [ifnmatch],
		c_args: basic_cflags
	)
	dep_fnmatch = declare_dependency(link_with: [fnmatch], include_directories: [ifnmatch])
endif

if cdata.get('HAVE_REGCOMP', 0) == 1
	dep_regex = dependency('', required: false)
else
	# use regcomp bundled with ctags
	regex = static_library('regex',
		'ctags/gnu_regex/regex.c',
		'ctags/gnu_regex/regex.h',
		dependencies: [dep_fnmatch],
		c_args: basic_cflags + [ '-D__USE_GNU' ]
	)
	dep_regex = declare_dependency(link_with: [regex], include_directories: [iregex])
endif

ctags = static_library('ctags',
	'ctags/dsl/es.c',
	'ctags/dsl/es.h',
	'ctags/dsl/optscript.c',
	'ctags/dsl/optscript.h',
	'ctags/libreadtags/readtags.c',
	'ctags/libreadtags/readtags.h',
	'ctags/main/args.c',
	'ctags/main/args_p.h',
	'ctags/main/colprint.c',
	'ctags/main/colprint_p.h',
	'ctags/main/collector.c',
	'ctags/main/collector.h',
	'ctags/main/CommonPrelude.c',
	'ctags/main/ctags.h',
	'ctags/main/debug.c',
	'ctags/main/debug.h',
	'ctags/main/dependency.c',
	'ctags/main/dependency.h',
	'ctags/main/dependency_p.h',
	'ctags/main/e_msoft.h',
	'ctags/main/entry.c',
	'ctags/main/entry.h',
	'ctags/main/entry_p.h',
	'ctags/main/entry_private.c',
	'ctags/main/error.c',
	'ctags/main/error_p.h',
	'ctags/main/field.c',
	'ctags/main/field.h',
	'ctags/main/field_p.h',
	'ctags/main/flags.c',
	'ctags/main/flags_p.h',
	'ctags/main/fmt.c',
	'ctags/main/fmt_p.h',
	'ctags/main/gcc-attr.h',
	'ctags/main/general.h',
	'ctags/main/gvars.h',
	'ctags/main/htable.c',
	'ctags/main/htable.h',
	'ctags/main/inline.h',
	'ctags/main/interactive_p.h',
	'ctags/main/interval_tree_generic.h',
	'ctags/main/keyword.c',
	'ctags/main/keyword.h',
	'ctags/main/keyword_p.h',
	'ctags/main/kind.c',
	'ctags/main/kind.h',
	'ctags/main/kind_p.h',
	'ctags/main/lregex.c',
	'ctags/main/lregex-default.c',
	'ctags/main/lregex.h',
	'ctags/main/lregex_p.h',
	'ctags/main/lxpath.c',
	'ctags/main/lxpath.h',
	'ctags/main/lxpath_p.h',
	'ctags/main/main.c',
	'ctags/main/main_p.h',
	'ctags/main/mbcs.c',
	'ctags/main/mbcs.h',
	'ctags/main/mbcs_p.h',
	'ctags/main/mio.c',
	'ctags/main/mio.h',
	'ctags/main/nestlevel.c',
	'ctags/main/nestlevel.h',
	'ctags/main/numarray.c',
	'ctags/main/numarray.h',
	'ctags/main/objpool.c',
	'ctags/main/objpool.h',
	'ctags/main/options.c',
	'ctags/main/options.h',
	'ctags/main/options_p.h',
	'ctags/main/param.c',
	'ctags/main/param.h',
	'ctags/main/param_p.h',
	'ctags/main/parse.c',
	'ctags/main/parse.h',
	'ctags/main/parse_p.h',
	'ctags/main/parsers_p.h',
	'ctags/main/portable-dirent_p.h',
	'ctags/main/portable-scandir.c',
	'ctags/main/promise.c',
	'ctags/main/promise.h',
	'ctags/main/promise_p.h',
	'ctags/main/ptag.c',
	'ctags/main/ptag_p.h',
	'ctags/main/ptrarray.c',
	'ctags/main/ptrarray.h',
	'ctags/main/rbtree.c',
	'ctags/main/rbtree.h',
	'ctags/main/rbtree_augmented.h',
	'ctags/main/read.c',
	'ctags/main/read.h',
	'ctags/main/read_p.h',
	'ctags/main/repoinfo.c',
	'ctags/main/repoinfo.h',
	'ctags/main/rexprcode.c',
	'ctags/main/rexprcode_p.h',
	'ctags/main/routines.c',
	'ctags/main/routines.h',
	'ctags/main/routines_p.h',
	'ctags/main/script.c',
	'ctags/main/script_p.h',
	'ctags/main/seccomp.c',
	'ctags/main/selectors.c',
	'ctags/main/selectors.h',
	'ctags/main/sort.c',
	'ctags/main/sort_p.h',
	'ctags/main/sort_r.h',
	'ctags/main/stats.c',
	'ctags/main/stats_p.h',
	'ctags/main/strlist.c',
	'ctags/main/strlist.h',
	'ctags/main/subparser.h',
	'ctags/main/subparser_p.h',
	'ctags/main/tokeninfo.c',
	'ctags/main/tokeninfo.h',
	'ctags/main/trace.c',
	'ctags/main/trace.h',
	'ctags/main/trashbox.c',
	'ctags/main/trashbox.h',
	'ctags/main/trashbox_p.h',
	'ctags/main/types.h',
	'ctags/main/unwindi.c',
	'ctags/main/unwindi.h',
	'ctags/main/utf8_str.c',
	'ctags/main/utf8_str.h',
	'ctags/main/vstring.c',
	'ctags/main/vstring.h',
	'ctags/main/writer.c',
	'ctags/main/writer-ctags.c',
	'ctags/main/writer-etags.c',
	'ctags/main/writer-json.c',
	'ctags/main/writer_p.h',
	'ctags/main/writer-xref.c',
	'ctags/main/xtag.c',
	'ctags/main/xtag.h',
	'ctags/main/xtag_p.h',
	'ctags/optlib/forth.c',
	'ctags/optlib/meson.c',
	'ctags/optlib/scss.c',
	'ctags/optlib/terraform.c',
	'ctags/parsers/abaqus.c',
	'ctags/parsers/abc.c',
	'ctags/parsers/ada.c',
	'ctags/parsers/asciidoc.c',
	'ctags/parsers/asm.c',
	'ctags/parsers/autoit.c',
	'ctags/parsers/basic.c',
	'ctags/parsers/bibtex.c',
	'ctags/parsers/x-bibtex.h',
	'ctags/parsers/clojure.c',
	'ctags/parsers/cobol.c',
	'ctags/parsers/cpreprocessor.c',
	'ctags/parsers/x-cpreprocessor.h',
	'ctags/parsers/css.c',
	'ctags/parsers/cxx/cxx.c',
	'ctags/parsers/cxx/cxx_debug.c',
	'ctags/parsers/cxx/cxx_debug.h',
	'ctags/parsers/cxx/cxx_debug_type.c',
	'ctags/parsers/cxx/cxx_keyword.c',
	'ctags/parsers/cxx/cxx_keyword.h',
	'ctags/parsers/cxx/cxx_parser_block.c',
	'ctags/parsers/cxx/cxx_parser.c',
	'ctags/parsers/cxx/cxx_parser_function.c',
	'ctags/parsers/cxx/cxx_parser.h',
	'ctags/parsers/cxx/cxx_parser_internal.h',
	'ctags/parsers/cxx/cxx_parser_lambda.c',
	'ctags/parsers/cxx/cxx_parser_module.c',
	'ctags/parsers/cxx/cxx_parser_namespace.c',
	'ctags/parsers/cxx/cxx_parser_template.c',
	'ctags/parsers/cxx/cxx_parser_tokenizer.c',
	'ctags/parsers/cxx/cxx_parser_typedef.c',
	'ctags/parsers/cxx/cxx_parser_using.c',
	'ctags/parsers/cxx/cxx_parser_variable.c',
	'ctags/parsers/cxx/cxx_qtmoc.c',
	'ctags/parsers/cxx/cxx_scope.c',
	'ctags/parsers/cxx/cxx_scope.h',
	'ctags/parsers/cxx/cxx_side_chain.c',
	'ctags/parsers/cxx/cxx_side_chain.h',
	'ctags/parsers/cxx/cxx_subparser.c',
	'ctags/parsers/cxx/cxx_subparser.h',
	'ctags/parsers/cxx/cxx_subparser_internal.h',
	'ctags/parsers/cxx/cxx_tag.c',
	'ctags/parsers/cxx/cxx_tag.h',
	'ctags/parsers/cxx/cxx_token.c',
	'ctags/parsers/cxx/cxx_token_chain.c',
	'ctags/parsers/cxx/cxx_token_chain.h',
	'ctags/parsers/cxx/cxx_token.h',
	'ctags/parsers/diff.c',
	'ctags/parsers/dosbatch.c',
	'ctags/parsers/erlang.c',
	'ctags/parsers/flex.c',
	'ctags/parsers/fortran.c',
	'ctags/parsers/gdscript.c',
	'ctags/parsers/geany_c.c',
	'ctags/parsers/geany_docbook.c',
	'ctags/parsers/geany_lcpp.c',
	'ctags/parsers/geany_lcpp.h',
	'ctags/parsers/matlab.c',
	'ctags/parsers/go.c',
	'ctags/parsers/haskell.c',
	'ctags/parsers/haxe.c',
	'ctags/parsers/html.c',
	'ctags/parsers/x-html.h',
	'ctags/parsers/iniconf.c',
	'ctags/parsers/x-iniconf.h',
	'ctags/parsers/jscript.c',
	'ctags/parsers/x-jscript.h',
	'ctags/parsers/json.c',
	'ctags/parsers/julia.c',
	'ctags/parsers/ldscript.c',
	'ctags/parsers/lisp.c',
	'ctags/parsers/x-lisp.h',
	'ctags/parsers/lua.c',
	'ctags/parsers/make.c',
	'ctags/parsers/x-make.h',
	'ctags/parsers/markdown.c',
	'ctags/parsers/x-markdown.h',
	'ctags/parsers/nsis.c',
	'ctags/parsers/objc.c',
	'ctags/parsers/ocaml.c',
	'ctags/parsers/pascal.c',
	'ctags/parsers/perl.c',
	'ctags/parsers/x-perl.h',
	'ctags/parsers/php.c',
	'ctags/parsers/powershell.c',
	'ctags/parsers/python.c',
	'ctags/parsers/x-python.h',
	'ctags/parsers/r.c',
	'ctags/parsers/x-r.h',
	'ctags/parsers/raku.c',
	'ctags/parsers/rst.c',
	'ctags/parsers/ruby.c',
	'ctags/parsers/x-ruby.h',
	'ctags/parsers/rust.c',
	'ctags/parsers/sh.c',
	'ctags/parsers/x-sh.h',
	'ctags/parsers/sinex.c',
	'ctags/parsers/sql.c',
	'ctags/parsers/tcl.c',
	'ctags/parsers/x-tcl.h',
	'ctags/parsers/tcloo.c',
	'ctags/parsers/tex.c',
	'ctags/parsers/x-tex.h',
	'ctags/parsers/txt2tags.c',
	'ctags/parsers/typescript.c',
	'ctags/parsers/verilog.c',
	'ctags/parsers/vhdl.c',
	c_args: geany_cflags + [ '-DG_LOG_DOMAIN="CTags"',
	                         '-DEXTERNAL_PARSER_LIST_FILE="src/tagmanager/tm_parsers.h"' ],
	dependencies: deps + [dep_fnmatch, dep_regex],
	include_directories: [ictags]
)
dep_ctags = declare_dependency(link_with: [ctags], include_directories: [ictags],
 dependencies: [dep_fnmatch, dep_regex])

install_headers(
	'src/tagmanager/tm_source_file.h',
	'src/tagmanager/tm_tag.h',
	'src/tagmanager/tm_workspace.h',
	'src/tagmanager/tm_parser.h',
	subdir: 'geany/tagmanager'
)

tagmanager = static_library('tagmanager',
	'src/tagmanager/tm_ctags.h',
	'src/tagmanager/tm_ctags.c',
	'src/tagmanager/tm_parser.h',
	'src/tagmanager/tm_parser.c',
	'src/tagmanager/tm_parsers.h',
	'src/tagmanager/tm_source_file.h',
	'src/tagmanager/tm_source_file.c',
	'src/tagmanager/tm_tag.h',
	'src/tagmanager/tm_tag.c',
	'src/tagmanager/tm_workspace.h',
	'src/tagmanager/tm_completion.c', 'src/tagmanager/tm_completion.h', 'src/tagmanager/tm_workspace.c',
	c_args: geany_cflags + [ '-DG_LOG_DOMAIN="Tagmanager"' ],
	dependencies: [dep_ctags, glib]
)
dep_tagmanager = declare_dependency(
	link_with: tagmanager,
	include_directories: [itagmanager]
)

# Generate signallist.i
gen_src = custom_target('gen-signallist',
	input : [ 'data/geany.glade' ],
	output : [ 'signallist.i' ],
	command : [find_program('scripts/gen-signallist.sh'), '@INPUT@', '@OUTPUT@' ]
)

win_src = []
win_deps = []
win_resc = []
win_link_args = []
if (host_machine.system() == 'windows')
	windows = import('windows')
	win_resc = windows.compile_resources('geany_private.rc')
	win_src += [ 'src/win32.c', 'src/win32.h' ]
	# Keep the editor's C++ allocator/runtime independent of the GTK bundle.
	if cc.get_id() == 'gcc'
		win_link_args = ['-static-libgcc', '-static-libstdc++']
	endif
	foreach lib : ['ole32', 'wsock32', 'comdlg32', 'dwmapi']
		win_deps += cc.find_library(lib)
	endforeach
	# install Windows run helper
	install_data(
		'src/geany-run-helper.bat',
		install_mode: 'rwxr-xr-x',
		install_dir: join_paths(get_option('libexecdir'), 'geany'),
	)
endif

osx_src = []
osx_deps = []
if host_machine.system() == 'darwin'
	dep_mac_integration = dependency('gtk-mac-integration', version: '>= 3.0.1',
		required: get_option('mac-integration'))
	if dep_mac_integration.found()
		osx_src += ['src/osx.c', 'src/osx.h']
		osx_deps += [dep_mac_integration]
	endif
endif

vte_src = ['src/vte.c', 'src/vte.h']
if host_machine.system() == 'windows'
	# No VTE support on Windows
	vte_src = []
endif

install_headers(
	'plugins/geanyfunctions.h',
	'plugins/geanyplugin.h',
	'src/app.h',
	'src/build.h',
	'src/dialogs.h',
	'src/document.h',
	'src/editor.h',
	'src/encodings.h',
	'src/filetypes.h',
	'src/geany.h',
	'src/gtkcompat.h',
	'src/highlighting.h',
	'src/keybindings.h',
	'src/main.h',
	'src/msgwindow.h',
	'src/navqueue.h',
	'src/plugindata.h',
	'src/pluginextension.h',
	'src/pluginutils.h',
	'src/prefs.h',
	'src/project.h',
	'src/sciwrappers.h',
	'src/search.h',
	'src/spawn.h',
	'src/stash.h',
	'src/support.h',
	'src/symbols.h',
	'src/templates.h',
	'src/toolbar.h',
	'src/ui_utils.h',
	'src/utils.h',
	subdir: 'geany'
)

insight_lua_sources = [
 'insight/vendor/lua/lapi.c',
 'insight/vendor/lua/lauxlib.c',
 'insight/vendor/lua/lbaselib.c',
 'insight/vendor/lua/lcode.c',
 'insight/vendor/lua/lctype.c',
 'insight/vendor/lua/ldebug.c',
 'insight/vendor/lua/ldo.c',
 'insight/vendor/lua/ldump.c',
 'insight/vendor/lua/lfunc.c',
 'insight/vendor/lua/lgc.c',
 'insight/vendor/lua/llex.c',
 'insight/vendor/lua/lmathlib.c',
 'insight/vendor/lua/lmem.c',
 'insight/vendor/lua/lobject.c',
 'insight/vendor/lua/lopcodes.c',
 'insight/vendor/lua/lparser.c',
 'insight/vendor/lua/lstate.c',
 'insight/vendor/lua/lstring.c',
 'insight/vendor/lua/lstrlib.c',
 'insight/vendor/lua/ltable.c',
 'insight/vendor/lua/ltablib.c',
 'insight/vendor/lua/ltests.c',
 'insight/vendor/lua/ltm.c',
 'insight/vendor/lua/lundump.c',
 'insight/vendor/lua/lutf8lib.c',
 'insight/vendor/lua/lvm.c',
 'insight/vendor/lua/lzio.c',
]
insight_native = static_library('insight_native',
 'insight/desktop/keyword_search.c',
 'insight/desktop/remote_io.c',
 'insight/desktop/remote_credentials.c',
 'insight/desktop/search_state.c',
	'insight/desktop/calculate.c', 'insight/desktop/text_boundary.c', 'insight/desktop/line_insert.c', 'insight/desktop/paragraph.c', 'insight/desktop/renumber.c', 'insight/desktop/text_transfer.c', 'insight/desktop/symbol_edit.c', 'insight/desktop/ai_client.c', 'insight/desktop/ai_edit_session.c', 'insight/desktop/replace_files.c', 'insight/desktop/html_export.c', 'insight/desktop/project_report.c', 'insight/desktop/backup_store.c', 'insight/desktop/intelligent_paste.c', 'insight/desktop/completion_settings.c', 'insight/desktop/completion_preferences.c', 'insight/desktop/filetype_pairs.c', 'insight/desktop/filetype_editing.c', 'insight/desktop/filetype_options.c', 'insight/desktop/git_history.c', 'insight/desktop/git_project.c', 'insight/desktop/folder_tree.c', 'insight/desktop/folder_layout.c', 'insight/desktop/disk_operation.c', 'insight/desktop/directory_operation.c', 'insight/desktop/project_files.c', 'insight/desktop/master_list.c', 'insight/desktop/master_sync.c',
 'insight/core/index.c', 'insight/core/document.c', 'insight/core/occurrences.c', 'insight/desktop/index_cache.c', 'insight/desktop/project_db.c',
 'src/insight-index.c', 'src/insight-scopes.c', 'insight/core/runtime.c', 'insight/core/context.c', 'insight/desktop/extensions.c', 'insight/desktop/lookup_filter.c', 'insight/desktop/file_patterns.c', 'insight/desktop/layout_state.c', 'insight/desktop/context_search.c', 'insight/desktop/notebook_store.c', 'insight/desktop/snippet.c', 'insight/desktop/workspace_watch.c', 'insight/desktop/source_mask.c', 'insight/desktop/relation_query.c', 'insight/desktop/type_resolve.c', insight_lua_sources,
 c_args: geany_cflags,
 include_directories: include_directories('insight/core', 'insight/vendor/lua'),
 dependencies: deps + [cc.find_library('m', required: false)])

libgeany = shared_library('geany',
	'src/about.c',
	'src/about.h',
	'src/app.h',
	'src/build.c',
	'src/build.h',
	'src/callbacks.c',
	'src/callbacks.h',
	'src/dialogs.c',
	'src/dialogs.h',
	'src/document.c',
	'src/document.h',
	'src/editor.c',
	'src/editor.h',
	'src/encodings.c',
	'src/encodings.h',
	'src/filetypes.c',
	'src/filetypes.h',
	'src/geanyentryaction.c',
	'src/geanyentryaction.h',
	'src/geanymenubuttonaction.c',
	'src/geanymenubuttonaction.h',
	'src/geanyobject.c',
	'src/geanyobject.h',
	'src/geanywraplabel.c',
	'src/geanywraplabel.h',
	'src/gtkcompat.h',
	'src/highlighting.c',
	'src/highlighting.h',
	'src/highlightingmappings.h',
	'src/keybindings.c',
	'src/keybindings.h',
	'src/keyfile.c',
	'src/keyfile.h',
	'src/log.c',
	'src/log.h',
	'src/libmain.c',
	'src/insight-git.c', 'src/insight-git.h', 'src/insight-workbench.c', 'src/insight-symbol-view.c', 'src/insight-symbol-view.h',
	'src/insight-directory.c', 'src/insight-directory.h',
	'insight/desktop/directory_compare.c', 'insight/desktop/directory_compare.h',
	'src/insight-diff.c',
	'src/insight-diff.h', 'src/insight-compare-ui.h',
	'src/insight-reload.c', 'src/insight-reload.h',
	'src/insight-tree-expand.c', 'src/insight-tree-expand.h',
	'src/insight-relation-tree.c',
	'src/insight-disk-browser.c',
	'src/insight-disk-browser.h',
	'src/insight-themes.c', 'src/insight-themes.h', 'src/insight-appearance.c', 'src/insight-appearance.h',
	'src/insight-list-style.c',
	'src/insight-list-style.h',
	'src/insight-template-fields.c',
	'src/insight-relation-tree.h',
	'src/insight-text-print.c',
	'src/insight-text-print.h',
	'src/insight-list-tools.c',
	'src/insight-list-tools.h',
	'src/insight-relation-graph.c',
	'src/insight-relation-graph.h',
	'src/insight-help.c',
 'src/insight-search-links.c', 'src/insight-search-links.h', 'insight/desktop/source_link_anchor.c',
 'src/insight-symbol-edit.c', 'src/insight-symbol-edit.h', 'src/insight-buffer-transfer.c', 'src/insight-buffer-transfer.h', 'src/insight-renumber-ranges.c', 'src/insight-renumber-ranges.h', 'src/insight-replace-files.c', 'src/insight-replace-files.h', 'src/insight-editor-ai.c', 'src/insight-editor-ai.h', 'src/insight-ai.c', 'src/insight-ai.h', 'src/insight-html-export.c', 'src/insight-html-export.h', 'src/insight-project-report.c', 'src/insight-project-report.h', 'src/insight-git-history.c', 'src/insight-git-history.h', 'src/insight-snapshot-compare.c', 'src/insight-snapshot-compare.h', 'src/insight-backups.c', 'src/insight-backups.h', 'src/insight-revisions.c', 'src/insight-revisions.h', 'src/insight-typing.c', 'src/insight-typing.h', 'src/insight-recording.c', 'src/insight-recording.h', 'src/insight-diff-syntax.c', 'src/insight-diff-syntax.h', 'src/insight-ai-profiles.c', 'src/insight-ai-profiles.h', 'src/insight-outlining.c', 'src/insight-outlining.h', 'src/insight-web-search.c', 'src/insight-web-search.h', 'insight/desktop/web_search.c', 'insight/desktop/web_search.h', 'src/insight-word-highlights.c', 'src/insight-word-highlights.h', 'src/insight-selection-search.c', 'src/insight-selection-search.h', 'src/insight-command-list.c', 'src/insight-command-list.h', 'src/insight-incremental-search.c', 'src/insight-incremental-search.h', 'src/insight-scroll-links.c', 'src/insight-scroll-links.h', 'src/insight-browser-mode.c', 'src/insight-browser-mode.h', 'src/insight-navigation.c', 'src/insight-navigation.h', 'src/insight-paste.c', 'src/insight-paste.h', 'src/insight-completion.c', 'src/insight-completion.h', 'src/insight-languages.c',
 'src/insight-formatting.c',
 'src/insight-formatting.h',
 'src/insight-i18n.c',
 'src/insight-i18n.h',
 'src/insight-languages.h',
	'src/insight-help.h',
	'src/insight-window-list.c',
	'src/insight-window-list.h',
	'insight/core/restore_lines.c', 'insight/core/restore_lines.h', 'insight/core/diff.c',
	'insight/core/repeat_typing.c', 'insight/core/repeat_typing.h',
	'insight/core/delimiter_navigation.c', 'insight/core/delimiter_navigation.h',
	'insight/core/structure_select.c',
	'insight/core/structure_select.h',
	'insight/core/diff.h',
	'src/insight-overview.c',
	'src/insight-overview.h',
	'src/insight-remote.c',
	'src/insight-remote.h',
	'src/insight-workbench.h',
	'src/main.h',
	'src/geany.h',
	'src/msgwindow.c',
	'src/msgwindow.h',
	'src/navqueue.c',
	'src/navqueue.h',
	'src/notebook.c',
	'src/notebook.h',
	'src/pluginextension.c',
	'src/pluginextension.h',
	'src/plugins.c',
	'src/plugins.h',
	'src/pluginutils.c',
	'src/pluginutils.h',
	'src/prefs.c',
	'src/prefs.h',
	'src/printing.c',
	'src/printing.h',
	'src/project.c',
	'src/project.h',
	'src/sciwrappers.c',
	'src/sciwrappers.h',
	'src/search.c',
	'src/search.h',
	'src/socket.c',
	'src/socket.h',
	'src/spawn.c',
	'src/spawn.h',
	'src/stash.c',
	'src/stash.h',
	'src/support.h',
	'src/symbols.c',
	'src/symbols.h',
	'src/templates.c',
	'src/templates.h',
	'src/toolbar.c',
	'src/toolbar.h',
	'src/tools.c',
	'src/tools.h',
	'src/sidebar.c',
	'src/sidebar.h',
	'src/ui_utils.c',
	'src/ui_utils.h',
	'src/utils.c',
	'src/utils.h',
	vte_src,
	gen_src,
	win_src,
	osx_src,
	link_with: insight_native,
	link_args: win_link_args,
	soversion: '0',
	c_args: geany_cflags + [ '-DG_LOG_DOMAIN="Geany"' ],
	include_directories: [iscintilla, include_directories('insight/core', 'insight/desktop')],
	dependencies: [dep_tagmanager, dep_ctags, dep_scintilla] + deps + win_deps + osx_deps,
	install: true
)
dep_libgeany = declare_dependency(
	link_with: libgeany,
	include_directories: [iscintilla, itagmanager, igeany]
)

executable('geany',
	win_resc,
	'src/main.c',
	link_with: libgeany,
	c_args: geany_cflags + [ '-DG_LOG_DOMAIN="Geany"' ],
	dependencies: deps,
	build_rpath: meson.project_build_root(),
	install_rpath: '$ORIGIN/../' + get_option('libdir'),
	install: true,
	win_subsystem: 'windows',
)

i18n = import('i18n')
executable('geany-insight-askpass', 'src/insight-askpass.c', 'insight/desktop/remote_credentials.c', dependencies: deps, install: true, win_subsystem: 'windows')

desktop_file = 'geany.desktop'
desktop_output_file = i18n.merge_file(
	type: 'desktop',
	input: desktop_file + '.in',
	output: desktop_file,
	po_dir: 'po',
	install: true,
	install_dir: join_paths(get_option('datadir'), 'applications')
)

subdir('po')
subdir('data')
subdir('doc')
subdir('icons')
if get_option('plugins')
 subdir('plugins')
endif
subdir('tests')

install_data('COPYING', rename: 'GPL-2', install_dir: cdata.get('GEANY_DATA_DIR'))
install_data(
	'scintilla/License.txt',
	'scintilla/lexilla/License.txt',
	rename: [
		'ScintillaLicense.txt',
		'LexillaLicense.txt'
	],
	install_dir: cdata.get('GEANY_DOC_DIR')
)
misc = [
	'AUTHORS',
	'COPYING',
	'ChangeLog',
	'NEWS',
	'README',
	'THANKS',
	'TODO'
]
install_data(misc, install_dir: cdata.get('GEANY_DOC_DIR'))
if host_machine.system() == 'windows'
	# on Windows, install those files as "Somename.txt" in the prefix as well
	misc_rename = []
	foreach name: misc
		misc_rename += name.substring(0, 1).to_upper() + name.substring(1).to_lower() + '.txt'
	endforeach
	install_data(misc, install_dir: prefix, rename: misc_rename)
endif

# Portable, offline Insight guides consumed by the built-in markup viewer.
install_data(
  'insight/docs/project-database.md', 'insight/docs/guide.md', 'insight/docs/tutorials.md', 'insight/docs/languages.md',
  'insight/docs/scripting.md', 'insight/docs/markup-viewer.rst',
  'insight/docs/directory-compare.md', 'insight/docs/lua-api.md', 'insight/docs/internal-diff.md',
  'insight/docs/desktop-check-builds.md',
  'insight/docs/context-bookmarks.md', 'insight/docs/relations-calls.md',
  'insight/docs/window-layouts.md', 'insight/docs/window-list.md', 'insight/docs/symbol-info.md',
  'insight/docs/panel-find-file.md', 'insight/docs/unified-file-tools.md', 'insight/docs/search-command-dialogs.md', 'insight/docs/source-links.md',
  'insight/docs/panel-project-files.md', 'insight/docs/panel-project-git.md',
  'insight/docs/panel-project-symbols.md',
  'insight/docs/panel-project-folders.md',
  'insight/docs/panel-relations.md',
  'insight/docs/panel-categories.md',
  'insight/docs/panel-file-types.md',
  'insight/docs/panel-bookmarks.md',
  'insight/docs/panel-clips.md',
  'insight/docs/panel-file-symbols.md',
  'insight/docs/panel-context.md', 'insight/docs/search-replace.md', 'insight/docs/syntax-formatting.md',
  'insight/docs/panel-search-results.md',
  'insight/docs/panel-native-symbols.md',
  'insight/docs/panel-documents.md',
  'insight/docs/panel-status.md',
  'insight/docs/panel-compiler.md',
  'insight/docs/panel-messages.md',
  'insight/docs/panel-scratchpad.md',
  'insight/docs/panel-terminal.md',
  'insight/docs/panel-editor.md', 'insight/docs/structural-selection.md', 'insight/docs/editor-history.md', 'insight/docs/insert-tools.md', 'insight/docs/file-text.md', 'insight/docs/renumber.md', 'insight/docs/paragraph-tools.md', 'insight/docs/line-insertion.md', 'insight/docs/text-boundaries.md', 'insight/docs/calculate.md', 'insight/docs/window-scrolling.md', 'insight/docs/word-editing.md', 'insight/docs/command-reference.md', 'insight/docs/restore-lines.md', 'insight/docs/delimiter-navigation.md', 'insight/docs/selection-search.md', 'insight/docs/comparison-layout.md', 'insight/docs/typing-replay.md', 'insight/docs/command-recording.md', 'insight/docs/checkpoint.md', 'insight/docs/backup-versions.md', 'insight/docs/snapshot-comparison.md', 'insight/docs/git-history.md', 'insight/docs/project-report.md', 'insight/docs/html-export.md', 'insight/docs/console-ai.md', 'insight/docs/editor-ai.md', 'insight/docs/replace-files.md',
  'insight/docs/panel-ssh.md', 'insight/docs/panel-activation.md', 'insight/docs/code-outlining.md', 'insight/docs/interface-style.md', 'insight/docs/reload-modified.md', 'insight/docs/ai-connections.md', 'insight/docs/web-search.md', 'insight/docs/word-highlights.md',
  'insight/docs/panel-overview.md',
  'insight/docs/panel-diff-input.md',
  'insight/docs/panel-merge-result.md',
  install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs')

install_data('insight/examples/workbench.lua', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'examples')

install_subdir('insight/locale', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight')
install_subdir('insight/docs/media', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs')
install_subdir('insight/docs/en', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs')
install_subdir('insight/docs/ru', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs')

install_subdir('styles', install_dir: cdata.get('GEANY_DATA_DIR'))
install_subdir('fonts', install_dir: cdata.get('GEANY_DATA_DIR'))

install_data('insight/tools/ai_client.py', 'insight/tools/remote_pack.py', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'tools')

install_data('insight/docs/relation-results.md', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs')

install_data('insight/docs/list-tools.md', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs')

install_data('insight/docs/list-printing.md', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs')

install_data('insight/docs/function-targets.md', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs')

install_data('insight/docs/browser-mode.md', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs')
install_data('insight/docs/media/browser-mode.svg', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs' / 'media')

install_data('insight/docs/scroll-links.md', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs')
install_data('insight/docs/media/scroll-links.svg', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs' / 'media')

install_data('insight/docs/incremental-search.md', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs')
install_data('insight/docs/media/incremental-search.svg', install_dir: cdata.get('GEANY_DATA_DIR') / 'insight' / 'docs' / 'media')
