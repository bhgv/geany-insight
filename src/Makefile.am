## Process this file with automake to produce Makefile.in

SUBDIRS = tagmanager

EXTRA_DIST = \
	gb.c \
	plugindata.h \
	documentprivate.h \
	encodingsprivate.h \
	filetypesprivate.h \
	keybindingsprivate.h \
	pluginprivate.h \
	projectprivate.h

AM_CPPFLAGS = \
	-I$(top_srcdir) \
	-I$(top_srcdir)/scintilla/include \
	-I$(top_srcdir)/scintilla/lexilla/include \
	-I$(srcdir)/tagmanager \
	-DGTK \
	-DGEANY_PRIVATE \
	-DG_LOG_DOMAIN=\""Geany"\" \
	@GTK_CFLAGS@ \
	$(MAC_INTEGRATION_CFLAGS)

bin_PROGRAMS = geany
lib_LTLIBRARIES = libgeany.la

geany_SOURCES = main.c
geany_LDADD = libgeany.la $(GTK_LIBS) $(INTLLIBS)
geany_LDFLAGS =

if ENABLE_BINRELOC
geany_LDFLAGS += -Wl,-rpath='$$ORIGIN/../lib'
endif

geany_includedir = $(includedir)/geany
geany_include_HEADERS = \
	app.h \
	build.h \
	dialogs.h \
	document.h \
	editor.h \
	encodings.h \
	filetypes.h \
	geany.h \
	gtkcompat.h \
	highlighting.h \
	keybindings.h \
	main.h \
	msgwindow.h \
	navqueue.h \
	plugindata.h \
	pluginextension.h \
	pluginutils.h \
	prefs.h \
	project.h \
	sciwrappers.h \
	search.h \
	spawn.h \
	stash.h \
	support.h \
	symbols.h \
	templates.h \
	toolbar.h \
	ui_utils.h \
	utils.h

libgeany_la_SOURCES = \
	about.c about.h \
	app.h \
	build.c build.h \
	callbacks.c callbacks.h \
	dialogs.c dialogs.h \
	document.c document.h \
	editor.c editor.h \
	encodings.c encodings.h \
	filetypes.c filetypes.h \
	geanyentryaction.c geanyentryaction.h \
	geanymenubuttonaction.c geanymenubuttonaction.h \
	geanyobject.c geanyobject.h \
	geanywraplabel.c geanywraplabel.h \
	gtkcompat.h \
	highlighting.c highlighting.h \
	highlightingmappings.h \
	keybindings.c keybindings.h \
	keyfile.c keyfile.h \
	log.c log.h \
	libmain.c main.h geany.h \
	msgwindow.c msgwindow.h \
	navqueue.c navqueue.h \
	notebook.c notebook.h \
	pluginextension.c pluginextension.h \
	plugins.c plugins.h \
	pluginutils.c pluginutils.h \
	prefs.c prefs.h \
	printing.c printing.h \
	project.c project.h \
	sciwrappers.c sciwrappers.h \
	search.c search.h \
	socket.c socket.h \
	spawn.c spawn.h \
	stash.c stash.h \
	support.h \
	symbols.c symbols.h \
	templates.c templates.h \
	toolbar.c toolbar.h \
	tools.c tools.h \
	sidebar.c sidebar.h \
	ui_utils.c ui_utils.h \
	utils.c utils.h

if ENABLE_BINRELOC
libgeany_la_SOURCES += prefix.c prefix.h
endif

if ENABLE_MAC_INTEGRATION
libgeany_la_SOURCES += osx.c osx.h
endif

libgeany_la_CFLAGS  = $(AM_CPPFLAGS) @LIBGEANY_CFLAGS@
libgeany_la_LDFLAGS = @LIBGEANY_LDFLAGS@

libgeany_la_LIBADD = \
	$(top_builddir)/scintilla/liblexilla.la \
	$(top_builddir)/scintilla/libscintilla.la \
	$(builddir)/tagmanager/libtagmanager.la \
	@GTK_LIBS@ \
	$(MAC_INTEGRATION_LIBS) \
	$(INTLLIBS)

# tell automake we have a C++ file so it uses the C++ linker we need for Scintilla
nodist_EXTRA_libgeany_la_SOURCES = dummy1.cxx
nodist_EXTRA_geany_SOURCES = dummy2.cxx

CLEANFILES =

if MINGW
# build Geany for Windows (possibly on non-Windows systems -- cross-compile)

AM_CPPFLAGS += \
	-DGEANY_DATADIR=\"data\" \
	-DGEANY_DOCDIR=\"\" \
	-DGEANY_LIBDIR=\"\" \
	-DGEANY_LIBEXECDIR=\"\" \
	-DGEANY_LOCALEDIR=\"\" \
	-DGEANY_PREFIX=\"\"

geany_LDADD += geany_private.res

WINDRES = $(host_alias)-windres

geany_private.res: $(top_srcdir)/geany_private.rc
	$(WINDRES) -i $(top_srcdir)/geany_private.rc --input-format=rc -o $@ -O coff

libgeany_la_SOURCES += win32.c win32.h
libgeany_la_LIBADD  += -lole32 -lwsock32 -lcomdlg32
libgeany_la_LDFLAGS += -mwindows -mms-bitfields -no-undefined
geany_LDFLAGS       += -mwindows -mms-bitfields

CLEANFILES          += geany_private.res

else
# build Geany for all other platforms

AM_CPPFLAGS += \
	-DGEANY_DATADIR=\""$(datadir)"\" \
	-DGEANY_DOCDIR=\""$(docdir)"\" \
	-DGEANY_LIBDIR=\""$(libdir)"\" \
	-DGEANY_LIBEXECDIR=\""$(libexecdir)"\" \
	-DGEANY_LOCALEDIR=\""$(localedir)"\" \
	-DGEANY_PREFIX=\""$(prefix)"\"

libgeany_la_SOURCES += vte.c vte.h

endif

callbacks.c: signallist.i

glade_file=$(top_srcdir)/data/geany.glade

signallist.i: $(glade_file) Makefile
	$(AM_V_GEN)$(top_srcdir)/scripts/gen-signallist.sh $(glade_file) $@

CLEANFILES += signallist.i

# install the run script
if MINGW
dist_pkglibexec_SCRIPTS = geany-run-helper.bat
endif

# Ubuntu ld has a bug so that libtool sees /usr/local/lib as a system path so
# doesn't add RPATH, but ld requires explicit ldconfig there, unlike when
# installing in /usr/lib.  So, workaround this by calling it explicitly when
# installing in /usr/local/lib and libgeany is not found by the linker.
fix-ubuntu-libdir:
	if test "$(DESTDIR)$(libdir)" = /usr/local/lib && \
	   LANG=C ldd "$(DESTDIR)$(bindir)/geany" | grep -q 'libgeany.*not found' \
	; then \
		ldconfig "$(DESTDIR)$(libdir)"; \
	fi

install-exec-hook: fix-ubuntu-libdir

# Integrated Insight workbench and embedded Lua (no plugin needed).
AM_CPPFLAGS += -I$(top_srcdir)/insight/core -I$(top_srcdir)/insight/vendor/lua
libgeany_la_SOURCES += \
	insight-workbench.c \
	insight-symbol-view.c \
	insight-symbol-view.h \
	insight-diff.c \
	insight-diff.h \
	insight-template-fields.c \
	insight-template-fields.h \
	insight-relation-tree.c \
	insight-disk-browser.c \
	insight-disk-browser.h \
	insight-list-style.c \
	insight-list-style.h \
	insight-relation-tree.h \
	insight-relation-graph.c \
	insight-relation-graph.h \
	insight-help.c \
	insight-languages.c \
	insight-formatting.c \
	insight-formatting.h \
	insight-i18n.c \
	insight-i18n.h \
	insight-languages.h \
	insight-help.h \
	insight-window-list.c \
	insight-window-list.h \
	../insight/core/diff.c \
	../insight/core/structure_select.c \
	../insight/core/structure_select.h \
	../insight/core/relation_graph_layout.h \
	../insight/core/diff.h \
	insight-workbench.h \
	insight-index.c \
	insight-scopes.c \
	insight-index.h \
	../insight/core/context.c \
	../insight/core/context.h \
	../insight/core/document.c \
	../insight/core/document.h \
	../insight/core/index.c \
	../insight/core/index.h \
	../insight/core/occurrences.c \
	../insight/core/occurrences.h \
	../insight/core/runtime.c \
	../insight/core/runtime.h \
	../insight/desktop/index_cache.c \
	../insight/desktop/index_cache.h \
	../insight/desktop/disk_operation.c \
	../insight/desktop/disk_operation.h \
	../insight/desktop/project_files.c \
	../insight/desktop/project_files.h \
	../insight/desktop/master_list.c \
	../insight/desktop/master_list.h \
	../insight/desktop/master_sync.c \
	../insight/desktop/master_sync.h \
	../insight/desktop/context_search.c \
	../insight/desktop/context_search.h \
	../insight/desktop/notebook_store.c \
	../insight/desktop/notebook_store.h \
	../insight/desktop/snippet.c \
	../insight/desktop/snippet.h \
	../insight/desktop/workspace_watch.c \
	../insight/desktop/workspace_watch.h \
	../insight/desktop/source_mask.c \
	../insight/desktop/source_mask.h \
	../insight/desktop/type_resolve.c \
	../insight/desktop/type_resolve.h \
	../insight/desktop/relation_query.c \
	../insight/desktop/relation_query.h \
	../insight/desktop/layout_state.c \
	../insight/desktop/layout_state.h \
	../insight/desktop/lookup_filter.c \
	../insight/desktop/lookup_filter.h \
	../insight/desktop/extensions.c \
	../insight/desktop/extensions.h \
	../insight/vendor/lua/lapi.c \
	../insight/vendor/lua/lapi.h \
	../insight/vendor/lua/lauxlib.c \
	../insight/vendor/lua/lauxlib.h \
	../insight/vendor/lua/lbaselib.c \
	../insight/vendor/lua/lcode.c \
	../insight/vendor/lua/lcode.h \
	../insight/vendor/lua/lctype.c \
	../insight/vendor/lua/lctype.h \
	../insight/vendor/lua/ldebug.c \
	../insight/vendor/lua/ldebug.h \
	../insight/vendor/lua/ldo.c \
	../insight/vendor/lua/ldo.h \
	../insight/vendor/lua/ldump.c \
	../insight/vendor/lua/lfunc.c \
	../insight/vendor/lua/lfunc.h \
	../insight/vendor/lua/lgc.c \
	../insight/vendor/lua/lgc.h \
	../insight/vendor/lua/ljumptab.h \
	../insight/vendor/lua/llex.c \
	../insight/vendor/lua/llex.h \
	../insight/vendor/lua/llimits.h \
	../insight/vendor/lua/lmathlib.c \
	../insight/vendor/lua/lmem.c \
	../insight/vendor/lua/lmem.h \
	../insight/vendor/lua/lobject.c \
	../insight/vendor/lua/lobject.h \
	../insight/vendor/lua/lopcodes.c \
	../insight/vendor/lua/lopcodes.h \
	../insight/vendor/lua/lopnames.h \
	../insight/vendor/lua/lparser.c \
	../insight/vendor/lua/lparser.h \
	../insight/vendor/lua/lprefix.h \
	../insight/vendor/lua/lstate.c \
	../insight/vendor/lua/lstate.h \
	../insight/vendor/lua/lstring.c \
	../insight/vendor/lua/lstring.h \
	../insight/vendor/lua/lstrlib.c \
	../insight/vendor/lua/ltable.c \
	../insight/vendor/lua/ltable.h \
	../insight/vendor/lua/ltablib.c \
	../insight/vendor/lua/ltests.c \
	../insight/vendor/lua/ltests.h \
	../insight/vendor/lua/ltm.c \
	../insight/vendor/lua/ltm.h \
	../insight/vendor/lua/lua.h \
	../insight/vendor/lua/luaconf.h \
	../insight/vendor/lua/lualib.h \
	../insight/vendor/lua/lundump.c \
	../insight/vendor/lua/lundump.h \
	../insight/vendor/lua/lutf8lib.c \
	../insight/vendor/lua/lvm.c \
	../insight/vendor/lua/lvm.h \
	../insight/vendor/lua/lzio.c \
	../insight/vendor/lua/lzio.h
libgeany_la_LIBADD += -lm

libgeany_la_SOURCES += insight-themes.c insight-themes.h
