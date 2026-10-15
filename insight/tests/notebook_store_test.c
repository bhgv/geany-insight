#include "notebook_store.h"
#include <glib.h>
#include <gio/gio.h>
#include <glib/gstdio.h>
static void symbol_anchors(void) {
 GError *error=NULL;InsightNotes *notes=insight_notes_new();gchar *path=g_build_filename(g_get_tmp_dir(),"anchor.c",NULL);
 g_assert_true(insight_notes_set(notes,INSIGHT_BOOKMARK,"symbol",path,5," return;",&error));
 InsightBookmarkSymbol original[]={{"first","",INSIGHT_BOOKMARK_FUNCTION,1},{"target","",INSIGHT_BOOKMARK_FUNCTION,4}};
 g_assert_true(insight_notes_capture_anchor(notes,"symbol",original,2,&error));const InsightNote *note=insight_notes_get(notes,INSIGHT_BOOKMARK,"symbol");g_assert_nonnull(note->anchor);g_assert_cmpstr(note->anchor->name,==,"target");g_assert_cmpuint(note->anchor->offset,==,1);
 const gchar *changed="header\nfirst()\n}\ngap\n return;\ngap\ntarget()\n return;\n}";
 InsightBookmarkSymbol moved[]={{"first","",INSIGHT_BOOKMARK_FUNCTION,2},{"target","",INSIGHT_BOOKMARK_FUNCTION,7}};guint64 line=0;gboolean ambiguous=FALSE;
 g_assert_true(insight_bookmark_resolve_symbols(note,changed,strlen(changed),moved,2,&line,&ambiguous));g_assert_cmpuint(line,==,8);
 g_assert_false(insight_bookmark_resolve_symbols(note,changed,strlen(changed),NULL,0,&line,&ambiguous));g_assert_true(ambiguous); /* No stale line-5 preference. */
 InsightBookmarkSymbol duplicate[]={{"target","",INSIGHT_BOOKMARK_FUNCTION,2},{"target","",INSIGHT_BOOKMARK_FUNCTION,7}};
 g_assert_false(insight_bookmark_resolve_symbols(note,changed,strlen(changed),duplicate,2,&line,&ambiguous));g_assert_true(ambiguous);
 const gchar *unique="x\r return;";g_assert_true(insight_bookmark_resolve_symbols(note,unique,strlen(unique),NULL,0,&line,&ambiguous));g_assert_cmpuint(line,==,2);
 gchar *cr=g_strdup(changed);for(gchar *p=cr;*p;p++) if(*p=='\n') *p='\r';g_assert_true(insight_bookmark_resolve_symbols(note,cr,strlen(cr),moved,2,&line,&ambiguous));g_assert_cmpuint(line,==,8);g_free(cr);
 InsightBookmarkSymbol scoped[]={{"target","other",INSIGHT_BOOKMARK_FUNCTION,2},{"target","",INSIGHT_BOOKMARK_FUNCTION,7}};g_assert_true(insight_bookmark_resolve_symbols(note,changed,strlen(changed),scoped,2,&line,&ambiguous));g_assert_cmpuint(line,==,8);
 g_assert_false(insight_bookmark_resolve_symbols(note,"x\ny",3,moved,2,&line,&ambiguous));g_assert_cmpuint(line,==,0);
 gchar *file=g_strdup_printf("anchor-notes-%u.conf",g_random_int());g_assert_true(insight_notes_save(notes,file,&error));InsightNotes *copy=insight_notes_load(file,&error);g_assert_no_error(error);g_assert_cmpstr(insight_notes_get(copy,INSIGHT_BOOKMARK,"symbol")->anchor->name,==,"target");insight_notes_free(copy);
 copy=insight_notes_rename(notes,INSIGHT_BOOKMARK,"symbol","renamed",&error);g_assert_no_error(error);g_assert_cmpuint(insight_notes_get(copy,INSIGHT_BOOKMARK,"renamed")->anchor->offset,==,1);insight_notes_free(copy);
 copy=insight_notes_select(notes,INSIGHT_BOOKMARK,&error);g_assert_nonnull(insight_notes_get(copy,INSIGHT_BOOKMARK,"symbol")->anchor);InsightNotes *empty=insight_notes_new(),*merged=insight_notes_merge(empty,copy,INSIGHT_BOOKMARK,&error);g_assert_no_error(error);g_assert_nonnull(insight_notes_get(merged,INSIGHT_BOOKMARK,"symbol")->anchor);insight_notes_free(empty);insight_notes_free(merged);insight_notes_free(copy);
 gchar *root=g_path_get_dirname(path);g_assert_true(insight_notes_save_portable(notes,file,root,&error));copy=insight_notes_load_portable(file,root,&error);g_assert_no_error(error);g_assert_cmpstr(insight_notes_get(copy,INSIGHT_BOOKMARK,"symbol")->anchor->name,==,"target");insight_notes_free(copy);g_free(root);
 const gchar *bad[]={"anchor-name=target\n","anchor-name=target\nanchor-scope=\nanchor-kind=3\nanchor-offset=1\n","anchor-name=target\nanchor-scope=\nanchor-kind=1\nanchor-offset=2147483647\n"};
 for(guint i=0;i<G_N_ELEMENTS(bad);i++) { gchar *text=g_strdup_printf("[notes]\nversion=1\ncount=1\n[note-0]\nkind=1\nname=x\npath=%s\nline=5\ntext= return;\n%s",path,bad[i]);g_assert_true(g_file_set_contents(file,text,-1,&error));g_free(text);copy=insight_notes_load(file,&error);g_assert_null(copy);g_assert_nonnull(error);g_clear_error(&error); }
 duplicate[1].line=2;g_assert_true(insight_notes_capture_anchor(notes,"symbol",duplicate,2,&error));g_assert_null(insight_notes_get(notes,INSIGHT_BOOKMARK,"symbol")->anchor);
 g_remove(file);g_free(file);g_free(path);insight_notes_free(notes);
}
static void checked_replacement(void) {
 GError *error=NULL;gchar *dir=g_dir_make_tmp("notes-etag-XXXXXX",&error),*file=g_build_filename(dir,"notes.conf",NULL),*source=g_build_filename(dir,"source.c",NULL),*etag=NULL;
 g_assert_no_error(error);InsightNotes *notes=insight_notes_new();
 g_assert_true(insight_notes_set(notes,INSIGHT_BOOKMARK,"mark",source,1,"code",&error));g_assert_true(insight_notes_set(notes,INSIGHT_CLIP,"clip",NULL,0,"текст ${field}",&error));
 const gchar *languages[]={"C","Python",NULL};g_assert_true(insight_notes_set_languages(notes,"clip",languages,&error));
 g_assert_true(insight_notes_save_portable_checked(notes,file,NULL,NULL,&etag,&error));g_assert_nonnull(etag);
 InsightNotes *clips=insight_notes_select(notes,INSIGHT_CLIP,&error);g_assert_nonnull(clips);
 g_assert_true(g_file_set_contents(file,"changed externally; retain this",-1,&error));
 g_assert_false(insight_notes_save_portable_checked(clips,file,NULL,etag,NULL,&error));g_assert_error(error,G_IO_ERROR,G_IO_ERROR_WRONG_ETAG);g_clear_error(&error);
 gchar *text=NULL;g_assert_true(g_file_get_contents(file,&text,NULL,&error));g_assert_cmpstr(text,==,"changed externally; retain this");g_free(text);g_free(etag);etag=NULL;
 g_assert_true(insight_notes_save_portable_checked(notes,file,dir,NULL,&etag,&error));g_assert_nonnull(etag);
 g_assert_true(insight_notes_save_portable_checked(clips,file,dir,etag,NULL,&error));
 InsightNotes *loaded=insight_notes_load_portable(file,dir,&error);g_assert_no_error(error);g_assert_cmpuint(loaded->items->len,==,1);const InsightNote *clip=insight_notes_get(loaded,INSIGHT_CLIP,"clip");g_assert_nonnull(clip);g_assert_cmpstr(clip->text,==,"текст ${field}");g_assert_cmpstr(clip->languages[0],==,"C");g_assert_cmpstr(clip->languages[1],==,"Python");
 insight_notes_free(loaded);insight_notes_free(clips);insight_notes_free(notes);g_free(etag);g_remove(file);g_rmdir(dir);g_free(source);g_free(file);g_free(dir);
}
int main(void) {
 checked_replacement();
 symbol_anchors();
 InsightNotes *n=insight_notes_new(); GError *e=NULL;
 gchar *source_path=g_build_filename(g_get_tmp_dir(),"source.c",NULL),*other_path=g_build_filename(g_get_tmp_dir(),"other.c",NULL),*external_path=g_build_filename(g_get_tmp_dir(),"external.c",NULL);
 g_assert_true(insight_notes_set(n,INSIGHT_BOOKMARK,"entry",source_path,2,"call();",&e));
 g_assert_true(insight_notes_set(n,INSIGHT_CLIP,"entry",NULL,0,"void f() {\n}\n",&e)); g_assert_cmpuint(n->items->len,==,2);
 g_assert_true(insight_notes_set(n,INSIGHT_CLIP,"entry",NULL,0,"updated",&e)); g_assert_cmpstr(insight_notes_get(n,INSIGHT_CLIP,"entry")->text,==,"updated");
 const InsightNote *mark=insight_notes_get(n,INSIGHT_BOOKMARK,"entry"); guint64 line; gboolean ambiguous;
 const char *moved="header\nextra\ncall();\n"; g_assert_true(insight_bookmark_resolve(mark,moved,strlen(moved),&line,&ambiguous)); g_assert_cmpuint(line,==,3);
 const char *dup="call();\nother\ncall();"; g_assert_false(insight_bookmark_resolve(mark,dup,strlen(dup),&line,&ambiguous)); g_assert_true(ambiguous);
 gchar *file=g_strdup_printf("notes-test-%u.conf",g_random_int()); g_assert_true(insight_notes_save(n,file,&e)); InsightNotes *copy=insight_notes_load(file,&e); g_assert_no_error(e); g_assert_nonnull(copy); g_assert_cmpuint(copy->items->len,==,2);
 g_assert_true(insight_notes_remove(copy,INSIGHT_CLIP,"entry")); g_assert_false(insight_notes_remove(copy,INSIGHT_CLIP,"missing")); insight_notes_free(copy);
 g_assert_true(g_file_set_contents(file,"[notes]\nversion=1\ncount=garbage\n",-1,&e)); copy=insight_notes_load(file,&e); g_assert_null(copy); g_assert_nonnull(e); g_clear_error(&e);
 g_assert_false(insight_notes_set(n,INSIGHT_BOOKMARK,"bad","relative.c",1,"text",NULL));
 /* Export only the selected panel, including Unicode and multiline text. */
 g_assert_true(insight_notes_set(n,INSIGHT_CLIP,"пример",NULL,0,"строка\n[section]\\value\t",&e));
 InsightNotes *clips=insight_notes_select(n,INSIGHT_CLIP,&e); g_assert_no_error(e); g_assert_cmpuint(clips->items->len,==,2);
 g_assert_null(insight_notes_get(clips,INSIGHT_BOOKMARK,"entry"));
 g_assert_true(insight_notes_save(clips,file,&e)); copy=insight_notes_load(file,&e); g_assert_no_error(e);
 g_assert_cmpstr(insight_notes_get(copy,INSIGHT_CLIP,"пример")->text,==,"строка\n[section]\\value\t"); insight_notes_free(copy);
 gchar *directory=g_strdup_printf("notes-directory-%u",g_random_int()); g_assert_cmpint(g_mkdir(directory,0700),==,0);
 g_assert_false(insight_notes_save(n,directory,&e)); g_assert_nonnull(e); g_clear_error(&e); g_rmdir(directory); g_free(directory);
 copy=insight_notes_load(file,&e); g_assert_no_error(e); g_assert_cmpuint(copy->items->len,==,2); insight_notes_free(copy);
 InsightNotes *destination=insight_notes_new(); g_assert_true(insight_notes_set(destination,INSIGHT_BOOKMARK,"entry",other_path,1,"other",&e));
 copy=insight_notes_merge(destination,clips,INSIGHT_BOOKMARK,&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA); g_clear_error(&e);
 copy=insight_notes_merge(destination,n,INSIGHT_CLIP,&e); g_assert_no_error(e); g_assert_cmpuint(copy->items->len,==,3);
 g_assert_cmpstr(insight_notes_get(copy,INSIGHT_BOOKMARK,"entry")->path,==,other_path); g_assert_cmpuint(destination->items->len,==,1); insight_notes_free(copy);
 /* A late collision must not import preceding records or overwrite data. */
 g_assert_true(insight_notes_set(destination,INSIGHT_CLIP,"пример",NULL,0,"keep",&e));
 copy=insight_notes_merge(destination,n,INSIGHT_CLIP,&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_EXISTS); g_clear_error(&e);
 g_assert_cmpuint(destination->items->len,==,2); g_assert_null(insight_notes_get(destination,INSIGHT_CLIP,"entry")); g_assert_cmpstr(insight_notes_get(destination,INSIGHT_CLIP,"пример")->text,==,"keep");
 for(guint i=0;i<62;i++) { gchar *name=g_strdup_printf("note-%u",i); g_assert_true(insight_notes_set(destination,INSIGHT_CLIP,name,NULL,0,"x",&e)); g_free(name); }
 InsightNotes *incoming=insight_notes_new(); g_assert_true(insight_notes_set(incoming,INSIGHT_CLIP,"new",NULL,0,"x",&e));
 copy=insight_notes_merge(destination,incoming,INSIGHT_CLIP,&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_NO_SPACE); g_clear_error(&e); g_assert_cmpuint(destination->items->len,==,64);
 copy=insight_notes_select(n,(InsightNoteKind)0,&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT); g_clear_error(&e);
 /* Rename still works at the shared limit and does not alter source notes. */
 copy=insight_notes_rename(destination,INSIGHT_CLIP,"пример","новое имя",&e); g_assert_no_error(e); g_assert_cmpuint(copy->items->len,==,64);
 g_assert_null(insight_notes_get(copy,INSIGHT_CLIP,"пример")); g_assert_cmpstr(insight_notes_get(copy,INSIGHT_CLIP,"новое имя")->text,==,"keep"); insight_notes_free(copy);
 copy=insight_notes_rename(destination,INSIGHT_CLIP,"пример","note-0",&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_EXISTS); g_clear_error(&e);
 copy=insight_notes_rename(destination,INSIGHT_CLIP,"пример","",&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA); g_clear_error(&e);
 g_assert_nonnull(insight_notes_get(destination,INSIGHT_CLIP,"пример"));
 copy=insight_notes_rename(n,INSIGHT_BOOKMARK,"entry","переход",&e); g_assert_no_error(e);
 g_assert_cmpstr(insight_notes_get(copy,INSIGHT_BOOKMARK,"переход")->path,==,source_path); g_assert_cmpuint(insight_notes_get(copy,INSIGHT_BOOKMARK,"переход")->line,==,2);
 g_assert_cmpstr(insight_notes_get(copy,INSIGHT_BOOKMARK,"переход")->text,==,"call();"); g_assert_nonnull(insight_notes_get(copy,INSIGHT_CLIP,"entry")); insight_notes_free(copy);
 /* Language metadata survives every storage operation, including legacy loads. */
 const gchar *langs[]={"C","C++",NULL};
 g_assert_true(insight_notes_set_languages(n,"entry",langs,&e));
 const InsightNote *clip=insight_notes_get(n,INSIGHT_CLIP,"entry");
 g_assert_true(insight_note_matches_language(clip,"C")); g_assert_true(insight_note_matches_language(clip,"C++")); g_assert_false(insight_note_matches_language(clip,"Python"));
 const gchar *bad_langs[]={"C","C",NULL}; g_assert_false(insight_notes_set_languages(n,"entry",bad_langs,&e)); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA); g_clear_error(&e); g_assert_true(insight_note_matches_language(clip,"C++"));
 g_assert_true(insight_notes_set(n,INSIGHT_CLIP,"entry",NULL,0,"edited",&e)); clip=insight_notes_get(n,INSIGHT_CLIP,"entry"); g_assert_false(insight_note_matches_language(clip,"Python"));
 g_assert_true(insight_notes_save(n,file,&e)); copy=insight_notes_load(file,&e); g_assert_no_error(e); g_assert_false(insight_note_matches_language(insight_notes_get(copy,INSIGHT_CLIP,"entry"),"Python")); insight_notes_free(copy);
 copy=insight_notes_rename(n,INSIGHT_CLIP,"entry","renamed language clip",&e); g_assert_no_error(e); g_assert_true(insight_note_matches_language(insight_notes_get(copy,INSIGHT_CLIP,"renamed language clip"),"C++")); insight_notes_free(copy);
 copy=insight_notes_select(n,INSIGHT_CLIP,&e); g_assert_no_error(e); InsightNotes *empty=insight_notes_new(),*merged=insight_notes_merge(empty,copy,INSIGHT_CLIP,&e); g_assert_no_error(e); g_assert_false(insight_note_matches_language(insight_notes_get(merged,INSIGHT_CLIP,"entry"),"Sh")); insight_notes_free(merged); insight_notes_free(empty); insight_notes_free(copy);
 g_assert_true(insight_notes_set_languages(n,"entry",NULL,&e)); g_assert_true(insight_note_matches_language(insight_notes_get(n,INSIGHT_CLIP,"entry"),NULL));
 g_assert_true(g_file_set_contents(file,"[notes]\nversion=1\ncount=1\n[note-0]\nkind=2\nname=legacy\npath=\nline=0\ntext=plain\n",-1,&e)); copy=insight_notes_load(file,&e); g_assert_no_error(e); g_assert_true(insight_note_matches_language(insight_notes_get(copy,INSIGHT_CLIP,"legacy"),"Python")); insight_notes_free(copy);
 const char invalid[]="[notes]\nversion=1\ncount=0\n\0unexpected";
 g_assert_true(g_file_set_contents(file,invalid,sizeof invalid-1,&e)); copy=insight_notes_load(file,&e); g_assert_null(copy); g_assert_error(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA); g_clear_error(&e);
 /* Portable export rebases only project-local bookmarks, never the inputs. */
 gchar *old_root=g_build_filename(g_get_tmp_dir(),"insight-old",NULL),*new_root=g_build_filename(g_get_tmp_dir(),"insight-moved",NULL);
 gchar *old_path=g_build_filename(old_root,"sub","пример.c",NULL),*new_path=g_build_filename(new_root,"sub","пример.c",NULL);
 InsightNotes *portable=insight_notes_new();g_assert_true(insight_notes_set(portable,INSIGHT_BOOKMARK,"relative",old_path,7,"anchor",&e));
 g_assert_true(insight_notes_set(portable,INSIGHT_BOOKMARK,"external",external_path,2,"external",&e));g_assert_true(insight_notes_set(portable,INSIGHT_CLIP,"clip",NULL,0,"content",&e));
 g_assert_true(insight_notes_save_portable(portable,file,old_root,&e));g_assert_no_error(e);
 g_assert_cmpstr(insight_notes_get(portable,INSIGHT_BOOKMARK,"relative")->path,==,old_path);
 copy=insight_notes_load_portable(file,new_root,&e);g_assert_no_error(e);g_assert_nonnull(copy);
 g_assert_cmpstr(insight_notes_get(copy,INSIGHT_BOOKMARK,"relative")->path,==,new_path);g_assert_cmpstr(insight_notes_get(copy,INSIGHT_BOOKMARK,"external")->path,==,external_path);g_assert_cmpstr(insight_notes_get(copy,INSIGHT_CLIP,"clip")->text,==,"content");insight_notes_free(copy);
 copy=insight_notes_load(file,&e);g_assert_null(copy);g_assert_error(e,G_IO_ERROR,G_IO_ERROR_INVALID_DATA);g_clear_error(&e);
 const char *unsafe[]={"../outside.c","sub/../../outside.c","/absolute.c","C:/outside.c","sub\\outside.c","sub//file.c","./file.c"};
 for(guint i=0;i<G_N_ELEMENTS(unsafe);i++) {
  gchar *snapshot=g_strdup_printf("[notes]\nversion=2\ncount=1\n[note-0]\nkind=1\nname=bad\npath=%s\nproject-relative=true\nline=1\ntext=anchor\n",unsafe[i]);
  g_assert_true(g_file_set_contents(file,snapshot,-1,&e));g_free(snapshot);copy=insight_notes_load_portable(file,new_root,&e);g_assert_null(copy);g_assert_nonnull(e);g_clear_error(&e);
 }
 insight_notes_free(portable);g_free(old_root);g_free(new_root);g_free(old_path);g_free(new_path);
 insight_notes_free(incoming); insight_notes_free(destination); insight_notes_free(clips);
 g_free(source_path);g_free(other_path);g_free(external_path);
 g_remove(file); g_free(file); insight_notes_free(n); g_print("Notes checks passed\n"); return 0;
}
