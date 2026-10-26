/*
 *      navqueue.c - this file is part of Geany, a fast and lightweight IDE
 *
 *      Copyright 2007 The Geany contributors
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
 * Simple code navigation
 */

#ifdef HAVE_CONFIG_H
# include "config.h"
#endif

#include "navqueue.h"

#include "document.h"
#include "geanyobject.h"
#include "sciwrappers.h"
#include "toolbar.h"
#include "main.h"
#include "ui_utils.h"
#include "insight-i18n.h"
#include "utils.h"

#include <gtk/gtk.h>


/* for the navigation history queue */
typedef struct
{
	gchar *file;	/* Owned filename: Save As may replace the document string. */
	gint pos;
} filepos;

static GQueue *navigation_queue;
static guint nav_queue_pos;
static guint64 navigation_serial;
static struct { gboolean forward; guint64 serial; guint document; gint position; } back_toggle;

#define HISTORY_LIMIT 512

static GtkAction *navigation_buttons[2];



static void filepos_free(gpointer data)
{
 filepos *position=data;if(position) {g_free(position->file);g_free(position);}
}

void navqueue_init(void)
{
	navigation_queue = g_queue_new();
	nav_queue_pos = 0;

	navigation_buttons[0] = toolbar_get_action_by_name("NavBack");
	navigation_buttons[1] = toolbar_get_action_by_name("NavFor");

	gtk_action_set_sensitive(navigation_buttons[0], FALSE);
	gtk_action_set_sensitive(navigation_buttons[1], FALSE);
}


void navqueue_free(void)
{
	while (! g_queue_is_empty(navigation_queue))
	{
		filepos_free(g_queue_pop_tail(navigation_queue));
	}
	g_queue_free(navigation_queue);
	navigation_queue = NULL;
	navigation_serial++;
}


static void adjust_buttons(void)
{
 guint length=g_queue_get_length(navigation_queue);gboolean moved=FALSE;
 GeanyDocument *doc=document_get_current();filepos *at=g_queue_peek_nth(navigation_queue,nav_queue_pos);
 if(at && doc && doc->file_name) moved=!utils_str_equal(at->file,doc->file_name) || at->pos!=sci_get_current_position(doc->editor->sci);
 gtk_action_set_sensitive(navigation_buttons[0],length>0 && (moved || nav_queue_pos+1<length));
 gtk_action_set_sensitive(navigation_buttons[1],length>0 && nav_queue_pos>0);
}


static gboolean
queue_pos_matches(guint queue_pos, const gchar *fname, gint pos)
{
	if (queue_pos < g_queue_get_length(navigation_queue))
	{
		filepos *fpos = g_queue_peek_nth(navigation_queue, queue_pos);

		return (utils_str_equal(fpos->file, fname) && fpos->pos == pos);
	}
	return FALSE;
}


static void add_position(const gchar *utf8_filename, gint pos, gboolean keep_forward)
{
	filepos *npos;
	guint i;

	if (queue_pos_matches(nav_queue_pos, utf8_filename, pos))
		return;	/* prevent duplicates */

	npos = g_new0(filepos, 1);
	npos->file = g_strdup(utf8_filename);
	npos->pos = pos;

	/* if we've jumped to a new position from inside the queue rather than going forward */
	if (!keep_forward && nav_queue_pos > 0)
	{
		for (i = 0; i < nav_queue_pos; i++)
		{
			filepos_free(g_queue_pop_head(navigation_queue));
		}
		nav_queue_pos = 0;
	}
	if(keep_forward) g_queue_push_nth(navigation_queue,npos,nav_queue_pos);
 else g_queue_push_head(navigation_queue,npos);
	while (g_queue_get_length(navigation_queue) > HISTORY_LIMIT)
		filepos_free(g_queue_pop_tail(navigation_queue));
	navigation_serial++;
	adjust_buttons();
}


static void add_new_position(const gchar *file,gint pos) {add_position(file,pos,FALSE);}
/* Back/Forward capture a manually moved caret without discarding the forward
 * branch. Only a new explicit jump creates a new navigation branch. */
static void remember_current(void) {
 GeanyDocument *doc=document_get_current();
 if(doc && doc->file_name) add_position(doc->file_name,sci_get_current_position(doc->editor->sci),TRUE);
}

/**
 *  Adds old file position and new file position to the navqueue, then goes to the new position.
 *
 *  @param old_doc The document of the previous position, if set as invalid (@c NULL) then no old
 *         position is set
 *  @param new_doc The document of the new position, must be valid.
 *  @param line the line number of the new position. It is counted with 1 as the first line, not 0.
 *
 *  @return @c TRUE if the cursor has changed the position to @a line or @c FALSE otherwise.
 **/
GEANY_API_SYMBOL
gboolean navqueue_goto_line(GeanyDocument *old_doc, GeanyDocument *new_doc, gint line)
{
	gint pos;

	g_return_val_if_fail(old_doc == NULL || old_doc->is_valid, FALSE);
	g_return_val_if_fail(DOC_VALID(new_doc), FALSE);
	g_return_val_if_fail(line >= 1, FALSE);

	pos = sci_get_position_from_line(new_doc->editor->sci, line - 1);
	if (pos < 0) return FALSE;
	return navqueue_goto_position(old_doc, new_doc, pos);
}

/* Internal byte-position counterpart; delimiter navigation must not record
 * only a line start when its destination is in the middle of that line. */
gboolean navqueue_goto_position(GeanyDocument *old_doc, GeanyDocument *new_doc, gint pos)
{
	g_return_val_if_fail(old_doc == NULL || old_doc->is_valid, FALSE);
	g_return_val_if_fail(DOC_VALID(new_doc), FALSE);
	g_return_val_if_fail(pos >= 0 && pos <= sci_get_length(new_doc->editor->sci), FALSE);

	/* first add old file position */
	if (old_doc != NULL && old_doc->file_name)
	{
		gint cur_pos = sci_get_current_position(old_doc->editor->sci);

		add_new_position(old_doc->file_name, cur_pos);
	}

	/* now add new file position */
	if (new_doc->file_name)
	{
		add_new_position(new_doc->file_name, pos);
	}

	return editor_goto_pos(new_doc->editor, pos, TRUE);
}


static gboolean goto_file_pos(const gchar *file, gint pos)
{
	GeanyDocument *doc = document_find_by_filename(file);

	if (doc == NULL)
		return FALSE;

	return editor_goto_pos(doc->editor, pos, TRUE);
}


void navqueue_go_back(void)
{
    back_toggle.forward = FALSE;
	filepos *fprev;
	remember_current();

	/* return if there's no place to go back to */
	if (g_queue_is_empty(navigation_queue) ||
		nav_queue_pos >= g_queue_get_length(navigation_queue) - 1)
		return;

	/* jump back */
	fprev = g_queue_peek_nth(navigation_queue, nav_queue_pos + 1);
	if (goto_file_pos(fprev->file, fprev->pos))
	{
		nav_queue_pos++;
		navigation_serial++;
	}
	else
	{
		/** TODO: add option to re open the file */
		filepos_free(g_queue_pop_nth(navigation_queue, nav_queue_pos + 1));
		navigation_serial++;
	}
	adjust_buttons();
}


void navqueue_go_forward(void)
{
    back_toggle.forward = FALSE;
	filepos *fnext;

	if (nav_queue_pos < 1 ||
		nav_queue_pos >= g_queue_get_length(navigation_queue))
		return;

	remember_current();

	/* jump forward */
	fnext = g_queue_peek_nth(navigation_queue, nav_queue_pos - 1);
	if (goto_file_pos(fnext->file, fnext->pos))
	{
		nav_queue_pos--;
		navigation_serial++;
	}
	else
	{
		/** TODO: add option to re open the file */
		filepos_free(g_queue_pop_nth(navigation_queue, nav_queue_pos - 1));
		nav_queue_pos--;
		navigation_serial++;
	}

	adjust_buttons();
}


/* A toggle pair belongs to the exact history revision and current caret.
 * Manual navigation, edits, queue pruning and a moved caret start a new pair. */
void navqueue_go_back_toggle(void)
{
    GeanyDocument *doc = document_get_current();
    if (!navigation_queue || !doc || !doc->file_name) return;
    gboolean forward = back_toggle.forward && back_toggle.serial == navigation_serial &&
        back_toggle.document == doc->id && back_toggle.position == sci_get_current_position(doc->editor->sci);
    guint previous_document = doc->id;gint previous_position = sci_get_current_position(doc->editor->sci);
    if (forward) navqueue_go_forward();else navqueue_go_back();
    doc = document_get_current();
    if (doc && (doc->id != previous_document || sci_get_current_position(doc->editor->sci) != previous_position))
    {
        back_toggle.forward = !forward;back_toggle.serial = navigation_serial;
        back_toggle.document = doc->id;back_toggle.position = sci_get_current_position(doc->editor->sci);
    }
    else back_toggle.forward = FALSE;
}

static gint find_by_filename(gconstpointer a, gconstpointer b)
{
	if (utils_str_equal(((const filepos*)a)->file, (const gchar*) b))
		return 0;
	else
		return 1;
}


/* Remove all elements with the given filename */
void navqueue_remove_file(const gchar *filename)
{
	GList *match;

	if (filename == NULL)
		return;

	while ((match = g_queue_find_custom(navigation_queue, filename, find_by_filename)))
	{
		gint index = g_queue_link_index(navigation_queue, match);
		if ((guint)index < nav_queue_pos) nav_queue_pos--;
		filepos_free(match->data);
		navigation_serial++;
		g_queue_delete_link(navigation_queue, match);
	}

	nav_queue_pos = g_queue_is_empty(navigation_queue) ? 0 : MIN(nav_queue_pos,g_queue_get_length(navigation_queue)-1);
	adjust_buttons();
}

/* Keep retained positions anchored to edits in their open document. Deleting
 * a position collapses it to the deletion start; Undo does not resurrect it. */
void navqueue_notify(GeanyDocument *doc, SCNotification *nt)
{
 if(navigation_queue && nt->nmhdr.code==SCN_UPDATEUI && doc==document_get_current()) adjust_buttons();
 if(!navigation_queue || !doc->file_name || nt->nmhdr.code!=SCN_MODIFIED ||
    !(nt->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) return;
 for(GList *link=navigation_queue->head;link;link=link->next) {
  filepos *p=link->data;
  if(!utils_str_equal(p->file,doc->file_name)) continue;
  if(nt->modificationType&SC_MOD_INSERTTEXT) { if(p->pos>=nt->position) p->pos+=nt->length; }
  else if(p->pos>nt->position) p->pos=MAX(nt->position,p->pos-nt->length);
 }
 navigation_serial++;
}
enum { HISTORY_CURRENT,HISTORY_FILE,HISTORY_LINE,HISTORY_COLUMN,HISTORY_TEXT,HISTORY_PATH,HISTORY_INDEX,HISTORY_DOC,HISTORY_POS,HISTORY_COLUMNS };
static void history_append(GtkListStore *store,GeanyDocument *doc,gint pos,gint index,gboolean current)
{
 ScintillaObject *sci=doc->editor->sci;
 pos=CLAMP(pos,0,sci_get_length(sci));
 gint line=sci_get_line_from_position(sci,pos),column=scintilla_send_message(sci,SCI_GETCOLUMN,pos,0);
 gint start=sci_get_position_from_line(sci,line),end=MIN(sci_get_line_end_position(sci,line),start+240);
 gchar *raw=sci_get_contents_range(sci,start,end),*excerpt=g_utf8_make_valid(raw,-1),*base=g_path_get_basename(doc->file_name);
 GtkTreeIter iter;gtk_list_store_append(store,&iter);
 gtk_list_store_set(store,&iter,HISTORY_CURRENT,current ? "●" : "",HISTORY_FILE,base,HISTORY_LINE,line+1,HISTORY_COLUMN,column+1,HISTORY_TEXT,excerpt,HISTORY_PATH,doc->file_name,HISTORY_INDEX,index,HISTORY_DOC,doc->id,HISTORY_POS,pos,-1);
 g_free(base);g_free(excerpt);g_free(raw);
}
static guint64 history_fill(GtkListStore *store,GtkTreeView *view)
{
 gtk_list_store_clear(store);
 GeanyDocument *current=document_get_current();
 if(current && current->file_name && !queue_pos_matches(nav_queue_pos,current->file_name,sci_get_current_position(current->editor->sci)))
  history_append(store,current,sci_get_current_position(current->editor->sci),-1,TRUE);
 if(navigation_queue) for(guint i=0;i<g_queue_get_length(navigation_queue);i++) {
  filepos *p=g_queue_peek_nth(navigation_queue,i);GeanyDocument *doc=document_find_by_filename(p->file);
  if(doc) history_append(store,doc,p->pos,i,i==nav_queue_pos && current==doc && p->pos==sci_get_current_position(doc->editor->sci));
 }
 GtkTreeIter first;if(gtk_tree_model_get_iter_first(GTK_TREE_MODEL(store),&first)) gtk_tree_selection_select_iter(gtk_tree_view_get_selection(view),&first);
 return navigation_serial;
}
static void history_activated(GtkTreeView *view,GtkTreePath *path,GtkTreeViewColumn *column,gpointer data)
{ (void)view;(void)path;(void)column;gtk_dialog_response(GTK_DIALOG(data),GTK_RESPONSE_ACCEPT); }
void navqueue_show_history(void)
{
 if(!navigation_queue) return;
 GtkWidget *dialog=gtk_dialog_new_with_buttons(insight_tr("Selection History"),GTK_WINDOW(main_widgets.window),GTK_DIALOG_MODAL|GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Close"),GTK_RESPONSE_CLOSE,insight_tr("Refresh"),GTK_RESPONSE_APPLY,insight_tr("Go To"),GTK_RESPONSE_ACCEPT,NULL);
 gtk_widget_set_name(dialog,"insight-selection-history");gtk_window_set_default_size(GTK_WINDOW(dialog),850,380);g_object_ref_sink(dialog);
 GtkListStore *store=gtk_list_store_new(HISTORY_COLUMNS,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT,G_TYPE_INT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_INT,G_TYPE_UINT,G_TYPE_INT);
 GtkWidget *view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store)),*scroll=gtk_scrolled_window_new(NULL,NULL);
 gtk_widget_set_name(view,"insight-selection-history-list");gtk_tree_view_set_tooltip_column(GTK_TREE_VIEW(view),HISTORY_PATH);
 const gchar *labels[]={"Current","File","Line","Column","Source line"};
 for(guint i=0;i<G_N_ELEMENTS(labels);i++) {
  GtkCellRenderer *cell=gtk_cell_renderer_text_new();if(i==HISTORY_TEXT) g_object_set(cell,"ellipsize",PANGO_ELLIPSIZE_END,NULL);
  GtkTreeViewColumn *column=gtk_tree_view_column_new_with_attributes(insight_tr(labels[i]),cell,"text",i,NULL);gtk_tree_view_column_set_resizable(column,TRUE);if(i==HISTORY_FILE) gtk_tree_view_column_set_min_width(column,150);gtk_tree_view_column_set_expand(column,i==HISTORY_TEXT);gtk_tree_view_append_column(GTK_TREE_VIEW(view),column);
 }
 GtkWidget *hint=gtk_label_new(insight_tr("Recent navigation positions in open files. Double-click or Go To to restore a position."));gtk_label_set_line_wrap(GTK_LABEL(hint),TRUE);
 gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),hint,FALSE,FALSE,6);
 gtk_container_add(GTK_CONTAINER(scroll),view);gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(dialog))),scroll,TRUE,TRUE,6);
 g_signal_connect(view,"row-activated",G_CALLBACK(history_activated),dialog);
 guint64 serial=history_fill(store,GTK_TREE_VIEW(view));gtk_dialog_set_default_response(GTK_DIALOG(dialog),GTK_RESPONSE_ACCEPT);gtk_widget_show_all(dialog);
 for(;;) {
  gint response=gtk_dialog_run(GTK_DIALOG(dialog));
  if(response==GTK_RESPONSE_APPLY) { serial=history_fill(store,GTK_TREE_VIEW(view));continue; }
  if(response!=GTK_RESPONSE_ACCEPT) break;
  if(!navigation_queue || serial!=navigation_serial) { ui_set_statusbar(TRUE,"Navigation history changed; refreshed the list");if(!navigation_queue) break;serial=history_fill(store,GTK_TREE_VIEW(view));continue; }
  GtkTreeIter iter;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),NULL,&iter)) continue;
  guint id;gint pos,index;gtk_tree_model_get(GTK_TREE_MODEL(store),&iter,HISTORY_DOC,&id,HISTORY_POS,&pos,HISTORY_INDEX,&index,-1);
  GeanyDocument *doc=document_find_by_id(id);if(!doc) { serial=history_fill(store,GTK_TREE_VIEW(view));continue; }
  if(index>=0) nav_queue_pos=index;
  navigation_serial++;back_toggle.forward=FALSE;
  editor_goto_pos(doc->editor,pos,TRUE);adjust_buttons();break;
 }
 gtk_widget_destroy(dialog);g_object_unref(store);g_object_unref(dialog);
}
