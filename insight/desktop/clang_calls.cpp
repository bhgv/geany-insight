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
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/PreprocessingRecord.h>
#include <clang/Sema/CodeCompleteConsumer.h>
#include <clang/Tooling/JSONCompilationDatabase.h>
#include <llvm/Support/VirtualFileSystem.h>
#include "glib_compat.h"
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
#include <map>
#include <tuple>
namespace {
class Calls : public clang::RecursiveASTVisitor<Calls> {
 clang::SourceManager &sm;
 const clang::LangOptions &language;
 const gchar *text;
 InsightClangResult *result=nullptr;
 const clang::FunctionDecl *owner=nullptr;
 const gchar *path;
 gsize length;
 GCancellable *cancel;
 unsigned visited=0;
 bool valid=true;
 std::map<const clang::Decl *,std::string> identities;
 std::map<std::string,const InsightSymbol *> old_definitions;
 std::set<std::tuple<std::string,std::string,unsigned>> callsites;
 gchar *own(const std::string &s) {auto *p=g_strdup(s.c_str());g_ptr_array_add(strings,p);return p;}
 bool active() {return valid && !(cancel && g_cancellable_is_cancelled(cancel));}
 std::string usr(const clang::Decl *decl) {
  if(!decl) return {};decl=decl->getCanonicalDecl();auto cached=identities.find(decl);if(cached!=identities.end()) return cached->second;
  llvm::SmallString<128> value;std::string result;if(!clang::index::generateUSRForDecl(decl,value)) result=value.str().str();identities.emplace(decl,result);return result;
 }
 const gchar *destination_kind(const clang::NamedDecl *decl) {
  if(auto *function=llvm::dyn_cast<clang::FunctionDecl>(decl)) return function->isThisDeclarationADefinition() ? "f":"p";
  if(llvm::isa<clang::ParmVarDecl>(decl)) return "z";
  if(llvm::isa<clang::FieldDecl>(decl)) return "m";
  if(llvm::isa<clang::EnumConstantDecl>(decl)) return "e";
  if(auto *variable=llvm::dyn_cast<clang::VarDecl>(decl)) return variable->isLocalVarDecl() ? "l":"v";
  if(llvm::isa<clang::BindingDecl>(decl)) return "l";
  if(auto *tag=llvm::dyn_cast<clang::TagDecl>(decl)) return tag->isEnum() ? "g":tag->isUnion() ? "u":tag->isStruct() ? "s":"c";
  if(llvm::isa<clang::TypedefNameDecl>(decl) || llvm::isa<clang::TypeAliasTemplateDecl>(decl) || llvm::isa<clang::TemplateTypeParmDecl>(decl)) return "t";
  if(llvm::isa<clang::ClassTemplateDecl>(decl)) return "c";
  if(llvm::isa<clang::NamespaceDecl>(decl) || llvm::isa<clang::NamespaceAliasDecl>(decl)) return "n";
  return "";
 }
 void destination(const clang::NamedDecl *target) {
  if(!target || !active()) return;
  const clang::NamedDecl *definition=target;
  if(auto *function=llvm::dyn_cast<clang::FunctionDecl>(target)) {if(auto *body=function->getDefinition()) definition=body;}
  else if(auto *variable=llvm::dyn_cast<clang::VarDecl>(target)) {if(auto *body=variable->getDefinition()) definition=body;}
  else if(auto *templ=llvm::dyn_cast<clang::ClassTemplateDecl>(target)) {if(auto *body=templ->getTemplatedDecl()->getDefinition()) definition=body;}
  else if(auto *tag=llvm::dyn_cast<clang::TagDecl>(target)) {if(auto *body=tag->getDefinition()) definition=body;}
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
  InsightSymbol row={own(identity),path,own(file),"@clangtarget",line,position,own(name),llvm::isa<clang::ClassTemplateDecl>(target) ? "c":destination_kind(definition)};g_array_append_val(rows,row);
 }
 InsightClangRole semantic_role(const clang::NamedDecl *decl) {
  const auto *kind=destination_kind(decl);if(strchr("sucgtn",*kind) && *kind) return *kind=='n' ? INSIGHT_CLANG_NAMESPACE:INSIGHT_CLANG_TYPE;
  if(*kind=='f' || *kind=='p') return INSIGHT_CLANG_FUNCTION;if(*kind=='m') return INSIGHT_CLANG_MEMBER;if(*kind=='e') return INSIGHT_CLANG_CONSTANT;return INSIGHT_CLANG_VARIABLE;
 }
 void token(const clang::NamedDecl *decl,clang::SourceLocation at) {
  if(!result || !decl || at.isInvalid() || at.isMacroID() || !sm.isWrittenInMainFile(at) || !decl->getIdentifier()) return;
  auto name=decl->getNameAsString();unsigned begin=sm.getFileOffset(at),size=clang::Lexer::MeasureTokenLength(at,sm,language);
  if(!size || begin>length || size>length-begin || size!=name.size() || memcmp(text+begin,name.data(),size)) return;
  if(result->tokens->len>=200000) {result->limited=TRUE;return;}InsightClangToken item={begin,begin+size,semantic_role(decl)};g_array_append_val(result->tokens,item);
 }
 std::string scope(const clang::NamedDecl *decl) {
  std::vector<std::string> parts;auto *context=decl->getDeclContext();for(unsigned i=0;context && !context->isTranslationUnit() && i<32;i++,context=context->getParent()) if(auto *named=llvm::dyn_cast<clang::NamedDecl>(context)) {auto name=named->getNameAsString();if(!name.empty()) parts.push_back(name);}
  std::string value;for(auto it=parts.rbegin();it!=parts.rend();++it) {if(!value.empty()) value+="::";value+=*it;}return value;
 }
 void symbol(const clang::NamedDecl *decl) {
  if(!result || decl->isImplicit() || llvm::isa<clang::TemplateDecl>(decl) || llvm::isa<clang::TemplateTypeParmDecl>(decl)) return;
  auto at=decl->getLocation();if(at.isInvalid() || at.isMacroID() || !sm.isWrittenInMainFile(at)) return;
  const gchar *kind=destination_kind(decl);if(!*kind) return;auto name=decl->getNameAsString();if(name.empty()) return;
  if(symbols->len>=50000) {valid=false;result->limited=TRUE;return;}
  std::string signature,type,bases;auto *function=llvm::dyn_cast<clang::FunctionDecl>(decl);
  if(function) {if(function->getType().isNull()) return;signature=parameters(function);type=function->getReturnType().getAsString();}
  else if(auto *alias=llvm::dyn_cast<clang::TypedefNameDecl>(decl)) type=alias->getUnderlyingType().getAsString();
  else if(auto *value=llvm::dyn_cast<clang::ValueDecl>(decl)) {if(!value->getType().isNull()) type=value->getType().getAsString();}
  if(auto *record=llvm::dyn_cast<clang::CXXRecordDecl>(decl)) {if(record->getDescribedClassTemplate()) kind="c";if(record->isCompleteDefinition()) for(const auto &base:record->bases()) {if(!bases.empty()) bases+=",";bases+=base.getType().getAsString();}}
  InsightSymbol row={own(name),path,own(scope(decl)),kind,sm.getSpellingLineNumber(at),own(signature),own(type),own(bases)};g_array_append_val(symbols,row);
  guint flags=(!decl->isExternallyVisible() || *kind=='l' || *kind=='z') ? INSIGHT_CLANG_INTERNAL:0;
  if(decl->getAccess()==clang::AS_public) flags|=INSIGHT_CLANG_PUBLIC;else if(decl->getAccess()==clang::AS_protected) flags|=INSIGHT_CLANG_PROTECTED;else if(decl->getAccess()==clang::AS_private) flags|=INSIGHT_CLANG_PRIVATE;
  if(auto *method=llvm::dyn_cast<clang::CXXMethodDecl>(decl)) if(method->isVirtual()) flags|=INSIGHT_CLANG_VIRTUAL;
  if(auto *variable=llvm::dyn_cast<clang::VarDecl>(decl)) if(variable->hasExternalStorage() && !variable->isThisDeclarationADefinition()) flags|=INSIGHT_CLANG_EXTERN;
  g_hash_table_replace(result->attributes,insight_clang_attribute_key(&row),GUINT_TO_POINTER(flags));token(decl,at);
  bool defined=!function;if(auto *variable=llvm::dyn_cast<clang::VarDecl>(decl)) defined=variable->isThisDeclarationADefinition()!=clang::VarDecl::DeclarationOnly;else if(auto *tag=llvm::dyn_cast<clang::TagDecl>(decl)) defined=tag->isThisDeclarationADefinition();
  if(defined) {auto identity=usr(decl);if(!identity.empty()) {InsightSymbol definition={own(identity),path,flags&INSIGHT_CLANG_INTERNAL ? "internal":"","@clangdef",row.line,own(name),own(type),""};g_array_append_val(rows,definition);}}
 }
 void reference(const clang::NamedDecl *target,clang::SourceLocation at) {
  // Macro expansions and non-identifier/operator spellings retain the C frontend
  // result: a spelling location alone cannot describe their source occurrence.
  if(!target || !active() || at.isInvalid() || at.isMacroID() || at==target->getLocation() || !sm.isWrittenInMainFile(at)) return;
  const auto *identifier=target->getIdentifier();if(!identifier) return;
  auto name=identifier->getName().str(),identity=usr(target->getCanonicalDecl());
  unsigned begin=sm.getFileOffset(at),size=clang::Lexer::MeasureTokenLength(at,sm,language);
  if(identity.empty() || !size || begin>length || size>length-begin || size!=name.size() || std::memcmp(text+begin,name.data(),size)) return;
  destination(target);if(!active()) return;token(target,at);
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
 std::string parameters(const clang::FunctionDecl *function) {
  std::string result="(";bool first=true;
  for(auto *param:function->parameters()) {
   if(!first) result+=", ";first=false;auto range=clang::CharSourceRange::getTokenRange(param->getSourceRange());bool invalid=false;
   auto raw=clang::Lexer::getSourceText(range,sm,language,&invalid);
   if(!invalid && !raw.empty() && raw.size()<=4096 && raw.find('\n')==llvm::StringRef::npos && raw.find('\r')==llvm::StringRef::npos) result+=raw.str();
   else {result+=param->getType().getAsString();if(param->getIdentifier()) result+=" "+param->getNameAsString();}
   if(result.size()>16384) return {};
  }
  if(function->isVariadic()) {if(!first) result+=", ";result+="...";}
  if(first && !language.CPlusPlus && function->hasPrototype()) result+="void";
  result+=")";if(auto *method=llvm::dyn_cast<clang::CXXMethodDecl>(function)) if(method->isConst()) result+=" const";
  return result;
 }
 void definition(const clang::FunctionDecl *function) {
  if(!active() || function->isImplicit() || !function->doesThisDeclarationHaveABody()) return;
  auto at=function->getLocation();if(at.isInvalid() || at.isMacroID() || !sm.isWrittenInMainFile(at)) return;
  auto identity=usr(function),name=function->getNameAsString(),signature=parameters(function);
  auto old=old_definitions.find(identity);if(old!=old_definitions.end()) {signature=old->second->type;name=old->second->signature;} // retain native presentation/signature matching
  if(identity.empty() || name.empty() || signature.empty() || !definitions.insert(identity).second) return;
  if(rows->len>=50000) {valid=false;return;}
  InsightSymbol row={own(identity),path,!function->isExternallyVisible() ? "internal":"","@clangdef",sm.getSpellingLineNumber(at),own(name),own(signature),""};g_array_append_val(rows,row);
  auto *body=llvm::dyn_cast_or_null<clang::CompoundStmt>(function->getBody());if(!body) return;
  auto begin=body->getBeginLoc(),end=clang::Lexer::getLocForEndOfToken(body->getEndLoc(),0,sm,language);
  if(begin.isInvalid() || end.isInvalid() || begin.isMacroID() || end.isMacroID() || !sm.isWrittenInMainFile(begin) || !sm.isWrittenInMainFile(end) || sm.getFileOffset(end)>length) return;
  if(rows->len>=50000) {valid=false;return;}
  gchar *span=g_strdup_printf("%u:%u:%u:%u",sm.getSpellingLineNumber(begin),sm.getSpellingColumnNumber(begin),sm.getSpellingLineNumber(end),sm.getSpellingColumnNumber(end));g_ptr_array_add(strings,span);
  InsightSymbol extent={own(identity),path,"","@clangbody",row.line,span,own(name),""};g_array_append_val(rows,extent);bodies.insert(identity);
 }
 template<typename F> bool without_calls(F traverse) {auto *previous=owner;owner=nullptr;bool ok=traverse();owner=previous;return ok;}
 void call(const clang::FunctionDecl *target,clang::SourceLocation at) {
  if(!target || !active()) return;
  at=sm.getSpellingLoc(at);if(at.isInvalid() || !sm.isWrittenInMainFile(at)) return;
  auto callee=usr(target->getCanonicalDecl()),caller=owner ? usr(owner->getCanonicalDecl()):std::string();if(callee.empty()) return;
  destination(target);if(!active()) return;
  auto name=target->getNameAsString();unsigned offset=sm.getFileOffset(at);if(offset>length || name.empty()) return;
  // A VLA bound or instantiated expression can be traversed repeatedly. Graph
  // occurrences describe source sites, not the number of AST paths to a site.
  if(!callsites.emplace(caller,callee,offset).second) return;
  if(result) {gchar *position=g_strdup_printf("%u:%u:%u",offset,(unsigned)MIN(length,offset+name.size()),sm.getSpellingColumnNumber(at));g_ptr_array_add(strings,position);InsightSymbol occurrence={own(name),path,own(callee),"@call",sm.getSpellingLineNumber(at),position,"",""};g_array_append_val(rows,occurrence);}
  if(!owner) return;
  if(rows->len>=50000) {valid=false;return;}
  auto *method=llvm::dyn_cast<clang::CXXMethodDecl>(target);bool virt=method && method->isVirtual();
  const auto *definition=target->getDefinition();std::string file=definition ? sm.getFilename(sm.getSpellingLoc(definition->getLocation())).str():"";
  gchar *range=g_strdup_printf("%u:%u:%u",offset,(unsigned)MIN(length,offset+name.size()),sm.getSpellingColumnNumber(at));g_ptr_array_add(strings,range);
  InsightSymbol row={own(name),path,own(callee),virt ? "@clangvirtual":"@clangcall",sm.getSpellingLineNumber(at),range,own(caller),own(file)};g_array_append_val(rows,row);
 }
public:
 std::set<std::pair<unsigned,std::string>> references;
 std::set<std::string> destinations,definitions,bodies;
 GArray *symbols=g_array_new(FALSE,FALSE,sizeof(InsightSymbol));
 GArray *rows=g_array_new(FALSE,FALSE,sizeof(InsightSymbol));
 GPtrArray *strings=g_ptr_array_new_with_free_func(g_free);
 Calls(clang::ASTContext &context,const gchar *p,const gchar *source,GCancellable *c,GArray *previous,InsightClangResult *output=nullptr):sm(context.getSourceManager()),language(context.getLangOpts()),text(source),result(output),path(p),length(std::strlen(source)),cancel(c) {
  for(guint i=0;i<previous->len;i++) {const auto &row=g_array_index(previous,InsightSymbol,i);if(!std::strcmp(row.kind,"@clangdef")) old_definitions.emplace(row.name,&row);}
 }
 ~Calls() {g_array_unref(symbols);g_array_unref(rows);g_ptr_array_unref(strings);}
 bool TraverseDecl(clang::Decl *decl) {
  if(!decl) return true;if(!active()) return false;
  if(!llvm::isa<clang::TranslationUnitDecl>(decl) && decl->getLocation().isValid() && !sm.isWrittenInMainFile(sm.getSpellingLoc(decl->getLocation()))) return true;
  auto *previous=owner;if(auto *function=llvm::dyn_cast<clang::FunctionDecl>(decl)) owner=function->doesThisDeclarationHaveABody() ? function:nullptr;
  if(language.CPlusPlus && llvm::isa<clang::ParmVarDecl>(decl)) owner=nullptr; // declaration defaults are not calls by this function
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
 bool VisitNamedDecl(clang::NamedDecl *decl) {symbol(decl);return active();}
 bool VisitFunctionDecl(clang::FunctionDecl *decl) {definition(decl);return active();}
 bool VisitTagTypeLoc(clang::TagTypeLoc loc) {if(!loc.isDefinition()) reference(loc.getDecl(),loc.getNameLoc());return active();}
 bool VisitTypedefTypeLoc(clang::TypedefTypeLoc loc) {reference(loc.getTypedefNameDecl(),loc.getNameLoc());return active();}
 bool VisitInjectedClassNameTypeLoc(clang::InjectedClassNameTypeLoc loc) {reference(loc.getDecl(),loc.getNameLoc());return active();}
 bool VisitTemplateTypeParmTypeLoc(clang::TemplateTypeParmTypeLoc loc) {reference(loc.getDecl(),loc.getNameLoc());return active();}
 bool VisitTemplateSpecializationTypeLoc(clang::TemplateSpecializationTypeLoc loc) {reference(loc.getTypePtr()->getTemplateName().getAsTemplateDecl(),loc.getTemplateNameLoc());return active();}
 bool TraverseNestedNameSpecifierLoc(clang::NestedNameSpecifierLoc loc) {
  if(loc) {auto *name=loc.getNestedNameSpecifier();if(auto *space=name->getAsNamespace()) reference(space,loc.getLocalBeginLoc());else if(auto *alias=name->getAsNamespaceAlias()) reference(alias,loc.getLocalBeginLoc());}
  return active() && clang::RecursiveASTVisitor<Calls>::TraverseNestedNameSpecifierLoc(loc);
 }
 bool TraverseUnaryExprOrTypeTraitExpr(clang::UnaryExprOrTypeTraitExpr *expr) {
  if(expr->getKind()==clang::UETT_SizeOf && expr->getTypeOfArgument()->isVariablyModifiedType()) return clang::RecursiveASTVisitor<Calls>::TraverseUnaryExprOrTypeTraitExpr(expr);
  return without_calls([&]{return clang::RecursiveASTVisitor<Calls>::TraverseUnaryExprOrTypeTraitExpr(expr);});
 }
 bool TraverseFunctionProtoTypeLoc(clang::FunctionProtoTypeLoc loc) {
  if(!language.CPlusPlus) return clang::RecursiveASTVisitor<Calls>::TraverseFunctionProtoTypeLoc(loc);
  // Exception specifications are stored as raw expressions, not necessarily
  // CXXNoexceptExpr nodes. The declarator is unevaluated; the body is separate.
  return without_calls([&]{return clang::RecursiveASTVisitor<Calls>::TraverseFunctionProtoTypeLoc(loc);});
 }
 bool TraverseCXXNoexceptExpr(clang::CXXNoexceptExpr *expr) {return without_calls([&]{return clang::RecursiveASTVisitor<Calls>::TraverseCXXNoexceptExpr(expr);});}
 bool TraverseDecltypeTypeLoc(clang::DecltypeTypeLoc loc) {return without_calls([&]{return clang::RecursiveASTVisitor<Calls>::TraverseDecltypeTypeLoc(loc);});}
 bool TraverseCXXTypeidExpr(clang::CXXTypeidExpr *expr) {
  if(expr->isPotentiallyEvaluated()) return clang::RecursiveASTVisitor<Calls>::TraverseCXXTypeidExpr(expr);
  return without_calls([&]{return clang::RecursiveASTVisitor<Calls>::TraverseCXXTypeidExpr(expr);});
 }
 bool TraverseCXXDefaultArgExpr(clang::CXXDefaultArgExpr *expr) {return without_calls([&]{return clang::RecursiveASTVisitor<Calls>::TraverseCXXDefaultArgExpr(expr);});}
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
 Calls calls(ast->getASTContext(),path,text,cancel,rows);if(!calls.TraverseDecl(ast->getASTContext().getTranslationUnitDecl()) || rows->len+calls.rows->len>100000 || (cancel && g_cancellable_is_cancelled(cancel))) return FALSE;
 guint retained=0;for(guint i=0;i<rows->len;i++) {const auto row=g_array_index(rows,InsightSymbol,i);bool replaced_reference=false;
  if(!std::strcmp(row.kind,"@clangref")) {char *end=nullptr;auto offset=g_ascii_strtoull(row.signature,&end,10);if(end && *end==':' && offset<=G_MAXUINT) replaced_reference=calls.references.count(std::make_pair((unsigned)offset,std::string(row.scope)))!=0;}
  bool replaced=replaced_reference || (!std::strcmp(row.kind,"@clangtarget") && calls.destinations.count(row.name)) || (!std::strcmp(row.kind,"@clangdef") && calls.definitions.count(row.name)) || (!std::strcmp(row.kind,"@clangbody") && calls.bodies.count(row.name)) || !std::strcmp(row.kind,"@clangcall") || !std::strcmp(row.kind,"@clangvirtual");
  if(!replaced) g_array_index(rows,InsightSymbol,retained++)=row;
 }
 g_array_set_size(rows,retained);
 g_array_append_vals(rows,calls.rows->data,calls.rows->len);for(guint i=0;i<calls.strings->len;i++) g_ptr_array_add(owned,g_ptr_array_index(calls.strings,i));g_ptr_array_set_free_func(calls.strings,nullptr);return TRUE;
}
namespace {
using NativeDatabase=clang::tooling::JSONCompilationDatabase *;
using NativeCommands=std::vector<clang::tooling::CompileCommand> *;
using NativeCommand=const clang::tooling::CompileCommand *;
using NativeDBError=int;
static gchar *copied(const std::string &value) {return g_strdup(value.c_str());}
static NativeDatabase native_CompilationDatabase_fromDirectory(const gchar *path,NativeDBError *) {std::string error;return clang::tooling::JSONCompilationDatabase::loadFromFile(std::string(path)+"/compile_commands.json",error,clang::tooling::JSONCommandLineSyntax::AutoDetect).release();}
static void native_CompilationDatabase_dispose(NativeDatabase db) {delete db;}
static NativeCommands native_CompilationDatabase_getAllCompileCommands(NativeDatabase db) {return new std::vector<clang::tooling::CompileCommand>(db->getAllCompileCommands());}
static NativeCommands native_CompilationDatabase_getCompileCommands(NativeDatabase db,const gchar *path) {return new std::vector<clang::tooling::CompileCommand>(db->getCompileCommands(path));}
static guint native_CompileCommands_getSize(NativeCommands commands) {return commands ? commands->size():0;}
static NativeCommand native_CompileCommands_getCommand(NativeCommands commands,guint i) {return &(*commands)[i];}
static void native_CompileCommands_dispose(NativeCommands commands) {delete commands;}
static std::string native_CompileCommand_getDirectory(NativeCommand command) {return command->Directory;}
static std::string native_CompileCommand_getFilename(NativeCommand command) {return command->Filename;}
static guint native_CompileCommand_getNumArgs(NativeCommand command) {return command->CommandLine.size();}
static std::string native_CompileCommand_getArg(NativeCommand command,guint i) {return command->CommandLine[i];}
static gboolean path_option(const gchar *s) {return !strcmp(s,"-I") || !strcmp(s,"-isystem") || !strcmp(s,"-iquote") || !strcmp(s,"-include") || !strcmp(s,"-imacros") || !strcmp(s,"-isysroot") || !strcmp(s,"--sysroot");}
static void argument(GPtrArray *args,const gchar *s) {g_ptr_array_add(args,g_strdup(s));}
static gint command_path_compare(const gchar *a,const gchar *b) {
#ifdef G_OS_WIN32
 return g_ascii_strcasecmp(a,b);
#else
 return g_strcmp0(a,b);
#endif
}
static gboolean header_source(const gchar *path) {
 const gchar *ext=strrchr(path,'.');return ext && (!g_ascii_strcasecmp(ext,".h") || !g_ascii_strcasecmp(ext,".hpp") || !g_ascii_strcasecmp(ext,".hh") || !g_ascii_strcasecmp(ext,".hxx"));
}
static gboolean cpp_header(const gchar *path) {
 const gchar *ext=strrchr(path,'.');return ext && (strcmp(ext,".h") && (!g_ascii_strcasecmp(ext,".h") || !g_ascii_strcasecmp(ext,".hpp") || !g_ascii_strcasecmp(ext,".hh") || !g_ascii_strcasecmp(ext,".hxx")));
}
static gchar *command_file(NativeCommand command) {
 gchar *cwd=copied(native_CompileCommand_getDirectory(command)),*file=copied(native_CompileCommand_getFilename(command));gchar *absolute=g_path_is_absolute(cwd) ? insight_canonicalize_filename(file,cwd):g_strdup("");g_free(cwd);g_free(file);return absolute;
}
/* Explicit -x wins over driver/file suffix. The driver is inspected only;
 * compilation-database commands and wrappers are never executed. */
static const gchar *command_language(NativeCommand command,const gchar *file) {
 const gchar *language=NULL;gboolean driver_cpp=FALSE,wrapped=FALSE;
 for(guint i=0;i<native_CompileCommand_getNumArgs(command) && i<2048;i++) {
  gchar *arg=copied(native_CompileCommand_getArg(command,i));
  if(!strcmp(arg,"-x") && i+1<native_CompileCommand_getNumArgs(command)) {g_free(arg);arg=copied(native_CompileCommand_getArg(command,++i));if(!strcmp(arg,"c") || !strcmp(arg,"c-header")) language="c";else if(!strcmp(arg,"c++") || !strcmp(arg,"c++-header")) language="c++";else if(!strcmp(arg,"none")) language=NULL;else language="unsupported";}
  else if(g_str_has_prefix(arg,"-x") && arg[2]) {if(!strcmp(arg+2,"c") || !strcmp(arg+2,"c-header")) language="c";else if(!strcmp(arg+2,"c++") || !strcmp(arg+2,"c++-header")) language="c++";else if(!strcmp(arg+2,"none")) language=NULL;else language="unsupported";}
  else if((i==0 || (i==1 && wrapped)) && arg[0]!='-') {gchar *name=g_path_get_basename(arg);const gchar *tail=strstr(name,"++");if(tail && (!tail[2] || tail[2]=='-' || !g_ascii_strcasecmp(tail+2,".exe"))) driver_cpp=TRUE;if(i==0 && (g_str_has_prefix(name,"ccache") || g_str_has_prefix(name,"sccache") || g_str_has_prefix(name,"distcc"))) wrapped=TRUE;g_free(name);}
  g_free(arg);
 }
 if(language) return strcmp(language,"unsupported") ? language:NULL;
 const gchar *ext=strrchr(file,'.');if(driver_cpp || (ext && (!strcmp(ext,".C") || !g_ascii_strcasecmp(ext,".cc") || !g_ascii_strcasecmp(ext,".cpp") || !g_ascii_strcasecmp(ext,".cxx") || !g_ascii_strcasecmp(ext,".c++") || !g_ascii_strcasecmp(ext,".hpp") || !g_ascii_strcasecmp(ext,".hh") || !g_ascii_strcasecmp(ext,".hxx")))) return "c++";
 return ext && (!strcmp(ext,".c") || !g_ascii_strcasecmp(ext,".h")) ? "c":NULL;
}
static gchar *file_stem(const gchar *path) {gchar *stem=g_path_get_basename(path),*dot=strrchr(stem,'.');if(dot && dot!=stem) *dot=0;return stem;}
static guint directory_distance(const gchar *a,const gchar *b) {
 gchar *left=g_path_get_dirname(a),*right=g_path_get_dirname(b);gchar **x=g_strsplit_set(left,"/\\",-1),**y=g_strsplit_set(right,"/\\",-1);guint same=0;
 while(x[same] && y[same] && !command_path_compare(x[same],y[same])) same++;
 guint distance=g_strv_length(x)+g_strv_length(y)-2*same;g_strfreev(x);g_strfreev(y);g_free(left);g_free(right);return distance;
}
/* Header commands are hints, not proof that a translation unit includes it.
 * Companion basename wins, then closest directory. A mixed C/C++ tie for .h
 * declines rather than silently choosing a language. Same-language ties use
 * canonical filename order, independent of database entry order. */
static gint header_command(NativeCommands commands,const gchar *header,gboolean *ambiguous) {
 guint count=native_CompileCommands_getSize(commands);if(count>8192) return -1;
 gchar *stem=file_stem(header),*best_file=NULL;const gchar *best_language=NULL;guint best_family=G_MAXUINT,best_distance=G_MAXUINT;gint best=-1;*ambiguous=FALSE;
 for(guint i=0;i<count;i++) {
  NativeCommand command=native_CompileCommands_getCommand(commands,i);gchar *file=command_file(command);if(!*file || strlen(file)>4096) {g_free(file);continue;}
  const gchar *language=command_language(command,file);if(!language || (cpp_header(header) && strcmp(language,"c++"))) {g_free(file);continue;}
  gchar *name=file_stem(file);guint family=command_path_compare(stem,name)!=0,distance=directory_distance(header,file);g_free(name);
  if(family<best_family || (family==best_family && distance<best_distance)) {best=(gint)i;best_family=family;best_distance=distance;best_language=language;*ambiguous=FALSE;g_free(best_file);best_file=g_strdup(file);}
  else if(family==best_family && distance==best_distance) {if(strcmp(best_language,language)) *ambiguous=TRUE;if(command_path_compare(file,best_file)<0) {best=(gint)i;g_free(best_file);best_file=g_strdup(file);}}
  g_free(file);
 }
 g_free(stem);g_free(best_file);return *ambiguous ? -1:best;
}
static GPtrArray *arguments(const gchar *path,const gchar *root,const gchar *resources,InsightClangResult *report) {
 GPtrArray *args=g_ptr_array_new_with_free_func(g_free);NativeDatabase db=NULL;NativeCommands commands=NULL;NativeDBError error;gint selected=-1;gboolean inferred=FALSE,ambiguous=FALSE;
 if(root) {gchar *manifest=g_build_filename(root,"compile_commands.json",NULL);if(g_file_test(manifest,G_FILE_TEST_IS_REGULAR)) db=native_CompilationDatabase_fromDirectory(root,&error);g_free(manifest);if(!db) {gchar *build=g_build_filename(root,"build",NULL);manifest=g_build_filename(build,"compile_commands.json",NULL);if(g_file_test(manifest,G_FILE_TEST_IS_REGULAR)) db=native_CompilationDatabase_fromDirectory(build,&error);g_free(manifest);g_free(build);}}
 if(db && header_source(path)) {
  /* Enumerate explicit rows first: some Clang database wrappers can return an
   * interpolated command from getCompileCommands for an unlisted header. */
  commands=native_CompilationDatabase_getAllCompileCommands(db);guint count=commands ? native_CompileCommands_getSize(commands):0;gchar *expected=insight_canonicalize_filename(path,NULL);
  if(count<=8192) {for(guint i=0;i<count;i++) {gchar *file=command_file(native_CompileCommands_getCommand(commands,i));gboolean exact=!command_path_compare(file,expected);g_free(file);if(exact) {selected=(gint)i;break;}}if(selected<0 && commands) {selected=header_command(commands,path,&ambiguous);inferred=selected>=0;}}
  else {native_CompileCommands_dispose(commands);commands=native_CompilationDatabase_getCompileCommands(db,path);if(commands && native_CompileCommands_getSize(commands)) {gchar *file=command_file(native_CompileCommands_getCommand(commands,0));if(!command_path_compare(file,expected)) selected=0;g_free(file);}}
  g_free(expected);
 } else if(db) {commands=native_CompilationDatabase_getCompileCommands(db,path);if(commands && native_CompileCommands_getSize(commands)) {gchar *file=command_file(native_CompileCommands_getCommand(commands,0)),*expected=insight_canonicalize_filename(path,NULL);if(!command_path_compare(file,expected)) selected=0;g_free(file);g_free(expected);}}

 if(selected>=0) {
  NativeCommand command=native_CompileCommands_getCommand(commands,(guint)selected);gchar *cwd=copied(native_CompileCommand_getDirectory(command)),*file=command_file(command);const gchar *language=command_language(command,file);
  if(report) {report->compilation_database=TRUE;report->inferred_command=inferred;report->command_source=g_strdup(file);report->parse_language=g_strdup(language ? language:"configured");}
  for(guint i=1;i<native_CompileCommand_getNumArgs(command) && args->len<2048;i++) {gchar *s=copied(native_CompileCommand_getArg(command,i));
   if(path_option(s) || !strcmp(s,"-D") || !strcmp(s,"-U") || !strcmp(s,"-x") || !strcmp(s,"-target")) {if(i+1<native_CompileCommand_getNumArgs(command)) {gchar *value=copied(native_CompileCommand_getArg(command,++i));argument(args,s);if(path_option(s)) {gchar *absolute=insight_canonicalize_filename(value,cwd);argument(args,absolute);g_free(absolute);}else argument(args,value);g_free(value);}}
   else if(g_str_has_prefix(s,"-I") && s[2]) {gchar *absolute=insight_canonicalize_filename(s+2,cwd),*joined=g_strconcat("-I",absolute,NULL);argument(args,joined);g_free(joined);g_free(absolute);}
   else if(g_str_has_prefix(s,"-isystem") || g_str_has_prefix(s,"-iquote") || g_str_has_prefix(s,"--sysroot=")) {gsize prefix=g_str_has_prefix(s,"-isystem") ? 8:g_str_has_prefix(s,"-iquote") ? 7:10;gchar *absolute=insight_canonicalize_filename(s+prefix,cwd);argument(args,prefix==8 ? "-isystem":prefix==7 ? "-iquote":"--sysroot");argument(args,absolute);g_free(absolute);}
   else if(g_str_has_prefix(s,"-O") || (g_str_has_prefix(s,"-m") && !g_str_has_prefix(s,"-mllvm")) || !strcmp(s,"-pthread") || !strcmp(s,"-ffreestanding") || !strcmp(s,"-fno-builtin") || !strcmp(s,"-fPIC") || !strcmp(s,"-fpic") || !strcmp(s,"-fPIE") || !strcmp(s,"-fpie")) argument(args,s);
   else if(g_str_has_prefix(s,"-D") || g_str_has_prefix(s,"-U") || g_str_has_prefix(s,"-std=") || g_str_has_prefix(s,"--target=") || !strcmp(s,"-m32") || !strcmp(s,"-m64") || !strcmp(s,"-fms-extensions") || !strcmp(s,"-fms-compatibility") || !strcmp(s,"-fno-exceptions") || !strcmp(s,"-fexceptions") || !strcmp(s,"-fno-rtti") || !strcmp(s,"-frtti")) argument(args,s);
   g_free(s);
  }
  if(language) {argument(args,"-x");argument(args,language);}g_free(file);g_free(cwd);
 }
 if(report) report->ambiguous_command=ambiguous;
 if(commands) native_CompileCommands_dispose(commands);if(db) native_CompilationDatabase_dispose(db);
 if(selected<0) {const gchar *dot=strrchr(path,'.'),*language=dot && (!strcmp(dot,".c") || !strcmp(dot,".h")) ? "c":"c++";argument(args,"-x");argument(args,language);if(report) report->parse_language=g_strdup(language);}
 if(root) {argument(args,"-I");argument(args,root);}if(resources && g_file_test(resources,G_FILE_TEST_IS_DIR)) {argument(args,"-resource-dir");argument(args,resources);}
 argument(args,"-fsyntax-only");argument(args,"-fno-modules");argument(args,"-fno-implicit-modules");return args;
}
class Diagnostics : public clang::DiagnosticConsumer {
public:
 std::string text;bool errors=false;
 void HandleDiagnostic(clang::DiagnosticsEngine::Level level,const clang::Diagnostic &info) override {
  clang::DiagnosticConsumer::HandleDiagnostic(level,info);if(level>=clang::DiagnosticsEngine::Error) errors=true;if(text.size()>=65536) return;
  llvm::SmallString<256> message;info.FormatDiagnostic(message);
  if(info.hasSourceManager() && info.getLocation().isValid()) {const auto &sm=info.getSourceManager();auto at=sm.getSpellingLoc(info.getLocation());text+=sm.getFilename(at).str()+":"+std::to_string(sm.getSpellingLineNumber(at))+":"+std::to_string(sm.getSpellingColumnNumber(at))+": ";}
  text+=message.str().str()+"\n";if(text.size()>65536) text.resize(65536);
 }
};
static std::vector<std::string> flags_for(const gchar *path,const gchar *root,const gchar *resources,InsightClangResult *report) {
 GPtrArray *array=arguments(path,root,resources,report);std::vector<std::string> flags;for(guint i=0;i<array->len;i++) flags.emplace_back((const gchar *)array->pdata[i]);g_ptr_array_unref(array);return flags;
}
static clang::tooling::FileContentMappings mappings(const gchar *path,GHashTable *overlays) {
 clang::tooling::FileContentMappings files;gsize bytes=0;if(overlays) {GHashTableIter it;gpointer key,value;g_hash_table_iter_init(&it,overlays);while(g_hash_table_iter_next(&it,&key,&value)) if(value && strcmp((const gchar *)key,path) && files.size()<256 && bytes+strlen((const gchar *)value)<=32u*1024u*1024u) {files.emplace_back((const gchar *)key,(const gchar *)value);bytes+=strlen((const gchar *)value);}}return files;
}
static std::string file_stamp(const gchar *path) {
 GFile *file=g_file_new_for_path(path);GFileInfo *info=g_file_query_info(file,"standard::size,time::modified,time::modified-usec",G_FILE_QUERY_INFO_NONE,nullptr,nullptr);g_object_unref(file);if(!info) return "missing";
 gchar *value=g_strdup_printf("%"G_GUINT64_FORMAT":%"G_GUINT64_FORMAT":%u",(guint64)g_file_info_get_size(info),g_file_info_get_attribute_uint64(info,G_FILE_ATTRIBUTE_TIME_MODIFIED),g_file_info_get_attribute_uint32(info,G_FILE_ATTRIBUTE_TIME_MODIFIED_USEC));g_object_unref(info);std::string result=value;g_free(value);return result;
}
static void preprocessing(clang::ASTUnit &ast,Calls &data,const gchar *path,const gchar *text,InsightClangResult *result,GCancellable *cancel) {
 auto *record=ast.getPreprocessor().getPreprocessingRecord();if(!record) return;auto &sm=ast.getSourceManager();gsize length=strlen(text);std::set<std::string> includes;
 unsigned visited=0;for(auto it=record->begin();it!=record->end();++it) {
  if((cancel && g_cancellable_is_cancelled(cancel)) || ++visited>200000 || data.symbols->len>=50000 || data.rows->len>=100000) {result->limited=TRUE;return;}
  auto *entity=*it;if(!entity) continue;
  if(auto *include=llvm::dyn_cast<clang::InclusionDirective>(entity)) {auto file=include->getFile();if(file) {auto name=file->getName().str();if(name!=path && includes.insert(name).second && data.rows->len<100000) {InsightSymbol row={g_strdup(name.c_str()),path,"","@clanginclude",1,g_strdup(file_stamp(name.c_str()).c_str()),"",""};g_ptr_array_add(data.strings,(gpointer)row.name);g_ptr_array_add(data.strings,(gpointer)row.signature);g_array_append_val(data.rows,row);}}continue;}
  auto at=entity->getSourceRange().getBegin();if(at.isInvalid() || at.isMacroID() || !sm.isWrittenInMainFile(at)) continue;
  const clang::MacroDefinitionRecord *definition=llvm::dyn_cast<clang::MacroDefinitionRecord>(entity);auto *expansion=llvm::dyn_cast<clang::MacroExpansion>(entity);if(expansion) definition=expansion->getDefinition();if(!definition) continue;
  auto name=definition->getName()->getName().str();guint begin=sm.getFileOffset(at),end=begin+name.size();if(end>length || memcmp(text+begin,name.data(),name.size())) continue;
  if(result->tokens->len<200000) {InsightClangToken token={begin,end,INSIGHT_CLANG_MACRO};g_array_append_val(result->tokens,token);}else result->limited=TRUE;
  auto own=[&](const std::string &value)->const gchar * {auto *p=g_strdup(value.c_str());g_ptr_array_add(data.strings,p);return p;};
  llvm::SmallString<128> identity;clang::index::generateUSRForMacro(definition,sm,identity);
  if(!expansion) {
   std::string signature;bool callable=end<length && text[end]=='(';if(callable) {signature="(";for(gsize i=end+1;i<length && signature.size()<4096;i++) {char c=text[i];if(c==')') {signature+=')';break;}if(c==',') signature+=", ";else if(!g_ascii_isspace(c)) signature+=c;}}
   InsightSymbol row={own(name),path,"","d",sm.getSpellingLineNumber(at),own(signature),"",""};g_array_append_val(data.symbols,row);
   g_hash_table_replace(result->attributes,insight_clang_attribute_key(&row),GUINT_TO_POINTER(callable ? INSIGHT_CLANG_FUNCTION_MACRO:0));
   if(!identity.empty()) {InsightSymbol definition_row={own(identity.str().str()),path,"","@clangdef",row.line,own(name),"",""};g_array_append_val(data.rows,definition_row);}
  } else if(!identity.empty()) {
   auto target_at=definition->getSourceRange().getBegin();auto file=sm.getFilename(target_at).str();gchar *position=g_strdup_printf("%u:%u",sm.getSpellingLineNumber(target_at),sm.getSpellingColumnNumber(target_at));g_ptr_array_add(data.strings,position);InsightSymbol target={own(identity.str().str()),path,own(file),"@clangtarget",sm.getSpellingLineNumber(target_at),position,own(name),"d"};g_array_append_val(data.rows,target);
   gchar *range=g_strdup_printf("%u:%u:%u",begin,end,sm.getSpellingColumnNumber(at));g_ptr_array_add(data.strings,range);InsightSymbol row={own(name),path,own(identity.str().str()),"@clangref",sm.getSpellingLineNumber(at),range,"",""};g_array_append_val(data.rows,row);
  }
 }
}
static void completion_free(gpointer value) {auto *c=(InsightClangCompletion *)value;g_free(c->name);g_free(c->signature);g_free(c->type);g_free(c->comment);g_ptr_array_unref(c->parameters);g_free(c);}
static gint completion_order(gconstpointer a,gconstpointer b) {auto *x=*(InsightClangCompletion *const *)a,*y=*(InsightClangCompletion *const *)b;if(x->priority!=y->priority) return x->priority<y->priority ? -1:1;return g_strcmp0(x->name,y->name);}
class Completions : public clang::CodeCompleteConsumer {
 clang::CodeCompletionTUInfo info{std::make_shared<clang::GlobalCodeCompletionAllocator>()};
 GPtrArray *out;GCancellable *cancel;gsize bytes=0;
public:
 Completions(const clang::CodeCompleteOptions &options,GPtrArray *o,GCancellable *c):CodeCompleteConsumer(options),out(o),cancel(c) {}
 clang::CodeCompletionAllocator &getAllocator() override {return info.getAllocator();}
 clang::CodeCompletionTUInfo &getCodeCompletionTUInfo() override {return info;}
 void ProcessCodeCompleteResults(clang::Sema &sema,clang::CodeCompletionContext context,clang::CodeCompletionResult *results,unsigned count) override {
  for(unsigned i=0;i<count && i<50000 && out->len<4096 && bytes<4u*1024u*1024u;i++) {
   if(cancel && g_cancellable_is_cancelled(cancel)) return;auto *str=results[i].CreateCodeCompletionString(sema,context,getAllocator(),info,true);if(!str || str->getAvailability()!=CXAvailability_Available) continue;
   auto *candidate=g_new0(InsightClangCompletion,1);candidate->parameters=g_ptr_array_new_with_free_func(g_free);candidate->priority=str->getPriority();candidate->name=g_strdup(str->getTypedText());candidate->type=g_strdup("");std::string label;
   for(const auto &chunk:*str) {using CC=clang::CodeCompletionString;if(chunk.Kind==CC::CK_ResultType) {g_free(candidate->type);candidate->type=g_strdup(chunk.Text);}else if(chunk.Kind==CC::CK_Placeholder && candidate->parameters->len<64) g_ptr_array_add(candidate->parameters,g_strdup(chunk.Text));if(chunk.Kind==CC::CK_LeftParen) candidate->callable=TRUE;if(chunk.Kind!=CC::CK_ResultType && chunk.Kind!=CC::CK_Informative && chunk.Kind!=CC::CK_Optional) label+=chunk.Text;if(label.size()>16384) break;}
   candidate->signature=g_strdup(label.c_str());candidate->comment=g_strdup(str->getBriefComment() ? str->getBriefComment():"");
   if(candidate->name && *candidate->name && !strpbrk(candidate->name,"\r\n\t") && strlen(candidate->name)<=1024 && label.size()<=16384) {bytes+=label.size()+strlen(candidate->name)+strlen(candidate->type)+strlen(candidate->comment);g_ptr_array_add(out,candidate);}else completion_free(candidate);
  }
 }
};
class CompletionAction : public clang::SyntaxOnlyAction {
 GPtrArray *out;GCancellable *cancel;
public:
 CompletionAction(GPtrArray *o,GCancellable *c):out(o),cancel(c) {}
 bool BeginSourceFileAction(clang::CompilerInstance &compiler) override {clang::CodeCompleteOptions options;options.IncludeMacros=true;options.IncludeBriefComments=true;options.IncludeCodePatterns=false;compiler.setCodeCompletionConsumer(new Completions(options,out,cancel));return clang::SyntaxOnlyAction::BeginSourceFileAction(compiler);}
};
}
extern "C" InsightClangResult *insight_clang_cpp_parse(const gchar *path,const gchar *text,const gchar *root,const gchar *resources,GHashTable *overlays,GCancellable *cancel,GError **error) {
 if(!path || !text || !insight_clang_source(path) || strlen(text)>2u*1024u*1024u || !g_utf8_validate(text,-1,nullptr)) {g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Clang requires a C/C++ UTF-8 source up to 2 MiB");return nullptr;}if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) return nullptr;
 auto *r=g_new0(InsightClangResult,1);r->symbols=insight_index_new();r->relations=insight_index_new();r->tokens=g_array_new(FALSE,FALSE,sizeof(InsightClangToken));r->attributes=g_hash_table_new_full(g_str_hash,g_str_equal,g_free,nullptr);r->digest=g_compute_checksum_for_string(G_CHECKSUM_SHA256,text,-1);
 auto flags=flags_for(path,root,resources,r);flags.emplace_back("-Xclang");flags.emplace_back("-detailed-preprocessing-record");Diagnostics diagnostics;
 auto ast=clang::tooling::buildASTFromCodeWithArgs(text,flags,path,"clang-tool",std::make_shared<clang::PCHContainerOperations>(),clang::tooling::getClangStripDependencyFileAdjuster(),mappings(path,overlays),&diagnostics);
 r->diagnostics=g_utf8_make_valid(diagnostics.text.c_str(),diagnostics.text.size());r->errors=diagnostics.errors;
 if(!ast) {insight_clang_result_free(r);g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_FAILED,"Clang could not parse the translation unit");return nullptr;}
 GArray *empty=g_array_new(FALSE,FALSE,sizeof(InsightSymbol));Calls data(ast->getASTContext(),path,text,cancel,empty,r);g_array_unref(empty);if(!data.TraverseDecl(ast->getASTContext().getTranslationUnitDecl())) r->limited=TRUE;r->internal_calls=TRUE;preprocessing(*ast,data,path,text,r,cancel);
 if(root) for(const auto *relative:{"compile_commands.json","build/compile_commands.json"}) {gchar *name=g_build_filename(root,relative,nullptr),*stamp=g_strdup(file_stamp(name).c_str());g_ptr_array_add(data.strings,name);g_ptr_array_add(data.strings,stamp);InsightSymbol row={name,path,"","@clanginclude",1,stamp,"",""};g_array_append_val(data.rows,row);}
 InsightSymbol coverage={"@file",path,"","@file",1,"","",r->digest};g_array_append_val(data.rows,coverage);InsightSymbol compiler={"@clangfile",path,"","@clangfile",1,"",(r->errors || r->ambiguous_command || r->limited) ? "partial":"complete",r->digest};g_array_append_val(data.rows,compiler);
 if(!insight_index_update(r->symbols,path,(InsightSymbol *)data.symbols->data,data.symbols->len) || !insight_index_update(r->relations,path,(InsightSymbol *)data.rows->data,data.rows->len)) r->limited=TRUE;
 if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) {insight_clang_result_free(r);return nullptr;}return r;
}
extern "C" GPtrArray *insight_clang_cpp_complete(const gchar *path,const gchar *text,guint offset,const gchar *root,const gchar *resources,GHashTable *overlays,GCancellable *cancel,GError **error) {
 if(!path || !text || !insight_clang_source(path) || strlen(text)>2u*1024u*1024u || offset>strlen(text) || !g_utf8_validate(text,-1,nullptr) || !g_utf8_validate(text,offset,nullptr)) {g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_INVALID_ARGUMENT,"Invalid C/C++ completion source or offset");return nullptr;}if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) return nullptr;
 InsightClangResult command={};auto flags=flags_for(path,root,resources,&command);g_free(command.command_source);g_free(command.parse_language);if(command.ambiguous_command) {g_set_error_literal(error,G_IO_ERROR,G_IO_ERROR_NOT_SUPPORTED,"Ambiguous C/C++ header compilation settings");return nullptr;}
 auto memory=llvm::makeIntrusiveRefCnt<llvm::vfs::InMemoryFileSystem>();memory->addFile(path,0,llvm::MemoryBuffer::getMemBufferCopy(text,path));for(const auto &file:mappings(path,overlays)) memory->addFile(file.first,0,llvm::MemoryBuffer::getMemBufferCopy(file.second,file.first));auto fs=llvm::makeIntrusiveRefCnt<llvm::vfs::OverlayFileSystem>(llvm::vfs::getRealFileSystem());fs->pushOverlay(memory);
 auto *out=g_ptr_array_new_with_free_func(completion_free);unsigned line=1,column=1;for(guint i=0;i<offset;i++) if(text[i]=='\n') {line++;column=1;}else column++;
 flags.insert(flags.begin(),"clang-tool");flags.emplace_back("-Xclang");flags.emplace_back("-code-completion-at="+std::string(path)+":"+std::to_string(line)+":"+std::to_string(column));flags.emplace_back(path);
 clang::FileSystemOptions options;auto files=llvm::makeIntrusiveRefCnt<clang::FileManager>(options,fs);Diagnostics diagnostics;clang::tooling::ToolInvocation invocation(flags,std::make_unique<CompletionAction>(out,cancel),files.get());invocation.setDiagnosticConsumer(&diagnostics);invocation.run();
 if(cancel && g_cancellable_set_error_if_cancelled(cancel,error)) {g_ptr_array_unref(out);return nullptr;}g_ptr_array_sort(out,completion_order);return out;
}
#endif
