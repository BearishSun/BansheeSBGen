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
	bool parseJavadocComments(Decl* decl, CommentEntry& entry);
	void parseCommentInfo(NamedDecl* decl, CommentInfo& commentInfo);
	void parseComments(NamedDecl* decl);
	void parseComments(CXXRecordDecl* decl);

	ASTContext* astContext;
};