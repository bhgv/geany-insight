/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "filetype_editing.h"
typedef struct { GtkWidget *edit_enabled,*edit_grid,*edit_tab,*edit_indent,*edit_type,*edit_auto,*edit_margin,*edit_flags[7]; } EditingPage;
static void editing_sensitive(GtkWidget *widget,gpointer data) {
 (void)widget;EditingPage *d=data;gboolean enabled=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->edit_enabled));gtk_widget_set_sensitive(d->edit_grid,enabled);gtk_widget_set_sensitive(d->edit_indent,enabled && gtk_combo_box_get_active(GTK_COMBO_BOX(d->edit_type))!=1);
}
static void editing_fill(EditingPage *d,const InsightFiletypeOptions *p) {
 gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->edit_enabled),p->enabled);gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->edit_tab),p->tab_width);gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->edit_indent),p->indent_width);gtk_combo_box_set_active(GTK_COMBO_BOX(d->edit_type),p->indent_type);gtk_combo_box_set_active(GTK_COMBO_BOX(d->edit_auto),p->auto_indent);gtk_spin_button_set_value(GTK_SPIN_BUTTON(d->edit_margin),p->margin);
 const gboolean flags[]={p->soft_wrap,p->hard_wrap,p->line_numbers,p->white_space,p->line_endings,p->indent_guides,p->right_margin};for(guint i=0;i<7;i++) gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(d->edit_flags[i]),flags[i]);editing_sensitive(NULL,d);
}
static void editing_read(EditingPage *d,InsightFiletypeOptions *p) {
 insight_filetype_options_default(p);p->enabled=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->edit_enabled));p->tab_width=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->edit_tab));p->indent_width=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->edit_indent));p->indent_type=gtk_combo_box_get_active(GTK_COMBO_BOX(d->edit_type));p->auto_indent=gtk_combo_box_get_active(GTK_COMBO_BOX(d->edit_auto));p->margin=gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON(d->edit_margin));
 gboolean *flags[]={&p->soft_wrap,&p->hard_wrap,&p->line_numbers,&p->white_space,&p->line_endings,&p->indent_guides,&p->right_margin};for(guint i=0;i<7;i++) *flags[i]=gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(d->edit_flags[i]));if(p->indent_type==1) p->indent_width=p->tab_width;
}
GtkWidget *insight_filetype_editing_new(void) {
 EditingPage *d=g_new0(EditingPage,1);
 GtkWidget *page=gtk_box_new(GTK_ORIENTATION_VERTICAL,6);d->edit_enabled=gtk_check_button_new_with_label("Use file type editing options");gtk_widget_set_name(d->edit_enabled,"insight-filetype-editing-enabled");gtk_box_pack_start(GTK_BOX(page),d->edit_enabled,FALSE,FALSE,6);gtk_box_pack_start(GTK_BOX(page),gtk_label_new("Disabled profiles inherit existing document settings. Apply does not reformat text."),FALSE,FALSE,2);
 d->edit_grid=gtk_grid_new();gtk_grid_set_row_spacing(GTK_GRID(d->edit_grid),6);gtk_grid_set_column_spacing(GTK_GRID(d->edit_grid),12);gtk_box_pack_start(GTK_BOX(page),d->edit_grid,FALSE,FALSE,6);
 d->edit_tab=gtk_spin_button_new_with_range(1,32,1);d->edit_indent=gtk_spin_button_new_with_range(1,32,1);d->edit_margin=gtk_spin_button_new_with_range(20,500,1);d->edit_type=gtk_combo_box_text_new();const gchar *types[]={"Spaces","Tabs","Tabs and spaces"};for(guint i=0;i<3;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->edit_type),types[i]);d->edit_auto=gtk_combo_box_text_new();const gchar *modes[]={"No auto indent","Basic auto indent","Current characters","Matching braces"};for(guint i=0;i<4;i++) gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(d->edit_auto),modes[i]);
 GtkWidget *fields[]={d->edit_type,d->edit_tab,d->edit_indent,d->edit_auto,d->edit_margin};const gchar *labels[]={"Indent using","Tab width","Indent width","Auto indent","Margin column"},*names[]={"type","tab","indent","auto","margin"};for(guint i=0;i<5;i++) { gchar *name=g_strconcat("insight-filetype-editing-",names[i],NULL);gtk_widget_set_name(fields[i],name);g_free(name);gtk_grid_attach(GTK_GRID(d->edit_grid),gtk_label_new(labels[i]),0,i,1,1);gtk_grid_attach(GTK_GRID(d->edit_grid),fields[i],1,i,1,1); }
 const gchar *flags[]={"Wrap long lines on screen","Wrap while typing (insert newlines)","Show line numbers","Visible tabs and spaces","Visible line endings","Show indentation guides","Show right margin"},*ids[]={"soft-wrap","hard-wrap","line-numbers","white-space","line-endings","guides","right-margin"};for(guint i=0;i<7;i++) { d->edit_flags[i]=gtk_check_button_new_with_label(flags[i]);gchar *name=g_strconcat("insight-filetype-editing-",ids[i],NULL);gtk_widget_set_name(d->edit_flags[i],name);g_free(name);gtk_grid_attach(GTK_GRID(d->edit_grid),d->edit_flags[i],2,i,1,1); }
 g_object_set_data_full(G_OBJECT(page),"insight-filetype-editing",d,g_free);g_signal_connect(d->edit_enabled,"toggled",G_CALLBACK(editing_sensitive),d);g_signal_connect(d->edit_type,"changed",G_CALLBACK(editing_sensitive),d);
 InsightFiletypeOptions defaults;insight_filetype_options_default(&defaults);editing_fill(d,&defaults);return page;
}

void insight_filetype_editing_set(GtkWidget *page,const InsightFiletypeOptions *options) { editing_fill(g_object_get_data(G_OBJECT(page),"insight-filetype-editing"),options); }
void insight_filetype_editing_get(GtkWidget *page,InsightFiletypeOptions *options) { editing_read(g_object_get_data(G_OBJECT(page),"insight-filetype-editing"),options); }
