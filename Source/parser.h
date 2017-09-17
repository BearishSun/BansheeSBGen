#pragma once
#include "common.h"

struct FunctionTypeInfo;

class ScriptExportParser : public RecursiveASTVisitor<ScriptExportParser>
{
public:
	explicit ScriptExportParser(CompilerInstance* CI);

	bool VisitEnumDecl(EnumDecl* decl);
	bool VisitCXXRecordDecl(CXXRecordDecl* decl);

private:
	bool evaluateLiteral(Expr* expr, std::string& evalValue);
	bool evaluateExpression(Expr* expr, std::string& evalValue, std::string& valType);
	bool parseEventSignature(QualType type, FunctionTypeInfo& typeInfo);
	bool parseEvent(ValueDecl* decl, const std::string& className, MethodInfo& eventInfo);
	bool parseType(QualType type, std::string& outType, int& typeFlags, unsigned& arraySize, bool returnValue = false);
	std::string parseTemplArguments(const std::string& className, const TemplateArgument* tmplArgs, unsigned numArgs, SmallVector<TemplateParamInfo, 0>* templParams);
	bool parseJavadocComments(const Decl* decl, CommentEntry& entry);
	void parseCommentInfo(const NamedDecl* decl, CommentInfo& commentInfo);
	void parseCommentInfo(const FunctionDecl* decl, CommentInfo& commentInfo);
	void parseComments(const NamedDecl* decl, CommentInfo& commentInfo);
	void parseComments(const CXXRecordDecl* decl);

	ASTContext* astContext;
};
