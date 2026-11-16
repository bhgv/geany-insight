#include "insight-workbench.h"
/*
 *      search.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2006 The Geany contributors
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License along
 *      with this program; if not, write to the Free Software Foundation, Inc.,
 *      51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

/*
 * Find, Replace, Find in Files dialog related functions.
 * Note that the basic text find functions are in document.c.
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "search.h"
#include "insight-selection-search.h"

#include "app.h"
#include "dialogs.h"
#include "document.h"
#include "encodings.h"
#include "encodingsprivate.h"
#include "keyfile.h"
#include "msgwindow.h"
#include "prefs.h"
#include "sciwrappers.h"
#include "stash.h"
#include "support.h"
#include "toolbar.h"
#include "ui_utils.h"
#include "utils.h"
#include "win32.h"

#include <unistd.h>
#include <string.h>
#include <ctype.h>

#include <gdk/gdkkeysyms.h>

#define MIN_DLG_BUTTON_SIZE 130

enum
{
	GEANY_RESPONSE_FIND = 1,
	GEANY_RESPONSE_FIND_PREVIOUS,
	GEANY_RESPONSE_FIND_IN_FILE,
	GEANY_RESPONSE_FIND_IN_SESSION,
	GEANY_RESPONSE_MARK,
	GEANY_RESPONSE_REPLACE,
	GEANY_RESPONSE_REPLACE_AND_FIND,
	GEANY_RESPONSE_REPLACE_IN_SESSION,
	GEANY_RESPONSE_REPLACE_IN_FILE,
	GEANY_RESPONSE_REPLACE_IN_SEL
};


enum
{
	FILES_MODE_ALL,
	FILES_MODE_PROJECT,
	FILES_MODE_CUSTOM
};


GeanySearchData search_data;
GeanySearchPrefs search_prefs;


static struct
{
	gboolean fif_regexp;
	gboolean fif_case_sensitive;
	gboolean fif_match_whole_word;
	gboolean fif_invert_results;
	gboolean fif_recursive;
	gboolean fif_use_extra_options;
	gchar *fif_extra_options;
	gint fif_files_mode;
	gchar *fif_files;
	gboolean find_regexp;
	gboolean find_regexp_multiline;
	gboolean find_escape_sequences;
	gboolean find_case_sensitive;
	gboolean find_match_whole_word;
	gboolean find_match_word_start;
	gboolean find_close_dialog;
	gboolean replace_regexp;
	gboolean replace_regexp_multiline;
	gboolean replace_escape_sequences;
	gboolean replace_case_sensitive;
	gboolean replace_match_whole_word;
	gboolean replace_match_word_start;
	gboolean replace_search_backwards;
	gboolean replace_close_dialog;
}
settings;

static StashGroup *fif_prefs = NULL;
static StashGroup *find_prefs = NULL;
static StashGroup *replace_prefs = NULL;


static struct
{
	GtkWidget	*dialog;
	GtkWidget	*entry;
	gboolean	all_expanded;
	gint		position[2]; /* x, y */
}
find_dlg = {NULL, NULL, FALSE, {0, 0}};

static struct
{
	GtkWidget	*dialog;
	GtkWidget	*find_combobox;
	GtkWidget	*find_entry;
	GtkWidget	*replace_combobox;
	GtkWidget	*replace_entry;
	gboolean	all_expanded;
	gint		position[2]; /* x, y */
}
replace_dlg = {NULL, NULL, NULL, NULL, NULL, FALSE, {0, 0}};

static struct
{
	GtkWidget	*dialog;
	GtkWidget	*dir_combo;
	GtkWidget	*files_combo;
	GtkWidget	*search_combo;
	GtkWidget	*encoding_combo;
	GtkWidget	*files_mode_combo;
	gint		position[2]; /* x, y */
}
fif_dlg = {NULL, NULL, NULL, NULL, NULL, NULL, {0, 0}};


static GRegex *compile_regex(const gchar *str, GeanyFindFlags sflags);


static void
on_find_replace_checkbutton_toggled(GtkToggleButton *togglebutton, gpointer user_data);

static void
on_find_dialog_response(GtkDialog *dialog, gint response, gpointer user_data);

static void
on_find_entry_activate(GtkEntry *entry, gpointer user_data);

static void
on_find_entry_activate_backward(GtkEntry *entry, gpointer user_data);

static void
on_replace_dialog_response(GtkDialog *dialog, gint response, gpointer user_data);

static void
on_replace_find_entry_activate(GtkEntry *entry, gpointer user_data);

static void
on_replace_entry_activate(GtkEntry *entry, gpointer user_data);

static void
on_find_in_files_dialog_response(GtkDialog *dialog, gint response, gpointer user_data);

/* EN: Compatibility entry point; opts and enc belonged to external-tool search.
 * Native text and UTF-8 dir are handled internally; return whether UI is ready.
 * RU: Совместимый вход; opts и enc относились к внешней утилите. text и каталог
 * UTF-8 обрабатываются внутри редактора; возвращает готовность интерфейса. */
static gboolean
search_find_in_files(const gchar *utf8_search_text, const gchar *utf8_dir,
 const gchar *opts, const gchar *enc)
{
 (void)opts;
 (void)enc;
 return insight_workbench_show_project_search(utf8_search_text,utf8_dir);
}


static GRegex *compile_regex(const gchar *str, GeanyFindFlags sflags)
{
	GRegex *regex;
	GError *error = NULL;
	gint rflags = 0;

	if (sflags & GEANY_FIND_MULTILINE)
		rflags |= G_REGEX_MULTILINE;
	if (~sflags & GEANY_FIND_MATCHCASE)
		rflags |= G_REGEX_CASELESS;
	if (sflags & (GEANY_FIND_WHOLEWORD | GEANY_FIND_WORDSTART))
	{
		geany_debug("%s: Unsupported regex flags found!", G_STRFUNC);
	}

	regex = g_regex_new(str, rflags, 0, &error);
	if (!regex)
	{
		ui_set_statusbar(FALSE, _("Bad regex: %s"), error->message);
		g_error_free(error);
	}
	return regex;
}


/* groups that don't exist are handled OK as len = end - start = (-1) - (-1) = 0 */
static gchar *get_regex_match_string(const gchar *text, const GeanyMatchInfo *match, guint nth)
{
	const gint start = match->matches[nth].start;
	const gint end = match->matches[nth].end;
	return g_strndup(&text[start], end - start);
}


static gint find_regex(ScintillaObject *sci, guint pos, GRegex *regex, gboolean multiline, GeanyMatchInfo *match)
{
	GError *match_error = NULL;
	const gchar *text;
	GMatchInfo *minfo;
	guint document_length;
	gint ret = -1;
	gint offset = 0;

	document_length = (guint)sci_get_length(sci);
	if (document_length <= 0)
		return -1; /* skip empty documents */

	g_return_val_if_fail(pos <= document_length, -1);

	if (multiline)
	{
		/* Warning: any SCI calls will invalidate 'text' after calling SCI_GETCHARACTERPOINTER */
		text = (void*)SSM(sci, SCI_GETCHARACTERPOINTER, 0, 0);
		g_regex_match_full(regex, text, -1, pos, 0, &minfo, &match_error);
	}
	else /* single-line mode, manually match against each line */
	{
		gint line = sci_get_line_from_position(sci, pos);

		for (;;)
		{
			gint start = sci_get_position_from_line(sci, line);
			gint end = sci_get_line_end_position(sci, line);

			text = (void*)SSM(sci, SCI_GETRANGEPOINTER, start, end - start);
			if (g_regex_match_full(regex, text, end - start, pos - start, 0, &minfo, &match_error))
			{
				offset = start;
				break;
			}
			else if (match_error) /* match error, abort search */
			{
				break;
			}
			else /* not found, try next line */
			{
				line ++;
				if (line >= sci_get_line_count(sci))
					break;
				pos = sci_get_position_from_line(sci, line);
				/* don't free last info, it's freed below */
				g_match_info_free(minfo);
			}
		}
	}

	/* if there was a match error, log it because it's probably a bug */
	if (match_error)
	{
		g_warning("Failed to match regex: %s", match_error->message);
		g_error_free(match_error);
	}

	/* Warning: minfo will become invalid when 'text' does! */
	if (g_match_info_matches(minfo))
	{
		guint i;

		/* copy whole match text and offsets before they become invalid */
		SETPTR(match->match_text, g_match_info_fetch(minfo, 0));

		foreach_range(i, G_N_ELEMENTS(match->matches))
		{
			gint start = -1, end = -1;

			g_match_info_fetch_pos(minfo, (gint)i, &start, &end);
			match->matches[i].start = offset + start;
			match->matches[i].end = offset + end;
		}
		match->start = match->matches[0].start;
		match->end = match->matches[0].end;
		ret = match->start;
	}
	g_match_info_free(minfo);
	return ret;
}


static gint geany_find_flags_to_sci_flags(GeanyFindFlags flags)
{
	g_warn_if_fail(! (flags & GEANY_FIND_REGEXP) || ! (flags & GEANY_FIND_MULTILINE));

	return ((flags & GEANY_FIND_MATCHCASE) ? SCFIND_MATCHCASE : 0) |
		((flags & GEANY_FIND_WHOLEWORD) ? SCFIND_WHOLEWORD : 0) |
		((flags & GEANY_FIND_REGEXP) ? SCFIND_REGEXP | SCFIND_POSIX : 0) |
		((flags & GEANY_FIND_WORDSTART) ? SCFIND_WORDSTART : 0);
}


gint search_find_prev(ScintillaObject *sci, const gchar *str, GeanyFindFlags flags, GeanyMatchInfo **match_)
{
	gint ret;

	g_return_val_if_fail(! (flags & GEANY_FIND_REGEXP), -1);

	ret = sci_search_prev(sci, geany_find_flags_to_sci_flags(flags), str);
	if (ret != -1 && match_)
		*match_ = match_info_new(flags, ret, ret + strlen(str));
	return ret;
}


gint search_find_next(ScintillaObject *sci, const gchar *str, GeanyFindFlags flags, GeanyMatchInfo **match_)
{
	GeanyMatchInfo *match;
	GRegex *regex;
	gint ret = -1;
	gint pos;

	if (~flags & GEANY_FIND_REGEXP)
	{
		ret = sci_search_next(sci, geany_find_flags_to_sci_flags(flags), str);
		if (ret != -1 && match_)
			*match_ = match_info_new(flags, ret, ret + strlen(str));
		return ret;
	}

	regex = compile_regex(str, flags);
	if (!regex)
		return -1;

	match = match_info_new(flags, 0, 0);

	pos = sci_get_current_position(sci);
	ret = find_regex(sci, pos, regex, flags & GEANY_FIND_MULTILINE, match);
	/* avoid re-matching the same position in case of empty matches */
	if (ret == pos && match->matches[0].start == match->matches[0].end)
		ret = find_regex(sci, pos + 1, regex, flags & GEANY_FIND_MULTILINE, match);
	if (ret >= 0)
		sci_set_selection(sci, match->start, match->end);

	if (ret != -1 && match_)
		*match_ = match;
	else
		geany_match_info_free(match);

	g_regex_unref(regex);
	return ret;
}


gint search_replace_match(ScintillaObject *sci, const GeanyMatchInfo *match, const gchar *replace_text)
{
	GString *str;
	gint ret = 0;
	gint i = 0;

	sci_set_target_start(sci, match->start);
	sci_set_target_end(sci, match->end);

	if (! (match->flags & GEANY_FIND_REGEXP))
		return sci_replace_target(sci, replace_text, FALSE);

	str = g_string_new(replace_text);
	while (str->str[i])
	{
		gchar *ptr = &str->str[i];
		gchar *grp;
		gchar c;

		if (ptr[0] != '\\')
		{
			i++;
			continue;
		}
		c = ptr[1];
		/* backslash or unnecessary escape */
		if (c == '\\' || !isdigit(c))
		{
			g_string_erase(str, i, 1);
			i++;
			continue;
		}
		/* digit escape */
		g_string_erase(str, i, 2);
		/* fix match offsets by subtracting index of whole match start from the string */
		grp = get_regex_match_string(match->match_text - match->matches[0].start, match, c - '0');
		g_string_insert(str, i, grp);
		i += strlen(grp);
		g_free(grp);
	}
	ret = sci_replace_target(sci, str->str, FALSE);
	g_string_free(str, TRUE);
	return ret;
}


gint search_find_text(ScintillaObject *sci, GeanyFindFlags flags, struct Sci_TextToFind *ttf, GeanyMatchInfo **match_)
{
	GeanyMatchInfo *match = NULL;
	GRegex *regex;
	gint ret;

	if (~flags & GEANY_FIND_REGEXP)
	{
		ret = sci_find_text(sci, geany_find_flags_to_sci_flags(flags), ttf);
		if (ret != -1 && match_)
			*match_ = match_info_new(flags, ttf->chrgText.cpMin, ttf->chrgText.cpMax);
		return ret;
	}

	regex = compile_regex(ttf->lpstrText, flags);
	if (!regex)
		return -1;

	match = match_info_new(flags, 0, 0);

	ret = find_regex(sci, ttf->chrg.cpMin, regex, flags & GEANY_FIND_MULTILINE, match);
	if (ret >= ttf->chrg.cpMax)
		ret = -1;
	else if (ret >= 0)
	{
		ttf->chrgText.cpMin = match->start;
		ttf->chrgText.cpMax = match->end;
	}

	if (ret != -1 && match_)
		*match_ = match;
	else
		geany_match_info_free(match);

	g_regex_unref(regex);
	return ret;
}


static gint find_document_usage(GeanyDocument *doc, const gchar *search_text, GeanyFindFlags flags)
{
	gchar *buffer, *short_file_name;
	struct Sci_TextToFind ttf;
	gint count = 0;
	gint prev_line = -1;
	GSList *match, *matches;

	g_return_val_if_fail(DOC_VALID(doc), 0);

	short_file_name = g_path_get_basename(DOC_FILENAME(doc));

	ttf.chrg.cpMin = 0;
	ttf.chrg.cpMax = sci_get_length(doc->editor->sci);
	ttf.lpstrText = (gchar *)search_text;

	matches = find_range(doc->editor->sci, flags, &ttf);
	foreach_slist (match, matches)
	{
		GeanyMatchInfo *info = match->data;
		gint line = sci_get_line_from_position(doc->editor->sci, info->start);

		if (line != prev_line)
		{
			buffer = sci_get_line(doc->editor->sci, line);
			msgwin_msg_add(COLOR_BLACK, line + 1, doc,
				"%s:%d: %s", short_file_name, line + 1, g_strstrip(buffer));
			g_free(buffer);
			prev_line = line;
		}
		count++;

		geany_match_info_free(info);
	}
	g_slist_free(matches);
	g_free(short_file_name);
	return count;
}


/* EN: Find search_text using flags in project/session or current document;
 * original_search_text labels legacy results. Native search includes closed files.
 * RU: Ищет search_text с flags в проекте/сеансе или текущем файле;
 * original_search_text подписывает прежние результаты. Учитывает закрытые файлы. */
void search_find_usage(const gchar *search_text, const gchar *original_search_text,
		GeanyFindFlags flags, gboolean in_session)
{
 if(!EMPTY(search_text) && insight_workbench_find_usage(search_text,flags,in_session)) return;
	GeanyDocument *doc;
	gint count = 0;

	doc = document_get_current();
	g_return_if_fail(doc != NULL);

	if (G_UNLIKELY(EMPTY(search_text)))
	{
		utils_beep();
		return;
	}
	reset_msgwin();

	if (! in_session)
	{	/* use current document */
		count = find_document_usage(doc, search_text, flags);
	}
	else
	{
		guint i;
		for (i = 0; i < documents_array->len; i++)
		{
			if (documents[i]->is_valid)
			{
				count += find_document_usage(documents[i], search_text, flags);
			}
		}
	}

	if (count == 0) /* no matches were found */
	{
		ui_set_statusbar(FALSE, _("No matches found for \"%s\"."), original_search_text);
		msgwin_msg_add(COLOR_BLUE, -1, NULL, _("No matches found for \"%s\"."), original_search_text);
	}
	else
	{
		ui_set_statusbar(FALSE, ngettext(
			"Found %d match for \"%s\".", "Found %d matches for \"%s\".", count),
			count, original_search_text);
		msgwin_msg_add(COLOR_BLUE, -1, NULL, ngettext(
			"Found %d match for \"%s\".", "Found %d matches for \"%s\".", count),
			count, original_search_text);
	}
}


/* ttf is updated to include the last match position (ttf->chrg.cpMin) and
 * the new search range end (ttf->chrg.cpMax).
 * Note: Normally you would call sci_start/end_undo_action() around this call. */
guint search_replace_range(ScintillaObject *sci, struct Sci_TextToFind *ttf,
		GeanyFindFlags flags, const gchar *replace_text)
{
	gint count = 0;
	gint offset = 0; /* difference between search pos and replace pos */
	GSList *match, *matches;

	g_return_val_if_fail(sci != NULL && ttf->lpstrText != NULL && replace_text != NULL, 0);
	if (! *ttf->lpstrText)
		return 0;

	matches = find_range(sci, flags, ttf);
	foreach_slist (match, matches)
	{
		GeanyMatchInfo *info = match->data;
		gint replace_len;

		info->start += offset;
		info->end += offset;

		replace_len = search_replace_match(sci, info, replace_text);
		offset += replace_len - (info->end - info->start);
		count ++;

		/* on last match, update the last match/new range end */
		if (! match->next)
		{
			ttf->chrg.cpMin = info->start;
			ttf->chrg.cpMax += offset;
		}

		geany_match_info_free(info);
	}
	g_slist_free(matches);

	return count;
}


void search_find_again(gboolean change_direction)
{
    if (insight_selection_search_find_again(change_direction)) return;
	if (insight_workbench_file_find_again(change_direction ? !search_data.backwards : search_data.backwards)) return;
	GeanyDocument *doc = document_get_current();

	g_return_if_fail(doc != NULL);

	if (search_data.text)
	{
		gboolean forward = ! search_data.backwards;
		gint result = document_find_text(doc, search_data.text, search_data.original_text, search_data.flags,
			change_direction ? forward : !forward, NULL, FALSE);

		if (result > -1)
			editor_display_current_line(doc->editor, 0.3F);

		if (search_data.search_bar)
			ui_set_search_entry_background(
				toolbar_get_widget_child_by_name("SearchEntry"), (result > -1));
	}
}

/* Literal selection-search state shared with Find/Replace and F3. Do not run a
 * search or change the current source selection while populating controls. */
void search_insight_set_pattern(const gchar *pattern,GeanyFindFlags flags,gboolean backwards)
{
    setup_find_next(pattern);search_data.flags=flags;search_data.backwards=backwards;
    settings.find_regexp=settings.find_regexp_multiline=settings.find_escape_sequences=FALSE;
    settings.find_match_word_start=FALSE;settings.find_case_sensitive=!!(flags&GEANY_FIND_MATCHCASE);settings.find_match_whole_word=!!(flags&GEANY_FIND_WHOLEWORD);
    settings.replace_regexp=settings.replace_regexp_multiline=settings.replace_escape_sequences=FALSE;
    settings.replace_match_word_start=FALSE;settings.replace_case_sensitive=settings.find_case_sensitive;settings.replace_match_whole_word=settings.find_match_whole_word;settings.replace_search_backwards=backwards;
    if(find_dlg.dialog) { stash_group_display(find_prefs,find_dlg.dialog);gtk_entry_set_text(GTK_ENTRY(find_dlg.entry),pattern); }
    if(replace_dlg.dialog) { stash_group_display(replace_prefs,replace_dlg.dialog);gtk_entry_set_text(GTK_ENTRY(replace_dlg.find_entry),pattern); }
}
