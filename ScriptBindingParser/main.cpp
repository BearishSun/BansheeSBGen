#include "clang/AST/ASTConsumer.h"
#include "clang/Frontend/ASTConsumers.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/Frontend/CompilerInstance.h"
#include "clang/Rewrite/Frontend/FixItRewriter.h"
#include "clang/Rewrite/Frontend/FrontendActions.h"
#include "clang/StaticAnalyzer/Frontend/FrontendActions.h"
#include "clang/Tooling/CommonOptionsParser.h"
#include "clang/Tooling/Tooling.h"
#include "clang/Lex/Lexer.h"
#include "llvm/Support/Path.h"
#include "clang/AST/Decl.h"
#include "clang/AST/Comment.h"

#include <fstream>
#include <sstream>
#include <unordered_map>
#include <vector>

using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;
using namespace clang;

static cl::OptionCategory MyToolCategory("Script binding options");

const char* BUILTIN_COMPONENT_TYPE = "Component";
const char* BUILTIN_SCENEOBJECT_TYPE = "SceneObject";
const char* BUILTIN_RESOURCE_TYPE = "Resource";

enum class ParsedType
{
	Component,
	SceneObject,
	Resource,
	Class,
	Struct,
	Enum,
	Builtin,
	String,
	WString
};

enum class TypeFlags
{
	Builtin = 1 << 0,
	Output = 1 << 1,
	Array = 1 << 2,
	SrcPtr = 1 << 3,
	SrcSPtr = 1 << 4,
	SrcRef = 1 << 5,
	SrcRHandle = 1 << 6,
	SrcGHandle = 1 << 7,
	String = 1 << 8,
	WString = 1 << 9
};

enum class CSVisibility
{
	Public,
	Internal
};

enum class ExportFlags
{
	Plain = 1 << 0,
	PropertyGetter = 1 << 1,
	PropertySetter = 1 << 2,
	External = 1 << 3
};

struct CSTypeInfo
{
	CSTypeInfo() {}

	CSTypeInfo(const std::string& name, ParsedType type)
		:name(name), type(type)
	{ }

	std::string name;
	ParsedType type;
	BuiltinType::Kind underlyingType; // For enums
};

struct VarInfo
{
	std::string name;
	std::string type;
	
	std::string defaultValue;
	int flags;
};

struct ReturnInfo
{
	std::string type;
	int flags;
};

struct MethodInfo
{
	std::string sourceName;
	std::string scriptName;

	ReturnInfo returnInfo;
	std::vector<VarInfo> paramInfos;
	std::string documentation;
};

struct PropertyInfo
{
	std::string name;
	std::string type;

	std::string getter;
	std::string setter;

	int flags;
	std::string documentation;
};

struct ClassInfo
{
	std::string name;
	CSVisibility visibility;

	std::vector<PropertyInfo> propertyInfos;
	std::vector<MethodInfo> methodInfos;
	std::string documentation;
};

struct SimpleConstructorInfo
{
	std::vector<VarInfo> params;
	std::unordered_map<std::string, std::string> fieldAssignments;
};

struct SimpleTypeInfo
{
	std::string name;
	CSVisibility visibility;

	std::vector<SimpleConstructorInfo> ctors;
	std::vector<VarInfo> fields;
};

struct PlainTypeInfos
{
	std::string code;
};

struct FileInfo
{
	std::vector<ClassInfo> classInfos;
	std::vector<SimpleTypeInfo> simpleTypeInfos;
	std::vector<PlainTypeInfos> plainTypeInfos;
};

std::unordered_map<std::string, CSTypeInfo> cppToCsTypeMap;
std::string csNamespace = "BansheeEngine"; // TODO - This should be settable by command line

std::unordered_map<std::string, FileInfo> outputFileInfos;

class ScriptExportVisitor : public RecursiveASTVisitor<ScriptExportVisitor>
{
public:
	explicit ScriptExportVisitor(CompilerInstance* CI)
		:astContext(&(CI->getASTContext()))
	{
		cppToCsTypeMap["Vector2"] = CSTypeInfo("Vector2", ParsedType::Struct);
		cppToCsTypeMap["Vector3"] = CSTypeInfo("Vector3", ParsedType::Struct);
		cppToCsTypeMap["Vector4"] = CSTypeInfo("Vector4", ParsedType::Struct);
		cppToCsTypeMap["Matrix3"] = CSTypeInfo("Matrix3", ParsedType::Struct);
		cppToCsTypeMap["Matrix4"] = CSTypeInfo("Matrix4", ParsedType::Struct);
		cppToCsTypeMap["Quaternion"] = CSTypeInfo("Quaternion", ParsedType::Struct);
		cppToCsTypeMap["Radian"] = CSTypeInfo("Radian", ParsedType::Struct);
		cppToCsTypeMap["Degree"] = CSTypeInfo("Degree", ParsedType::Struct);
		cppToCsTypeMap["Color"] = CSTypeInfo("Color", ParsedType::Struct);
		cppToCsTypeMap["AABox"] = CSTypeInfo("AABox", ParsedType::Struct);
		cppToCsTypeMap["Sphere"] = CSTypeInfo("Sphere", ParsedType::Struct);
		cppToCsTypeMap["Capsule"] = CSTypeInfo("Capsule", ParsedType::Struct);
		cppToCsTypeMap["Ray"] = CSTypeInfo("Ray", ParsedType::Struct);
		cppToCsTypeMap["Vector2I"] = CSTypeInfo("Vector2I", ParsedType::Struct);
		cppToCsTypeMap["Rect2"] = CSTypeInfo("Rect2", ParsedType::Struct);
		cppToCsTypeMap["Rect2I"] = CSTypeInfo("Rect2I", ParsedType::Struct);
	}

	ParsedType getObjectType(const CXXRecordDecl* decl) const
	{
		std::stack<const CXXRecordDecl*> todo;
		todo.push(decl);

		while(!todo.empty())
		{
			const CXXRecordDecl* curDecl = todo.top();
			todo.pop();

			auto iter = curDecl->bases_begin();
			while(iter != curDecl->bases_end())
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

	bool isGameObjectOrResource(QualType type) const
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

	std::string getNamespace(const RecordDecl* decl) const
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

	bool mapBuiltinTypeToCSType(BuiltinType::Kind kind, std::string& output) const
	{
		switch(kind)
		{
		case BuiltinType::Void:
			output = "void";
			return true;
		case BuiltinType::Bool:
			output = "bool";
			return true;
		case BuiltinType::Char_S:
			output = "byte";
			return true;
		case BuiltinType::Char_U:
			output = "byte";
			return true;
		case BuiltinType::SChar:
			output = "byte";
			return true;
		case BuiltinType::Short:
			output = "short";
			return true;
		case BuiltinType::Int:
			output = "int";
			return true;
		case BuiltinType::Long:
			output = "long";
			return true;
		case BuiltinType::LongLong:
			output = "long";
			return true;
		case BuiltinType::UChar:
			output = "byte";
			return true;
		case BuiltinType::UShort:
			output = "ushort";
			return true;
		case BuiltinType::UInt:
			output = "uint";
			return true;
		case BuiltinType::ULong:
			output = "ulong";
			return true;
		case BuiltinType::ULongLong:
			output = "ulong";
			return true;
		case BuiltinType::Float:
			output = "float";
			return true;
		case BuiltinType::Double:
			output = "double";
			return true;
		case BuiltinType::WChar_S:
		case BuiltinType::WChar_U:
			output = "short";
			return true;
		case BuiltinType::Char16:
			output = "short";
			return true;
		case BuiltinType::Char32:
			output = "int";
			return true;
		default:
			break;
		}

		errs() << "Unrecognized builtin type found.";
		return false;
	}

	std::string mapBuiltinTypeToCppType(const std::string& input) const
	{
		if (input == "byte")
			return "UINT8";

		if (input == "int")
			return "INT32";

		if (input == "uint")
			return "UINT32";

		if (input == "short")
			return "INT16";

		if (input == "ushort")
			return "UINT16";

		if (input == "long")
			return "INT64";

		if (input == "ulong")
			return "UINT64";

		return input;
	}

	bool parseType(QualType type, std::string& outType, int& typeFlags)
	{
		typeFlags = 0;

		QualType realType;
		if (type->isPointerType())
		{
			realType = type->getPointeeType();
			typeFlags |= (int)TypeFlags::SrcPtr;

			if (!type.isConstQualified())
				typeFlags |= (int)TypeFlags::Output;
		}
		else if (type->isReferenceType())
		{
			realType = type->getPointeeType();
			typeFlags |= (int)TypeFlags::SrcRef;

			if (!type.isConstQualified())
				typeFlags |= (int)TypeFlags::Output;
		}
		else if(realType->isStructureOrClassType())
		{
			// Check for arrays
			// Note: Not supporting nested arrays
			const TemplateSpecializationType* specType = realType->getAs<TemplateSpecializationType>();
			int numArgs = 0;

			if (specType != nullptr)
				numArgs = specType->getNumArgs();

			if(numArgs > 0)
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
				else
					realType = type;
			}
			else
				realType = type;
		}
		else
			realType = type;

		if(realType->isPointerType())
		{
			errs() << "Only normal pointers are supported for parameter types.";
			return false;
		}

		if(realType->isBuiltinType())
		{
			const BuiltinType* builtinType = realType->getAs<BuiltinType>();
			if (!mapBuiltinTypeToCSType(builtinType->getKind(), outType))
				return false;

			typeFlags |= (int)TypeFlags::Builtin;
			return true;
		}
		else if(realType->isStructureOrClassType())
		{
			const RecordType* recordType = realType->getAs<RecordType>();
			const RecordDecl* recordDecl = recordType->getDecl();

			std::string sourceTypeName = recordDecl->getName();
			std::string nsName = getNamespace(recordDecl);

			// Handle special templated types
			const TemplateSpecializationType* specType = realType->getAs<TemplateSpecializationType>();
			if(specType != nullptr)
			{
				int numArgs = specType->getNumArgs();
				if (numArgs > 0)
				{
					QualType argType = specType->getArg(0).getAsType();
					
					bool isValid = false;
					if(argType->isBuiltinType())
					{
						const BuiltinType* builtinType = argType->getAs<BuiltinType>();
						isValid = mapBuiltinTypeToCSType(builtinType->getKind(), outType);

						typeFlags |= (int)TypeFlags::Builtin;
					}
					else if(argType->isStructureOrClassType())
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
									<< " for scripting purposes";
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

					if(isValid)
						return true;
				}
			}

			outType = sourceTypeName;
			return true;
		}
		else
		{
			errs() << "Unrecognized type";
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
						sourceName << "\".";
			}
			else if(annotParam.first == "f")
			{
				exportFile = annotParam.second;
			}
			else if(annotParam.first == "pl")
			{
				if (annotParam.second == "true")
					flags |= (int)ExportFlags::Plain;
				else if(annotParam.second != "false")
				{
					errs() << "Unrecognized value for \"pl\" option: \"" + annotParam.second + "\" for type \"" <<
						sourceName << "\".";
				}
			}
			else if(annotParam.first == "pr")
			{
				if (annotParam.second == "getter")
					flags |= (int)ExportFlags::PropertyGetter;
				else if (annotParam.second == "setter")
					flags |= (int)ExportFlags::PropertySetter;
				else
				{
					errs() << "Unrecognized value for \"pr\" option: \"" + annotParam.second + "\" for type \"" <<
						sourceName << "\".";
				}
			}
			else if(annotParam.first == "e")
			{
				flags |= (int)ExportFlags::External;

				externalClass = annotParam.second;
			}
			else
				errs() << "Unrecognized annotation attribute option: \"" + annotParam.first + "\" for type \"" <<
				sourceName << "\".";
		}

		return true;
	}

	bool evaluateExpression(Expr* expr, std::string& evalValue)
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

	std::string convertJavadocToXMLComments(Decl* decl)
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
		while(commentIter != comment->child_end())
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
				errs() << "Unrecognized comment command.";
			}

			++commentIter;
		}

		std::stringstream output;

		auto parseParagraphComment = [&output](comments::ParagraphComment* paragraph)
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
					output << trimmedText.str();
				}

				++childIter;
			}
		};

		if(brief != nullptr)
		{
			output << "<summary>" << std::endl;
			parseParagraphComment(brief->getParagraph());
			output << std::endl;
			output << "<summary/>" << std::endl;
		}
		else if(firstParagraph != nullptr)
		{
			output << "<summary>" << std::endl;
			parseParagraphComment(firstParagraph);
			output << std::endl;
			output << "<summary/>" << std::endl;
		}
		else
		{
			output << "<summary><summary/>" << std::endl;
		}

		for(auto& entry : params)
		{
			std::string paramName;

			if (entry->isParamIndexValid())
				paramName = entry->getParamName(comment).str();
			else
				paramName = entry->getParamNameAsWritten().str();

			output << "<param name=\"" << paramName << "\">";
			parseParagraphComment(entry->getParagraph());
			output << "<returns/>" << std::endl;
		}

		if(returns != nullptr)
		{
			output << "<returns>";
			parseParagraphComment(returns->getParagraph());
			output << "<returns/>" << std::endl;
		}

		return output.str();
	}

	bool VisitEnumDecl(EnumDecl* decl)
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
		if(!underlyingType->isBuiltinType())
		{
			errs() << "Found an enum with non-builtin underlying type, skipping.";
			return true;
		}

		const BuiltinType* builtinType = underlyingType->getAs<BuiltinType>();

		bool explicitType = false;
		std::string enumType;
		if(builtinType->getKind() != BuiltinType::Kind::Int)
		{
			if(mapBuiltinTypeToCSType(builtinType->getKind(), enumType))
				explicitType = true;
		}
		
		cppToCsTypeMap[sourceClassName] = CSTypeInfo(className, ParsedType::Enum);
		cppToCsTypeMap[sourceClassName].underlyingType = builtinType->getKind();

		std::stringstream output;

		if (visibility == CSVisibility::Internal)
			output << "internal ";
		else if(visibility == CSVisibility::Public)
			output << "public ";
		
		output << "enum " << className.str();

		if (explicitType)
			output << " : " << enumType;

		output << std::endl;
		output << "{" << std::endl;

		auto iter = decl->enumerator_begin();
		while(iter != decl->enumerator_end())
		{
			EnumConstantDecl* constDecl = *iter;

			SmallString<5> valueStr;
			constDecl->getInitVal().toString(valueStr);

			output << "\t" << constDecl->getName().str();
			output << " = ";
			output << valueStr.str().str();

			++iter;

			if (iter != decl->enumerator_end())
				output << ",";

			output << std::endl;
		}

		output << "}" << std::endl;

		FileInfo& fileInfo = outputFileInfos[fileName.str()];

		PlainTypeInfos simpleEntry;
		simpleEntry.code = output.str();

		fileInfo.plainTypeInfos.push_back(simpleEntry);

		return true;
	}

	bool VisitCXXRecordDecl(CXXRecordDecl* decl)
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

		if ((exportFlags & (int)ExportFlags::Plain) != 0)
		{
			SimpleTypeInfo simpleTypeInfo;
			simpleTypeInfo.name = sourceClassName;
			simpleTypeInfo.visibility = visibility;
			
			std::unordered_map<FieldDecl*, std::string> defaultFieldValues;

			// Parse non-default constructors & determine default values for fields
			if(decl->hasUserDeclaredConstructor())
			{
				auto ctorIter = decl->ctor_begin();
				while(ctorIter != decl->ctor_end())
				{
					SimpleConstructorInfo ctorInfo;
					CXXConstructorDecl* ctorDecl = *ctorIter;

					bool skippedDefaultArgument = false;
					for(auto I = ctorDecl->param_begin(); I != ctorDecl->param_end(); ++I)
					{
						ParmVarDecl* paramDecl = *I;

						VarInfo paramInfo;
						paramInfo.name = paramDecl->getName();						

						std::string typeName;
						if(!parseType(paramDecl->getType(), paramInfo.type, paramInfo.flags))
						{
							errs() << "Unable to detect type for constructor parameter \"" << paramDecl->getName().str() 
								<< "\". Skipping.";
							continue;
						}

						if(paramDecl->hasDefaultArg() && !skippedDefaultArgument)
						{
							if(!evaluateExpression(paramDecl->getDefaultArg(), paramInfo.defaultValue))
							{
								errs() << "Constructor parameter \"" << paramDecl->getName().str() << "\" has a default "
									<< "argument that cannot be constantly evaluated, ignoring it.";
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
					CompoundStmt* functionBody = dyn_cast<CompoundStmt>(ctorDecl->getBody()); // Note: Not handling inner blocks
					assert(functionBody != nullptr);

					for(auto I = functionBody->child_begin(); I != functionBody->child_end(); ++I)
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
						if(rhsDecl != nullptr)
							parmVarDecl = dyn_cast<ParmVarDecl>(rhsDecl);

						if (parmVarDecl == nullptr)
						{
							errs() << "Found a non-trivial field assignment for field \"" << fieldDecl->getName() << "\" in"
								<< " constructor of \"" << sourceClassName << "\". Ignoring assignment.";
							continue;
						}

						assignments[fieldDecl] = parmVarDecl;
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

					simpleTypeInfo.ctors.push_back(ctorInfo);
					++ctorIter;
				}
			}

			for(auto I = decl->field_begin(); I != decl->field_end(); ++I)
			{
				FieldDecl* fieldDecl = *I;
				VarInfo fieldInfo;
				fieldInfo.name = fieldDecl->getName();

				auto iterFind = defaultFieldValues.find(fieldDecl);
				if(iterFind != defaultFieldValues.end())
					fieldInfo.defaultValue = iterFind->second;

				if(fieldDecl->hasInClassInitializer())
				{
					Expr* initExpr = fieldDecl->getInClassInitializer();

					std::string inClassInitValue;
					if (evaluateExpression(initExpr, inClassInitValue))
						fieldInfo.defaultValue = inClassInitValue;
				}

				std::string typeName;
				if(!parseType(fieldDecl->getType(), fieldInfo.type, fieldInfo.flags))
				{
					errs() << "Unable to detect type for field \"" << fieldDecl->getName().str() << "\" in \"" 
						   << sourceClassName << "\". Skipping field.";
					continue;
				}
			}

			cppToCsTypeMap[sourceClassName] = CSTypeInfo(className, ParsedType::Struct);

			FileInfo& fileInfo = outputFileInfos[fileName.str()];
			fileInfo.simpleTypeInfos.push_back(simpleTypeInfo);
		}
		else
		{
			ClassInfo classInfo;
			classInfo.name = sourceClassName;
			classInfo.visibility = visibility;
			
			ParsedType classType = getObjectType(decl);
			cppToCsTypeMap[sourceClassName] = CSTypeInfo(className, classType);

			// TODO - Iterate over constructors as well

			for (auto I = decl->method_begin(); I != decl->method_end(); ++I)
			{
				CXXMethodDecl* methodDecl = *I;

				StringRef sourceMethodName = methodDecl->getName();
				StringRef methodName = sourceMethodName;
				StringRef fileName;
				StringRef externalClass;
				int exportFlags;
				CSVisibility visibility;

				if (!parseExportAttribute(attr, sourceMethodName, className, fileName, visibility, exportFlags, externalClass))
					continue;

				bool isProperty = (exportFlags & ((int)ExportFlags::PropertyGetter | (int)ExportFlags::PropertySetter));

				if (!isProperty)
				{
					MethodInfo methodInfo;
					methodInfo.sourceName = sourceMethodName;
					methodInfo.scriptName = methodName;
					methodInfo.documentation = convertJavadocToXMLComments(methodDecl);

					QualType returnType = methodDecl->getReturnType();
					if (!returnType->isVoidType())
					{
						ReturnInfo returnInfo;
						if (!parseType(returnType, returnInfo.type, returnInfo.flags))
						{
							errs() << "Unable to parse return type for method \"" << sourceMethodName << "\". Skipping method.";
							continue;
						}

						methodInfo.returnInfo = returnInfo;
					}

					bool invalidParam = false;
					for (auto J = methodDecl->param_begin(); J != methodDecl->param_end(); ++J)
					{
						ParmVarDecl* paramDecl = *J;
						QualType paramType = paramDecl->getType();

						VarInfo paramInfo;
						paramInfo.name = paramDecl->getName();

						if (!parseType(paramType, paramInfo.type, paramInfo.flags))
						{
							errs() << "Unable to parse return type for method \"" << sourceMethodName << "\". Skipping method.";
							invalidParam = true;
							continue;
						}

						methodInfo.paramInfos.push_back(paramInfo);
					}

					if (invalidParam)
						continue;

					classInfo.methodInfos.push_back(methodInfo);
				}
				else
				{
					PropertyInfo propertyInfo;
					propertyInfo.name = methodName;
					propertyInfo.documentation = convertJavadocToXMLComments(methodDecl);

					if(exportFlags & ((int)ExportFlags::PropertyGetter) != 0)
					{
						propertyInfo.getter = sourceMethodName;

						QualType returnType = methodDecl->getReturnType();
						if (returnType->isVoidType())
						{
							errs() << "Unable to create a getter for property because method \"" << sourceMethodName 
								<< "\" has no return value.";
							continue;
						}

						// Note: I can potentially allow an output parameter instead of a return value
						if(methodDecl->param_size() > 0)
						{
							errs() << "Unable to create a getter for property because method \"" << sourceMethodName
								<< "\" has parameters.";
							continue;
						}

						if (!parseType(returnType, propertyInfo.type, propertyInfo.flags))
						{
							errs() << "Unable to parse property type for property \"" << propertyInfo.name << "\". Skipping property.";
							continue;
						}
					}
					else // Must be setter
					{
						propertyInfo.setter = sourceMethodName;

						QualType returnType = methodDecl->getReturnType();
						if (!returnType->isVoidType())
						{
							errs() << "Unable to create a setter for property because method \"" << sourceMethodName
								<< "\" has a return value.";
							continue;
						}

						if (methodDecl->param_size() != 1)
						{
							errs() << "Unable to create a setter for property because method \"" << sourceMethodName
								<< "\" has more or less than one parameter.";
							continue;
						}

						ParmVarDecl* paramDecl = methodDecl->getParamDecl(0);
						if (!parseType(paramDecl->getType(), propertyInfo.type, propertyInfo.flags))
						{
							errs() << "Unable to parse property type for property \"" << propertyInfo.name << "\". Skipping property.";
							continue;
						}
					}

					auto iterFind = std::find_if(classInfo.propertyInfos.begin(), classInfo.propertyInfos.end(),
												 [&propertyInfo](const PropertyInfo& info)
					{
						return propertyInfo.name == info.name;
					});

					if (iterFind == classInfo.propertyInfos.end())
						classInfo.propertyInfos.push_back(propertyInfo);
					else
					{
						PropertyInfo& existingInfo = *iterFind;
						if(existingInfo.type != propertyInfo.type)
						{
							errs() << "Getter and setter types for the property \"" << propertyInfo.name << "\" don't match. Skipping property.";
							continue;
						}

						if (!propertyInfo.getter.empty())
						{
							existingInfo.getter = propertyInfo.getter;

							// Prefer documentation from setter, but use getter if no other available
							if (existingInfo.documentation.empty())
								existingInfo.documentation = propertyInfo.documentation;
						}
						else
						{
							existingInfo.setter = propertyInfo.setter;

							if (!propertyInfo.documentation.empty())
								existingInfo.documentation = propertyInfo.documentation;
						}
					}
				}
			}

			FileInfo& fileInfo = outputFileInfos[fileName.str()];
			fileInfo.classInfos.push_back(classInfo);
		}
	
		return true;
	}

	bool getMappedType(const std::string& sourceType, int flags, std::string& outType) const
	{
		CSTypeInfo typeInfo;
		bool retVal = getMappedType(sourceType, flags, typeInfo);

		outType = typeInfo.name;
		return retVal;
	}

	bool getMappedType(const std::string& sourceType, int flags, CSTypeInfo& outType) const
	{
		if ((flags & (int)TypeFlags::Builtin) != 0)
		{
			outType.name = mapBuiltinTypeToCppType(sourceType);
			outType.type = ParsedType::Builtin;
			return true;
		}

		if ((flags & (int)TypeFlags::String) != 0)
		{
			outType.name = "String";
			outType.type = ParsedType::String;
			return true;
		}

		if ((flags & (int)TypeFlags::WString) != 0)
		{
			outType.name = "WString";
			outType.type = ParsedType::WString;
			return true;
		}

		auto iterFind = cppToCsTypeMap.find(sourceType);
		if (iterFind == cppToCsTypeMap.end())
		{
			outType.name = mapBuiltinTypeToCppType(sourceType);
			outType.type = ParsedType::Builtin;

			errs() << "Unable to map type \"" << sourceType << "\". Assuming same name as source. ";
			return false;
		}

		outType = iterFind->second;
		return true;
	}

	bool isOutput(int flags) const
	{
		return (flags & (int)TypeFlags::Output) != 0;
	}

	bool isArray(int flags) const
	{
		return (flags & (int)TypeFlags::Array) != 0;
	}

	bool isSrcPointer(int flags) const
	{
		return (flags & (int)TypeFlags::SrcPtr) != 0;
	}

	bool isSrcReference(int flags) const
	{
		return (flags & (int)TypeFlags::SrcRef) != 0;
	}

	bool isSrcValue(int flags) const
	{
		int nonValueFlags = (int)TypeFlags::SrcPtr | (int)TypeFlags::SrcRef | (int)TypeFlags::SrcSPtr | 
			(int)TypeFlags::SrcRHandle | (int)TypeFlags::SrcGHandle;

		return (flags & nonValueFlags) == 0;
	}

	bool isSrcSPtr(int flags) const
	{
		return (flags & (int)TypeFlags::SrcSPtr) != 0;
	}

	bool isSrcRHandle(int flags) const
	{
		return (flags & (int)TypeFlags::SrcRHandle) != 0;
	}

	bool isSrcGHandle(int flags) const
	{
		return (flags & (int)TypeFlags::SrcGHandle) != 0;
	}

	bool isHandleType(ParsedType type)
	{
		return type == ParsedType::Resource || type == ParsedType::SceneObject || type == ParsedType::Component;
	}

	bool canBeReturned(ParsedType type, int flags)
	{
		if (isOutput(flags))
			return false;

		if (isArray(flags))
			return true;

		if (type == ParsedType::Struct)
			return false;

		return true;
	}

	std::string getInteropParamCppType(const CSTypeInfo& type, int flags)
	{
		if(isArray(flags))
		{
			if (isOutput(flags))
				return "MonoArray**";
			else
				return "MonoArray*";
		}

		switch(type.type)
		{
		case ParsedType::Builtin:
		case ParsedType::Enum:
			if (isOutput(flags))
				return type.name + "*";
			else
				return type.name;
		case ParsedType::Struct:
			return type.name + "*";
		case ParsedType::String:
		case ParsedType::WString:
			if (isOutput(flags))
				return "MonoString**";
			else
				return "MonoString*";
		default:
			if (isOutput(flags))
				return "MonoObject**";
			else
				return "MonoObject*";
		}
	}

	std::string getCppVarType(const std::string& typeName, ParsedType type)
	{
		if (type == ParsedType::Resource)
			return "ResourceHandle<" + typeName + ">";
		else if (type == ParsedType::SceneObject || type == ParsedType::Component)
			return "GameObjectHandle<" + typeName + ">";
		else if (type == ParsedType::Class)
			return "SPtr<" + typeName + ">";
		else
			return typeName;
	}

	std::string getAsArgument(const std::string& name, ParsedType type, int flags, const std::string& methodName)
	{
		auto getArgumentPlain = [&](bool isPtr)
		{
			assert(!isSrcRHandle(flags) && !isSrcGHandle(flags) && !isSrcSPtr(flags));

			if (isSrcPointer(flags))
				return (isPtr ? "" : "&") + name;
			else if (isSrcReference(flags) || isSrcValue(flags))
				return (isPtr ? "*" : "") + name;
			else
			{
				errs() << "Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".";
				return name;
			}
		};

		switch (type)
		{
		case ParsedType::Builtin:
		case ParsedType::Enum: // Input type is either value or pointer depending or output or not
			return getArgumentPlain(isOutput(flags));
		case ParsedType::Struct: // Input type is always a pointer
			return getArgumentPlain(true);
		case ParsedType::String:
		case ParsedType::WString: // Input type is always a value
			return getArgumentPlain(false);
		case ParsedType::Component: // Input type is always a handle
		case ParsedType::SceneObject:
		case ParsedType::Resource:
		{
			if (isSrcPointer(flags))
				return name + ".get()";
			else if (isSrcReference(flags) || isSrcValue(flags))
				return "*" + name;
			else if (isSrcSPtr(flags))
				return name + ".getInternalPtr()";
			else if (isSrcRHandle(flags) || isSrcGHandle(flags))
				return name;
			else
			{
				errs() << "Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".";
				return name;
			}
		}
		case ParsedType::Class: // Input type is always a SPtr
		{
			assert(!isSrcRHandle(flags) && !isSrcGHandle(flags));

			if (isSrcPointer(flags))
				return name + ".get()";
			else if (isSrcReference(flags) || isSrcValue(flags))
				return "*" + name;
			else if (isSrcSPtr(flags))
				return name;
			else
			{
				errs() << "Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".";
				return name;
			}

		}
		default: // Some object type
			assert(false);
			return "";
		}
	}
	
	std::string getScriptInteropType(const std::string& name)
	{
		auto iterFind = cppToCsTypeMap.find(name);
		if (iterFind == cppToCsTypeMap.end())
			errs() << "Type \"" << name << "\" referenced as a script interop type, but no script interop mapping found. Assuming default type name.";

		bool isValidInteropType = iterFind->second.type != ParsedType::Builtin &&
			iterFind->second.type != ParsedType::Enum &&
			iterFind->second.type != ParsedType::String &&
			iterFind->second.type != ParsedType::WString;

		if(!isValidInteropType)
			errs() << "Type \"" << name << "\" referenced as a script interop type, but script interop object cannot be generated for this object type.";

		return "Script" + name;
	}

	std::string generateOutput(SimpleTypeInfo& input)
	{
		std::stringstream output;

		if (input.visibility == CSVisibility::Internal)
			output << "internal ";
		else if (input.visibility == CSVisibility::Public)
			output << "public ";

		std::string scriptName = cppToCsTypeMap[input.name].name;

		output << "partial struct " << scriptName;

		output << std::endl;
		output << "{" << std::endl;

		for(auto& entry : input.ctors)
		{
			output << "\tpublic " << scriptName << "(";

			for (auto I = entry.params.begin(); I != entry.params.end(); ++I)
			{
				const VarInfo& paramInfo = *I;

				// TODO - Check for output, array, and complex types (they're not allowed)

				std::string typeName;
				getMappedType(paramInfo.type, paramInfo.flags, typeName);
				
				output << typeName << " " << paramInfo.name;

				if(!paramInfo.defaultValue.empty())
					output << " = " << paramInfo.defaultValue;

				if((I + 1) != entry.params.end())
					output << ", ";
			}

			output << ")" << std::endl;
			output << "\t{" << std::endl;

			for (auto I = entry.params.begin(); I != entry.params.end(); ++I)
			{
				// TODO - Check for output, array, and complex types (they're not allowed)

				auto iterFind = entry.fieldAssignments.find(I->name);
				if (iterFind == entry.fieldAssignments.end())
					continue;

				std::string fieldName = iterFind->first;
				std::string paramName = iterFind->second;

				output << "\t\t" << fieldName << " = " << paramName << ";" << std::endl;
			}

			output << "\t}" << std::endl;
			output << std::endl;
		}

		for (auto I = input.fields.begin(); I != input.fields.end(); ++I)
		{
			const VarInfo& fieldInfo = *I;

			// TODO - Check for output, array, and complex types (they're not allowed)

			std::string typeName;
			getMappedType(fieldInfo.type, fieldInfo.flags, typeName);

			output << "\tpublic ";
			output << typeName << " ";
			output << fieldInfo.name;

			if (!fieldInfo.defaultValue.empty())
				output << " = " << fieldInfo.defaultValue;

			output << ";" << std::endl;
		}

		output << "}" << std::endl;
		return output.str();
	}

	std::string generateCppMethodSignature(const MethodInfo& methodInfo, const std::string& nestedName)
	{
		std::stringstream output;

		bool returnAsParameter = false;
		if (methodInfo.returnInfo.type.empty())
			output << "void";
		else
		{
			CSTypeInfo returnTypeInfo;
			getMappedType(methodInfo.returnInfo.type, methodInfo.returnInfo.flags, returnTypeInfo);
			if (!canBeReturned(returnTypeInfo.type, methodInfo.returnInfo.flags))
			{
				output << "void";
				returnAsParameter = true;
			}
			else
			{
				output << getInteropParamCppType(returnTypeInfo, methodInfo.returnInfo.flags);
			}
		}

		output << " ";

		if (!nestedName.empty())
			output << nestedName << "::";

		output << "Internal_" << methodInfo.scriptName << "(";

		for(auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
		{
			CSTypeInfo paramTypeInfo;
			getMappedType(I->type, I->flags, paramTypeInfo);

			output << getInteropParamCppType(paramTypeInfo, I->flags) << " " << I->name;

			if ((I + 1) != methodInfo.paramInfos.end() || returnAsParameter)
				output << ", ";
		}

		if(returnAsParameter)
		{
			CSTypeInfo returnTypeInfo;
			getMappedType(methodInfo.returnInfo.type, methodInfo.returnInfo.flags, returnTypeInfo);

			output << getInteropParamCppType(returnTypeInfo, methodInfo.returnInfo.flags) << " " << "__output";
		}

		output << ")";
		return output.str();
	}

	std::string generateNativeToScriptObjectLine(ParsedType type, const std::string& scriptType, const std::string& scriptName,
												 const std::string& argName, const std::string& indent = "\t")
	{
		std::stringstream output;

		output << indent << scriptType << "* " << scriptName << ";" << std::endl;

		if (type == ParsedType::Resource)
		{
			output << indent << "ScriptResourceManager::instance().getScriptResource(" << argName << ", &" <<
				scriptName << ", true);" << std::endl;
		}
		else if (type == ParsedType::Component)
		{
			output << indent << scriptName << " = ScriptGameObjectManager::instance().getBuiltinScriptComponent(" <<
				argName << ");" << std::endl;
		}
		else if (type == ParsedType::SceneObject)
		{
			output << indent << scriptName << " = ScriptGameObjectManager::instance().getOrCreateScriptSceneObject(" <<
				argName << ");" << std::endl;
		}
		else if (type == ParsedType::Class)
		{
			output << indent << scriptName << " = " << scriptType << "::create(" << argName << ");" << std::endl;
		}
		else
			assert(false);

		return output.str();
	}

	void generateMethodBodyBlockForParam(const std::string& name, const std::string& type, int flags, const std::string& methodName, bool isLast,
										 std::stringstream& preCallActions, std::stringstream& methodArgs, std::stringstream& postCallActions)
	{
		CSTypeInfo paramTypeInfo;
		getMappedType(type, flags, paramTypeInfo);

		if (!isArray(flags))
		{
			std::string argName;

			switch (paramTypeInfo.type)
			{
			case ParsedType::Builtin:
			case ParsedType::Enum:
			case ParsedType::Struct:
				argName = name;
				break;
			case ParsedType::String:
			{
				argName = "tmp" + name;
				preCallActions << "\tString " << argName << ";" << std::endl;

				if (!isOutput(flags))
					preCallActions << "\t" << argName << "MonoUtil::monoToString(" << name << ");" << std::endl;
				else
					postCallActions << "\t" << name << " = " << "MonoUtil::stringToMono(" << argName << ");";

				if (!isLast)
				{
					preCallActions << std::endl;
					postCallActions << std::endl;
				}
			}
			break;
			case ParsedType::WString:
			{
				argName = "tmp" + name;
				preCallActions << "\tWString " << argName << ";" << std::endl;

				if (!isOutput(flags))
					preCallActions << "\t" << argName << "MonoUtil::monoToWString(" << name << ");" << std::endl;
				else
					postCallActions << "\t" << name << " = " << "MonoUtil::wstringToMono(" << argName << ");";

				if (!isLast)
				{
					preCallActions << std::endl;
					postCallActions << std::endl;
				}
			}
			break;
			default: // Some object type
			{
				argName = "tmp" + name;
				std::string tmpType = getCppVarType(type, paramTypeInfo.type);

				preCallActions << "\t" << tmpType << " " << argName << ";" << std::endl;

				if (!isOutput(flags))
				{
					std::string scriptName = "script" + name;
					std::string scriptType = getScriptInteropType(type);

					preCallActions << "\t" << scriptType << "* " << scriptName << ";" << std::endl;
					preCallActions << "\t" << scriptName << " = " << scriptType << "::toNative(" << name << ");" << std::endl;

					if (isHandleType(paramTypeInfo.type))
						preCallActions << "\t" << argName << " = " << scriptName << "->getHandle();";
					else
						preCallActions << "\t" << argName << " = " << scriptName << "->getInternal();";

					if (!isLast)
						preCallActions << std::endl;
				}
				else
				{
					std::string scriptName = "script" + name;
					std::string scriptType = getScriptInteropType(type);

					postCallActions << "\t" << scriptType << "* " << scriptName << ";" << std::endl;
					postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.type, scriptType, scriptName, argName);
					postCallActions << "\t*" << name << " = " << scriptName << "->getMangedInstance();" << std::endl;

					if (!isLast)
					{
						preCallActions << std::endl;
						postCallActions << std::endl;
					}
				}
			}
			break;
			}

			methodArgs << getAsArgument(argName, paramTypeInfo.type, flags, methodName);

			if (!isLast)
				methodArgs << ", ";
		}
		else
		{
			std::string entryType;
			switch (paramTypeInfo.type)
			{
			case ParsedType::Builtin:
			case ParsedType::String:
			case ParsedType::WString:
			case ParsedType::Enum:
				entryType = type;
				break;
			default: // Some object or struct type
				entryType = getScriptInteropType(type);
				break;
			}

			std::string argType = "Vector<" + paramTypeInfo.name + ">";
			std::string argName = "vec" + name;
			preCallActions << "\t" << argType << " " << argName << ";" << std::endl;

			methodArgs << getAsArgument(argName, ParsedType::Builtin, flags, methodName);

			if (!isOutput(flags))
			{
				std::string arrayName = "array" + name;
				preCallActions << "\tScriptArray " << arrayName << "(" << name << ");" << std::endl;
				preCallActions << "\tfor(int i = 0; i < " << arrayName << ".size(); i++)" << std::endl;
				preCallActions << "\t{" << std::endl;

				switch (paramTypeInfo.type)
				{
				case ParsedType::Builtin:
				case ParsedType::String:
				case ParsedType::WString:
					preCallActions << "\t\t" << argName << "[i] = " << arrayName << ".get<" << entryType << ">(i);" << std::endl;
					break;
				case ParsedType::Enum:
				{
					std::string csType;
					mapBuiltinTypeToCSType(paramTypeInfo.underlyingType, csType);
					std::string enumType = mapBuiltinTypeToCppType(csType);

					preCallActions << "\t\t" << argName << "[i] = (" << entryType << ")" << arrayName << ".get<" << enumType << ">(i);" << std::endl;
					break;
				}
				case ParsedType::Struct:
					preCallActions << "\t\t" << argName << "[i] = " << entryType << "::unbox(" << arrayName << ".get<MonoObject*>(i));" << std::endl;
					break;
				default: // Some object type
				{
					std::string scriptName = "script" + name;
					preCallActions << "\t\t" << entryType << "* " << scriptName << ";" << std::endl;
					preCallActions << "\t\t" << scriptName << " = " << entryType << "::toNative(" << arrayName << ".get<MonoObject*>(i));" << std::endl;
					preCallActions << "\t\tif(scriptName != nullptr)" << std::endl;

					if (isHandleType(paramTypeInfo.type))
						preCallActions << "\t\t\t" << argName << "[i] = " << scriptName << "->getHandle();";
					else
						preCallActions << "\t\t\t" << argName << "[i] = " << scriptName << "->getInternal();";
				}
				break;
				}

				preCallActions << "\t}" << std::endl;

				if (!isLast)
					preCallActions << std::endl;
			}
			else
			{
				std::string arrayName = "array" + name;
				postCallActions << "\tScriptArray " << arrayName << ";" << std::endl;
				postCallActions << "\t" << arrayName << " = " << "ScriptArray::create<" << entryType << ">((int)" << argName << ".size())";
				postCallActions << "\tfor(int i = 0; i < (int)" << argName << ".size(); i++)" << std::endl;
				postCallActions << "\t{" << std::endl;

				switch (paramTypeInfo.type)
				{
				case ParsedType::Builtin:
				case ParsedType::String:
				case ParsedType::WString:
					postCallActions << "\t\t" << arrayName << ".set(i, " << argName << "[i]);" << std::endl;
					break;
				case ParsedType::Enum:
				{
					std::string csType;
					mapBuiltinTypeToCSType(paramTypeInfo.underlyingType, csType);
					std::string enumType = mapBuiltinTypeToCppType(csType);

					postCallActions << "\t\t" << arrayName << ".set(i, (" << enumType << ")" << argName << "[i]);" << std::endl;
					break;
				}
				case ParsedType::Struct:
					postCallActions << "\t\t" << arrayName << ".set(i, " << entryType << "::box(" << argName << "[i]));" << std::endl;
					break;
				default: // Some object type
				{
					std::string scriptName = "script" + name;

					postCallActions << "\t\t" << entryType << "* " << scriptName << ";" << std::endl;
					postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.type, entryType, scriptName, argName + "[i]", "\t\t");
					postCallActions << "\t\t" << arrayName << ".set(i, " << scriptName << "->getMangedInstance());" << std::endl;
				}
				break;
				}

				postCallActions << "\t}" << std::endl;
				postCallActions << "\t*" << name << " = " << arrayName << ".getInternal()";

				if (!isLast)
				{
					preCallActions << std::endl;
					postCallActions << std::endl;
				}
			}

			if (!isLast)
				methodArgs << ", ";
		}
	}

	std::string generateCppMethodBody(const MethodInfo& methodInfo, ParsedType classType)
	{
		std::string returnValue;

		bool returnAsParameter = false;
		if (methodInfo.returnInfo.type.empty())
			returnValue = "void";
		else
		{
			CSTypeInfo returnTypeInfo;
			getMappedType(methodInfo.returnInfo.type, methodInfo.returnInfo.flags, returnTypeInfo);
			if (!canBeReturned(returnTypeInfo.type, methodInfo.returnInfo.flags))
			{
				returnValue = "void";
				returnAsParameter = true;
			}
			else
			{
				// TODO - Parse return value and assign returnValue
			}
		}

		std::stringstream preCallActions;
		std::stringstream methodArgs;
		std::stringstream postCallActions;
		for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
		{
			bool isLast = (I + 1) == methodInfo.paramInfos.end() && !returnAsParameter;

			generateMethodBodyBlockForParam(I->name, I->type, I->flags, methodInfo.sourceName, isLast,
				preCallActions, methodArgs, postCallActions);
		}

		if(returnAsParameter)
		{
			// TODO - Won't work as different way of calling the method must be done

			generateMethodBodyBlockForParam("__output", methodInfo.returnInfo.type, methodInfo.returnInfo.flags, 
											methodInfo.sourceName, true, preCallActions, methodArgs, postCallActions);
		}

		std::stringstream output;
		output << "{" << std::endl;
		output << preCallActions.str();

		// TODO - Return value & method call
		if (classType == ParsedType::Class)
		{

		}
		else // Must be one of the handle types
		{
			assert(isHandleType(classType));
		}

		output << postCallActions.str();

		output << "}" << std::endl;
		return output.str();
	}

	std::string generateHeaderOutput(const ClassInfo& classInfo)
	{
		// TODO - Handle inheritance (need to generate an intermediate class)
	}

private:
	ASTContext* astContext;
};

class ScriptExportConsumer : public ASTConsumer 
{
public:
	explicit ScriptExportConsumer(CompilerInstance* CI)
		: visitor(new ScriptExportVisitor(CI))
	{ }

	~ScriptExportConsumer()
	{
		delete visitor;
	}

	void HandleTranslationUnit(ASTContext& Context) override
	{
		visitor->TraverseDecl(Context.getTranslationUnitDecl());

		auto comments = Context.getRawCommentList().getComments();
		size_t numComments = comments.size();

		outs() << "Num comments: " << numComments;
	}

private:
	ScriptExportVisitor *visitor;
};

class ScriptExportFrontendAction : public ASTFrontendAction 
{
public:
	std::unique_ptr<ASTConsumer> CreateASTConsumer(CompilerInstance& CI, StringRef file) override
	{
		return std::make_unique<ScriptExportConsumer>(&CI);
	}
};

int main(int argc, const char** argv)
{
	CommonOptionsParser op(argc, argv, MyToolCategory);
	ClangTool Tool(op.getCompilations(), op.getSourcePathList());

	std::unique_ptr<FrontendActionFactory> factory = newFrontendActionFactory<ScriptExportFrontendAction>();
	int output = Tool.run(factory.get());

	system("pause");
	return output;
}

