/* SPDX-License-Identifier: GPL-2.0-or-later */
/* EN: Shared AI profiles, connection wizard and parallel requests.
 * RU: Общие профили ИИ, мастер подключения и параллельные запросы. */
#include "geany.h"
#include "insight-ai-profiles.h"
#include "insight-ai-ui.h"
#include "main.h"
#include "insight-i18n.h"
#include "insight-help.h"
#include "insight-diff.h"
#include "ui_utils.h"
#include <glib/gstdio.h>
#include <string.h>
typedef struct { gchar *name,*endpoint,*model,*python,*key,*env;gboolean checked,remember; } Profile;
struct InsightAiProfiles { GtkWidget *combo,*provider,*endpoint,*model,*key,*python; };
static GPtrArray *profiles;static GList *bindings;static guint active;static gboolean syncing;static GtkWidget *manager,*tree,*fields[6],*remember,*checked,*notice;static GtkListStore *store;
static const gchar *endpoints[]={"https://api.openai.com/v1/chat/completions","https://api.x.ai/v1/chat/completions","https://api.deepseek.com/chat/completions","https://YOUR_WORKSPACE.ap-southeast-1.maas.aliyuncs.com/compatible-mode/v1/chat/completions","http://127.0.0.1:8000/v1/chat/completions"};
static const gchar *env_names[]={"OPENAI_API_KEY","XAI_API_KEY","DEEPSEEK_API_KEY","DASHSCOPE_API_KEY",""};
const gchar *insight_ai_profiles_endpoint(guint i) { return endpoints[MIN(i,4)]; }
const gchar *insight_ai_profiles_key_env(guint i) { return env_names[MIN(i,4)]; }
static const gchar *names[]={"name","endpoint","model","python","key","env"};
static void secret_free(gchar *s) { if(s) { volatile gchar *p=(volatile gchar *)s;gsize length=strlen(s);for(gsize i=0;i<length;i++) p[i]=0;g_free(s); } }
static void profile_free(gpointer data) { Profile *p=data;g_free(p->name);g_free(p->endpoint);g_free(p->model);g_free(p->python);secret_free(p->key);g_free(p->env);g_free(p); }
static gchar *path(void) { return g_build_filename(app->configdir,"insight-ai-profiles.conf",NULL); }
static Profile *new_profile(void) { Profile *p=g_new0(Profile,1);p->name=g_strdup("Default");p->endpoint=g_strdup("https://api.openai.com/v1/chat/completions");p->model=g_strdup("");p->env=g_strdup("OPENAI_API_KEY");p->key=g_strdup("");p->checked=TRUE;
#ifdef G_OS_WIN32
 p->python=g_strdup("python");
#else
 p->python=g_strdup("python3");
#endif
 return p; }
static gboolean bounded(const gchar *s,gsize n) { return s && strlen(s)<=n && g_utf8_validate(s,-1,NULL); }
static void init(void) {
 if(profiles) return;profiles=g_ptr_array_new_with_free_func(profile_free);GKeyFile *key=g_key_file_new();gchar *file=path();GStatBuf st;
 if(!g_stat(file,&st) && st.st_size<=512*1024 && g_key_file_load_from_file(key,file,G_KEY_FILE_NONE,NULL)) {
  gint count=g_key_file_get_integer(key,"profiles","count",NULL);for(gint i=0;i<count && i<16;i++) { gchar *group=g_strdup_printf("profile-%d",i);Profile *p=new_profile();gchar **values[]={&p->name,&p->endpoint,&p->model,&p->python,&p->key,&p->env};const gsize limits[]={128,2048,256,2048,8192,128};gboolean valid=TRUE;for(guint j=0;j<6;j++) { gchar *v=g_key_file_get_string(key,group,names[j],NULL);if(v) { if(!bounded(v,limits[j])) valid=FALSE;g_free(*values[j]);*values[j]=v; } }p->checked=g_key_file_get_boolean(key,group,"checked",NULL);p->remember=g_key_file_get_boolean(key,group,"remember-key",NULL);if(!p->remember) { secret_free(p->key);p->key=g_strdup(""); }if(valid && *p->name) g_ptr_array_add(profiles,p);else profile_free(p);g_free(group); }gint chosen=g_key_file_get_integer(key,"profiles","active",NULL);if(chosen>=0 && (guint)chosen<=profiles->len) active=chosen;
 }
 if(!profiles->len) { Profile *p=new_profile();gchar *legacy=g_build_filename(app->configdir,"insight-ai.conf",NULL);if(g_key_file_load_from_file(key,legacy,G_KEY_FILE_NONE,NULL)) { gint provider=CLAMP(g_key_file_get_integer(key,"assistant","provider",NULL),0,4);g_free(p->env);p->env=g_strdup(insight_ai_profiles_key_env(provider));const gchar *old[]={"endpoint","model","python"};gchar **v[]={&p->endpoint,&p->model,&p->python};for(guint i=0;i<3;i++) { gchar *s=g_key_file_get_string(key,"assistant",old[i],NULL);if(s && bounded(s,2048)) { g_free(*v[i]);*v[i]=s; }else g_free(s); } }g_free(legacy);g_ptr_array_add(profiles,p);active=0; }
 g_free(file);g_key_file_unref(key);
}
static void save(void) {
 GKeyFile *key=g_key_file_new();g_key_file_set_integer(key,"profiles","count",profiles->len);g_key_file_set_integer(key,"profiles","active",active);
 for(guint i=0;i<profiles->len;i++) { Profile *p=g_ptr_array_index(profiles,i);const gchar *values[]={p->name,p->endpoint,p->model,p->python,p->remember ? p->key:"",p->env};gchar *group=g_strdup_printf("profile-%u",i);for(guint n=0;n<6;n++) g_key_file_set_string(key,group,names[n],values[n]);g_key_file_set_boolean(key,group,"checked",p->checked);g_key_file_set_boolean(key,group,"remember-key",p->remember);g_free(group); }
 gsize n;gchar *raw=g_key_file_to_data(key,&n,NULL),*file=path();GFile *target=g_file_new_for_path(file);GError *error=NULL;if(!g_file_replace_contents(target,raw,n,NULL,FALSE,G_FILE_CREATE_PRIVATE,NULL,NULL,&error)) { ui_set_statusbar(TRUE,"%s",error->message);g_clear_error(&error); }g_object_unref(target);secret_free(raw);g_free(file);g_key_file_unref(key);
}
static const gchar *profile_key(Profile *p) { const gchar *env=*p->env ? g_getenv(p->env):NULL;return *p->key ? p->key:(env ? env:""); }
static void broadcast(void) {
 syncing=TRUE;for(GList *l=bindings;l;l=l->next) { InsightAiProfiles *b=l->data;gtk_combo_box_text_remove_all(GTK_COMBO_BOX_TEXT(b->combo));for(guint i=0;i<profiles->len;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(b->combo),((Profile *)g_ptr_array_index(profiles,i))->name);gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(b->combo),insight_tr("Compare checked AI profiles"));gtk_combo_box_set_active(GTK_COMBO_BOX(b->combo),active);if(active<profiles->len) { Profile *p=g_ptr_array_index(profiles,active);gtk_combo_box_set_active(GTK_COMBO_BOX(b->provider),4);GtkWidget *fields[]={b->endpoint,b->model,b->key,b->python};const gchar *values[]={p->endpoint,p->model,profile_key(p),p->python};for(guint i=0;i<4;i++) gtk_entry_set_text(GTK_ENTRY(fields[i]),values[i]); } }syncing=FALSE;
}
static void changed(GtkComboBox *combo,gpointer data) { (void)data;if(syncing) return;gint n=gtk_combo_box_get_active(combo);if(n>=0 && (guint)n<=profiles->len) { active=n;save();broadcast(); } }
static void binding_free(gpointer data) { bindings=g_list_remove(bindings,data);g_free(data); }
static GtkWidget *wizard_button(void);

/* EN: Bind the provider, endpoint, model, key and python fields to box.
 * RU: Связывает поля provider, endpoint, model, key и python с контейнером box. */
InsightAiProfiles *insight_ai_profiles_bind(GtkWidget *box,GtkWidget *provider,GtkWidget *endpoint,GtkWidget *model,GtkWidget *key,GtkWidget *python) {
 init();InsightAiProfiles *b=g_new0(InsightAiProfiles,1);b->provider=provider;b->endpoint=endpoint;b->model=model;b->key=key;b->python=python;GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);b->combo=gtk_combo_box_text_new();gtk_widget_set_name(b->combo,"insight-ai-shared-profile");GList *renderers=gtk_cell_layout_get_cells(GTK_CELL_LAYOUT(b->combo));for(GList *c=renderers;c;c=c->next) if(GTK_IS_CELL_RENDERER_TEXT(c->data)) g_object_set(c->data,"ellipsize",PANGO_ELLIPSIZE_END,"width-chars",1,"max-width-chars",30,NULL);g_list_free(renderers);gtk_widget_set_tooltip_text(b->combo,insight_tr("Choose a saved AI connection or compare enabled providers"));gtk_box_pack_start(GTK_BOX(row),b->combo,TRUE,TRUE,0);GtkWidget *button=gtk_button_new_with_label(insight_tr("AI Connections…"));gtk_widget_set_name(button,"insight-ai-connections");g_signal_connect(button,"clicked",G_CALLBACK(insight_ai_profiles_show),NULL);gtk_box_pack_start(GTK_BOX(row),button,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(row),wizard_button(),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0);gtk_box_reorder_child(GTK_BOX(box),row,1);bindings=g_list_append(bindings,b);g_object_set_data_full(G_OBJECT(row),"insight-ai-profile-binding",b,binding_free);g_signal_connect(b->combo,"changed",G_CALLBACK(changed),b);broadcast();return b;
}
gboolean insight_ai_profiles_multiple(InsightAiProfiles *b) { (void)b;init();return active>=profiles->len; }
void insight_ai_profiles_save_current(InsightAiProfiles *b) {
 init();if(!b || active>=profiles->len) return;if(!bounded(gtk_entry_get_text(GTK_ENTRY(b->endpoint)),2048) || !bounded(gtk_entry_get_text(GTK_ENTRY(b->model)),256) || !bounded(gtk_entry_get_text(GTK_ENTRY(b->key)),8192) || !bounded(gtk_entry_get_text(GTK_ENTRY(b->python)),2048)) { ui_set_statusbar(TRUE,"%s",insight_tr("AI connection fields exceed their size limits"));return; }Profile *p=g_ptr_array_index(profiles,active);gint preset=gtk_combo_box_get_active(GTK_COMBO_BOX(b->provider));if(preset>=0 && preset<4) { g_free(p->env);p->env=g_strdup(insight_ai_profiles_key_env(preset)); }g_free(p->endpoint);p->endpoint=g_strdup(gtk_entry_get_text(GTK_ENTRY(b->endpoint)));g_free(p->model);p->model=g_strdup(gtk_entry_get_text(GTK_ENTRY(b->model)));g_free(p->python);p->python=g_strdup(gtk_entry_get_text(GTK_ENTRY(b->python)));secret_free(p->key);p->key=g_strdup(gtk_entry_get_text(GTK_ENTRY(b->key)));save();broadcast();
}
static gint selected(void) { GtkTreeModel *m=NULL;GtkTreeIter iter;if(!gtk_tree_selection_get_selected(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),&m,&iter)) return -1;gint id=-1;gtk_tree_model_get(m,&iter,0,&id,-1);return id; }
static void selected_changed(GtkTreeSelection *selection,gpointer data) { (void)selection;(void)data;gint id=selected();if(id<0 || (guint)id>=profiles->len) return;Profile *p=g_ptr_array_index(profiles,id);const gchar *values[]={p->name,p->endpoint,p->model,p->python,p->key,p->env};for(guint i=0;i<6;i++) gtk_entry_set_text(GTK_ENTRY(fields[i]),values[i]);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(remember),p->remember);gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(checked),p->checked); }
static void refresh(gint id) { gtk_list_store_clear(store);for(guint i=0;i<profiles->len;i++) { Profile *p=g_ptr_array_index(profiles,i);GtkTreeIter iter;gtk_list_store_append(store,&iter);gtk_list_store_set(store,&iter,0,i,1,p->name,2,p->checked ? "✓":"",3,p->model,-1); }if(id>=0 && (guint)id<profiles->len) { GtkTreePath *path=gtk_tree_path_new_from_indices(id,-1);gtk_tree_view_set_cursor(GTK_TREE_VIEW(tree),path,NULL,FALSE);gtk_tree_path_free(path); }broadcast(); }
static void edit_profile(GtkButton *button,gpointer data) {
 (void)button;gint action=GPOINTER_TO_INT(data),id=selected();if(action==2) { if(id<0 || profiles->len<=1) return;g_ptr_array_remove_index(profiles,id);if(active>(guint)id) active--;active=MIN(active,profiles->len-1);id=MIN(id,(gint)profiles->len-1); }
 else { if((!action && profiles->len>=16) || (action==1 && id<0)) return;const gsize limits[]={128,2048,256,2048,8192,128};gboolean valid=TRUE;for(guint i=0;i<6;i++) if(!bounded(gtk_entry_get_text(GTK_ENTRY(fields[i])),limits[i])) valid=FALSE;const gchar *endpoint=gtk_entry_get_text(GTK_ENTRY(fields[1]));if(!*gtk_entry_get_text(GTK_ENTRY(fields[0])) || !*gtk_entry_get_text(GTK_ENTRY(fields[2])) || (g_ascii_strncasecmp(endpoint,"https://",8) && g_ascii_strncasecmp(endpoint,"http://",7))) valid=FALSE;
  if(!valid) { gtk_label_set_text(GTK_LABEL(notice),insight_tr("Enter a name, HTTP(S) API endpoint and model; check field limits."));return; }Profile *p=new_profile();gchar **values[]={&p->name,&p->endpoint,&p->model,&p->python,&p->key,&p->env};for(guint i=0;i<6;i++) { g_free(*values[i]);*values[i]=g_strdup(gtk_entry_get_text(GTK_ENTRY(fields[i]))); }p->remember=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remember));p->checked=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(checked));if(!action) { g_ptr_array_add(profiles,p);id=profiles->len-1; }else { profile_free(profiles->pdata[id]);profiles->pdata[id]=p; }
 }
 gtk_label_set_text(GTK_LABEL(notice),"");save();refresh(id);
}
static void preset_changed(GtkComboBox *combo,gpointer data) { (void)data;gint i=gtk_combo_box_get_active(combo);if(i>=0 && i<=4) { gtk_entry_set_text(GTK_ENTRY(fields[1]),insight_ai_profiles_endpoint(i));gtk_entry_set_text(GTK_ENTRY(fields[5]),insight_ai_profiles_key_env(i));gtk_entry_set_text(GTK_ENTRY(fields[4]),""); } }
static void manager_destroyed(GtkWidget *widget,gpointer data) { (void)widget;(void)data;manager=tree=remember=checked=notice=NULL;memset(fields,0,sizeof fields);g_clear_object(&store); }
enum { TEST_CONNECTION = 101 };

typedef struct
{
 Profile *profile;
 gchar *helper;
} ConnectionProbe;

/* EN: Release probe data, including the copied API key.
 * RU: Освобождает данные проверки data, включая копию ключа API. */
static void probe_free(gpointer data)
{
 ConnectionProbe *probe = data;
 profile_free(probe->profile);
 g_free(probe->helper);
 g_free(probe);
}

/* EN: Run a fixed sample through task using probe data and cancel.
 * The source object is unused. Errors are returned through task.
 * RU: Выполняет тестовый запрос через task с данными data и отменой cancel.
 * object не используется. Ошибки возвращаются через task. */
static void probe_worker(GTask *task, gpointer object, gpointer data, GCancellable *cancel)
{
 (void)object;
 ConnectionProbe *probe = data;
 Profile *p = probe->profile;
 InsightAiRequest request = { p->endpoint, p->model, profile_key(p),
                              "Reply with OK.", "", "", "", "", "" };
 GError *error = NULL;
 InsightAiAnswer *answer = insight_ai_request(p->python, probe->helper, &request, cancel, &error);
 if (answer)
 {
  insight_ai_answer_free(answer);
  g_task_return_boolean(task, TRUE);
 }
 else if (error)
  g_task_return_error(task, error);
 else
  g_task_return_new_error(task, G_IO_ERROR, G_IO_ERROR_FAILED, "AI request failed");
}

/* EN: Update dialog object after a probe if the dialog is still open.
 * result carries success or a transport error. data is unused.
 * RU: Обновляет диалог object после проверки, если он ещё открыт.
 * result содержит успех или ошибку транспорта. data не используется. */
static void probe_done(GObject *object, GAsyncResult *result, gpointer data)
{
 (void)data;
 GtkWidget *status = g_object_get_data(object, "insight-wizard-status");
 GError *error = NULL;
 gboolean ok = g_task_propagate_boolean(G_TASK(result), &error);
 if (!status)
 {
  g_clear_error(&error);
  return;
 }
 const gchar *message = "Test failed. Check Python, endpoint, key, model access and API quota.";
 GtkWidget *grid = g_object_get_data(object, "insight-wizard-grid");
 if (ok)
  message = grid ? "Connection verified. Save to activate this profile." : "Connection verified.";
 else if (error && error->domain == G_SPAWN_ERROR)
  message = "Could not start Python. Select a working Python 3 executable.";
 else if (error && g_str_has_prefix(error->message, "Provider connection failed"))
  message = "Could not reach the provider. Check the URL, network and TLS certificates.";
 else if (error && g_str_has_prefix(error->message, "Provider HTTP status "))
 {
  guint64 code = g_ascii_strtoull(error->message + strlen("Provider HTTP status "), NULL, 10);
  if (code == 401 || code == 403)
   message = "Access denied (401/403). Check the API key and permissions for the model.";
  else if (code == 404)
   message = "API or model not found (404). Check the complete URL and model ID.";
  else if (code == 429)
   message = "Request limited (429). Check API quota and rate limits.";
 }
 gtk_label_set_text(GTK_LABEL(status), insight_tr(message));
 g_clear_error(&error);
 if (!grid)
 {
  GtkWidget *close = gtk_dialog_get_widget_for_response(GTK_DIALOG(object), GTK_RESPONSE_CLOSE);
  gtk_button_set_label(GTK_BUTTON(close), insight_tr("Close"));
  return;
 }
 gtk_widget_set_sensitive(grid, TRUE);
 gtk_dialog_set_response_sensitive(GTK_DIALOG(object), TEST_CONNECTION, TRUE);
 gtk_dialog_set_response_sensitive(GTK_DIALOG(object), GTK_RESPONSE_ACCEPT, TRUE);
}

/* EN: Add wrapped instructions or status text to box and return its label.
 * RU: Добавляет переносимый текст text в box и возвращает подпись. */
static GtkWidget *wizard_label(GtkWidget *box, const gchar *text)
{
 GtkWidget *label = gtk_label_new(text);
 gtk_label_set_xalign(GTK_LABEL(label), 0);
 gtk_label_set_line_wrap(GTK_LABEL(label), TRUE);
 gtk_label_set_max_width_chars(GTK_LABEL(label), 64);
 gtk_box_pack_start(GTK_BOX(box), label, FALSE, FALSE, 0);
 return label;
}

/* EN: Clear a stale result in label after a connection field changes.
 * editable is the changed field and is otherwise unused.
 * RU: Сбрасывает результат в label при изменении поля подключения editable. */
static void wizard_settings_changed(GtkEditable *editable, gpointer label)
{
 (void)editable;
 gtk_label_set_text(GTK_LABEL(label), "");
}

/* EN: Detach the status label when dialog is destroyed. data is unused.
 * RU: Отвязывает подпись при уничтожении dialog. data не используется. */
static void wizard_destroyed(GtkWidget *dialog, gpointer data)
{
 (void)data;
 g_object_set_data(G_OBJECT(dialog), "insight-wizard-status", NULL);
}

/* EN: Open the modal setup wizard under widget. Signal data is unused.
 * Invalid fields stay editable. Test runs only on explicit request.
 * RU: Открывает модальный мастер над widget. Параметр data не используется.
 * Ошибочные поля можно исправить. Проверка запускается по кнопке. */
static void connection_wizard(GtkWidget *widget, gpointer data)
{
 (void)data;
 init();
 GtkWidget *parent = gtk_widget_get_toplevel(widget);
 GtkWidget *dialog = gtk_dialog_new_with_buttons(insight_tr("AI Connection Wizard"),
  GTK_WINDOW(GTK_IS_WINDOW(parent) ? parent : main_widgets.window),
  GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
  insight_tr("Cancel"), GTK_RESPONSE_CANCEL, insight_tr("Next"), GTK_RESPONSE_ACCEPT, NULL);
 g_object_ref_sink(dialog);
 g_signal_connect(dialog, "destroy", G_CALLBACK(wizard_destroyed), NULL);
 GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
 gtk_container_set_border_width(GTK_CONTAINER(box), 12);
 gtk_box_set_spacing(GTK_BOX(box), 8);
 wizard_label(box, insight_tr("1. Choose a provider. Get a key and a model ID from its API dashboard. For a local server, start it and look up its API URL and model ID."));

 const gchar *presets[] = { "OpenAI / ChatGPT API", "xAI / Grok", "DeepSeek",
                            "Qwen / Model Studio", "Custom compatible API" };
 GtkWidget *provider = gtk_combo_box_text_new();
 for (guint i = 0; i < G_N_ELEMENTS(presets); i++)
  gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(provider), insight_tr(presets[i]));
 gtk_combo_box_set_active(GTK_COMBO_BOX(provider), 0);
 gtk_box_pack_start(GTK_BOX(box), provider, FALSE, FALSE, 0);
 gtk_widget_show_all(dialog);
 if (gtk_dialog_run(GTK_DIALOG(dialog)) != GTK_RESPONSE_ACCEPT)
  goto close;

 guint index = gtk_combo_box_get_active(GTK_COMBO_BOX(provider));
 gtk_widget_set_sensitive(provider, FALSE);
 const gchar *guides[] = {
  "https://platform.openai.com/docs/quickstart",
  "https://docs.x.ai/developers/quickstart",
  "https://api-docs.deepseek.com/",
  "https://www.alibabacloud.com/help/en/model-studio/qwen-api-via-openai-chat-completions",
  NULL
 };
 if (guides[index])
  gtk_box_pack_start(GTK_BOX(box), gtk_link_button_new_with_label(guides[index],
   insight_tr("Open provider setup guide")), FALSE, FALSE, 0);
 wizard_label(box, insight_tr("2. Enter the model ID and paste the API key or use the suggested environment variable. For Qwen, replace the sample address with your workspace and region endpoint."));

 Profile *defaults = new_profile();
 gchar *python = g_find_program_in_path(defaults->python);
 const gchar *values[] = { insight_tr(presets[index]), endpoints[index], "",
                           python ? python : defaults->python, "", env_names[index] };
 const gchar *labels[] = { "Profile name", "API endpoint", "Model ID", "Python 3",
                           "API key", "API key environment variable" };
 const gsize limits[] = { 128, 2048, 256, 2048, 8192, 128 };
 GtkWidget *entries[G_N_ELEMENTS(labels)];
 GtkWidget *grid = gtk_grid_new();
 gtk_grid_set_row_spacing(GTK_GRID(grid), 6);
 gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
 gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);
 for (guint i = 0; i < G_N_ELEMENTS(labels); i++)
 {
  GtkWidget *label = gtk_label_new(insight_tr(labels[i]));
  gtk_widget_set_halign(label, GTK_ALIGN_START);
  entries[i] = gtk_entry_new();
  gtk_widget_set_hexpand(entries[i], TRUE);
  gtk_entry_set_max_length(GTK_ENTRY(entries[i]), limits[i]);
  gtk_entry_set_visibility(GTK_ENTRY(entries[i]), i != 4);
  gtk_entry_set_text(GTK_ENTRY(entries[i]), values[i]);
  gtk_label_set_mnemonic_widget(GTK_LABEL(label), entries[i]);
  gtk_grid_attach(GTK_GRID(grid), label, 0, i, 1, 1);
  gtk_grid_attach(GTK_GRID(grid), entries[i], 1, i, 1, 1);
 }
 g_free(python);
 profile_free(defaults);

 GtkWidget *remember_key = gtk_check_button_new_with_label(
  insight_tr("Save API key in private config (not encrypted)"));
 gtk_box_pack_start(GTK_BOX(box), remember_key, FALSE, FALSE, 0);
 wizard_label(box, insight_tr("3. Test the connection, then save and activate. The test sends the key and a short sample prompt to the endpoint. It may consume quota and includes no source, history or attachments."));
 GtkWidget *status = wizard_label(box, "");
 for (guint i = 1; i < G_N_ELEMENTS(entries); i++)
  g_signal_connect_object(entries[i], "changed", G_CALLBACK(wizard_settings_changed), status, 0);
 g_object_set_data(G_OBJECT(dialog), "insight-wizard-status", status);
 gtk_dialog_add_button(GTK_DIALOG(dialog), insight_tr("Test connection"), TEST_CONNECTION);
 GtkWidget *save_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
 gtk_button_set_label(GTK_BUTTON(save_button), insight_tr("Save and activate"));
 gtk_widget_show_all(dialog);
 gtk_widget_grab_focus(entries[2]);

 GCancellable *cancel = g_cancellable_new();
 for (;;)
 {
  gint response = gtk_dialog_run(GTK_DIALOG(dialog));
  if (response != TEST_CONNECTION && response != GTK_RESPONSE_ACCEPT)
   break;

  Profile *p = g_new0(Profile, 1);
  gchar **fields_to_copy[] = { &p->name, &p->endpoint, &p->model, &p->python, &p->key, &p->env };
  gboolean valid = TRUE;
  for (guint i = 0; i < G_N_ELEMENTS(entries); i++)
  {
   const gchar *value = gtk_entry_get_text(GTK_ENTRY(entries[i]));
   valid = valid && bounded(value, limits[i]);
   *fields_to_copy[i] = g_strdup(value);
  }
  valid = valid && *p->name && *p->model && *p->python &&
   (!g_ascii_strncasecmp(p->endpoint, "https://", 8) ||
    !g_ascii_strncasecmp(p->endpoint, "http://", 7)) &&
   !strstr(p->endpoint, "YOUR_WORKSPACE");
  if (!valid)
  {
   gtk_label_set_text(GTK_LABEL(status),
    insight_tr("Enter a profile name, complete HTTP(S) endpoint, model ID and Python executable."));
   profile_free(p);
   continue;
  }

  if (response == TEST_CONNECTION)
  {
   ConnectionProbe *probe = g_new0(ConnectionProbe, 1);
   probe->profile = p;
   probe->helper = g_build_filename(app->datadir, "insight", "tools", "ai_client.py", NULL);
   GTask *task = g_task_new(dialog, cancel, probe_done, NULL);
   g_task_set_task_data(task, probe, probe_free);
   gtk_widget_set_sensitive(grid, FALSE);
   gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), TEST_CONNECTION, FALSE);
   gtk_dialog_set_response_sensitive(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT, FALSE);
   gtk_label_set_text(GTK_LABEL(status), insight_tr("Testing the connection…"));
   g_object_set_data(G_OBJECT(dialog), "insight-wizard-grid", grid);
   g_task_run_in_thread(task, probe_worker);
   g_object_unref(task);
   continue;
  }

  if (profiles->len >= 16)
  {
   gtk_label_set_text(GTK_LABEL(status),
    insight_tr("There are already 16 profiles. Remove one in AI Connections before adding another."));
   profile_free(p);
   continue;
  }
  p->remember = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(remember_key));
  g_ptr_array_add(profiles, p);
  active = profiles->len - 1;
  save();
  if (manager)
   refresh(active);
  else
   broadcast();
  break;
 }
 g_object_set_data(G_OBJECT(dialog), "insight-wizard-status", NULL);
 g_cancellable_cancel(cancel);
 g_object_unref(cancel);

close:
 gtk_widget_destroy(dialog);
 g_object_unref(dialog);
}

/* EN: Build a button that opens the shared connection wizard.
 * RU: Создаёт кнопку открытия общего мастера подключения. */
static GtkWidget *wizard_button(void)
{
 GtkWidget *button = gtk_button_new_with_label(insight_tr("Connection Wizard…"));
 g_signal_connect(button, "clicked", G_CALLBACK(connection_wizard), NULL);
 return button;
}

/* EN: Test a snapshot of the manager fields without saving. Closing the
 * dialog cancels the request. button and data are unused.
 * RU: Проверяет текущие поля без сохранения. Закрытие окна отменяет запрос.
 * Параметры button и data не используются. */
static void manager_test_connection(GtkButton *button, gpointer data)
{
 (void)button;
 (void)data;
 Profile *p = g_new0(Profile, 1);
 gchar **values[] = { &p->name, &p->endpoint, &p->model, &p->python, &p->key, &p->env };
 for (guint i = 0; i < G_N_ELEMENTS(values); i++)
  *values[i] = g_strdup(gtk_entry_get_text(GTK_ENTRY(fields[i])));
 if (!*p->endpoint || !*p->model || !*p->python || strstr(p->endpoint, "YOUR_WORKSPACE"))
 {
  gtk_label_set_text(GTK_LABEL(notice),
   insight_tr("Enter a complete API endpoint, model ID and Python executable before testing."));
  profile_free(p);
  return;
 }

 GtkWidget *dialog = gtk_dialog_new_with_buttons(insight_tr("Test connection"),
  GTK_WINDOW(manager), GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
  insight_tr("Cancel"), GTK_RESPONSE_CLOSE, NULL);
 g_object_ref_sink(dialog);
 g_signal_connect(dialog, "destroy", G_CALLBACK(wizard_destroyed), NULL);
 GtkWidget *box = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
 gtk_container_set_border_width(GTK_CONTAINER(box), 12);
 GtkWidget *status = wizard_label(box, insight_tr("Testing the connection…"));
 g_object_set_data(G_OBJECT(dialog), "insight-wizard-status", status);

 ConnectionProbe *probe = g_new0(ConnectionProbe, 1);
 probe->profile = p;
 probe->helper = g_build_filename(app->datadir, "insight", "tools", "ai_client.py", NULL);
 GCancellable *cancel = g_cancellable_new();
 GTask *task = g_task_new(dialog, cancel, probe_done, NULL);
 g_task_set_task_data(task, probe, probe_free);
 g_task_run_in_thread(task, probe_worker);
 g_object_unref(task);

 gtk_widget_show_all(dialog);
 gtk_dialog_run(GTK_DIALOG(dialog));
 g_object_set_data(G_OBJECT(dialog), "insight-wizard-status", NULL);
 g_cancellable_cancel(cancel);
 g_object_unref(cancel);
 gtk_widget_destroy(dialog);
 g_object_unref(dialog);
}

/* EN: Open shared profiles under the invoking widget's window. data is unused.
 * RU: Открывает общие профили над окном вызывающего widget. data не используется. */
void insight_ai_profiles_show(GtkWidget *widget,gpointer data) {
 (void)data;
 init();
 GtkWidget *parent = widget && GTK_IS_BUTTON(widget) ? gtk_widget_get_toplevel(widget) : NULL;
 if (!GTK_IS_WINDOW(parent))
  parent = main_widgets.window;
 if (manager)
 {
  gtk_window_set_transient_for(GTK_WINDOW(manager), GTK_WINDOW(parent));
  gtk_window_set_modal(GTK_WINDOW(manager), gtk_window_get_modal(GTK_WINDOW(parent)));
  gtk_window_present(GTK_WINDOW(manager));
  return;
 }
 manager=gtk_window_new(GTK_WINDOW_TOPLEVEL);gtk_window_set_title(GTK_WINDOW(manager),insight_tr("AI Connections"));gtk_widget_set_name(manager,"insight-ai-profiles-window");gtk_window_set_transient_for(GTK_WINDOW(manager),GTK_WINDOW(parent));gtk_window_set_modal(GTK_WINDOW(manager),gtk_window_get_modal(GTK_WINDOW(parent)));gtk_window_set_destroy_with_parent(GTK_WINDOW(manager),TRUE);gtk_window_set_default_size(GTK_WINDOW(manager),760,540);GtkWidget *box=gtk_box_new(GTK_ORIENTATION_VERTICAL,4);gtk_container_set_border_width(GTK_CONTAINER(box),8);gtk_container_add(GTK_CONTAINER(manager),box);store=gtk_list_store_new(4,G_TYPE_INT,G_TYPE_STRING,G_TYPE_STRING,G_TYPE_STRING);tree=gtk_tree_view_new_with_model(GTK_TREE_MODEL(store));gtk_widget_set_name(tree,"insight-ai-profiles-list");const gchar *titles[]={"Name","Compare","Model"};for(guint i=0;i<3;i++) gtk_tree_view_insert_column_with_attributes(GTK_TREE_VIEW(tree),-1,insight_tr(titles[i]),gtk_cell_renderer_text_new(),"text",i+1,NULL);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),tree);gtk_box_pack_start(GTK_BOX(box),scroll,TRUE,TRUE,0);
 const gchar *labels[]={"Profile name","API endpoint","Model ID","Python 3","API key","API key environment variable"};for(guint i=0;i<6;i++) { GtkWidget *row=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4),*label=gtk_label_new(insight_tr(labels[i]));gtk_widget_set_size_request(label,180,-1);gtk_box_pack_start(GTK_BOX(row),label,FALSE,FALSE,0);fields[i]=gtk_entry_new();gchar *name=g_strconcat("insight-ai-profile-",names[i],NULL);gtk_widget_set_name(fields[i],name);g_free(name);gtk_entry_set_max_length(GTK_ENTRY(fields[i]),i==4 ? 8192:2048);gtk_entry_set_visibility(GTK_ENTRY(fields[i]),i!=4);insight_ai_ui_entry(fields[i],insight_tr(labels[i]),18);gtk_box_pack_start(GTK_BOX(row),fields[i],TRUE,TRUE,0);gtk_box_pack_start(GTK_BOX(box),row,FALSE,FALSE,0); }
 GtkWidget *preset=gtk_combo_box_text_new();gtk_widget_set_name(preset,"insight-ai-profile-preset");const gchar *presets[]={"OpenAI / ChatGPT API","xAI / Grok","DeepSeek","Qwen / Model Studio","Custom compatible API"};for(guint i=0;i<5;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(preset),presets[i]);gtk_box_pack_start(GTK_BOX(box),preset,FALSE,FALSE,0);g_signal_connect(preset,"changed",G_CALLBACK(preset_changed),NULL);
 checked=gtk_check_button_new_with_label(insight_tr("Include in parallel comparison (up to 4)"));gtk_widget_set_name(checked,"insight-ai-profile-checked");remember=gtk_check_button_new_with_label(insight_tr("Save API key in private config (not encrypted)"));gtk_widget_set_name(remember,"insight-ai-profile-remember");gtk_box_pack_start(GTK_BOX(box),checked,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),remember,FALSE,FALSE,0);GtkWidget *bar=gtk_box_new(GTK_ORIENTATION_HORIZONTAL,4);const gchar *buttons[]={"Add profile","Update profile","Remove profile"},*ids[]={"add","update","remove"};for(guint i=0;i<3;i++) { GtkWidget *button=gtk_button_new_with_label(insight_tr(buttons[i]));gchar *name=g_strconcat("insight-ai-profile-",ids[i],NULL);gtk_widget_set_name(button,name);g_free(name);g_signal_connect(button,"clicked",G_CALLBACK(edit_profile),GINT_TO_POINTER(i));gtk_box_pack_start(GTK_BOX(bar),button,FALSE,FALSE,0); }GtkWidget *test = gtk_button_new_with_label(insight_tr("Test connection"));
 gtk_widget_set_name(test, "insight-ai-profile-test");
 gtk_widget_set_tooltip_text(test, insight_tr("Send a short test request using the current fields without saving them."));
 g_signal_connect(test, "clicked", G_CALLBACK(manager_test_connection), NULL);
 gtk_box_pack_start(GTK_BOX(bar), test, FALSE, FALSE, 0);
 gtk_box_pack_start(GTK_BOX(bar),wizard_button(),FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),bar,FALSE,FALSE,0);notice=gtk_label_new(insight_tr("Connections are shared. Request history is stored separately per project and context; replies are not saved."));gtk_label_set_line_wrap(GTK_LABEL(notice),TRUE);gtk_box_pack_start(GTK_BOX(box),notice,FALSE,FALSE,0);gtk_box_pack_start(GTK_BOX(box),insight_panel_help_button("ai-connections.md"),FALSE,FALSE,0);g_signal_connect(manager,"destroy",G_CALLBACK(manager_destroyed),NULL);g_signal_connect(gtk_tree_view_get_selection(GTK_TREE_VIEW(tree)),"changed",G_CALLBACK(selected_changed),NULL);refresh(active<profiles->len ? active:0);insight_ai_ui_window(manager,"Minimize window","Maximize / restore window","Close window");gtk_widget_show_all(manager);
}
/* Every worker owns an immutable request and connection snapshot. */
typedef struct { gchar *name,*python,*helper;gchar *values[9];InsightAiAnswer *answer;GtkWidget *view; } Candidate;
typedef struct { GTask *outer;InsightAiAnswer **output;GPtrArray *candidates;GCancellable *cancel;GtkWidget *dialog,*tabs;gboolean finished; } Batch;
static void candidate_free(gpointer data) { Candidate *c=data;g_free(c->name);g_free(c->python);g_free(c->helper);for(guint i=0;i<9;i++) if(i==2) secret_free(c->values[i]);else g_free(c->values[i]);insight_ai_answer_free(c->answer);g_free(c); }
typedef struct { GPtrArray *answers;guint chosen; } AnswerPool;
static void pool_free(gpointer data) { AnswerPool *p=data;g_ptr_array_unref(p->answers);g_free(p); }
static void retain_answers(Batch *b,Candidate *chosen) {
 AnswerPool *pool=g_new0(AnswerPool,1);pool->answers=g_ptr_array_new_with_free_func(candidate_free);for(guint i=0;i<b->candidates->len;i++) { Candidate *c=g_ptr_array_index(b->candidates,i);if(!c->answer) continue;if(c==chosen) pool->chosen=pool->answers->len;Candidate *copy=g_new0(Candidate,1);copy->name=g_strdup(c->name);copy->values[5]=g_strdup(c->answer->has_replacement ? c->answer->replacement:c->values[5]);copy->answer=g_new0(InsightAiAnswer,1);copy->answer->explanation=g_strdup(c->answer->explanation);g_ptr_array_add(pool->answers,copy); }g_object_set_data_full(g_task_get_source_object(b->outer),"insight-ai-answer-pool",pool,pool_free);
}
void insight_ai_profiles_install_choices(GtkWidget *source,GtkWidget *target,const gchar *base,const gchar *filename) {
 if(!target) return;AnswerPool *p=g_object_get_data(G_OBJECT(source),"insight-ai-answer-pool");insight_diff_review_language(target,filename);if(!p) { insight_diff_review_choices(target,NULL,NULL,NULL,NULL,0,0);return; }const gchar *names[4],*codes[4],*notes[4];for(guint i=0;i<p->answers->len;i++) { Candidate *a=g_ptr_array_index(p->answers,i);names[i]=a->name;codes[i]=a->values[5];notes[i]=a->answer->explanation; }insight_diff_review_choices(target,base,names,codes,notes,p->answers->len,p->chosen);
}
static void batch_free(gpointer data) { Batch *b=data;g_object_unref(b->outer);g_ptr_array_unref(b->candidates);g_object_unref(b->cancel);g_free(b); }
static void batch_controls(Batch *b) { if(b->finished) return;gint page=gtk_notebook_get_current_page(GTK_NOTEBOOK(b->tabs));gboolean ready=page>=0 && (guint)page<b->candidates->len && ((Candidate *)g_ptr_array_index(b->candidates,page))->answer;gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(b->dialog),GTK_RESPONSE_ACCEPT),ready); }
static void batch_page(GtkNotebook *tabs,GtkWidget *page,guint num,gpointer data) { (void)tabs;(void)page;Batch *b=data;if(!b->finished) gtk_widget_set_sensitive(gtk_dialog_get_widget_for_response(GTK_DIALOG(b->dialog),GTK_RESPONSE_ACCEPT),num<b->candidates->len && ((Candidate *)g_ptr_array_index(b->candidates,num))->answer!=NULL); }
static void finish(Batch *b,gboolean use) {
 if(b->finished) return;gint page=gtk_notebook_get_current_page(GTK_NOTEBOOK(b->tabs));Candidate *c=page>=0 && (guint)page<b->candidates->len ? g_ptr_array_index(b->candidates,page):NULL;if(use && (!c || !c->answer)) return;b->finished=TRUE;
 if(use) { retain_answers(b,c);*b->output=c->answer;c->answer=NULL;g_task_return_boolean(b->outer,TRUE); }else g_task_return_new_error(b->outer,G_IO_ERROR,G_IO_ERROR_CANCELLED,"AI answer comparison cancelled");g_cancellable_cancel(b->cancel);gtk_widget_destroy(b->dialog);
}
static void batch_response(GtkDialog *dialog,gint response,gpointer data) { (void)dialog;Batch *b=data;if(response==101) { gint page=gtk_notebook_get_current_page(GTK_NOTEBOOK(b->tabs));if(page>=0 && (guint)page<b->candidates->len) { Candidate *c=g_ptr_array_index(b->candidates,page);if(c->answer) gtk_clipboard_set_text(gtk_clipboard_get(GDK_SELECTION_CLIPBOARD),c->answer->explanation,-1); }return; }finish(b,response==GTK_RESPONSE_ACCEPT); }
static void batch_destroyed(GtkWidget *widget,gpointer data) { (void)widget;Batch *b=data;if(!b->finished) { b->finished=TRUE;g_cancellable_cancel(b->cancel);g_task_return_new_error(b->outer,G_IO_ERROR,G_IO_ERROR_CANCELLED,"AI answer comparison closed"); } }
static gboolean cancelled_idle(gpointer data) { GtkWidget *dialog=data;Batch *b=g_object_get_data(G_OBJECT(dialog),"insight-ai-batch");if(b && !b->finished) finish(b,FALSE);g_object_unref(dialog);return G_SOURCE_REMOVE; }
static void cancelled(GCancellable *cancel,gpointer data) { (void)cancel;g_main_context_invoke(NULL,cancelled_idle,g_object_ref(data)); }
static void candidate_worker(GTask *task,gpointer object,gpointer data,GCancellable *cancel) { (void)object;Candidate *c=data;InsightAiRequest r={c->values[0],c->values[1],c->values[2],c->values[3],c->values[4],c->values[5],c->values[6],c->values[7],c->values[8]};GError *error=NULL;InsightAiAnswer *answer=insight_ai_request(c->python,c->helper,&r,cancel,&error);if(answer) g_task_return_pointer(task,answer,(GDestroyNotify)insight_ai_answer_free);else if(error) g_task_return_error(task,error);else g_task_return_new_error(task,G_IO_ERROR,G_IO_ERROR_FAILED,"AI request failed"); }
static void candidate_done(GObject *object,GAsyncResult *result,gpointer data) { (void)data;Batch *b=g_object_get_data(object,"insight-ai-batch");Candidate *c=g_task_get_task_data(G_TASK(result));GError *error=NULL;InsightAiAnswer *response=g_task_propagate_pointer(G_TASK(result),&error);if(!b->finished) { c->answer=response;response=NULL;gchar *answer=c->answer ? g_strdup_printf("%s\n\n%s\n\n%s%s",c->name,c->answer->explanation,c->answer->command ? c->answer->command:"",c->answer->has_replacement ? c->answer->replacement:"") : g_strdup_printf("%s\n\n%s",c->name,error ? error->message:"Request failed");gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(c->view)),answer,-1);g_free(answer);batch_controls(b); }insight_ai_answer_free(response);g_clear_error(&error); }
gboolean insight_ai_profiles_dispatch(InsightAiProfiles *binding,GTask *outer,const InsightAiRequest *request,InsightAiAnswer **output) {
 (void)binding;init();if(active<profiles->len) { g_object_set_data(G_OBJECT(g_task_get_source_object(outer)),"insight-ai-answer-pool",NULL);return FALSE; }guint count=0;for(guint i=0;i<profiles->len;i++) if(((Profile *)g_ptr_array_index(profiles,i))->checked) count++;
 if(!count || count>4) { g_task_return_new_error(outer,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Check between one and four AI profiles in AI Connections");return TRUE; }
 Batch *b=g_new0(Batch,1);b->outer=g_object_ref(outer);b->output=output;b->cancel=g_cancellable_new();b->candidates=g_ptr_array_new_with_free_func(candidate_free);GtkWidget *parent=GTK_WIDGET(g_task_get_source_object(outer));if(GTK_IS_WINDOW(parent) && !gtk_widget_get_visible(parent) && gtk_window_get_transient_for(GTK_WINDOW(parent))) parent=GTK_WIDGET(gtk_window_get_transient_for(GTK_WINDOW(parent)));b->dialog=gtk_dialog_new_with_buttons(insight_tr("Compare AI Answers"),GTK_WINDOW(parent),GTK_DIALOG_DESTROY_WITH_PARENT,insight_tr("Cancel requests"),GTK_RESPONSE_CANCEL,insight_tr("Copy answer"),101,insight_tr("Use selected answer"),GTK_RESPONSE_ACCEPT,NULL);gtk_widget_set_name(b->dialog,"insight-ai-comparison-window");gtk_window_set_default_size(GTK_WINDOW(b->dialog),850,560);b->tabs=gtk_notebook_new();gtk_widget_set_name(b->tabs,"insight-ai-comparison-tabs");gtk_box_pack_start(GTK_BOX(gtk_dialog_get_content_area(GTK_DIALOG(b->dialog))),b->tabs,TRUE,TRUE,0);g_object_set_data_full(G_OBJECT(b->dialog),"insight-ai-batch",b,batch_free);
 for(guint i=0;i<profiles->len;i++) { Profile *p=g_ptr_array_index(profiles,i);if(!p->checked) continue;Candidate *c=g_new0(Candidate,1);c->name=g_strdup(p->name);c->python=g_strdup(p->python);c->helper=g_build_filename(app->datadir,"insight","tools","ai_client.py",NULL);const gchar *values[]={p->endpoint,p->model,profile_key(p),request->question,request->context,request->code,request->history,request->location,request->attachments};for(guint n=0;n<9;n++) c->values[n]=g_strdup(values[n] ? values[n]:"");c->view=gtk_text_view_new();gtk_text_view_set_editable(GTK_TEXT_VIEW(c->view),FALSE);gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(c->view),GTK_WRAP_WORD_CHAR);gtk_text_buffer_set_text(gtk_text_view_get_buffer(GTK_TEXT_VIEW(c->view)),insight_tr("Waiting for this AI response…"),-1);GtkWidget *scroll=gtk_scrolled_window_new(NULL,NULL);gtk_container_add(GTK_CONTAINER(scroll),c->view);gtk_notebook_append_page(GTK_NOTEBOOK(b->tabs),scroll,gtk_label_new(c->name));g_ptr_array_add(b->candidates,c);
  GTask *task=g_task_new(b->dialog,b->cancel,candidate_done,NULL);g_task_set_task_data(task,c,NULL);g_task_run_in_thread(task,candidate_worker);g_object_unref(task);
 }
 g_signal_connect(b->dialog,"response",G_CALLBACK(batch_response),b);g_signal_connect(b->dialog,"destroy",G_CALLBACK(batch_destroyed),b);g_signal_connect(b->tabs,"switch-page",G_CALLBACK(batch_page),b);GCancellable *cancel=g_task_get_cancellable(outer);if(cancel) g_signal_connect_object(cancel,"cancelled",G_CALLBACK(cancelled),b->dialog,0);batch_controls(b);gtk_widget_show_all(b->dialog);return TRUE;
}
