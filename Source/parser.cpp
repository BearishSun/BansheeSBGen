#include "parser.h"
#include <cctype>

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

bool parseType(QualType type, std::string& outType, int& typeFlags, bool returnValue = false)
{
	typeFlags = 0;

	QualType realType;
	if (type->isPointerType())
	{
		realType = type->getPointeeType();
		typeFlags |= (int)TypeFlags::SrcPtr;

		if (!returnValue && !realType.isConstQualified())
			typeFlags |= (int)TypeFlags::Output;
	}
	else if (type->isReferenceType())
	{
		realType = type->getPointeeType();
		typeFlags |= (int)TypeFlags::SrcRef;

		if (!returnValue && !realType.isConstQualified())
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
				realType = specType->getArg(0).getAsType();
				typeFlags |= (int)TypeFlags::Array;
			}
		}
	}

	if (realType->isPointerType())
	{
		outs() << "Error: Only normal pointers are supported for parameter types.\n";
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
							outs() << "Error: Game object and resource types are only allowed to be referenced through handles"
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
		else
		{
			// Check for ScriptObject types (this is a special type that allows interop with manually coded script bindings)
			if(sourceTypeName == "ScriptObjectBase")
			{
				if (isSrcPointer(typeFlags))
					typeFlags |= (int)TypeFlags::ScriptObject;
				else
				{
					outs() << "Error: Found an object of type ScriptObjectBase but not passed by pointer. This is not supported. \n";
					return false;
				}
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
		outs() << "Error: Unrecognized type\n";
		return false;
	}
}

struct ParsedTypeInfo
{
	std::string name;
	int flags;
};

struct FunctionTypeInfo
{
	// Only relevant for function types
	std::vector<ParsedTypeInfo> paramTypes;
	ParsedTypeInfo returnType;
};

bool parseEventSignature(QualType type, FunctionTypeInfo& typeInfo)
{
	if (type->isStructureOrClassType())
	{
		// Check for arrays
		// Note: Not supporting nested arrays
		const TemplateSpecializationType* specType = type->getAs<TemplateSpecializationType>();
		int numArgs = 0;

		if (specType != nullptr)
			numArgs = specType->getNumArgs();

		if (numArgs > 0)
		{
			const RecordType* recordType = type->getAs<RecordType>();
			const RecordDecl* recordDecl = recordType->getDecl();

			std::string sourceTypeName = recordDecl->getName();
			std::string nsName = getNamespace(recordDecl);

			if (sourceTypeName == "Event" && nsName == "bs")
			{
				type = specType->getArg(0).getAsType();
				if(type->isFunctionProtoType())
				{
					const FunctionProtoType* funcType = type->getAs<FunctionProtoType>();

					unsigned int numParams = funcType->getNumParams();
					typeInfo.paramTypes.resize(numParams);

					for(unsigned int i = 0; i < numParams; i++)
					{
						QualType paramType = funcType->getParamType(i);
						parseType(paramType, typeInfo.paramTypes[i].name, typeInfo.paramTypes[i].flags, false);
					}

					QualType returnType = funcType->getReturnType();
					if (!returnType->isVoidType())
						parseType(returnType, typeInfo.returnType.name, typeInfo.returnType.flags, true);
					else
						typeInfo.returnType.flags = 0;
				}
			}

			return true;
		}
	}

	return false;
}

struct ParsedDeclInfo
{
	StringRef exportName;
	StringRef exportFile;
	StringRef externalClass;
	CSVisibility visibility;
	int exportFlags;
	StringRef moduleName;
};


bool parseExportAttribute(AnnotateAttr* attr, StringRef sourceName, ParsedDeclInfo& output)
{
	SmallVector<StringRef, 4> annotEntries;
	attr->getAnnotation().split(annotEntries, ',');

	if (annotEntries.size() == 0)
		return false;

	StringRef exportTypeStr = annotEntries[0];

	if (exportTypeStr != "se")
		return false;

	output.exportName = sourceName;
	output.exportFile = sourceName;
	output.visibility = CSVisibility::Public;
	output.exportFlags = 0;

	for (size_t i = 1; i < annotEntries.size(); i++)
	{
		if (annotEntries[i].empty())
			continue;

		auto annotParam = annotEntries[i].split(':');
		if (annotParam.first == "n")
			output.exportName = annotParam.second;
		else if (annotParam.first == "v")
		{
			if (annotParam.second == "public")
				output.visibility = CSVisibility::Public;
			else if (annotParam.second == "internal")
				output.visibility = CSVisibility::Internal;
			else if (annotParam.second == "private")
				output.visibility = CSVisibility::Private;
			else
				outs() << "Warning: Unrecognized value for \"v\" option: \"" + annotParam.second + "\" for type \"" <<
				sourceName << "\".\n";
		}
		else if (annotParam.first == "f")
		{
			output.exportFile = annotParam.second;
		}
		else if (annotParam.first == "pl")
		{
			if (annotParam.second == "true")
				output.exportFlags |= (int)ExportFlags::Plain;
			else if (annotParam.second != "false")
			{
				outs() << "Warning: Unrecognized value for \"pl\" option: \"" + annotParam.second + "\" for type \"" <<
					sourceName << "\".\n";
			}
		}
		else if (annotParam.first == "pr")
		{
			if (annotParam.second == "getter")
				output.exportFlags |= (int)ExportFlags::PropertyGetter;
			else if (annotParam.second == "setter")
				output.exportFlags |= (int)ExportFlags::PropertySetter;
			else
			{
				outs() << "Warning: Unrecognized value for \"pr\" option: \"" + annotParam.second + "\" for type \"" <<
					sourceName << "\".\n";
			}
		}
		else if (annotParam.first == "e")
		{
			output.exportFlags |= (int)ExportFlags::External;

			output.externalClass = annotParam.second;
		}
		else if (annotParam.first == "ec")
		{
			output.exportFlags |= (int)ExportFlags::ExternalConstructor;

			output.externalClass = annotParam.second;
		}
		else if (annotParam.first == "ed")
		{
			if (annotParam.second == "true")
				output.exportFlags |= (int)ExportFlags::Editor;
			else if (annotParam.second != "false")
			{
				outs() << "Warning: Unrecognized value for \"ed\" option: \"" + annotParam.second + "\" for type \"" <<
					sourceName << "\".\n";
			}
		}
		else if (annotParam.first == "ex")
		{
			if (annotParam.second == "true")
				output.exportFlags |= (int)ExportFlags::Exclude;
			else if (annotParam.second != "false")
			{
				outs() << "Warning: Unrecognized value for \"ex\" option: \"" + annotParam.second + "\" for type \"" <<
					sourceName << "\".\n";
			}
		}
		else if (annotParam.first == "in")
		{
			if (annotParam.second == "true")
				output.exportFlags |= (int)ExportFlags::InteropOnly;
			else if (annotParam.second != "false")
			{
				outs() << "Warning: Unrecognized value for \"in\" option: \"" + annotParam.second + "\" for type \"" <<
					sourceName << "\".\n";
			}
		}
		else if(annotParam.first == "m")
			output.moduleName = annotParam.second;
		else
			outs() << "Warning: Unrecognized annotation attribute option: \"" + annotParam.first + "\" for type \"" <<
			sourceName << "\".\n";
	}

	return true;
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
			{
				iter++;
				continue;
			}
			else if (className == BUILTIN_RESOURCE_TYPE)
			{
				iter++;
				continue;
			}
			else if (className == BUILTIN_SCENEOBJECT_TYPE)
			{
				iter++;
				continue;
			}

			AnnotateAttr* attr = baseDecl->getAttr<AnnotateAttr>();
			if (attr != nullptr)
			{
				StringRef sourceClassName = baseDecl->getName();
				ParsedDeclInfo parsedDeclInfo;

				if (parseExportAttribute(attr, sourceClassName, parsedDeclInfo))
					return sourceClassName;
			}

			todo.push(baseDecl);
			iter++;
		}
	}

	return "";
}

bool isModule(const CXXRecordDecl* decl)
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
			if (className == "Module")
				return true;

			todo.push(baseDecl);
			iter++;
		}
	}

	return false;
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
	else if(type->isEnumeralType())
	{
		const EnumType* enumType = type->getAs<EnumType>();
		const EnumDecl* enumDecl = enumType->getDecl();

		APSInt result;
		expr->EvaluateAsInt(result, *astContext);

		SmallString<5> valueStr;
		result.toString(valueStr);
		evalValue = valueStr.str().str();

		return true;
	}

	return false;
}

bool ScriptExportParser::parseJavadocComments(const Decl* decl, CommentEntry& output)
{
	comments::FullComment* comment = astContext->getCommentForDecl(decl, nullptr);
	if (comment == nullptr)
		return false;

	const comments::CommandTraits& traits = astContext->getCommentCommandTraits();

	comments::BlockCommandComment* brief = nullptr;
	comments::BlockCommandComment* returns = nullptr;
	std::vector<comments::ParagraphComment*> headerParagraphs;
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
				headerParagraphs.push_back(paragraphComment);

			break;
		}
		case comments::Comment::CommentKind::ParamCommandCommentKind:
		{
			comments::ParamCommandComment* paramComment = cast<comments::ParamCommandComment>(childComment);

			if (paramComment->hasParamName() && paramComment->hasNonWhitespaceParagraph())
				params.push_back(paramComment);

			break;
		}
		}

		++commentIter;
	}

	bool hasAnyData = false;
	auto parseParagraphComments = [&traits, &hasAnyData, this](const std::vector<comments::ParagraphComment*>& paragraphs, SmallVector<std::string, 2>& output)
	{
		auto getTrimmedText = [](const StringRef& input, std::stringstream& output)
		{
			bool lastIsSpace = false;
			for (auto& entry : input)
			{
				if (lastIsSpace)
				{
					if (entry == ' ' || entry == '\t')
						continue;

					output << entry;
					lastIsSpace = false;
				}
				else
				{
					if (entry == ' ')
						lastIsSpace = true;

					if (entry == '\t')
					{
						output << " ";
						lastIsSpace = true;
					}
					else
						output << entry;
				}
			}
		};

		int nativeDoc = 0;
		for (auto& paragraph : paragraphs)
		{
			std::stringstream paragraphText;
			std::stringstream copydocArg;
			auto childIter = paragraph->child_begin();

			bool isCopydoc = false;
			while (childIter != paragraph->child_end())
			{
				comments::Comment* childComment = *childIter;
				int kind = childComment->getCommentKind();

				if (kind == comments::Comment::CommentKind::TextCommentKind)
				{
					if (nativeDoc <= 0)
					{
						comments::TextComment* textCommand = cast<comments::TextComment>(childComment);

						StringRef text = textCommand->getText();
						if (text.empty())
						{
							++childIter;
							continue;
						}

						if (isCopydoc)
							getTrimmedText(text, copydocArg);
						else
							getTrimmedText(text, paragraphText);

						hasAnyData = true;
					}
				}
				else if (kind == comments::Comment::CommentKind::InlineCommandCommentKind)
				{
					comments::InlineCommandComment* inlineCommand = cast<comments::InlineCommandComment>(childComment);

					std::string name = inlineCommand->getCommandName(traits);
					if (name == "copydoc")
						isCopydoc = true;
					else if (name == "native")
						nativeDoc++;
					else if (name == "endnative")
						nativeDoc--;
				}

				++childIter;
			}

			if (isCopydoc)
				output.push_back("@copydoc " + copydocArg.str());
			else
			{
				std::string paragraphStr = paragraphText.str();
				StringRef trimmedText(paragraphStr.data(), paragraphStr.length());
				trimmedText = trimmedText.trim();

				if(!trimmedText.empty())
					output.push_back(trimmedText);
			}
		}
	};

	if (brief != nullptr)
		parseParagraphComments({ brief->getParagraph() }, output.brief);

	parseParagraphComments(headerParagraphs, output.brief);

	for (auto& entry : params)
	{
		CommentParamEntry paramEntry;

		if (entry->isParamIndexValid())
			paramEntry.name = entry->getParamName(comment).str();
		else
			paramEntry.name = entry->getParamNameAsWritten().str();

		parseParagraphComments({ entry->getParagraph() }, paramEntry.comments);

		output.params.push_back(paramEntry);
	}

	if (returns != nullptr)
		parseParagraphComments({ returns->getParagraph() }, output.returns);

	return hasAnyData;
}

void ScriptExportParser::parseCommentInfo(const FunctionDecl* decl, CommentInfo& commentInfo)
{
	const FunctionProtoType* ft = nullptr;
	if (decl->hasWrittenPrototype())
		ft = dyn_cast<FunctionProtoType>(decl->getType()->castAs<FunctionType>());

	CommentMethodInfo methodInfo;
	if (ft)
	{
		unsigned numParams = decl->getNumParams();
		for (unsigned i = 0; i < numParams; ++i)
		{
			std::string paramType = decl->getParamDecl(i)->getType().getAsString(astContext->getPrintingPolicy());
			methodInfo.params.push_back(paramType);
		}
	}

	commentInfo.overloads.push_back(methodInfo);
}

void ScriptExportParser::parseCommentInfo(const NamedDecl* decl, CommentInfo& commentInfo)
{
	commentInfo.isFunction = false;

	const DeclContext* context = decl->getDeclContext();
	SmallVector<const NamedDecl *, 8> contexts;

	// Collect contexts
	if(dyn_cast<NamedDecl>(context) != decl)
		contexts.push_back(decl);

	while (context && isa<NamedDecl>(context)) 
	{
		contexts.push_back(dyn_cast<NamedDecl>(context));
		context = context->getParent();
	}

	SmallVector<std::string, 2> typeName;
	for (const NamedDecl* dc : reverse(contexts))
	{
		if (const auto* nd = dyn_cast<NamespaceDecl>(dc)) 
		{
			if (!nd->isAnonymousNamespace())
				commentInfo.namespaces.push_back(nd->getDeclName().getAsString());
		}
		else if (const auto* rd = dyn_cast<RecordDecl>(dc)) 
		{
			if (rd->getIdentifier())
				typeName.push_back(rd->getDeclName().getAsString());
		}
		else if (const auto* fd = dyn_cast<FunctionDecl>(dc)) 
		{
			parseCommentInfo(fd, commentInfo);

			typeName.push_back(fd->getDeclName().getAsString());
			commentInfo.isFunction = true;
		}
		else if (const auto* ed = dyn_cast<EnumDecl>(dc)) {
			if (ed->isScoped() || ed->getIdentifier())
				typeName.push_back(ed->getDeclName().getAsString());
		}
		else 
		{
			typeName.push_back(cast<NamedDecl>(dc)->getDeclName().getAsString());
		}
	}

	std::stringstream typeNameStream;
	for(int i = 0; i < (int)typeName.size(); i++)
	{
		if (i > 0)
			typeNameStream << "::";

		typeNameStream << typeName[i];
	}

	commentInfo.name = typeNameStream.str();

	std::stringstream fullTypeNameStream;
	for(int i = 0; i < (int)commentInfo.namespaces.size(); i++)
	{
		if (i > 0)
			fullTypeNameStream << "::";

		fullTypeNameStream << commentInfo.namespaces[i];
	}

	fullTypeNameStream << "::" << commentInfo.name;
	commentInfo.fullName = fullTypeNameStream.str();
}

void ScriptExportParser::parseComments(const NamedDecl* decl, CommentInfo& commentInfo)
{
	auto iterFind = commentFullLookup.find(commentInfo.fullName);
	if (iterFind == commentFullLookup.end())
	{
		bool hasComment;
		if (commentInfo.isFunction)
			hasComment = parseJavadocComments(decl, commentInfo.overloads[0].comment);
		else
			hasComment = parseJavadocComments(decl, commentInfo.comment);

		if (!hasComment)
			return;

		commentFullLookup[commentInfo.fullName] = (int)commentInfos.size();

		SmallVector<int, 2>& entries = commentSimpleLookup[commentInfo.name];
		entries.push_back((int)commentInfos.size());

		commentInfos.push_back(commentInfo);
	}
	else if(commentInfo.isFunction) // Can be an overload
	{
		CommentInfo& existingInfo = commentInfos[iterFind->second];

		bool foundExisting = false;
		for(auto& paramInfo : commentInfo.overloads)
		{
			int numParams = paramInfo.params.size();
			if (numParams != commentInfo.overloads[0].params.size())
				continue;

			bool paramsMatch = true;
			for(int i = 0; i < numParams; i++)
			{
				if(paramInfo.params[i] != commentInfo.overloads[0].params[i])
				{
					paramsMatch = false;
					break;
				}
			}

			if(paramsMatch)
			{
				foundExisting = true;
				break;
			}
		}

		if(!foundExisting)
		{
			bool hasComment = parseJavadocComments(decl, commentInfo.overloads[0].comment);
			if (hasComment)
				existingInfo.overloads.push_back(commentInfo.overloads[0]);
		}
	}
}

void ScriptExportParser::parseComments(const CXXRecordDecl* decl)
{
	if (!decl->isCompleteDefinition())
		return;

	CommentInfo commentInfo;
	parseCommentInfo(decl, commentInfo);
	parseComments(decl, commentInfo);

	std::stack<const CXXRecordDecl*> todo;
	todo.push(decl);

	while (!todo.empty())
	{
		const CXXRecordDecl* curDecl = todo.top();
		todo.pop();

		for (auto I = curDecl->method_begin(); I != curDecl->method_end(); ++I)
		{
			if (I->isImplicit())
				continue;

			if (const auto* fd = dyn_cast<FunctionDecl>(*I))
			{
				CommentInfo methodCommentInfo;
				methodCommentInfo.isFunction = true;
				methodCommentInfo.namespaces = commentInfo.namespaces;
				methodCommentInfo.name = commentInfo.name + "::" + I->getDeclName().getAsString();
				methodCommentInfo.fullName = commentInfo.fullName + "::" + I->getDeclName().getAsString();

				parseCommentInfo(fd, methodCommentInfo);
				parseComments(fd, methodCommentInfo);
			}
		}

		auto iter = curDecl->bases_begin();
		while (iter != curDecl->bases_end())
		{
			const CXXBaseSpecifier* baseSpec = iter;
			CXXRecordDecl* baseDecl = baseSpec->getType()->getAsCXXRecordDecl();

			if(baseDecl != nullptr)
				todo.push(baseDecl);

			iter++;
		}
	}
}

bool ScriptExportParser::parseEvent(ValueDecl* decl, const std::string& className, MethodInfo& eventInfo)
{
	AnnotateAttr* fieldAttr = decl->getAttr<AnnotateAttr>();
	if (fieldAttr == nullptr)
		return false;

	StringRef sourceFieldName = decl->getName();

	ParsedDeclInfo parsedEventInfo;
	if (!parseExportAttribute(fieldAttr, sourceFieldName, parsedEventInfo))
		return false;

	FunctionTypeInfo eventSignature;
	if (!parseEventSignature(decl->getType(), eventSignature))
	{
		outs() << "Error: Exported class field \"" + sourceFieldName + "\" isn't an event. Non-event class fields cannot be exported to the script interface.";
		return false;
	}

	if (decl->getAccess() != AS_public)
		outs() << "Error: Exported event \"" + sourceFieldName + "\" isn't public. This will likely result in invalid code generation.";

	int eventFlags = 0;

	if ((parsedEventInfo.exportFlags & (int)ExportFlags::External) != 0)
	{
		outs() << "Error: External events currently not supported. Skipping export for event \"" + sourceFieldName + "\".";
		return false;
	}

	if ((parsedEventInfo.exportFlags & (int)ExportFlags::InteropOnly))
		eventFlags |= (int)MethodFlags::InteropOnly;

	eventInfo.sourceName = sourceFieldName;
	eventInfo.scriptName = parsedEventInfo.exportName;
	eventInfo.flags = eventFlags;
	eventInfo.externalClass = className;
	eventInfo.visibility = parsedEventInfo.visibility;
	parseJavadocComments(decl, eventInfo.documentation);

	if (!eventSignature.returnType.name.empty())
	{
		eventInfo.returnInfo.type = eventSignature.returnType.name;
		eventInfo.returnInfo.flags = eventSignature.returnType.flags;
	}

	for(auto& entry : eventSignature.paramTypes)
	{
		VarInfo paramInfo;
		paramInfo.flags = entry.flags;
		paramInfo.type = entry.name;

		eventInfo.paramInfos.push_back(paramInfo);
	}

	return true;
}

void parseNamespace(NamedDecl* decl, SmallVector<std::string, 4>& output)
{
	const DeclContext* context = decl->getDeclContext();
	SmallVector<const DeclContext *, 8> contexts;

	// Collect contexts.
	while (context && isa<NamedDecl>(context))
	{
		contexts.push_back(context);
		context = context->getParent();
	}

	for (const DeclContext* dc : reverse(contexts))
	{
		if (const auto* nd = dyn_cast<NamespaceDecl>(dc))
		{
			if (!nd->isAnonymousNamespace())
				output.push_back(nd->getDeclName().getAsString());
		}
	}
}

bool ScriptExportParser::VisitEnumDecl(EnumDecl* decl)
{
	CommentInfo commentInfo;
	parseCommentInfo(decl, commentInfo);
	parseComments(decl, commentInfo);

	AnnotateAttr* attr = decl->getAttr<AnnotateAttr>();
	if (attr == nullptr)
		return true;

	StringRef sourceClassName = decl->getName();
	ParsedDeclInfo parsedEnumInfo;
	parsedEnumInfo.exportName = sourceClassName;

	if (!parseExportAttribute(attr, sourceClassName, parsedEnumInfo))
		return true;

	FileInfo& fileInfo = outputFileInfos[parsedEnumInfo.exportFile.str()];
	auto iterFind = std::find_if(fileInfo.enumInfos.begin(), fileInfo.enumInfos.end(), 
		[&sourceClassName](const EnumInfo& ei)
	{
		return ei.name == sourceClassName;
	});

	if (iterFind != fileInfo.enumInfos.end())
		return true; // Already parsed

	QualType underlyingType = decl->getIntegerType();
	if (!underlyingType->isBuiltinType())
	{
		outs() << "Error: Found an enum with non-builtin underlying type, skipping.\n";
		return true;
	}

	EnumInfo enumEntry;
	enumEntry.name = sourceClassName;
	enumEntry.scriptName = parsedEnumInfo.exportName;
	enumEntry.visibility = parsedEnumInfo.visibility;
	enumEntry.module = parsedEnumInfo.moduleName;
	parseJavadocComments(decl, enumEntry.documentation);
	parseNamespace(decl, enumEntry.ns);

	const BuiltinType* builtinType = underlyingType->getAs<BuiltinType>();

	std::string enumType;
	if (builtinType->getKind() != BuiltinType::Kind::Int)
		mapBuiltinTypeToCSType(builtinType->getKind(), enumEntry.explicitType);

	std::string declFile = sys::path::filename(astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()));

	cppToCsTypeMap[sourceClassName] = UserTypeInfo(parsedEnumInfo.exportName, ParsedType::Enum, declFile, parsedEnumInfo.exportFile);
	cppToCsTypeMap[sourceClassName].underlyingType = builtinType->getKind();
	
	auto iter = decl->enumerator_begin();
	while (iter != decl->enumerator_end())
	{
		EnumConstantDecl* constDecl = *iter;

		ParsedDeclInfo parsedEnumEntryInfo;
		AnnotateAttr* enumAttr = constDecl->getAttr<AnnotateAttr>();

		StringRef entryName = constDecl->getName();
		parsedEnumEntryInfo.exportName = entryName;
		parsedEnumEntryInfo.exportFlags = 0;

		if (enumAttr != nullptr)
			parseExportAttribute(enumAttr, entryName, parsedEnumEntryInfo);

		if ((parsedEnumEntryInfo.exportFlags & (int)ExportFlags::Exclude) != 0)
		{
			++iter;
			continue;
		}

		const APSInt& entryVal = constDecl->getInitVal();

		EnumEntryInfo entryInfo;
		entryInfo.name = entryName.str();
		entryInfo.scriptName = parsedEnumEntryInfo.exportName.str();
		parseJavadocComments(constDecl, entryInfo.documentation);

		SmallString<5> valueStr;
		entryVal.toString(valueStr);
		entryInfo.value = valueStr.str();

		enumEntry.entries[(int)entryVal.getExtValue()] = entryInfo;
		++iter;
	}

	fileInfo.enumInfos.push_back(enumEntry);

	return true;
}

bool ScriptExportParser::VisitCXXRecordDecl(CXXRecordDecl* decl)
{
	parseComments(decl);

	AnnotateAttr* attr = decl->getAttr<AnnotateAttr>();
	if (attr == nullptr)
		return true;

	StringRef sourceClassName = decl->getName();

	ParsedDeclInfo parsedClassInfo;
	parsedClassInfo.exportName = sourceClassName;

	if (!parseExportAttribute(attr, sourceClassName, parsedClassInfo))
		return true;

	FileInfo& fileInfo = outputFileInfos[parsedClassInfo.exportFile.str()];
	if ((parsedClassInfo.exportFlags & (int)ExportFlags::Plain) != 0)
	{
		auto iterFind = std::find_if(fileInfo.structInfos.begin(), fileInfo.structInfos.end(), 
			[&sourceClassName](const StructInfo& si)
		{
			return si.name == sourceClassName;
		});

		if (iterFind != fileInfo.structInfos.end())
			return true; // Already parsed

		StructInfo structInfo;
		structInfo.name = sourceClassName;
		structInfo.visibility = parsedClassInfo.visibility;
		structInfo.inEditor = (parsedClassInfo.exportFlags & (int)ExportFlags::Editor) != 0;
		structInfo.requiresInterop = false;
		structInfo.module = parsedClassInfo.moduleName;

		parseJavadocComments(decl, structInfo.documentation);
		parseNamespace(decl, structInfo.ns);

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
						outs() << "Error: Unable to detect type for constructor parameter \"" << paramDecl->getName().str()
							<< "\". Skipping.\n";
						continue;
					}

					if (paramDecl->hasDefaultArg() && !skippedDefaultArgument)
					{
						if (!evaluateExpression(paramDecl->getDefaultArg(), paramInfo.defaultValue))
						{
							outs() << "Error: Constructor parameter \"" << paramDecl->getName().str() << "\" has a default "
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
							outs() << "Warning: Found a non-trivial field assignment for field \"" << fieldDecl->getName() << "\" in"
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
				outs() << "Error: Unable to detect type for field \"" << fieldDecl->getName().str() << "\" in \""
					<< sourceClassName << "\". Skipping field.\n";
				continue;
			}

			structInfo.fields.push_back(fieldInfo);
		}

		std::string declFile = sys::path::filename(astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()));
		cppToCsTypeMap[sourceClassName] = UserTypeInfo(parsedClassInfo.exportName, ParsedType::Struct, declFile, parsedClassInfo.exportFile);

		fileInfo.structInfos.push_back(structInfo);

		if (structInfo.inEditor)
			fileInfo.inEditor = true;
	}
	else
	{
		auto iterFind = std::find_if(fileInfo.classInfos.begin(), fileInfo.classInfos.end(), 
			[&sourceClassName](const ClassInfo& ci)
		{
			return ci.name == sourceClassName;
		});

		if (iterFind != fileInfo.classInfos.end())
			return true; // Already parsed

		ClassInfo classInfo;
		classInfo.name = sourceClassName;
		classInfo.visibility = parsedClassInfo.visibility;
		classInfo.flags = 0;
		classInfo.baseClass = parseExportableBaseClass(decl);
		classInfo.module = parsedClassInfo.moduleName;
		parseJavadocComments(decl, classInfo.documentation);
		parseNamespace(decl, classInfo.ns);

		if ((parsedClassInfo.exportFlags & (int)ExportFlags::Editor) != 0)
			classInfo.flags |= (int)ClassFlags::Editor;

		bool clsIsModule = isModule(decl);
		if (clsIsModule)
			classInfo.flags |= (int)ClassFlags::IsModule;

		ParsedType classType = getObjectType(decl);

		std::string declFile = sys::path::filename(astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()));

		cppToCsTypeMap[sourceClassName] = UserTypeInfo(parsedClassInfo.exportName, classType, declFile, parsedClassInfo.exportFile);

		// Parse constructors for non-module (singleton) classes
		if (!clsIsModule)
		{
			for (auto I = decl->ctor_begin(); I != decl->ctor_end(); ++I)
			{
				CXXConstructorDecl* ctorDecl = *I;

				AnnotateAttr* methodAttr = ctorDecl->getAttr<AnnotateAttr>();
				if (methodAttr == nullptr)
					continue;

				StringRef dummy;
				ParsedDeclInfo parsedMethodInfo;
				if (!parseExportAttribute(methodAttr, dummy, parsedMethodInfo))
					continue;

				MethodInfo methodInfo;
				methodInfo.sourceName = sourceClassName;
				methodInfo.scriptName = parsedClassInfo.exportName;
				methodInfo.flags = (int)MethodFlags::Constructor;
				methodInfo.visibility = parsedMethodInfo.visibility;
				parseJavadocComments(ctorDecl, methodInfo.documentation);

				if ((parsedMethodInfo.exportFlags & (int)ExportFlags::InteropOnly))
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
						outs() << "Error: Unable to parse parameter \"" << paramInfo.name << "\" type in \"" << sourceClassName << "\"'s constructor.\n";
						invalidParam = true;
						continue;
					}

					if (paramDecl->hasDefaultArg() && !skippedDefaultArg)
					{
						if (!evaluateExpression(paramDecl->getDefaultArg(), paramInfo.defaultValue))
						{
							outs() << "Error: Constructor parameter \"" << paramDecl->getName().str() << "\" has a default "
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

			ParsedDeclInfo parsedMethodInfo;
			if (!parseExportAttribute(methodAttr, sourceMethodName, parsedMethodInfo))
				continue;

			if (methodDecl->getAccess() != AS_public)
				outs() << "Error: Exported method \"" + sourceMethodName + "\" isn't public. This will likely result in invalid code generation.";

			int methodFlags = 0;

			bool isExternal = false;
			if ((parsedMethodInfo.exportFlags & (int)ExportFlags::External) != 0)
			{
				methodFlags |= (int)MethodFlags::External;
				isExternal = true;
			}

			if ((parsedMethodInfo.exportFlags & (int)ExportFlags::ExternalConstructor) != 0)
			{
				methodFlags |= (int)MethodFlags::External;
				methodFlags |= (int)MethodFlags::Constructor;

				isExternal = true;
			}

			if ((parsedMethodInfo.exportFlags & (int)ExportFlags::InteropOnly))
				methodFlags |= (int)MethodFlags::InteropOnly;

			bool isStatic = false;
			if (methodDecl->isStatic() && !isExternal) // Note: Perhaps add a way to mark external methods as static
			{
				methodFlags |= (int)MethodFlags::Static;
				isStatic = true;
			}

			if ((parsedMethodInfo.exportFlags & (int)ExportFlags::PropertyGetter) != 0)
				methodFlags |= (int)MethodFlags::PropertyGetter;
			else if ((parsedMethodInfo.exportFlags & (int)ExportFlags::PropertySetter) != 0)
				methodFlags |= (int)MethodFlags::PropertySetter;

			MethodInfo methodInfo;
			methodInfo.sourceName = sourceMethodName;
			methodInfo.scriptName = parsedMethodInfo.exportName;
			methodInfo.flags = methodFlags;
			methodInfo.externalClass = sourceClassName;
			methodInfo.visibility = parsedMethodInfo.visibility;
			parseJavadocComments(methodDecl, methodInfo.documentation);

			bool isProperty = (parsedMethodInfo.exportFlags & ((int)ExportFlags::PropertyGetter | (int)ExportFlags::PropertySetter));

			if (!isProperty)
			{
				QualType returnType = methodDecl->getReturnType();
				if (!returnType->isVoidType())
				{
					ReturnInfo returnInfo;
					if (!parseType(returnType, returnInfo.type, returnInfo.flags, true))
					{
						outs() << "Error: Unable to parse return type for method \"" << sourceMethodName << "\". Skipping method.\n";
						continue;
					}

					methodInfo.returnInfo = returnInfo;
				}
			}
			else
			{
				if ((parsedMethodInfo.exportFlags & (int)ExportFlags::PropertyGetter) != 0)
				{
					QualType returnType = methodDecl->getReturnType();
					if (returnType->isVoidType())
					{
						outs() << "Error: Unable to create a getter for property because method \"" << sourceMethodName
							<< "\" has no return value.\n";
						continue;
					}

					// Note: I can potentially allow an output parameter instead of a return value
					if (methodDecl->param_size() > 1 || ((!isExternal || isStatic) && methodDecl->param_size() > 0))
					{
						outs() << "Error: Unable to create a getter for property because method \"" << sourceMethodName
							<< "\" has parameters.\n";
						continue;
					}

					if (!parseType(returnType, methodInfo.returnInfo.type, methodInfo.returnInfo.flags, true))
					{
						outs() << "Error: Unable to parse property type for method \"" << sourceMethodName << "\". Skipping property.\n";
						continue;
					}
				}
				else // Must be setter
				{
					QualType returnType = methodDecl->getReturnType();
					if (!returnType->isVoidType())
					{
						outs() << "Error: Unable to create a setter for property because method \"" << sourceMethodName
							<< "\" has a return value.\n";
						continue;
					}

					if (methodDecl->param_size() == 0 || methodDecl->param_size() > 2 || ((!isExternal || isStatic) && methodDecl->param_size() != 1))
					{
						outs() << "Error: Unable to create a setter for property because method \"" << sourceMethodName
							<< "\" has more or less than one parameter.\n";
						continue;
					}

					ParmVarDecl* paramDecl = methodDecl->getParamDecl(0);

					VarInfo paramInfo;
					paramInfo.name = paramDecl->getName();

					if (!parseType(paramDecl->getType(), paramInfo.type, paramInfo.flags))
					{
						outs() << "Error: Unable to parse property type for method \"" << sourceMethodName << "\". Skipping property.\n";
						continue;
					}
				}
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
					outs() << "Error: Unable to parse return type for method \"" << sourceMethodName << "\". Skipping method.\n";
					invalidParam = true;
					continue;
				}

				if (paramDecl->hasDefaultArg() && !skippedDefaultArg)
				{
					if (!evaluateExpression(paramDecl->getDefaultArg(), paramInfo.defaultValue))
					{
						outs() << "Error: Method parameter \"" << paramDecl->getName().str() << "\" has a default "
							<< "argument that cannot be constantly evaluated, ignoring it.\n";
						skippedDefaultArg = true;
					}
				}

				methodInfo.paramInfos.push_back(paramInfo);
			}

			if (invalidParam)
				continue;

			if (isExternal)
			{
				ExternalClassInfos& infos = externalClassInfos[parsedMethodInfo.externalClass];
				infos.methods.push_back(methodInfo);
			}
			else
				classInfo.methodInfos.push_back(methodInfo);
		}

		// Look for exported events
		for (auto I = decl->field_begin(); I != decl->field_end(); ++I)
		{
			FieldDecl* fieldDecl = *I;

			MethodInfo eventInfo;
			if (!parseEvent(fieldDecl, sourceClassName, eventInfo))
				continue;

			classInfo.eventInfos.push_back(eventInfo);
		}

		// Find static data events
		DeclContext* context = dyn_cast<DeclContext>(decl);
		for (auto I = context->decls_begin(); I != context->decls_end(); ++I)
		{
			if(VarDecl* varDecl = dyn_cast<VarDecl>(*I))
			{
				if (!varDecl->isStaticDataMember())
					continue;

				MethodInfo eventInfo;
				if (!parseEvent(varDecl, sourceClassName, eventInfo))
					continue;

				eventInfo.flags |= (int)MethodFlags::Static;
				classInfo.eventInfos.push_back(eventInfo);
			}
		}

		// External classes are just containers for external methods, we don't need to process them directly
		if ((parsedClassInfo.exportFlags & (int)ExportFlags::External) == 0)
		{
			FileInfo& fileInfo = outputFileInfos[parsedClassInfo.exportFile.str()];
			fileInfo.classInfos.push_back(classInfo);

			if ((classInfo.flags & (int)ClassFlags::Editor) != 0)
				fileInfo.inEditor = true;
		}
	}

	return true;
}