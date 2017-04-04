#include "parser.h"

ParsedType getObjectType(const CXXRecordDecl* decl)
{
	std::stack<const CXXRecordDecl*> todo;
	todo.push(decl);

	while (!todo.empty())
	{
		const CXXRecordDecl* curDecl = todo.top();
		todo.pop();

		auto iter = curDecl->bases_begin();
		while (iter != curDecl->bases_end())
		{
			const CXXBaseSpecifier* baseSpec = iter;
			CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

			std::string className = baseDecl->getName();

			if (className == BUILTIN_COMPONENT_TYPE)
				return ParsedType::Component;
			else if (className == BUILTIN_RESOURCE_TYPE)
				return ParsedType::Resource;
			else if (className == BUILTIN_SCENEOBJECT_TYPE)
				return ParsedType::SceneObject;

			todo.push(baseDecl);
			iter++;
		}
	}

	return ParsedType::Class;
}

bool isGameObjectOrResource(QualType type)
{
	const RecordType* recordType = type->getAs<RecordType>();
	if (recordType == nullptr)
		return false;

	const RecordDecl* recordDecl = recordType->getDecl();
	const CXXRecordDecl* cxxDecl = dyn_cast<CXXRecordDecl>(recordDecl);
	if (cxxDecl == nullptr)
		return false;

	ParsedType objType = getObjectType(cxxDecl);
	return objType == ParsedType::Component || objType == ParsedType::SceneObject || objType == ParsedType::Resource;
}

std::string getNamespace(const RecordDecl* decl)
{
	std::string nsName;
	const DeclContext* nsContext = decl->getEnclosingNamespaceContext();
	if (nsContext != nullptr && nsContext->isNamespace())
	{
		// Note: Not checking more than one level of namespaces
		const NamespaceDecl* nsDecl = cast<NamespaceDecl>(nsContext);
		nsName = nsDecl->getName();
	}

	return nsName;
}

bool parseType(QualType type, std::string& outType, int& typeFlags)
{
	typeFlags = 0;

	QualType realType;
	if (type->isPointerType())
	{
		realType = type->getPointeeType();
		typeFlags |= (int)TypeFlags::SrcPtr;

		if (!realType.isConstQualified())
			typeFlags |= (int)TypeFlags::Output;
	}
	else if (type->isReferenceType())
	{
		realType = type->getPointeeType();
		typeFlags |= (int)TypeFlags::SrcRef;

		if (!realType.isConstQualified())
			typeFlags |= (int)TypeFlags::Output;
	}
	else
		realType = type;

	if (realType->isStructureOrClassType())
	{
		// Check for arrays
		// Note: Not supporting nested arrays
		const TemplateSpecializationType* specType = realType->getAs<TemplateSpecializationType>();
		int numArgs = 0;

		if (specType != nullptr)
			numArgs = specType->getNumArgs();

		if (numArgs > 0)
		{
			const RecordType* recordType = realType->getAs<RecordType>();
			const RecordDecl* recordDecl = recordType->getDecl();

			std::string sourceTypeName = recordDecl->getName();
			std::string nsName = getNamespace(recordDecl);

			if (sourceTypeName == "vector" && nsName == "std")
			{
				realType = specType->getArg(0).getAsType();;
				typeFlags |= (int)TypeFlags::Array;
			}
		}
	}

	if (realType->isPointerType())
	{
		errs() << "Only normal pointers are supported for parameter types.\n";
		return false;
	}

	if (realType->isBuiltinType())
	{
		const BuiltinType* builtinType = realType->getAs<BuiltinType>();
		if (!mapBuiltinTypeToCppType(builtinType->getKind(), outType))
			return false;

		typeFlags |= (int)TypeFlags::Builtin;
		return true;
	}
	else if (realType->isStructureOrClassType())
	{
		const RecordType* recordType = realType->getAs<RecordType>();
		const RecordDecl* recordDecl = recordType->getDecl();

		std::string sourceTypeName = recordDecl->getName();
		std::string nsName = getNamespace(recordDecl);

		// Handle special templated types
		const TemplateSpecializationType* specType = realType->getAs<TemplateSpecializationType>();
		if (specType != nullptr)
		{
			int numArgs = specType->getNumArgs();
			if (numArgs > 0)
			{
				QualType argType = specType->getArg(0).getAsType();

				// Check for string types
				if (sourceTypeName == "basic_string" && nsName == "std")
				{
					const BuiltinType* builtinType = argType->getAs<BuiltinType>();
					if (builtinType->getKind() == BuiltinType::Kind::WChar_U)
						typeFlags |= (int)TypeFlags::WString;
					else
						typeFlags |= (int)TypeFlags::String;

					outType = "string";

					return true;
				}

				bool isValid = false;
				if (argType->isBuiltinType())
				{
					const BuiltinType* builtinType = argType->getAs<BuiltinType>();
					isValid = mapBuiltinTypeToCppType(builtinType->getKind(), outType);

					typeFlags |= (int)TypeFlags::Builtin;
				}
				else if (argType->isStructureOrClassType())
				{
					const RecordType* argRecordType = argType->getAs<RecordType>();
					const RecordDecl* argRecordDecl = argRecordType->getDecl();

					outType = argRecordDecl->getName();
					isValid = true;
				}

				// Note: Template specializations are only supported for specific builtin types
				if (isValid)
				{
					isValid = false;
					if (sourceTypeName == "shared_ptr" && nsName == "std")
					{
						typeFlags |= (int)TypeFlags::SrcSPtr;

						if (!isGameObjectOrResource(argType))
							isValid = true;
						else
						{
							errs() << "Game object and resource types are only allowed to be referenced through handles"
								<< " for scripting purposes\n";
						}
					}
					else if (sourceTypeName == "TResourceHandle")
					{
						// Note: Not supporting weak resource handles

						typeFlags |= (int)TypeFlags::SrcRHandle;
						isValid = true;
					}
					else if (sourceTypeName == "GameObjectHandle")
					{
						typeFlags |= (int)TypeFlags::SrcGHandle;
						isValid = true;
					}
				}

				if (isValid)
					return true;
			}
		}

		// Its a user-defined type
		outType = sourceTypeName;
		return true;
	}
	else if (realType->isEnumeralType())
	{
		const EnumType* enumType = realType->getAs<EnumType>();
		const EnumDecl* enumDecl = enumType->getDecl();

		std::string sourceTypeName = enumDecl->getName();
		outType = sourceTypeName;
		return true;
	}
	else
	{
		errs() << "Unrecognized type\n";
		return false;
	}
}

bool parseExportAttribute(AnnotateAttr* attr, StringRef sourceName, StringRef& exportName, StringRef& exportFile,
	CSVisibility& visibility, int& flags, StringRef& externalClass)
{
	SmallVector<StringRef, 4> annotEntries;
	attr->getAnnotation().split(annotEntries, ',');

	if (annotEntries.size() == 0)
		return false;

	StringRef exportTypeStr = annotEntries[0];

	if (exportTypeStr != "se")
		return false;

	exportName = sourceName;
	exportFile = sourceName;
	visibility = CSVisibility::Public;
	flags = 0;

	for (size_t i = 1; i < annotEntries.size(); i++)
	{
		if (annotEntries[i].empty())
			continue;

		auto annotParam = annotEntries[i].split(':');
		if (annotParam.first == "n")
			exportName = annotParam.second;
		else if (annotParam.first == "v")
		{
			if (annotParam.second == "public")
				visibility = CSVisibility::Public;
			else if (annotParam.second == "internal")
				visibility = CSVisibility::Internal;
			else
				errs() << "Unrecognized value for \"v\" option: \"" + annotParam.second + "\" for type \"" <<
				sourceName << "\".\n";
		}
		else if (annotParam.first == "f")
		{
			exportFile = annotParam.second;
		}
		else if (annotParam.first == "pl")
		{
			if (annotParam.second == "true")
				flags |= (int)ExportFlags::Plain;
			else if (annotParam.second != "false")
			{
				errs() << "Unrecognized value for \"pl\" option: \"" + annotParam.second + "\" for type \"" <<
					sourceName << "\".\n";
			}
		}
		else if (annotParam.first == "pr")
		{
			if (annotParam.second == "getter")
				flags |= (int)ExportFlags::PropertyGetter;
			else if (annotParam.second == "setter")
				flags |= (int)ExportFlags::PropertySetter;
			else
			{
				errs() << "Unrecognized value for \"pr\" option: \"" + annotParam.second + "\" for type \"" <<
					sourceName << "\".\n";
			}
		}
		else if (annotParam.first == "e")
		{
			flags |= (int)ExportFlags::External;

			externalClass = annotParam.second;
		}
		else if (annotParam.first == "ec")
		{
			flags |= (int)ExportFlags::ExternalConstructor;

			externalClass = annotParam.second;
		}
		else if (annotParam.first == "ed")
		{
			if (annotParam.second == "true")
				flags |= (int)ExportFlags::Editor;
			else if (annotParam.second != "false")
			{
				errs() << "Unrecognized value for \"ed\" option: \"" + annotParam.second + "\" for type \"" <<
					sourceName << "\".\n";
			}
		}
		else if (annotParam.first == "ex")
		{
			if (annotParam.second == "true")
				flags |= (int)ExportFlags::Exclude;
			else if (annotParam.second != "false")
			{
				errs() << "Unrecognized value for \"ex\" option: \"" + annotParam.second + "\" for type \"" <<
					sourceName << "\".\n";
			}
		}
		else if (annotParam.first == "in")
		{
			if (annotParam.second == "true")
				flags |= (int)ExportFlags::InteropOnly;
			else if (annotParam.second != "false")
			{
				errs() << "Unrecognized value for \"in\" option: \"" + annotParam.second + "\" for type \"" <<
					sourceName << "\".\n";
			}
		}
		else
			errs() << "Unrecognized annotation attribute option: \"" + annotParam.first + "\" for type \"" <<
			sourceName << "\".\n";
	}

	return true;
}

bool parseExportAttribute(AnnotateAttr* attr, StringRef sourceName, StringRef& exportName, int& exportFlags)
{
	StringRef fileName;
	StringRef externalClass;
	CSVisibility visibility;

	return parseExportAttribute(attr, sourceName, exportName, fileName, visibility, exportFlags, externalClass);
}

std::string parseExportableBaseClass(const CXXRecordDecl* decl)
{
	std::stack<const CXXRecordDecl*> todo;
	todo.push(decl);

	while (!todo.empty())
	{
		const CXXRecordDecl* curDecl = todo.top();
		todo.pop();

		auto iter = curDecl->bases_begin();
		while (iter != curDecl->bases_end())
		{
			const CXXBaseSpecifier* baseSpec = iter;
			CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

			std::string className = baseDecl->getName();

			if (className == BUILTIN_COMPONENT_TYPE)
				continue;
			else if (className == BUILTIN_RESOURCE_TYPE)
				continue;
			else if (className == BUILTIN_SCENEOBJECT_TYPE)
				continue;

			AnnotateAttr* attr = baseDecl->getAttr<AnnotateAttr>();
			if (attr != nullptr)
			{
				StringRef sourceClassName = baseDecl->getName();
				StringRef scriptClassName = sourceClassName;
				int exportFlags;

				if (parseExportAttribute(attr, sourceClassName, scriptClassName, exportFlags))
					return sourceClassName;
			}

			todo.push(baseDecl);
			iter++;
		}
	}

	return "";
}

ScriptExportParser::ScriptExportParser(CompilerInstance* CI)
	:astContext(&(CI->getASTContext()))
{ }

bool ScriptExportParser::evaluateExpression(Expr* expr, std::string& evalValue)
{
	if (!expr->isEvaluatable(*astContext))
		return false;

	QualType type = expr->getType();
	if (type->isBuiltinType())
	{
		const BuiltinType* builtinType = type->getAs<BuiltinType>();
		switch (builtinType->getKind())
		{
		case BuiltinType::Bool:
		{
			bool result;
			expr->EvaluateAsBooleanCondition(result, *astContext);

			evalValue = result ? "true" : "false";

			return true;
		}
		case BuiltinType::Char_S:
		case BuiltinType::Char_U:
		case BuiltinType::SChar:
		case BuiltinType::Short:
		case BuiltinType::Int:
		case BuiltinType::Long:
		case BuiltinType::LongLong:
		case BuiltinType::UChar:
		case BuiltinType::UShort:
		case BuiltinType::UInt:
		case BuiltinType::ULong:
		case BuiltinType::ULongLong:
		case BuiltinType::WChar_S:
		case BuiltinType::WChar_U:
		case BuiltinType::Char16:
		case BuiltinType::Char32:
		{
			APSInt result;
			expr->EvaluateAsInt(result, *astContext);

			SmallString<5> valueStr;
			result.toString(valueStr);
			evalValue = valueStr.str().str();

			return true;
		}
		case BuiltinType::Float:
		case BuiltinType::Double:
		{
			APFloat result(0.0f);
			expr->EvaluateAsFloat(result, *astContext);

			SmallString<8> valueStr;
			result.toString(valueStr);
			evalValue = valueStr.str().str();

			return true;
		}
		default:
			return false;
		}
	}
}

std::string ScriptExportParser::convertJavadocToXMLComments(Decl* decl, const std::string& indent)
{
	comments::FullComment* comment = astContext->getCommentForDecl(decl, nullptr);
	if (comment == nullptr)
		return "";

	const comments::CommandTraits& traits = astContext->getCommentCommandTraits();

	comments::BlockCommandComment* brief = nullptr;
	comments::BlockCommandComment* returns = nullptr;
	comments::ParagraphComment* firstParagraph = nullptr;
	SmallVector<comments::ParamCommandComment*, 5> params;

	auto commentIter = comment->child_begin();
	while (commentIter != comment->child_end())
	{
		comments::Comment* childComment = *commentIter;
		int kind = childComment->getCommentKind();

		switch (kind)
		{
		case comments::Comment::CommentKind::NoCommentKind:
			break;
		case comments::Comment::CommentKind::BlockCommandCommentKind:
		{
			comments::BlockCommandComment* blockComment = cast<comments::BlockCommandComment>(childComment);
			const comments::CommandInfo *commandInfo = traits.getCommandInfo(blockComment->getCommandID());

			if (brief == nullptr && commandInfo->IsBriefCommand)
				brief = blockComment;

			if (returns == nullptr && commandInfo->IsReturnsCommand)
				returns = blockComment;

			break;
		}
		case comments::Comment::CommentKind::ParagraphCommentKind:
		{
			comments::ParagraphComment* paragraphComment = cast<comments::ParagraphComment>(childComment);

			if (!paragraphComment->isWhitespace())
			{
				if (firstParagraph == nullptr)
					firstParagraph = paragraphComment;
			}
			break;
		}
		case comments::Comment::CommentKind::ParamCommandCommentKind:
		{
			comments::ParamCommandComment* paramComment = cast<comments::ParamCommandComment>(childComment);

			if (paramComment->hasParamName() && paramComment->hasNonWhitespaceParagraph())
				params.push_back(paramComment);

			break;
		}
		default:
			errs() << "Unrecognized comment command.\n";
		}

		++commentIter;
	}

	std::stringstream output;

	auto parseParagraphComment = [&output, &indent](comments::ParagraphComment* paragraph)
	{
		auto childIter = paragraph->child_begin();
		while (childIter != paragraph->child_end())
		{
			comments::Comment* childComment = *childIter;
			int kind = childComment->getCommentKind();

			if (kind == comments::Comment::CommentKind::TextCommentKind)
			{
				comments::TextComment* textCommand = cast<comments::TextComment>(childComment);

				// TODO - Need to prettify this text. Remove all whitespace and break into lines based on char count
				StringRef trimmedText = textCommand->getText().trim();
				if (trimmedText.empty())
				{
					++childIter;
					continue;
				}

				output << indent << "// " << trimmedText.str();

				if ((childIter + 1) != paragraph->child_end())
					output << std::endl;
			}

			++childIter;
		}
	};

	if (brief != nullptr)
	{
		output << indent << "// <summary>" << std::endl;
		parseParagraphComment(brief->getParagraph());
		output << std::endl;
		output << indent << "// <summary/>" << std::endl;
	}
	else if (firstParagraph != nullptr)
	{
		output << indent << "// <summary>" << std::endl;
		parseParagraphComment(firstParagraph);
		output << std::endl;
		output << indent << "// <summary/>" << std::endl;
	}
	else
	{
		output << indent << "// <summary><summary/>" << std::endl;
	}

	for (auto& entry : params)
	{
		std::string paramName;

		if (entry->isParamIndexValid())
			paramName = entry->getParamName(comment).str();
		else
			paramName = entry->getParamNameAsWritten().str();

		output << indent << "// <param name=\"" << paramName << "\">";
		parseParagraphComment(entry->getParagraph());
		output << indent << "// <returns/>" << std::endl;
	}

	if (returns != nullptr)
	{
		output << indent << "// <returns>";
		parseParagraphComment(returns->getParagraph());
		output << indent << "// <returns/>" << std::endl;
	}

	return output.str();
}

bool ScriptExportParser::VisitEnumDecl(EnumDecl* decl)
{
	AnnotateAttr* attr = decl->getAttr<AnnotateAttr>();
	if (attr == nullptr)
		return true;

	StringRef sourceClassName = decl->getName();
	StringRef className = sourceClassName;
	StringRef fileName;
	StringRef externalClass;
	int exportFlags;
	CSVisibility visibility;

	if (!parseExportAttribute(attr, sourceClassName, className, fileName, visibility, exportFlags, externalClass))
		return true;

	QualType underlyingType = decl->getIntegerType();
	if (!underlyingType->isBuiltinType())
	{
		errs() << "Found an enum with non-builtin underlying type, skipping.\n";
		return true;
	}

	const BuiltinType* builtinType = underlyingType->getAs<BuiltinType>();

	bool explicitType = false;
	std::string enumType;
	if (builtinType->getKind() != BuiltinType::Kind::Int)
	{
		if (mapBuiltinTypeToCSType(builtinType->getKind(), enumType))
			explicitType = true;
	}

	std::string declFile = sys::path::filename(astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()));

	cppToCsTypeMap[sourceClassName] = UserTypeInfo(className, ParsedType::Enum, declFile, fileName);
	cppToCsTypeMap[sourceClassName].underlyingType = builtinType->getKind();

	std::stringstream output;

	output << convertJavadocToXMLComments(decl, "\t");
	if (visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (visibility == CSVisibility::Public)
		output << "\tpublic ";

	output << "enum " << className.str();

	if (explicitType)
		output << " : " << enumType;

	output << std::endl;
	output << "\t{" << std::endl;

	auto iter = decl->enumerator_begin();
	while (iter != decl->enumerator_end())
	{
		EnumConstantDecl* constDecl = *iter;

		AnnotateAttr* enumAttr = constDecl->getAttr<AnnotateAttr>();
		StringRef enumName = constDecl->getName();
		int enumFlags = 0;
		if (enumAttr != nullptr)
			parseExportAttribute(enumAttr, constDecl->getName(), enumName, enumFlags);

		if ((enumFlags & (int)ExportFlags::Exclude) != 0)
		{
			++iter;
			continue;
		}

		SmallString<5> valueStr;
		constDecl->getInitVal().toString(valueStr);

		output << convertJavadocToXMLComments(constDecl, "\t\t");
		output << "\t\t" << enumName.str();
		output << " = ";
		output << valueStr.str().str();

		++iter;

		if (iter != decl->enumerator_end())
			output << ",";

		output << std::endl;
	}

	output << "\t}" << std::endl;

	FileInfo& fileInfo = outputFileInfos[fileName.str()];

	EnumInfo simpleEntry;
	simpleEntry.code = output.str();

	fileInfo.enumInfos.push_back(simpleEntry);

	return true;
}

bool ScriptExportParser::VisitCXXRecordDecl(CXXRecordDecl* decl)
{
	AnnotateAttr* attr = decl->getAttr<AnnotateAttr>();
	if (attr == nullptr)
		return true;

	StringRef sourceClassName = decl->getName();
	StringRef className = sourceClassName;
	StringRef fileName;
	StringRef externalClass;
	int classExportFlags;
	CSVisibility visibility;

	if (!parseExportAttribute(attr, sourceClassName, className, fileName, visibility, classExportFlags, externalClass))
		return true;

	if ((classExportFlags & (int)ExportFlags::Plain) != 0)
	{
		StructInfo structInfo;
		structInfo.name = sourceClassName;
		structInfo.visibility = visibility;
		structInfo.inEditor = (classExportFlags & (int)ExportFlags::Editor) != 0;
		structInfo.documentation = convertJavadocToXMLComments(decl, "\t");

		std::unordered_map<FieldDecl*, std::string> defaultFieldValues;

		// Parse non-default constructors & determine default values for fields
		if (decl->hasUserDeclaredConstructor())
		{
			auto ctorIter = decl->ctor_begin();
			while (ctorIter != decl->ctor_end())
			{
				SimpleConstructorInfo ctorInfo;
				CXXConstructorDecl* ctorDecl = *ctorIter;

				if (ctorDecl->isImplicit())
				{
					++ctorIter;
					continue;
				}

				bool skippedDefaultArgument = false;
				for (auto I = ctorDecl->param_begin(); I != ctorDecl->param_end(); ++I)
				{
					ParmVarDecl* paramDecl = *I;

					VarInfo paramInfo;
					paramInfo.name = paramDecl->getName();

					std::string typeName;
					if (!parseType(paramDecl->getType(), paramInfo.type, paramInfo.flags))
					{
						errs() << "Unable to detect type for constructor parameter \"" << paramDecl->getName().str()
							<< "\". Skipping.\n";
						continue;
					}

					if (paramDecl->hasDefaultArg() && !skippedDefaultArgument)
					{
						if (!evaluateExpression(paramDecl->getDefaultArg(), paramInfo.defaultValue))
						{
							errs() << "Constructor parameter \"" << paramDecl->getName().str() << "\" has a default "
								<< "argument that cannot be constantly evaluated, ignoring it.\n";
							skippedDefaultArgument = true;
						}
					}

					ctorInfo.params.push_back(paramInfo);
				}

				std::unordered_map<FieldDecl*, ParmVarDecl*> assignments;

				// Parse initializers for assignments & default values
				for (auto I = ctorDecl->init_begin(); I != ctorDecl->init_end(); ++I)
				{
					CXXCtorInitializer* init = *I;

					if (init->isMemberInitializer())
					{
						FieldDecl* field = init->getMember();
						Expr* initExpr = init->getInit();

						// Check for constant value first
						std::string evalValue;
						if (evaluateExpression(initExpr, evalValue))
							defaultFieldValues[field] = evalValue;
						else // Check for initializers referencing parameters
						{
							Decl* varDecl = initExpr->getReferencedDeclOfCallee();

							ParmVarDecl* parmVarDecl = dyn_cast<ParmVarDecl>(varDecl);
							if (parmVarDecl != nullptr)
								assignments[field] = parmVarDecl;
						}
					}
				}

				// Parse any assignments in the function body
				// Note: Searching for trivially simple assignments only, ignoring anything else
				if (ctorDecl->hasBody())
				{
					CompoundStmt* functionBody = dyn_cast<CompoundStmt>(ctorDecl->getBody()); // Note: Not handling inner blocks
					assert(functionBody != nullptr);

					for (auto I = functionBody->child_begin(); I != functionBody->child_end(); ++I)
					{
						Stmt* stmt = *I;

						BinaryOperator* binaryOp = dyn_cast<BinaryOperator>(stmt);
						if (binaryOp == nullptr)
							continue;

						if (binaryOp->getOpcode() != BO_Assign)
							continue;

						Expr* lhsExpr = binaryOp->getLHS()->IgnoreParenCasts(); // Note: Ignoring even explicit casts
						Decl* lhsDecl;

						if (DeclRefExpr* varExpr = dyn_cast<DeclRefExpr>(lhsExpr))
							lhsDecl = varExpr->getDecl();
						else if (MemberExpr* memberExpr = dyn_cast<MemberExpr>(lhsExpr))
							lhsDecl = memberExpr->getMemberDecl();
						else
							continue;

						FieldDecl* fieldDecl = dyn_cast<FieldDecl>(lhsDecl);
						if (fieldDecl == nullptr)
							continue;

						Expr* rhsExpr = binaryOp->getRHS()->IgnoreParenCasts();
						Decl* rhsDecl = nullptr;

						if (DeclRefExpr* varExpr = dyn_cast<DeclRefExpr>(rhsExpr))
							rhsDecl = varExpr->getDecl();
						else if (MemberExpr* memberExpr = dyn_cast<MemberExpr>(rhsExpr))
							rhsDecl = memberExpr->getMemberDecl();

						ParmVarDecl* parmVarDecl = nullptr;
						if (rhsDecl != nullptr)
							parmVarDecl = dyn_cast<ParmVarDecl>(rhsDecl);

						if (parmVarDecl == nullptr)
						{
							errs() << "Found a non-trivial field assignment for field \"" << fieldDecl->getName() << "\" in"
								<< " constructor of \"" << sourceClassName << "\". Ignoring assignment.\n";
							continue;
						}

						assignments[fieldDecl] = parmVarDecl;
					}
				}

				for (auto I = decl->field_begin(); I != decl->field_end(); ++I)
				{
					auto iterFind = assignments.find(*I);
					if (iterFind == assignments.end())
						continue;

					std::string fieldName = iterFind->first->getName().str();
					std::string paramName = iterFind->second->getName().str();

					ctorInfo.fieldAssignments[fieldName] = paramName;
				}

				structInfo.ctors.push_back(ctorInfo);
				++ctorIter;
			}
		}

		for (auto I = decl->field_begin(); I != decl->field_end(); ++I)
		{
			FieldDecl* fieldDecl = *I;
			VarInfo fieldInfo;
			fieldInfo.name = fieldDecl->getName();

			auto iterFind = defaultFieldValues.find(fieldDecl);
			if (iterFind != defaultFieldValues.end())
				fieldInfo.defaultValue = iterFind->second;

			if (fieldDecl->hasInClassInitializer())
			{
				Expr* initExpr = fieldDecl->getInClassInitializer();

				std::string inClassInitValue;
				if (evaluateExpression(initExpr, inClassInitValue))
					fieldInfo.defaultValue = inClassInitValue;
			}

			std::string typeName;
			if (!parseType(fieldDecl->getType(), fieldInfo.type, fieldInfo.flags))
			{
				errs() << "Unable to detect type for field \"" << fieldDecl->getName().str() << "\" in \""
					<< sourceClassName << "\". Skipping field.\n";
				continue;
			}

			structInfo.fields.push_back(fieldInfo);
		}

		std::string declFile = sys::path::filename(astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()));
		cppToCsTypeMap[sourceClassName] = UserTypeInfo(className, ParsedType::Struct, declFile, fileName);

		FileInfo& fileInfo = outputFileInfos[fileName.str()];
		fileInfo.structInfos.push_back(structInfo);

		if (structInfo.inEditor)
			fileInfo.inEditor = true;
	}
	else
	{
		ClassInfo classInfo;
		classInfo.name = sourceClassName;
		classInfo.visibility = visibility;
		classInfo.flags = 0;
		classInfo.documentation = convertJavadocToXMLComments(decl, "\t");
		classInfo.baseClass = parseExportableBaseClass(decl);

		if ((classExportFlags & (int)ExportFlags::Editor) != 0)
			classInfo.flags |= (int)ClassFlags::Editor;

		ParsedType classType = getObjectType(decl);

		std::string declFile = sys::path::filename(astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()));

		cppToCsTypeMap[sourceClassName] = UserTypeInfo(className, classType, declFile, fileName);

		for (auto I = decl->ctor_begin(); I != decl->ctor_end(); ++I)
		{
			CXXConstructorDecl* ctorDecl = *I;

			AnnotateAttr* methodAttr = ctorDecl->getAttr<AnnotateAttr>();
			if (methodAttr == nullptr)
				continue;

			StringRef dummy0;
			StringRef dummy1;
			StringRef dummy2;
			CSVisibility dummy3;
			int methodExportFlags;

			if (!parseExportAttribute(methodAttr, dummy0, dummy1, dummy2, dummy3, methodExportFlags, externalClass))
				continue;

			MethodInfo methodInfo;
			methodInfo.sourceName = sourceClassName;
			methodInfo.scriptName = className;
			methodInfo.documentation = convertJavadocToXMLComments(ctorDecl, "\t\t");
			methodInfo.flags = (int)MethodFlags::Constructor;

			if ((methodExportFlags & (int)ExportFlags::InteropOnly))
				methodInfo.flags |= (int)MethodFlags::InteropOnly;

			bool invalidParam = false;
			bool skippedDefaultArg = false;
			for (auto J = ctorDecl->param_begin(); J != ctorDecl->param_end(); ++J)
			{
				ParmVarDecl* paramDecl = *J;
				QualType paramType = paramDecl->getType();

				VarInfo paramInfo;
				paramInfo.name = paramDecl->getName();

				if (!parseType(paramType, paramInfo.type, paramInfo.flags))
				{
					errs() << "Unable to parse parameter \"" << paramInfo.name << "\" type in \"" << sourceClassName << "\"'s constructor.\n";
					invalidParam = true;
					continue;
				}

				if (paramDecl->hasDefaultArg() && !skippedDefaultArg)
				{
					if (!evaluateExpression(paramDecl->getDefaultArg(), paramInfo.defaultValue))
					{
						errs() << "Constructor parameter \"" << paramDecl->getName().str() << "\" has a default "
							<< "argument that cannot be constantly evaluated, ignoring it.\n";
						skippedDefaultArg = true;
					}
				}

				methodInfo.paramInfos.push_back(paramInfo);
			}

			if (invalidParam)
				continue;

			classInfo.ctorInfos.push_back(methodInfo);
		}

		for (auto I = decl->method_begin(); I != decl->method_end(); ++I)
		{
			CXXMethodDecl* methodDecl = *I;

			CXXConstructorDecl* ctorDecl = dyn_cast<CXXConstructorDecl>(methodDecl);
			if (ctorDecl != nullptr)
				continue;

			if (!methodDecl->isUserProvided() || methodDecl->isImplicit())
				continue;

			AnnotateAttr* methodAttr = methodDecl->getAttr<AnnotateAttr>();
			if (methodAttr == nullptr)
				continue;

			StringRef sourceMethodName = methodDecl->getName();
			StringRef methodName = sourceMethodName;
			StringRef dummy0;
			CSVisibility dummy1;
			int methodExportFlags;

			if (!parseExportAttribute(methodAttr, sourceMethodName, methodName, dummy0, dummy1, methodExportFlags, externalClass))
				continue;

			int methodFlags = 0;

			bool isExternal = false;
			if ((methodExportFlags & (int)ExportFlags::External) != 0)
			{
				methodFlags |= (int)MethodFlags::External;
				isExternal = true;
			}

			if ((methodExportFlags & (int)ExportFlags::ExternalConstructor) != 0)
			{
				methodFlags |= (int)MethodFlags::External;
				methodFlags |= (int)MethodFlags::Constructor;

				isExternal = true;
			}

			if ((methodExportFlags & (int)ExportFlags::InteropOnly))
				methodFlags |= (int)MethodFlags::InteropOnly;

			if (methodDecl->isStatic() && !isExternal) // Note: Perhaps add a way to mark external methods as static
				methodFlags |= (int)MethodFlags::Static;

			if ((methodExportFlags & (int)ExportFlags::PropertyGetter) != 0)
				methodFlags |= (int)MethodFlags::PropertyGetter;
			else if ((methodExportFlags & (int)ExportFlags::PropertySetter) != 0)
				methodFlags |= (int)MethodFlags::PropertySetter;

			MethodInfo methodInfo;
			methodInfo.sourceName = sourceMethodName;
			methodInfo.scriptName = methodName;
			methodInfo.documentation = convertJavadocToXMLComments(methodDecl, "\t\t");
			methodInfo.flags = methodFlags;
			methodInfo.externalClass = sourceClassName;

			bool isProperty = (methodExportFlags & ((int)ExportFlags::PropertyGetter | (int)ExportFlags::PropertySetter));

			if (!isProperty)
			{
				QualType returnType = methodDecl->getReturnType();
				if (!returnType->isVoidType())
				{
					ReturnInfo returnInfo;
					if (!parseType(returnType, returnInfo.type, returnInfo.flags))
					{
						errs() << "Unable to parse return type for method \"" << sourceMethodName << "\". Skipping method.\n";
						continue;
					}

					methodInfo.returnInfo = returnInfo;
				}

				bool invalidParam = false;
				bool skippedDefaultArg = false;
				for (auto J = methodDecl->param_begin(); J != methodDecl->param_end(); ++J)
				{
					ParmVarDecl* paramDecl = *J;
					QualType paramType = paramDecl->getType();

					VarInfo paramInfo;
					paramInfo.name = paramDecl->getName();

					if (!parseType(paramType, paramInfo.type, paramInfo.flags))
					{
						errs() << "Unable to parse return type for method \"" << sourceMethodName << "\". Skipping method.\n";
						invalidParam = true;
						continue;
					}

					if (paramDecl->hasDefaultArg() && !skippedDefaultArg)
					{
						if (!evaluateExpression(paramDecl->getDefaultArg(), paramInfo.defaultValue))
						{
							errs() << "Method parameter \"" << paramDecl->getName().str() << "\" has a default "
								<< "argument that cannot be constantly evaluated, ignoring it.\n";
							skippedDefaultArg = true;
						}
					}

					methodInfo.paramInfos.push_back(paramInfo);
				}

				if (invalidParam)
					continue;
			}
			else
			{
				if ((methodExportFlags & (int)ExportFlags::PropertyGetter) != 0)
				{
					QualType returnType = methodDecl->getReturnType();
					if (returnType->isVoidType())
					{
						errs() << "Unable to create a getter for property because method \"" << sourceMethodName
							<< "\" has no return value.\n";
						continue;
					}

					// Note: I can potentially allow an output parameter instead of a return value
					if (methodDecl->param_size() > 0)
					{
						errs() << "Unable to create a getter for property because method \"" << sourceMethodName
							<< "\" has parameters.\n";
						continue;
					}

					if (!parseType(returnType, methodInfo.returnInfo.type, methodInfo.returnInfo.flags))
					{
						errs() << "Unable to parse property type for method \"" << sourceMethodName << "\". Skipping property.\n";
						continue;
					}
				}
				else // Must be setter
				{
					QualType returnType = methodDecl->getReturnType();
					if (!returnType->isVoidType())
					{
						errs() << "Unable to create a setter for property because method \"" << sourceMethodName
							<< "\" has a return value.\n";
						continue;
					}

					if (methodDecl->param_size() != 1)
					{
						errs() << "Unable to create a setter for property because method \"" << sourceMethodName
							<< "\" has more or less than one parameter.\n";
						continue;
					}

					ParmVarDecl* paramDecl = methodDecl->getParamDecl(0);

					VarInfo paramInfo;
					paramInfo.name = paramDecl->getName();

					if (!parseType(paramDecl->getType(), paramInfo.type, paramInfo.flags))
					{
						errs() << "Unable to parse property type for method \"" << sourceMethodName << "\". Skipping property.\n";
						continue;
					}

					methodInfo.paramInfos.push_back(paramInfo);
				}
			}

			if (isExternal)
			{
				ExternalMethodInfos& infos = externalMethodInfos[externalClass];
				infos.methods.push_back(methodInfo);
			}
			else
				classInfo.methodInfos.push_back(methodInfo);
		}

		// External classes are just containers for external methods, we don't need to process them
		if ((classExportFlags & (int)ExportFlags::External) == 0)
		{
			FileInfo& fileInfo = outputFileInfos[fileName.str()];
			fileInfo.classInfos.push_back(classInfo);

			if ((classInfo.flags & (int)ClassFlags::Editor) != 0)
				fileInfo.inEditor = true;
		}
	}

	return true;
}