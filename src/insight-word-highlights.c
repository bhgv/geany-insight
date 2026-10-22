/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "geany.h"
#include "insight-word-highlights.h"
#include "document.h"
#include "geanyobject.h"
#include "main.h"
#include "sciwrappers.h"
#include "ui_utils.h"
#include "keybindings.h"
#include "navqueue.h"
#include "insight-i18n.h"
#include "insight-help.h"
#include "../insight/desktop/context_search.h"
#include <glib/gstdio.h>
#include <gio/gio.h>
#include <string.h>
#define SEND(s,m,w,l) scintilla_send_message(s,m,w,l)
/* Reserved Insight indicator; keep 27–31 for template/match/link layers. */
#define INDICATOR 26
#define WORDS_MAX 16
#define TEXT_MAX (2u*1024u*1024u)
#define HITS_MAX 20000
#define STATE "insight-word-highlights-state"
typedef struct { gint begin,end,value; } Hit;
typedef struct { guint64 generation,applied_generation,revision;GArray *hits; } State;
typedef struct { guint document;guint64 generation,revision;gchar *text;gchar **words;gboolean sensitive,limited;GArray *hits; } Job;
typedef struct { guint document;guint64 generation,revision;guint position;GArray *hits;gboolean limited; } Apply;
static GPtrArray *words;static guint64 revision=1;static guint timer,applying,busy_document;static gboolean alive,busy,sensitive=TRUE;static GCancellable *cancel;
static GtkWidget *manager,*view,*case_button;static GtkListStore *store;static gint color=0x00ffff;static gulong signals[2];
static void schedule(guint delay);
static void manager_refresh(void);
static void state_free(gpointer data) { State *s=data;g_clear_pointer(&s->hits,g_array_unref);g_free(s); }
static State *state(ScintillaObject *sci) { State *s=g_object_get_data(G_OBJECT(sci),STATE);if(!s) { s=g_new0(State,1);g_object_set_data_full(G_OBJECT(sci),STATE,s,state_free); }return s; }
static gboolean ordinary(GeanyDocument *doc) { if(!DOC_VALID(doc)) return FALSE;ScintillaObject *sci=doc->editor->sci;return SEND(sci,SCI_GETSELECTIONS,0,0)==1 && sci_get_selection_mode(sci)==SC_SEL_STREAM && !SEND(sci,SCI_GETSELECTIONNCARETVIRTUALSPACE,0,0) && !SEND(sci,SCI_GETSELECTIONNANCHORVIRTUALSPACE,0,0); }
static void clear_marks(ScintillaObject *sci) { SEND(sci,SCI_SETINDICATORCURRENT,INDICATOR,0);SEND(sci,SCI_INDICATORCLEARRANGE,0,sci_get_length(sci)); }
static void styles(ScintillaObject *sci) { SEND(sci,SCI_INDICSETSTYLE,INDICATOR,INDIC_STRAIGHTBOX);SEND(sci,SCI_INDICSETFORE,INDICATOR,color);SEND(sci,SCI_INDICSETALPHA,INDICATOR,100);SEND(sci,SCI_INDICSETOUTLINEALPHA,INDICATOR,0);SEND(sci,SCI_INDICSETUNDER,INDICATOR,TRUE); }
static gboolean current(guint document,guint64 generation,guint64 rev,GeanyDocument **result) { GeanyDocument *doc=document_find_by_id(document);if(!alive || !DOC_VALID(doc) || rev!=revision || state(doc->editor->sci)->generation!=generation) return FALSE;if(result) *result=doc;return TRUE; }
static void job_free(gpointer data) { Job *j=data;g_free(j->text);g_strfreev(j->words);g_array_unref(j->hits);g_free(j); }
static gint compare_hit(gconstpointer a,gconstpointer b) { const Hit *x=a,*y=b;return x->begin<y->begin ? -1 : x->begin>y->begin ? 1 : x->end<y->end ? -1 : x->end>y->end; }
static void collect(GTask *task,gpointer source,gpointer data,GCancellable *stop) {
 (void)source;Job *j=data;InsightSearchMatch *matches=g_new(InsightSearchMatch,HITS_MAX);
 for(guint i=0;j->words[i] && !g_cancellable_is_cancelled(stop);i++) {
  gsize total=0,room=HITS_MAX-j->hits->len;
  if(!room) { j->limited=TRUE;break; }
  if(!insight_context_collect(j->text,strlen(j->text),j->words[i],INSIGHT_SEARCH_WORD|(j->sensitive ? INSIGHT_SEARCH_CASE : 0),matches,room,&total,NULL)) continue;
  for(gsize n=0;n<MIN(total,room);n++) { Hit h={(gint)matches[n].start,(gint)matches[n].end,(gint)i+1};g_array_append_val(j->hits,h); }
  if(total>room) j->limited=TRUE;
 }
 g_free(matches);g_array_sort(j->hits,compare_hit);g_task_return_boolean(task,!g_cancellable_is_cancelled(stop));
}
static void apply_free(gpointer data) { Apply *a=data;g_array_unref(a->hits);g_free(a); }
static gboolean apply_step(gpointer data) {
 Apply *a=data;GeanyDocument *doc=NULL;
 if(!current(a->document,a->generation,a->revision,&doc)) { applying=0;busy=FALSE;schedule(200);return G_SOURCE_REMOVE; }
 ScintillaObject *sci=doc->editor->sci;gint64 deadline=g_get_monotonic_time()+3000;SEND(sci,SCI_SETINDICATORCURRENT,INDICATOR,0);
 while(a->position<a->hits->len) { Hit *h=&g_array_index(a->hits,Hit,a->position++);SEND(sci,SCI_SETINDICATORVALUE,h->value,0);SEND(sci,SCI_INDICATORFILLRANGE,h->begin,h->end-h->begin);if(g_get_monotonic_time()>=deadline) return G_SOURCE_CONTINUE; }
 State *s=state(sci);g_clear_pointer(&s->hits,g_array_unref);s->hits=g_array_ref(a->hits);s->revision=a->revision;s->applied_generation=a->generation;
 if(a->limited && doc==document_get_current()) ui_set_statusbar(FALSE,"%s",insight_tr("Word highlight limit reached: first 20000 matches shown"));
 applying=0;busy=FALSE;schedule(0);return G_SOURCE_REMOVE;
}
static void collected(GObject *object,GAsyncResult *result,gpointer data) {
 (void)object;(void)data;GTask *task=G_TASK(result);Job *j=g_task_get_task_data(task);gboolean ok=g_task_propagate_boolean(task,NULL);g_clear_object(&cancel);GeanyDocument *doc=NULL;
 if(!ok || !current(j->document,j->generation,j->revision,&doc)) { busy=FALSE;schedule(200);return; }
 styles(doc->editor->sci);clear_marks(doc->editor->sci);
 Apply *a=g_new0(Apply,1);a->document=j->document;a->generation=j->generation;a->revision=j->revision;a->hits=g_array_ref(j->hits);a->limited=j->limited;
 applying=g_idle_add_full(G_PRIORITY_DEFAULT_IDLE,apply_step,a,apply_free);
}
static gboolean pump(gpointer data) {
 (void)data;timer=0;if(!alive || busy) return G_SOURCE_REMOVE;
 for(guint n=0;n<documents_array->len;n++) {
  GeanyDocument *doc=g_ptr_array_index(documents_array,n);if(!DOC_VALID(doc)) continue;State *s=state(doc->editor->sci);
  if(s->revision==revision && s->applied_generation==s->generation) continue;
  gint length=sci_get_length(doc->editor->sci);
  if(!words->len || length<0 || (guint)length>TEXT_MAX) { clear_marks(doc->editor->sci);g_clear_pointer(&s->hits,g_array_unref);s->revision=revision;s->applied_generation=s->generation;if(words->len && doc==document_get_current()) ui_set_statusbar(FALSE,"%s",insight_tr("Word highlights support source buffers up to 2 MiB"));continue; }
  gchar *text=sci_get_contents(doc->editor->sci,length+1);
  if(!g_utf8_validate(text,length,NULL) || memchr(text,0,length)) { g_free(text);clear_marks(doc->editor->sci);g_clear_pointer(&s->hits,g_array_unref);s->revision=revision;s->applied_generation=s->generation;continue; }
  Job *j=g_new0(Job,1);j->document=doc->id;j->generation=s->generation;j->revision=revision;j->text=text;j->sensitive=sensitive;j->hits=g_array_new(FALSE,FALSE,sizeof(Hit));j->words=g_new0(gchar *,words->len+1);for(guint k=0;k<words->len;k++) j->words[k]=g_strdup(g_ptr_array_index(words,k));
  busy=TRUE;busy_document=doc->id;cancel=g_cancellable_new();GTask *task=g_task_new(NULL,cancel,collected,NULL);g_task_set_task_data(task,j,job_free);g_task_run_in_thread(task,collect);g_object_unref(task);break;
 }
 return G_SOURCE_REMOVE;
}
static void schedule(guint delay) { if(!alive || timer) return;timer=delay ? g_timeout_add(delay,pump,NULL) : g_idle_add(pump,NULL); }
void insight_word_highlights_notify(GeanyEditor *editor,SCNotification *nt) { if(!alive || nt->nmhdr.code!=SCN_MODIFIED || !(nt->modificationType&(SC_MOD_INSERTTEXT|SC_MOD_DELETETEXT))) return;state(editor->sci)->generation++;if(cancel && busy_document==editor->document->id) g_cancellable_cancel(cancel);schedule(200); }
static void opened(GObject *object,GeanyDocument *doc,gpointer data) { (void)object;(void)data;state(doc->editor->sci)->revision=0;schedule(0); }
static void changed(void) { revision++;if(cancel) g_cancellable_cancel(cancel);manager_refresh();schedule(0); }
static gchar *word_at_caret(void) {
 GeanyDocument *doc=document_get_current();if(!ordinary(doc)) return NULL;ScintillaObject *sci=doc->editor->sci;gint a=SEND(sci,SCI_GETSELECTIONSTART,0,0),b=SEND(sci,SCI_GETSELECTIONEND,0,0);
 if(a==b) { a=SEND(sci,SCI_WORDSTARTPOSITION,a,TRUE);b=SEND(sci,SCI_WORDENDPOSITION,b,TRUE); }
 if(b<=a || b-a>256) return NULL;gchar *text=sci_get_contents_range(sci,a,b);if(!text || !g_utf8_validate(text,-1,NULL)) { g_free(text);return NULL; }
 gboolean first=TRUE;for(const gchar *p=text;*p;p=g_utf8_next_char(p)) { gunichar c=g_utf8_get_char(p);if(!(g_unichar_isalnum(c) || c=='_' || (!first && g_unichar_ismark(c)))) { g_free(text);return NULL; }first=FALSE; }return text;
}
static void navigate(gboolean backwards) {
 GeanyDocument *doc=document_get_current();if(!ordinary(doc)) return;State *s=state(doc->editor->sci);
 if(s->revision!=revision || s->applied_generation!=s->generation) { ui_set_statusbar(FALSE,"%s",insight_tr("Word highlights are updating"));return; }
 if(!s->hits || !s->hits->len) { ui_set_statusbar(FALSE,"%s",insight_tr("No highlighted words in this file"));return; }
 gint pos=SEND(doc->editor->sci,backwards ? SCI_GETSELECTIONSTART : SCI_GETSELECTIONEND,0,0);guint chosen=backwards ? s->hits->len-1 : 0;
 if(backwards) { for(guint n=s->hits->len;n>0;n--) if(g_array_index(s->hits,Hit,n-1).end<=pos) { chosen=n-1;break; } }
 else for(guint n=0;n<s->hits->len;n++) if(g_array_index(s->hits,Hit,n).begin>=pos) { chosen=n;break; }
 Hit *h=&g_array_index(s->hits,Hit,chosen);navqueue_goto_position(doc,doc,h->begin);SEND(doc->editor->sci,SCI_SETSEL,backwards ? h->end : h->begin,backwards ? h->begin : h->end);SEND(doc->editor->sci,SCI_SCROLLCARET,0,0);
}
static gchar *preferences_path(void) { return g_build_filename(app->configdir,"insight","word-highlights.conf",NULL); }
static void save_preferences(void) { gchar *path=preferences_path(),*directory=g_path_get_dirname(path);GKeyFile *key=g_key_file_new();g_key_file_set_boolean(key,"highlights","case-sensitive",sensitive);g_key_file_set_integer(key,"highlights","color",color);gsize length;gchar *text=g_key_file_to_data(key,&length,NULL);if(g_mkdir_with_parents(directory,0700) || !g_file_set_contents(path,text,length,NULL)) ui_set_statusbar(TRUE,"%s",insight_tr("Cannot save word highlight settings"));g_free(text);g_key_file_unref(key);g_free(directory);g_free(path); }
static void case_changed(GtkToggleButton *button,gpointer data) { (void)data;sensitive=gtk_toggle_button_get_active(button);save_preferences();changed(); }
static void color_changed(GtkColorButton *button,gpointer data) { (void)data;GdkRGBA value;gtk_color_chooser_get_rgba(GTK_COLOR_CHOOSER(button),&value);color=(gint)(value.red*255+0.5)|((gint)(value.green*255+0.5)<<8)|((gint)(value.blue*255+0.5)<<16);save_preferences();for(guint i=0;i<documents_array->len;i++) { GeanyDocument *doc=g_ptr_array_index(documents_array,i);if(DOC_VALID(doc)) styles(doc->editor->sci); } }
static void manager_refresh(void) { if(!manager) return;gtk_list_store_clear(store);for(guint i=0;i<words->len;i++) { GtkTreeIter iter;gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,(gchar *)g_ptr_array_index(words,i),-1); } }
static void remove_word(GtkButton *button,gpointer data) { (void)button;(void)data;GtkTreeModel *model=NULL;GtkTreeIter iter;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(view)),&model,&iter)) return;GtkTreePath *path=gtk_tree_model_get_path(model,&iter);gint *indices=gtk_tree_path_get_indices(path);if(indices && (guint)indices[0]<words->len) { g_ptr_array_remove_index(words,indices[0]);changed(); }gtk_tree_path_free(path); }
static void clear_words(GtkButton *button,gpointer data) { (void)button;(void)data;if(!words->len) return;g_ptr_array_set_size(words,0);changed(); }
static void manager_destroyed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;manager=NULL;view=case_button=NULL;g_clear_object(&store); }
static void show_manager(void) {
 if(manager) { gtk_window_present(GTK_WINDOW(manager));return; }
 manager=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_widget_set_name(manager,"insight-word-highlights-window");gtk_window_set_title(GTK_WINDOW(manager),insight_tr("Highlighted Words"));gtk_window_set_transient_for(GTK_WINDOW(manager),GTK_WINDOW(main_widgets.window));gtk_window_set_default_size(GTK_WINDOW(manager),440,360);g_signal_connect(manager,"destroy",G_CALLBACK(manager_destroyed),NULL);
 GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_container_add(GTK_CONTAINER(manager),box);GtkWidget *label=gtk_label_new(insight_tr("Up to 16 words, shared by all open source buffers for this session"));gtk_label_set_line_wrap(GTK_LABEL(label),TRUE);gtk_box_pack_start(GTK_BOX(box),label,FALSE,FALSE,0);
 store=gtk_list_store_new(1,G_TYPE_STRING);view=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));gtk_widget_set_name(view,"insight-word-highlights-list");gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(view),-1,insight_tr("Word"),gtk_cell_renderer_text_new(),"text",0,NULL);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),view);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 GtkWidget *controls=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,5);case_button=gtk_check_button_new_with_label(insight_tr("Case sensitive"));gtk_widget_set_name(case_button,"insight-word-highlights-case");gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(case_button),sensitive);g_signal_connect(case_button,"toggled",G_CALLBACK(case_changed),NULL);gtk_box_pack_start(GTK_BOX(controls),case_button,FALSE,FALSE,0);
 GdkRGBA rgba={(color&255)/255.0,((color>>8)&255)/255.0,((color>>16)&255)/255.0,1};GtkWidget *pick=gtk_color_button_new_with_rgba(&rgba);gtk_widget_set_name(pick,"insight-word-highlights-color");g_signal_connect(pick,"color-set",G_CALLBACK(color_changed),NULL);gtk_box_pack_start(GTK_BOX(controls),pick,FALSE,FALSE,0);
 GtkWidget *remove=gtk_button_new_with_label(insight_tr("Remove selected")),*clear=gtk_button_new_with_label(insight_tr("Clear all"));gtk_widget_set_name(remove,"insight-word-highlights-remove");gtk_widget_set_name(clear,"insight-word-highlights-clear");g_signal_connect(remove,"clicked",G_CALLBACK(remove_word),NULL);g_signal_connect(clear,"clicked",G_CALLBACK(clear_words),NULL);gtk_box_pack_start(GTK_BOX(controls),remove,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(controls),clear,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),controls,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),insight_panel_help_button("word-highlights.md"),FALSE,FALSE,0);manager_refresh();gtk_widget_show_all(manager);gtk_window_present(GTK_WINDOW(manager));
}
void insight_word_highlights_command(guint action) {
 if(!alive) return;
 if(action==1) { show_manager();return; }if(action==2) { clear_words(NULL,NULL);return; }if(action==3 || action==4) { navigate(action==4);return; }if(action) return;
 gchar *word=word_at_caret();if(!word) { ui_set_statusbar(FALSE,"%s",insight_tr("Place the caret in a word or select one word up to 256 bytes"));return; }
 for(guint n=0;n<words->len;n++) if(!strcmp(word,g_ptr_array_index(words,n))) { g_ptr_array_remove_index(words,n);g_free(word);changed();return; }
 if(words->len==WORDS_MAX) { ui_set_statusbar(TRUE,"%s",insight_tr("Highlight list is full; remove a word first"));g_free(word);return; }
 g_ptr_array_add(words,word);changed();
}
static void command(GtkMenuItem *item,gpointer data) { (void)item;insight_word_highlights_command(GPOINTER_TO_UINT(data)); }
static void menu_show(GtkWidget *menu,gpointer data) { (void)data;GList *children=gtk_container_get_children(GTK_CONTAINER(menu));for(GList *p=children;p;p=p->next) { gpointer action=g_object_get_data(G_OBJECT(p->data),"insight-word-highlight-action");if(action) { guint n=GPOINTER_TO_UINT(action)-1;gtk_widget_set_sensitive(p->data,n==1 || (n==2 ? words->len>0 : ordinary(document_get_current()) && (n==0 || words->len>0))); } }g_list_free(children); }
void insight_word_highlights_init(void) {
 if(alive) return;alive=TRUE;words=g_ptr_array_new_with_free_func(g_free);gchar *path=preferences_path();GKeyFile *key=g_key_file_new();if(g_key_file_load_from_file(key,path,G_KEY_FILE_NONE,NULL)) { if(g_key_file_has_key(key,"highlights","case-sensitive",NULL)) sensitive=g_key_file_get_boolean(key,"highlights","case-sensitive",NULL);gint value=g_key_file_get_integer(key,"highlights","color",NULL);if(g_key_file_has_key(key,"highlights","color",NULL) && value>=0 && value<=0xffffff) color=value; }g_key_file_unref(key);g_free(path);
 GtkWidget *parent=gtk_menu_item_new_with_label(insight_tr("Word highlights")),*menu=gtk_menu_new();gtk_widget_set_name(parent,"insight-word-highlights-menu");gtk_menu_item_set_submenu(GTK_MENU_ITEM(parent),menu);gtk_menu_shell_append(GTK_MENU_SHELL(ui_lookup_widget(main_widgets.window,"search1_menu")),parent);
 const gchar *labels[]={"Highlight Word","Highlighted Words…","Clear Word Highlights","Next Highlighted Word","Previous Highlighted Word"},*names[]={"toggle","manage","clear","next","previous"};const guint keys[]={GEANY_KEYS_INSIGHT_HIGHLIGHTWORD,GEANY_KEYS_INSIGHT_HIGHLIGHTWORDS,GEANY_KEYS_INSIGHT_CLEARHIGHLIGHTS,GEANY_KEYS_INSIGHT_NEXTHIGHLIGHT,GEANY_KEYS_INSIGHT_PREVIOUSHIGHLIGHT};
 for(guint n=0;n<5;n++) { GtkWidget *item=gtk_menu_item_new_with_label(insight_tr(labels[n]));gchar *name=g_strconcat("insight-word-highlights-",names[n],NULL);gtk_widget_set_name(item,name);g_free(name);g_object_set_data(G_OBJECT(item),"insight-word-highlight-action",GUINT_TO_POINTER(n+1));keybindings_attach_menu(GEANY_KEY_GROUP_SEARCH,keys[n],item);g_signal_connect(item,"activate",G_CALLBACK(command),GUINT_TO_POINTER(n));gtk_menu_shell_append(GTK_MENU_SHELL(menu),item); }
 for(guint n=0;n<3;n++) { GtkWidget *item=gtk_menu_item_new_with_label(insight_tr(labels[n]));gchar *name=g_strconcat("insight-word-highlights-popup-",names[n],NULL);gtk_widget_set_name(item,name);g_free(name);g_object_set_data(G_OBJECT(item),"insight-word-highlight-action",GUINT_TO_POINTER(n+1));g_signal_connect(item,"activate",G_CALLBACK(command),GUINT_TO_POINTER(n));gtk_menu_shell_append(GTK_MENU_SHELL(main_widgets.editor_menu),item);gtk_widget_show(item); }
 g_signal_connect(menu,"show",G_CALLBACK(menu_show),NULL);g_signal_connect(main_widgets.editor_menu,"show",G_CALLBACK(menu_show),NULL);gtk_widget_show_all(parent);
 signals[0]=g_signal_connect_after(geany_object,"document-open",G_CALLBACK(opened),NULL);signals[1]=g_signal_connect_after(geany_object,"document-new",G_CALLBACK(opened),NULL);schedule(0);
}
void insight_word_highlights_finalize(void) { if(!alive) return;alive=FALSE;if(timer) { g_source_remove(timer);timer=0; }if(applying) { g_source_remove(applying);applying=0; }if(cancel) { g_cancellable_cancel(cancel);g_clear_object(&cancel); }for(guint n=0;n<2;n++) if(signals[n]) g_signal_handler_disconnect(geany_object,signals[n]);if(manager) gtk_widget_destroy(manager);g_clear_pointer(&words,g_ptr_array_unref); }
