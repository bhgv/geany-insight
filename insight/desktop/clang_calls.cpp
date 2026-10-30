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
#include <clang/Tooling/Tooling.h>
#include <llvm/ADT/SmallString.h>
#include <cstring>
#include <string>
#include <vector>
namespace {
class Calls : public clang::RecursiveASTVisitor<Calls> {
 clang::SourceManager &sm;
 const clang::FunctionDecl *owner=nullptr;
 const gchar *path;
 gsize length;
 GCancellable *cancel;
 unsigned visited=0;
 bool valid=true;
 gchar *own(const std::string &s) {auto *p=g_strdup(s.c_str());g_ptr_array_add(strings,p);return p;}
 bool active() {return valid && !(cancel && g_cancellable_is_cancelled(cancel));}
 std::string usr(const clang::Decl *decl) {llvm::SmallString<128> value;if(clang::index::generateUSRForDecl(decl,value)) return {};return value.str().str();}
 void call(const clang::FunctionDecl *target,clang::SourceLocation at) {
  if(!owner || !target || !active()) return;
  at=sm.getSpellingLoc(at);if(at.isInvalid() || !sm.isWrittenInMainFile(at)) return;
  auto callee=usr(target->getCanonicalDecl()),caller=usr(owner->getCanonicalDecl());if(callee.empty() || caller.empty()) return;
  auto name=target->getNameAsString();unsigned offset=sm.getFileOffset(at);if(offset>length || name.empty()) return;
  if(rows->len>=50000) {valid=false;return;}
  auto *method=llvm::dyn_cast<clang::CXXMethodDecl>(target);bool virt=method && method->isVirtual();
  const auto *definition=target->getDefinition();std::string file=definition ? sm.getFilename(sm.getSpellingLoc(definition->getLocation())).str():"";
  gchar *range=g_strdup_printf("%u:%u:%u",offset,(unsigned)MIN(length,offset+name.size()),sm.getSpellingColumnNumber(at));g_ptr_array_add(strings,range);
  InsightSymbol row={own(name),path,own(callee),virt ? "@clangvirtual":"@clangcall",sm.getSpellingLineNumber(at),range,own(caller),own(file)};g_array_append_val(rows,row);
 }
public:
 GArray *rows=g_array_new(FALSE,FALSE,sizeof(InsightSymbol));
 GPtrArray *strings=g_ptr_array_new_with_free_func(g_free);
 Calls(clang::ASTContext &context,const gchar *p,gsize n,GCancellable *c):sm(context.getSourceManager()),path(p),length(n),cancel(c) {}
 ~Calls() {g_array_unref(rows);g_ptr_array_unref(strings);}
 bool TraverseDecl(clang::Decl *decl) {
  if(!decl) return true;if(!active()) return false;
  if(!llvm::isa<clang::TranslationUnitDecl>(decl) && decl->getLocation().isValid() && !sm.isWrittenInMainFile(sm.getSpellingLoc(decl->getLocation()))) return true;
  auto *previous=owner;if(auto *function=llvm::dyn_cast<clang::FunctionDecl>(decl)) owner=function->doesThisDeclarationHaveABody() ? function:nullptr;
  bool ok=clang::RecursiveASTVisitor<Calls>::TraverseDecl(decl);owner=previous;return ok;
 }
 bool TraverseLambdaExpr(clang::LambdaExpr *) {return true;} // no invented enclosing-function edge
 bool VisitStmt(clang::Stmt *) {if(++visited>200000) valid=false;return active();}
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
 Calls calls(ast->getASTContext(),path,std::strlen(text),cancel);if(!calls.TraverseDecl(ast->getASTContext().getTranslationUnitDecl()) || rows->len+calls.rows->len>100000) return FALSE;
 for(guint i=rows->len;i>0;i--) {auto &row=g_array_index(rows,InsightSymbol,i-1);if(!std::strcmp(row.kind,"@clangcall") || !std::strcmp(row.kind,"@clangvirtual")) g_array_remove_index(rows,i-1);}
 g_array_append_vals(rows,calls.rows->data,calls.rows->len);for(guint i=0;i<calls.strings->len;i++) g_ptr_array_add(owned,g_ptr_array_index(calls.strings,i));g_ptr_array_set_free_func(calls.strings,nullptr);return TRUE;
}
#endif
