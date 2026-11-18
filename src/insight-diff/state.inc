/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared comparison state and callback ownership.
 * RU: Общее состояние сравнения и владение обработчиками. */

void insight_diff_set_colorizer(InsightDiffColorize callback) { colorizer=callback;insight_stream_ui_set_colorizer(callback); }
static InsightDiffOpen source_opener;
void insight_diff_set_source_opener(InsightDiffOpen opener) { source_opener=opener; }
typedef struct { gchar *text; gint *positions;guchar *resolutions; } Snapshot;
typedef struct { guint begin,end; } MapBlock;
typedef struct {
 gboolean browse, file_layout;
 GtkWidget *mode, *outer_pane, *result_panel;
 GtkTreeViewColumn *symbol_column, *merge_column;
 GPtrArray *symbols[2];
 GArray *symbol_history, *symbol_future;
 gboolean memory,review_ready,review_locked;GtkWidget *apply,*review_tools;InsightDiffApply apply_cb;InsightDiffAsk ask_cb;gpointer review_data;GDestroyNotify review_destroy;
 GtkWidget *toolbar,*answer_page,*answer_left,*answer_right;GPtrArray *answers;gchar *answer_base,*syntax_path;guint answer_l,answer_r,syntax_idle;gboolean answer_sync;GtkTextBuffer *syntax_buffers[3];
 InsightHistoryUI *history_ui;GtkWidget *headers[3],*destination,*destination_kind;GtkTreeViewColumn *file_columns[2];guchar *resolutions;
 GtkWidget *window,*paths[2],*views[3],*status,*undo,*redo,*take[2],*take_all[2],*external;
 GtkWidget *find_entry,*find_side,*find_case,*find_word,*find_wrap,*find_previous,*find_next,*find_status,*numbers,*find_regex,*find_dotall;
 GtkWidget *replace_entry,*replace_scope,*replace_once,*replace_case,*replace_status,*replace_apply,*replace_confirm;
 GtkWidget *edit_tab_width;
 GtkWidget *visible_space,*visible_eol;
 GtkWidget *line_side,*line_number,*line_select,*line_status;
 GArray *map_blocks[2];guint map_lines[2];
 GtkWidget *scrollers[3],*maps[2],*zoom,*mono,*vertical,*overview,*list_toggle,*wrap_toggle;GtkCssProvider *font_css;
 GtkWidget *compare_button,*cancel_button,*save_button; GCancellable *cancel; gboolean busy;
 GtkWidget *previous,*next,*list_scroll,*sync,*ignore_space,*ignore_eol; GtkAdjustment *adjustments[2]; gboolean scrolling;GtkAdjustment *horizontal[2];GPtrArray *alignment[2];guint alignment_idle;gint source_width[2];gboolean aligned;
 GtkListStore *list; GtkWidget *hunk_view; GtkTextBuffer *output; GtkTreeSelection *selection;
 gchar *sources[2],*loaded_paths[2],*picked[2],*saved;
 InsightDiff diff; GtkTextMark **marks;
 gboolean search_fields[4];
 gboolean zero_found;gint zero_side,zero_offset;guint64 zero_serial;
 GPtrArray *history; guint position;guint64 history_serial; gboolean changing,user_action,pending;
} Merge;
typedef struct { gchar *name,*code,*working,*explanation; } AnswerChoice;

static void file_compare_layout(Merge *m);

static InsightDiffCollectSymbols symbol_collector;
/* EN: Register callback for private snapshot definitions; NULL disables semantic context collection.
 * RU: Регистрирует callback определений отдельных снимков; NULL отключает сбор контекста символов. */
void insight_diff_set_symbol_collector(InsightDiffCollectSymbols callback)
{
    symbol_collector = callback;
}
