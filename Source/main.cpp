#include "common.h"
#include "parser.h"

const char* BUILTIN_COMPONENT_TYPE = "Component";
const char* BUILTIN_SCENEOBJECT_TYPE = "SceneObject";
const char* BUILTIN_RESOURCE_TYPE = "Resource";
const char* BUILTIN_MODULE_TYPE = "Module";
const char* BUILTIN_GUIELEMENT_TYPE = "GUIElement";
const char* BUILTIN_REFLECTABLE_TYPE = "IReflectable";

std::string sFrameworkCppNs = "bs";
std::string sEditorCppNs = "bs";
std::string sFrameworkCsNs = "bs";
std::string sEditorCsNs = "bs.Editor";
std::string sFrameworkExportMacro = "BS_SCR_BE_EXPORT";
std::string sEditorExportMacro = "BS_SCR_BED_EXPORT";

std::unordered_map<std::string, UserTypeInfo> cppToCsTypeMap;
std::unordered_map<std::string, FileInfo> outputFileInfos;
std::unordered_map<std::string, ExternalClassInfos> externalClassInfos;
std::unordered_map<std::string, BaseClassInfo> baseClassLookup;

std::vector<CommentInfo> commentInfos;
std::unordered_map<std::string, int> commentFullLookup;
std::unordered_map<std::string, SmallVector<int, 2>> commentSimpleLookup;

static cl::OptionCategory OptCategory("Script binding options");
static cl::extrahelp CommonHelp(CommonOptionsParser::HelpMessage);
static cl::extrahelp CustomHelp("\nAdd \"-- <compiler arguments>\" at the end to setup the compiler "
	"invocation\n");

static cl::opt<std::string> OutputCppEngineOption(
	"output-cpp",
	cl::desc("Specify output directory. Generated non-editor CPP files will be placed into that folder.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> OutputCppEditorOption(
	"output-cpp-editor",
	cl::desc("Specify output directory. Generated editor CPP files will be placed into that folder.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> OutputCSEngineOption(
	"output-cs",
	cl::desc("Specify output directory. Generated non-editor CS files will be placed relative to that folder.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> OutputCSEditorOption(
	"output-cs-editor",
	cl::desc("Specify output directory. Generated editor CS files will be placed relative to that folder.\n"),
	cl::cat(OptCategory));

static cl::opt<bool> GenerateEditorOption(
	"gen-editor",
	cl::desc("If enabled the script code marked with BED API will be generated as well.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppFrameworkNamespaceOption(
	"cpp-framework-ns",
	cl::desc("Specify namespace to place generated C++ framework types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppEditorNamespaceOption(
	"cpp-editor-ns",
	cl::desc("Specify namespace to place generated C++ editor types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CsFrameworkNamespaceOption(
	"cs-framework-ns",
	cl::desc("Specify namespace to place generated C# framework types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CsEditorNamespaceOption(
	"cs-editor-ns",
	cl::desc("Specify namespace to place generated C# editor types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppFrameworkExportMacroOption(
	"cpp-framework-export-macro",
	cl::desc("Specify DLL export macro to use for generated C++ framework types.\n"),
	cl::cat(OptCategory));

static cl::opt<std::string> CppEditorExportMacroOption(
	"cpp-editor-export-macro",
	cl::desc("Specify DLL export macro to use for generated C++ editor types.\n"),
	cl::cat(OptCategory));

class ScriptExportConsumer : public ASTConsumer 
{
public:
	explicit ScriptExportConsumer(CompilerInstance* CI)
		: visitor(new ScriptExportParser(CI))
	{ }

	~ScriptExportConsumer()
	{
		delete visitor;
	}

	void HandleTranslationUnit(ASTContext& Context) override
	{
		visitor->TraverseDecl(Context.getTranslationUnitDecl());
	}

private:
	ScriptExportParser *visitor;
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

	if (!CppFrameworkNamespaceOption.getValue().empty())
		sFrameworkCppNs = std::string(CppFrameworkNamespaceOption.getValue().c_str());
	
	if (!CppEditorNamespaceOption.getValue().empty())
		sEditorCppNs = std::string(CppEditorNamespaceOption.getValue().c_str());
	
	if (!CsFrameworkNamespaceOption.getValue().empty())
		sFrameworkCsNs = std::string(CsFrameworkNamespaceOption.getValue().c_str());
	
	if (!CsEditorNamespaceOption.getValue().empty())
		sEditorCsNs = std::string(CsEditorNamespaceOption.getValue().c_str());
	
	if (!CppFrameworkExportMacroOption.getValue().empty())
		sFrameworkExportMacro = std::string(CppFrameworkExportMacroOption.getValue().c_str());
	
	if (!CppEditorExportMacroOption.getValue().empty())
		sEditorExportMacro = std::string(CppEditorExportMacroOption.getValue().c_str());
	
	// Note: I could auto-generate C++ wrappers for these types
	SmallVector<std::string, 4> frameworkNs = { sFrameworkCppNs };
	
	cppToCsTypeMap["Vector2"] = UserTypeInfo(frameworkNs,"Vector2", ParsedType::Struct, "Math/BsVector2.h", "Wrappers/BsScriptVector.h");
	cppToCsTypeMap["Vector3"] = UserTypeInfo(frameworkNs, "Vector3", ParsedType::Struct, "Math/BsVector3.h", "Wrappers/BsScriptVector.h");
	cppToCsTypeMap["Vector4"] = UserTypeInfo(frameworkNs, "Vector4", ParsedType::Struct, "Math/BsVector4.h", "Wrappers/BsScriptVector.h");
	cppToCsTypeMap["Matrix3"] = UserTypeInfo(frameworkNs, "Matrix3", ParsedType::Struct, "Math/BsMatrix3.h", "");
	cppToCsTypeMap["Matrix4"] = UserTypeInfo(frameworkNs, "Matrix4", ParsedType::Struct, "Math/BsMatrix4.h", "");
	cppToCsTypeMap["Quaternion"] = UserTypeInfo(frameworkNs, "Quaternion", ParsedType::Struct, "Math/BsQuaternion.h", "Wrappers/BsScriptQuaternion.h");
	cppToCsTypeMap["Radian"] = UserTypeInfo(frameworkNs, "Radian", ParsedType::Struct, "Math/BsRadian.h", "");
	cppToCsTypeMap["Degree"] = UserTypeInfo(frameworkNs, "Degree", ParsedType::Struct, "Math/BsDegree.h", "");
	cppToCsTypeMap["Color"] = UserTypeInfo(frameworkNs, "Color", ParsedType::Struct, "Image/BsColor.h", "Wrappers/BsScriptColor.h");
	cppToCsTypeMap["AABox"] = UserTypeInfo(frameworkNs, "AABox", ParsedType::Struct, "Math/BsAABox.h", "");
	cppToCsTypeMap["Sphere"] = UserTypeInfo(frameworkNs, "Sphere", ParsedType::Struct, "Math/BsSphere.h", "");
	cppToCsTypeMap["Capsule"] = UserTypeInfo(frameworkNs, "Capsule", ParsedType::Struct, "Math/BsCapsule.h", "");
	cppToCsTypeMap["Ray"] = UserTypeInfo(frameworkNs, "Ray", ParsedType::Struct, "Math/BsRay.h", "");
	cppToCsTypeMap["Vector2I"] = UserTypeInfo(frameworkNs, "Vector2I", ParsedType::Struct, "Math/BsVector2I.h", "Wrappers/BsScriptVector2I.h");
	cppToCsTypeMap["Rect2"] = UserTypeInfo(frameworkNs, "Rect2", ParsedType::Struct, "Math/BsRect2.h", "");
	cppToCsTypeMap["Rect2I"] = UserTypeInfo(frameworkNs, "Rect2I", ParsedType::Struct, "Math/BsRect2I.h", "");
	cppToCsTypeMap["Bounds"] = UserTypeInfo(frameworkNs, "Bounds", ParsedType::Struct, "Math/BsBounds.h", "");
	cppToCsTypeMap["Plane"] = UserTypeInfo(frameworkNs, "Plane", ParsedType::Struct, "Math/BsPlane.h", "Wrappers/BsScriptPlane.h");
	cppToCsTypeMap["UUID"] = UserTypeInfo(frameworkNs, "UUID", ParsedType::Struct, "Utility/BsUUID.h", "");
	cppToCsTypeMap["SceneObject"] = UserTypeInfo(frameworkNs, "SceneObject", ParsedType::SceneObject, "Scene/BsSceneObject.h", "Wrappers/BsScriptSceneObject.h");
	cppToCsTypeMap["Resource"] = UserTypeInfo(frameworkNs, "Resource", ParsedType::Resource, "Resources/BsResource.h", "Wrappers/BsScriptResource.h");
	cppToCsTypeMap["Any"] = UserTypeInfo(frameworkNs, "Any", ParsedType::Class, "Utility/BsAny.h", "");

	// Parse C++ into an easy to read format
	std::unique_ptr<FrontendActionFactory> factory = newFrontendActionFactory<ScriptExportFrontendAction>();
	int output = Tool.run(factory.get());

	bool genEditor = GenerateEditorOption.getValue();

	// Generate code
	generateAll(
		OutputCppEngineOption.getValue(), 
		OutputCppEditorOption.getValue(),
		OutputCSEngineOption.getValue(),
		OutputCSEditorOption.getValue(),
		genEditor);

	//system("pause");
	return output;
}

