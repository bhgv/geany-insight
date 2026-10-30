/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifdef HAVE_CONFIG_H
#include "config.h"
#endif
#ifdef HAVE_CLANG_CPP
#include "clang_calls.h"
extern "C" {
#include "../core/index.h"
}
#include <clang/AST/RecursiveASTVisitor.h>
#include <clang/AST/ExprCXX.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Frontend/ASTUnit.h>
#include <clang/Frontend/PCHContainerOperations.h>
#include <clang/Index/USRGeneration.h>
#include <clang/Lex/Lexer.h>
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <cstring>
#include <string>
#include <vector>
#include <set>
namespace {
class Calls : public clang::RecursiveASTVisitor<Calls> {
 clang::SourceManager &sm;
 const clang::LangOptions &language;
 const gchar *text;
 const clang::FunctionDecl *owner=nullptr;
 const gchar *path;
 gsize length;
 GCancellable *cancel;
 unsigned visited=0;
 bool valid=true;
 gchar *own(const std::string &s) {auto *p=g_strdup(s.c_str());g_ptr_array_add(strings,p);return p;}
 bool active() {return valid && !(cancel && g_cancellable_is_cancelled(cancel));}
 std::string usr(const clang::Decl *decl) {llvm::SmallString<128> value;if(clang::index::generateUSRForDecl(decl,value)) return {};return value.str().str();}
 const gchar *destination_kind(const clang::ValueDecl *decl) {
  if(auto *function=llvm::dyn_cast<clang::FunctionDecl>(decl)) return function->isThisDeclarationADefinition() ? "f":"p";
  if(llvm::isa<clang::ParmVarDecl>(decl)) return "z";
  if(llvm::isa<clang::FieldDecl>(decl)) return "m";
  if(llvm::isa<clang::EnumConstantDecl>(decl)) return "e";
  if(auto *variable=llvm::dyn_cast<clang::VarDecl>(decl)) return variable->isLocalVarDecl() ? "l":"v";
  if(llvm::isa<clang::BindingDecl>(decl)) return "l";
  return "";
 }
 void destination(const clang::ValueDecl *target) {
  if(!target || !active()) return;
  const clang::ValueDecl *definition=target;
  if(auto *function=llvm::dyn_cast<clang::FunctionDecl>(target)) {if(auto *body=function->getDefinition()) definition=body;}
  else if(auto *variable=llvm::dyn_cast<clang::VarDecl>(target)) {if(auto *body=variable->getDefinition()) definition=body;}
  auto at=definition->getLocation();
  // Preserve the established frontend fallback for generated declarations and
  // macros: their spelling location is not necessarily an editable target.
  if(at.isInvalid() || at.isMacroID()) return;
  auto file=sm.getFilename(at).str(),identity=usr(target->getCanonicalDecl()),name=target->getNameAsString();
  auto line=sm.getSpellingLineNumber(at),column=sm.getSpellingColumnNumber(at);
  if(file.empty() || identity.empty() || name.empty() || !line || !column || !g_path_is_absolute(file.c_str())) return;
  if(!destinations.insert(identity).second) return;
  if(rows->len>=50000) {valid=false;return;}
  gchar *position=g_strdup_printf("%u:%u",line,column);g_ptr_array_add(strings,position);
  InsightSymbol row={own(identity),path,own(file),"@clangtarget",line,position,own(name),destination_kind(definition)};g_array_append_val(rows,row);
 }
 void reference(const clang::ValueDecl *target,clang::SourceLocation at) {
  // Macro expansions and non-identifier/operator spellings retain the C frontend
  // result: a spelling location alone cannot describe their source occurrence.
  if(!target || !active() || at.isInvalid() || at.isMacroID() || !sm.isWrittenInMainFile(at)) return;
  const auto *identifier=target->getIdentifier();if(!identifier) return;
  auto name=identifier->getName().str(),identity=usr(target->getCanonicalDecl());
  unsigned begin=sm.getFileOffset(at),size=clang::Lexer::MeasureTokenLength(at,sm,language);
  if(identity.empty() || !size || begin>length || size>length-begin || size!=name.size() || std::memcmp(text+begin,name.data(),size)) return;
  destination(target);if(!active()) return;
  // An instantiated template can visit the same spelling more than once. Do
  // not invent multiple source occurrences for one resolved identity.
  auto key=std::make_pair(begin,identity);if(!references.insert(key).second) return;
  if(rows->len>=50000) {valid=false;return;}
  gsize first=begin,last=begin;while(first && text[first-1]!='\n' && last-first<512) first--;while(last<length && text[last]!='\n' && last-first<1024) last++;
  gchar *snippet=g_utf8_make_valid(text+first,last-first);g_ptr_array_add(strings,snippet);
  gsize line_start=begin;while(line_start && text[line_start-1]!='\n') line_start--;
  guint column=(guint)g_utf8_strlen(text+line_start,begin-line_start)+1;
  gchar *range=g_strdup_printf("%u:%u:%u",begin,begin+size,column);g_ptr_array_add(strings,range);
  InsightSymbol row={own(name),path,own(identity),"@clangref",sm.getSpellingLineNumber(at),range,snippet,""};g_array_append_val(rows,row);
 }
 void call(const clang::FunctionDecl *target,clang::SourceLocation at) {
  if(!owner || !target || !active()) return;
  at=sm.getSpellingLoc(at);if(at.isInvalid() || !sm.isWrittenInMainFile(at)) return;
  auto callee=usr(target->getCanonicalDecl()),caller=usr(owner->getCanonicalDecl());if(callee.empty() || caller.empty()) return;
  destination(target);if(!active()) return;
  auto name=target->getNameAsString();unsigned offset=sm.getFileOffset(at);if(offset>length || name.empty()) return;
  if(rows->len>=50000) {valid=false;return;}
  auto *method=llvm::dyn_cast<clang::CXXMethodDecl>(target);bool virt=method && method->isVirtual();
  const auto *definition=target->getDefinition();std::string file=definition ? sm.getFilename(sm.getSpellingLoc(definition->getLocation())).str():"";
  gchar *range=g_strdup_printf("%u:%u:%u",offset,(unsigned)MIN(length,offset+name.size()),sm.getSpellingColumnNumber(at));g_ptr_array_add(strings,range);
  InsightSymbol row={own(name),path,own(callee),virt ? "@clangvirtual":"@clangcall",sm.getSpellingLineNumber(at),range,own(caller),own(file)};g_array_append_val(rows,row);
 }
public:
 std::set<std::pair<unsigned,std::string>> references;
 std::set<std::string> destinations;
 GArray *rows=g_array_new(FALSE,FALSE,sizeof(InsightSymbol));
 GPtrArray *strings=g_ptr_array_new_with_free_func(g_free);
 Calls(clang::ASTContext &context,const gchar *p,const gchar *source,GCancellable *c):sm(context.getSourceManager()),language(context.getLangOpts()),text(source),path(p),length(std::strlen(source)),cancel(c) {}
 ~Calls() {g_array_unref(rows);g_ptr_array_unref(strings);}
 bool TraverseDecl(clang::Decl *decl) {
  if(!decl) return true;if(!active()) return false;
  if(!llvm::isa<clang::TranslationUnitDecl>(decl) && decl->getLocation().isValid() && !sm.isWrittenInMainFile(sm.getSpellingLoc(decl->getLocation()))) return true;
  auto *previous=owner;if(auto *function=llvm::dyn_cast<clang::FunctionDecl>(decl)) owner=function->doesThisDeclarationHaveABody() ? function:nullptr;
  bool ok=clang::RecursiveASTVisitor<Calls>::TraverseDecl(decl);owner=previous;return ok;
 }
 bool TraverseLambdaExpr(clang::LambdaExpr *expr) {
  if(!VisitStmt(expr)) return false;
  // Capture initializers execute in the enclosing function. The deferred body
  // does not: keep its references without inventing an enclosing call edge.
  for(auto *init:expr->capture_inits()) if(init && !TraverseStmt(init)) return false;
  auto *previous=owner;owner=nullptr;bool ok=TraverseStmt(expr->getBody());owner=previous;return ok;
 }
 bool VisitStmt(clang::Stmt *) {if(++visited>200000) valid=false;return active();}
 bool VisitDeclRefExpr(clang::DeclRefExpr *expr) {reference(expr->getDecl(),expr->getLocation());return active();}
 bool VisitMemberExpr(clang::MemberExpr *expr) {reference(expr->getMemberDecl(),expr->getMemberLoc());return active();}
 bool VisitCallExpr(clang::CallExpr *expr) {
  auto *callee=expr->getCallee()->IgnoreParenImpCasts();auto loc=callee->getExprLoc();if(auto *member=llvm::dyn_cast<clang::MemberExpr>(callee)) loc=member->getMemberLoc();
  call(expr->getDirectCallee(),loc);return active();
 }
 bool VisitCXXConstructExpr(clang::CXXConstructExpr *expr) {call(expr->getConstructor(),expr->getLocation());return active();}
};
}
extern "C" gboolean insight_clang_cpp_calls(const gchar *path,const gchar *text,const gchar *const *args,guint argc,GHashTable *overlays,GArray *rows,GPtrArray *owned,GCancellable *cancel) {
 if(cancel && g_cancellable_is_cancelled(cancel)) return FALSE;
 std::vector<std::string> flags;for(guint i=0;i<argc;i++) flags.emplace_back(args[i]);
 clang::tooling::FileContentMappings files;gsize bytes=0;if(overlays) {GHashTableIter it;gpointer key,value;g_hash_table_iter_init(&it,overlays);while(g_hash_table_iter_next(&it,&key,&value)) if(value && std::strcmp(static_cast<const char *>(key),path) && files.size()<256 && bytes+std::strlen(static_cast<const char *>(value))<=32u*1024u*1024u) {files.emplace_back(static_cast<const char *>(key),static_cast<const char *>(value));bytes+=std::strlen(static_cast<const char *>(value));}}
 clang::IgnoringDiagConsumer diagnostics;
 auto ast=clang::tooling::buildASTFromCodeWithArgs(text,flags,path,"clang-tool",std::make_shared<clang::PCHContainerOperations>(),clang::tooling::getClangStripDependencyFileAdjuster(),files,&diagnostics);
 if(!ast || ast->getDiagnostics().hasErrorOccurred() || (cancel && g_cancellable_is_cancelled(cancel))) return FALSE;
 Calls calls(ast->getASTContext(),path,text,cancel);if(!calls.TraverseDecl(ast->getASTContext().getTranslationUnitDecl()) || rows->len+calls.rows->len>100000 || (cancel && g_cancellable_is_cancelled(cancel))) return FALSE;
 for(guint i=rows->len;i>0;i--) {auto &row=g_array_index(rows,InsightSymbol,i-1);bool replaced_reference=false;
  if(!std::strcmp(row.kind,"@clangref")) {char *end=nullptr;auto offset=g_ascii_strtoull(row.signature,&end,10);if(end && *end==':' && offset<=G_MAXUINT) replaced_reference=calls.references.count(std::make_pair((unsigned)offset,std::string(row.scope)))!=0;}
  if(replaced_reference || (!std::strcmp(row.kind,"@clangtarget") && calls.destinations.count(row.name)) || !std::strcmp(row.kind,"@clangcall") || !std::strcmp(row.kind,"@clangvirtual")) g_array_remove_index(rows,i-1);}
 g_array_append_vals(rows,calls.rows->data,calls.rows->len);for(guint i=0;i<calls.strings->len;i++) g_ptr_array_add(owned,g_ptr_array_index(calls.strings,i));g_ptr_array_set_free_func(calls.strings,nullptr);return TRUE;
}
#endif
