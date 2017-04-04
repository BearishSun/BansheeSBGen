#pragma once

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

extern const char* BUILTIN_COMPONENT_TYPE;
extern const char* BUILTIN_SCENEOBJECT_TYPE;
extern const char* BUILTIN_RESOURCE_TYPE;

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
	PropertySetter = 1 << 4,
	InteropOnly = 1 << 5
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
	Editor = 1 << 5,
	Exclude = 1 << 6,
	InteropOnly = 1 << 7
};

enum class ClassFlags
{
	Editor = 1 << 0,
	IsBase = 1 << 1,
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
	int flags;

	std::vector<MethodInfo> ctorInfos;
	std::vector<PropertyInfo> propertyInfos;
	std::vector<MethodInfo> methodInfos;
	std::string baseClass;
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
	std::string name;
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
	IncludeInfo() { }
	IncludeInfo(const std::string& typeName, const UserTypeInfo& typeInfo, bool sourceInclude)
		:typeName(typeName), typeInfo(typeInfo), sourceInclude(sourceInclude)
	{ }

	std::string typeName;
	UserTypeInfo typeInfo;
	bool sourceInclude;
};

enum FileType
{
	FT_ENGINE_H,
	FT_ENGINE_CPP,
	FT_EDITOR_H,
	FT_EDITOR_CPP,
	FT_ENGINE_CS,
	FT_EDITOR_CS,
	FT_COUNT // Keep at end
};

extern std::array<std::string, FT_COUNT> fileTypeFolders;

extern std::unordered_map<std::string, UserTypeInfo> cppToCsTypeMap;
extern std::unordered_map<std::string, FileInfo> outputFileInfos;
extern std::unordered_map<std::string, ExternalMethodInfos> externalMethodInfos;

inline bool mapBuiltinTypeToCSType(BuiltinType::Kind kind, std::string& output)
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

	errs() << "Unrecognized builtin type found.\n";
	return false;
}

inline std::string mapCppTypeToCSType(const std::string& cppType)
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

inline bool mapBuiltinTypeToCppType(BuiltinType::Kind kind, std::string& output)
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

	errs() << "Unrecognized builtin type found.\n";
	return false;
}

inline UserTypeInfo getTypeInfo(const std::string& sourceType, int flags)
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

		errs() << "Unable to map type \"" << sourceType << "\". Assuming same name as source.\n";
		return outType;
	}

	return iterFind->second;
}

inline bool isOutput(int flags)
{
	return (flags & (int)TypeFlags::Output) != 0;
}

inline bool isArray(int flags)
{
	return (flags & (int)TypeFlags::Array) != 0;
}

inline bool isSrcPointer(int flags)
{
	return (flags & (int)TypeFlags::SrcPtr) != 0;
}

inline bool isSrcReference(int flags)
{
	return (flags & (int)TypeFlags::SrcRef) != 0;
}

inline bool isSrcValue(int flags)
{
	int nonValueFlags = (int)TypeFlags::SrcPtr | (int)TypeFlags::SrcRef | (int)TypeFlags::SrcSPtr |
		(int)TypeFlags::SrcRHandle | (int)TypeFlags::SrcGHandle;

	return (flags & nonValueFlags) == 0;
}

inline bool isSrcSPtr(int flags)
{
	return (flags & (int)TypeFlags::SrcSPtr) != 0;
}

inline bool isSrcRHandle(int flags)
{
	return (flags & (int)TypeFlags::SrcRHandle) != 0;
}

inline bool isSrcGHandle(int flags)
{
	return (flags & (int)TypeFlags::SrcGHandle) != 0;
}

inline bool isHandleType(ParsedType type)
{
	return type == ParsedType::Resource || type == ParsedType::SceneObject || type == ParsedType::Component;
}

inline bool canBeReturned(ParsedType type, int flags)
{
	if (isOutput(flags))
		return false;

	if (isArray(flags))
		return true;

	if (type == ParsedType::Struct)
		return false;

	return true;
}

void generateAll(StringRef cppOutputFolder, StringRef csEngineOutputFolder, StringRef csEditorOutputFolder);