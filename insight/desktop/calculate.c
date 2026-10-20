/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "calculate.h"
#include <errno.h>
#include <math.h>
#include <string.h>
typedef struct { const gchar *text,*cursor,*message;guint depth,nodes,error_offset; } Parser;
static gdouble sum(Parser *p);
static gdouble unary(Parser *p);
static void whitespace(Parser *p) { while(g_ascii_isspace(*p->cursor)) p->cursor++; }
static gdouble fail(Parser *p,const gchar *message) { if(!p->message) { p->message=message;p->error_offset=p->cursor-p->text; }return 0; }
static gboolean budget(Parser *p) { if(++p->nodes>2048) { fail(p,"Calculation exceeds 2048 values/operators");return FALSE; }return TRUE; }
static gdouble finite_value(Parser *p,gdouble value) { return isfinite(value) ? value : fail(p,"Calculation produced a non-finite result"); }
static gdouble primary(Parser *p) {
 whitespace(p);if(p->message) return 0;
 if(!budget(p)) return 0;
 if(*p->cursor=='(') { p->cursor++;gdouble value=sum(p);whitespace(p);if(*p->cursor!=')') return fail(p,"Expected a closing parenthesis");p->cursor++;return value; }
 if(g_ascii_isdigit(*p->cursor) || (*p->cursor=='.' && g_ascii_isdigit(p->cursor[1]))) {
  const gchar *start=p->cursor;while(g_ascii_isdigit(*p->cursor)) p->cursor++;
  if(*p->cursor=='.') { p->cursor++;while(g_ascii_isdigit(*p->cursor)) p->cursor++; }
  if(*p->cursor=='e' || *p->cursor=='E') { p->cursor++;if(*p->cursor=='+' || *p->cursor=='-') p->cursor++;if(!g_ascii_isdigit(*p->cursor)) return fail(p,"Expected exponent digits");while(g_ascii_isdigit(*p->cursor)) p->cursor++; }
  gchar *token=g_strndup(start,p->cursor-start);errno=0;gdouble value=g_ascii_strtod(token,NULL);g_free(token);
  if(errno==ERANGE) return fail(p,"Number is outside the supported floating-point range");
  return finite_value(p,value);
 }
 if(g_ascii_isalpha(*p->cursor)) {
  const gchar *start=p->cursor;while(g_ascii_isalpha(*p->cursor)) p->cursor++;
  gchar *name=g_strndup(start,p->cursor-start);gint function=-1,arity=1;
  const gchar *names[]={"abs","sqrt","floor","ceil","round","sin","cos","tan","log","exp","min","max","pow"};
  if(!strcmp(name,"pi") || !strcmp(name,"e")) { gdouble value=!strcmp(name,"pi") ? G_PI : 2.71828182845904523536;g_free(name);return value; }
  for(guint i=0;i<G_N_ELEMENTS(names);i++) if(!strcmp(name,names[i])) function=i;
  g_free(name);if(function<0) return fail(p,"Unknown function or constant");if(function>=10) arity=2;
  whitespace(p);if(*p->cursor!='(') return fail(p,"Expected an opening parenthesis");p->cursor++;gdouble a=sum(p),b=0;whitespace(p);
  if(arity==2) { if(*p->cursor!=',') return fail(p,"Expected two function arguments separated by a comma");p->cursor++;b=sum(p);whitespace(p); }
  if(*p->cursor!=')') return fail(p,"Expected a closing parenthesis");
  p->cursor++;if(p->message) return 0;
  errno=0;gdouble value=0;
  switch(function) {
   case 0:value=fabs(a);break;case 1:value=sqrt(a);break;case 2:value=floor(a);break;case 3:value=ceil(a);break;case 4:value=round(a);break;
   case 5:value=sin(a);break;case 6:value=cos(a);break;case 7:value=tan(a);break;case 8:value=log(a);break;case 9:value=exp(a);break;
   case 10:value=fmin(a,b);break;case 11:value=fmax(a,b);break;case 12:value=pow(a,b);break;
  }
  if(errno==EDOM || errno==ERANGE) return fail(p,"Function domain or range error");
  return finite_value(p,value);
 }
 return fail(p,"Expected a number, function or parenthesis");
}
static gdouble power(Parser *p) {
 gdouble value=primary(p);whitespace(p);
 if(!p->message && *p->cursor=='^') { if(!budget(p)) return 0;p->cursor++;gdouble exponent=unary(p);if(p->message) return 0;errno=0;value=pow(value,exponent);if(errno==EDOM || errno==ERANGE) return fail(p,"Power domain or range error"); }
 return finite_value(p,value);
}
static gdouble unary(Parser *p) {
 if(p->message) return 0;
 if(++p->depth>65) { p->depth--;return fail(p,"Calculation exceeds 64 nesting levels"); }
 whitespace(p);gdouble value;
 if(*p->cursor=='+' || *p->cursor=='-') { if(!budget(p)) { p->depth--;return 0; }gchar sign=*p->cursor++;value=unary(p);if(sign=='-') value=-value; }else value=power(p);
 p->depth--;return value;
}
static gdouble product(Parser *p) {
 gdouble value=unary(p);whitespace(p);
 while(!p->message && (*p->cursor=='*' || *p->cursor=='/' || *p->cursor=='%')) {
  if(!budget(p)) return 0;
  gchar op=*p->cursor++;gdouble right=unary(p);if(p->message) return 0;
  if((op=='/' || op=='%') && right==0) return fail(p,"Division or remainder by zero");
  value=finite_value(p,op=='*' ? value*right : op=='/' ? value/right : fmod(value,right));whitespace(p);
 }
 return value;
}
static gdouble sum(Parser *p) {
 gdouble value=product(p);whitespace(p);
 while(!p->message && (*p->cursor=='+' || *p->cursor=='-')) {
  if(!budget(p)) return 0;
  gchar op=*p->cursor++;gdouble right=product(p);value=finite_value(p,op=='+' ? value+right : value-right);whitespace(p);
 }
 return value;
}
gchar *insight_calculate(const gchar *expression,guint *byte_offset,GError **error) {
 if(byte_offset) *byte_offset=0;
 if(!expression || strlen(expression)>4096 || !g_utf8_validate(expression,-1,NULL)) { g_set_error_literal(error,G_OPTION_ERROR,G_OPTION_ERROR_BAD_VALUE,"Calculation requires UTF-8 up to 4096 bytes");return NULL; }
 Parser p={expression,expression,NULL,0,0,0};gdouble value=sum(&p);whitespace(&p);if(!p.message && *p.cursor) fail(&p,"Unexpected text after the expression");
 if(p.message) { if(byte_offset) *byte_offset=p.error_offset;g_set_error_literal(error,G_OPTION_ERROR,G_OPTION_ERROR_BAD_VALUE,p.message);return NULL; }
 gchar buffer[G_ASCII_DTOSTR_BUF_SIZE];g_ascii_dtostr(buffer,sizeof(buffer),value==0 ? 0 : value);return g_strdup(buffer);
}
