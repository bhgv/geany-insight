/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "../core/structure_select.h"
#include "../desktop/source_mask.h"
#include <string.h>
static void range(const gchar *text,const gchar *name,const gchar *keyword,InsightCodeDialect dialect,gboolean found) {
 GError *error=NULL;gchar *mask=insight_source_code_mask(text,strlen(text),dialect,&error);g_assert_no_error(error);g_assert_nonnull(mask);
 size_t body=0,last=0;int result=insight_nominal_range(text,mask,strlen(text),0,name,keyword,dialect==INSIGHT_CODE_PYTHON ? INSIGHT_FUNCTION_PYTHON : INSIGHT_FUNCTION_BRACES,&body,&last);
 g_assert_cmpint(result,==,found ? 1 : 0);
 if(found) { g_assert_cmpuint(body,>,0);g_assert_cmpuint(last,>,body);if(dialect!=INSIGHT_CODE_PYTHON) g_assert_cmpuint(last,==,(size_t)(strrchr(text,'}')-text+1)); }
 g_free(mask);
}
int main(void) {
 range("struct Point {\n const char *s = \"} ; {\"; // }\n /* { */ int x;\n};\n","Point","struct",INSIGHT_CODE_C,TRUE);
 range("enum class Kind : unsigned {\r\n A, B\r\n};\r\n","Kind","enum",INSIGHT_CODE_C,TRUE);
 range("struct Проверка { int число; };","Проверка","struct",INSIGHT_CODE_C,TRUE);
 range("class Класс:\n    pass\n","Класс","class",INSIGHT_CODE_PYTHON,TRUE);
 range("union Storage { int x; double y; };","Storage","union",INSIGHT_CODE_C,TRUE);
 range("inline namespace Outer::Inner { struct Item {}; }","Inner","namespace",INSIGHT_CODE_C,TRUE);
 range("public interface Service extends Base { void work(); }","Service","interface",INSIGHT_CODE_C,TRUE);
 range("template<class T> class Box final : public Base<T> {\n struct Nested {};\n};","Box","class",INSIGHT_CODE_C,TRUE);
 range("class Child : Base<decltype([] { return 1; })> { int x; };","Child","class",INSIGHT_CODE_C,TRUE);
 range("[[deprecated]] struct Old { int n; };","Old","struct",INSIGHT_CODE_C,TRUE);
 range("export default class View extends factory({x: 1}) { method() {} }","View","class",INSIGHT_CODE_JS,TRUE);
 range("namespace Alias = Real;","Alias","namespace",INSIGHT_CODE_C,FALSE);
 range("class Forward; struct Other {};","Forward","class",INSIGHT_CODE_C,FALSE);
 range("struct Object value = { 1 };","Object","struct",INSIGHT_CODE_C,FALSE);
 range("struct First {}; struct Second {};","Second","struct",INSIGHT_CODE_C,FALSE);
 range("void first() {} struct Second {};","Second","struct",INSIGHT_CODE_C,FALSE);
 range("template<class T> class Box {};","T","class",INSIGHT_CODE_C,FALSE);
 range("struct Name { int x;","Name","struct",INSIGHT_CODE_C,FALSE);
 range("\nstruct Name {};","Name","struct",INSIGHT_CODE_C,FALSE);
 range("struct Name {};","Other","struct",INSIGHT_CODE_C,FALSE);
 range("struct Name {};","Name","class",INSIGHT_CODE_C,FALSE);
 range("class [[attribute]] Name {};","Name","class",INSIGHT_CODE_C,FALSE);
 range("class Klass:\n    \"\"\"doc { }\"\"\"\n    def method(self):\n        return 1\n\nnext_value = 2\n","Klass","class",INSIGHT_CODE_PYTHON,TRUE);
 range("class Klass(\n    Base,\n):\n    pass\n","Klass","class",INSIGHT_CODE_PYTHON,TRUE);
 range("class Inline: pass\n","Inline","class",INSIGHT_CODE_PYTHON,TRUE);
 range("class Missing:\npass\n","Missing","class",INSIGHT_CODE_PYTHON,FALSE);
 range("def Klass():\n    pass\n","Klass","class",INSIGHT_CODE_PYTHON,FALSE);
 range("class Klass:\n    pass\n","Other","class",INSIGHT_CODE_PYTHON,FALSE);
 gchar *padding=g_strnfill(17000,' '),*large=g_strconcat("struct Long",padding,"{};",NULL);range(large,"Long","struct",INSIGHT_CODE_C,FALSE);g_free(large);g_free(padding);
 GString *deep=g_string_new("class Deep : Base");for(guint i=0;i<65;i++) g_string_append_c(deep,'<');g_string_append(deep,"T");for(guint i=0;i<65;i++) g_string_append_c(deep,'>');g_string_append(deep," {}; ");range(deep->str,"Deep","class",INSIGHT_CODE_C,FALSE);g_string_free(deep,TRUE);
 size_t body=0,last=0;g_assert_cmpint(insight_nominal_range(NULL,"",0,0,"X","class",INSIGHT_FUNCTION_BRACES,&body,&last),==,-1);
 g_print("Nominal definition ranges: kinds, languages, comments/strings, templates, guards and bounds passed\n");return 0;
}
