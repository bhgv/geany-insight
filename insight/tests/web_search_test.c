/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "web_search.h"
#include <string.h>
static void bad(const gchar *pattern,const gchar *term) { GError *error=NULL;gchar *url=insight_web_search_url(pattern,term,&error);g_assert_null(url);g_assert_nonnull(error);g_clear_error(&error); }
int main(void) {
 GError *error=NULL;gchar *url=insight_web_search_url("https://example.com/search?q=%S&lang=en","α &+#/%?='\"",&error);g_assert_no_error(error);g_assert_cmpstr(url,==,"https://example.com/search?q=%CE%B1%20%26%2B%23%2F%25%3F%3D%27%22&lang=en");g_free(url);
 url=insight_web_search_url("http://localhost:8080/docs/%S?fixed=%20","x y",&error);g_assert_no_error(error);g_assert_cmpstr(url,==,"http://localhost:8080/docs/x%20y?fixed=%20");g_free(url);
 const gchar *invalid[]={"file:///tmp/%S","javascript:alert('%S')","https://example.com/no-query","https://example.com/?a=%S&b=%S","https://user:password@example.com/?q=%S","https://%S.example.com/","https://example.com:%S/","https://example.com:bad/?q=%S","https:///?q=%S","https://example.com/?q=%S%","https://example.com/?q=%S%2x","https://example.com/a b?q=%S","https://example.com/\n?q=%S",NULL};
 for(guint i=0;invalid[i];i++) bad(invalid[i],"foo");bad(NULL,"foo");bad("https://example.com/?q=%S",NULL);bad("https://example.com/?q=%S","");bad("https://example.com/?q=%S","a\nb");bad("https://example.com/?q=%S","a\tb");bad("https://example.com/?q=%S","\xff");
 gchar *large=g_strnfill(4097,'x');bad("https://example.com/?q=%S",large);g_free(large);g_print("Web search URL: escaped Unicode/query delimiters, literal escapes, HTTP(S), authority, placeholder, malformed templates and input limits passed\n");return 0;
}
