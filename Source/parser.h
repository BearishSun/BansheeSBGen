#pragma once
#include "common.h"

class ScriptExportParser : public RecursiveASTVisitor<ScriptExportParser>
{
public:
	explicit ScriptExportParser(CompilerInstance* CI);

	bool VisitEnumDecl(EnumDecl* decl);
	bool VisitCXXRecordDecl(CXXRecordDecl* decl);

private:
	bool evaluateExpression(Expr* expr, std::string& evalValue);
	bool parseJavadocComments(const Decl* decl, CommentEntry& entry);
	void parseCommentInfo(const NamedDecl* decl, CommentInfo& commentInfo);
	void parseCommentInfo(const FunctionDecl* decl, CommentInfo& commentInfo);
	void parseComments(const NamedDecl* decl, CommentInfo& commentInfo);
	void parseComments(const CXXRecordDecl* decl);

	ASTContext* astContext;
};