#include "common.h"
#include <chrono>

std::string getInteropCppVarType(const std::string& typeName, ParsedType type, int flags, bool forStruct = false)
{
	if (isArrayOrVector(flags))
	{
		if (isOutput(flags) && !forStruct)
			return "MonoArray**";
		else
			return "MonoArray*";
	}

	switch (type)
	{
	case ParsedType::Builtin:
		if (isOutput(flags) && !forStruct)
			return typeName + "*";
		else
			return typeName;
	case ParsedType::Enum:
		if (isFlagsEnum(flags) && forStruct)
			return "Flags<" + typeName + ">";
		else
		{
			if (isOutput(flags) && !forStruct)
				return typeName + "*";
			else
				return typeName;
		}
	case ParsedType::Struct:
		if(isComplexStruct(flags))
		{
			if (forStruct)
				return getStructInteropType(typeName);
			else
				return getStructInteropType(typeName) + "*";
		}
		else
		{
			if (forStruct)
				return typeName;
			else
				return typeName + "*";
		}
	case ParsedType::String:
	case ParsedType::WString:
	case ParsedType::Path:
		if (isOutput(flags) && !forStruct)
			return "MonoString**";
		else
			return "MonoString*";
	default: // Class, resource, component or ScriptObject
		if (isOutput(flags) && !forStruct)
			return "MonoObject**";
		else
			return "MonoObject*";
	}
}

std::string getCppVarType(const std::string& typeName, ParsedType type, int flags = 0, bool assumeDefaultTypes = true)
{
	if (type == ParsedType::Resource)
		return "ResourceHandle<" + typeName + ">";
	else if (type == ParsedType::SceneObject || type == ParsedType::Component)
		return "GameObjectHandle<" + typeName + ">";
	else if (isClassType(type))
	{
		if(assumeDefaultTypes || isSrcSPtr(flags))
			return "SPtr<" + typeName + ">";
		else
		{
			if(isSrcPointer(flags))
				return typeName + "*";
			else if(isSrcReference(flags))
				return typeName + "&";
			else
				return typeName;
		}
	}
	else if (type == ParsedType::String)
		return "String";
	else if (type == ParsedType::WString)
		return "WString";
	else if (type == ParsedType::Path)
		return "Path";
	else if (type == ParsedType::Enum && isFlagsEnum(flags))
		return "Flags<" + typeName + ">";
	else if(type == ParsedType::GUIElement)
		return typeName + "*";
	else
		return typeName;
}

std::string getCSVarType(const std::string& typeName, ParsedType type, int flags, bool paramPrefixes,
	bool arraySuffixes, bool forceStructAsRef, bool forSignature = false)
{
	std::stringstream output;

	if (!forSignature)
	{
		if (paramPrefixes && isOutput(flags))
			output << "out ";
		else if (forceStructAsRef && (isPlainStruct(type, flags)))
			output << "ref ";
	}

	output << typeName;

	if (arraySuffixes && isArrayOrVector(flags))
		output << "[]";

	if (forSignature)
	{
		if (paramPrefixes && isOutput(flags))
			output << "&";
		else if (forceStructAsRef && (isPlainStruct(type, flags)))
			output << "&";
	}

	return output.str();
}

std::string generateGetInternalLine(const std::string& sourceClassName, const std::string& obj, ParsedType classType, int flags)
{
	bool isRRef = getPassAsResourceRef(flags);
	bool isBase = isBaseParam(flags);

	std::stringstream output;
	if (isClassType(classType))
		output << obj << "->getInternal()";
	else if(classType == ParsedType::GUIElement)
		output << "static_cast<" << sourceClassName << "*>(" << obj << "->getGUIElement())";
	else // Must be one of the handle types
	{
		assert(isHandleType(classType));

		if (!isBase || isRRef)
		{
			if(isRRef)
				output << "static_resource_cast<" << sourceClassName << ">(" << obj << "->getHandle())";
			else
			{
				if(classType == ParsedType::Resource && sourceClassName == "Resource")
					output << "static_resource_cast<" << sourceClassName << ">(" << obj << "->getGenericHandle())";
				else
					output << obj << "->getHandle()";
			}
		}
		else
		{
			if (classType == ParsedType::Resource)
				output << "static_resource_cast<" << sourceClassName << ">(" << obj << "->getGenericHandle())";
			else if (classType == ParsedType::Component)
				output << "static_object_cast<" << sourceClassName << ">(" << obj << "->getComponent())";
		}
	}
	
	return output.str();
}

std::string generateManagedToScriptObjectLine(const std::string& indent, const std::string& scriptType, 
	const std::string& scriptName, const std::string& name, ParsedType type, int flags)
{
	bool isRRef = getPassAsResourceRef(flags);
	bool isBase = isBaseParam(flags);

	std::stringstream output;
	if (!isBase || isRRef)
	{
		output << indent << scriptType << "* " << scriptName << ";" << std::endl;
		output << indent << scriptName << " = " << scriptType << "::toNative(" << name << ");" << std::endl;
	}
	else
	{
		std::string scriptBaseType;
		if(type == ParsedType::GUIElement)
			scriptBaseType = "ScriptGUIElementBaseTBase";
		else
			scriptBaseType = scriptType + "Base";

		output << indent << scriptBaseType << "* " << scriptName << ";" << std::endl;
		output << indent << scriptName << " = (" << scriptBaseType << "*)" << scriptType << "::toNative(" << name << ");" << std::endl;
	}

	return output.str();
}

std::string getAsManagedToCppArgumentPlain (const std::string& name, int flags, bool isPtr, const std::string& methodName)
{
	if (isSrcPointer(flags))
		return (isPtr ? "" : "&") + name;
	else if (isSrcReference(flags) || isSrcValue(flags))
		return (isPtr ? "*" : "") + name;
	else
		return name;
}

std::string getAsManagedToCppArgument(const std::string& name, ParsedType type, int flags, const std::string& methodName)
{
	switch (type)
	{
	case ParsedType::Builtin:
	case ParsedType::Enum: // Input type is either value or pointer depending if output or not
		return getAsManagedToCppArgumentPlain(name, flags, isOutput(flags), methodName);
	case ParsedType::Struct: // Input type is always a pointer
		if (isComplexStruct(flags))
			return getAsManagedToCppArgumentPlain(name, flags, false, methodName);
		else
			return getAsManagedToCppArgumentPlain(name, flags, true, methodName);
	case ParsedType::MonoObject: // Input type is either a pointer or a pointer to pointer, depending if output or not
		{
			if (isOutput(flags))
				return "&" + name;
			else
				return name;
		}
	case ParsedType::String: // Input type is always a value
	case ParsedType::WString: 
	case ParsedType::Path:
		return getAsManagedToCppArgumentPlain(name, flags, false, methodName);
	case ParsedType::GUIElement: // Input type is always a pointer
		return getAsManagedToCppArgumentPlain(name, flags, true, methodName);
	case ParsedType::Component: // Input type is always a handle
	case ParsedType::SceneObject:
	case ParsedType::Resource:
	{
		if (isSrcRHandle(flags) || isSrcGHandle(flags))
			return name;
		else if (isSrcSPtr(flags))
			return name + ".getInternalPtr()";
		else if (isSrcPointer(flags))
			return name + ".get()";
		else if (isSrcReference(flags) || isSrcValue(flags))
			return "*" + name;
		else
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}
	}
	case ParsedType::Class: // Input type is always a SPtr
	case ParsedType::ReflectableClass:
	{
		assert(!isSrcRHandle(flags) && !isSrcGHandle(flags));

		if (isSrcPointer(flags))
			return name + ".get()";
		else if (isSrcSPtr(flags))
			return name;
		else if (isSrcReference(flags) || isSrcValue(flags))
			return "*" + name;
		else
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}

	}
	default: // Some object type
		assert(false);
		return "";
	}
}

std::string getAsCppToManagedArgument(const std::string& name, ParsedType type, int flags, const std::string& methodName)
{
	switch (type)
	{
	case ParsedType::Builtin:
	case ParsedType::Enum: // Always passed as value type, input can be either pointer or ref/value type
	{
		if (isSrcPointer(flags))
			return "*" + name;
		else if (isSrcReference(flags) || isSrcValue(flags))
			return name;
		else
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}
	}
	case ParsedType::Struct: // Always passed as a pointer, input can be either pointer or ref/value type
	{
		if (isSrcPointer(flags))
			return name;
		else if (isSrcReference(flags) || isSrcValue(flags))
			return "&" + name;
		else
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}
	}
	case ParsedType::MonoObject: // Always passed as a pointer, input must always be a pointer
	case ParsedType::String:
	case ParsedType::WString:
	case ParsedType::Path:
	case ParsedType::Component:
	case ParsedType::SceneObject:
	case ParsedType::Resource:
	case ParsedType::Class:
	case ParsedType::ReflectableClass:
			return name;
	default: // Some object type
		assert(false);
		return "";
	}
}

std::string getAsCppToInteropArgument(const std::string& name, ParsedType type, int flags, const std::string& methodName)
{
	switch (type)
	{
	case ParsedType::Builtin: // Always passed as value type, input can be either pointer or ref/value type
	case ParsedType::Enum: 
	case ParsedType::String:
	case ParsedType::WString:
	case ParsedType::Path:
	case ParsedType::Struct:
	{
		if (isSrcPointer(flags))
			return "*" + name;
		else if (isSrcReference(flags) || isSrcValue(flags))
			return name;
		else
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}
	}
	case ParsedType::MonoObject: // Always passed as a pointer, input must always be a pointer
	case ParsedType::GUIElement:
			return name;
	case ParsedType::Component: // Always passed as a handle, input must be a handle
		if (!isSrcGHandle(flags))
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";

		if(getIsComponentOrActor(flags))
			return name + ".getComponent()";

		return name;
	case ParsedType::SceneObject:
	case ParsedType::Resource:
	{
		if (isSrcRHandle(flags) || isSrcGHandle(flags))
			return name;
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}
	}
	case ParsedType::Class: // Always passed as a sptr, input can be a sptr, pointer, reference or value type
	case ParsedType::ReflectableClass:
	{
		assert(!isSrcRHandle(flags) && !isSrcGHandle(flags));

		if (isSrcPointer(flags))
			return "*" + name;
		else if (isSrcSPtr(flags))
			return name;
		else if (isSrcReference(flags) || isSrcValue(flags))
			return name;
		else
		{
			outs() << "Error: Unsure how to pass parameter \"" << name << "\" to method \"" << methodName << "\".\n";
			return name;
		}
	}
	default: // Some object type
		assert(false);
		return "";
	}
}

std::string getScriptInteropType(const std::string& name, bool resourceRef = false)
{
	auto iterFind = cppToCsTypeMap.find(name);
	if (iterFind == cppToCsTypeMap.end())
		outs() << "Warning: Type \"" << name << "\" referenced as a script interop type, but no script interop mapping found. Assuming default type name.\n";

	bool isValidInteropType = iterFind->second.type != ParsedType::Builtin &&
		iterFind->second.type != ParsedType::Enum &&
		iterFind->second.type != ParsedType::String &&
		iterFind->second.type != ParsedType::WString &&
		iterFind->second.type != ParsedType::Path;

	if (!isValidInteropType)
		outs() << "Error: Type \"" << name << "\" referenced as a script interop type, but script interop object cannot be generated for this object type.\n";

	std::string cleanName = cleanTemplParams(name);

	if(resourceRef)
	{
		if(iterFind->second.type != ParsedType::Resource)
			outs() << "Error: Type \"" << name << "\" cannot be wrapped in a resource reference.\n";

		return "ScriptRRefBase";
	}
	
	return "Script" + cleanName;
}

MethodInfo findUnusedCtorSignature(const ClassInfo& classInfo)
{
	auto checkSignature = [](int numParams, const MethodInfo& info)
	{
		if ((int)info.paramInfos.size() != numParams)
			return true;

		for (auto& paramInfo : info.paramInfos)
		{
			if (paramInfo.typeName != "bool")
				return true;
		}

		return false;
	};

	int numBools = 1;
	while (true)
	{
		bool isSignatureValid = true;

		// Check normal constructors
		for (auto& entry : classInfo.ctorInfos)
		{
			if(!checkSignature(numBools, entry))
			{
				isSignatureValid = false;
				break;
			}
		}

		// Check external constructors
		if(isSignatureValid)
		{
			for (auto& entry : classInfo.methodInfos)
			{
				bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
				if (!isConstructor)
					continue;

				if(!checkSignature(numBools, entry))
				{
					isSignatureValid = false;
					break;
				}
			}
		}

		if (isSignatureValid)
			break;

		numBools++;
	}

	MethodInfo output;
	output.sourceName = classInfo.cleanName;
	output.scriptName = classInfo.cleanName;
	output.flags = (int)MethodFlags::Constructor;
	output.visibility = CSVisibility::Private;

	for (int i = 0; i < numBools; i++)
	{
		VarInfo paramInfo;
		paramInfo.name = "__dummy" + std::to_string(i);
		paramInfo.typeName = "bool";
		paramInfo.flags = (int)TypeFlags::Builtin;

		output.paramInfos.push_back(paramInfo);
	}

	return output;
}

bool hasParameterlessConstructor(const ClassInfo& classInfo)
{
	// Check normal constructors
	for (auto& entry : classInfo.ctorInfos)
	{
		if (entry.paramInfos.size() == 0)
			return true;
	}

	// Check external constructors
	for (auto& entry : classInfo.methodInfos)
	{
		bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
		if (!isConstructor)
			continue;

		if (entry.paramInfos.size() == 0)
			return true;
	}

	return false;
}

void gatherIncludes(const std::string& typeName, int flags, bool isEditor, IncludesInfo& output)
{
	UserTypeInfo typeInfo = getTypeInfo(typeName, flags);
	if (typeInfo.type == ParsedType::Class || typeInfo.type == ParsedType::ReflectableClass || 
		typeInfo.type == ParsedType::Struct || typeInfo.type == ParsedType::Component || 
		typeInfo.type == ParsedType::SceneObject || typeInfo.type == ParsedType::Resource || 
		typeInfo.type == ParsedType::Enum)
	{
		auto iterFind = output.includes.find(typeName);
		if (iterFind == output.includes.end())
		{
			uint32_t sourceIncludeType = 0;
			uint32_t interopIncludeType = typeInfo.type != ParsedType::Enum ? IT_IMPL : 0;
			bool isStruct = false;

			if(getPassAsResourceRef(flags))
			{
				sourceIncludeType = IT_IMPL;
				interopIncludeType = 0;
			}
			
			if(typeInfo.type == ParsedType::Struct && !isComplexStruct(flags))
			{
				sourceIncludeType = IT_HEADER;
				isStruct = true;
			}

			// If enum or passed by value we need to include the header for the source type
			if(typeInfo.type == ParsedType::Enum || isSrcValue(flags))
				sourceIncludeType = IT_HEADER;

			// If a class is being passed by reference or a raw pointer then we need the declaration because we perform
			// assignment copy
			if (isClassType(typeInfo.type) && !isSrcSPtr(flags))
				sourceIncludeType = IT_HEADER;

			output.includes[typeName] = IncludeInfo(typeName, typeInfo, sourceIncludeType, interopIncludeType, isStruct, isEditor);
		}

		if (isClassType(typeInfo.type))
		{
			bool isBase = isBaseParam(flags);
			if (isBase)
			{
				std::vector<std::string> derivedClasses;
				getDerivedClasses(typeName, derivedClasses);

				for (auto& entry : derivedClasses)
					output.includes[entry] = IncludeInfo(entry, getTypeInfo(entry, 0), IT_IMPL, IT_IMPL, false, isEditor);

				output.requiresRTTI = true;
			}
		}
	}

	if (typeInfo.type == ParsedType::Struct && isComplexStruct(flags))
		output.fwdDecls[typeName] = { typeInfo.ns, getStructInteropType(typeName), true };

	if (typeInfo.type == ParsedType::Resource)
	{
		output.requiresResourceManager = true;

		if (getPassAsResourceRef(flags))
			output.requiresRRef = true;
	}
	else if (typeInfo.type == ParsedType::Component || typeInfo.type == ParsedType::SceneObject)
		output.requiresGameObjectManager = true;

   if(getIsAsyncOp(flags))
	   output.requiresAsyncOp = true;
}

void gatherIncludes(const MethodInfo& methodInfo, bool isEditor, IncludesInfo& output)
{
	bool returnAsParameter = false;
	if (!methodInfo.returnInfo.typeName.empty())
		gatherIncludes(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags, isEditor, output);

	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
		gatherIncludes(I->typeName, I->flags, isEditor, output);

	if((methodInfo.flags & (int)MethodFlags::External) != 0)
	{
		auto iterFind = output.includes.find(methodInfo.externalClass);
		if (iterFind == output.includes.end())
		{
			UserTypeInfo typeInfo = getTypeInfo(methodInfo.externalClass, 0);
			output.includes[methodInfo.externalClass] = IncludeInfo(methodInfo.externalClass, typeInfo, IT_FWD_AND_IMPL, 0, false, isEditor);
		}
	}
}

void gatherIncludes(const FieldInfo& fieldInfo, bool isEditor, IncludesInfo& output)
{
	UserTypeInfo fieldTypeInfo = getTypeInfo(fieldInfo.typeName, fieldInfo.flags);

	// These types never require additional includes
	if (fieldTypeInfo.type == ParsedType::Builtin || fieldTypeInfo.type == ParsedType::String || 
		fieldTypeInfo.type == ParsedType::WString || fieldTypeInfo.type == ParsedType::Path)
		return;

	// If passed by value, we needs its header in our header
	if (isSrcValue(fieldInfo.flags))
	{
		bool complexStruct = isComplexStruct(fieldInfo.flags);

		output.includes[fieldInfo.typeName] = IncludeInfo(fieldInfo.typeName, fieldTypeInfo, IT_HEADER, complexStruct ? IT_HEADER : 0, false, isEditor);
	}

	if (fieldTypeInfo.type == ParsedType::Class || fieldTypeInfo.type == ParsedType::ReflectableClass ||
		fieldTypeInfo.type == ParsedType::Struct || fieldTypeInfo.type == ParsedType::Component || 
		fieldTypeInfo.type == ParsedType::SceneObject || fieldTypeInfo.type == ParsedType::Resource)
	{
		bool isRRef = getPassAsResourceRef(fieldInfo.flags);

		if (!fieldTypeInfo.destFile.empty() || isRRef)
		{
			std::string name = "__" + fieldInfo.typeName;
			output.includes[name] = IncludeInfo(fieldInfo.typeName, fieldTypeInfo, IT_IMPL, IT_IMPL, false, isEditor);
		}

		if (fieldTypeInfo.type == ParsedType::Resource)
		{
			output.requiresResourceManager = true;

			if (getPassAsResourceRef(fieldInfo.flags))
				output.requiresRRef = true;
		}
		else if (fieldTypeInfo.type == ParsedType::Component || fieldTypeInfo.type == ParsedType::SceneObject)
			output.requiresGameObjectManager = true;
		else if(fieldTypeInfo.type == ParsedType::Class || fieldTypeInfo.type == ParsedType::ReflectableClass)
		{
			bool isBase = isBaseParam(fieldInfo.flags);
			if (isBase)
			{
				std::vector<std::string> derivedClasses;
				getDerivedClasses(fieldInfo.typeName, derivedClasses);

				for(auto& entry : derivedClasses)
					output.includes[entry] = IncludeInfo(entry, getTypeInfo(entry, 0), IT_IMPL, IT_IMPL, false, isEditor);

				output.requiresRTTI = true;
			}
		}

		if (getIsAsyncOp(fieldInfo.flags))
			output.requiresAsyncOp = true;
	}
}

void gatherIncludes(const ClassInfo& classInfo, IncludesInfo& output)
{
	bool isEditor = hasAPIBED(classInfo.api);

	for (auto& methodInfo : classInfo.ctorInfos)
		gatherIncludes(methodInfo, isEditor, output);

	for (auto& methodInfo : classInfo.methodInfos)
		gatherIncludes(methodInfo, isEditor, output);

	for (auto& eventInfo : classInfo.eventInfos)
		gatherIncludes(eventInfo, isEditor, output);
}

void gatherIncludes(const StructInfo& structInfo, IncludesInfo& output)
{
	bool isEditor = hasAPIBED(structInfo.api);

	if (structInfo.requiresInterop)
	{
		for (auto& fieldInfo : structInfo.fields)
			gatherIncludes(fieldInfo, isEditor, output);
	}
}

bool parseCopydocString(const std::string& str, const std::string& parentType, const SmallVector<std::string, 4>& curNS, CommentEntry& outputComment)
{
	StringRef inputStr(str.data(), str.length());
	inputStr = inputStr.trim();

	bool hasParamList = inputStr.find('(') != -1;

	StringRef fullTypeName;
	StringRef params;

	if (hasParamList)
	{
		auto paramSplit = inputStr.split('(');

		fullTypeName = paramSplit.first.trim();
		params = paramSplit.second.trim(") \t\n\v\f\r");
	}
	else
	{
		fullTypeName = inputStr;
	}

	SmallVector<StringRef, 4> typeSplits;
	fullTypeName.split(typeSplits, "::", -1, false);

	if (typeSplits.empty())
		typeSplits.push_back(fullTypeName);

	// Find matching type (no namespace)
	int namespaceStart = -1;
	std::string simpleTypeName;
	SmallVector<int, 2> lookup;

	if (typeSplits.size() > 1)
	{
		simpleTypeName = typeSplits[typeSplits.size() - 2].str() + "::" + typeSplits[typeSplits.size() - 1].str();
		namespaceStart = 2;

		auto iterFind = commentSimpleLookup.find(simpleTypeName);
		if (iterFind == commentSimpleLookup.end())
		{
			simpleTypeName = typeSplits[typeSplits.size() - 1].str();
			iterFind = commentSimpleLookup.find(simpleTypeName);
			namespaceStart = 1;
		}

		if (iterFind == commentSimpleLookup.end())
		{
			outs() << "Warning: Cannot find identifier referenced by the @copydoc command: \"" << str << "\".\n";
			return false;
		}
		else
			lookup = iterFind->second;
	}
	else
	{
		simpleTypeName = typeSplits[typeSplits.size() - 1].str();
		namespaceStart = 1;

		auto iterFind = commentSimpleLookup.find(simpleTypeName);
		if (iterFind == commentSimpleLookup.end())
		{
			// Try appending the parent type
			simpleTypeName = parentType + "::" + simpleTypeName;

			iterFind = commentSimpleLookup.find(simpleTypeName);
			if (iterFind == commentSimpleLookup.end())
			{
				outs() << "Warning: Cannot find identifier referenced by the @copydoc command: \"" << str << "\".\n";
				return false;
			}
			else
				lookup = iterFind->second;
		}
		else
			lookup = iterFind->second;
	}

	// Confirm namespace matches
	SmallVector<std::string, 4> copydocNS;
	for (int i = 0; i < (int)(typeSplits.size() - namespaceStart); i++)
		copydocNS.push_back(typeSplits[i].str());

	SmallVector<std::string, 4> fullNS;
	for (int i = 0; i < (int)curNS.size(); i++)
		fullNS.push_back(curNS[i]);

	for (int i = 0; i < (int)copydocNS.size(); i++)
		fullNS.push_back(copydocNS[i]);

	// First try to assume @copydoc specified namespace is relative to current NS
	int entryMatch = -1;
	for (int i = 0; i < (int)lookup.size(); i++)
	{
		CommentInfo& curCommentInfo = commentInfos[lookup[i]];

		if (fullNS.size() != curCommentInfo.namespaces.size())
			continue;

		bool matches = true;
		for (int j = 0; j < (int)curCommentInfo.namespaces.size(); j++)
		{
			if (fullNS[j] != curCommentInfo.namespaces[j])
			{
				matches = false;
				break;
			}
		}

		if (matches)
		{
			entryMatch = i;
			break;
		}
	}

	// If nothing is found, assume provided namespace is global
	if (entryMatch == -1)
	{
		for (int i = 0; i < (int)lookup.size(); i++)
		{
			CommentInfo& curCommentInfo = commentInfos[lookup[i]];

			if (copydocNS.size() != curCommentInfo.namespaces.size())
				continue;

			bool matches = true;
			for (int j = 0; j < (int)curCommentInfo.namespaces.size(); j++)
			{
				if (copydocNS[j] != curCommentInfo.namespaces[j])
				{
					matches = false;
					break;
				}
			}

			if (matches)
			{
				entryMatch = i;
				break;
			}
		}
	}

	if (entryMatch == -1)
	{
		outs() << "Warning: Cannot find identifier referenced by the @copydoc command: \"" << str << "\".\n";
		return false;
	}

	CommentInfo& finalCommentInfo = commentInfos[lookup[entryMatch]];
	if (hasParamList)
	{
		if (!finalCommentInfo.isFunction)
		{
			outs() << "Warning: Cannot find identifier referenced by the @copydoc command: \"" << str << "\".\n";
			return false;
		}

		SmallVector<StringRef, 8> paramSplits;
		params.split(paramSplits, ",", -1, false);

		for (int i = 0; i < (int)paramSplits.size(); i++)
			paramSplits[i] = paramSplits[i].trim();

		int overloadMatch = -1;
		for (int i = 0; i < (int)finalCommentInfo.overloads.size(); i++)
		{
			if (paramSplits.size() != finalCommentInfo.overloads[i].params.size())
				continue;

			bool matches = true;
			for (int j = 0; j < (int)paramSplits.size(); j++)
			{
				if (paramSplits[j] != finalCommentInfo.overloads[i].params[j])
				{
					matches = false;
					break;
				}
			}

			if (matches)
			{
				overloadMatch = i;
				break;
			}
		}

		if (overloadMatch == -1)
		{
			// Assume the user doesn't care which overload is used
			if (paramSplits.empty())
				overloadMatch = 0;
			else
			{
				outs() << "Warning: Cannot find identifier referenced by the @copydoc command: \"" << str << "\".\n";
				return false;
			}
		}

		outputComment = finalCommentInfo.overloads[overloadMatch].comment;
		return true;
	}

	if (finalCommentInfo.isFunction)
		outputComment = finalCommentInfo.overloads[0].comment;
	else
		outputComment = finalCommentInfo.comment;

	return true;
}

void resolveCopydocComment(CommentEntry& comment, const std::string& parentType, const SmallVector<std::string, 4>& curNS)
{
	StringRef copydocArg;
	for(auto& entry : comment.brief)
	{
		StringRef commentRef(entry.text.data(), entry.text.length());

		if (commentRef.startswith("@copydoc"))
		{
			copydocArg = commentRef.split(' ').second;
			break;
		}
	}

	if (copydocArg.empty())
		return;

	CommentEntry outComment;
	if (!parseCopydocString(copydocArg, parentType, curNS, outComment))
	{
		comment = CommentEntry();
		return;
	}
	else
		comment = outComment;

	resolveCopydocComment(comment, parentType, curNS);
}

std::string generateXMLCommentText(const CommentText& commentTextEntry)
{
	uint32_t idx = 0;
	std::stringstream output;

	for(auto& entry : commentTextEntry.text)
	{
		for (auto& refEntry : commentTextEntry.paramRefs)
		{
			if(refEntry.index == idx)
			{
				output << "<paramref name=\"" << escapeXML(refEntry.name) << "\"/>";
				idx += refEntry.name.size();
			}
		}

		for (auto& refEntry : commentTextEntry.genericRefs)
		{
			if (refEntry.index == idx)
			{
				output << "<see cref=\"" << escapeXML(refEntry.name) << "\"/>";
				idx += refEntry.name.size();
			}
		}

		switch (entry)
		{
		case '&':  output << "&amp;";         break;
		case '\"': output << "&quot;";        break;
		case '\'': output << "&apos;";        break;
		case '<':  output << "&lt;";          break;
		case '>':  output << "&gt;";          break;
		default:   output << entry;           break;
		}

		idx++;
	}

	return output.str();
}
 std::string generateXMLCommentText(const SmallVector<CommentText, 2>& input)
 {
	 std::stringstream output;
	 for (auto I = input.begin(); I != input.end(); ++I)
	 {
		 if (I != input.begin())
			 output << "\n";

		 std::string text = generateXMLCommentText(*I);
		 output << text;
	 }

	return output.str();
 }

std::string generateXMLComments(const CommentEntry& commentEntry, const std::string& indent)
{
	std::stringstream output;
	
	auto wordWrap = [](const std::string& input, const std::string& linePrefix, int columnLength = 124)
	{
		int prefixLength = (int)linePrefix.length();
		int inputLength = (int)input.length();

		if ((inputLength + prefixLength) <= columnLength)
			return linePrefix + input + "\n";

		StringRef inputRef(input.data(), input.length());
		std::stringstream wordWrapped;

		int lineLength = columnLength - prefixLength;
		int curIdx = 0;
		while(curIdx < inputLength)
		{
			int remainingLength = inputLength - curIdx;
			if(remainingLength <= lineLength)
			{
				StringRef lineRef = inputRef.substr(curIdx, remainingLength);
				wordWrapped << linePrefix << lineRef.str() << std::endl;
				break;
			}
			else
			{
				int lastSpace = inputRef.find_last_of(' ', curIdx + lineLength);
				if(lastSpace == -1 || lastSpace <= curIdx) // Need to break word
				{
					StringRef lineRef = inputRef.substr(curIdx, lineLength);

					wordWrapped << linePrefix << lineRef.str() << std::endl;
					curIdx += lineLength;
				}
				else
				{
					int length = lastSpace - curIdx + 1;
					StringRef lineRef = inputRef.substr(curIdx, length);

					wordWrapped << linePrefix << lineRef.str() << std::endl;
					curIdx += length;
				}
			}
		}
		
		return wordWrapped.str();
	};

	auto printParagraphs = [&output, &indent, &wordWrap](const std::string& head, const std::string& tail, const SmallVector<CommentText, 2>& input)
	{
		bool multiline = false;
		if(input.size() > 1)
			multiline = true;
		else
		{
			int refLength = 0;
			for (auto& entry : input[0].paramRefs)
				refLength += sizeof("<paramref name=\"\"/>") + entry.name.size();

			for (auto& entry : input[0].genericRefs)
				refLength += sizeof("<see cref=\"\"/>") + entry.name.size();

			int lineLength = head.length() + tail.length() + indent.size() + 4 + input[0].text.size() + refLength;
			if (lineLength >= 124)
				multiline = true;
		}

		if (multiline)
		{
			output << indent << "/// " << head << "\n";
			for (auto I = input.begin(); I != input.end(); ++I)
			{
				if (I != input.begin())
					output << indent << "///\n";

				std::string text = generateXMLCommentText(*I);
				output << wordWrap(text, indent + "/// ");
			}
			output << indent << "/// " << tail << "\n";
		}
		else
		{
			std::string text = generateXMLCommentText(input[0]);
			output << indent << "/// " << head << text << tail << "\n";
		}
	};

	if (!commentEntry.brief.empty())
		printParagraphs("<summary>", "</summary>", commentEntry.brief);
	else
	{
		if(!commentEntry.params.empty() || !commentEntry.returns.empty())
			output << indent << "/// <summary></summary>" << std::endl;
	}

	for(auto& entry : commentEntry.params)
	{
		if (entry.comments.empty())
			continue;

		printParagraphs("<param name=\"" + entry.name + "\">", "</param>", entry.comments);
	}

	if(!commentEntry.returns.empty())
		printParagraphs("<returns>", "</returns>", commentEntry.returns);

	return output.str();
}

void handleDefaultParams(MethodInfo& methodInfo, std::vector<MethodInfo>& newMethodInfos)
{
	int firstDefaultParam = -1;
	int lastInvalidParam = -1;
	for (int i = 0; i < methodInfo.paramInfos.size(); i++)
	{
		const VarInfo& param = methodInfo.paramInfos[i];

		if (!param.defaultValue.empty())
		{
			firstDefaultParam = i;
			break;
		}
	}

	for (int i = 0; i < methodInfo.paramInfos.size(); i++)
	{
		const VarInfo& param = methodInfo.paramInfos[i];

		if (!param.defaultValueType.empty() && !isFlagsEnum(param.flags))
			lastInvalidParam = i;
	}

	// Nothing to handle
	if (lastInvalidParam == -1)
		return;

	// Mark any non-complex default params as complex, so the generator doesn't generate them (since default arguments
	// must follow them, which they can't because at least one is complex)
	for(int i = firstDefaultParam; i <= lastInvalidParam; i++)
	{
		VarInfo& param = methodInfo.paramInfos[i];

		if (param.defaultValueType.empty())
			param.defaultValueType = "null";
	}

	// Generate a method for each default param
	for(int i = lastInvalidParam; i >= firstDefaultParam; i--)
	{
		MethodInfo copyMethodInfo = methodInfo;

		// Clear all param default values
		for(int j = firstDefaultParam; j < i; j++)
		{
			VarInfo& param = copyMethodInfo.paramInfos[j];
			param.defaultValue = "";
			param.defaultValueType = "";
		}

		// Erase docs for the params we'll skip during generation
		CommentEntry& docs = copyMethodInfo.documentation;
		for(int j = i; j <= lastInvalidParam; j++)
		{
			const std::string& paramName = copyMethodInfo.paramInfos[j].name;

			for(auto iter = docs.params.begin(); iter != docs.params.end();)
			{
				if (iter->name == paramName)
					iter = docs.params.erase(iter);
				else
					++iter;
			}
		}

		copyMethodInfo.flags |= (int)MethodFlags::CSOnly;
		newMethodInfos.push_back(copyMethodInfo);
	}

	// Clear default params from this method
	for(int i = firstDefaultParam; i <= lastInvalidParam; i++)
	{
		VarInfo& param = methodInfo.paramInfos[i];
		param.defaultValue = "";
		param.defaultValueType = "";
	}
}

StructInfo* findStructInfo(const std::string& name)
{
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& structInfo : fileInfo.second.structInfos)
		{
			if (structInfo.name == name)
				return &structInfo;
		}
	}

	return nullptr;
};

void postProcessFileInfos()
{
	// Inject external methods into their appropriate class infos
	auto findClassInfo = [](const std::string& name, bool isEditor) -> ClassInfo*
	{
		for (auto& fileInfo : outputFileInfos)
		{
			for (auto& classInfo : fileInfo.second.classInfos)
			{
				if (classInfo.name != name)
					continue;

				// Two versions of editor and BSF class migth exist, make sure to pick the right one
				if((isEditor && classInfo.api == ApiFlags::BSF) || (!isEditor &&  hasAPIBED(classInfo.api)))
					continue;

				return &classInfo;
			}
		}

		return nullptr;
	};

	auto findEnumInfo = [](const std::string& name) -> EnumInfo*
	{
		for (auto& fileInfo : outputFileInfos)
		{
			for (auto& enumInfo : fileInfo.second.enumInfos)
			{
				if (enumInfo.name == name)
					return &enumInfo;
			}
		}

		return nullptr;
	};

	for (auto& entry : externalClassInfos)
	{
		for (auto& fileInfo : outputFileInfos)
		{
			for (auto& classInfo : fileInfo.second.classInfos)
			{
				if (classInfo.name != entry.first)
					continue;

				for (auto& method : entry.second.methods)
				{
					if (((int)method.flags & (int)MethodFlags::Constructor) != 0)
					{
						if (method.returnInfo.typeName.size() == 0)
						{
							outs() << "Error: Found an external constructor \"" << method.sourceName << "\" with no return value, skipping.\n";
							continue;
						}

						if (method.returnInfo.typeName != entry.first)
						{
							outs() << "Error: Found an external constructor \"" << method.sourceName << "\" whose return value doesn't match the external class, skipping.\n";
							continue;
						}
					}
					else
					{
						if (method.paramInfos.size() == 0)
						{
							outs() << "Error: Found an external method \"" << method.sourceName << "\" with no parameters. This isn't supported, skipping.\n";
							continue;
						}

						if (method.paramInfos[0].typeName != entry.first)
						{
							outs() << "Error: Found an external method \"" << method.sourceName << "\" whose first parameter doesn't "
								" accept the class its operating on. This is not supported, skipping. \n";
							continue;
						}

						method.paramInfos.erase(method.paramInfos.begin());
					}

					classInfo.methodInfos.push_back(method);
				}
			}
		}
	}

	// Resolve copydoc comment commands
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			resolveCopydocComment(classInfo.documentation, classInfo.name, classInfo.ns);

			for (auto& methodInfo : classInfo.methodInfos)
				resolveCopydocComment(methodInfo.documentation, classInfo.name, classInfo.ns);

			for (auto& ctorInfo : classInfo.ctorInfos)
				resolveCopydocComment(ctorInfo.documentation, classInfo.name, classInfo.ns);

			for (auto& eventInfo : classInfo.eventInfos)
				resolveCopydocComment(eventInfo.documentation, classInfo.name, classInfo.ns);
		}

		for (auto& structInfo : fileInfo.second.structInfos)
			resolveCopydocComment(structInfo.documentation, structInfo.name, structInfo.ns);

		for(auto& enumInfo : fileInfo.second.enumInfos)
		{
			resolveCopydocComment(enumInfo.documentation, enumInfo.name, enumInfo.ns);

			for (auto& enumEntryInfo : enumInfo.entries)
				resolveCopydocComment(enumEntryInfo.second.documentation, enumInfo.name, enumInfo.ns);
		}
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

			for (auto& eventInfo : classInfo.eventInfos)
				generateInteropName(eventInfo);
		}
	}

	// Generate property infos
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			for (auto& methodInfo : classInfo.methodInfos)
			{
				bool isGetter = (methodInfo.flags & (int)MethodFlags::PropertyGetter) != 0;
				bool isSetter = (methodInfo.flags & (int)MethodFlags::PropertySetter) != 0;

				if (!isGetter && !isSetter)
					continue;

				PropertyInfo propertyInfo;
				propertyInfo.name = methodInfo.scriptName;
				propertyInfo.documentation = methodInfo.documentation;
				propertyInfo.isStatic = (methodInfo.flags & (int)MethodFlags::Static);
				propertyInfo.visibility = methodInfo.visibility;
				propertyInfo.api = methodInfo.api;
				propertyInfo.style = methodInfo.style;

				if (isGetter)
				{
					propertyInfo.getter = methodInfo.interopName;
					propertyInfo.type = methodInfo.returnInfo.typeName;
					propertyInfo.typeFlags = methodInfo.returnInfo.flags;
				}
				else // Setter
				{
					propertyInfo.setter = methodInfo.interopName;
					propertyInfo.type = methodInfo.paramInfos[0].typeName;
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
						outs() << "Error: Getter and setter types for the property \"" << propertyInfo.name << "\" don't match. Skipping property.\n";
						continue;
					}

					if (!propertyInfo.getter.empty())
					{
						existingInfo.getter = propertyInfo.getter;

						// Prefer documentation from setter, but use getter if no other available
						if (existingInfo.documentation.brief.empty())
							existingInfo.documentation = propertyInfo.documentation;
					}
					else
					{
						existingInfo.setter = propertyInfo.setter;

						if (!propertyInfo.documentation.brief.empty())
							existingInfo.documentation = propertyInfo.documentation;
					}
				}
			}
		}
	}

	// Generate meta-data about base classes
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			if (classInfo.baseClass.empty())
				continue;

			bool isEditor = hasAPIBED(classInfo.api);
			ClassInfo* baseClassInfo = findClassInfo(classInfo.baseClass, isEditor);
			if (baseClassInfo == nullptr)
			{
				assert(false);
				continue;
			}

			baseClassInfo->flags |= (int)ClassFlags::IsBase;
			baseClassLookup[baseClassInfo->name].childClasses.push_back(classInfo.name);
		}
	}

	// Properly generate enum default values
	auto parseDefaultValue = [&](VarInfo& paramInfo)
	{
		if (paramInfo.defaultValue.empty())
			return;

		UserTypeInfo typeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);

		if (typeInfo.type != ParsedType::Enum)
			return;

		int enumIdx = atoi(paramInfo.defaultValue.c_str());
		EnumInfo* enumInfo = findEnumInfo(paramInfo.typeName);
		if(enumInfo == nullptr)
		{
			outs() << "Error: Cannot map default value of \"" + paramInfo.name + 
				"\" to enum entry for enum type \"" + paramInfo.typeName + "\". Ignoring.";
			paramInfo.defaultValue = "";
			return;
		}

		auto iterFind = enumInfo->entries.find(enumIdx);
		if(iterFind == enumInfo->entries.end())
		{
			outs() << "Error: Cannot map default value of \"" + paramInfo.name + 
				"\" to enum entry for enum type \"" + paramInfo.typeName + "\". Ignoring.";
			paramInfo.defaultValue = "";
			return;
		}

		paramInfo.defaultValue = enumInfo->scriptName + "." + iterFind->second.scriptName;
	};

	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			for(auto& methodInfo : classInfo.methodInfos)
			{
				for (auto& paramInfo : methodInfo.paramInfos)
					parseDefaultValue(paramInfo);
			}

			for (auto& ctorInfo : classInfo.ctorInfos)
			{
				for (auto& paramInfo : ctorInfo.paramInfos)
					parseDefaultValue(paramInfo);
			}
		}

		for(auto& structInfo : fileInfo.second.structInfos)
		{
			for(auto& fieldInfo : structInfo.fields)
				parseDefaultValue(fieldInfo);

			for (auto& ctorInfo : structInfo.ctors)
			{
				for (auto& paramInfo : ctorInfo.params)
					parseDefaultValue(paramInfo);
			}
		}
	}

	// Find structs requiring special conversion
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& structInfo : fileInfo.second.structInfos)
		{
			for(auto& fieldInfo : structInfo.fields)
			{
				UserTypeInfo typeInfo = getTypeInfo(fieldInfo.typeName, fieldInfo.flags);

				if(isArrayOrVector(fieldInfo.flags) || !(typeInfo.type == ParsedType::Builtin || typeInfo.type == ParsedType::Enum))
				{
					structInfo.requiresInterop = true;
					break;
				}
			}

			if (structInfo.requiresInterop)
				structInfo.interopName = getStructInteropType(structInfo.name);
			else
				structInfo.interopName = structInfo.name;
		}
	}

	// Mark parameters referencing complex structs and base types
	for (auto& fileInfo : outputFileInfos)
	{
		auto markComplexType = [](const std::string& type, int& flags)
		{
			UserTypeInfo typeInfo = getTypeInfo(type, flags);
			if (typeInfo.type != ParsedType::Struct)
				return;

			StructInfo* structInfo = findStructInfo(type);
			if (structInfo != nullptr && structInfo->requiresInterop)
				flags |= (int)TypeFlags::ComplexStruct;
		};

		auto markBaseType = [&findClassInfo](const std::string& type, int& flags)
		{
			UserTypeInfo typeInfo = getTypeInfo(type, flags);
			if (typeInfo.type != ParsedType::Class && typeInfo.type != ParsedType::ReflectableClass && 
				typeInfo.type != ParsedType::GUIElement && !isHandleType(typeInfo.type))
				return;

			ClassInfo* classInfo = findClassInfo(type, false);
			if (classInfo != nullptr)
			{
				bool isBase = (classInfo->flags & (int)ClassFlags::IsBase) != 0;
				if (isBase)
					flags |= (int)TypeFlags::ReferencesBase;
			}
		};

		auto markParam = [&markComplexType,&markBaseType](VarInfo& paramInfo)
		{
			markComplexType(paramInfo.typeName, paramInfo.flags);
			markBaseType(paramInfo.typeName, paramInfo.flags);
		};

		for (auto& classInfo : fileInfo.second.classInfos)
		{
			for(auto& methodInfo : classInfo.methodInfos)
			{
				for (auto& paramInfo : methodInfo.paramInfos)
					markParam(paramInfo);

				if (methodInfo.returnInfo.typeName.size() != 0)
				{
					markComplexType(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
					markBaseType(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
				}
			}

			for (auto& eventInfo : classInfo.eventInfos)
			{
				for (auto& paramInfo : eventInfo.paramInfos)
					markParam(paramInfo);
			}

			for (auto& ctorInfo : classInfo.ctorInfos)
			{
				for (auto& paramInfo : ctorInfo.paramInfos)
					markParam(paramInfo);
			}
		}

		for(auto& structInfo : fileInfo.second.structInfos)
		{
			for(auto& fieldInfo : structInfo.fields)
			{
				markComplexType(fieldInfo.typeName, fieldInfo.flags);
				markParam(fieldInfo);
			}
		}
	}

	// Generate referenced includes
	{
		for (auto& fileInfo : outputFileInfos)
		{
			IncludesInfo includesInfo;
			for (auto& classInfo : fileInfo.second.classInfos)
				gatherIncludes(classInfo, includesInfo);

			for (auto& structInfo : fileInfo.second.structInfos)
				gatherIncludes(structInfo, includesInfo);

			// Needed for all .h files
			if (!fileInfo.second.inEditor)
				fileInfo.second.referencedHeaderIncludes.push_back("BsScriptEnginePrerequisites.h");
			else
				fileInfo.second.referencedHeaderIncludes.push_back("BsScriptEditorPrerequisites.h");

			// Needed for all .cpp files
			fileInfo.second.referencedSourceIncludes.push_back("BsScript" + fileInfo.first + ".generated.h");
			fileInfo.second.referencedSourceIncludes.push_back("BsMonoMethod.h");
			fileInfo.second.referencedSourceIncludes.push_back("BsMonoClass.h");
			fileInfo.second.referencedSourceIncludes.push_back("BsMonoUtil.h");

			for (auto& classInfo : fileInfo.second.classInfos)
			{
				UserTypeInfo& typeInfo = cppToCsTypeMap[classInfo.name];

				fileInfo.second.forwardDeclarations.insert({ classInfo.ns, classInfo.cleanName, isStruct(classInfo.flags), classInfo.templParams });

				if (typeInfo.type == ParsedType::Resource)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/BsScriptResource.h");
				else if (typeInfo.type == ParsedType::Component)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/BsScriptComponent.h");
				else if (typeInfo.type == ParsedType::SceneObject)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/BsScriptSceneObject.h");
				else if (typeInfo.type == ParsedType::GUIElement)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/GUI/BsScriptGUIElement.h");
				else if (typeInfo.type == ParsedType::ReflectableClass)
					fileInfo.second.referencedHeaderIncludes.push_back("Wrappers/BsScriptReflectable.h");
				else // Class
					fileInfo.second.referencedHeaderIncludes.push_back("BsScriptObject.h");

				if (!classInfo.baseClass.empty())
				{
					UserTypeInfo& baseTypeInfo = cppToCsTypeMap[classInfo.baseClass];

					if(hasAPIBED(classInfo.api))
						fileInfo.second.referencedHeaderIncludes.push_back(baseTypeInfo.destFileEditor);
					else
						fileInfo.second.referencedHeaderIncludes.push_back(baseTypeInfo.destFile);
				}

				if (typeInfo.type != ParsedType::ReflectableClass && classInfo.templParams.empty())
					fileInfo.second.referencedSourceIncludes.push_back(typeInfo.declFile);
				else
				{
					// Templated classes need to be included in header, so the linker doesn't instantiate them multiple times for different libraries
					// (in case template is exported).
					// Reflectable classes need to be included in the header because they provide a getInternal<T>() method
					// which requires information about T.
					fileInfo.second.referencedHeaderIncludes.push_back(typeInfo.declFile);
				}
			}

			for(auto& structInfo : fileInfo.second.structInfos)
			{
				UserTypeInfo& typeInfo = cppToCsTypeMap[structInfo.name];

				fileInfo.second.referencedHeaderIncludes.push_back("BsScriptObject.h");
				fileInfo.second.referencedHeaderIncludes.push_back(typeInfo.declFile);
			}

			if(includesInfo.requiresResourceManager)
				fileInfo.second.referencedSourceIncludes.push_back("BsScriptResourceManager.h");

			if (includesInfo.requiresRRef)
				fileInfo.second.referencedSourceIncludes.push_back("Wrappers/BsScriptRRefBase.h");

			if (includesInfo.requiresAsyncOp)
				fileInfo.second.referencedSourceIncludes.push_back("Wrappers/BsScriptAsyncOp.h");

			if(includesInfo.requiresGameObjectManager)
				fileInfo.second.referencedSourceIncludes.push_back("BsScriptGameObjectManager.h");

			if(includesInfo.requiresRTTI)
				fileInfo.second.referencedSourceIncludes.push_back("Reflection/BsRTTIType.h");

			for (auto& entry : includesInfo.includes)
			{
				uint32_t originFlags = entry.second.originIncludeFlags;
				uint32_t interopFlags = entry.second.interopIncludeFlags;

				if (originFlags != 0)
				{
					std::string include = entry.second.typeInfo.declFile;

					if ((originFlags & IT_FWD) != 0)
						fileInfo.second.forwardDeclarations.insert({ entry.second.typeInfo.ns, entry.second.typeName, entry.second.isStruct });

					if((originFlags & IT_IMPL) != 0)
						fileInfo.second.referencedSourceIncludes.push_back(include);
					else
						fileInfo.second.referencedHeaderIncludes.push_back(include);
				}

				if (interopFlags != 0)
				{
					std::string include;
					if(entry.second.isEditor)
						include = entry.second.typeInfo.destFileEditor;
					else
						include = entry.second.typeInfo.destFile;

					if ((interopFlags & IT_FWD) != 0)
					{
						if(entry.second.isEditor)
							fileInfo.second.forwardDeclarations.insert({ entry.second.typeInfo.ns, entry.second.typeName, false });
					}

					if(!include.empty())
					{
						if ((interopFlags & IT_IMPL) != 0)
							fileInfo.second.referencedSourceIncludes.push_back(include);
						else
							fileInfo.second.referencedHeaderIncludes.push_back(include);
					}
				}
			}

			for (auto& entry : includesInfo.fwdDecls)
				fileInfo.second.forwardDeclarations.insert(entry.second);
		}
	}

	// Generate overloads for unsupported default parameters
	for (auto& fileInfo : outputFileInfos)
	{
		for (auto& classInfo : fileInfo.second.classInfos)
		{
			std::vector<MethodInfo> newMethodInfos;
			for (auto& methodInfo : classInfo.methodInfos)
				handleDefaultParams(methodInfo, newMethodInfos);

			for (auto& methodInfo : newMethodInfos)
				classInfo.methodInfos.push_back(methodInfo);

			std::vector<MethodInfo> newCtorInfos;
			for (auto& ctorInfo : classInfo.ctorInfos)
				handleDefaultParams(ctorInfo, newCtorInfos);

			for (auto& ctorInfo : newCtorInfos)
				classInfo.ctorInfos.push_back(ctorInfo);
		}
	}
}

std::string generateFileHeader(bool isBanshee)
{
	std::stringstream output;
	if (isBanshee)
		output << sEditorCopyrightNotice;
	else
		output << sFrameworkCopyrightNotice;

	return output.str();
}

std::string generateCppApiCheckBegin(ApiFlags api)
{
	if(api == ApiFlags::BSF)
		return "#if !BS_IS_BANSHEE3D\n";
	else if(api == ApiFlags::B3D)
		return "#if BS_IS_BANSHEE3D\n";

	return "";
}

std::string generateCsApiCheckBegin(ApiFlags api)
{
	if(api == ApiFlags::BSF)
		return "#if !IS_B3D\n";
	else if(api == ApiFlags::B3D)
		return "#if IS_B3D\n";

	return "";
}

std::string generateApiCheckEnd(ApiFlags api)
{
	if(api == ApiFlags::BSF || api == ApiFlags::B3D)
		return "#endif\n";

	return "";
}

std::string generateCppMethodSignature(const MethodInfo& methodInfo, const std::string& thisPtrType, const std::string& nestedName, bool isModule)
{
	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
	bool isCtor = (methodInfo.flags & (int)MethodFlags::Constructor) != 0;

	std::stringstream output;

	bool returnAsParameter = false;
	if (methodInfo.returnInfo.typeName.empty() || isCtor)
		output << "void";
	else
	{
		UserTypeInfo returnTypeInfo = getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
		if (!canBeReturned(returnTypeInfo.type, methodInfo.returnInfo.flags))
		{
			output << "void";
			returnAsParameter = true;
		}
		else
		{
			output << getInteropCppVarType(methodInfo.returnInfo.typeName, returnTypeInfo.type, methodInfo.returnInfo.flags);
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
	else if (!isStatic && !isModule)
	{
		output << thisPtrType << "* thisPtr";

		if (methodInfo.paramInfos.size() > 0 || returnAsParameter)
			output << ", ";
	}

	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		UserTypeInfo paramTypeInfo = getTypeInfo(I->typeName, I->flags);

		output << getInteropCppVarType(I->typeName, paramTypeInfo.type, I->flags) << " " << I->name;

		if ((I + 1) != methodInfo.paramInfos.end() || returnAsParameter)
			output << ", ";
	}

	if (returnAsParameter)
	{
		UserTypeInfo returnTypeInfo = getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);

		output << getInteropCppVarType(methodInfo.returnInfo.typeName, returnTypeInfo.type, methodInfo.returnInfo.flags) <<
			" " << "__output";
	}

	output << ")";
	return output.str();
}

std::string generateCppEventCallbackSignature(const MethodInfo& eventInfo, const std::string& nestedName, bool isModule)
{
	bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;

	std::stringstream output;

	if ((isStatic || isModule) && nestedName.empty())
		output << "static ";

	output << "void ";
	
	if (!nestedName.empty())
		output << nestedName << "::";
	
	output << eventInfo.interopName << "(";

	int idx = 0;
	for (auto I = eventInfo.paramInfos.begin(); I != eventInfo.paramInfos.end(); ++I)
	{
		UserTypeInfo paramTypeInfo = getTypeInfo(I->typeName, I->flags);

		if (!isSrcValue(I->flags) && !isOutput(I->flags))
			output << "const ";

		if (isVector(I->flags))
			output << "std::vector<";
		else if(isSmallVector(I->flags))
			output << "SmallVector<";

		output << getCppVarType(I->typeName, paramTypeInfo.type, I->flags, false);

		if(!isSrcValue(I->flags))
		{
			if (isSrcPointer(I->flags))
				output << "*";
			else if (isSrcReference(I->flags))
				output << "&";
		}

		if(isSmallVector(I->flags))
			output << ", " << I->arraySize << ">";

		if (isVector(I->flags))
			output << ">";

		output << " p" << idx;

		if (isArray(I->flags))
			output << "[" << I->arraySize << "]";

		if ((I + 1) != eventInfo.paramInfos.end())
			output << ", ";

		idx++;
	}

	output << ")";
	return output.str();
}

std::string generateCppEventThunk(const MethodInfo& eventInfo, bool isModule)
{
	bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;

	std::stringstream output;
	output << "\t\ttypedef void(BS_THUNKCALL *" << eventInfo.sourceName << "ThunkDef) (";
	
	if (!isStatic && !isModule)
		output << "MonoObject*, ";

	for (auto I = eventInfo.paramInfos.begin(); I != eventInfo.paramInfos.end(); ++I)
	{
		UserTypeInfo paramTypeInfo = getTypeInfo(I->typeName, I->flags);

		if (paramTypeInfo.type == ParsedType::Struct)
			output << "MonoObject* " << I-> name << ", ";
		else
			output << getInteropCppVarType(I->typeName, paramTypeInfo.type, I->flags) << " " << I->name << ", ";
	}

	output << "MonoException**);" << std::endl;
	output << "\t\tstatic " << eventInfo.sourceName << "ThunkDef " << eventInfo.sourceName << "Thunk;" << std::endl;

	return output.str();
}

std::string generateClassNativeToScriptObjectLine(int flags, const std::string& typeName, const std::string& outputName, 
	const std::string& scriptType, const std::string& argName, bool asRef = false, const std::string& indent = "\t\t")
{
	std::stringstream output;

	auto generateCreateLine = [&output, &outputName, asRef](const std::string& scriptType, const std::string& argName, const std::string& indent)
	{
		if (asRef)
			output << indent << "MonoUtil::referenceCopy(" << outputName << ", " << scriptType << "::create(" << argName << "));\n";
		else
			output << indent << outputName << " = " << scriptType << "::create(" << argName << ");\n";
	};

	if(isBaseParam(flags))
	{
		std::vector<std::string> derivedClasses;
		getDerivedClasses(typeName, derivedClasses);

		if(!derivedClasses.empty())
		{
			output << indent << "if(" << argName << ")\n";
			output << indent << "{\n";

			output << indent << "\tif(rtti_is_of_type<" << derivedClasses[0] << ">(" << argName << "))\n";
			generateCreateLine(getScriptInteropType(derivedClasses[0]), 
				"std::static_pointer_cast<" + derivedClasses[0] + ">(" + argName + ")", indent + "\t\t");

			for(uint32_t i = 1; i < (uint32_t)derivedClasses.size(); i++)
			{
				output << indent << "\telse if(rtti_is_of_type<" << derivedClasses[i] << ">(" << argName << "))\n";
				generateCreateLine(getScriptInteropType(derivedClasses[i]),
					"std::static_pointer_cast<" + derivedClasses[i] + ">(" + argName + ")", indent + "\t\t");
			}

			output << indent << "\telse\n";
			generateCreateLine(scriptType, argName, indent + "\t\t");


			output << indent << "}\n";
			output << indent << "else\n";
			generateCreateLine(scriptType, argName, indent + "\t");

			return output.str();
		}
	}
	else
		generateCreateLine(scriptType, argName, indent);

	return output.str();
}

std::string generateNativeToScriptObjectLine(ParsedType type, int flags, const std::string& scriptName,
	const std::string& argName, const std::string& indent = "\t\t")
{
	std::stringstream output;

	if (type == ParsedType::Resource)
	{
		if(getPassAsResourceRef(flags))
		{
			output << indent << "ScriptRRefBase* " << scriptName << ";\n";
			output << indent << scriptName << " = ScriptResourceManager::instance().getScriptRRef(" << argName << ");\n";
		}
		else
		{
			output << indent << "ScriptResourceBase* " << scriptName << ";\n";
			output << indent << scriptName << " = ScriptResourceManager::instance().getScriptResource(" << argName
				<< ", true);\n";
		}
	}
	else if (type == ParsedType::Component)
	{
		output << indent << "ScriptComponentBase* " << scriptName << " = nullptr;\n";
		output << indent << "if(" << argName << ")\n";
		output << indent << "\t" << scriptName << " = ScriptGameObjectManager::instance().getBuiltinScriptComponent(" <<
			"static_object_cast<Component>(" << argName << "));\n";
	}
	else if (type == ParsedType::SceneObject)
	{
		output << indent << "ScriptSceneObject* " << scriptName << " = nullptr;\n";
		output << indent << "if(" << argName << ")\n";
		output << indent << scriptName << " = ScriptGameObjectManager::instance().getOrCreateScriptSceneObject(" <<
			argName << ");\n";
	}
	else
		assert(false);

	return output.str();
}

std::string generateMethodBodyBlockForParam(const std::string& name, const VarTypeInfo& varTypeInfo,
	bool isLast, bool returnValue, std::stringstream& preCallActions, std::stringstream& postCallActions)
{
	UserTypeInfo paramTypeInfo = getTypeInfo(varTypeInfo.typeName, varTypeInfo.flags);

	if(getIsAsyncOp(varTypeInfo.flags))
	{
		if (!isOutput(varTypeInfo.flags) && !returnValue)
		{
			outs() << "Error: AsyncOp type not supported as input parameter. \n";
			return "";
		}

		if (paramTypeInfo.type != ParsedType::ReflectableClass && paramTypeInfo.type != ParsedType::Class && 
			paramTypeInfo.type != ParsedType::Resource)
		{
			outs() << "Error: Type not supported as an AsyncOp return value. \n";
			return "";
		}

		std::string argType;
		std::string argName;
		if (!isArrayOrVector(varTypeInfo.flags))
		{
			argName = "tmp" + name;
			argType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type);

			preCallActions << "\t\tTAsyncOp<" << argType << "> " << argName << ";\n";
		}
		else
		{
			if (isVector(varTypeInfo.flags))
				argType = "Vector<" + getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags, false) + ">";
			else if(isSmallVector(varTypeInfo.flags))
				argType = "SmallVector<" + getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags, false) + ", " + std::to_string(varTypeInfo.arraySize) + ">";
			else
				argType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags, false);

			argName = "vec" + name;

			preCallActions << "\t\t" << argType << " " << argName;
			if (isArray(varTypeInfo.flags))
				preCallActions << "[" << varTypeInfo.arraySize << "]";
			preCallActions << ";\n";
		}

		std::string monoType;
		if(varTypeInfo.typeName != "Any")
		{
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName,
				paramTypeInfo.type == ParsedType::Resource && getPassAsResourceRef(varTypeInfo.flags));

			monoType = scriptType + "::getMetaData()->scriptClass";

			postCallActions << "\t\tauto convertCallback = [](const Any& returnVal)\n";
			postCallActions << "\t\t{\n";
			postCallActions << "\t\t\t" << argType << " nativeObj = any_cast<" << argType << ">(returnVal);\n";
			postCallActions << "\t\t\tMonoObject* monoObj;\n";

			if (!isArrayOrVector(varTypeInfo.flags))
			{
				if (paramTypeInfo.type == ParsedType::ReflectableClass || paramTypeInfo.type == ParsedType::Class)
					postCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, "monoObj", scriptType, "nativeObj", false, "\t\t\t");
				else // Resource
				{
					postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.type, varTypeInfo.flags, "scriptObj", "nativeObj", "\t\t\t");
					postCallActions << "\t\t\tif(scriptObj != nullptr)" << std::endl;
					postCallActions << "\t\t\t\tmonoObj = scriptObj->getManagedInstance();" << std::endl;
					postCallActions << "\t\t\telse" << std::endl;
					postCallActions << "\t\t\t\tmonoObj = nullptr;" << std::endl;
				}

			}
			else
			{
				std::string arrayName = "scriptArray";

				postCallActions << "\t\t\tint arraySize = ";
				if (isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
					postCallActions << "(int)" << argName << ".size()";
				else
					postCallActions << varTypeInfo.arraySize;
				postCallActions << ";\n";

				postCallActions << "\t\t\tScriptArray " << arrayName;
				postCallActions << " = " << "ScriptArray::create<" << scriptType << ">(arraySize);" << std::endl;
				postCallActions << "\t\t\tfor(int i = 0; i < arraySize; i++)" << std::endl;
				postCallActions << "\t\t\t{" << std::endl;

				switch (paramTypeInfo.type)
				{
				case ParsedType::ReflectableClass:
				case ParsedType::Class:
				{
					std::string elemName = "arrayElem" + name;

					std::string elemPtrType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags);
					std::string elemPtrName = "arrayElemPtr" + name;

					postCallActions << "\t\t\t\t" << elemPtrType << " " << elemPtrName;
					if (willBeDereferenced(varTypeInfo.flags))
					{
						postCallActions << " = bs_shared_ptr_new<" << varTypeInfo.typeName << ">();\n";

						if (isSrcPointer(varTypeInfo.flags))
						{
							postCallActions << "\t\t\t\tif(nativeObj[i])\n";
							postCallActions << "\t\t\t\t\t*" << elemPtrName << " = *";
						}
						else
						{
							postCallActions << "\t\t\t\t*" << elemPtrName << " = ";
						}

						postCallActions << "nativeObj[i];\n";
					}
					else
						postCallActions << " = nativeObj[i];\n";

					postCallActions << "\t\t\t\tMonoObject* " << elemName << ";\n";
					postCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, elemName,
						scriptType, elemPtrName, false, "\t\t\t\t");

					postCallActions << "\t\t\t\t" << arrayName << ".set(i, " << elemName << ");" << std::endl;
					break;
				}
				case ParsedType::Resource:
				{
					std::string scriptName = "scriptObj";

					postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.type, varTypeInfo.flags, scriptName, "nativeObj[i]", "\t\t\t\t");
					postCallActions << "\t\t\t\tif(" << scriptName << " != nullptr)" << std::endl;
					postCallActions << "\t\t\t\t\t" << arrayName << ".set(i, " << scriptName << "->getManagedInstance());" << std::endl;
					postCallActions << "\t\t\t\telse" << std::endl;
					postCallActions << "\t\t\t\t\t" << arrayName << ".set(i, nullptr);" << std::endl;
				}
				break;
				default:
					outs() << "Error: Type not supported as an AsyncOp return value. \n";
					break;
				}

				postCallActions << "\t\t\t}" << std::endl;
				postCallActions << "\t\t\tmonoObj = " << arrayName << ".getInternal();" << std::endl;
			}

			postCallActions << "\t\t\treturn monoObj;\n";
			postCallActions << "\t\t};\n";
			postCallActions << "\n;";
		}
		else
			postCallActions << "\t\tauto convertCallback = nullptr;\n";

		if (returnValue)
			postCallActions << "\t\t" << name << " = " << "ScriptAsyncOpBase::create(" << argName << ", convertCallback, " << monoType << ");\n";
		else
			postCallActions << "\t\tMonoUtil::referenceCopy(" << name << ", " << "ScriptAsyncOpBase::create(" << argName << ", convertCallback, " << monoType << "));\n";

		return argName;
	}

	if (!isArrayOrVector(varTypeInfo.flags))
	{
		std::string argName;

		switch (paramTypeInfo.type)
		{
		case ParsedType::Builtin:
		case ParsedType::Enum:
		case ParsedType::Struct:
			if (returnValue)
			{
				argName = "tmp" + name;

				if(isFlagsEnum(varTypeInfo.flags))
					preCallActions << "\t\tFlags<" << varTypeInfo.typeName << "> " << argName << ";" << std::endl;
				else
					preCallActions << "\t\t" << varTypeInfo.typeName << " " << argName << ";" << std::endl;

				if (paramTypeInfo.type == ParsedType::Struct)
				{
					if(isComplexStruct(varTypeInfo.flags))
					{
						std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

						postCallActions << "\t\t" << getStructInteropType(varTypeInfo.typeName) << " interop" << name << ";\n";
						postCallActions << "\t\tinterop" << name << " = " << scriptType << "::toInterop(" << argName << ");\n";

						postCallActions << "\t\tMonoUtil::valueCopy(" << name << ", ";
						postCallActions << "&interop" << name << ", ";
						postCallActions << scriptType << "::getMetaData()->scriptClass->_getInternalClass());\n";
					}
					else
						postCallActions << "\t\t*" << name << " = " << argName << ";" << std::endl;
				}
				else if(isFlagsEnum(varTypeInfo.flags))
					postCallActions << "\t\t" << name << " = (" << varTypeInfo.typeName << ")(uint32_t)" << argName << ";" << std::endl;
				else
					postCallActions << "\t\t" << name << " = " << argName << ";" << std::endl;
			}
			else if (isOutput(varTypeInfo.flags))
			{
				if(paramTypeInfo.type == ParsedType::Struct && isComplexStruct(varTypeInfo.flags))
				{
					argName = "tmp" + name;
					preCallActions << "\t\t" << varTypeInfo.typeName << " " << argName << ";" << std::endl;

					std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

					postCallActions << "\t\t" << getStructInteropType(varTypeInfo.typeName) << " interop" << name << ";\n";
					postCallActions << "\t\tinterop" << name << " = " << scriptType << "::toInterop(" << argName << ");\n";

					postCallActions << "\t\tMonoUtil::valueCopy(" << name << ", ";
					postCallActions << "&interop" << name << ", ";
					postCallActions << scriptType << "::getMetaData()->scriptClass->_getInternalClass());\n";
				}
				else if (isFlagsEnum(varTypeInfo.flags))
				{
					argName = "tmp" + name;
					preCallActions << "\t\tFlags<" << varTypeInfo.typeName << "> " << argName << ";" << std::endl;

					postCallActions << "\t\t*" << name << " = (" << varTypeInfo.typeName << ")(uint32_t)" << argName << ";" << std::endl;
				}
				else
					argName = name;
			}
			else
			{
				if(paramTypeInfo.type == ParsedType::Struct && isComplexStruct(varTypeInfo.flags))
				{
					argName = "tmp" + name;
					preCallActions << "\t\t" << varTypeInfo.typeName << " " << argName << ";" << std::endl;

					std::string scriptType = getScriptInteropType(varTypeInfo.typeName);
					preCallActions << "\t\t" << argName << " = " << scriptType << "::fromInterop(*" << name << ");" << std::endl;
				}
				else
					argName = name;
			}

			break;
		case ParsedType::String:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tString " << argName << ";" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << name << " = MonoUtil::stringToMono(" << argName << ");" << std::endl;
			else if (isOutput(varTypeInfo.flags))
				postCallActions << "\t\tMonoUtil::referenceCopy(" << name << ",  (MonoObject*)MonoUtil::stringToMono(" << argName << "));" << std::endl;
			else
				preCallActions << "\t\t" << argName << " = MonoUtil::monoToString(" << name << ");" << std::endl;
		}
		break;
		case ParsedType::Path:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tPath " << argName << ";" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << name << " = MonoUtil::stringToMono(" << argName << ".toString());" << std::endl;
			else if (isOutput(varTypeInfo.flags))
				postCallActions << "\t\tMonoUtil::referenceCopy(" << name << ",  (MonoObject*)MonoUtil::stringToMono(" << argName << ".toString()));" << std::endl;
			else
				preCallActions << "\t\t" << argName << " = MonoUtil::monoToString(" << name << ");" << std::endl;
		}
		break;
		case ParsedType::WString:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tWString " << argName << ";" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << name << " = MonoUtil::wstringToMono(" << argName << ");" << std::endl;
			else if (isOutput(varTypeInfo.flags))
				postCallActions << "\t\tMonoUtil::referenceCopy(" << name << ", (MonoObject*)MonoUtil::wstringToMono(" << argName << "));" << std::endl;
			else
				preCallActions << "\t\t" << argName << " = MonoUtil::monoToWString(" << name << ");" << std::endl;
		}
		break;
		case ParsedType::MonoObject:
		{
			argName = "tmp" + name;
			
			if (returnValue)
			{
				preCallActions << "\t\tMonoObject* " << argName << ";" << std::endl;
				postCallActions << "\t\t" << name << " = " << argName << ";" << std::endl;
			}
			else if (isOutput(varTypeInfo.flags))
			{
				preCallActions << "\t\tMonoObject* " << argName << ";" << std::endl;
				postCallActions << "\t\tMonoUtil::referenceCopy(" << name << ", " << argName << ");" << std::endl;
			}
			else
			{
				outs() << "Error: MonoObject type not supported as input. Ignoring. \n";
			}
		}
		break;
		case ParsedType::GUIElement:
		{
			argName = "tmp" + name;
			std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type);
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

			preCallActions << "\t\t" << tmpType << " " << argName << ";\n";
			if(returnValue || isOutput(varTypeInfo.flags))
				outs() << "Error: GUIElement cannot be used as parameter outputs or return values. Ignoring. \n";
			else
			{
				std::string scriptName = "script" + name;

				preCallActions << generateManagedToScriptObjectLine("\t\t", scriptType, scriptName, name, 
					paramTypeInfo.type, varTypeInfo.flags);
				preCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preCallActions << "\t\t\t" << argName << " = " << generateGetInternalLine(varTypeInfo.typeName, scriptName, 
					paramTypeInfo.type, varTypeInfo.flags) << ";" << std::endl;
			}
		}
			break;
		case ParsedType::Class:
		case ParsedType::ReflectableClass:
		{
			argName = "tmp" + name;
			std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type);
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

			preCallActions << "\t\t" << tmpType << " " << argName;
			if ((returnValue || isOutput(varTypeInfo.flags)) && willBeDereferenced(varTypeInfo.flags))
				preCallActions << " = bs_shared_ptr_new<" << varTypeInfo.typeName << ">()";

			preCallActions << ";\n";

			if (returnValue)
				postCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, name, scriptType, argName);
			else if (isOutput(varTypeInfo.flags))
				postCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, name, scriptType, argName, true);
			else
			{
				std::string scriptName = "script" + name;
				
				preCallActions << generateManagedToScriptObjectLine("\t\t", scriptType, scriptName, name, 
					paramTypeInfo.type, varTypeInfo.flags);
				preCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preCallActions << "\t\t\t" << argName << " = " << generateGetInternalLine(varTypeInfo.typeName, scriptName, 
					paramTypeInfo.type, varTypeInfo.flags) << ";" << std::endl;
			}
		}
			break;
		default: // Some resource or game object type
		{
			argName = "tmp" + name;
			std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type);

			preCallActions << "\t\t" << tmpType << " " << argName << ";" << std::endl;

			std::string scriptName = "script" + name;
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));

			if (returnValue)
			{
				postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.type, varTypeInfo.flags, scriptName, argName);
				postCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				postCallActions << "\t\t\t" << name << " = " << scriptName << "->getManagedInstance();" << std::endl;
				postCallActions << "\t\telse" << std::endl;
				postCallActions << "\t\t\t" << name << " = nullptr;" << std::endl;
			}
			else if (isOutput(varTypeInfo.flags))
			{
				postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.type, varTypeInfo.flags, scriptName, argName);
				postCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				postCallActions << "\t\t\tMonoUtil::referenceCopy(" << name << ", " << scriptName << "->getManagedInstance());" << std::endl;
				postCallActions << "\t\telse" << std::endl;
				postCallActions << "\t\t\t*" << name << " = nullptr;" << std::endl;
			}
			else
			{
				preCallActions << generateManagedToScriptObjectLine("\t\t", scriptType, scriptName, name, paramTypeInfo.type, varTypeInfo.flags);
				preCallActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preCallActions << "\t\t\t" << argName << " = " << generateGetInternalLine(varTypeInfo.typeName, scriptName, paramTypeInfo.type, varTypeInfo.flags) << ";" << std::endl;
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
		case ParsedType::Path:
		case ParsedType::Enum:
			entryType = varTypeInfo.typeName;
			break;
		case ParsedType::MonoObject:
			entryType = "MonoObject*";
			break;
		default: // Some object or struct type
			entryType = getScriptInteropType(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));
			break;
		}

		std::string argType;
		
		if (isVector(varTypeInfo.flags))
			argType = "Vector<" + getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags, false) + ">";
		else if(isSmallVector(varTypeInfo.flags))
			argType = "SmallVector<" + getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags, false) + ", " + std::to_string(varTypeInfo.arraySize) + ">";
		else
			argType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags, false);

		std::string argName = "vec" + name;

		preCallActions << "\t\t" << argType << " " << argName;
		if (isArray(varTypeInfo.flags))
			preCallActions << "[" << varTypeInfo.arraySize << "]";
		preCallActions << ";\n";

		if (!isOutput(varTypeInfo.flags) && !returnValue)
		{
			std::string arrayName = "array" + name;

			preCallActions << "\t\tif(" << name << " != nullptr)\n";
			preCallActions << "\t\t{\n";

			preCallActions << "\t\t\tScriptArray " << arrayName << "(" << name << ");" << std::endl;

			if(isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
				preCallActions << "\t\t\t" << argName << ".resize(" << arrayName << ".size());" << std::endl;

			preCallActions << "\t\t\tfor(int i = 0; i < (int)" << arrayName << ".size(); i++)" << std::endl;
			preCallActions << "\t\t\t{" << std::endl;

			switch (paramTypeInfo.type)
			{
			case ParsedType::Builtin:
			case ParsedType::String:
			case ParsedType::WString:
			case ParsedType::Path:
				preCallActions << "\t\t\t\t" << argName << "[i] = " << arrayName << ".get<" << entryType << ">(i);" << std::endl;
				break;
			case ParsedType::MonoObject:
				outs() << "Error: MonoObject type not supported as input. Ignoring. \n";
				break;
			case ParsedType::Enum:
			{
				std::string enumType;
				mapBuiltinTypeToCppType(paramTypeInfo.underlyingType, enumType);

				preCallActions << "\t\t\t\t" << argName << "[i] = (" << entryType << ")" << arrayName << ".get<" << enumType << ">(i);" << std::endl;
				break;
			}
			case ParsedType::Struct:

				preCallActions << "\t\t\t\t" << argName << "[i] = ";

				if (isComplexStruct(varTypeInfo.flags))
				{
					preCallActions << entryType << "::fromInterop(";
					preCallActions << arrayName << ".get<" << getStructInteropType(varTypeInfo.typeName) << ">(i)";
					preCallActions << ")";
				}
				else
					preCallActions << arrayName << ".get<" << varTypeInfo.typeName << ">(i)";

				preCallActions << ";\n";

				break;
			default: // Some object type
			{
				std::string scriptName = "script" + name;

				preCallActions << generateManagedToScriptObjectLine("\t\t\t\t", entryType, scriptName, arrayName + ".get<MonoObject*>(i)", paramTypeInfo.type, varTypeInfo.flags);
				preCallActions << "\t\t\t\tif(" << scriptName << " != nullptr)\n";
				preCallActions << "\t\t\t\t{\n";

				std::string elemPtrType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags);
				std::string elemPtrName = "arrayElemPtr" + name;

				preCallActions << "\t\t\t\t\t" << elemPtrType << " " << elemPtrName << " = " << 
					generateGetInternalLine(varTypeInfo.typeName, scriptName, paramTypeInfo.type, varTypeInfo.flags) << ";\n";

				if(paramTypeInfo.type == ParsedType::Class || paramTypeInfo.type == ParsedType::ReflectableClass)
				{
					if(isSrcPointer(varTypeInfo.flags))
						preCallActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ".get();\n";
					else if((isSrcReference(varTypeInfo.flags) || isSrcValue(varTypeInfo.flags)) && !isSrcSPtr(varTypeInfo.flags))
					{
						preCallActions << "\t\t\t\t\tif(" << elemPtrName << ")\n";
						preCallActions << "\t\t\t\t\t\t" << argName << "[i] = *" << elemPtrName << ";\n";
					}
					else
						preCallActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";
				}
				else
					preCallActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";

				preCallActions << "\t\t\t\t}\n";
			}
			break;
			}

			preCallActions << "\t\t\t}" << std::endl;

			if (!isLast)
				preCallActions << std::endl;

			preCallActions << "\t\t}\n";
		}
		else
		{
			std::string arrayName = "array" + name;

			postCallActions << "\t\tint arraySize" << name << " = ";
			if (isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
				postCallActions << "(int)" << argName << ".size()";
			else
				postCallActions << varTypeInfo.arraySize;
			postCallActions << ";\n";

			postCallActions << "\t\tScriptArray " << arrayName;
			postCallActions << " = " << "ScriptArray::create<" << entryType << ">(arraySize" << name << ");" << std::endl;
			postCallActions << "\t\tfor(int i = 0; i < arraySize" << name << "; i++)" << std::endl;
			postCallActions << "\t\t{" << std::endl;

			switch (paramTypeInfo.type)
			{
			case ParsedType::Builtin:
			case ParsedType::String:
			case ParsedType::WString:
			case ParsedType::Path:
				postCallActions << "\t\t\t" << arrayName << ".set(i, " << argName << "[i]);" << std::endl;
				break;
			case ParsedType::Enum:
			{
				std::string enumType;
				mapBuiltinTypeToCppType(paramTypeInfo.underlyingType, enumType);

				if(isFlagsEnum(varTypeInfo.flags))
					postCallActions << "\t\t\t" << arrayName << ".set(i, (" << enumType << ")(uint32_t)" << argName << "[i]);" << std::endl;
				else
					postCallActions << "\t\t\t" << arrayName << ".set(i, (" << enumType << ")" << argName << "[i]);" << std::endl;
				break;
			}
			case ParsedType::Struct:
				postCallActions << "\t\t\t" << arrayName << ".set(i, ";

				if(isComplexStruct(varTypeInfo.flags))
					postCallActions << entryType << "::toInterop(";

				postCallActions << argName << "[i]";

				if (isComplexStruct(varTypeInfo.flags))
					postCallActions << ")";

				postCallActions << ");\n";

				break;
			case ParsedType::MonoObject:
				postCallActions << "\t\t\t" << arrayName << ".set(i, " << argName << "[i]);" << std::endl;
				break;
			case ParsedType::Class:
			case ParsedType::ReflectableClass:
			{
				std::string elemName = "arrayElem" + name;

				std::string elemPtrType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags);
				std::string elemPtrName = "arrayElemPtr" + name;

				postCallActions << "\t\t\t" << elemPtrType << " " << elemPtrName;
				if(willBeDereferenced(varTypeInfo.flags))
				{
					postCallActions << " = bs_shared_ptr_new<" << varTypeInfo.typeName << ">();\n";

					if (isSrcPointer(varTypeInfo.flags))
					{
						postCallActions << "\t\t\tif(" << argName << "[i])\n";
						postCallActions << "\t\t\t\t*" << elemPtrName << " = *";
					}
					else
					{
						postCallActions << "\t\t\t*" << elemPtrName << " = ";
					}

					postCallActions << argName << "[i];\n";
				}
				else
					postCallActions << " = " << argName << "[i];\n";

				postCallActions << "\t\t\tMonoObject* " << elemName << ";\n";
				postCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, elemName, 
					entryType, elemPtrName, false, "\t\t\t");

				postCallActions << "\t\t\t" << arrayName << ".set(i, " << elemName << ");" << std::endl;
				break;
			}
			case ParsedType::GUIElement:
				outs() << "Error: GUIElement cannot be used as parameter outputs or return values. Ignoring. \n";
				break;
			default: // Some resource or game object type
			{
				std::string scriptName = "script" + name;

				postCallActions << generateNativeToScriptObjectLine(paramTypeInfo.type, varTypeInfo.flags, scriptName, argName + "[i]", "\t\t\t");
				postCallActions << "\t\t\tif(" << scriptName << " != nullptr)" << std::endl;
				postCallActions << "\t\t\t\t" << arrayName << ".set(i, " << scriptName << "->getManagedInstance());" << std::endl;
				postCallActions << "\t\t\telse" << std::endl;
				postCallActions << "\t\t\t\t" << arrayName << ".set(i, nullptr);" << std::endl;
			}
			break;
			}

			postCallActions << "\t\t}" << std::endl;

			if (returnValue)
				postCallActions << "\t\t" << name << " = " << arrayName << ".getInternal();" << std::endl;
			else
				postCallActions << "\t\tMonoUtil::referenceCopy(" << name << ", (MonoObject*)" << arrayName << ".getInternal());" << std::endl;
		}

		return argName;
	}
}

std::string generateFieldConvertBlock(const std::string& name, const VarTypeInfo& varTypeInfo, bool toInterop, std::stringstream& preActions)
{
	UserTypeInfo paramTypeInfo = getTypeInfo(varTypeInfo.typeName, varTypeInfo.flags);

	if (getIsAsyncOp(varTypeInfo.flags))
	{
		outs() << "Error: AsyncOp type not supported as a struct field. \n";
		return "";
	}

	if (!isArrayOrVector(varTypeInfo.flags))
	{
		std::string arg;

		switch (paramTypeInfo.type)
		{
		case ParsedType::Builtin:
		case ParsedType::Enum:
			arg = "value." + name;
			break;
		case ParsedType::Struct:
			if(isComplexStruct(varTypeInfo.flags))
			{
				std::string interopType = getStructInteropType(varTypeInfo.typeName);
				std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

				arg = "tmp" + name;
				if(toInterop)
				{
					preActions << "\t\t" << interopType << " " << arg << ";" << std::endl;
					preActions << "\t\t" << arg << " = " << scriptType << "::toInterop(value." << name << ");" << std::endl;
				}
				else
				{
					preActions << "\t\t" << varTypeInfo.typeName << " " << arg << ";" << std::endl;
					preActions << "\t\t" << arg << " = " << scriptType << "::fromInterop(value." << name << ");" << std::endl;
				}
			}
			else
				arg = "value." + name;
			break;
		case ParsedType::String:
		{
			arg = "tmp" + name;

			if(toInterop)
			{
				preActions << "\t\tMonoString* " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::stringToMono(value." << name << ");" << std::endl;
			}
			else
			{
				preActions << "\t\tString " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::monoToString(value." << name << ");" << std::endl;
			}
		}
		break;
		case ParsedType::WString:
		{
			arg = "tmp" + name;

			if(toInterop)
			{
				preActions << "\t\tMonoString* " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::wstringToMono(value." << name << ");" << std::endl;
			}
			else
			{
				preActions << "\t\tWString " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::monoToWString(value." << name << ");" << std::endl;
			}
		}
		break;
		case ParsedType::Path:
		{
			arg = "tmp" + name;

			if(toInterop)
			{
				preActions << "\t\tMonoString* " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::stringToMono(value." << name << ".toString());" << std::endl;
			}
			else
			{
				preActions << "\t\tPath " << arg << ";" << std::endl;
				preActions << "\t\t" << arg << " = MonoUtil::monoToString(value." << name << ");" << std::endl;
			}
		}
		break;
		case ParsedType::MonoObject:
		{
			arg = "tmp" + name;

			preActions << "\t\tMonoObject* " << arg << ";" << std::endl;
			preActions << "\t\t" << arg << " = " << name << ";" << std::endl;
		}
		break;
		case ParsedType::GUIElement:
		{
			arg = "tmp" + name;
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

			if(!toInterop)
			{
				if(isSrcPointer(varTypeInfo.flags))
				{
					std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type);
					preActions << "\t\t" << tmpType << " " << arg << ";" << std::endl;

					std::string scriptName = "script" + name;
					preActions << generateManagedToScriptObjectLine("\t\t", scriptType, scriptName, "value." + name, 
						paramTypeInfo.type, varTypeInfo.flags);
					preActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
					preActions << "\t\t\t" << arg << " = " << generateGetInternalLine(varTypeInfo.typeName, scriptName,
						paramTypeInfo.type, varTypeInfo.flags) << ";" << std::endl;
				}
				else
					outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
			}
		}
			break;
		case ParsedType::Class:
		case ParsedType::ReflectableClass:
		{
			arg = "tmp" + name;
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

			if(toInterop)
			{
				preActions << "\t\tMonoObject* " << arg << ";\n";

				// Need to copy by value
				if(isSrcValue(varTypeInfo.flags) || isSrcPointer(varTypeInfo.flags))
				{
					std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type);
					preActions << "\t\t" << tmpType << " " << arg << "copy;\n";

					// Note: Assuming a copy constructor exists
					if (isSrcPointer(varTypeInfo.flags))
					{
						preActions << "\t\tif(value." << name << " != nullptr)\n";
						preActions << "\t\t\t" << arg << "copy = bs_shared_ptr_new<" << varTypeInfo.typeName << ">(*value." << name << ");\n";
					}
					else
						preActions << "\t\t" << arg << "copy = bs_shared_ptr_new<" << varTypeInfo.typeName << ">(value." << name << ");\n";

					preActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, arg, scriptType, arg + "copy");
				}
				else if(isSrcSPtr(varTypeInfo.flags))
					preActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, arg, scriptType, "value." + name);
				else
					outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
			}
			else
			{
				std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type);
				preActions << "\t\t" << tmpType << " " << arg << ";" << std::endl;

				std::string scriptName = "script" + name;
				preActions << generateManagedToScriptObjectLine("\t\t", scriptType, scriptName, "value." + name, paramTypeInfo.type, varTypeInfo.flags);
				preActions << "\t\tif(" << scriptName << " != nullptr)" << std::endl;
				preActions << "\t\t\t" << arg << " = " << scriptName << "->getInternal();" << std::endl;

				// Cast to the source type from SPtr
				if (isSrcValue(varTypeInfo.flags))
				{
					preActions << "\t\tif(" << arg << " != nullptr)" << std::endl;
					arg = "*" + arg;
				}
				else if (isSrcPointer(varTypeInfo.flags))
					arg = arg + ".get()";
				else if(!isSrcSPtr(varTypeInfo.flags))
					outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
			}
		}
			break;
		default: // Some resource or game object type
		{
			arg = "tmp" + name;
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));
			std::string scriptName = "script" + name;

			if(toInterop)
			{
				std::string argName;
				
				if(!getIsComponentOrActor(varTypeInfo.flags))
					argName = "value." + name;
				else
					argName = "value." + name + ".getComponent()";

				preActions << generateNativeToScriptObjectLine(paramTypeInfo.type, varTypeInfo.flags, scriptName, argName);

				preActions << "\t\tMonoObject* " << arg << ";\n";
				preActions << "\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t" << arg << " = " << scriptName << "->getManagedInstance();" << std::endl;
				preActions << "\t\telse\n";
				preActions << "\t\t\t" << arg << " = nullptr;\n";
			}
			else
			{
				std::string tmpType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type);
				preActions << "\t\t" << tmpType << " " << arg << ";" << std::endl;
				
				preActions << generateManagedToScriptObjectLine("\t\t", scriptType, scriptName, "value." + name, paramTypeInfo.type, varTypeInfo.flags);
				preActions << "\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t" << arg << " = " << generateGetInternalLine(varTypeInfo.typeName, scriptName, paramTypeInfo.type, varTypeInfo.flags) << ";" << std::endl;
			}

			if(!isSrcGHandle(varTypeInfo.flags) && !isSrcRHandle(varTypeInfo.flags))
				outs() << "Error: Invalid struct member type for \"" << name << "\"\n";
		}
		break;
		}

		return arg;
	}
	else
	{
		std::string entryType;
		switch (paramTypeInfo.type)
		{
		case ParsedType::Builtin:
		case ParsedType::String:
		case ParsedType::WString:
		case ParsedType::Path:
		case ParsedType::Enum:
			entryType = varTypeInfo.typeName;
			break;
		case ParsedType::MonoObject:
			entryType = "MonoObject*";
			break;
		default: // Some object or struct type
			entryType = getScriptInteropType(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));
			break;
		}

		std::string argType;
		if(isVector(varTypeInfo.flags))
			argType = "Vector<" + getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags, false) + ">";
		else if(isSmallVector(varTypeInfo.flags))
			argType = "SmallVector<" + getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags, false) + ", " + std::to_string(varTypeInfo.arraySize) + ">";
		else
			argType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags, false);

		std::string argName = "vec" + name;

		if (!toInterop)
		{
			std::string arrayName = "array" + name;
			preActions << "\t\t" << argType << " " << argName;
			if (isArray(varTypeInfo.flags))
				preActions << "[" << varTypeInfo.arraySize << "]";
			preActions << ";" << std::endl;

			preActions << "\t\tif(value." << name << " != nullptr)\n";
			preActions << "\t\t{\n";
			preActions << "\t\t\tScriptArray " << arrayName << "(value." << name << ");" << std::endl;

			if(isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
				preActions << "\t\t\t" << argName << ".resize(" << arrayName << ".size());" << std::endl;

			preActions << "\t\t\tfor(int i = 0; i < (int)" << arrayName << ".size(); i++)" << std::endl;
			preActions << "\t\t\t{" << std::endl;

			switch (paramTypeInfo.type)
			{
			case ParsedType::Builtin:
			case ParsedType::String:
			case ParsedType::WString:
			case ParsedType::Path:
				preActions << "\t\t\t\t" << argName << "[i] = " << arrayName << ".get<" << entryType << ">(i);" << std::endl;
				break;
			case ParsedType::MonoObject:
				outs() << "Error: MonoObject type not supported as input. Ignoring. \n";
				break;
			case ParsedType::Enum:
			{
				std::string enumType;
				mapBuiltinTypeToCppType(paramTypeInfo.underlyingType, enumType);

				preActions << "\t\t\t\t" << argName << "[i] = (" << entryType << ")" << arrayName << ".get<" << enumType << ">(i);" << std::endl;
				break;
			}
			case ParsedType::Struct:
				preActions << "\t\t\t\t" << argName << "[i] = ";

				if (isComplexStruct(varTypeInfo.flags))
				{
					preActions << entryType << "::fromInterop(";
					preActions << arrayName << ".get<" << getStructInteropType(varTypeInfo.typeName) << ">(i)";
					preActions << ")";
				}
				else
					preActions << arrayName << ".get<" << varTypeInfo.typeName << ">(i)";

				preActions << ";\n";
				break;
			default: // Some object type
			{
				std::string scriptName = "script" + name;
				preActions << generateManagedToScriptObjectLine("\t\t\t\t", entryType, scriptName, arrayName + ".get<MonoObject*>(i)", paramTypeInfo.type, varTypeInfo.flags);
				
				preActions << "\t\t\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t\t{\n";

				std::string elemPtrType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags);
				std::string elemPtrName = "arrayElemPtr" + name;

				preActions << "\t\t\t\t\t" << elemPtrType << " " << elemPtrName << " = " << 
					generateGetInternalLine(varTypeInfo.typeName, scriptName, paramTypeInfo.type, varTypeInfo.flags) << ";\n";

				if(paramTypeInfo.type == ParsedType::Class || paramTypeInfo.type == ParsedType::ReflectableClass)
				{
					if(isSrcPointer(varTypeInfo.flags))
						preActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ".get();\n";
					else if((isSrcReference(varTypeInfo.flags) || isSrcValue(varTypeInfo.flags)) && !isSrcSPtr(varTypeInfo.flags))
					{
						preActions << "\t\t\t\t\tif(" << elemPtrName << ")\n";
						preActions << "\t\t\t\t\t\t" << argName << "[i] = *" << elemPtrName << ";\n";
					}
					else
						preActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";
				}
				else
					preActions << "\t\t\t\t\t" << argName << "[i] = " << elemPtrName << ";\n";

				preActions << "\t\t\t\t}\n";
			}
			break;
			}

			preActions << "\t\t\t}" << std::endl;
			preActions << "\t\t}\n";
		}
		else
		{
			preActions << "\t\tint arraySize" << name << " = ";
			if (isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
				preActions << "(int)value." << name << ".size()";
			else
				preActions << varTypeInfo.arraySize;
			preActions << ";\n";

			preActions << "\t\tMonoArray* " << argName << ";" << std::endl;

			std::string arrayName = "array" + name;
			preActions << "\t\tScriptArray " << arrayName;
			preActions << " = " << "ScriptArray::create<" << entryType << ">(arraySize" << name << ");" << std::endl;
			preActions << "\t\tfor(int i = 0; i < arraySize" << name << "; i++)" << std::endl;
			preActions << "\t\t{" << std::endl;

			switch (paramTypeInfo.type)
			{
			case ParsedType::Builtin:
			case ParsedType::String:
			case ParsedType::WString:
			case ParsedType::Path:
				preActions << "\t\t\t" << arrayName << ".set(i, value." << name << "[i]);" << std::endl;
				break;
			case ParsedType::Enum:
			{
				std::string enumType;
				mapBuiltinTypeToCppType(paramTypeInfo.underlyingType, enumType);

				preActions << "\t\t\t" << arrayName << ".set(i, (" << enumType << ")value." << name << "[i]);" << std::endl;
				break;
			}
			case ParsedType::Struct:
				preActions << "\t\t\t" << arrayName << ".set(i, ";

				if(isComplexStruct(varTypeInfo.flags))
					preActions << entryType << "::toInterop(";

				preActions << "value." << name << "[i]";

				if (isComplexStruct(varTypeInfo.flags))
					preActions << ")";

				preActions << ");\n";
				break;
			case ParsedType::MonoObject:
				preActions << "\t\t\t" << arrayName << ".set(i, value." << name << "[i]);" << std::endl;
				break;
			case ParsedType::Class:
			case ParsedType::ReflectableClass:
			{
				std::string elemName = "arrayElem" + name;

				std::string elemPtrType = getCppVarType(varTypeInfo.typeName, paramTypeInfo.type, varTypeInfo.flags);
				std::string elemPtrName = "arrayElemPtr" + name;

				preActions << "\t\t\t" << elemPtrType << " " << elemPtrName;
				if(willBeDereferenced(varTypeInfo.flags))
				{
					preActions << " = bs_shared_ptr_new<" << varTypeInfo.typeName << ">();\n";

					if (isSrcPointer(varTypeInfo.flags))
					{
						preActions << "\t\t\tif(value." << name << "[i])\n";
						preActions << "\t\t\t\t*" << elemPtrName << " = *";
					}
					else
					{
						preActions << "\t\t\t*" << elemPtrName << " = ";
					}

					preActions << "value." << name << "[i];\n";
				}
				else
					preActions << " = value." << name << "[i];\n";

				preActions << "\t\t\tMonoObject* " << elemName << ";\n";
				preActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, elemName, 
					entryType, elemPtrName, false, "\t\t\t");

				preActions << "\t\t\t" << arrayName << ".set(i, " << elemName << ");" << std::endl;
			}
			break;
			case ParsedType::GUIElement:
				// Unsupported as output
				break;
			default: // Some resource or game object type
			{
				std::string scriptName = "script" + name;

				preActions << generateNativeToScriptObjectLine(paramTypeInfo.type, varTypeInfo.flags, scriptName, "value." + name + "[i]", "\t\t\t");
				preActions << "\t\t\t\tif(" << scriptName << " != nullptr)\n";
				preActions << "\t\t\t\t" << arrayName << ".set(i, " << scriptName << "->getManagedInstance());" << std::endl;
				preActions << "\t\t\telse\n";
				preActions << "\t\t\t\t" << arrayName << ".set(i, nullptr);" << std::endl;
			}
			break;
			}

			preActions << "\t\t}" << std::endl;
			preActions << "\t\t" << argName << " = " << arrayName << ".getInternal();" << std::endl;
		}

		return argName;
	}
}

std::string generateEventCallbackBodyBlockForParam(const std::string& name, const VarTypeInfo& varTypeInfo, std::stringstream& preCallActions)
{
	UserTypeInfo paramTypeInfo = getTypeInfo(varTypeInfo.typeName, varTypeInfo.flags);

	if (getIsAsyncOp(varTypeInfo.flags))
	{
		outs() << "Error: AsyncOp type not supported as an event callback parameter. \n";
		return "";
	}

	if (!isArrayOrVector(varTypeInfo.flags))
	{
		std::string argName;

		switch (paramTypeInfo.type)
		{
		case ParsedType::Builtin:
			argName = name;
			break;
		case ParsedType::Enum:
			if(isFlagsEnum(varTypeInfo.flags))
			{
				argName = "tmp" + name;
				preCallActions << "\t\t" << varTypeInfo.typeName << argName << ";" << std::endl;
				preCallActions << "\t\t" << argName << " = (" << varTypeInfo.typeName << ")(uint32_t)" << name << ";" << std::endl;
			}
			else
				argName = name;
			break;
		case ParsedType::Struct:
			{
				argName = "tmp" + name;

				std::string scriptType = getScriptInteropType(varTypeInfo.typeName);
				preCallActions << "\t\tMonoObject* " << argName << ";\n";

				if(isComplexStruct(varTypeInfo.flags))
				{
					std::string interopName = "interop" + name;
					std::string interopType = getStructInteropType(varTypeInfo.typeName);
					
					preCallActions << "\t\t" << interopType << " " << interopName << ";" << std::endl;
					preCallActions << "\t\t" << interopName << " = " << scriptType << "::toInterop(" << name << ");" << std::endl;
					preCallActions << "\t\t" << argName << " = " << scriptType << "::box(" << interopName << ");\n";
				}
				else
					preCallActions << "\t\t" << argName << " = " << scriptType << "::box(" << name << ");\n";
			}

			break;
		case ParsedType::String:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoString* " << argName << ";" << std::endl;
			preCallActions << "\t\t" << argName << " = MonoUtil::stringToMono(" << name << ");" << std::endl;
		}
		break;
		case ParsedType::WString:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoString* " << argName << ";" << std::endl;
			preCallActions << "\t\t" << argName << " = MonoUtil::wstringToMono(" << name << ");" << std::endl;
		}
		break;
		case ParsedType::Path:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoString* " << argName << ";" << std::endl;
			preCallActions << "\t\t" << argName << " = MonoUtil::stringToMono(" << name << ".toString());" << std::endl;
		}
		break;
		case ParsedType::MonoObject:
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoObject* " << argName << " = " << name << ";\n";
		}
		break;
		case ParsedType::Class:
		case ParsedType::ReflectableClass:
		{
			argName = "tmp" + name;
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName);

			preCallActions << "\t\tMonoObject* " << argName << ";\n";
			preCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, argName, scriptType, name);
		}
			break;
		default: // Some resource or game object type
		{
			argName = "tmp" + name;
			preCallActions << "\t\tMonoObject* " << argName << ";" << std::endl;

			std::string scriptName = "script" + name;
			std::string scriptType = getScriptInteropType(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));

			preCallActions << generateNativeToScriptObjectLine(paramTypeInfo.type, varTypeInfo.flags, scriptName, name);
			preCallActions << "\t\tif(" << scriptName << " != nullptr)\n";
			preCallActions << "\t\t\t" << argName << " = " << scriptName << "->getManagedInstance();" << std::endl;
			preCallActions << "\t\telse\n";
			preCallActions << "\t\t\t" << argName << " = nullptr;\n";
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
		case ParsedType::Path:
		case ParsedType::Enum:
			entryType = varTypeInfo.typeName;
			break;
		case ParsedType::MonoObject:
			entryType = "MonoObject*";
			break;
		default: // Some object or struct type
			entryType = getScriptInteropType(varTypeInfo.typeName, getPassAsResourceRef(varTypeInfo.flags));
			break;
		}

		std::string argName = "vec" + name;
		preCallActions << "\t\tMonoArray* " << argName << ";" << std::endl;

		preCallActions << "\t\tint arraySize" << name << " = ";
		if (isVector(varTypeInfo.flags) || isSmallVector(varTypeInfo.flags))
			preCallActions << "(int)value." << name << ".size()";
		else
			preCallActions << varTypeInfo.arraySize;
		preCallActions << ";\n";

		std::string arrayName = "array" + name;
		preCallActions << "\t\tScriptArray " << arrayName;
		preCallActions << " = " << "ScriptArray::create<" << entryType << ">(arraySize" << name << ");" << std::endl;
		preCallActions << "\t\tfor(int i = 0; i < arraySize" << name << "; i++)" << std::endl;
		preCallActions << "\t\t{" << std::endl;

		switch (paramTypeInfo.type)
		{
		case ParsedType::Builtin:
		case ParsedType::String:
		case ParsedType::WString:
		case ParsedType::Path:
			preCallActions << "\t\t\t" << arrayName << ".set(i, " << name << "[i]);" << std::endl;
			break;
		case ParsedType::Enum:
		{
			std::string enumType;
			mapBuiltinTypeToCppType(paramTypeInfo.underlyingType, enumType);

			if(isFlagsEnum(varTypeInfo.flags))
				preCallActions << "\t\t\t" << arrayName << ".set(i, (" << enumType << ")(uint32_t)" << name << "[i]);" << std::endl;
			else
				preCallActions << "\t\t\t" << arrayName << ".set(i, (" << enumType << ")" << name << "[i]);" << std::endl;
			break;
		}
		case ParsedType::Struct:
			preCallActions << "\t\t\t" << arrayName << ".set(i, ";

			if (isComplexStruct(varTypeInfo.flags))
				preCallActions << entryType << "::toInterop(";

			preCallActions << name << "[i]";

			if (isComplexStruct(varTypeInfo.flags))
				preCallActions << ")";

			preCallActions << ");\n";
			break;
		case ParsedType::MonoObject:
			preCallActions << "\t\t\t\t" << arrayName << ".set(i, " << name << "[i]);" << std::endl;
			break;
		case ParsedType::Class:
		case ParsedType::ReflectableClass:
		{
			std::string elemName = "arrayElem" + name;
			preCallActions << "\t\t\tMonoObject* " << elemName << ";\n";
			preCallActions << generateClassNativeToScriptObjectLine(varTypeInfo.flags, varTypeInfo.typeName, elemName,
				entryType, name + "[i]", false, "\t\t\t");
			preCallActions << "\t\t\t" << arrayName << ".set(i, " << elemName << ");" << std::endl;
		}
		break;
		default: // Some resource or game object type
		{
			std::string scriptName = "script" + name;

			preCallActions << generateNativeToScriptObjectLine(paramTypeInfo.type, varTypeInfo.flags, scriptName, name + "[i]", "\t\t\t");
			preCallActions << "\t\t\tif(" << scriptName << "[i] != nullptr)\n";
			preCallActions << "\t\t\t" << arrayName << ".set(i, " << scriptName << "->getManagedInstance());" << std::endl;
			preCallActions << "\t\t\telse\n";
			preCallActions << "\t\t\t\t" << arrayName << ".set(i, nullptr);" << std::endl;
		}
		break;
		}

		preCallActions << "\t\t}" << std::endl;
		preCallActions << "\t\t" << argName << " = " << arrayName << ".getInternal();" << std::endl;

		return argName;
	}
}

std::string generateCppMethodBody(const ClassInfo& classInfo, const MethodInfo& methodInfo, const std::string& sourceClassName,
	const std::string& interopClassName, ParsedType classType, bool isModule)
{
	std::string returnAssignment;
	std::string returnStmt;
	std::stringstream preCallActions;
	std::stringstream methodArgs;
	std::stringstream postCallActions;

	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;

	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
	bool isCtor = (methodInfo.flags & (int)MethodFlags::Constructor) != 0;
	bool isExternal = (methodInfo.flags & (int)MethodFlags::External) != 0;

	bool returnAsParameter = false;
	UserTypeInfo returnTypeInfo;
	if (!methodInfo.returnInfo.typeName.empty() && !isCtor)
	{
		returnTypeInfo = getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
		if (!canBeReturned(returnTypeInfo.type, methodInfo.returnInfo.flags))
			returnAsParameter = true;
		else
		{
			std::string returnType = getInteropCppVarType(methodInfo.returnInfo.typeName, returnTypeInfo.type, methodInfo.returnInfo.flags);
			postCallActions << "\t\t" << returnType << " __output;" << std::endl;

			std::string argName = generateMethodBodyBlockForParam("__output", methodInfo.returnInfo, true, true, preCallActions, postCallActions);

			returnAssignment = argName + " = ";
			returnStmt = "\t\treturn __output;";
		}
	}

	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		bool isLast = (I + 1) == methodInfo.paramInfos.end();

		std::string argName = generateMethodBodyBlockForParam(I->name, *I, isLast, false, preCallActions, postCallActions);

		if (!isArrayOrVector(I->flags))
		{
			UserTypeInfo paramTypeInfo = getTypeInfo(I->typeName, I->flags);

			methodArgs << getAsManagedToCppArgument(argName, paramTypeInfo.type, I->flags, methodInfo.sourceName);
		}
		else
			methodArgs << getAsManagedToCppArgumentPlain(argName, I->flags, isOutput(I->flags), methodInfo.sourceName);

		if (!isLast)
			methodArgs << ", ";
	}

	if (returnAsParameter)
	{
		std::string argName = generateMethodBodyBlockForParam("__output", methodInfo.returnInfo, true, true, preCallActions, postCallActions);

		returnAssignment = argName + " = ";
	}

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	if (isCtor)
	{
		bool isValid = false;
		if (!isExternal)
		{
			if (isClassType(classType))
			{
				output << "\t\tSPtr<" << sourceClassName << "> instance = bs_shared_ptr_new<" << sourceClassName << ">(" << methodArgs.str() << ");" << std::endl;
				output << "\t\tnew (bs_alloc<" << interopClassName << ">())" << interopClassName << "(managedInstance, instance);" << std::endl;
				isValid = true;
			}
		}
		else
		{
			std::string fullMethodName = methodInfo.externalClass + "::" + methodInfo.sourceName;

			if (isClassType(classType))
			{
				output << "\t\tSPtr<" << sourceClassName << "> instance = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				output << "\t\tnew (bs_alloc<" << interopClassName << ">())" << interopClassName << "(managedInstance, instance);" << std::endl;
				isValid = true;
			}
			else if (classType == ParsedType::Resource)
			{
				output << "\t\tResourceHandle<" << sourceClassName << "> instance = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				output << "\t\tScriptResourceManager::instance().createBuiltinScriptResource(instance, managedInstance);" << std::endl;
				isValid = true;
			}
			else if (classType == ParsedType::GUIElement)
			{
				output << "\t\t" << sourceClassName << "* instance = " << fullMethodName << "(" << methodArgs.str() << ");" << std::endl;
				output << "\t\tnew (bs_alloc<" << interopClassName << ">())" << interopClassName << "(managedInstance, instance);" << std::endl;
				isValid = true;
			}
		}

		if (!isValid)
			outs() << "Error: Cannot generate a constructor for \"" << sourceClassName << "\". Unsupported class type. \n";
	}
	else
	{
		std::stringstream methodCall;
		if (!isExternal)
		{
			if (isStatic)
				methodCall << sourceClassName << "::" << methodInfo.sourceName << "(" << methodArgs.str() << ")"; 
			else if(isModule)
				methodCall << sourceClassName << "::instance()." << methodInfo.sourceName << "(" << methodArgs.str() << ")";
			else
			{
				methodCall << generateGetInternalLine(sourceClassName, "thisPtr", classType, isBase ? (int)TypeFlags::ReferencesBase : 0);
				methodCall << "->" << methodInfo.sourceName << "(" << methodArgs.str() << ")";
			}
		}
		else
		{
			std::string fullMethodName = methodInfo.externalClass + "::" + methodInfo.sourceName;
			if (isStatic)
				methodCall << fullMethodName << "(" << methodArgs.str() << ")";
			else
			{
				methodCall << fullMethodName << "(" << generateGetInternalLine(sourceClassName, "thisPtr", classType, isBase ? (int)TypeFlags::ReferencesBase : 0);

				std::string methodArgsStr = methodArgs.str();
				if (!methodArgsStr.empty())
					methodCall << ", " << methodArgsStr;

				methodCall << ")";
			}
		}

		std::string call;
		if (!methodInfo.returnInfo.typeName.empty())
		{
			// Dereference input if needed
			if (isClassType(returnTypeInfo.type) && !isArrayOrVector(methodInfo.returnInfo.flags))
			{
				if ((isSrcPointer(methodInfo.returnInfo.flags) || isSrcReference(methodInfo.returnInfo.flags) || 
					isSrcValue(methodInfo.returnInfo.flags)) && !isSrcSPtr(methodInfo.returnInfo.flags))
					returnAssignment = "*" + returnAssignment;
			}

			call = getAsCppToInteropArgument(methodCall.str(), returnTypeInfo.type, methodInfo.returnInfo.flags, "return");
		}
		else
			call = methodCall.str();

		output << "\t\t" << returnAssignment << call << ";\n";
	}

	std::string postCallActionsStr = postCallActions.str();
	if (!postCallActionsStr.empty())
		output << std::endl;

	output << postCallActionsStr;

	if (!returnStmt.empty())
	{
		output << std::endl;
		output << returnStmt << std::endl;
	}

	output << "\t}" << std::endl;
	return output.str();
}

std::string generateCppFieldGetterBody(const ClassInfo& classInfo, const FieldInfo& fieldInfo, const MethodInfo& methodInfo,	
	ParsedType classType, bool isModule)
{
	std::string returnAssignment;
	std::string returnStmt;
	std::stringstream preCallActions;
	std::stringstream methodArgs;
	std::stringstream postCallActions;

	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;
	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;

	bool returnAsParameter = false;
	UserTypeInfo returnTypeInfo = getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
	if (!canBeReturned(returnTypeInfo.type, methodInfo.returnInfo.flags))
		returnAsParameter = true;
	else
	{
		std::string returnType = getInteropCppVarType(methodInfo.returnInfo.typeName, returnTypeInfo.type, methodInfo.returnInfo.flags);
		postCallActions << "\t\t" << returnType << " __output;" << std::endl;

		std::string argName = generateMethodBodyBlockForParam("__output", methodInfo.returnInfo, true, true, preCallActions, postCallActions);

		returnAssignment = argName + " = ";
		returnStmt = "\t\treturn __output;";
	}

	if (returnAsParameter)
	{
		std::string argName = generateMethodBodyBlockForParam("__output", methodInfo.returnInfo, true, true, preCallActions, postCallActions);

		returnAssignment = argName + " = ";
	}

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	std::stringstream fieldAccess;
	if (isStatic)
		fieldAccess << classInfo.name << "::" << fieldInfo.name; 
	else if(isModule)
		fieldAccess << classInfo.name << "::instance()." << fieldInfo.name;
	else
	{
		fieldAccess << generateGetInternalLine(classInfo.name, "thisPtr", classType, isBase ? (int)TypeFlags::ReferencesBase : 0);
		fieldAccess << "->" << fieldInfo.name;
	}

	// Dereference input if needed
	if (isClassType(returnTypeInfo.type) && !isArrayOrVector(methodInfo.returnInfo.flags))
	{
		if ((isSrcPointer(methodInfo.returnInfo.flags) || isSrcReference(methodInfo.returnInfo.flags) || 
			isSrcValue(methodInfo.returnInfo.flags)) && !isSrcSPtr(methodInfo.returnInfo.flags))
			returnAssignment = "*" + returnAssignment;
	}

	std::string access = getAsCppToInteropArgument(fieldAccess.str(), returnTypeInfo.type, methodInfo.returnInfo.flags, "return");

	output << "\t\t" << returnAssignment << access << ";\n";

	std::string postCallActionsStr = postCallActions.str();
	if (!postCallActionsStr.empty())
		output << std::endl;

	output << postCallActionsStr;

	output << std::endl;
	output << returnStmt << std::endl;

	output << "\t}" << std::endl;
	return output.str();
}

std::string generateCppFieldSetterBody(const ClassInfo& classInfo, const FieldInfo& fieldInfo, const MethodInfo& methodInfo,
	ParsedType classType, bool isModule)
{
	std::stringstream preCallActions;
	std::stringstream argValue;
	std::stringstream postCallActions;

	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;
	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;

	const VarInfo& paramInfo = methodInfo.paramInfos[0];
	std::string argName = generateMethodBodyBlockForParam(paramInfo.name, paramInfo, false, false, preCallActions, postCallActions);

	UserTypeInfo paramTypeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);

	if(!isArrayOrVector(paramInfo.flags))
		argValue << getAsManagedToCppArgument(argName, paramTypeInfo.type, paramInfo.flags, methodInfo.sourceName);
	else
		argValue << argName;

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	std::stringstream fieldAccess;
	if (isStatic)
		fieldAccess << classInfo.name << "::" << fieldInfo.name; 
	else if(isModule)
		fieldAccess << classInfo.name << "::instance()." << fieldInfo.name;
	else
	{
		fieldAccess << generateGetInternalLine(classInfo.name, "thisPtr", classType, isBase ? (int)TypeFlags::ReferencesBase : 0);
		fieldAccess << "->" << fieldInfo.name;
	}

	output << "\t\t" << fieldAccess.str() << " = " << argValue.str() << ";\n";

	std::string postCallActionsStr = postCallActions.str();
	if (!postCallActionsStr.empty())
		output << std::endl;

	output << postCallActionsStr;

	output << "\t}" << std::endl;
	return output.str();
}

std::string generateCppEventCallbackBody(const MethodInfo& eventInfo, bool isModule)
{
	std::stringstream preCallActions;
	std::stringstream methodArgs;

	bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;

	int idx = 0;
	for (auto I = eventInfo.paramInfos.begin(); I != eventInfo.paramInfos.end(); ++I)
	{
		bool isLast = (I + 1) == eventInfo.paramInfos.end();

		std::string argName = generateEventCallbackBodyBlockForParam(I->name, *I, preCallActions);

		if (!isArrayOrVector(I->flags))
		{
			UserTypeInfo paramTypeInfo = getTypeInfo(I->typeName, I->flags);

			if(paramTypeInfo.type == ParsedType::Struct)
				methodArgs << getAsCppToManagedArgument(argName, ParsedType::Class, I->flags, eventInfo.sourceName);
			else
				methodArgs << getAsCppToManagedArgument(argName, paramTypeInfo.type, I->flags, eventInfo.sourceName);
		}
		else
			methodArgs << getAsCppToManagedArgument(argName, ParsedType::Class, I->flags, eventInfo.sourceName);

		if (!isLast)
			methodArgs << ", ";

		idx++;
	}

	std::stringstream output;
	output << "\t{" << std::endl;
	output << preCallActions.str();

	output << "\t\tMonoUtil::invokeThunk(" << eventInfo.sourceName << "Thunk";

	if (!isStatic && !isModule)
		output << ", getManagedInstance()";
	
	if (!eventInfo.paramInfos.empty())
		output << ", " << methodArgs.str();

	output << ");\n";

	output << "\t}" << std::endl;
	return output.str();
}

std::string generateCppHeaderOutput(const ClassInfo& classInfo, const UserTypeInfo& typeInfo)
{
	bool inEditor = hasAPIBED (classInfo.api);
	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;
	bool isModule = (classInfo.flags & (int)ClassFlags::IsModule) != 0;
	bool isRootBase = classInfo.baseClass.empty();

	bool hasStaticEvents = isModule && !classInfo.eventInfos.empty();
	if (!hasStaticEvents)
	{
		for (auto& eventInfo : classInfo.eventInfos)
		{
			bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
			if (isStatic)
			{
				hasStaticEvents = true;
				break;
			}
		}
	}

	std::string exportAttr;
	if (!inEditor)
		exportAttr = sFrameworkExportMacro;
	else
		exportAttr = sEditorExportMacro;

	std::string wrappedDataType = getCppVarType(classInfo.name, typeInfo.type);
	std::string interopBaseClassName;

	std::stringstream output;
	output << generateCppApiCheckBegin(classInfo.api);

	// Generate a common base class if required
	// (GUIElements already have one by default)
	if(typeInfo.type != ParsedType::GUIElement)
	{
		if (isBase)
		{
			interopBaseClassName = getScriptInteropType(classInfo.name) + "Base";

			output << "\tclass " << exportAttr << " ";
			output << interopBaseClassName << " : public ";

			if (isRootBase)
			{
				if (typeInfo.type == ParsedType::Class)
					output << "ScriptObjectBase";
				if (typeInfo.type == ParsedType::ReflectableClass)
					output << "ScriptReflectableBase";
				else if (typeInfo.type == ParsedType::Component)
					output << "ScriptComponentBase";
				else if (typeInfo.type == ParsedType::Resource)
					output << "ScriptResourceBase";
			}
			else
			{
				std::string parentBaseClassName = getScriptInteropType(classInfo.baseClass) + "Base";
				output << parentBaseClassName;
			}

			output << std::endl;
			output << "\t{" << std::endl;
			output << "\tpublic:" << std::endl;
			output << "\t\t" << interopBaseClassName << "(MonoObject* instance);" << std::endl;
			output << "\t\tvirtual ~" << interopBaseClassName << "() {}" << std::endl;

			if(!isModule)
			{
				if (typeInfo.type == ParsedType::ReflectableClass)
				{
					output << std::endl;
					output << "\t\t" << wrappedDataType << " getInternal() const;\n";
				}
				else if (typeInfo.type == ParsedType::Class)
				{
					output << std::endl;
					output << "\t\t" << wrappedDataType << " getInternal() const { return mInternal; }" << std::endl;

					// Data member only present in the top-most base class
					if (isRootBase)
					{
						output << "\tprotected:" << std::endl;
						output << "\t\t" << wrappedDataType << " mInternal;" << std::endl;
					}
				}
			}

			output << "\t};" << std::endl;
			output << std::endl;
		}
		else if (!classInfo.baseClass.empty())
		{
			interopBaseClassName = getScriptInteropType(classInfo.baseClass) + "Base";
		}
	}

	// Generate main class
	output << "\tclass " << exportAttr << " ";;

	std::string interopClassName = getScriptInteropType(classInfo.name);
	output << interopClassName << " : public ";

	if (typeInfo.type == ParsedType::Resource)
		output << "TScriptResource<" << interopClassName << ", " << classInfo.name;
	else if (typeInfo.type == ParsedType::Component)
		output << "TScriptComponent<" << interopClassName << ", " << classInfo.name;
	else if (typeInfo.type == ParsedType::GUIElement)
		output << "TScriptGUIElement<" << interopClassName;
	else if (typeInfo.type == ParsedType::ReflectableClass)
		output << "TScriptReflectable<" << interopClassName << ", " << classInfo.name;
	else // Class
		output << "ScriptObject<" << interopClassName;

	if (!interopBaseClassName.empty())
		output << ", " << interopBaseClassName;

	output << ">";

	output << std::endl;
	output << "\t{" << std::endl;
	output << "\tpublic:" << std::endl;

	if (!inEditor)
		output << "\t\tSCRIPT_OBJ(ENGINE_ASSEMBLY, ENGINE_NS, \"" << typeInfo.scriptName << "\")" << std::endl;
	else
		output << "\t\tSCRIPT_OBJ(EDITOR_ASSEMBLY, EDITOR_NS, \"" << typeInfo.scriptName << "\")" << std::endl;

	output << std::endl;

	// Constructor
	if (!isModule)
	{
		output << "\t\t" << interopClassName << "(MonoObject* managedInstance, ";

		if (typeInfo.type != ParsedType::GUIElement)
			output << "const " << wrappedDataType << "& value";
		else
			output << wrappedDataType << " value";

		output << ");\n";
	}
	else
		output << "\t\t" << interopClassName << "(MonoObject* managedInstance);" << std::endl;

	output << std::endl;

	if (typeInfo.type == ParsedType::Class && !isModule)
	{
		// getInternal() method (handle types have getHandle() implemented by their base type)
		if (isBase || !classInfo.baseClass.empty())
			output << "\t\t" << wrappedDataType << " getInternal() const;\n";
		else
			output << "\t\t" << wrappedDataType << " getInternal() const { return mInternal; }" << std::endl;
	}

	if(isClassType(typeInfo.type) && !isModule)
	{
		// getManagedInstance() method (needed for events)
		if (!classInfo.eventInfos.empty())
			output << "\t\tMonoObject* getManagedInstance() const;\n";

		// create() method
		output << "\t\tstatic MonoObject* create(const " << wrappedDataType << "& value);" << std::endl;
		output << std::endl;
	}

	if (typeInfo.type == ParsedType::Resource)
	{
		// createInstance() method required by script resource manager
		output << "\t\tstatic MonoObject* createInstance();" << std::endl;
		output << std::endl;
	}

	// Static start-up and shut-down methods, if required
	if(hasStaticEvents)
	{
		output << "\t\tstatic void startUp();" << std::endl;
		output << "\t\tstatic void shutDown();" << std::endl;
		output << std::endl;
	}

	output << "\tprivate:" << std::endl;

	// Handle (if required)
	if (isClassType(typeInfo.type))
	{
		if (!classInfo.eventInfos.empty())
			output << "\t\tuint32_t mGCHandle = 0;\n\n";
	}

	// Event callback methods
	for (auto& eventInfo : classInfo.eventInfos)
	{
		output << generateCppApiCheckBegin(eventInfo.api);
		output << "\t\t" << generateCppEventCallbackSignature(eventInfo, "", isModule) << ";" << std::endl;
		output << generateApiCheckEnd(eventInfo.api);
	}

	if(!classInfo.eventInfos.empty())
		output << std::endl;

	// Data member
	if (typeInfo.type == ParsedType::Class && !isModule && classInfo.baseClass.empty() && !isBase)
	{
		output << "\t\t" << wrappedDataType << " mInternal;" << std::endl;
		output << std::endl;
	}

	// Event thunks
	for (auto& eventInfo : classInfo.eventInfos)
	{
		output << generateCppApiCheckBegin(eventInfo.api);
		output << generateCppEventThunk(eventInfo, isModule);
		output << generateApiCheckEnd(eventInfo.api);
	}

	if(!classInfo.eventInfos.empty())
		output << std::endl;

	// Event handles
	for (auto& eventInfo : classInfo.eventInfos)
	{
		bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
		bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
		if(!isCallback && (isStatic || isModule))
		{
			output << generateCppApiCheckBegin(eventInfo.api);
			output << "\t\tstatic HEvent " << eventInfo.sourceName << "Conn;" << std::endl;
			output << generateApiCheckEnd(eventInfo.api);
		}
	}

	if(hasStaticEvents)
		output << std::endl;

	// CLR hooks
	std::string interopClassThisPtrType;
	if (isBase)
	{
		if(typeInfo.type == ParsedType::GUIElement)
			interopClassThisPtrType = "ScriptGUIElementBaseTBase";
		else
			interopClassThisPtrType = interopBaseClassName;
	}
	else
		interopClassThisPtrType = interopClassName;

	// Internal_GetRef interop method
	if (typeInfo.type == ParsedType::Resource)
		output << "\t\tstatic MonoObject* Internal_getRef(" << interopClassThisPtrType << "* thisPtr);\n\n";

	for (auto& methodInfo : classInfo.ctorInfos)
	{
		if (isCSOnly(methodInfo.flags))
			continue;

		output << generateCppApiCheckBegin(methodInfo.api);
		output << "\t\tstatic " << generateCppMethodSignature(methodInfo, interopClassThisPtrType, "", isModule) << ";" << std::endl;
		output << generateApiCheckEnd(methodInfo.api);
	}

	for (auto& methodInfo : classInfo.methodInfos)
	{
		if (isCSOnly(methodInfo.flags))
			continue;

		output << generateCppApiCheckBegin(methodInfo.api);
		output << "\t\tstatic " << generateCppMethodSignature(methodInfo, interopClassThisPtrType, "", isModule) << ";" << std::endl;
		output << generateApiCheckEnd(methodInfo.api);
	}

	output << "\t};" << std::endl;
	output << generateApiCheckEnd(classInfo.api);

	return output.str();
}

std::string generateCppSourceOutput(const ClassInfo& classInfo, const UserTypeInfo& typeInfo)
{
	bool isBase = (classInfo.flags & (int)ClassFlags::IsBase) != 0;
	bool isModule = (classInfo.flags & (int)ClassFlags::IsModule) != 0;

	bool hasStaticEvents = isModule && !classInfo.eventInfos.empty();
	for(auto& eventInfo : classInfo.eventInfos)
	{
		bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
		if(isStatic)
		{
			hasStaticEvents = true;
			break;
		}
	}

	std::string interopClassName = getScriptInteropType(classInfo.name);
	std::string wrappedDataType = getCppVarType(classInfo.name, typeInfo.type);

	std::string interopBaseClassName;

	if(typeInfo.type != ParsedType::GUIElement)
	{
		if (isBase)
			interopBaseClassName = getScriptInteropType(classInfo.name) + "Base";
		else if (!classInfo.baseClass.empty())
			interopBaseClassName = getScriptInteropType(classInfo.baseClass) + "Base";
	}

	std::stringstream output;
	output << generateCppApiCheckBegin(classInfo.api);

	if (isBase && typeInfo.type != ParsedType::GUIElement)
	{
		// Base class constructor
		output << "\t" << interopBaseClassName << "::" << interopBaseClassName << "(MonoObject* managedInstance)\n";
		output << "\t\t:";

		bool isRootBase = classInfo.baseClass.empty();
		if (isRootBase)
		{
			if (typeInfo.type == ParsedType::Class)
				output << "ScriptObjectBase";
			if (typeInfo.type == ParsedType::ReflectableClass)
				output << "ScriptReflectableBase";
			else if (typeInfo.type == ParsedType::Component)
				output << "ScriptComponentBase";
			else if (typeInfo.type == ParsedType::Resource)
				output << "ScriptResourceBase";
		}
		else
		{
			std::string parentBaseClassName = getScriptInteropType(classInfo.baseClass) + "Base";
			output << parentBaseClassName;
		}

		output << "(managedInstance)\n";
		output << "\t { }\n";
		output << "\n";

		// Base class getInternal() method
		if(typeInfo.type == ParsedType::ReflectableClass)
		{
			output << "\t" << wrappedDataType << " " << interopBaseClassName << "::" << "getInternal() const\n";
			output << "\t{\n";
			output << "\t\treturn std::static_pointer_cast<" << classInfo.name << ">(mInternal);\n";
			output << "\t}\n";
		}
	}

	// Event thunks
	for (auto& eventInfo : classInfo.eventInfos)
	{
		output << generateCppApiCheckBegin(eventInfo.api);
		output << "\t" << interopClassName << "::" << eventInfo.sourceName << "ThunkDef " << interopClassName << "::" << eventInfo.sourceName << "Thunk; \n";
		output << generateApiCheckEnd(eventInfo.api);
	}

	if (!classInfo.eventInfos.empty())
		output << "\n";

	// Event handles
	bool hasEventHandles = false;
	for (auto& eventInfo : classInfo.eventInfos)
	{
		bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
		bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
		if(!isCallback && (isStatic || isModule))
		{
			output << generateCppApiCheckBegin(eventInfo.api);
			output << "\tHEvent " << interopClassName << "::" << eventInfo.sourceName << "Conn;\n";
			output << generateApiCheckEnd(eventInfo.api);

			hasEventHandles = true;
		}
	}

	if (hasEventHandles)
		output << "\n";

	// Constructor
	if (!isModule)
	{
		output << "\t" << interopClassName << "::" << interopClassName << "(MonoObject* managedInstance, ";

		if (typeInfo.type != ParsedType::GUIElement)
			output << "const " << wrappedDataType << "& value";
		else
			output << wrappedDataType << " value";

		output << ")\n";
	}
	else
		output << "\t" << interopClassName << "::" << interopClassName << "(MonoObject* managedInstance)" << std::endl;

	output << "\t\t:";

	if (typeInfo.type == ParsedType::Resource)
		output << "TScriptResource(managedInstance, value)";
	else if (typeInfo.type == ParsedType::Component)
		output << "TScriptComponent(managedInstance, value)";
	else if (typeInfo.type == ParsedType::GUIElement)
		output << "TScriptGUIElement(managedInstance, value)";
	else if (typeInfo.type == ParsedType::ReflectableClass)
	{
		if(!isModule)
			output << "TScriptReflectable(managedInstance, value)";
		else
			output << "TScriptReflectable(managedInstance, nullptr)";
	}
	else // Class
	{
		if(!isModule && !isBase && classInfo.baseClass.empty())
			output << "ScriptObject(managedInstance), mInternal(value)";
		else
			output << "ScriptObject(managedInstance)";
	}
	output << std::endl;
	output << "\t{" << std::endl;

	if (isClassType(typeInfo.type))
	{
		if (!classInfo.eventInfos.empty())
			output << "\t\tmGCHandle = MonoUtil::newWeakGCHandle(managedInstance);\n";

		if (!isModule && (isBase || !classInfo.baseClass.empty()))
			output << "\t\tmInternal = value;\n";
	}

	// Register any non-static events
	if (!isModule)
	{
		for (auto& eventInfo : classInfo.eventInfos)
		{
			bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
			bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
			if (!isStatic)
			{
				output << generateCppApiCheckBegin(eventInfo.api);

				if (!isCallback)
					output << "\t\tvalue->" << eventInfo.sourceName << ".connect(";
				else
					output << "\t\tvalue->" << eventInfo.sourceName << " = ";

				output << "std::bind(&" << interopClassName << "::" << eventInfo.interopName << ", this";

				for (int i = 0; i < (int)eventInfo.paramInfos.size(); i++)
					output << ", std::placeholders::_" << (i + 1);

				if (!isCallback)
					output << ")";

				output << ");\n";
				output << generateApiCheckEnd(eventInfo.api);
			}
		}
	}

	output << "\t}" << std::endl;
	output << std::endl;

	if (typeInfo.type == ParsedType::Class)
	{
		// getInternal method
		if (isBase || !classInfo.baseClass.empty())
		{
			output << "\t" << wrappedDataType << " " << interopClassName << "::getInternal() const \n";
			output << "\t{\n";
			output << "\t\treturn std::static_pointer_cast<" << classInfo.name << ">(mInternal);\n";
			output << "\t}\n\n";
		}
	}

	if (isClassType(typeInfo.type) && !isModule)
	{
		// getManagedInstance() method (needed for events)
		if (!classInfo.eventInfos.empty())
		{
			output << "\tMonoObject* " << interopClassName << "::getManagedInstance() const\n";
			output << "\t{\n";
			output << "\t\treturn MonoUtil::getObjectFromGCHandle(mGCHandle);\n";
			output << "\t}\n\n";
		}
	}

	// CLR hook registration
	output << "\tvoid " << interopClassName << "::initRuntimeData()" << std::endl;
	output << "\t{" << std::endl;

	// Internal_GetRef interop method
	if (typeInfo.type == ParsedType::Resource)
	{
		output << "\t\tmetaData.scriptClass->addInternalCall(\"Internal_GetRef\", (void*)&" <<
			interopClassName << "::Internal_getRef);\n";
	}

	for (auto& methodInfo : classInfo.ctorInfos)
	{
		if (isCSOnly(methodInfo.flags))
			continue;

		output << generateCppApiCheckBegin(methodInfo.api);
		output << "\t\tmetaData.scriptClass->addInternalCall(\"Internal_" << methodInfo.interopName << "\", (void*)&" <<
			interopClassName << "::Internal_" << methodInfo.interopName << ");" << std::endl;
		output << generateApiCheckEnd(methodInfo.api);
	}

	for (auto& methodInfo : classInfo.methodInfos)
	{
		if (isCSOnly(methodInfo.flags))
			continue;

		output << generateCppApiCheckBegin(methodInfo.api);
		output << "\t\tmetaData.scriptClass->addInternalCall(\"Internal_" << methodInfo.interopName << "\", (void*)&" <<
			interopClassName << "::Internal_" << methodInfo.interopName << ");" << std::endl;
		output << generateApiCheckEnd(methodInfo.api);
	}

	output << std::endl;

	for(auto& eventInfo : classInfo.eventInfos)
	{
		output << generateCppApiCheckBegin(eventInfo.api);
		output << "\t\t" << eventInfo.sourceName << "Thunk = ";
		output << "(" << eventInfo.sourceName << "ThunkDef)metaData.scriptClass->getMethodExact(";
		output << "\"Internal_" << eventInfo.interopName << "\", \"";

		for (auto I = eventInfo.paramInfos.begin(); I != eventInfo.paramInfos.end(); ++I)
		{
			const VarInfo& paramInfo = *I;
			UserTypeInfo paramTypeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);

			std::string typeName;

			// Generic types require `X after their name
			StringRef inputStr(paramTypeInfo.scriptName.data(), paramTypeInfo.scriptName.length());
			inputStr = inputStr.trim();

			const size_t leftBracketIdx = inputStr.find_first_of('<');
			const size_t rightBracketIdx = inputStr.find_last_of('>');
			const size_t numLeftBrackets = inputStr.count('<');
			const size_t numRightBrackets = inputStr.count('>');

			if (numLeftBrackets > 1 || numRightBrackets > 1)
			{
				outs() << "Error: Cannot parse event signature type. Nested generic parameters are not allowed.\n";
				typeName = paramTypeInfo.scriptName;
			}
			else if (leftBracketIdx != StringRef::npos && rightBracketIdx != StringRef::npos)
			{
				StringRef templateType = inputStr.substr(0, leftBracketIdx);
				StringRef templateArgs = inputStr.substr(leftBracketIdx + 1, rightBracketIdx - leftBracketIdx - 1);
				const size_t numTemplateArgs = templateArgs.count(',') + 1;

				typeName = templateType.str() + "`" + std::to_string(numTemplateArgs) + "<" + templateArgs.str() + ">";
			}
			else
				typeName = paramTypeInfo.scriptName;

			if(typeName == "float")
				typeName = "single";

			std::string csType = getCSVarType(typeName, paramTypeInfo.type, paramInfo.flags, true, true, true, true);

			output << csType;

			if ((I + 1) != eventInfo.paramInfos.end())
				output << ",";
		}

		output << "\")->getThunk();" << std::endl;
		output << generateApiCheckEnd(eventInfo.api);
	}

	output << "\t}" << std::endl;
	output << std::endl;

	// create() or createInstance() methods
	if ((isClassType(typeInfo.type) && !isModule) || typeInfo.type == ParsedType::Resource)
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
			ctorSignature << unusedCtor.paramInfos[i].typeName;

			if ((i + 1) < numDummyParams)
			{
				ctorParamsInit << ", ";
				ctorSignature << ",";
			}
		}

		ctorParamsInit << " };" << std::endl;
		ctorParamsInit << std::endl;

		if (isClassType(typeInfo.type))
		{
			output << "\tMonoObject* " << interopClassName << "::create(const " << wrappedDataType << "& value)" << std::endl;
			output << "\t{" << std::endl;
			output << "\t\tif(value == nullptr) return nullptr; " << std::endl;
			output << std::endl;

			output << ctorParamsInit.str();
			output << "\t\tMonoObject* managedInstance = metaData.scriptClass->createInstance(\"" << ctorSignature.str() << "\", ctorParams);" << std::endl;
			output << "\t\tnew (bs_alloc<" << interopClassName << ">()) " << interopClassName << "(managedInstance, value);" << std::endl;
			output << "\t\treturn managedInstance;" << std::endl;

			output << "\t}" << std::endl;
		}
		else if (typeInfo.type == ParsedType::Resource)
		{
			output << "\t MonoObject*" << interopClassName << "::createInstance()" << std::endl;
			output << "\t{" << std::endl;

			output << ctorParamsInit.str();
			output << "\t\treturn metaData.scriptClass->createInstance(\"" << ctorSignature.str() << "\", ctorParams);" << std::endl;

			output << "\t}" << std::endl;
		}
	}

	// Static start-up and shut-down methods, if required
	if(hasStaticEvents)
	{
		output << "\tvoid " << interopClassName << "::startUp()" << std::endl;
		output << "\t{" << std::endl;

		for(auto& eventInfo : classInfo.eventInfos)
		{
			bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
			bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
			if (!isCallback)
			{
				if (isStatic)
				{
					output << "\t\t" << eventInfo.sourceName << "Conn = ";
					output << classInfo.name << "::" << eventInfo.sourceName << ".connect(&" << interopClassName << "::" << eventInfo.interopName << ");" << std::endl;
				}
				else if (isModule)
				{

					output << "\t\t" << eventInfo.sourceName << "Conn = ";
					output << classInfo.name << "::instance()." << eventInfo.sourceName << ".connect(&" << interopClassName << "::" << eventInfo.interopName << ");" << std::endl;
				}
			}
			else
			{
				if (isStatic)
					output << classInfo.name << "::" << eventInfo.sourceName << " = &" << interopClassName << "::" << eventInfo.interopName << ";" << std::endl;
				else if (isModule)
					output << classInfo.name << "::instance()." << eventInfo.sourceName << " = &" << interopClassName << "::" << eventInfo.interopName << ";" << std::endl;
			}
		}

		output << "\t}" << std::endl;

		output << "\tvoid " << interopClassName << "::shutDown()" << std::endl;
		output << "\t{" << std::endl;

		for(auto& eventInfo : classInfo.eventInfos)
		{
			bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
			bool isCallback = (eventInfo.flags & (int)MethodFlags::Callback) != 0;
			if(!isCallback && (isStatic || isModule))
				output << "\t\t" << eventInfo.sourceName << "Conn.disconnect();" << std::endl;
		}

		output << "\t}" << std::endl;
		output << std::endl;
	}

	// Event callback method implementations
	for (auto I = classInfo.eventInfos.begin(); I != classInfo.eventInfos.end(); ++I)
	{
		const MethodInfo& eventInfo = *I;

		output << generateCppApiCheckBegin(eventInfo.api);
		output << "\t" << generateCppEventCallbackSignature(eventInfo, interopClassName, isModule) << std::endl;
		output << generateCppEventCallbackBody(eventInfo, isModule);
		output << generateApiCheckEnd(eventInfo.api);

		if ((I + 1) != classInfo.eventInfos.end())
			output << std::endl;
	}

	// CLR hook method implementations
	std::string interopClassThisPtrType;
	if (isBase)
	{
		if(typeInfo.type == ParsedType::GUIElement)
			interopClassThisPtrType = "ScriptGUIElementBaseTBase";
		else
			interopClassThisPtrType = interopBaseClassName;
	}
	else
		interopClassThisPtrType = interopClassName;

	// Internal_GetRef interop method
	if (typeInfo.type == ParsedType::Resource)
	{
		output << "\tMonoObject* " << interopClassName << "::Internal_getRef(" << interopClassThisPtrType << "* thisPtr)\n";
		output << "\t{\n";
		output << "\t\treturn thisPtr->getRRef();\n";
		output << "\t}\n\n";
	}

	// Constructors
	for (auto I = classInfo.ctorInfos.begin(); I != classInfo.ctorInfos.end(); ++I)
	{
		const MethodInfo& methodInfo = *I;

		if (isCSOnly(methodInfo.flags))
			continue;

		output << generateCppApiCheckBegin(methodInfo.api);
		output << "\t" << generateCppMethodSignature(methodInfo, interopClassThisPtrType, interopClassName, isModule) << std::endl;
		output << generateCppMethodBody(classInfo, methodInfo, classInfo.name, interopClassName, typeInfo.type, isModule);
		output << generateApiCheckEnd(methodInfo.api);

		if ((I + 1) != classInfo.methodInfos.end())
			output << std::endl;
	}

	// Methods
	for (auto I = classInfo.methodInfos.begin(); I != classInfo.methodInfos.end(); ++I)
	{
		const MethodInfo& methodInfo = *I;

		if (isCSOnly(methodInfo.flags))
			continue;

		if ((methodInfo.flags & (int)MethodFlags::FieldWrapper) != 0)
			continue;

		output << generateCppApiCheckBegin(methodInfo.api);
		output << "\t" << generateCppMethodSignature(methodInfo, interopClassThisPtrType, interopClassName, isModule) << std::endl;
		output << generateCppMethodBody(classInfo, methodInfo, classInfo.name, interopClassName, typeInfo.type, isModule);
		output << generateApiCheckEnd(methodInfo.api);

		if ((I + 1) != classInfo.methodInfos.end())
			output << std::endl;
	}

	// Field wrapper methods
	for(auto I = classInfo.fieldInfos.begin(); I != classInfo.fieldInfos.end(); ++I)
	{
		const MethodInfo* setterInfo = nullptr;
		const MethodInfo* getterInfo = nullptr;

		std::string getterName = "get" + I->name;
		std::string setterName = "set" + I->name;
		for(auto& entry : classInfo.methodInfos)
		{
			if ((entry.flags & (int)MethodFlags::FieldWrapper) == 0)
				continue;

			if (entry.sourceName == getterName)
				getterInfo = &entry;
			else if (entry.sourceName == setterName)
				setterInfo = &entry;

			if (getterInfo != nullptr && setterInfo != nullptr)
				break;
		}

		assert(getterInfo && setterInfo);

		output << generateCppApiCheckBegin(getterInfo->api);
		output << "\t" << generateCppMethodSignature(*getterInfo, interopClassThisPtrType, interopClassName, isModule) << std::endl;
		output << generateCppFieldGetterBody(classInfo, *I, *getterInfo, typeInfo.type, isModule);
		output << generateApiCheckEnd(getterInfo->api);
		
		output << std::endl;

		output << generateCppApiCheckBegin(setterInfo->api);
		output << "\t" << generateCppMethodSignature(*setterInfo, interopClassThisPtrType, interopClassName, isModule) << std::endl;
		output << generateCppFieldSetterBody(classInfo, *I, *setterInfo, typeInfo.type, isModule);
		output << generateApiCheckEnd(setterInfo->api);
			
		if ((I + 1) != classInfo.fieldInfos.end())
			output << std::endl;
	}

	output << generateApiCheckEnd(classInfo.api);

	return output.str();
}

std::string generateCppStructHeader(const StructInfo& structInfo)
{
	UserTypeInfo typeInfo = getTypeInfo(structInfo.name, 0);

	std::stringstream output;
	output << generateCppApiCheckBegin(structInfo.api);

	if(structInfo.requiresInterop)
	{
		output << "\tstruct " << structInfo.interopName << "\n";
		output << "\t{\n";

		for(auto& fieldInfo : structInfo.fields)
		{
			UserTypeInfo fieldTypeInfo = getTypeInfo(fieldInfo.typeName, fieldInfo.flags);

			output << "\t\t";
			output << getInteropCppVarType(fieldInfo.typeName, fieldTypeInfo.type, fieldInfo.flags, true);
			output << " " << fieldInfo.name << ";\n";
		}

		output << "\t};\n\n";
	}

	output << "\tclass ";

	bool inEditor = hasAPIBED (structInfo.api);
	if (!inEditor)
		output << sFrameworkExportMacro << " ";
	else
		output << sEditorExportMacro << " ";

	std::string interopClassName = getScriptInteropType(structInfo.name);
	output << interopClassName << " : public " << "ScriptObject<" << interopClassName << ">";

	output << std::endl;
	output << "\t{" << std::endl;
	output << "\tpublic:" << std::endl;

	if (!inEditor)
		output << "\t\tSCRIPT_OBJ(ENGINE_ASSEMBLY, ENGINE_NS, \"" << typeInfo.scriptName << "\")" << std::endl;
	else
		output << "\t\tSCRIPT_OBJ(EDITOR_ASSEMBLY, EDITOR_NS, \"" << typeInfo.scriptName << "\")" << std::endl;

	output << std::endl;

	output << "\t\tstatic MonoObject* box(const " << structInfo.interopName << "& value);" << std::endl;
	output << "\t\tstatic " << structInfo.interopName << " unbox(MonoObject* value);" << std::endl;

	if(structInfo.requiresInterop)
	{
		output << "\t\tstatic " << structInfo.name << " fromInterop(const " << structInfo.interopName << "& value);\n";
		output << "\t\tstatic " << structInfo.interopName << " toInterop(const " << structInfo.name << "& value);\n";
	}

	output << std::endl;
	output << "\tprivate:" << std::endl;

	// Constructor
	output << "\t\t" << interopClassName << "(MonoObject* managedInstance);" << std::endl;
	output << std::endl;

	output << "\t};" << std::endl;
	output << generateApiCheckEnd(structInfo.api);

	return output.str();
}

std::string generateCppStructSource(const StructInfo& structInfo)
{
	UserTypeInfo typeInfo = getTypeInfo(structInfo.name, 0);
	std::string interopClassName = getScriptInteropType(structInfo.name);

	std::stringstream output;
	output << generateCppApiCheckBegin(structInfo.api);

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
	output << "\tMonoObject*" << interopClassName << "::box(const " << structInfo.interopName << "& value)" << std::endl;
	output << "\t{" << std::endl;
	output << "\t\treturn MonoUtil::box(metaData.scriptClass->_getInternalClass(), (void*)&value);" << std::endl;
	output << "\t}" << std::endl;
	output << std::endl;

	// Unbox
	output << "\t" << structInfo.interopName << " " << interopClassName << "::unbox(MonoObject* value)" << std::endl;
	output << "\t{" << std::endl;
	output << "\t\treturn *(" << structInfo.interopName << "*)MonoUtil::unbox(value);" << std::endl;
	output << "\t}" << std::endl;
	output << std::endl;

	if(structInfo.requiresInterop)
	{
		// Convert from interop
		output << "\t" << structInfo.name << " " << interopClassName << "::fromInterop(const " << structInfo.interopName << "& value)\n";
		output << "\t{\n";

		output << "\t\t" << structInfo.name << " output;\n";
		for (auto& fieldInfo : structInfo.fields)
		{
			// Arrays can be assigned, so copy them entry by entry
			if(isArray(fieldInfo.flags))
			{
				std::string argName = generateFieldConvertBlock(fieldInfo.name, fieldInfo, false, output);

				output << "\t\tauto tmp" << fieldInfo.name << " = " << argName << ";\n";
				output << "\t\tfor(int i = 0; i < " << fieldInfo.arraySize << "; ++i)\n";
				output << "\t\t\toutput." << fieldInfo.name << "[i] = tmp" << fieldInfo.name << "[i];\n";
			}
			else
			{
				std::string argName = generateFieldConvertBlock(fieldInfo.name, fieldInfo, false, output);

				output << "\t\toutput." << fieldInfo.name << " = " << argName << ";\n";
			}
		}

		output << "\n";
		output << "\t\treturn output;\n";
		output << "\t}\n\n";

		// Convert to interop
		output << "\t" << structInfo.interopName << " " << interopClassName << "::toInterop(const " << structInfo.name << "& value)\n";
		output << "\t{\n";

		output << "\t\t" << structInfo.interopName << " output;\n";
		for(auto& fieldInfo : structInfo.fields)
		{
			std::string argName = generateFieldConvertBlock(fieldInfo.name, fieldInfo, true, output);

			output << "\t\toutput." << fieldInfo.name << " = " << argName << ";\n";
		}

		output << "\n";
		output << "\t\treturn output;\n";
		output << "\t}\n\n";
	}

	output << generateApiCheckEnd(structInfo.api);
	return output.str();
}

std::string generateCSStyleAttributes(const Style& style, const UserTypeInfo& typeInfo, int typeFlags, bool isStruct)
{
	std::stringstream output;
	
	if(((style.flags & (int)StyleFlags::AsLayerMask) != 0) && isInt64(typeInfo))
		output << "\t\t[LayerMask]\n";

	if ((style.flags & (int)StyleFlags::Step) != 0)
		output << "\t\t[Step(" << style.step << "f)]\n";

	if ((style.flags & (int)StyleFlags::Range) != 0)
	{
		std::string isSlider = ((style.flags & (int)StyleFlags::AsSlider) != 0) ? "true" : "false";
		output << "\t\t[Range(" << style.rangeMin << "f, " << style.rangeMax << "f, " << isSlider << ")]\n";
	}
	else if ((style.flags & (int)StyleFlags::AsSlider) != 0)
		output << "\t\t[Range(float.MinValue, float.MaxValue, true)]\n";

	if(((style.flags & (int)StyleFlags::Order) != 0))
		output << "\t\t[Order(" << style.order << ")]\n";

	if(((style.flags & (int)StyleFlags::Category) != 0))
		output << "\t\t[Category(\"" << style.category << "\")]\n";

	if(((style.flags & (int)StyleFlags::Inline) != 0))
		output << "\t\t[Inline]\n";

	bool notNull = (style.flags & (int)StyleFlags::NotNull) != 0;
	bool passByCopy = (style.flags & (int)StyleFlags::PassByCopy) != 0;

	if(!isStruct && (isClassType(typeInfo.type) && isPassedByValue(typeFlags)))
	{
		notNull = true;
		passByCopy = true;
	}

	if(notNull)
		output << "\t\t[NotNull]\n";

	if(passByCopy)
		output << "\t\t[PassByCopy]\n";

	if(((style.flags & (int)StyleFlags::ApplyOnDirty) != 0))
		output << "\t\t[ApplyOnDirty]\n";

	if(((style.flags & (int)StyleFlags::AsQuaternion) != 0))
		output << "\t\t[AsQuaternion]\n";

	if(((style.flags & (int)StyleFlags::LoadOnAssign) != 0))
		output << "\t\t[LoadOnAssign]\n";

	if(((style.flags & (int)StyleFlags::HDR) != 0))
		output << "\t\t[HDR]\n";

	return output.str();
}

std::string generateCSDefaultValueAssignment(const VarInfo& paramInfo)
{
	if (paramInfo.defaultValueType.empty() || isFlagsEnum(paramInfo.flags))
		return paramInfo.defaultValue;
	else
	{
		// Constructor or cast, assuming constructor as cast implies a constructor accepting the type exists (and we don't export cast operators anyway)
		UserTypeInfo defaultValTypeInfo = getTypeInfo(paramInfo.defaultValueType, 0);

		if(defaultValTypeInfo.type == ParsedType::Struct && paramInfo.defaultValue.empty())
			return defaultValTypeInfo.scriptName + ".Default()";
		else
			return "new " + defaultValTypeInfo.scriptName + "(" + paramInfo.defaultValue + ")";
	}
}

std::string generateCSMethodParams(const MethodInfo& methodInfo, bool forInterop)
{
	std::stringstream output;
	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const VarInfo& paramInfo = *I;

		if(!forInterop && !paramInfo.defaultValueType.empty() && !isFlagsEnum(paramInfo.flags))
		{
			// We don't generate parameters that have complex default values (as they're not supported in C#).
			// Instead the post-processor has generated different versions of this method, so we can just skip
			// such parameters
			continue;
		}

		if (I != methodInfo.paramInfos.begin())
			output << ", ";

		UserTypeInfo paramTypeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);
		std::string qualifiedType = getCSVarType(paramTypeInfo.scriptName, paramTypeInfo.type, paramInfo.flags, true, true, forInterop);

		bool isLastParam = (I + 1) == methodInfo.paramInfos.end();
		if (isVarParam(paramInfo.flags) && isLastParam)
			output << "params ";

		output << qualifiedType << " " << paramInfo.name;

		if (!forInterop && !paramInfo.defaultValue.empty())
			output << " = " << generateCSDefaultValueAssignment(paramInfo);
	}

	return output.str();
}

std::string generateCSMethodArgs(const MethodInfo& methodInfo, bool forInterop)
{
	std::stringstream output;
	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const VarInfo& paramInfo = *I;
		UserTypeInfo paramTypeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);

		if (isOutput(paramInfo.flags))
			output << "out ";
		else if (forInterop && isPlainStruct(paramTypeInfo.type, paramInfo.flags))
			output << "ref ";

		output << paramInfo.name;

		if ((I + 1) != methodInfo.paramInfos.end())
			output << ", ";
	}

	return output.str();
}

std::string generateCSMethodDefaultParamAssignments(const MethodInfo& methodInfo, const std::string& indent)
{
	std::stringstream output;
	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const VarInfo& paramInfo = *I;
		
		if (paramInfo.defaultValueType.empty() || isFlagsEnum(paramInfo.flags))
			continue;

		if (paramInfo.defaultValueType == "null" || paramInfo.defaultValue == "null")
		{
			UserTypeInfo paramTypeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);
			output << indent << paramTypeInfo.scriptName << " " << paramInfo.name << " = " << paramInfo.defaultValue << ";\n";
		}
		else
		{
			UserTypeInfo defaultValTypeInfo = getTypeInfo(paramInfo.defaultValueType, 0);
			output << indent << defaultValTypeInfo.scriptName << " " << paramInfo.name << " = ";
			output << "new " << defaultValTypeInfo.scriptName << "(" << paramInfo.defaultValue << ");\n";
		}
	}

	return output.str();
	
}

std::string generateCSEventSignature(const MethodInfo& methodInfo)
{
	std::stringstream output;
	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		const VarInfo& paramInfo = *I;
		UserTypeInfo paramTypeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);
		std::string type = getCSVarType(paramTypeInfo.scriptName, paramTypeInfo.type, paramInfo.flags, false, true, false);

		output << type;

		if ((I + 1) != methodInfo.paramInfos.end())
			output << ", ";
	}

	return output.str();
}

std::string generateCSEventArgs(const MethodInfo& methodInfo)
{
	std::stringstream output;

	for (auto I = methodInfo.paramInfos.begin(); I != methodInfo.paramInfos.end(); ++I)
	{
		output << I->name;

		if ((I + 1) != methodInfo.paramInfos.end())
			output << ", ";
	}

	return output.str();
}

std::string generateCSInteropMethodSignature(const MethodInfo& methodInfo, const std::string& csClassName, bool isModule)
{
	bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
	bool isCtor = (methodInfo.flags & (int)MethodFlags::Constructor) != 0;

	std::stringstream output;

	bool returnAsParameter = false;
	if (methodInfo.returnInfo.typeName.empty() || isCtor)
		output << "void";
	else
	{
		UserTypeInfo returnTypeInfo = getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
		if (!canBeReturned(returnTypeInfo.type, methodInfo.returnInfo.flags))
		{
			output << "void";
			returnAsParameter = true;
		}
		else
		{
			std::string qualifiedType = getCSVarType(returnTypeInfo.scriptName, returnTypeInfo.type,
				methodInfo.returnInfo.flags, false, true, false);
			output << qualifiedType;
		}
	}

	output << " ";

	output << "Internal_" << methodInfo.interopName << "(";

	if (isCtor)
	{
		output << csClassName << " managedInstance";

		if (methodInfo.paramInfos.size() > 0)
			output << ", ";
	}
	else if (!isStatic && !isModule)
	{
		output << "IntPtr thisPtr";

		if (methodInfo.paramInfos.size() > 0 || returnAsParameter)
			output << ", ";
	}

	output << generateCSMethodParams(methodInfo, true);

	if (returnAsParameter)
	{
		UserTypeInfo returnTypeInfo = getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags);
		std::string qualifiedType = getCSVarType(returnTypeInfo.scriptName, returnTypeInfo.type, methodInfo.returnInfo.flags, false, true, false);

		if (methodInfo.paramInfos.size() > 0)
			output << ", ";

		output << "out " << qualifiedType << " __output";
	}

	output << ")";
	return output.str();
}

std::string generateCSClass(ClassInfo& input, UserTypeInfo& typeInfo)
{
	bool isModule = (input.flags & (int)ClassFlags::IsModule) != 0;

	std::stringstream ctors;
	std::stringstream properties;
	std::stringstream events;
	std::stringstream methods;
	std::stringstream interops;

	// Private constructor for runtime use
	MethodInfo pvtCtor = findUnusedCtorSignature(input);
	ctors << "\t\tprivate " << typeInfo.scriptName << "(" << generateCSMethodParams(pvtCtor, false) << ") { }" << std::endl;

	// Parameterless constructor in case anything derives from this class
	if (!hasParameterlessConstructor(input))
		ctors << "\t\tprotected " << typeInfo.scriptName << "() { }" << std::endl;

	ctors << std::endl;

	// Constructors
	for (auto& entry : input.ctorInfos)
	{
		if (!isCSOnly(entry.flags))
		{
			// Generate interop
			interops << generateCsApiCheckBegin(entry.api);
			interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]" << std::endl;
			interops << "\t\tprivate static extern void Internal_" << entry.interopName << "(" << typeInfo.scriptName << " managedInstance";

			if (entry.paramInfos.size() > 0)
				interops << ", " << generateCSMethodParams(entry, true);

			interops << ");\n";
			interops << generateApiCheckEnd(entry.api);
		}

		bool interopOnly = (entry.flags & (int)MethodFlags::InteropOnly) != 0;
		if (interopOnly)
			continue;

		ctors << generateCsApiCheckBegin(entry.api);
		ctors << generateXMLComments(entry.documentation, "\t\t");

		if (entry.visibility == CSVisibility::Internal)
			ctors << "\t\tinternal ";
		else if (entry.visibility == CSVisibility::Private)
			ctors << "\t\tprivate ";
		else
			ctors << "\t\tpublic ";

		ctors << typeInfo.scriptName << "(" << generateCSMethodParams(entry, false) << ")" << std::endl;
		ctors << "\t\t{" << std::endl;
		ctors << generateCSMethodDefaultParamAssignments(entry, "\t\t\t");
		ctors << "\t\t\tInternal_" << entry.interopName << "(this";

		if (entry.paramInfos.size() > 0)
			ctors << ", " << generateCSMethodArgs(entry, true);

		ctors << ");" << std::endl;
		ctors << "\t\t}" << std::endl;
		ctors << generateApiCheckEnd(entry.api);
		ctors << std::endl;
	}

	// 'Ref' property & conversion operator to RRef<T>
	if(typeInfo.type == ParsedType::Resource)
	{
		interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]\n";
		interops << "\t\tprivate static extern RRef<" << typeInfo.scriptName << "> Internal_GetRef(IntPtr thisPtr);\n";

		properties << "\t\t/// <summary>Returns a reference wrapper for this resource.</summary>\n";
		properties << "\t\tpublic RRef<" << typeInfo.scriptName << "> Ref\n";
		properties << "\t\t{\n";
		properties << "\t\t\tget { return Internal_GetRef(mCachedPtr); }\n";
		properties << "\t\t}\n";
		properties << "\n";

		methods << "\t\t/// <summary>Returns a reference wrapper for this resource.</summary>\n";
		methods << "\t\tpublic static implicit operator RRef<" << typeInfo.scriptName << ">(" << typeInfo.scriptName << " x)\n";
		methods << "\t\t{\n";
		methods << "\t\t\tif(x != null)\n";
		methods << "\t\t\t\treturn Internal_GetRef(x.mCachedPtr);\n";
		methods << "\t\t\telse\n";
		methods << "\t\t\t\treturn null;\n"; 
		methods << "\t\t}\n\n";
	}

	// External constructors, methods and interop stubs
	for (auto& entry : input.methodInfos)
	{
		// Generate interop
		if (!isCSOnly(entry.flags))
		{
			interops << generateCsApiCheckBegin(entry.api);
			interops << "\t\t[MethodImpl(MethodImplOptions.InternalCall)]" << std::endl;
			interops << "\t\tprivate static extern " << generateCSInteropMethodSignature(entry, typeInfo.scriptName, isModule) << ";";
			interops << std::endl;
			interops << generateApiCheckEnd(entry.api);
		}

		bool interopOnly = (entry.flags & (int)MethodFlags::InteropOnly) != 0;
		if (interopOnly)
			continue;

		bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
		bool isStatic = (entry.flags & (int)MethodFlags::Static) != 0;

		if (isConstructor)
		{
			ctors << generateCsApiCheckBegin(entry.api);
			ctors << generateXMLComments(entry.documentation, "\t\t");

			if (entry.visibility == CSVisibility::Internal)
				ctors << "\t\tinternal ";
			else if (entry.visibility == CSVisibility::Private)
				ctors << "\t\tprivate ";
			else
				ctors << "\t\tpublic ";

			ctors << typeInfo.scriptName << "(" << generateCSMethodParams(entry, false) << ")" << std::endl;
			ctors << "\t\t{" << std::endl;
			ctors << generateCSMethodDefaultParamAssignments(entry, "\t\t\t");
			ctors << "\t\t\tInternal_" << entry.interopName << "(this";

			if (entry.paramInfos.size() > 0)
				ctors << ", " << generateCSMethodArgs(entry, true);

			ctors << ");" << std::endl;
			ctors << "\t\t}" << std::endl;
			ctors << generateApiCheckEnd(entry.api);
			ctors << std::endl;
		}
		else
		{
			bool isProperty = entry.flags & ((int)MethodFlags::PropertyGetter | (int)MethodFlags::PropertySetter);
			if (!isProperty)
			{
				UserTypeInfo returnTypeInfo;
				std::string returnType;
				if (entry.returnInfo.typeName.empty())
					returnType = "void";
				else
				{
					returnTypeInfo = getTypeInfo(entry.returnInfo.typeName, entry.returnInfo.flags);
					returnType = getCSVarType(returnTypeInfo.scriptName, returnTypeInfo.type, entry.returnInfo.flags, false, true, false);
				}

				methods << generateCsApiCheckBegin(entry.api);
				methods << generateXMLComments(entry.documentation, "\t\t");

				if (entry.visibility == CSVisibility::Internal)
					methods << "\t\tinternal ";
				else if (entry.visibility == CSVisibility::Private)
					methods << "\t\tprivate ";
				else
					methods << "\t\tpublic ";

				if (isStatic || isModule)
					methods << "static ";

				methods << returnType << " " << entry.scriptName << "(" << generateCSMethodParams(entry, false) << ")" << std::endl;
				methods << "\t\t{" << std::endl;
				methods << generateCSMethodDefaultParamAssignments(entry, "\t\t\t");

				bool returnByParam = false;
				if (!entry.returnInfo.typeName.empty())
				{
					if (!canBeReturned(returnTypeInfo.type, entry.returnInfo.flags))
					{
						methods << "\t\t\t" << returnType << " temp;" << std::endl;
						methods << "\t\t\tInternal_" << entry.interopName << "(";
						returnByParam = true;
					}
					else
						methods << "\t\t\treturn Internal_" << entry.interopName << "(";
				}
				else
					methods << "\t\t\tInternal_" << entry.interopName << "(";

				if (!isStatic && !isModule)
				{
					methods << "mCachedPtr";

					if (entry.paramInfos.size() > 0 || returnByParam)
						methods << ", ";
				}

				methods << generateCSMethodArgs(entry, true);

				if (returnByParam)
				{
					if (entry.paramInfos.size() > 0)
						methods << ", ";

					methods << "out temp";
				}

				methods << ");" << std::endl;

				if (returnByParam)
					methods << "\t\t\treturn temp;" << std::endl;

				methods << "\t\t}" << std::endl;
				methods << generateApiCheckEnd(entry.api);
				methods << std::endl;
			}
		}
	}

	// Properties
	for (auto& entry : input.propertyInfos)
	{
		UserTypeInfo propTypeInfo = getTypeInfo(entry.type, entry.typeFlags);
		std::string propTypeName = getCSVarType(propTypeInfo.scriptName, propTypeInfo.type, entry.typeFlags, false, true, false);

		properties << generateCsApiCheckBegin(entry.api);
		properties << generateXMLComments(entry.documentation, "\t\t");

		bool defaultVisible = entry.visibility != CSVisibility::Internal && entry.visibility != CSVisibility::Private &&
			!entry.setter.empty();
		if (defaultVisible)
		{
			if ((entry.style.flags & (int)StyleFlags::ForceHide) == 0)
				properties << "\t\t[ShowInInspector]" << std::endl;
		}
		else
		{
			if ((entry.style.flags & (int)StyleFlags::ForceShow) != 0)
				properties << "\t\t[ShowInInspector]" << std::endl;
		}

		properties << generateCSStyleAttributes(entry.style, propTypeInfo, entry.typeFlags, false);

		properties << "\t\t[NativeWrapper]\n";

		if (entry.visibility == CSVisibility::Internal)
			properties << "\t\tinternal ";
		else if (entry.visibility == CSVisibility::Private)
			properties << "\t\tprivate ";
		else
			properties << "\t\tpublic ";

		if (entry.isStatic || isModule)
			properties << "static ";

		properties << propTypeName << " " << entry.name << std::endl;
		properties << "\t\t{" << std::endl;

		if (!entry.getter.empty())
		{
			if (canBeReturned(propTypeInfo.type, entry.typeFlags))
			{
				properties << "\t\t\tget { return Internal_" << entry.getter << "(";

				if (!entry.isStatic && !isModule)
					properties << "mCachedPtr";

				properties << "); }" << std::endl;
			}
			else
			{
				properties << "\t\t\tget" << std::endl;
				properties << "\t\t\t{" << std::endl;
				properties << "\t\t\t\t" << propTypeName << " temp;" << std::endl;

				properties << "\t\t\t\tInternal_" << entry.getter << "(";

				if (!entry.isStatic && !isModule)
					properties << "mCachedPtr, ";

				properties << "out temp);" << std::endl;

				properties << "\t\t\t\treturn temp;" << std::endl;
				properties << "\t\t\t}" << std::endl;
			}
		}

		if (!entry.setter.empty())
		{
			properties << "\t\t\tset { Internal_" << entry.setter << "(";

			if (!entry.isStatic && !isModule)
				properties << "mCachedPtr, ";

			if(isPlainStruct(propTypeInfo.type, entry.typeFlags))
				properties << "ref ";

			properties << "value); }" << std::endl;
		}

		properties << "\t\t}" << std::endl;
		properties << generateApiCheckEnd(entry.api);
		properties << std::endl;
	}

	// Events & callbacks
	for(auto& entry : input.eventInfos)
	{
		bool isStatic = (entry.flags & (int)MethodFlags::Static) != 0;
		bool isCallback = (entry.flags & (int)MethodFlags::Callback) != 0;
		bool isInternal = (entry.flags & (int)MethodFlags::InteropOnly) != 0;

		events << generateCsApiCheckBegin(entry.api);
		events << generateXMLComments(entry.documentation, "\t\t");
		events << "\t\t";

		if (!isCallback && !isInternal)
		{
			if (entry.visibility == CSVisibility::Internal)
				events << "internal ";
			else if (entry.visibility == CSVisibility::Private)
				events << "private ";
			else
				events << "public ";
		}

		if (isStatic || isModule)
			events << "static ";

		if (!isCallback && !isInternal)
		{
			events << "event Action";

			if (!entry.paramInfos.empty())
				events << "<" << generateCSEventSignature(entry) << ">";
		
			events << " " << entry.scriptName << ";\n\n";
		}
		else
		{
			events << "partial void Callback_" << entry.scriptName << "(";

			if (!entry.paramInfos.empty())
				events << generateCSMethodParams(entry, false);
		
			events << ");\n";
			events << generateApiCheckEnd(entry.api);
			events << "\n";
		}		

		// Event interop
		interops << generateCsApiCheckBegin(entry.api);

		interops << "\t\tprivate ";

		if (isStatic || isModule)
			interops << "static ";

		interops << "void Internal_" << entry.interopName << "(" << generateCSMethodParams(entry, true) << ")" << std::endl;
		interops << "\t\t{" << std::endl;
		if (!isCallback && !isInternal)
			interops << "\t\t\t" << entry.scriptName << "?.Invoke(" << generateCSEventArgs(entry) << ");\n";
		else
			interops << "\t\t\tCallback_" << entry.scriptName << "(" << generateCSEventArgs(entry) << ");\n";
		interops << "\t\t}" << std::endl;
		interops << generateApiCheckEnd(entry.api);
	}

	std::stringstream output;
	output << generateCsApiCheckBegin(input.api);

	if(!input.module.empty())
	{
		output << "\t/** @addtogroup " << input.module << "\n";
		output << "\t *  @{\n";
		output << "\t */\n";
		output << "\n";
	}

	output << generateXMLComments(input.documentation, "\t");

	// Force non-resource and non-component types to show in inspector, except explicitly hidden
	if (isClassType(typeInfo.type) || (input.flags & (int)ClassFlags::HideInInspector) == 0)
		output << "\t[ShowInInspector]\n";

	if (input.visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (input.visibility == CSVisibility::Public)
		output << "\tpublic ";
	else if (input.visibility == CSVisibility::Private)
		output << "\tprivate ";
	else
		output << "\t";

	std::string baseType;
	if (!input.baseClass.empty())
	{
		UserTypeInfo baseTypeInfo = getTypeInfo(input.baseClass, 0);
		baseType = baseTypeInfo.scriptName;
	}
	else if (typeInfo.type == ParsedType::Resource)
		baseType = "Resource";
	else if (typeInfo.type == ParsedType::Component)
		baseType = "Component";
	else if (typeInfo.type == ParsedType::GUIElement)
		baseType = "GUIElement";
	else
		baseType = "ScriptObject";

	output << "partial class " << typeInfo.scriptName << " : " << baseType;

	output << std::endl;
	output << "\t{" << std::endl;

	output << ctors.str();
	output << properties.str();
	output << events.str();
	output << methods.str();
	output << interops.str();

	output << "\t}" << std::endl;

	if(!input.module.empty())
	{
		output << "\n";
		output << "\t/** @} */\n";
	}

	output << generateApiCheckEnd(input.api);

	return output.str();
}

std::string generateCSStruct(StructInfo& input)
{
	std::stringstream output;
	output << generateCsApiCheckBegin(input.api);

	if(!input.module.empty())
	{
		output << "\t/** @addtogroup " << input.module << "\n";
		output << "\t *  @{\n";
		output << "\t */\n";
		output << "\n";
	}

	output << generateXMLComments(input.documentation, "\t");

	output << "\t[StructLayout(LayoutKind.Sequential), SerializeObject]\n";

	if (input.visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (input.visibility == CSVisibility::Public)
		output << "\tpublic ";
	else if (input.visibility == CSVisibility::Private)
		output << "\tprivate ";
	else
		output << "\t";

	std::string scriptName = cppToCsTypeMap[input.name].scriptName;
	output << "partial struct " << scriptName;

	output << std::endl;
	output << "\t{" << std::endl;

	for (auto& entry : input.ctors)
	{
		bool isParameterless = entry.params.size() == 0;
		if (isParameterless) // Parameterless constructors not supported on C# structs
		{
			output << "\t\t/// <summary>Initializes the struct with default values.</summary>" << std::endl;
			output << "\t\tpublic static " << scriptName << " Default(";
		}
		else
		{
			output << generateXMLComments(entry.documentation, "\t\t");
			output << "\t\tpublic " << scriptName << "(";
		}

		for (auto I = entry.params.begin(); I != entry.params.end(); ++I)
		{
			const VarInfo& paramInfo = *I;

			UserTypeInfo typeInfo = getTypeInfo(paramInfo.typeName, paramInfo.flags);

			if (!isValidStructType(typeInfo, paramInfo.flags))
			{
				// We report the error during field generation, as it checks for the same condition
				continue;
			}

			
			if(!paramInfo.defaultValueType.empty() && !isFlagsEnum(paramInfo.flags))
			{
				// We don't generate parameters that have complex default values (as they're not supported in C#).
				// Instead the post-processor has generated different versions of this method, so we can just skip
				// such parameters
				continue;
			}

			output << typeInfo.scriptName << " " << paramInfo.name;

			if (!paramInfo.defaultValue.empty())
				output << " = " << generateCSDefaultValueAssignment(paramInfo);

			if ((I + 1) != entry.params.end())
				output << ", ";
		}

		output << ")" << std::endl;
		output << "\t\t{" << std::endl;

		std::string thisPtr;
		if (isParameterless)
		{
			output << "\t\t\t" << scriptName << " value = new " << scriptName << "();" << std::endl;
			thisPtr = "value";
		}
		else
			thisPtr = "this";

		for (auto I = input.fields.begin(); I != input.fields.end(); ++I)
		{
			const VarInfo& fieldInfo = *I;

			UserTypeInfo typeInfo = getTypeInfo(fieldInfo.typeName, fieldInfo.flags);

			if (!isValidStructType(typeInfo, fieldInfo.flags))
			{
				// We report the error during field generation, as it checks for the same condition
				continue;
			}

			std::string fieldName = fieldInfo.name;
			
			auto iterFind = entry.fieldAssignments.find(fieldInfo.name);
			if (iterFind != entry.fieldAssignments.end())
			{
				std::string paramName = iterFind->second;
				output << "\t\t\t" << thisPtr << "." << fieldName << " = " << paramName << ";" << std::endl;
			}
			else
			{
				std::string defaultValue;
				if (!fieldInfo.defaultValue.empty())
					defaultValue = generateCSDefaultValueAssignment(fieldInfo);
				else
					defaultValue = getDefaultValue(fieldInfo.typeName, fieldInfo.flags, typeInfo);

				output << "\t\t\t" << thisPtr << "." << fieldName << " = " << defaultValue << ";" << std::endl;
			}
		}

		if (isParameterless)
		{
			output << std::endl;
			output << "\t\t\treturn value;" << std::endl;
		}

		output << "\t\t}" << std::endl;
		output << std::endl;
	}

	if(!input.baseClass.empty())
	{
		UserTypeInfo baseTypeInfo = getTypeInfo(input.baseClass, 0);
		StructInfo* baseStructInfo = findStructInfo(input.baseClass);
		if (baseStructInfo != nullptr)
		{
			// GetBase()
			output << "\t\t///<summary>\n";
			output << "\t\t/// Returns a subset of this struct. This subset usually contains common fields shared with another struct.\n";
			output << "\t\t///</summary>\n";
			output << "\t\tpublic " << baseTypeInfo.scriptName << " GetBase()\n";
			output << "\t\t{\n";
			output << "\t\t\t" << baseTypeInfo.scriptName << " value;\n";

			for (auto I = baseStructInfo->fields.begin(); I != baseStructInfo->fields.end(); ++I)
			{
				const FieldInfo& fieldInfo = *I;
				output << "\t\t\tvalue." << fieldInfo.name << " = " << fieldInfo.name << ";\n";
			}

			output << "\t\t\treturn value;\n";
			output << "\t\t}\n";
			output << "\n";

			// SetBase()
			output << "\t\t///<summary>\n";
			output << "\t\t/// Assigns values to a subset of fields of this struct. This subset usually contains common field shared with \n";
			output << "\t\t/// another struct.\n";
			output << "\t\t///</summary>\n";
			output << "\t\tpublic void SetBase(" << baseTypeInfo.scriptName << " value)\n";
			output << "\t\t{\n";

			for (auto I = baseStructInfo->fields.begin(); I != baseStructInfo->fields.end(); ++I)
			{
				const FieldInfo& fieldInfo = *I;
				output << "\t\t\t" << fieldInfo.name << " = value." << fieldInfo.name << ";\n";
			}

			output << "\t\t}\n";
			output << "\n";
		}
	}

	for (auto I = input.fields.begin(); I != input.fields.end(); ++I)
	{
		const FieldInfo& fieldInfo = *I;

		UserTypeInfo typeInfo = getTypeInfo(fieldInfo.typeName, fieldInfo.flags);

		if (!isValidStructType(typeInfo, fieldInfo.flags))
		{
			outs() << "Error: Invalid field type found in struct \"" << scriptName << "\" for field \"" << fieldInfo.name << "\". Skipping.\n";
			continue;
		}

		output << generateXMLComments(fieldInfo.documentation, "\t\t");
		output << generateCSStyleAttributes(fieldInfo.style, typeInfo, fieldInfo.flags, true);

		if ((fieldInfo.style.flags & (int)StyleFlags::ForceHide) != 0)
			output << "\t\t[HideInInspector]" << std::endl;

		output << "\t\tpublic ";

		output << typeInfo.scriptName;
		if (isArrayOrVector(fieldInfo.flags))
			output << "[]";

		output << " ";
		output << fieldInfo.name;

		output << ";" << std::endl;
	}

	output << "\t}" << std::endl;

	if(!input.module.empty())
	{
		output << "\n";
		output << "\t/** @} */\n";
	}

	output << generateApiCheckEnd(input.api);
	return output.str();
}

std::string generateCSEnum(EnumInfo& input)
{
	std::stringstream output;
	output << generateCsApiCheckBegin(input.api);

	if(!input.module.empty())
	{
		output << "\t/** @addtogroup " << input.module << "\n";
		output << "\t *  @{\n";
		output << "\t */\n";
		output << "\n";
	}

	output << generateXMLComments(input.documentation, "\t");
	if (input.visibility == CSVisibility::Internal)
		output << "\tinternal ";
	else if (input.visibility == CSVisibility::Public)
		output << "\tpublic ";
	else if (input.visibility == CSVisibility::Private)
		output << "\tprivate ";

	output << "enum " << input.scriptName;

	if (!input.explicitType.empty())
		output << " : " << input.explicitType;

	output << std::endl;
	output << "\t{" << std::endl;

	for (auto I = input.entries.begin(); I != input.entries.end(); ++I)
	{
		if (I != input.entries.begin())
			output << ",\n";

		const EnumEntryInfo& entryInfo = I->second;

		output << generateXMLComments(entryInfo.documentation, "\t\t");
		output << "\t\t" << entryInfo.scriptName;
		output << " = ";
		output << entryInfo.value;
	}
	
	output << "\n";
	output << "\t}" << std::endl;

	if(!input.module.empty())
	{
		output << "\n";
		output << "\t/** @} */\n";
	}

	output << generateApiCheckEnd(input.api);
	return output.str();
}

std::string generateXMLParamInfo(const VarInfo& varInfo, const CommentEntry& methodDoc, const std::string& indent)
{
	std::stringstream output;
	output << indent << "<param name=\"" << escapeXML(varInfo.name) << "\" type=\"" << 
		escapeXML(getTypeInfo(varInfo.typeName, varInfo.flags).scriptName) << "\">\n";

	auto iterFind = std::find_if(methodDoc.params.begin(), methodDoc.params.end(), 
		[&varName = varInfo.name](const CommentParamEntry& entry) { return varName == entry.name; });
	if (iterFind != methodDoc.params.end() && !iterFind->comments.empty())
		output << indent << "\t<doc>" << generateXMLCommentText(iterFind->comments) << "</doc>\n";

	output << indent << "</param>\n";
	return output.str();
}

std::string generateXMLFieldInfo(const FieldInfo& fieldInfo, const std::string& indent)
{
	std::stringstream output;
	output << indent << "<field name=\"" << escapeXML(fieldInfo.name) << "\" type=\"" << 
		escapeXML(getTypeInfo(fieldInfo.typeName, fieldInfo.flags).scriptName) << "\">\n";

	// TODO - Generate inspector visibility
	if(!fieldInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << generateXMLCommentText(fieldInfo.documentation.brief) << "</doc>\n";

	output << indent << "</field>\n";
	return output.str();
}

std::string generateXMLMethodInfo(const MethodInfo& methodInfo, const std::string& indent, bool ctor)
{
	std::stringstream output;

   std::string isStaticStr = "false";
   bool isStatic = (methodInfo.flags & (int)MethodFlags::Static) != 0;
   if(!ctor && isStatic)
	   isStaticStr = "true";

	if(!ctor)
	{
		output << indent << "<method native=\"" << escapeXML(methodInfo.sourceName) << "\" script=\"" << 
			escapeXML(methodInfo.scriptName) << "\" static=\"" << isStaticStr << "\">\n";
	}
	else
		output << indent << "<ctor>\n";

	if(!methodInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << generateXMLCommentText(methodInfo.documentation.brief) << "</doc>\n";

	for(auto& param : methodInfo.paramInfos)
		output << generateXMLParamInfo(param, methodInfo.documentation, indent + "\t");

	if(!ctor && !methodInfo.returnInfo.typeName.empty())
	{
		output << indent << "\t<returns type=\"" << escapeXML(getTypeInfo(methodInfo.returnInfo.typeName, methodInfo.returnInfo.flags).scriptName) << "\">\n";

		if (!methodInfo.documentation.returns.empty())
			output << indent << "\t\t<doc>" << generateXMLCommentText(methodInfo.documentation.returns) << "</doc>\n";

		output << indent << "\t</returns>\n";
	}

	if(!ctor)
		output << indent << "</method>\n";
	else
		output << indent << "</ctor>\n";

	return output.str();
}

std::string generateXMLMethodInfo(const SimpleConstructorInfo& methodInfo, const std::string& indent)
{
	std::stringstream output;
	output << indent << "<ctor>\n";
	if(!methodInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << generateXMLCommentText(methodInfo.documentation.brief) << "</doc>\n";

	for(auto& param : methodInfo.params)
		output << generateXMLParamInfo(param, methodInfo.documentation, indent + "\t");

	output << indent << "</ctor>\n";
	return output.str();
}

std::string generateXMLPropertyInfo(const PropertyInfo& propertyInfo, const std::string& indent)
{
	std::string staticStr = propertyInfo.isStatic ? "true" : "false";

	std::stringstream output;
	output << indent << "<property name=\"" << escapeXML(propertyInfo.name) << "\" type=\"" << 
		escapeXML(getTypeInfo(propertyInfo.type, propertyInfo.typeFlags).scriptName) << 
		"\" getter=\"" << escapeXML(propertyInfo.getter) << "\" setter=\"" << escapeXML(propertyInfo.setter) << 
		"\" static=\"" << staticStr << "\">\n";

	// TODO - Generate inspector visibility
	if(!propertyInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << generateXMLCommentText(propertyInfo.documentation.brief) << "</doc>\n";

	output << indent << "</property>\n";
	return output.str();
}

std::string generateXMLEventInfo(const MethodInfo& eventInfo, const std::string& indent)
{
   bool isStatic = (eventInfo.flags & (int)MethodFlags::Static) != 0;
   std::string staticStr = isStatic ? "true" : "false";

	std::stringstream output;
	output << indent << "<event native=\"" << escapeXML(eventInfo.sourceName) << "\" script=\"" << escapeXML(eventInfo.scriptName) << 
		"\" static=\"" << staticStr << "\">\n";

	// TODO - Generate inspector visibility
	if (!eventInfo.documentation.brief.empty())
		output << indent << "\t<doc>" << generateXMLCommentText(eventInfo.documentation.brief) << "</doc>\n";

	for(auto& param : eventInfo.paramInfos)
		output << generateXMLParamInfo(param, eventInfo.documentation, indent + "\t");

	if(!eventInfo.returnInfo.typeName.empty())
	{
		output << indent << "\t<returns type=\"" << escapeXML(getTypeInfo(eventInfo.returnInfo.typeName, eventInfo.returnInfo.flags).scriptName) << "\">\n";

		if (!eventInfo.documentation.returns.empty())
			output << indent << "\t\t<doc>" << generateXMLCommentText(eventInfo.documentation.returns) << "</doc>\n";

		output << indent << "\t</returns>\n";
	}

	output << indent << "</event>\n";
	return output.str();
}

std::string generateXMLEnum(EnumInfo& input, const std::string& indent)
{
	std::stringstream output;

	output << indent << "<enum native=\"" << escapeXML(input.name) << "\" script=\"" << escapeXML(input.scriptName) << "\">\n";
	if (!input.documentation.brief.empty())
		output << indent << "\t<doc>" << generateXMLCommentText(input.documentation.brief) << "</doc>\n";
	
	for (auto I = input.entries.begin(); I != input.entries.end(); ++I)
	{
		const EnumEntryInfo& entryInfo = I->second;

	   output << indent << "\t<enumentry native=\"" << escapeXML(entryInfo.name) << "\" script=\"" << escapeXML(entryInfo.scriptName) << "\">\n";
	   if (!entryInfo.documentation.brief.empty())
		   output << indent << "\t\t<doc>" << generateXMLCommentText(entryInfo.documentation.brief) << "</doc>\n";
	   output << indent << "\t</enumentry>\n";
	}
	
	output << indent << "</enum>\n";
	return output.str();
}

std::string generateXMLStruct(StructInfo& input, const std::string& indent)
{
	std::stringstream output;

	UserTypeInfo& typeInfo = cppToCsTypeMap[input.name];

	output << indent << "<struct native=\"" << escapeXML(input.name) << "\" script=\"" << escapeXML(typeInfo.scriptName) << "\">\n";
	if (!input.documentation.brief.empty())
		output << indent << "\t<doc>" << generateXMLCommentText(input.documentation.brief) << "</doc>\n";

	for (auto& entry : input.ctors)
		output << generateXMLMethodInfo(entry, indent + "\t");

	for(auto& entry : input.fields)
	  output << generateXMLFieldInfo(entry, indent + "\t");
	
	output << indent << "</struct>\n";
	return output.str();
}

std::string generateXMLClass(ClassInfo& input, bool editor, const std::string& indent)
{
	std::stringstream output;

	UserTypeInfo& typeInfo = cppToCsTypeMap[input.name];

	output << indent << "<class native=\"" << escapeXML(input.name) << "\" script=\"" << escapeXML(typeInfo.scriptName) << "\">\n";
	if (!input.documentation.brief.empty())
		output << indent << "\t<doc>" << generateXMLCommentText(input.documentation.brief) << "</doc>\n";

	for (auto& entry : input.ctorInfos)
	{
		bool interopOnly = (entry.flags & (int)MethodFlags::InteropOnly) != 0;
		if(isValidAPI(entry.api, editor) && !interopOnly)
			output << generateXMLMethodInfo(entry, indent + "\t", true);
	}

	for(auto& entry : input.methodInfos)
	{
		bool interopOnly = (entry.flags & (int)MethodFlags::InteropOnly) != 0;
		bool isConstructor = (entry.flags & (int)MethodFlags::Constructor) != 0;
		bool isProperty = entry.flags & ((int)MethodFlags::PropertyGetter | (int)MethodFlags::PropertySetter);

		if(isValidAPI(entry.api, editor) && !interopOnly && !isProperty)
			output << generateXMLMethodInfo(entry, indent + "\t", isConstructor);
	}

   for(auto& entry : input.propertyInfos)
   {
		if(isValidAPI(entry.api, editor))
			output << generateXMLPropertyInfo(entry, indent + "\t");
   }

   for(auto& entry : input.eventInfos)
   {
	   bool isCallback = (entry.flags & (int)MethodFlags::Callback) != 0;
	   bool isInternal = (entry.flags & (int)MethodFlags::InteropOnly) != 0;

	  if(!isCallback && !isInternal)
		  output << generateXMLEventInfo(entry, indent + "\t");
   }
	
	output << indent << "</class>\n";
	return output.str();
}

void cleanAndPrepareFolder(const StringRef& folder)
{
	if (sys::fs::exists(folder))
	{
		std::error_code ec;
		for (sys::fs::directory_iterator file(folder, ec), fileEnd; file != fileEnd && !ec; file.increment(ec))
			sys::fs::remove(file->path());
	}

	sys::fs::create_directories(folder);
}

std::ofstream createFile(const std::string& filename, StringRef outputFolder)
{
	std::string relativePath = "/" + filename;
	StringRef filenameRef(relativePath.data(), relativePath.size());

	SmallString<128> filepath = outputFolder;
	sys::path::append(filepath, filenameRef);

	std::ofstream output;
	output.open(filepath.str(), std::ios::out);

	return output;
}

void generateMappingXMLFile(bool editor, const std::string& outputFolder)
{
	std::stringstream body;
	for (auto& fileInfo : outputFileInfos)
	{
		auto& enumInfos = fileInfo.second.enumInfos;
		for (auto& entry : enumInfos)
		{
			if (isValidAPI(entry.api, editor))
				body << generateXMLEnum(entry, "\t");
		}

		auto& structInfos = fileInfo.second.structInfos;
		for (auto& entry : structInfos)
		{
			if (isValidAPI(entry.api, editor))
				body << generateXMLStruct(entry, "\t");
		}


		auto& classInfos = fileInfo.second.classInfos;
		for (auto& entry : classInfos)
		{
			if (isValidAPI(entry.api, editor))
				body << generateXMLClass(entry, editor, "\t");
		}
	}

	std::ofstream output = createFile("info.xml", outputFolder);

	output << "<?xml version='1.0' encoding='UTF-8' standalone='no'?>\n";
	output << "<entries>\n";
	output << body.str();
	output << "</entries>\n";
	output.close();
}

void generateLookupFile(const std::string& tableName, ParsedType type, bool editor, 
	const std::string& engineOutputFolder, const std::string& editorOutputFolder)
{
	StringRef cppOutputFolder = editor ? editorOutputFolder : engineOutputFolder;

	std::stringstream body;
	std::stringstream includes;
	for (auto& fileInfo : outputFileInfos)
	{
		auto& classInfos = fileInfo.second.classInfos;
		if (classInfos.empty())
			continue;

		if(fileInfo.second.inEditor != editor)
			continue;

		bool hasType = false;
		for (auto& classInfo : classInfos)
		{
			UserTypeInfo& typeInfo = cppToCsTypeMap[classInfo.name];
			if (typeInfo.type != type)
				continue;

			includes << generateCppApiCheckBegin(classInfo.api);
			includes << "#include \"" << getRelativeTo(typeInfo.declFile, cppOutputFolder) << "\"" << std::endl;
			includes << generateApiCheckEnd(classInfo.api);

			std::string interopClassName = getScriptInteropType(classInfo.name);
			body << generateCppApiCheckBegin(classInfo.api);
			body << "\t\tADD_ENTRY(" << classInfo.name << ", " << interopClassName << ")" << std::endl;
			body << generateApiCheckEnd(classInfo.api);

			hasType = true;
		}

		if(hasType)
			includes << "#include \"BsScript" + fileInfo.first + ".generated.h\"" << std::endl;
	}

	std::string prefix = editor ? "Editor" : "";
	std::ofstream output = createFile("Bs" + prefix + tableName + "Lookup.generated.h", cppOutputFolder);

	// License/copyright header
	output << generateFileHeader(editor);

	output << "#pragma once" << std::endl;
	output << std::endl;

	output << "#include \"Serialization/Bs" << tableName << "Lookup.h\"" << std::endl;
	output << "#include \"Reflection/BsRTTIType.h\"" << std::endl;
	output << includes.str();

	output << std::endl;

	output << "namespace " << (editor ? sEditorCppNs : sFrameworkCppNs) << std::endl;
	output << "{" << std::endl;
	output << "\tLOOKUP_BEGIN(" << prefix << tableName << ")" << std::endl;

	output << body.str();

	output << "\tLOOKUP_END" << std::endl;
	output << "}" << std::endl;

	output << "#undef LOOKUP_BEGIN" << std::endl;
	output << "#undef ADD_ENTRY" << std::endl;
	output << "#undef LOOKUP_END" << std::endl;

	output.close();
}

void generateAll(StringRef cppEngineOutputFolder, StringRef cppEditorOutputFolder, StringRef csEngineOutputFolder, 
	StringRef csEditorOutputFolder, bool genEditor)
{
	postProcessFileInfos();

	cleanAndPrepareFolder(cppEngineOutputFolder);
	cleanAndPrepareFolder(csEngineOutputFolder);

	if(genEditor)
	{
		cleanAndPrepareFolder(cppEditorOutputFolder);
		cleanAndPrepareFolder(csEditorOutputFolder);
	}

	//{
	//	std::string relativePath = "scriptBindings.timestamp";
	//	StringRef filenameRef(relativePath.data(), relativePath.size());

	//	SmallString<128> filepath = cppOutputFolder;
	//	sys::path::append(filepath, filenameRef);

	//	std::ofstream output;
	//	output.open(filepath.str(), std::ios::out);

	//	std::chrono::milliseconds ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	//		std::chrono::system_clock::now().time_since_epoch());
	//	output << std::to_string(ms.count());
	//	output.close();
	//}

	// Generate H
	for (auto& fileInfo : outputFileInfos)
	{
		if(fileInfo.second.inEditor && !genEditor)
			continue;

		std::stringstream body;

		auto& classInfos = fileInfo.second.classInfos;
		auto& structInfos = fileInfo.second.structInfos;

		if (classInfos.empty() && structInfos.empty())
			continue;

		for (auto I = classInfos.begin(); I != classInfos.end(); ++I)
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

		StringRef cppOutputFolder = fileInfo.second.inEditor ? cppEditorOutputFolder : cppEngineOutputFolder;
		std::ofstream output = createFile("BsScript" + fileInfo.first + ".generated.h", cppOutputFolder);

		// License/copyright header
		output << generateFileHeader(fileInfo.second.inEditor);

		output << "#pragma once" << std::endl;
		output << std::endl;

		// Output includes
		for (auto& include : fileInfo.second.referencedHeaderIncludes)
			output << "#include \"" << getRelativeTo(include, cppOutputFolder) << "\"" << std::endl;

		output << std::endl;

		// Output forward declarations
		for (auto& decl : fileInfo.second.forwardDeclarations)
		{
			for (auto& nsEntry : decl.ns)
				output << "namespace " << nsEntry << " { ";
			
			if (decl.templParams.size() > 0)
			{
				output << "template<";

				for (int i = 0; i < (int)decl.templParams.size(); ++i)
				{
					if (i != 0)
						output << ", ";

					output << decl.templParams[i].type << " T" << std::to_string(i);
				}

				output << "> ";
			}

			if (decl.isStruct)
				output << "struct " << decl.name << ";";
			else
				output << "class " << decl.name << ";";

			for (auto& nsEntry : decl.ns)
				output << " }";
			
			output << "\n";
		}

		output << "namespace " << (fileInfo.second.inEditor ? sEditorCppNs : sFrameworkCppNs) << std::endl;
		output << "{" << std::endl;
		output << body.str();
		output << "}" << std::endl;

		output.close();
	}

	// Generate CPP
	for (auto& fileInfo : outputFileInfos)
	{
		if(fileInfo.second.inEditor && !genEditor)
			continue;

		std::stringstream body;

		auto& classInfos = fileInfo.second.classInfos;
		auto& structInfos = fileInfo.second.structInfos;

		if (classInfos.empty() && structInfos.empty())
			continue;

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

		StringRef cppOutputFolder = fileInfo.second.inEditor ? cppEditorOutputFolder : cppEngineOutputFolder;
		std::ofstream output = createFile("BsScript" + fileInfo.first + ".generated.cpp", cppOutputFolder);

		// License/copyright header
		output << generateFileHeader(fileInfo.second.inEditor);

		// Output includes
		for (auto& include : fileInfo.second.referencedSourceIncludes)
			output << "#include \"" << getRelativeTo(include, cppOutputFolder) << "\"" << std::endl;

		output << std::endl;

		output << "namespace " << (fileInfo.second.inEditor ? sEditorCppNs : sFrameworkCppNs) << std::endl;
		output << "{" << std::endl;
		output << body.str();
		output << "}" << std::endl;

		output.close();
	}

	// Generate CS
	for (auto& fileInfo : outputFileInfos)
	{
		if(fileInfo.second.inEditor && !genEditor)
			continue;

		std::stringstream body;

		auto& classInfos = fileInfo.second.classInfos;
		auto& structInfos = fileInfo.second.structInfos;
		auto& enumInfos = fileInfo.second.enumInfos;

		if (classInfos.empty() && structInfos.empty() && enumInfos.empty())
			continue;

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
			body << generateCSEnum(*I);

			if ((I + 1) != enumInfos.end())
				body << std::endl;
		}

		StringRef csOutputFolder = fileInfo.second.inEditor ? csEditorOutputFolder : csEngineOutputFolder;
		std::ofstream output = createFile(fileInfo.first + ".generated.cs", csOutputFolder);

		// License/copyright header
		output << generateFileHeader(fileInfo.second.inEditor);

		output << "using System;" << std::endl;
		output << "using System.Runtime.CompilerServices;" << std::endl;
		output << "using System.Runtime.InteropServices;" << std::endl;

		if (fileInfo.second.inEditor)
			output << "using " << sFrameworkCsNs << ";" << std::endl;

		output << std::endl;

		if (!fileInfo.second.inEditor)
			output << "namespace " << sFrameworkCsNs << "\n";
		else
			output << "namespace " << sEditorCsNs << "\n";

		output << "{" << std::endl;
		output << body.str();
		output << "}" << std::endl;

		output.close();
	}

	// Generate builtin component lookup file
	generateLookupFile("BuiltinComponent", ParsedType::Component, false, cppEngineOutputFolder, cppEditorOutputFolder);

	// Generate C++ reflectable type lookup files
	generateLookupFile("BuiltinReflectableTypes", ParsedType::ReflectableClass, false, cppEngineOutputFolder, cppEditorOutputFolder);
	generateLookupFile("BuiltinReflectableTypes", ParsedType::ReflectableClass, true, cppEngineOutputFolder, cppEditorOutputFolder);

	// Generate XML lookup
	generateMappingXMLFile(false, csEngineOutputFolder);

	if(genEditor)
		generateMappingXMLFile(true, csEditorOutputFolder);
}
