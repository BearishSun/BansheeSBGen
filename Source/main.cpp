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
#include <unordered_set>
#include <vector>

using namespace clang::driver;
using namespace clang::tooling;
using namespace llvm;
using namespace clang;

static cl::OptionCategory OptCategory("Script binding options");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp CustomHelp("\nAdd \"-- <compiler arguments>\" at the end to setup the compiler "
								"invocation\n");

static cl::opt<std::string> OutputOption(
	"output",
	cl::desc("Specify output directory. Generated CPP files will be placed relative to that folder in Cpp/Include"
			 "and Cpp/Source folder. Generated CS files will be placed in CS/ folder.\n"),
	cl::cat(OptCategory));

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

enum class MethodFlags
{
	Static = 1 << 0,
	External = 1 << 1,
	Constructor = 1 << 2,
	PropertyGetter = 1 << 3,
	PropertySetter = 1 << 4
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
	External = 1 << 3,
	ExternalConstructor = 1 << 4,
	Editor = 1 << 5
};

struct UserTypeInfo
{
	UserTypeInfo() {}

	UserTypeInfo(const std::string& scriptName, ParsedType type, const std::string& declFile, const std::string& destFile)
		:scriptName(scriptName), type(type), declFile(declFile), destFile(destFile)
	{ }

	std::string scriptName;
	std::string declFile;
	std::string destFile;
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
	std::string interopName;
	std::string scriptName;

	ReturnInfo returnInfo;
	std::vector<VarInfo> paramInfos;
	std::string documentation;

	std::string externalClass;
	int flags;
};

struct PropertyInfo
{
	std::string name;
	std::string type;

	std::string getter;
	std::string setter;

	int typeFlags;
	bool isStatic;
	std::string documentation;
};

struct ClassInfo
{
	std::string name;
	CSVisibility visibility;
	bool inEditor;

	std::vector<MethodInfo> ctorInfos;
	std::vector<PropertyInfo> propertyInfos;
	std::vector<MethodInfo> methodInfos;
	std::string documentation;
};

struct ExternalMethodInfos
{
	std::vector<MethodInfo> methods;
};

struct SimpleConstructorInfo
{
	std::vector<VarInfo> params;
	std::unordered_map<std::string, std::string> fieldAssignments;
};

struct StructInfo
{
	std::string name;
	CSVisibility visibility;

	std::vector<SimpleConstructorInfo> ctors;
	std::vector<VarInfo> fields;
	bool inEditor;

	std::string documentation;
};

struct EnumInfo
{
	std::string code;
};

struct FileInfo
{
	std::vector<ClassInfo> classInfos;
	std::vector<StructInfo> structInfos;
	std::vector<EnumInfo> enumInfos;

	std::vector<std::string> referencedHeaderIncludes;
	std::vector<std::string> referencedSourceIncludes;
	bool inEditor;
};

struct IncludeInfo
{
	IncludeInfo(const std::string& typeName, const UserTypeInfo& typeInfo, bool sourceInclude)
		:typeName(typeName), typeInfo(typeInfo), sourceInclude(sourceInclude)
	{ }

	std::string typeName;
	UserTypeInfo typeInfo;
	bool sourceInclude;
};

std::unordered_map<std::string, UserTypeInfo> cppToCsTypeMap;
std::unordered_map<std::string, FileInfo> outputFileInfos;
std::unordered_map<std::string, ExternalMethodInfos> externalMethodInfos;

class ScriptExportVisitor : public RecursiveASTVisitor<ScriptExportVisitor>
{
public:
	explicit ScriptExportVisitor(CompilerInstance* CI)
		:astContext(&(CI->getASTContext()))
	{
		// Note: I could auto-generate C++ wrappers for these types
		cppToCsTypeMap["Vector2"] = UserTypeInfo("Vector2", ParsedType::Struct, "BsVector2.h", "BsScriptVector.h");
		cppToCsTypeMap["Vector3"] = UserTypeInfo("Vector3", ParsedType::Struct, "BsVector3.h", "BsScriptVector.h");
		cppToCsTypeMap["Vector4"] = UserTypeInfo("Vector4", ParsedType::Struct, "BsVector4.h", "BsScriptVector.h");
		cppToCsTypeMap["Matrix3"] = UserTypeInfo("Matrix3", ParsedType::Struct, "BsMatrix3.h", "");
		cppToCsTypeMap["Matrix4"] = UserTypeInfo("Matrix4", ParsedType::Struct, "BsMatrix4.h", "");
		cppToCsTypeMap["Quaternion"] = UserTypeInfo("Quaternion", ParsedType::Struct, "BsQuaternion.h", "");
		cppToCsTypeMap["Radian"] = UserTypeInfo("Radian", ParsedType::Struct, "BsRadian.h", "");
		cppToCsTypeMap["Degree"] = UserTypeInfo("Degree", ParsedType::Struct, "BsDegree.h", "");
		cppToCsTypeMap["Color"] = UserTypeInfo("Color", ParsedType::Struct, "BsColor.h", "BsScriptColor.h");
		cppToCsTypeMap["AABox"] = UserTypeInfo("AABox", ParsedType::Struct, "BsAABox.h", "");
		cppToCsTypeMap["Sphere"] = UserTypeInfo("Sphere", ParsedType::Struct, "BsSphere.h", "");
		cppToCsTypeMap["Capsule"] = UserTypeInfo("Capsule", ParsedType::Struct, "BsCapsule.h", "");
		cppToCsTypeMap["Ray"] = UserTypeInfo("Ray", ParsedType::Struct, "BsRay.h", "");
		cppToCsTypeMap["Vector2I"] = UserTypeInfo("Vector2I", ParsedType::Struct, "BsVector2I.h", "BsScriptVector2I.h");
		cppToCsTypeMap["Rect2"] = UserTypeInfo("Rect2", ParsedType::Struct, "BsRect2.h", "");
		cppToCsTypeMap["Rect2I"] = UserTypeInfo("Rect2I", ParsedType::Struct, "BsRect2I.h", "");
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

	std::string mapCppTypeToCSType(const std::string& cppType) const
	{
		if (cppType == "INT8")
			return "sbyte";

		if (cppType == "UINT8")
			return "byte";

		if (cppType == "INT16")
			return "short";

		if (cppType == "UINT16")
			return "ushort";

		if (cppType == "INT32")
			return "int";

		if (cppType == "UINT32")
			return "uint";

		if (cppType == "INT64")
			return "long";

		if (cppType == "UINT64")
			return "ulong";

		if (cppType == "wchar_t")
			return "char";

		return cppType;
	}

	bool mapBuiltinTypeToCppType(BuiltinType::Kind kind, std::string& output) const
	{
		switch (kind)
		{
		case BuiltinType::Void:
			output = "void";
			return true;
		case BuiltinType::Bool:
			output = "bool";
			return true;
		case BuiltinType::Char_S:
		case BuiltinType::SChar:
			output = "INT8";
			return true;
		case BuiltinType::Char_U:
			output = "UINT8";
			return true;
		case BuiltinType::Short:
			output = "INT16";
			return true;
		case BuiltinType::Int:
			output = "INT32";
			return true;
		case BuiltinType::Long:
			output = "INT32";
			return true;
		case BuiltinType::LongLong:
			output = "INT64";
			return true;
		case BuiltinType::UChar:
			output = "UINT8";
			return true;
		case BuiltinType::UShort:
			output = "UINT16";
			return true;
		case BuiltinType::UInt:
			output = "UINT32";
			return true;
		case BuiltinType::ULong:
			output = "UINT32";
			return true;
		case BuiltinType::ULongLong:
			output = "UINT64";
			return true;
		case BuiltinType::Float:
			output = "float";
			return true;
		case BuiltinType::Double:
			output = "double";
			return true;
		case BuiltinType::WChar_S:
		case BuiltinType::WChar_U:
			output = "wchar_t";
			return true;
		case BuiltinType::Char16:
			output = "UINT16";
			return true;
		case BuiltinType::Char32:
			output = "UINT32";
			return true;
		default:
			break;
		}

		errs() << "Unrecognized builtin type found.";
		return false;
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
		else if(type->isStructureOrClassType())
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
			if (!mapBuiltinTypeToCppType(builtinType->getKind(), outType))
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
					
					// Check for string types
					if (sourceTypeName == "basic_string" && nsName == "std")
					{
						const BuiltinType* builtinType = argType->getAs<BuiltinType>();
						if(builtinType->getKind() == BuiltinType::Kind::WChar_U)
							typeFlags |= (int)TypeFlags::WString;
						else
							typeFlags |= (int)TypeFlags::String;

						outType = "string";

						return true;
					}

					bool isValid = false;
					if(argType->isBuiltinType())
					{
						const BuiltinType* builtinType = argType->getAs<BuiltinType>();
						isValid = mapBuiltinTypeToCppType(builtinType->getKind(), outType);

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

			// Its a user-defined type
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
						sourceName << "\".";
				}
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
		
		std::string declFile = sys::path::filename(astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()));

		cppToCsTypeMap[sourceClassName] = UserTypeInfo(className, ParsedType::Enum, declFile, fileName);
		cppToCsTypeMap[sourceClassName].underlyingType = builtinType->getKind();

		std::stringstream output;

		output << convertJavadocToXMLComments(decl);
		if (visibility == CSVisibility::Internal)
			output << "\tinternal ";
		else if(visibility == CSVisibility::Public)
			output << "\tpublic ";
		
		output << "enum " << className.str();

		if (explicitType)
			output << " : " << enumType;

		output << std::endl;
		output << "\t{" << std::endl;

		auto iter = decl->enumerator_begin();
		while(iter != decl->enumerator_end())
		{
			EnumConstantDecl* constDecl = *iter;

			SmallString<5> valueStr;
			constDecl->getInitVal().toString(valueStr);

			output << "\t\t" << convertJavadocToXMLComments(constDecl);
			output << "\t\t" << constDecl->getName().str();
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

	bool VisitCXXRecordDecl(CXXRecordDecl* decl)
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
			structInfo.documentation = convertJavadocToXMLComments(decl);

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

					structInfo.ctors.push_back(ctorInfo);
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
			classInfo.inEditor = (classExportFlags & (int)ExportFlags::Editor) != 0;
			classInfo.documentation = convertJavadocToXMLComments(decl);

			ParsedType classType = getObjectType(decl);

			std::string declFile = sys::path::filename(astContext->getSourceManager().getFilename(decl->getSourceRange().getBegin()));

			cppToCsTypeMap[sourceClassName] = UserTypeInfo(className, classType, declFile, fileName);

			for(auto I = decl->ctor_begin(); I != decl->ctor_end(); ++I)
			{
				CXXConstructorDecl* ctorDecl = *I;

				StringRef dummy0;
				StringRef dummy1;
				StringRef dummy2;
				CSVisibility dummy3;
				int methodExportFlags;

				if (!parseExportAttribute(attr, dummy0, dummy1, dummy2, dummy3, methodExportFlags, externalClass))
					continue;

				MethodInfo methodInfo;
				methodInfo.sourceName = sourceClassName;
				methodInfo.scriptName = className;
				methodInfo.documentation = convertJavadocToXMLComments(ctorDecl);
				methodInfo.flags = (int)MethodFlags::Constructor;

				bool invalidParam = false;
				for (auto J = ctorDecl->param_begin(); J != ctorDecl->param_end(); ++J)
				{
					ParmVarDecl* paramDecl = *J;
					QualType paramType = paramDecl->getType();

					VarInfo paramInfo;
					paramInfo.name = paramDecl->getName();

					if (!parseType(paramType, paramInfo.type, paramInfo.flags))
					{
						errs() << "Unable to parse return type for \"" << sourceClassName << "\"'s constructor.";
						invalidParam = true;
						continue;
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

				StringRef sourceMethodName = methodDecl->getName();
				StringRef methodName = sourceMethodName;
				StringRef dummy0;
				CSVisibility dummy1;
				int methodExportFlags;

				if (!parseExportAttribute(attr, sourceMethodName, methodName, dummy0, dummy1, methodExportFlags, externalClass))
					continue;

				int methodFlags = 0;
				if (methodDecl->isStatic())
					methodFlags |= (int)MethodFlags::Static;

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

				if ((methodExportFlags & (int)ExportFlags::PropertyGetter) != 0)
					methodFlags |= (int)MethodFlags::PropertyGetter;
				else if ((methodExportFlags & (int)ExportFlags::PropertySetter) != 0)
					methodFlags |= (int)MethodFlags::PropertySetter;

				MethodInfo methodInfo;
				methodInfo.sourceName = sourceMethodName;
				methodInfo.scriptName = methodName;
				methodInfo.documentation = convertJavadocToXMLComments(methodDecl);
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
				}
				else
				{
					if(methodExportFlags & ((int)ExportFlags::PropertyGetter) != 0)
					{
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

						if (!parseType(returnType, methodInfo.returnInfo.type, methodInfo.returnInfo.flags))
						{
							errs() << "Unable to parse property type for method \"" << sourceMethodName << "\". Skipping property.";
							continue;
						}
					}
					else // Must be setter
					{
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

						VarInfo paramInfo;
						paramInfo.name = paramDecl->getName();

						if (!parseType(paramDecl->getType(), paramInfo.type, paramInfo.flags))
						{
							errs() << "Unable to parse property type for method \"" << sourceMethodName << "\". Skipping property.";
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

				if (classInfo.inEditor)
					fileInfo.inEditor = true;
			}
		}
	
		return true;
	}

	UserTypeInfo getTypeInfo(const std::string& sourceType, int flags) const
	{
		if ((flags & (int)TypeFlags::Builtin) != 0)
		{
			UserTypeInfo outType;
			outType.scriptName = mapCppTypeToCSType(sourceType);
			outType.type = ParsedType::Builtin;

			return outType;
		}

		if ((flags & (int)TypeFlags::String) != 0)
		{
			UserTypeInfo outType;
			outType.scriptName = "string";
			outType.type = ParsedType::String;

			return outType;
		}

		if ((flags & (int)TypeFlags::WString) != 0)
		{
			UserTypeInfo outType;
			outType.scriptName = "string";
			outType.type = ParsedType::WString;

			return outType;
		}

		auto iterFind = cppToCsTypeMap.find(sourceType);
		if (iterFind == cppToCsTypeMap.end())
		{
			UserTypeInfo outType;
			outType.scriptName = mapCppTypeToCSType(sourceType);
			outType.type = ParsedType::Builtin;

			errs() << "Unable to map type \"" << sourceType << "\". Assuming same name as source. ";
			return outType;
		}

		return iterFind->second;
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

	std::string getInteropCppVarType(const std::string& typeName, ParsedType type, int flags)
	{
		if(isArray(flags))
		{
			if (isOutput(flags))
				return "MonoArray**";
			else
				return "MonoArray*";
		}

		switch(type)
		{
		case ParsedType::Builtin:
		case ParsedType::Enum:
			if (isOutput(flags))
				return typeName + "*";
			else
				return typeName;
		case ParsedType::Struct:
			return typeName + "*";
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

	std::string getCSVarType(const std::string& typeName, ParsedType type, int flags, bool paramPrefixes, bool arraySuffixes)
	{
		std::stringstream output;

		if(paramPrefixes && isOutput(flags))
		{
			if (type == ParsedType::Struct)
				output << "ref ";
			else
				output << "out ";
		}

		output << typeName;

		if (arraySuffixes && isArray(flags))
			output << "[]";

		return output.str();
	}

	std::string getAsCppArgument(const std::string& name, ParsedType type, int flags, const std::string& methodName)
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

	bool isValidStructType(UserTypeInfo& typeInfo, int flags)
	{
		if (isOutput(flags) || isArray(flags))
			return false;

		if (typeInfo.type == ParsedType::Builtin || typeInfo.type == ParsedType::Enum || typeInfo.type == ParsedType::Struct)
			return true;

		return false;
	}

	MethodInfo findUnusedCtorSignature(const ClassInfo& classInfo) const
	{
		int numBools = 1;
		while (true)
		{
			bool isSignatureValid = true;
			for (auto& entry : classInfo.ctorInfos)
			{
				if ((int)entry.paramInfos.size() != numBools)
					continue;

				bool isCtorValid = false;
				for(auto& paramInfo : entry.paramInfos)
				{
					if(paramInfo.type != "bool")
					{
						isCtorValid = true;
						break;
					}
				}

				if(!isCtorValid)
				{
					isSignatureValid = false;
					break;
				}
			}

			if (isSignatureValid)
				break;

			numBools++;
		}

		MethodInfo output;
		output.sourceName = classInfo.name;
		output.scriptName = classInfo.name;
		output.flags = (int)MethodFlags::Constructor;

		for(int i = 0; i < numBools; i++)
		{
			VarInfo paramInfo;
			paramInfo.name = "__dummy" + std::to_string(i);
			paramInfo.type = "bool";
			paramInfo.flags = (int)TypeFlags::Builtin;

			output.paramInfos.push_back(paramInfo);
		}

		return output;
	}

	void postProcessFileInfos()
	{
		// Inject external methods into their appropriate class infos
		auto findClassInfo = [](const std::string& name) -> ClassInfo*
		{
			for(auto& fileInfo : outputFileInfos)
			{
				for(auto& classInfo : fileInfo.second.classInfos)
				{
					if (classInfo.name == name)
						return &classInfo;
				}
			}
			
			return nullptr;
		};

		for(auto& entry : externalMethodInfos)
		{
			ClassInfo* classInfo = findClassInfo(entry.first);
			if (classInfo == nullptr)
				continue;

			for(auto& method : entry.second.methods)
				classInfo->methodInfos.push_back(method);
		}

		// Generate unique interop method names
		std::unordered_set<std::string> usedNames;
		for (auto& fileInfo : outputFileInfos)
		{
			for (auto& classInfo : fileInfo.second.classInfos)
			{
				usedNames.clear();

				auto generateInteropName = [&usedNames](MethodInfo& methodInfo)
				{
					std::string interopName = methodInfo.sourceName;
					int counter = 0;
					while (true)
					{
						auto iterFind = usedNames.find(interopName);
						if (iterFind == usedNames.end())
							break;

						interopName = methodInfo.sourceName + std::to_string(counter);
						counter++;
					}

					usedNames.insert(interopName);
					methodInfo.interopName = interopName;
				};

				for (auto& methodInfo : classInfo.methodInfos)
					generateInteropName(methodInfo);

				for (auto& methodInfo : classInfo.ctorInfos)
					generateInteropName(methodInfo);
			}
		}

		// Generate property infos
		for (auto& fileInfo : outputFileInfos)
		{
			for (auto& classInfo : fileInfo.second.classInfos)
			{
				for(auto& methodInfo : classInfo.methodInfos)
				{
					bool isGetter = (methodInfo.flags & (int)MethodFlags::PropertyGetter) != 0;
					bool isSetter = (methodInfo.flags & (int)MethodFlags::PropertySetter) != 0;

					if (!isGetter && !isSetter)
						continue;

					PropertyInfo propertyInfo;
					propertyInfo.name = methodInfo.scriptName;
					propertyInfo.documentation = methodInfo.documentation;
					propertyInfo.isStatic = (methodInfo.flags & (int)MethodFlags::Static);

					if(isGetter)
					{
						propertyInfo.getter = methodInfo.interopName;
						propertyInfo.type = methodInfo.returnInfo.type;
						propertyInfo.typeFlags = methodInfo.returnInfo.flags;
					}
					else // Setter
					{
						propertyInfo.setter = methodInfo.interopName;
						propertyInfo.type = methodInfo.paramInfos[0].type;
						propertyInfo.typeFlags = methodInfo.paramInfos[0].flags;
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
						if (existingInfo.type != propertyInfo.type || existingInfo.isStatic != propertyInfo.isStatic)
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
		}

		// Generate referenced includes
		{
			for (auto& fileInfo : outputFileInfos)
			{
				std::unordered_map<std::string, IncludeInfo> includes;
				for (auto& classInfo : fileInfo.second.classInfos)
					gatherIncludes(classInfo, includes);

				// Needed for all .h files
				if (!fileInfo.second.inEditor)
					fileInfo.second.referencedHeaderIncludes.push_back("BsScriptEnginePrerequisites.h");
				else
					fileInfo.second.referencedHeaderIncludes.push_back("BsScriptEditorPrerequisites.h");

				// Needed for all .cpp files
				fileInfo.second.referencedSourceIncludes.push_back("BsScript" + fileInfo.first + ".h");
				fileInfo.second.referencedSourceIncludes.push_back("BsMonoClass.h");
				fileInfo.second.referencedSourceIncludes.push_back("BsMonoUtil.h");

				for (auto& classInfo : fileInfo.second.classInfos)
				{
					UserTypeInfo& typeInfo = cppToCsTypeMap[classInfo.name];

					if (typeInfo.type == ParsedType::Resource)
						fileInfo.second.referencedHeaderIncludes.push_back("BsScriptResource.h");
					else if (typeInfo.type == ParsedType::Component)
						fileInfo.second.referencedHeaderIncludes.push_back("BsScriptComponent.h");
					else // Class
						fileInfo.second.referencedHeaderIncludes.push_back("BsScriptObject.h");

					fileInfo.second.referencedSourceIncludes.push_back(typeInfo.declFile);
				}

				for (auto& structInfo : fileInfo.second.structInfos)
				{
					UserTypeInfo& typeInfo = cppToCsTypeMap[structInfo.name];

					fileInfo.second.referencedHeaderIncludes.push_back("BsScriptObject.h");
					fileInfo.second.referencedHeaderIncludes.push_back(typeInfo.declFile);
				}

				for(auto& entry : includes)
				{
					if(entry.second.sourceInclude)
					{
						std::string include = entry.second.typeInfo.declFile;
						fileInfo.second.referencedHeaderIncludes.push_back(include);
					}
					
					if(entry.second.typeInfo.type != ParsedType::Enum)
					{
						if (!entry.second.typeInfo.destFile.empty())
						{
							std::string include = "BsScript" + entry.second.typeInfo.destFile + ".h";
							fileInfo.second.referencedSourceIncludes.push_back(include);
						}
					}
				}
			}
		}
	}

	std::string generateCppMethodSignature(const MethodInfo& methodInfo, const std::string& interopClassName, const std::string& nestedName)
	{
		bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
		bool isCtor = (methodInfo.flags & (int)MethodFlags::Constructor) != 0;

		std::stringstream output;

		bool returnAsParameter = false;
		if (methodInfo.returnInfo.type.empty() || isCtor)
			output << "void";
		else
		{
			UserTypeInfo returnTypeInfo = getTypeInfo(methodInfo.returnInfo.type, methodInfo.returnInfo.flags);
			if (!canBeReturned(returnTypeInfo.type, methodInfo.returnInfo.flags))
			{
				output << "void";
				returnAsParameter = true;
			}
			else
			{
				output << getInteropCppVarType(methodInfo.returnInfo.type, returnTypeInfo.type, methodInfo.returnInfo.flags);
			}
		}

		output << " ";

		if (!nestedName.empty())
			output << nestedName << "::";

		output << "Internal_" << methodInfo.interopName << "(";

		if (isCtor)
		{
			output << "MonoObject* managedInstance";

			if (methodInfo.paramInfos.size() > 0)
				output << ", ";
		}
		else if(!isStatic)
		{
			output << interopClassName << " thisPtr";

			if (methodInfo.paramInfos.size() > 0 || returnAsParameter)
				output << ", ";
		}

		for(auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
		{
			UserTypeInfo paramTypeInfo = getTypeInfo(I->type, I->flags);

			output << getInteropCppVarType(I->type, paramTypeInfo.type, I->flags) << " " << I->name;

			if ((I + 1) != methodInfo.paramInfos.end() || returnAsParameter)
				output << ", ";
		}

		if(returnAsParameter)
		{
			UserTypeInfo returnTypeInfo = getTypeInfo(methodInfo.returnInfo.type, methodInfo.returnInfo.flags);

			output << getInteropCppVarType(methodInfo.returnInfo.type, returnTypeInfo.type, methodInfo.returnInfo.flags) <<
				" " << "__output";
		}

		output << ")";
		return output.str();
	}

	std::string generateNativeToScriptObjectLine(ParsedType type, const std::string& scriptType, const std::string& scriptName,
												 const std::string& argName, const std::string& indent = "\t\t")
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

	std::string generateMethodBodyBlockForParam(const std::string& name, const std::string& typeName, int flags, 
			bool isLast, bool returnValue, std::stringstream& preCallActions, std::stringstream& postCallActions)
	{
		UserTypeInfo paramTypeInfo = getTypeInfo(typeName, flags);

		if (!isArray(flags))
		{
			std::string argName;

			switch (paramTypeInfo.type)
			{
			case ParsedType::Builtin:
			case ParsedType::Enum:
			case ParsedType::Struct:
				if(returnValue)
				{
					argName = "tmp" + name;
					preCallActions << "\t\t" << typeName << " " << argName << ";" << std::endl;
					postCallActions << name << " = " << argName << ";" << std::endl;
				}
				else
					argName = name;

				if (!isLast)
				{
					preCallActions << std::endl;
					postCallActions << std::endl;
				}

				break;
			case ParsedType::String:
			{
				argName = "tmp" + name;
				preCallActions << "\t\tString " << argName << ";" << std::endl;

				if (!isOutput(flags) && !returnValue)
					preCallActions << "\t\t" << argName << "MonoUtil::monoToString(" << name << ");" << std::endl;
				else
					postCallActions << "\t\t" << name << " = " << "MonoUtil::stringToMono(" << argName << ");";

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
				preCallActions << "\t\tWString " << argName << ";" << std::endl;

				if (!isOutput(flags) && !returnValue)
					preCallActions << "\t\t" << argName << "MonoUtil::monoToWString(" << name << ");" << std::endl;
				else
					postCallActions << "\t\t" << name << " = " << "MonoUtil::wstringToMono(" << argName << ");";

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
				std::string tmpType = getCppVarType(typeName, paramTypeInfo.type);

				preCallActions << "\t\t" << tmpType << " " << argName << ";" << std::endl;

				if (!isOutput(flags) && !returnValue)
				{
					std::string scriptName = "script" + name;
					std::string scriptType = getScriptInteropType(typeName);

					preCallActions << "\t\t" << scriptType << "* " << scriptName << ";" << std::endl;
					preCallActions << "\t\t" << scriptName << " = " << scriptType << "::toNative(" << name << ");" << std::endl;

					if (isHandleType(paramTypeInfo.type))
						preCallActions << "\t\t" << argName << " = " << scriptName << "->getHandle();";
					else
						preCallActions << "\t\t" << argName << " = " << scriptName << "->getInternal();";

					if (!isLast)
						preCallActions << std::endl;
				}
				else
				{
					std::string scriptName = "script" + name;
					std::string scriptType = getScriptInteropType(typeName);

					postCallActions << "\t\t" << scriptType << "* " << scriptName << ";" << std::endl;
					postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.type, scriptType, scriptName, argName);
					postCallActions << "\t\t*" << name << " = " << scriptName << "->getMangedInstance();" << std::endl;

					if (!isLast)
					{
						preCallActions << std::endl;
						postCallActions << std::endl;
					}
				}
			}
			break;
			}

			return argName;
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
				entryType = typeName;
				break;
			default: // Some object or struct type
				entryType = getScriptInteropType(typeName);
				break;
			}

			std::string argType = "Vector<" + typeName + ">";
			std::string argName = "vec" + name;
			preCallActions << "\t\t" << argType << " " << argName << ";" << std::endl;

			if (!isOutput(flags) && !returnValue)
			{
				std::string arrayName = "array" + name;
				preCallActions << "\t\tScriptArray " << arrayName << "(" << name << ");" << std::endl;
				preCallActions << "\t\tfor(int i = 0; i < " << arrayName << ".size(); i++)" << std::endl;
				preCallActions << "\t\t{" << std::endl;

				switch (paramTypeInfo.type)
				{
				case ParsedType::Builtin:
				case ParsedType::String:
				case ParsedType::WString:
					preCallActions << "\t\t\t" << argName << "[i] = " << arrayName << ".get<" << entryType << ">(i);" << std::endl;
					break;
				case ParsedType::Enum:
				{
					std::string enumType;
					mapBuiltinTypeToCppType(paramTypeInfo.underlyingType, enumType);

					preCallActions << "\t\t\t" << argName << "[i] = (" << entryType << ")" << arrayName << ".get<" << enumType << ">(i);" << std::endl;
					break;
				}
				case ParsedType::Struct:
					preCallActions << "\t\t\t" << argName << "[i] = " << entryType << "::unbox(" << arrayName << ".get<MonoObject*>(i));" << std::endl;
					break;
				default: // Some object type
				{
					std::string scriptName = "script" + name;
					preCallActions << "\t\t\t" << entryType << "* " << scriptName << ";" << std::endl;
					preCallActions << "\t\t\t" << scriptName << " = " << entryType << "::toNative(" << arrayName << ".get<MonoObject*>(i));" << std::endl;
					preCallActions << "\t\t\tif(scriptName != nullptr)" << std::endl;

					if (isHandleType(paramTypeInfo.type))
						preCallActions << "\t\t\t\t" << argName << "[i] = " << scriptName << "->getHandle();";
					else
						preCallActions << "\t\t\t\t" << argName << "[i] = " << scriptName << "->getInternal();";
				}
				break;
				}

				preCallActions << "\t\t}" << std::endl;

				if (!isLast)
					preCallActions << std::endl;
			}
			else
			{
				std::string arrayName = "array" + name;
				postCallActions << "\t\tScriptArray " << arrayName << ";" << std::endl;
				postCallActions << "\t\t" << arrayName << " = " << "ScriptArray::create<" << entryType << ">((int)" << argName << ".size())";
				postCallActions << "\t\tfor(int i = 0; i < (int)" << argName << ".size(); i++)" << std::endl;
				postCallActions << "\t\t{" << std::endl;

				switch (paramTypeInfo.type)
				{
				case ParsedType::Builtin:
				case ParsedType::String:
				case ParsedType::WString:
					postCallActions << "\t\t\t" << arrayName << ".set(i, " << argName << "[i]);" << std::endl;
					break;
				case ParsedType::Enum:
				{
					std::string enumType;
					mapBuiltinTypeToCppType(paramTypeInfo.underlyingType, enumType);

					postCallActions << "\t\t\t" << arrayName << ".set(i, (" << enumType << ")" << argName << "[i]);" << std::endl;
					break;
				}
				case ParsedType::Struct:
					postCallActions << "\t\t\t" << arrayName << ".set(i, " << entryType << "::box(" << argName << "[i]));" << std::endl;
					break;
				default: // Some object type
				{
					std::string scriptName = "script" + name;

					postCallActions << "\t\t\t" << entryType << "* " << scriptName << ";" << std::endl;
					postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.type, entryType, scriptName, argName + "[i]", "\t\t\t");
					postCallActions << "\t\t\t" << arrayName << ".set(i, " << scriptName << "->getMangedInstance());" << std::endl;
				}
				break;
				}

				postCallActions << "\t\t}" << std::endl;
				postCallActions << "\t\t*" << name << " = " << arrayName << ".getInternal()";

				if (!isLast)
				{
					preCallActions << std::endl;
					postCallActions << std::endl;
				}
			}
		}
	}

	void gatherIncludes(const std::string& typeName, int flags, std::unordered_map<std::string, IncludeInfo>& output)
	{
		UserTypeInfo typeInfo = getTypeInfo(typeName, flags);
		if (typeInfo.type == ParsedType::Class || typeInfo.type == ParsedType::Component || 
			typeInfo.type == ParsedType::SceneObject || typeInfo.type == ParsedType::Resource ||
			typeInfo.type == ParsedType::Enum)
		{
			auto iterFind = output.find(typeName);
			if (iterFind == output.end())
			{
				// If enum or passed by value we need to include the header for the source type
				bool sourceInclude = typeInfo.type == ParsedType::Enum || isSrcValue(flags);

				output[typeName] = IncludeInfo(typeName, typeInfo, sourceInclude);
			}
		}
	}

	void gatherIncludes(const MethodInfo& methodInfo, std::unordered_map<std::string, IncludeInfo>& output)
	{
		bool returnAsParameter = false;
		if (!methodInfo.returnInfo.type.empty())
			gatherIncludes(methodInfo.returnInfo.type, methodInfo.returnInfo.flags, output);

		for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
			gatherIncludes(I->type, I->flags, output);
	}

	void gatherIncludes(const ClassInfo& classInfo, std::unordered_map<std::string, IncludeInfo>& output)
	{
		for (auto& methodInfo : classInfo.ctorInfos)
			gatherIncludes(methodInfo, output);

		for (auto& methodInfo : classInfo.methodInfos)
			gatherIncludes(methodInfo, output);
	}

	std::string generateCppMethodBody(const MethodInfo& methodInfo, const std::string& sourceClassName, 
									  const std::string& interopClassName, ParsedType classType)
	{
		std::string returnAssignment;
		std::string returnStmt;
		std::stringstream preCallActions;
		std::stringstream methodArgs;
		std::stringstream postCallActions;

		bool returnAsParameter = false;
		if (!methodInfo.returnInfo.type.empty())
		{
			UserTypeInfo returnTypeInfo = getTypeInfo(methodInfo.returnInfo.type, methodInfo.returnInfo.flags);
			if (!canBeReturned(returnTypeInfo.type, methodInfo.returnInfo.flags))
				returnAsParameter = true;
			else
			{
				std::string argName = generateMethodBodyBlockForParam("__output", methodInfo.returnInfo.type,
										methodInfo.returnInfo.flags, true, true, preCallActions, postCallActions);

				returnAssignment = argName + " = ";
				returnStmt = "\t\treturn __output";
			}
		}

		for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
		{
			bool isLast = (I + 1) == methodInfo.paramInfos.end() && !returnAsParameter;

			std::string argName = generateMethodBodyBlockForParam(I->name, I->type, I->flags, isLast, false,
				preCallActions, postCallActions);

			if (!isArray(I->flags))
			{
				UserTypeInfo paramTypeInfo = getTypeInfo(I->type, I->flags);

				methodArgs << getAsCppArgument(argName, paramTypeInfo.type, I->flags, methodInfo.sourceName);
			}
			else
				methodArgs << getAsCppArgument(argName, ParsedType::Builtin, I->flags, methodInfo.sourceName);

			if (!isLast)
				methodArgs << ", ";
		}

		if(returnAsParameter)
		{
			std::string argName = generateMethodBodyBlockForParam("__output", methodInfo.returnInfo.type, 
										methodInfo.returnInfo.flags, true, true, preCallActions, postCallActions);

			returnAssignment = argName + " = ";
		}

		std::stringstream output;
		output << "\t{" << std::endl;
		output << preCallActions.str();

		bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
		bool isCtor = (methodInfo.flags & (int)MethodFlags::Constructor) != 0;
		bool isExternal = (methodInfo.flags & (int)MethodFlags::External) != 0;

		if (isCtor)
		{
			bool isValid = false;
			if (!isExternal)
			{
				if (classType == ParsedType::Class)
				{
					output << "\t\tSPtr<" << sourceClassName << "> instance = bs_shared_ptr_new<" << sourceClassName << ">(" << methodArgs.str() << ");" << std::endl;
					isValid = true;
				}
			}
			else
			{
				std::string fullMethodName = methodInfo.externalClass + "::" + methodInfo.sourceName;

				if (classType == ParsedType::Class)
				{
					output << "\t\tSPtr<" << sourceClassName << "> instance = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
					isValid = true;
				}
				else if(classType == ParsedType::Resource)
				{
					output << "\t\tResourceHandle<" << sourceClassName << "> instance = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
					isValid = true;
				}
			}

			if(isValid)
				output << "\t\t" << interopClassName << "* scriptInstance = new (bs_alloc<" << interopClassName << ">())" << interopClassName << "(managedInstance, instance);" << std::endl;
			else
				errs() << "Cannot generate a constructor for \"" << sourceClassName << "\". Unsupported class type. ";
		}
		else
		{
			if (!isExternal)
			{
				if (isStatic)
				{
					output << "\t\t" << returnAssignment << sourceClassName << "::" << methodInfo.sourceName << "(" << methodArgs.str() << ");" << std::endl;
				}
				else
				{
					if (classType == ParsedType::Class)
					{
						output << "\t\t" << returnAssignment << "thisPtr->getInternal()->" << methodInfo.sourceName << "(" << methodArgs.str() << ");" << std::endl;
					}
					else // Must be one of the handle types
					{
						assert(isHandleType(classType));

						output << "\t\t" << returnAssignment << "thisPtr->getHandle()->" << methodInfo.sourceName << "(" << methodArgs.str() << ");" << std::endl;
					}
				}
			}
			else
			{
				std::string fullMethodName = methodInfo.externalClass + "::" + methodInfo.sourceName;
				if (isStatic)
				{
					output << "\t\t" << returnAssignment << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				}
				else
				{
					if (classType == ParsedType::Class)
					{
						output << "\t\t" << returnAssignment << fullMethodName << "(thisPtr->getInternal()";
					}
					else // Must be one of the handle types
					{
						assert(isHandleType(classType));

						output << "\t\t" << returnAssignment << fullMethodName << "(thisPtr->getHandle()";
					}

					std::string methodArgsStr = methodArgs.str();
					if (!methodArgsStr.empty())
						output << ", " << methodArgsStr;

					output << ");" << std::endl;
				}
			}
		}

		std::string postCallActionsStr = postCallActions.str();
		if (!postCallActionsStr.empty())
			output << std::endl;

		output << postCallActionsStr;

		if(!returnStmt.empty())
		{
			output << std::endl;
			output << returnStmt << std::endl;
		}

		output << "\t}" << std::endl;
		return output.str();
	}

	std::string generateCppHeaderOutput(const ClassInfo& classInfo, const UserTypeInfo& typeInfo)
	{
		// TODO - Handle inheritance (need to generate an intermediate class)

		std::stringstream output;

		output << "\tclass ";
		
		if (!classInfo.inEditor)
			output << "BS_SCR_BE_EXPORT ";
		else
			output << "BS_SCR_BED_EXPORT ";

		std::string interopClassName = getScriptInteropType(classInfo.name);
		output << interopClassName << " : public ";

		if (typeInfo.type == ParsedType::Resource)
			output << "TScriptResource<" << interopClassName << ", " << classInfo.name << ">";
		else if (typeInfo.type == ParsedType::Component)
			output << "TScriptComponent<" << interopClassName << ", " << classInfo.name << ">";
		else // Class
			output << "ScriptObject<" << interopClassName << ">";

		output << std::endl;
		output << "\t{" << std::endl;
		output << "\tpublic:" << std::endl;

		if (!classInfo.inEditor)
			output << "\t\tSCRIPT_OBJ(ENGINE_ASSEMBLY, \"BansheeEngine\", \"" << typeInfo.scriptName << "\")" << std::endl;
		else
			output << "\t\tSCRIPT_OBJ(EDITOR_ASSEMBLY, \"BansheeEditor\", \"" << typeInfo.scriptName << "\")" << std::endl;

		output << std::endl;

		std::string wrappedDataType = getCppVarType(classInfo.name, typeInfo.type);

		if (typeInfo.type == ParsedType::Class)
		{
			// getInternal() method (handle types have getHandle() implemented by their base type)
			output << "\t\t" << wrappedDataType << " getInternal() const { return mInternal; }" << std::endl;

			// create() method
			output << "\t\tstatic MonoObject* create(const " << wrappedDataType << "& value);" << std::endl;
		}
		else if(typeInfo.type == ParsedType::Resource)
		{
			// createInstance() method required by script resource manager
			output << "\t\tstatic MonoObject* createInstance();" << std::endl;
		}

		output << std::endl;
		output << "\tprivate:" << std::endl;

		// Constructor
		output << "\t\t" << interopClassName << "(MonoObject* managedInstance, const " << wrappedDataType << "& value);" << std::endl;
		output << std::endl;

		// Data member
		if (typeInfo.type == ParsedType::Class)
		{
			output << "\t\t" << wrappedDataType << " mInternal;" << std::endl;
			output << std::endl;
		}

		// CLR hooks
		for (auto& methodInfo : classInfo.ctorInfos)
			output << "\t\tstatic " << generateCppMethodSignature(methodInfo, interopClassName, "") << ";" << std::endl;

		for(auto& methodInfo : classInfo.methodInfos)
			output << "\t\tstatic " << generateCppMethodSignature(methodInfo, interopClassName, "") << ";" << std::endl;

		output << "\t};" << std::endl;
		return output.str();
	}

	std::string generateCppSourceOutput(const ClassInfo& classInfo, const UserTypeInfo& typeInfo)
	{
		std::string interopClassName = getScriptInteropType(classInfo.name);
		std::string wrappedDataType = getCppVarType(classInfo.name, typeInfo.type);

		std::stringstream output;

		// Constructor
		output << "\t" << interopClassName << "::" << interopClassName << "(MonoObject* managedInstance, const " << wrappedDataType << "& value)" << std::endl;
		output << "\t\t:";

		if (typeInfo.type == ParsedType::Resource)
			output << "TScriptResource(managedInstance, value)";
		else if (typeInfo.type == ParsedType::Component)
			output << "TScriptComponent(managedInstance, value)";
		else // Class
			output << "ScriptObject(managedInstance), mInternal(value)";

		output << std::endl;
		output << "\t{ }" << std::endl;
		output << std::endl;

		// CLR hook registration
		output << "\tvoid " << interopClassName << "::initRuntimeData()" << std::endl;
		output << "\t{" << std::endl;

		for (auto& methodInfo : classInfo.methodInfos)
		{
			output << "\t\tmetaData.scriptClass->addInternalCall(\"Internal_" << methodInfo.interopName << "\", &" <<
				interopClassName << "::Internal_" << methodInfo.interopName << ");" << std::endl;
		}

		output << "\t}" << std::endl;

		// create() or createInstance() methods
		if (typeInfo.type == ParsedType::Class || typeInfo.type == ParsedType::Resource)
		{
			std::stringstream ctorSignature;
			std::stringstream ctorParamsInit;
			MethodInfo unusedCtor = findUnusedCtorSignature(classInfo);
			int numDummyParams = (int)unusedCtor.paramInfos.size();

			ctorParamsInit << "\t\tbool dummy = false;" << std::endl;
			ctorParamsInit << "\t\tvoid* ctorParams[" << numDummyParams << "] = { ";

			for (int i = 0; i < numDummyParams; i++)
			{
				ctorParamsInit << "&dummy";
				ctorSignature << unusedCtor.paramInfos[i].type;

				if ((i + 1) < numDummyParams)
				{
					ctorParamsInit << ", ";
					ctorSignature << ",";
				}
			}

			ctorParamsInit << " };" << std::endl;
			ctorParamsInit << std::endl;

			if (typeInfo.type == ParsedType::Class)
			{
				output << "\t MonoObject*" << interopClassName << "::create(const " << wrappedDataType << "& value)" << std::endl;
				output << "\t{" << std::endl;

				output << ctorParamsInit.str();
				output << "\t\tMonoObject* managedInstance = metaData.scriptClass->createInstance(" << ctorSignature.str() << ", ctorParams);" << std::endl;
				output << "\t\t" << interopClassName << "* scriptInstance = new (bs_alloc<" << interopClassName << ">())" << interopClassName << "(managedInstance, value);" << std::endl;
				output << "\t\treturn managedInstance;";

				output << "\t}" << std::endl;
			}
			else if (typeInfo.type == ParsedType::Resource)
			{
				output << "\t MonoObject*" << interopClassName << "::createInstance()" << std::endl;
				output << "\t{" << std::endl;

				output << ctorParamsInit.str();
				output << "\t\treturn metaData.scriptClass->createInstance(" << ctorSignature.str() << ", ctorParams);" << std::endl;

				output << "\t}" << std::endl;
			}
		}

		// CLR hook method implementations
		for (auto I = classInfo.ctorInfos.begin(); I != classInfo.ctorInfos.end(); ++I)
		{
			const MethodInfo& methodInfo = *I;

			output << "\t" << generateCppMethodSignature(methodInfo, interopClassName, interopClassName) << std::endl;
			output << generateCppMethodBody(methodInfo, classInfo.name, interopClassName, typeInfo.type);

			if ((I + 1) != classInfo.methodInfos.end())
				output << std::endl;
		}

		for(auto I = classInfo.methodInfos.begin(); I != classInfo.methodInfos.end(); ++I)
		{
			const MethodInfo& methodInfo = *I;

			output << "\t" << generateCppMethodSignature(methodInfo, interopClassName, interopClassName) << std::endl;
			output << generateCppMethodBody(methodInfo, classInfo.name, interopClassName, typeInfo.type);

			if((I + 1) != classInfo.methodInfos.end())
				output << std::endl;
		}

		return output.str();
	}

	std::string generateCppStructHeader(const StructInfo& structInfo)
	{
		UserTypeInfo typeInfo = getTypeInfo(structInfo.name, 0);

		std::stringstream output;

		output << "\tclass ";

		if (!structInfo.inEditor)
			output << "BS_SCR_BE_EXPORT ";
		else
			output << "BS_SCR_BED_EXPORT ";

		std::string interopClassName = getScriptInteropType(structInfo.name);
		output << interopClassName << " : public " << "ScriptObject<" << interopClassName << ">";

		output << std::endl;
		output << "\t{" << std::endl;
		output << "\tpublic:" << std::endl;

		if (!structInfo.inEditor)
			output << "\t\tSCRIPT_OBJ(ENGINE_ASSEMBLY, \"BansheeEngine\", \"" << typeInfo.scriptName << "\")" << std::endl;
		else
			output << "\t\tSCRIPT_OBJ(EDITOR_ASSEMBLY, \"BansheeEditor\", \"" << typeInfo.scriptName << "\")" << std::endl;

		output << std::endl;

		output << "\t\tstatic MonoObject* box(const " << structInfo.name << "& value);" << std::endl;
		output << "\t\tstatic " << structInfo.name << " unbox(MonoObject* value);" << std::endl;

		output << std::endl;
		output << "\tprivate:" << std::endl;

		// Constructor
		output << "\t\t" << interopClassName << "(MonoObject* managedInstance);" << std::endl;
		output << std::endl;

		output << "\t};" << std::endl;
		return output.str();
	}

	std::string generateCppStructSource(const StructInfo& structInfo)
	{
		UserTypeInfo typeInfo = getTypeInfo(structInfo.name, 0);
		std::string interopClassName = getScriptInteropType(structInfo.name);

		std::stringstream output;

		// Constructor
		output << "\t" << interopClassName << "::" << interopClassName << "(MonoObject* managedInstance)" << std::endl;
		output << "\t\t:ScriptObject(managedInstance)" << std::endl;
		output << "\t{ }" << std::endl;
		output << std::endl;

		// Empty initRuntimeData
		output << "\tvoid " << interopClassName << "::initRuntimeData()" << std::endl;
		output << "\t{ }" << std::endl;
		output << std::endl;

		// Box
		output << "\t MonoObject*" << interopClassName << "::box(const " << structInfo.name << "& value)" << std::endl;
		output << "\t{" << std::endl;
		output << "\t\treturn MonoUtil::box(metaData.scriptClass->_getInternalClass(), (void*)&value);";
		output << "\t}" << std::endl;
		output << std::endl;

		// Unbox
		output << "\t " << structInfo.name << " " << interopClassName << "::unbox(MonoObject* value)" << std::endl;
		output << "\t{" << std::endl;
		output << "\t\treturn *(" << structInfo.name << "*)MonoUtil::unbox(value);";
		output << "\t}" << std::endl;
		output << std::endl;

		return output.str();
	}

	std::string generateCSMethodParams(const MethodInfo& methodInfo)
	{
		std::stringstream output;
		for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
		{
			const VarInfo& paramInfo = *I;
			UserTypeInfo paramTypeInfo = getTypeInfo(paramInfo.type, paramInfo.flags);
			std::string qualifiedType = getCSVarType(paramTypeInfo.scriptName, paramTypeInfo.type, paramInfo.flags, true, true);

			output << qualifiedType << " " << paramInfo.name;

			if ((I + 1) != methodInfo.paramInfos.end())
				output << ", ";
		}

		return output.str();
	}

	std::string generateCSMethodArgs(const MethodInfo& methodInfo)
	{
		std::stringstream output;
		for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
		{
			const VarInfo& paramInfo = *I;
			UserTypeInfo paramTypeInfo = getTypeInfo(paramInfo.type, paramInfo.flags);

			if (isOutput(paramInfo.flags))
			{
				if (paramTypeInfo.type == ParsedType::Struct)
					output << "ref ";
				else
					output << "out ";
			}

			output << paramInfo.name;

			if ((I + 1) != methodInfo.paramInfos.end())
				output << ", ";
		}

		return output.str();
	}

	std::string generateCSClass(ClassInfo& input, UserTypeInfo& typeInfo)
	{
		std::stringstream ctors;
		std::stringstream properties;
		std::stringstream methods;
		std::stringstream interops;

		// Private constructor for runtime use
		MethodInfo pvtCtor = findUnusedCtorSignature(input);

		ctors << "\t\tprivate " << typeInfo.scriptName << "(" << generateCSMethodParams(pvtCtor) << ") { }" << std::endl;
		ctors << std::endl;

		// Constructors
		for (auto& entry : input.methodInfos)
		{
			ctors << entry.documentation;
			ctors << "\t\tpublic " << typeInfo.scriptName << "(" << generateCSMethodParams(entry) << ")" << std::endl;
			ctors << "\t\t{" << std::endl;
			ctors << "\t\t\tInternal_" << entry.interopName << "(this, " << generateCSMethodArgs(entry) << ");" << std::endl;
			ctors << "\t\t}" << std::endl;
			ctors << std::endl;

			// Generate interop
			interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]" << std::endl;
			interops << "\t\tprivate static extern void Internal_" << entry.interopName << "(IntPtr thisPtr, " << generateCSMethodParams(entry) << ");";
			interops << std::endl;
		}

		// External constructors, methods and interop stubs
		for (auto& entry : input.methodInfos)
		{
			bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
			bool isStatic = (entry.flags & (int)MethodFlags::Static) != 0;

			if(isConstructor)
			{
				ctors << entry.documentation;
				ctors << "\t\tpublic " << typeInfo.scriptName << "(" << generateCSMethodParams(entry) << ")" << std::endl;
				ctors << "\t\t{" << std::endl;
				ctors << "\t\t\tInternal_" << entry.interopName << "(this, " << generateCSMethodArgs(entry) << ");" << std::endl;
				ctors << "\t\t}" << std::endl;
				ctors << std::endl;
			}
			else
			{
				bool isProperty = entry.flags & ((int)MethodFlags::PropertyGetter | (int)MethodFlags::PropertySetter);
				if(!isProperty)
				{
					std::string returnType;
					if (entry.returnInfo.type.empty())
						returnType = "void";
					else
					{
						UserTypeInfo paramTypeInfo = getTypeInfo(entry.returnInfo.type, entry.returnInfo.flags);
						returnType = getCSVarType(entry.returnInfo.type, paramTypeInfo.type, entry.returnInfo.flags, false, true);
					}

					methods << entry.documentation;
					methods << "\t\tpublic " << returnType << " " << typeInfo.scriptName << "(" << generateCSMethodParams(entry) << ")" << std::endl;
					methods << "\t\t{" << std::endl;

					if (!entry.returnInfo.type.empty())
						methods << "\t\t\treturn ";
					else
						methods << "\t\t\t";

					if (!isStatic)
						methods << "Internal_" << entry.interopName << "(mCachedPtr, " << generateCSMethodArgs(entry) << ");" << std::endl;
					else
						methods << "Internal_" << entry.interopName << "(" << generateCSMethodArgs(entry) << ");" << std::endl;

					methods << "\t\t}" << std::endl;
					methods << std::endl;
				}
			}

			// Generate interop
			interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]" << std::endl;

			if(!isStatic)
				interops << "\t\tprivate static extern void Internal_" << entry.interopName << "(IntPtr thisPtr, " << generateCSMethodParams(entry) << ");";
			else
				interops << "\t\tprivate static extern void Internal_" << entry.interopName << "(" << generateCSMethodParams(entry) << ");";

			interops << std::endl;
		}

		// Properties
		for (auto& entry : input.propertyInfos)
		{
			UserTypeInfo propTypeInfo = getTypeInfo(entry.type, entry.typeFlags);
			std::string propTypeName = getCSVarType(entry.type, propTypeInfo.type, entry.typeFlags, false, true);

			properties << "\t\tpublic " << propTypeName << " " << propTypeInfo.scriptName;
			properties << "\t\t{" << std::endl;

			if (!entry.getter.empty())
			{
				if (canBeReturned(propTypeInfo.type, entry.typeFlags))
					properties << "\t\t\tget { return Internal_" << entry.getter << "(mCachedPtr);" << std::endl;
				else
				{
					properties << "\t\t\tget" << std::endl;
					properties << "\t\t\t{" << std::endl;
					properties << "\t\t\t" << propTypeName << " temp;";

					properties << "\t\t\tInternal_" << entry.getter << "(";

					if (!entry.isStatic)
						properties << "mCachedPtr, ";

					properties << "ref temp);" << std::endl;

					properties << "\t\t\treturn temp;";
					properties << "\t\t\t}" << std::endl;
				}
			}

			if (!entry.setter.empty())
			{
				properties << "\t\t\tset { Internal_" << entry.setter << "(";

				if (!entry.isStatic)
					properties << "mCachedPtr, ";

				properties << "value);" << std::endl;
			}

			properties << "\t\t}" << std::endl;
			properties << std::endl;
		}

		std::stringstream output;

		if (input.visibility == CSVisibility::Internal)
			output << "\tinternal ";
		else if (input.visibility == CSVisibility::Public)
			output << "\tpublic ";

		output << input.documentation;

		// TODO - Handle inheritance from other types
		std::string baseType;
		if(typeInfo.type == ParsedType::Resource)
			baseType = "Resource";
		else if(typeInfo.type == ParsedType::Component)
			baseType = "Component";
		else
			baseType = "ScriptObject";

		output << "\tpartial class " << typeInfo.scriptName << " : " << baseType;

		output << std::endl;
		output << "\t{" << std::endl;

		output << ctors.str();
		output << properties.str();
		output << methods.str();
		output << interops.str();

		output << "\t}" << std::endl;
		return output.str();
	}

	std::string generateCSStruct(StructInfo& input)
	{
		std::stringstream output;

		if (input.visibility == CSVisibility::Internal)
			output << "\tinternal ";
		else if (input.visibility == CSVisibility::Public)
			output << "\tpublic ";

		std::string scriptName = cppToCsTypeMap[input.name].scriptName;

		output << input.documentation;
		output << "\tpartial struct " << scriptName;

		output << std::endl;
		output << "\t{" << std::endl;

		for (auto& entry : input.ctors)
		{
			output << "\t\tpublic " << scriptName << "(";

			for (auto I = entry.params.begin(); I != entry.params.end(); ++I)
			{
				const VarInfo& paramInfo = *I;

				UserTypeInfo typeInfo = getTypeInfo(paramInfo.type, paramInfo.flags);

				if (!isValidStructType(typeInfo, paramInfo.flags))
				{
					// We report the error during field generation, as it checks for the same condition
					continue;
				}

				output << typeInfo.scriptName << " " << paramInfo.name;

				if (!paramInfo.defaultValue.empty())
					output << " = " << paramInfo.defaultValue;

				if ((I + 1) != entry.params.end())
					output << ", ";
			}

			output << ")" << std::endl;
			output << "\t\t{" << std::endl;

			for (auto I = entry.params.begin(); I != entry.params.end(); ++I)
			{
				const VarInfo& paramInfo = *I;

				UserTypeInfo typeInfo = getTypeInfo(paramInfo.type, paramInfo.flags);

				if (!isValidStructType(typeInfo, paramInfo.flags))
				{
					// We report the error during field generation, as it checks for the same condition
					continue;
				}

				auto iterFind = entry.fieldAssignments.find(I->name);
				if (iterFind == entry.fieldAssignments.end())
					continue;

				std::string fieldName = iterFind->first;
				std::string paramName = iterFind->second;

				output << "\t\t\t" << fieldName << " = " << paramName << ";" << std::endl;
			}

			output << "\t\t}" << std::endl;
			output << std::endl;
		}

		for (auto I = input.fields.begin(); I != input.fields.end(); ++I)
		{
			const VarInfo& fieldInfo = *I;

			UserTypeInfo typeInfo = getTypeInfo(fieldInfo.type, fieldInfo.flags);

			if (!isValidStructType(typeInfo, fieldInfo.flags))
			{
				errs() << "Invalid field type found in struct \"" << scriptName << "\" for field \"" << fieldInfo.name << "\". Skipping.";
				continue;
			}

			output << "\t\tpublic ";
			output << typeInfo.scriptName << " ";
			output << fieldInfo.name;

			if (!fieldInfo.defaultValue.empty())
				output << " = " << fieldInfo.defaultValue;

			output << ";" << std::endl;
		}

		output << "\t}" << std::endl;
		return output.str();
	}

	void generateAll()
	{
		postProcessFileInfos();

		StringRef outputFolder = OutputOption.getValue();
		std::stringstream generatedCppFileList;
		std::stringstream generatedCsFileList;

		{
			std::string folderName = "Cpp/Include";
			StringRef filenameRef(folderName.data(), folderName.size());

			SmallString<128> folderPath = outputFolder;
			sys::path::append(folderPath, filenameRef);

			sys::fs::create_directories(folderPath);
		}

		{
			std::string folderName = "Cpp/Source";
			StringRef filenameRef(folderName.data(), folderName.size());

			SmallString<128> folderPath = outputFolder;
			sys::path::append(folderPath, filenameRef);

			sys::fs::create_directories(folderPath);
		}

		{
			std::string folderName = "Cs";
			StringRef filenameRef(folderName.data(), folderName.size());

			SmallString<128> folderPath = outputFolder;
			sys::path::append(folderPath, filenameRef);

			sys::fs::create_directories(folderPath);
		}
		
		// Generate H
		for(auto& fileInfo : outputFileInfos)
		{
			std::stringstream body;

			auto& classInfos = fileInfo.second.classInfos;
			auto& structInfos = fileInfo.second.structInfos;

			for(auto I = classInfos.begin(); I != classInfos.end(); ++I)
			{
				ClassInfo& classInfo = *I;
				UserTypeInfo& typeInfo = cppToCsTypeMap[classInfo.name];

				body << generateCppHeaderOutput(classInfo, typeInfo);

				if ((I + 1) != classInfos.end() || !structInfos.empty())
					body << std::endl;
			}

			for (auto I = structInfos.begin(); I != structInfos.end(); ++I)
			{
				StructInfo& structInfo = *I;
				body << generateCppStructHeader(structInfo);

				if ((I + 1) != structInfos.end())
					body << std::endl;
			}

			std::string filename = "Cpp/Include/BsScript" + fileInfo.first + ".h";
			StringRef filenameRef(filename.data(), filename.size());

			SmallString<128> filepath = outputFolder;
			sys::path::append(filepath, filenameRef);

			std::ofstream output;
			output.open(filepath.str(), std::ios::out);

			output << "#pragma once" << std::endl;
			output << std::endl;

			// Output includes
			for (auto& include : fileInfo.second.referencedHeaderIncludes)
				output << "#include \"" << include << "\"";

			output << std::endl;

			output << "namespace bs" << std::endl;
			output << "{" << std::endl;
			output << body.str();
			output << "}" << std::endl;

			output.close();

			generatedCppFileList << filename << std::endl;
		}

		// Generate CPP
		for (auto& fileInfo : outputFileInfos)
		{
			std::stringstream body;

			auto& classInfos = fileInfo.second.classInfos;
			auto& structInfos = fileInfo.second.structInfos;

			for (auto I = classInfos.begin(); I != classInfos.end(); ++I)
			{
				ClassInfo& classInfo = *I;
				UserTypeInfo& typeInfo = cppToCsTypeMap[classInfo.name];

				body << generateCppSourceOutput(classInfo, typeInfo);

				if ((I + 1) != classInfos.end() || !structInfos.empty())
					body << std::endl;
			}

			for (auto I = structInfos.begin(); I != structInfos.end(); ++I)
			{
				body << generateCppStructSource(*I);

				if ((I + 1) != structInfos.end())
					body << std::endl;
			}

			std::string filename = "Cpp/Source/BsScript" + fileInfo.first + ".cpp";
			StringRef filenameRef(filename.data(), filename.size());

			SmallString<128> filepath = outputFolder;
			sys::path::append(filepath, filenameRef);

			std::ofstream output;
			output.open(filepath.str(), std::ios::out);

			// Output includes
			for (auto& include : fileInfo.second.referencedSourceIncludes)
				output << "#include \"" << include << "\"";

			output << std::endl;

			output << "namespace bs" << std::endl;
			output << "{" << std::endl;
			output << body.str();
			output << "}" << std::endl;

			output.close();

			generatedCppFileList << filename << std::endl;
		}

		// Generate CS
		for (auto& fileInfo : outputFileInfos)
		{
			std::stringstream body;

			auto& classInfos = fileInfo.second.classInfos;
			auto& structInfos = fileInfo.second.structInfos;
			auto& enumInfos = fileInfo.second.enumInfos;

			for (auto I = classInfos.begin(); I != classInfos.end(); ++I)
			{
				ClassInfo& classInfo = *I;
				UserTypeInfo& typeInfo = cppToCsTypeMap[classInfo.name];

				body << generateCSClass(classInfo, typeInfo);

				if ((I + 1) != classInfos.end() || !structInfos.empty() || !enumInfos.empty())
					body << std::endl;
			}

			for (auto I = structInfos.begin(); I != structInfos.end(); ++I)
			{
				body << generateCSStruct(*I);

				if ((I + 1) != structInfos.end() || !enumInfos.empty())
					body << std::endl;
			}

			for (auto I = enumInfos.begin(); I != enumInfos.end(); ++I)
			{
				body << I->code;

				if ((I + 1) != enumInfos.end())
					body << std::endl;
			}

			std::string filename = "Cs/" + fileInfo.first + ".cs";
			StringRef filenameRef(filename.data(), filename.size());

			SmallString<128> filepath = outputFolder;
			sys::path::append(filepath, filenameRef);

			std::ofstream output;
			output.open(filepath.str(), std::ios::out);

			output << "using System;" << std::endl;
			output << "using System.Runtime.CompilerServices;" << std::endl;
			output << "using System.Runtime.InteropServices;" << std::endl;

			if (fileInfo.second.inEditor)
				output << "using BansheeEngine;" << std::endl;

			output << std::endl;

			if(!fileInfo.second.inEditor)
				output << "namespace BansheeEngine" << std::endl;
			else
				output << "namespace BansheeEditor" << std::endl;

			output << "{" << std::endl;
			output << body.str();
			output << "}" << std::endl;

			output.close();

			generatedCsFileList << filename;
		}

		{
			std::string filename = "generatedCppFiles.txt";
			StringRef filenameRef(filename.data(), filename.size());

			SmallString<128> filepath = outputFolder;
			sys::path::append(filepath, filenameRef);

			std::ofstream output;
			output.open(filepath.str(), std::ios::out);
			output << generatedCppFileList.str();
			output.close();
		}

		{
			std::string filename = "generatedCsFiles.txt";
			StringRef filenameRef(filename.data(), filename.size());

			SmallString<128> filepath = outputFolder;
			sys::path::append(filepath, filenameRef);

			std::ofstream output;
			output.open(filepath.str(), std::ios::out);
			output << generatedCsFileList.str();
			output.close();
		}
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
		visitor->generateAll();
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
	CommonOptionsParser op(argc, argv, OptCategory);
	ClangTool Tool(op.getCompilations(), op.getSourcePathList());

	std::unique_ptr<FrontendActionFactory> factory = newFrontendActionFactory<ScriptExportFrontendAction>();
	int output = Tool.run(factory.get());

	system("pause");
	return output;
}

