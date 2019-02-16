#include "common.h"
#include "parser.h"

const char* BUILTIN_COMPONENT_TYPE = "Component";
const char* BUILTIN_SCENEOBJECT_TYPE = "SceneObject";
const char* BUILTIN_RESOURCE_TYPE = "Resource";
const char* BUILTIN_MODULE_TYPE = "Module";
const char* BUILTIN_GUIELEMENT_TYPE = "GUIElement";

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
	// Note: I could auto-generate C++ wrappers for these types
	cppToCsTypeMap["Vector2"] = UserTypeInfo("Vector2", ParsedType::Struct, "Math/BsVector2.h", "Wrappers/BsScriptVector.h");
	cppToCsTypeMap["Vector3"] = UserTypeInfo("Vector3", ParsedType::Struct, "Math/BsVector3.h", "Wrappers/BsScriptVector.h");
	cppToCsTypeMap["Vector4"] = UserTypeInfo("Vector4", ParsedType::Struct, "Math/BsVector4.h", "Wrappers/BsScriptVector.h");
	cppToCsTypeMap["Matrix3"] = UserTypeInfo("Matrix3", ParsedType::Struct, "Math/BsMatrix3.h", "");
	cppToCsTypeMap["Matrix4"] = UserTypeInfo("Matrix4", ParsedType::Struct, "Math/BsMatrix4.h", "");
	cppToCsTypeMap["Quaternion"] = UserTypeInfo("Quaternion", ParsedType::Struct, "Math/BsQuaternion.h", "Wrappers/BsScriptQuaternion.h");
	cppToCsTypeMap["Radian"] = UserTypeInfo("Radian", ParsedType::Struct, "Math/BsRadian.h", "");
	cppToCsTypeMap["Degree"] = UserTypeInfo("Degree", ParsedType::Struct, "Math/BsDegree.h", "");
	cppToCsTypeMap["Color"] = UserTypeInfo("Color", ParsedType::Struct, "Image/BsColor.h", "Wrappers/BsScriptColor.h");
	cppToCsTypeMap["AABox"] = UserTypeInfo("AABox", ParsedType::Struct, "Math/BsAABox.h", "");
	cppToCsTypeMap["Sphere"] = UserTypeInfo("Sphere", ParsedType::Struct, "Math/BsSphere.h", "");
	cppToCsTypeMap["Capsule"] = UserTypeInfo("Capsule", ParsedType::Struct, "Math/BsCapsule.h", "");
	cppToCsTypeMap["Ray"] = UserTypeInfo("Ray", ParsedType::Struct, "Math/BsRay.h", "");
	cppToCsTypeMap["Vector2I"] = UserTypeInfo("Vector2I", ParsedType::Struct, "Math/BsVector2I.h", "Wrappers/BsScriptVector2I.h");
	cppToCsTypeMap["Rect2"] = UserTypeInfo("Rect2", ParsedType::Struct, "Math/BsRect2.h", "");
	cppToCsTypeMap["Rect2I"] = UserTypeInfo("Rect2I", ParsedType::Struct, "Math/BsRect2I.h", "");
	cppToCsTypeMap["Bounds"] = UserTypeInfo("Bounds", ParsedType::Struct, "Math/BsBounds.h", "");
	cppToCsTypeMap["Plane"] = UserTypeInfo("Plane", ParsedType::Struct, "Math/BsPlane.h", "Wrappers/BsScriptPlane.h");
	cppToCsTypeMap["UUID"] = UserTypeInfo("UUID", ParsedType::Struct, "Utility/BsUUID.h", "");
	cppToCsTypeMap["SceneObject"] = UserTypeInfo("SceneObject", ParsedType::SceneObject, "Scene/BsSceneObject.h", "Wrappers/BsScriptSceneObject.h");
	cppToCsTypeMap["Resource"] = UserTypeInfo("Resource", ParsedType::Resource, "Resources/BsResource.h", "Wrappers/BsScriptResource.h");

	CommonOptionsParser op(argc, argv, OptCategory);
	ClangTool Tool(op.getCompilations(), op.getSourcePathList());

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

