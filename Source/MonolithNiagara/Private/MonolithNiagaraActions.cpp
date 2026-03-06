#include "MonolithNiagaraActions.h"
#include "MonolithAssetUtils.h"
#include "MonolithJsonUtils.h"

#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScript.h"
#include "NiagaraScriptSource.h"
#include "NiagaraScriptSourceBase.h"
#include "NiagaraGraph.h"
#include "NiagaraNodeOutput.h"
#include "NiagaraNodeFunctionCall.h"
#include "NiagaraNodeCustomHlsl.h"
#include "NiagaraNodeInput.h"
#include "NiagaraDataInterface.h"
#include "NiagaraConstants.h"
#include "NiagaraTypes.h"
#include "NiagaraParameterStore.h"
#include "NiagaraRendererProperties.h"
#include "NiagaraSpriteRendererProperties.h"
#include "NiagaraMeshRendererProperties.h"
#include "NiagaraRibbonRendererProperties.h"
#include "NiagaraLightRendererProperties.h"
#include "NiagaraComponentRendererProperties.h"
#include "NiagaraEditorModule.h"
#include "NiagaraCommon.h"
#include "EdGraphSchema_Niagara.h"
#include "ViewModels/Stack/NiagaraStackGraphUtilities.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Materials/MaterialInterface.h"
#include "Editor.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogMonolithNiagara, Log, All);

// ============================================================================
// Workarounds for non-exported NiagaraEditor functions
// These functions exist in NiagaraStackGraphUtilities but lack NIAGARAEDITOR_API
// ============================================================================

namespace MonolithNiagaraHelpers
{
	// Reimplementation of GetOrderedModuleNodes — walks the output node's input chain
	void GetOrderedModuleNodes(UNiagaraNodeOutput& OutputNode, TArray<UNiagaraNodeFunctionCall*>& OutModuleNodes)
	{
		OutModuleNodes.Reset();
		UEdGraphPin* InputPin = nullptr;
		for (UEdGraphPin* Pin : OutputNode.Pins)
		{
			if (Pin->Direction == EGPD_Input)
			{
				InputPin = Pin;
				break;
			}
		}
		if (!InputPin) return;

		// Walk backward through the chain
		TArray<UNiagaraNodeFunctionCall*> Reversed;
		UEdGraphPin* Current = InputPin;
		while (Current && Current->LinkedTo.Num() > 0)
		{
			UEdGraphNode* LinkedNode = Current->LinkedTo[0]->GetOwningNode();
			if (UNiagaraNodeFunctionCall* FuncNode = Cast<UNiagaraNodeFunctionCall>(LinkedNode))
			{
				Reversed.Add(FuncNode);
				// Find the input pin of this function call node
				Current = nullptr;
				for (UEdGraphPin* P : FuncNode->Pins)
				{
					if (P->Direction == EGPD_Input && P->PinType.PinCategory == UEdGraphSchema_Niagara::PinCategoryMisc)
					{
						Current = P;
						break;
					}
				}
				// Fallback: find any input pin with a link
				if (!Current)
				{
					for (UEdGraphPin* P : FuncNode->Pins)
					{
						if (P->Direction == EGPD_Input && P->LinkedTo.Num() > 0)
						{
							Current = P;
							break;
						}
					}
				}
			}
			else
			{
				break;
			}
		}
		// Reverse to get proper order
		for (int32 i = Reversed.Num() - 1; i >= 0; --i)
		{
			OutModuleNodes.Add(Reversed[i]);
		}
	}

	// Reimplementation of GetStackFunctionInputOverridePin (read-only)
	UEdGraphPin* GetStackFunctionInputOverridePin(UNiagaraNodeFunctionCall& Node, const FNiagaraParameterHandle& AliasedHandle)
	{
		FString HandleStr = AliasedHandle.GetParameterHandleString().ToString();
		for (UEdGraphPin* Pin : Node.Pins)
		{
			if (Pin->Direction == EGPD_Input && Pin->PinName.ToString() == HandleStr)
			{
				return Pin;
			}
		}
		return nullptr;
	}

	// Check if a module is enabled via its metadata
	TOptional<bool> GetModuleIsEnabled(UNiagaraNodeFunctionCall& Node)
	{
		return Node.IsNodeEnabled() ? TOptional<bool>(true) : TOptional<bool>(false);
	}

	// RemoveModuleFromStack — disconnect and destroy the node
	bool RemoveModuleFromStack(UNiagaraSystem& System, FGuid EmitterGuid, UNiagaraNodeFunctionCall& ModuleNode)
	{
		UEdGraph* Graph = ModuleNode.GetGraph();
		if (!Graph) return false;
		// Break all connections
		ModuleNode.BreakAllNodeLinks();
		Graph->RemoveNode(&ModuleNode);
		return true;
	}

	// GetParametersForContext — simplified version that collects known parameters
	void GetParametersForContext(UEdGraph* Graph, UNiagaraSystem& System, TSet<FNiagaraVariableBase>& OutParams)
	{
		// Collect from user store
		FNiagaraUserRedirectionParameterStore& US = System.GetExposedParameters();
		TArrayView<const FNiagaraVariableWithOffset> Vars = US.ReadParameterVariables();
		for (const FNiagaraVariableWithOffset& V : Vars)
		{
			OutParams.Add(V);
		}
	}
	// GetStackFunctionInputs — simplified: enumerate input pins of the function call
	void GetStackFunctionInputs(const UNiagaraNodeFunctionCall& Node, TArray<FNiagaraVariable>& OutInputs)
	{
		OutInputs.Reset();
		for (const UEdGraphPin* Pin : Node.Pins)
		{
			if (Pin->Direction == EGPD_Input && !Pin->bHidden)
			{
				// Attempt to reconstruct the variable from pin info
				FNiagaraVariable Var;
				Var.SetName(Pin->PinName);
				// The pin type encodes the Niagara type — we'll use a best-effort approach
				OutInputs.Add(Var);
			}
		}
	}
} // namespace MonolithNiagaraHelpers

// Helper: wrap a string result in a FJsonObject for FMonolithActionResult::Success
static FMonolithActionResult SuccessStr(const FString& Msg)
{
	TSharedPtr<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetStringField(TEXT("result"), Msg);
	return FMonolithActionResult::Success(R);
}

// Helper: wrap a pre-built JSON object for Success
static FMonolithActionResult SuccessObj(const TSharedRef<FJsonObject>& Obj)
{
	return FMonolithActionResult::Success(Obj);
}

// ============================================================================
// JSON Helpers
// ============================================================================

FString FMonolithNiagaraActions::JsonObjectToString(const TSharedRef<FJsonObject>& JsonObj)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(JsonObj, W);
	return Out;
}

FString FMonolithNiagaraActions::JsonArrayToString(const TArray<TSharedPtr<FJsonValue>>& JsonArray)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(JsonArray, W);
	return Out;
}

FString FMonolithNiagaraActions::JsonValueToString(const TSharedPtr<FJsonValue>& Value)
{
	if (!Value.IsValid()) return FString();
	if (Value->Type == EJson::String) return FString::Printf(TEXT("\"%s\""), *Value->AsString());
	if (Value->Type == EJson::Number) return FString::SanitizeFloat(Value->AsNumber());
	if (Value->Type == EJson::Boolean) return Value->AsBool() ? TEXT("true") : TEXT("false");
	if (Value->Type == EJson::Object)
	{
		FString R;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&R);
		FJsonSerializer::Serialize(Value->AsObject().ToSharedRef(), W);
		return R;
	}
	if (Value->Type == EJson::Array)
	{
		FString R;
		TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&R);
		FJsonSerializer::Serialize(Value->AsArray(), W);
		return R;
	}
	return FString();
}

// ============================================================================
// Core Helpers
// ============================================================================

UNiagaraSystem* FMonolithNiagaraActions::LoadSystem(const FString& SystemPath)
{
	UNiagaraSystem* System = FMonolithAssetUtils::LoadAssetByPath<UNiagaraSystem>(SystemPath);
	if (!System)
	{
		UE_LOG(LogMonolithNiagara, Error, TEXT("Failed to load Niagara system: %s"), *SystemPath);
	}
	return System;
}

int32 FMonolithNiagaraActions::FindEmitterHandleIndex(UNiagaraSystem* System, const FString& HandleIdOrName)
{
	if (!System) return INDEX_NONE;
	const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	FGuid TestGuid;
	if (FGuid::Parse(HandleIdOrName, TestGuid))
	{
		for (int32 i = 0; i < Handles.Num(); ++i)
		{
			if (Handles[i].GetId() == TestGuid) return i;
		}
	}
	FName TestName(*HandleIdOrName);
	for (int32 i = 0; i < Handles.Num(); ++i)
	{
		if (Handles[i].GetName() == TestName) return i;
	}
	return INDEX_NONE;
}

ENiagaraScriptUsage FMonolithNiagaraActions::ResolveScriptUsage(const FString& UsageString)
{
	FString L = UsageString.ToLower();
	if (L == TEXT("system_spawn") || L == TEXT("systemspawn")) return ENiagaraScriptUsage::SystemSpawnScript;
	if (L == TEXT("system_update") || L == TEXT("systemupdate")) return ENiagaraScriptUsage::SystemUpdateScript;
	if (L == TEXT("emitter_spawn") || L == TEXT("emitterspawn")) return ENiagaraScriptUsage::EmitterSpawnScript;
	if (L == TEXT("emitter_update") || L == TEXT("emitterupdate")) return ENiagaraScriptUsage::EmitterUpdateScript;
	if (L == TEXT("particle_spawn") || L == TEXT("particlespawn")) return ENiagaraScriptUsage::ParticleSpawnScript;
	if (L == TEXT("particle_update") || L == TEXT("particleupdate")) return ENiagaraScriptUsage::ParticleUpdateScript;
	return ENiagaraScriptUsage::ParticleUpdateScript;
}

UNiagaraGraph* FMonolithNiagaraActions::GetGraphForUsage(UNiagaraSystem* System, const FString& EmitterHandleId, ENiagaraScriptUsage Usage)
{
	if (!System) return nullptr;
	UNiagaraScript* Script = nullptr;
	if (Usage == ENiagaraScriptUsage::SystemSpawnScript || Usage == ENiagaraScriptUsage::SystemUpdateScript)
	{
		Script = System->GetSystemSpawnScript();
		if (!Script && Usage == ENiagaraScriptUsage::SystemUpdateScript)
			Script = System->GetSystemUpdateScript();
	}
	else
	{
		int32 Idx = FindEmitterHandleIndex(System, EmitterHandleId);
		if (Idx == INDEX_NONE) return nullptr;
		FVersionedNiagaraEmitterData* ED = System->GetEmitterHandles()[Idx].GetEmitterData();
		if (!ED) return nullptr;
		Script = ED->GetScript(Usage, FGuid());
	}
	if (!Script) return nullptr;
	UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
	return Src ? Src->NodeGraph : nullptr;
}

UNiagaraNodeOutput* FMonolithNiagaraActions::FindOutputNode(UNiagaraSystem* System, const FString& EmitterHandleId, ENiagaraScriptUsage Usage)
{
	UNiagaraGraph* Graph = GetGraphForUsage(System, EmitterHandleId, Usage);
	if (!Graph) return nullptr;
	return Graph->FindEquivalentOutputNode(Usage, FGuid());
}

UNiagaraNodeFunctionCall* FMonolithNiagaraActions::FindModuleNode(UNiagaraSystem* System, const FString& EmitterHandleId,
	const FString& NodeGuidStr, ENiagaraScriptUsage* OutUsage)
{
	FGuid TargetGuid;
	bool bHasGuid = FGuid::Parse(NodeGuidStr, TargetGuid);

	static const ENiagaraScriptUsage AllUsages[] = {
		ENiagaraScriptUsage::SystemSpawnScript, ENiagaraScriptUsage::SystemUpdateScript,
		ENiagaraScriptUsage::EmitterSpawnScript, ENiagaraScriptUsage::EmitterUpdateScript,
		ENiagaraScriptUsage::ParticleSpawnScript, ENiagaraScriptUsage::ParticleUpdateScript,
	};

	for (ENiagaraScriptUsage Usage : AllUsages)
	{
		UNiagaraNodeOutput* Out = FindOutputNode(System, EmitterHandleId, Usage);
		if (!Out) continue;
		TArray<UNiagaraNodeFunctionCall*> Mods;
		MonolithNiagaraHelpers::GetOrderedModuleNodes(*Out, Mods);
		for (UNiagaraNodeFunctionCall* N : Mods)
		{
			if (!N) continue;
			if ((bHasGuid && N->NodeGuid == TargetGuid) || N->GetFunctionName() == NodeGuidStr)
			{
				if (OutUsage) *OutUsage = Usage;
				return N;
			}
		}
	}
	return nullptr;
}

UClass* FMonolithNiagaraActions::ResolveRendererClass(const FString& RendererClass)
{
	FString L = RendererClass.ToLower();
	if (L == TEXT("sprite") || L == TEXT("spriterenderer")) return UNiagaraSpriteRendererProperties::StaticClass();
	if (L == TEXT("mesh") || L == TEXT("meshrenderer")) return UNiagaraMeshRendererProperties::StaticClass();
	if (L == TEXT("ribbon") || L == TEXT("ribbonrenderer")) return UNiagaraRibbonRendererProperties::StaticClass();
	if (L == TEXT("light") || L == TEXT("lightrenderer")) return UNiagaraLightRendererProperties::StaticClass();
	if (L == TEXT("component") || L == TEXT("componentrenderer")) return UNiagaraComponentRendererProperties::StaticClass();

	FString Full = RendererClass;
	if (!Full.StartsWith(TEXT("UNiagara"))) Full = TEXT("UNiagara") + Full;
	if (!Full.EndsWith(TEXT("RendererProperties"))) Full += TEXT("RendererProperties");
	UClass* C = FindFirstObject<UClass>(*Full, EFindFirstObjectOptions::NativeFirst);
	if (!C) C = FindFirstObject<UClass>(*Full.Mid(1), EFindFirstObjectOptions::NativeFirst);
	return C;
}

UNiagaraRendererProperties* FMonolithNiagaraActions::GetRenderer(UNiagaraSystem* System, const FString& EmitterHandleId,
	int32 RendererIndex, FVersionedNiagaraEmitterData** OutEmitterData)
{
	if (!System) return nullptr;
	int32 Idx = FindEmitterHandleIndex(System, EmitterHandleId);
	if (Idx == INDEX_NONE) return nullptr;
	FVersionedNiagaraEmitterData* ED = System->GetEmitterHandles()[Idx].GetEmitterData();
	if (!ED) return nullptr;
	if (OutEmitterData) *OutEmitterData = ED;
	const TArray<UNiagaraRendererProperties*>& R = ED->GetRenderers();
	return R.IsValidIndex(RendererIndex) ? R[RendererIndex] : nullptr;
}

FNiagaraTypeDefinition FMonolithNiagaraActions::ResolveNiagaraType(const FString& TypeName)
{
	FString L = TypeName.ToLower();
	if (L == TEXT("float")) return FNiagaraTypeDefinition::GetFloatDef();
	if (L == TEXT("int") || L == TEXT("int32")) return FNiagaraTypeDefinition::GetIntDef();
	if (L == TEXT("bool")) return FNiagaraTypeDefinition::GetBoolDef();
	if (L == TEXT("vec2") || L == TEXT("vector2d") || L == TEXT("vector2")) return FNiagaraTypeDefinition::GetVec2Def();
	if (L == TEXT("vec3") || L == TEXT("vector") || L == TEXT("vector3")) return FNiagaraTypeDefinition::GetVec3Def();
	if (L == TEXT("vec4") || L == TEXT("vector4")) return FNiagaraTypeDefinition::GetVec4Def();
	if (L == TEXT("color") || L == TEXT("linearcolor")) return FNiagaraTypeDefinition::GetColorDef();
	if (L == TEXT("position")) return FNiagaraTypeDefinition::GetPositionDef();
	if (L == TEXT("quat") || L == TEXT("quaternion")) return FNiagaraTypeDefinition::GetQuatDef();
	if (L == TEXT("matrix") || L == TEXT("matrix4")) return FNiagaraTypeDefinition::GetMatrix4Def();
	return FNiagaraTypeDefinition::GetFloatDef();
}

FString FMonolithNiagaraActions::SerializeParameterValue(const FNiagaraVariable& Variable, const FNiagaraParameterStore& Store)
{
	const FNiagaraTypeDefinition& T = Variable.GetType();
	if (T == FNiagaraTypeDefinition::GetFloatDef()) return FString::SanitizeFloat(Store.GetParameterValue<float>(Variable));
	if (T == FNiagaraTypeDefinition::GetIntDef()) return FString::FromInt(Store.GetParameterValue<int32>(Variable));
	if (T == FNiagaraTypeDefinition::GetBoolDef())
	{
		FNiagaraBool V = Store.GetParameterValue<FNiagaraBool>(Variable);
		return V.IsValid() && V.GetValue() ? TEXT("true") : TEXT("false");
	}
	if (T == FNiagaraTypeDefinition::GetVec2Def())
	{
		FVector2f V = Store.GetParameterValue<FVector2f>(Variable);
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), V.X); O->SetNumberField(TEXT("y"), V.Y);
		return JsonObjectToString(O);
	}
	if (T == FNiagaraTypeDefinition::GetVec3Def() || T == FNiagaraTypeDefinition::GetPositionDef())
	{
		FVector3f V = Store.GetParameterValue<FVector3f>(Variable);
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), V.X); O->SetNumberField(TEXT("y"), V.Y); O->SetNumberField(TEXT("z"), V.Z);
		return JsonObjectToString(O);
	}
	if (T == FNiagaraTypeDefinition::GetVec4Def() || T == FNiagaraTypeDefinition::GetQuatDef())
	{
		FVector4f V = Store.GetParameterValue<FVector4f>(Variable);
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("x"), V.X); O->SetNumberField(TEXT("y"), V.Y);
		O->SetNumberField(TEXT("z"), V.Z); O->SetNumberField(TEXT("w"), V.W);
		return JsonObjectToString(O);
	}
	if (T == FNiagaraTypeDefinition::GetColorDef())
	{
		FLinearColor V = Store.GetParameterValue<FLinearColor>(Variable);
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetNumberField(TEXT("r"), V.R); O->SetNumberField(TEXT("g"), V.G);
		O->SetNumberField(TEXT("b"), V.B); O->SetNumberField(TEXT("a"), V.A);
		return JsonObjectToString(O);
	}
	return TEXT("\"<unsupported>\"");
}

FNiagaraVariable FMonolithNiagaraActions::MakeUserVariable(const FString& ParamName, const FNiagaraTypeDefinition& TypeDef)
{
	FString Full = ParamName;
	if (!Full.StartsWith(TEXT("User."))) Full = TEXT("User.") + Full;
	return FNiagaraVariable(TypeDef, FName(*Full));
}

// Helper: set typed value on parameter store from JSON
static bool SetTypedParameterValue(FNiagaraUserRedirectionParameterStore& Store, const FNiagaraVariable& Var,
	const FNiagaraTypeDefinition& TypeDef, const TSharedPtr<FJsonValue>& JsonValue)
{
	if (!JsonValue.IsValid()) return false;
	if (TypeDef == FNiagaraTypeDefinition::GetFloatDef())
	{
		Store.SetParameterValue<float>(static_cast<float>(JsonValue->AsNumber()), Var, true);
		return true;
	}
	if (TypeDef == FNiagaraTypeDefinition::GetIntDef())
	{
		Store.SetParameterValue<int32>(static_cast<int32>(JsonValue->AsNumber()), Var, true);
		return true;
	}
	if (TypeDef == FNiagaraTypeDefinition::GetBoolDef())
	{
		FNiagaraBool V; V.SetValue(JsonValue->AsBool());
		Store.SetParameterValue<FNiagaraBool>(V, Var, true);
		return true;
	}
	if (TypeDef == FNiagaraTypeDefinition::GetVec2Def())
	{
		TSharedPtr<FJsonObject> O = JsonValue->AsObject();
		if (!O) return false;
		FVector2f V(static_cast<float>(O->GetNumberField(TEXT("x"))), static_cast<float>(O->GetNumberField(TEXT("y"))));
		Store.SetParameterValue<FVector2f>(V, Var, true);
		return true;
	}
	if (TypeDef == FNiagaraTypeDefinition::GetVec3Def() || TypeDef == FNiagaraTypeDefinition::GetPositionDef())
	{
		TSharedPtr<FJsonObject> O = JsonValue->AsObject();
		if (!O) return false;
		FVector3f V(static_cast<float>(O->GetNumberField(TEXT("x"))), static_cast<float>(O->GetNumberField(TEXT("y"))),
			static_cast<float>(O->GetNumberField(TEXT("z"))));
		Store.SetParameterValue<FVector3f>(V, Var, true);
		return true;
	}
	if (TypeDef == FNiagaraTypeDefinition::GetVec4Def() || TypeDef == FNiagaraTypeDefinition::GetQuatDef())
	{
		TSharedPtr<FJsonObject> O = JsonValue->AsObject();
		if (!O) return false;
		FVector4f V(static_cast<float>(O->GetNumberField(TEXT("x"))), static_cast<float>(O->GetNumberField(TEXT("y"))),
			static_cast<float>(O->GetNumberField(TEXT("z"))), static_cast<float>(O->GetNumberField(TEXT("w"))));
		Store.SetParameterValue<FVector4f>(V, Var, true);
		return true;
	}
	if (TypeDef == FNiagaraTypeDefinition::GetColorDef())
	{
		TSharedPtr<FJsonObject> O = JsonValue->AsObject();
		if (!O) return false;
		FLinearColor V(static_cast<float>(O->GetNumberField(TEXT("r"))), static_cast<float>(O->GetNumberField(TEXT("g"))),
			static_cast<float>(O->GetNumberField(TEXT("b"))),
			O->HasField(TEXT("a")) ? static_cast<float>(O->GetNumberField(TEXT("a"))) : 1.0f);
		Store.SetParameterValue<FLinearColor>(V, Var, true);
		return true;
	}
	return false;
}

// Helper: collect params from a store
static void CollectParametersFromStore(const FNiagaraParameterStore& Store, const FString& Scope,
	TArray<TSharedPtr<FJsonValue>>& OutArray)
{
	TArrayView<const FNiagaraVariableWithOffset> Variables = Store.ReadParameterVariables();
	for (const FNiagaraVariableWithOffset& VWO : Variables)
	{
		const FNiagaraVariable& Var = VWO;
		TSharedRef<FJsonObject> P = MakeShared<FJsonObject>();
		P->SetStringField(TEXT("name"), Var.GetName().ToString());
		P->SetStringField(TEXT("type"), Var.GetType().GetName());
		P->SetStringField(TEXT("scope"), Scope);
		P->SetStringField(TEXT("value"), FMonolithNiagaraActions::SerializeParameterValue(Var, Store));
		OutArray.Add(MakeShared<FJsonValueObject>(P));
	}
}

// ============================================================================
// Registration — 39 actions across 7 domains
// ============================================================================

void FMonolithNiagaraActions::RegisterActions(FMonolithToolRegistry& Registry)
{
	// System (8)
	Registry.RegisterAction(TEXT("niagara"), TEXT("add_emitter"), TEXT("Add an emitter to a Niagara system"), FMonolithActionHandler::CreateStatic(&HandleAddEmitter));
	Registry.RegisterAction(TEXT("niagara"), TEXT("remove_emitter"), TEXT("Remove an emitter from a Niagara system"), FMonolithActionHandler::CreateStatic(&HandleRemoveEmitter));
	Registry.RegisterAction(TEXT("niagara"), TEXT("duplicate_emitter"), TEXT("Duplicate an emitter within a Niagara system"), FMonolithActionHandler::CreateStatic(&HandleDuplicateEmitter));
	Registry.RegisterAction(TEXT("niagara"), TEXT("set_emitter_enabled"), TEXT("Enable or disable an emitter"), FMonolithActionHandler::CreateStatic(&HandleSetEmitterEnabled));
	Registry.RegisterAction(TEXT("niagara"), TEXT("reorder_emitters"), TEXT("Reorder emitters in a system"), FMonolithActionHandler::CreateStatic(&HandleReorderEmitters));
	Registry.RegisterAction(TEXT("niagara"), TEXT("set_emitter_property"), TEXT("Set an emitter property"), FMonolithActionHandler::CreateStatic(&HandleSetEmitterProperty));
	Registry.RegisterAction(TEXT("niagara"), TEXT("request_compile"), TEXT("Request compilation of a Niagara system"), FMonolithActionHandler::CreateStatic(&HandleRequestCompile));
	Registry.RegisterAction(TEXT("niagara"), TEXT("create_system"), TEXT("Create a new Niagara system"), FMonolithActionHandler::CreateStatic(&HandleCreateSystem));

	// Module (12)
	Registry.RegisterAction(TEXT("niagara"), TEXT("get_ordered_modules"), TEXT("Get ordered modules in a script stage"), FMonolithActionHandler::CreateStatic(&HandleGetOrderedModules));
	Registry.RegisterAction(TEXT("niagara"), TEXT("get_module_inputs"), TEXT("Get inputs for a module node"), FMonolithActionHandler::CreateStatic(&HandleGetModuleInputs));
	Registry.RegisterAction(TEXT("niagara"), TEXT("get_module_graph"), TEXT("Get the node graph of a module script"), FMonolithActionHandler::CreateStatic(&HandleGetModuleGraph));
	Registry.RegisterAction(TEXT("niagara"), TEXT("add_module"), TEXT("Add a module to a script stage"), FMonolithActionHandler::CreateStatic(&HandleAddModule));
	Registry.RegisterAction(TEXT("niagara"), TEXT("remove_module"), TEXT("Remove a module from a script stage"), FMonolithActionHandler::CreateStatic(&HandleRemoveModule));
	Registry.RegisterAction(TEXT("niagara"), TEXT("move_module"), TEXT("Move a module to a new index"), FMonolithActionHandler::CreateStatic(&HandleMoveModule));
	Registry.RegisterAction(TEXT("niagara"), TEXT("set_module_enabled"), TEXT("Enable or disable a module"), FMonolithActionHandler::CreateStatic(&HandleSetModuleEnabled));
	Registry.RegisterAction(TEXT("niagara"), TEXT("set_module_input_value"), TEXT("Set a module input value"), FMonolithActionHandler::CreateStatic(&HandleSetModuleInputValue));
	Registry.RegisterAction(TEXT("niagara"), TEXT("set_module_input_binding"), TEXT("Bind a module input to a parameter"), FMonolithActionHandler::CreateStatic(&HandleSetModuleInputBinding));
	Registry.RegisterAction(TEXT("niagara"), TEXT("set_module_input_di"), TEXT("Set a data interface on a module input"), FMonolithActionHandler::CreateStatic(&HandleSetModuleInputDI));
	Registry.RegisterAction(TEXT("niagara"), TEXT("create_module_from_hlsl"), TEXT("Create a Niagara module from HLSL"), FMonolithActionHandler::CreateStatic(&HandleCreateModuleFromHLSL));
	Registry.RegisterAction(TEXT("niagara"), TEXT("create_function_from_hlsl"), TEXT("Create a Niagara function from HLSL"), FMonolithActionHandler::CreateStatic(&HandleCreateFunctionFromHLSL));

	// Parameter (9)
	Registry.RegisterAction(TEXT("niagara"), TEXT("get_all_parameters"), TEXT("Get all parameters in a system"), FMonolithActionHandler::CreateStatic(&HandleGetAllParameters));
	Registry.RegisterAction(TEXT("niagara"), TEXT("get_user_parameters"), TEXT("Get user-exposed parameters"), FMonolithActionHandler::CreateStatic(&HandleGetUserParameters));
	Registry.RegisterAction(TEXT("niagara"), TEXT("get_parameter_value"), TEXT("Get a parameter value"), FMonolithActionHandler::CreateStatic(&HandleGetParameterValue));
	Registry.RegisterAction(TEXT("niagara"), TEXT("get_parameter_type"), TEXT("Get info about a Niagara type"), FMonolithActionHandler::CreateStatic(&HandleGetParameterType));
	Registry.RegisterAction(TEXT("niagara"), TEXT("trace_parameter_binding"), TEXT("Trace where a parameter is used"), FMonolithActionHandler::CreateStatic(&HandleTraceParameterBinding));
	Registry.RegisterAction(TEXT("niagara"), TEXT("add_user_parameter"), TEXT("Add a user parameter"), FMonolithActionHandler::CreateStatic(&HandleAddUserParameter));
	Registry.RegisterAction(TEXT("niagara"), TEXT("remove_user_parameter"), TEXT("Remove a user parameter"), FMonolithActionHandler::CreateStatic(&HandleRemoveUserParameter));
	Registry.RegisterAction(TEXT("niagara"), TEXT("set_parameter_default"), TEXT("Set a parameter default value"), FMonolithActionHandler::CreateStatic(&HandleSetParameterDefault));
	Registry.RegisterAction(TEXT("niagara"), TEXT("set_curve_value"), TEXT("Set curve keys on a module input"), FMonolithActionHandler::CreateStatic(&HandleSetCurveValue));

	// Renderer (6)
	Registry.RegisterAction(TEXT("niagara"), TEXT("add_renderer"), TEXT("Add a renderer to an emitter"), FMonolithActionHandler::CreateStatic(&HandleAddRenderer));
	Registry.RegisterAction(TEXT("niagara"), TEXT("remove_renderer"), TEXT("Remove a renderer from an emitter"), FMonolithActionHandler::CreateStatic(&HandleRemoveRenderer));
	Registry.RegisterAction(TEXT("niagara"), TEXT("set_renderer_material"), TEXT("Set renderer material"), FMonolithActionHandler::CreateStatic(&HandleSetRendererMaterial));
	Registry.RegisterAction(TEXT("niagara"), TEXT("set_renderer_property"), TEXT("Set a renderer property"), FMonolithActionHandler::CreateStatic(&HandleSetRendererProperty));
	Registry.RegisterAction(TEXT("niagara"), TEXT("get_renderer_bindings"), TEXT("Get renderer attribute bindings"), FMonolithActionHandler::CreateStatic(&HandleGetRendererBindings));
	Registry.RegisterAction(TEXT("niagara"), TEXT("set_renderer_binding"), TEXT("Set a renderer attribute binding"), FMonolithActionHandler::CreateStatic(&HandleSetRendererBinding));

	// Batch (2)
	Registry.RegisterAction(TEXT("niagara"), TEXT("batch_execute"), TEXT("Execute multiple operations in one transaction"), FMonolithActionHandler::CreateStatic(&HandleBatchExecute));
	Registry.RegisterAction(TEXT("niagara"), TEXT("create_system_from_spec"), TEXT("Create a full system from JSON spec"), FMonolithActionHandler::CreateStatic(&HandleCreateSystemFromSpec));

	// DI (1)
	Registry.RegisterAction(TEXT("niagara"), TEXT("get_di_functions"), TEXT("Get data interface function signatures"), FMonolithActionHandler::CreateStatic(&HandleGetDIFunctions));

	// HLSL (1)
	Registry.RegisterAction(TEXT("niagara"), TEXT("get_compiled_gpu_hlsl"), TEXT("Get compiled GPU HLSL for an emitter"), FMonolithActionHandler::CreateStatic(&HandleGetCompiledGPUHLSL));
}

// ============================================================================
// System Actions (8)
// ============================================================================

FMonolithActionResult FMonolithNiagaraActions::HandleAddEmitter(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterAssetPath = Params->GetStringField(TEXT("emitter_asset"));
	FString EmitterName = Params->HasField(TEXT("name")) ? Params->GetStringField(TEXT("name")) : FString();

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UNiagaraEmitter* EmitterAsset = LoadObject<UNiagaraEmitter>(nullptr, *EmitterAssetPath);
	if (!EmitterAsset) return FMonolithActionResult::Error(TEXT("Failed to load emitter asset"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "AddEmitter", "Add Emitter"));
	System->Modify();

	FName Name = EmitterName.IsEmpty() ? EmitterAsset->GetFName() : FName(*EmitterName);
	// UE 5.7 FIX: AddEmitterHandle takes FGuid VersionGuid, not FNiagaraAssetVersion
	FNiagaraEmitterHandle NewHandle = System->AddEmitterHandle(*EmitterAsset, Name, EmitterAsset->GetExposedVersion().VersionGuid);

	GEditor->EndTransaction();
	System->RequestCompile(false);

	TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetStringField(TEXT("handle_id"), NewHandle.GetId().ToString());
	return SuccessObj(R);
}

FMonolithActionResult FMonolithNiagaraActions::HandleRemoveEmitter(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	int32 Index = FindEmitterHandleIndex(System, EmitterHandleId);
	if (Index == INDEX_NONE) return FMonolithActionResult::Error(TEXT("Emitter handle not found"));

	const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[Index];
	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "RemoveEmitter", "Remove Emitter"));
	System->Modify();
	System->RemoveEmitterHandle(Handle);
	GEditor->EndTransaction();
	System->RequestCompile(false);

	return SuccessStr(TEXT("Emitter removed"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleDuplicateEmitter(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString SourceHandleId = Params->GetStringField(TEXT("source_emitter"));
	FString NewName = Params->HasField(TEXT("new_name")) ? Params->GetStringField(TEXT("new_name")) : FString();

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	int32 Index = FindEmitterHandleIndex(System, SourceHandleId);
	if (Index == INDEX_NONE) return FMonolithActionResult::Error(TEXT("Source emitter not found"));

	const FNiagaraEmitterHandle& Src = System->GetEmitterHandles()[Index];
	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "DupEmitter", "Duplicate Emitter"));
	System->Modify();

	FName DupName = NewName.IsEmpty() ? FName(*(Src.GetName().ToString() + TEXT("_Copy"))) : FName(*NewName);
	FNiagaraEmitterHandle NewHandle = System->DuplicateEmitterHandle(Src, DupName);

	GEditor->EndTransaction();
	System->RequestCompile(false);

	TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetStringField(TEXT("handle_id"), NewHandle.GetId().ToString());
	return SuccessObj(R);
}

FMonolithActionResult FMonolithNiagaraActions::HandleSetEmitterEnabled(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	bool bEnabled = Params->GetBoolField(TEXT("enabled"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	int32 Index = FindEmitterHandleIndex(System, EmitterHandleId);
	if (Index == INDEX_NONE) return FMonolithActionResult::Error(TEXT("Emitter handle not found"));

	TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "SetEmEnabled", "Set Emitter Enabled"));
	System->Modify();
	Handles[Index].SetIsEnabled(bEnabled, *System, true);
	GEditor->EndTransaction();

	return SuccessStr(bEnabled ? TEXT("Emitter enabled") : TEXT("Emitter disabled"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleReorderEmitters(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	const TArray<TSharedPtr<FJsonValue>>& OrderArr = Params->GetArrayField(TEXT("order"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	const TArray<FNiagaraEmitterHandle>& Current = System->GetEmitterHandles();
	if (OrderArr.Num() != Current.Num())
		return FMonolithActionResult::Error(FString::Printf(TEXT("Provided %d IDs but system has %d emitters"), OrderArr.Num(), Current.Num()));

	TArray<FNiagaraEmitterHandle> NewOrder;
	NewOrder.Reserve(OrderArr.Num());
	for (const TSharedPtr<FJsonValue>& V : OrderArr)
	{
		int32 Idx = FindEmitterHandleIndex(System, V->AsString());
		if (Idx == INDEX_NONE) return FMonolithActionResult::Error(FString::Printf(TEXT("Handle '%s' not found"), *V->AsString()));
		NewOrder.Add(Current[Idx]);
	}

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "ReorderEm", "Reorder Emitters"));
	System->Modify();
	System->GetEmitterHandles() = MoveTemp(NewOrder);
	GEditor->EndTransaction();
	System->RequestCompile(false);

	return SuccessStr(TEXT("Emitters reordered"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleSetEmitterProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	FString PropertyName = Params->GetStringField(TEXT("property"));
	FString ValueJson = Params->HasField(TEXT("value")) ? JsonValueToString(Params->TryGetField(TEXT("value"))) : FString();

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	int32 Index = FindEmitterHandleIndex(System, EmitterHandleId);
	if (Index == INDEX_NONE) return FMonolithActionResult::Error(TEXT("Emitter handle not found"));

	FVersionedNiagaraEmitterData* ED = System->GetEmitterHandles()[Index].GetEmitterData();
	if (!ED) return FMonolithActionResult::Error(TEXT("No emitter data"));

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ValueJson);
	TSharedPtr<FJsonValue> JV;
	if (!FJsonSerializer::Deserialize(Reader, JV) || !JV.IsValid())
		return FMonolithActionResult::Error(TEXT("Failed to parse value JSON"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "SetEmProp", "Set Emitter Property"));
	System->Modify();
	bool bOk = false;

	if (PropertyName == TEXT("SimTarget") || PropertyName == TEXT("sim_target"))
	{
		FString V = JV->AsString();
		if (V == TEXT("CPU") || V == TEXT("CPUSim")) { ED->SimTarget = ENiagaraSimTarget::CPUSim; bOk = true; }
		else if (V == TEXT("GPU") || V == TEXT("GPUComputeSim")) { ED->SimTarget = ENiagaraSimTarget::GPUComputeSim; bOk = true; }
	}
	else if (PropertyName == TEXT("bLocalSpace") || PropertyName == TEXT("local_space"))
	{ ED->bLocalSpace = JV->AsBool(); bOk = true; }
	else if (PropertyName == TEXT("bDeterminism") || PropertyName == TEXT("determinism"))
	{ ED->bDeterminism = JV->AsBool(); bOk = true; }
	else if (PropertyName == TEXT("RandomSeed") || PropertyName == TEXT("random_seed"))
	{ ED->RandomSeed = static_cast<int32>(JV->AsNumber()); bOk = true; }
	else if (PropertyName == TEXT("AllocationMode") || PropertyName == TEXT("allocation_mode"))
	{
		FString V = JV->AsString();
		if (V == TEXT("AutomaticEstimate")) { ED->AllocationMode = EParticleAllocationMode::AutomaticEstimate; bOk = true; }
		else if (V == TEXT("ManualEstimate")) { ED->AllocationMode = EParticleAllocationMode::ManualEstimate; bOk = true; }
		else if (V == TEXT("FixedCount")) { ED->AllocationMode = EParticleAllocationMode::FixedCount; bOk = true; }
	}
	else if (PropertyName == TEXT("PreAllocationCount") || PropertyName == TEXT("pre_allocation_count"))
	{ ED->PreAllocationCount = static_cast<int32>(JV->AsNumber()); bOk = true; }
	else if (PropertyName == TEXT("bRequiresPersistentIDs") || PropertyName == TEXT("requires_persistent_ids"))
	{ ED->bRequiresPersistentIDs = JV->AsBool(); bOk = true; }
	else if (PropertyName == TEXT("MaxGPUParticlesSpawnPerFrame") || PropertyName == TEXT("max_gpu_particles_spawn_per_frame"))
	{ ED->MaxGPUParticlesSpawnPerFrame = static_cast<int32>(JV->AsNumber()); bOk = true; }

	GEditor->EndTransaction();
	if (bOk) System->RequestCompile(false);
	return bOk ? SuccessStr(TEXT("Property set")) : FMonolithActionResult::Error(TEXT("Unknown property"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleRequestCompile(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));
	System->RequestCompile(false);
	return SuccessStr(TEXT("Compile requested"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleCreateSystem(const TSharedPtr<FJsonObject>& Params)
{
	FString SavePath = Params->GetStringField(TEXT("save_path"));
	FString TemplatePath = Params->HasField(TEXT("template")) ? Params->GetStringField(TEXT("template")) : FString();

	if (!TemplatePath.IsEmpty())
	{
		UNiagaraSystem* Template = LoadObject<UNiagaraSystem>(nullptr, *TemplatePath);
		if (!Template) return FMonolithActionResult::Error(TEXT("Failed to load template"));

		FString PackagePath, AssetName;
		int32 LastSlash;
		if (!SavePath.FindLastChar('/', LastSlash)) return FMonolithActionResult::Error(TEXT("Invalid save path"));
		PackagePath = SavePath.Left(LastSlash);
		AssetName = SavePath.Mid(LastSlash + 1);

		IAssetTools& AT = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		UObject* Dup = AT.DuplicateAsset(AssetName, PackagePath, Template);
		if (!Dup) return FMonolithActionResult::Error(TEXT("Failed to duplicate template"));
		return SuccessStr(Dup->GetPathName());
	}

	FString PackagePath, AssetName;
	int32 LastSlash;
	if (!SavePath.FindLastChar('/', LastSlash)) return FMonolithActionResult::Error(TEXT("Invalid save path"));
	PackagePath = SavePath.Left(LastSlash);
	AssetName = SavePath.Mid(LastSlash + 1);

	FString FullPath = PackagePath / AssetName;
	UPackage* Pkg = CreatePackage(*FullPath);
	if (!Pkg) return FMonolithActionResult::Error(TEXT("Failed to create package"));

	UNiagaraSystem* NS = NewObject<UNiagaraSystem>(Pkg, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
	if (!NS) return FMonolithActionResult::Error(TEXT("Failed to create system"));

	FAssetRegistryModule::AssetCreated(NS);
	Pkg->MarkPackageDirty();
	return SuccessStr(NS->GetPathName());
}

// ============================================================================
// Module Actions (12)
// ============================================================================

FMonolithActionResult FMonolithNiagaraActions::HandleGetOrderedModules(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	FString ScriptUsage = Params->GetStringField(TEXT("usage"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	ENiagaraScriptUsage Usage = ResolveScriptUsage(ScriptUsage);
	UNiagaraNodeOutput* OutputNode = FindOutputNode(System, EmitterHandleId, Usage);
	if (!OutputNode) return FMonolithActionResult::Error(TEXT("No output node found"));

	TArray<UNiagaraNodeFunctionCall*> ModuleNodes;
	MonolithNiagaraHelpers::GetOrderedModuleNodes(*OutputNode, ModuleNodes);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (int32 i = 0; i < ModuleNodes.Num(); ++i)
	{
		UNiagaraNodeFunctionCall* N = ModuleNodes[i];
		if (!N) continue;
		TSharedRef<FJsonObject> M = MakeShared<FJsonObject>();
		M->SetStringField(TEXT("node_guid"), N->NodeGuid.ToString());
		M->SetStringField(TEXT("function_name"), N->GetFunctionName());
		M->SetNumberField(TEXT("index"), i);
		TOptional<bool> bEn = MonolithNiagaraHelpers::GetModuleIsEnabled(*N);
		M->SetBoolField(TEXT("enabled"), bEn.IsSet() ? bEn.GetValue() : true);
		if (N->FunctionScript) M->SetStringField(TEXT("script_path"), N->FunctionScript->GetPathName());
		Arr.Add(MakeShared<FJsonValueObject>(M));
	}
	return SuccessStr(JsonArrayToString(Arr));
}

FMonolithActionResult FMonolithNiagaraActions::HandleGetModuleInputs(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	FString ModuleNodeGuid = Params->GetStringField(TEXT("module_node"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UNiagaraNodeFunctionCall* ModuleNode = FindModuleNode(System, EmitterHandleId, ModuleNodeGuid);
	if (!ModuleNode) return FMonolithActionResult::Error(TEXT("Module node not found"));

	TArray<FNiagaraVariable> Inputs;
	MonolithNiagaraHelpers::GetStackFunctionInputs(*ModuleNode, Inputs);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FNiagaraVariable& Input : Inputs)
	{
		TSharedRef<FJsonObject> IO = MakeShared<FJsonObject>();
		IO->SetStringField(TEXT("name"), Input.GetName().ToString());
		IO->SetStringField(TEXT("type"), Input.GetType().GetName());

		FNiagaraParameterHandle AH = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
			FNiagaraParameterHandle(Input.GetName()), ModuleNode);
		UEdGraphPin* OP = MonolithNiagaraHelpers::GetStackFunctionInputOverridePin(*ModuleNode, AH);
		if (OP)
		{
			IO->SetStringField(TEXT("override_value"), OP->DefaultValue);
			IO->SetBoolField(TEXT("has_override"), true);
			if (OP->LinkedTo.Num() > 0)
			{
				IO->SetBoolField(TEXT("is_linked"), true);
				if (UNiagaraNodeInput* LI = Cast<UNiagaraNodeInput>(OP->LinkedTo[0]->GetOwningNode()))
					IO->SetStringField(TEXT("linked_parameter"), LI->Input.GetName().ToString());
			}
		}
		else
		{
			IO->SetBoolField(TEXT("has_override"), false);
		}
		Arr.Add(MakeShared<FJsonValueObject>(IO));
	}
	return SuccessStr(JsonArrayToString(Arr));
}

FMonolithActionResult FMonolithNiagaraActions::HandleGetModuleGraph(const TSharedPtr<FJsonObject>& Params)
{
	FString ScriptPath = Params->GetStringField(TEXT("script_path"));
	UNiagaraScript* Script = LoadObject<UNiagaraScript>(nullptr, *ScriptPath);
	if (!Script) return FMonolithActionResult::Error(TEXT("Failed to load script"));

	UNiagaraScriptSource* Src = Cast<UNiagaraScriptSource>(Script->GetLatestSource());
	if (!Src || !Src->NodeGraph) return FMonolithActionResult::Error(TEXT("No graph available"));

	UNiagaraGraph* Graph = Src->NodeGraph;
	TSharedRef<FJsonObject> Res = MakeShared<FJsonObject>();
	Res->SetStringField(TEXT("script_path"), ScriptPath);
	Res->SetStringField(TEXT("script_usage"), StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(static_cast<int64>(Script->GetUsage())));

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	TArray<UEdGraphNode*> AllNodes;
	Graph->GetNodesOfClass<UEdGraphNode>(AllNodes);
	for (UEdGraphNode* Node : AllNodes)
	{
		TSharedRef<FJsonObject> NO = MakeShared<FJsonObject>();
		NO->SetStringField(TEXT("node_guid"), Node->NodeGuid.ToString());
		NO->SetStringField(TEXT("class"), Node->GetClass()->GetName());
		NO->SetStringField(TEXT("title"), Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
		NO->SetNumberField(TEXT("pos_x"), Node->NodePosX);
		NO->SetNumberField(TEXT("pos_y"), Node->NodePosY);
		if (UNiagaraNodeFunctionCall* FN = Cast<UNiagaraNodeFunctionCall>(Node))
		{
			NO->SetStringField(TEXT("function_name"), FN->GetFunctionName());
			if (FN->FunctionScript) NO->SetStringField(TEXT("function_script"), FN->FunctionScript->GetPathName());
		}
		TArray<TSharedPtr<FJsonValue>> PinsArr;
		for (UEdGraphPin* Pin : Node->Pins)
		{
			TSharedRef<FJsonObject> PO = MakeShared<FJsonObject>();
			PO->SetStringField(TEXT("name"), Pin->PinName.ToString());
			PO->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("Input") : TEXT("Output"));
			PO->SetStringField(TEXT("type"), Pin->PinType.PinCategory.ToString());
			PO->SetStringField(TEXT("default_value"), Pin->DefaultValue);
			PO->SetNumberField(TEXT("linked_count"), Pin->LinkedTo.Num());
			PinsArr.Add(MakeShared<FJsonValueObject>(PO));
		}
		NO->SetArrayField(TEXT("pins"), PinsArr);
		NodesArr.Add(MakeShared<FJsonValueObject>(NO));
	}
	Res->SetArrayField(TEXT("nodes"), NodesArr);
	return SuccessObj(Res);
}

FMonolithActionResult FMonolithNiagaraActions::HandleAddModule(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	FString ScriptUsage = Params->GetStringField(TEXT("usage"));
	FString ModuleScriptPath = Params->GetStringField(TEXT("module_script"));
	int32 Index = Params->HasField(TEXT("index")) ? static_cast<int32>(Params->GetNumberField(TEXT("index"))) : -1;

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UNiagaraScript* ModScript = LoadObject<UNiagaraScript>(nullptr, *ModuleScriptPath);
	if (!ModScript) return FMonolithActionResult::Error(TEXT("Failed to load module script"));

	ENiagaraScriptUsage Usage = ResolveScriptUsage(ScriptUsage);
	UNiagaraNodeOutput* OutputNode = FindOutputNode(System, EmitterHandleId, Usage);
	if (!OutputNode) return FMonolithActionResult::Error(TEXT("No output node"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "AddMod", "Add Module"));
	System->Modify();
	UNiagaraNodeFunctionCall* NewNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(ModScript, *OutputNode, Index);
	GEditor->EndTransaction();

	if (!NewNode) return FMonolithActionResult::Error(TEXT("AddScriptModuleToStack failed"));
	System->RequestCompile(false);

	TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetStringField(TEXT("node_guid"), NewNode->NodeGuid.ToString());
	return SuccessObj(R);
}

FMonolithActionResult FMonolithNiagaraActions::HandleRemoveModule(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	FString ModuleNodeGuid = Params->GetStringField(TEXT("module_node"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UNiagaraNodeFunctionCall* MN = FindModuleNode(System, EmitterHandleId, ModuleNodeGuid);
	if (!MN) return FMonolithActionResult::Error(TEXT("Module node not found"));

	FGuid EmitterGuid;
	int32 EIdx = FindEmitterHandleIndex(System, EmitterHandleId);
	if (EIdx != INDEX_NONE) EmitterGuid = System->GetEmitterHandles()[EIdx].GetId();

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "RemMod", "Remove Module"));
	System->Modify();
	MonolithNiagaraHelpers::RemoveModuleFromStack(*System, EmitterGuid, *MN);
	GEditor->EndTransaction();
	System->RequestCompile(false);

	return SuccessStr(TEXT("Module removed"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleMoveModule(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	FString ModuleNodeGuid = Params->GetStringField(TEXT("module_node"));
	int32 NewIndex = static_cast<int32>(Params->GetNumberField(TEXT("new_index")));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	ENiagaraScriptUsage FoundUsage;
	UNiagaraNodeFunctionCall* MN = FindModuleNode(System, EmitterHandleId, ModuleNodeGuid, &FoundUsage);
	if (!MN) return FMonolithActionResult::Error(TEXT("Module node not found"));

	UNiagaraNodeOutput* OutputNode = FindOutputNode(System, EmitterHandleId, FoundUsage);
	if (!OutputNode) return FMonolithActionResult::Error(TEXT("No output node"));

	TArray<UNiagaraNodeFunctionCall*> Mods;
	MonolithNiagaraHelpers::GetOrderedModuleNodes(*OutputNode, Mods);
	int32 CurIdx = Mods.IndexOfByKey(MN);
	if (CurIdx == INDEX_NONE) return FMonolithActionResult::Error(TEXT("Module not in ordered list"));
	if (CurIdx == NewIndex) return SuccessStr(TEXT("Already at target index"));

	NewIndex = FMath::Clamp(NewIndex, 0, Mods.Num() - 1);
	UNiagaraScript* ModScript = MN->FunctionScript;
	if (!ModScript) return FMonolithActionResult::Error(TEXT("Module has no script"));

	FGuid EmitterGuid;
	int32 EIdx = FindEmitterHandleIndex(System, EmitterHandleId);
	if (EIdx != INDEX_NONE) EmitterGuid = System->GetEmitterHandles()[EIdx].GetId();

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "MoveMod", "Move Module"));
	System->Modify();
	MonolithNiagaraHelpers::RemoveModuleFromStack(*System, EmitterGuid, *MN);
	UNiagaraNodeFunctionCall* NewNode = FNiagaraStackGraphUtilities::AddScriptModuleToStack(ModScript, *OutputNode, NewIndex);
	GEditor->EndTransaction();

	if (!NewNode) return FMonolithActionResult::Error(TEXT("Failed to re-add module"));
	System->RequestCompile(false);
	return SuccessStr(TEXT("Module moved"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleSetModuleEnabled(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	FString ModuleNodeGuid = Params->GetStringField(TEXT("module_node"));
	bool bEnabled = Params->GetBoolField(TEXT("enabled"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UNiagaraNodeFunctionCall* MN = FindModuleNode(System, EmitterHandleId, ModuleNodeGuid);
	if (!MN) return FMonolithActionResult::Error(TEXT("Module node not found"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "SetModEn", "Set Module Enabled"));
	System->Modify();
	FNiagaraStackGraphUtilities::SetModuleIsEnabled(*MN, bEnabled);
	GEditor->EndTransaction();
	System->RequestCompile(false);

	return SuccessStr(bEnabled ? TEXT("Module enabled") : TEXT("Module disabled"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleSetModuleInputValue(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	FString ModuleNodeGuid = Params->GetStringField(TEXT("module_node"));
	FString InputName = Params->GetStringField(TEXT("input"));
	FString ValueJson = Params->HasField(TEXT("value")) ? JsonValueToString(Params->TryGetField(TEXT("value"))) : FString();

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UNiagaraNodeFunctionCall* MN = FindModuleNode(System, EmitterHandleId, ModuleNodeGuid);
	if (!MN) return FMonolithActionResult::Error(TEXT("Module node not found"));

	TArray<FNiagaraVariable> Inputs;
	MonolithNiagaraHelpers::GetStackFunctionInputs(*MN, Inputs);

	FNiagaraTypeDefinition InputType = FNiagaraTypeDefinition::GetFloatDef();
	for (const FNiagaraVariable& In : Inputs)
	{
		if (In.GetName() == FName(*InputName)) { InputType = In.GetType(); break; }
	}

	FNiagaraParameterHandle AH = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		FNiagaraParameterHandle(FName(*InputName)), MN);

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ValueJson);
	TSharedPtr<FJsonValue> JV;
	if (!FJsonSerializer::Deserialize(Reader, JV) || !JV.IsValid())
		return FMonolithActionResult::Error(TEXT("Failed to parse value JSON"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "SetModIn", "Set Module Input"));
	System->Modify();

	// UE 5.7 FIX: 5-param version of GetOrCreateStackFunctionInputOverridePin
	UEdGraphPin& OverridePin = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*MN, AH, InputType, FGuid(), FGuid());

	FString ValStr;
	if (JV->Type == EJson::Number) ValStr = FString::SanitizeFloat(JV->AsNumber());
	else if (JV->Type == EJson::Boolean) ValStr = JV->AsBool() ? TEXT("true") : TEXT("false");
	else if (JV->Type == EJson::String) ValStr = JV->AsString();
	else if (JV->Type == EJson::Object)
	{
		TSharedPtr<FJsonObject> O = JV->AsObject();
		if (O->HasField(TEXT("x")))
		{
			double X = O->GetNumberField(TEXT("x")), Y = O->GetNumberField(TEXT("y"));
			double Z = O->HasField(TEXT("z")) ? O->GetNumberField(TEXT("z")) : 0.0;
			double W = O->HasField(TEXT("w")) ? O->GetNumberField(TEXT("w")) : 0.0;
			if (O->HasField(TEXT("w"))) ValStr = FString::Printf(TEXT("%f,%f,%f,%f"), X, Y, Z, W);
			else if (O->HasField(TEXT("z"))) ValStr = FString::Printf(TEXT("%f,%f,%f"), X, Y, Z);
			else ValStr = FString::Printf(TEXT("%f,%f"), X, Y);
		}
		else if (O->HasField(TEXT("r")))
		{
			double R2 = O->GetNumberField(TEXT("r")), G = O->GetNumberField(TEXT("g"));
			double B = O->GetNumberField(TEXT("b")), A = O->HasField(TEXT("a")) ? O->GetNumberField(TEXT("a")) : 1.0;
			ValStr = FString::Printf(TEXT("%f,%f,%f,%f"), R2, G, B, A);
		}
		else
		{
			TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&ValStr);
			FJsonSerializer::Serialize(O.ToSharedRef(), W);
		}
	}
	else ValStr = ValueJson;

	OverridePin.DefaultValue = ValStr;
	GEditor->EndTransaction();
	System->RequestCompile(false);

	return SuccessStr(FString::Printf(TEXT("Set input '%s' = '%s'"), *InputName, *ValStr));
}

FMonolithActionResult FMonolithNiagaraActions::HandleSetModuleInputBinding(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	FString ModuleNodeGuid = Params->GetStringField(TEXT("module_node"));
	FString InputName = Params->GetStringField(TEXT("input"));
	FString BindingPath = Params->GetStringField(TEXT("binding"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UNiagaraNodeFunctionCall* MN = FindModuleNode(System, EmitterHandleId, ModuleNodeGuid);
	if (!MN) return FMonolithActionResult::Error(TEXT("Module node not found"));

	TArray<FNiagaraVariable> Inputs;
	MonolithNiagaraHelpers::GetStackFunctionInputs(*MN, Inputs);

	FNiagaraTypeDefinition InputType = FNiagaraTypeDefinition::GetFloatDef();
	for (const FNiagaraVariable& In : Inputs)
	{
		if (In.GetName() == FName(*InputName)) { InputType = In.GetType(); break; }
	}

	FNiagaraParameterHandle AH = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		FNiagaraParameterHandle(FName(*InputName)), MN);

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "SetModBind", "Set Module Binding"));
	System->Modify();

	// UE 5.7 FIX: 5-param version
	UEdGraphPin& OP = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*MN, AH, InputType, FGuid(), FGuid());

	FNiagaraVariable LinkedParam(InputType, FName(*BindingPath));
	UNiagaraGraph* Graph = MN->GetNiagaraGraph();
	TSet<FNiagaraVariableBase> KnownParams;
	if (Graph) MonolithNiagaraHelpers::GetParametersForContext(Graph, *System, KnownParams);
	FNiagaraStackGraphUtilities::SetLinkedParameterValueForFunctionInput(OP, LinkedParam, KnownParams);

	GEditor->EndTransaction();
	System->RequestCompile(false);

	return SuccessStr(FString::Printf(TEXT("Bound '%s' to '%s'"), *InputName, *BindingPath));
}

FMonolithActionResult FMonolithNiagaraActions::HandleSetModuleInputDI(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	FString ModuleNodeGuid = Params->GetStringField(TEXT("module_node"));
	FString InputName = Params->GetStringField(TEXT("input"));
	FString DIClass = Params->GetStringField(TEXT("di_class"));
	FString DIConfigJson = Params->HasField(TEXT("config")) ? Params->GetStringField(TEXT("config")) : FString();

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UNiagaraNodeFunctionCall* MN = FindModuleNode(System, EmitterHandleId, ModuleNodeGuid);
	if (!MN) return FMonolithActionResult::Error(TEXT("Module node not found"));

	FString FullName = DIClass;
	if (!FullName.StartsWith(TEXT("UNiagara"))) FullName = TEXT("UNiagara") + DIClass;
	UClass* DIUClass = FindFirstObject<UClass>(*FullName, EFindFirstObjectOptions::NativeFirst);
	if (!DIUClass) DIUClass = FindFirstObject<UClass>(*FullName.Mid(1), EFindFirstObjectOptions::NativeFirst);
	if (!DIUClass) return FMonolithActionResult::Error(TEXT("DI class not found"));

	TArray<FNiagaraVariable> Inputs;
	MonolithNiagaraHelpers::GetStackFunctionInputs(*MN, Inputs);
	FNiagaraTypeDefinition InputType(DIUClass);
	for (const FNiagaraVariable& In : Inputs)
	{
		if (In.GetName() == FName(*InputName)) { InputType = In.GetType(); break; }
	}

	FNiagaraParameterHandle AH = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		FNiagaraParameterHandle(FName(*InputName)), MN);

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "SetModDI", "Set Module DI"));
	System->Modify();

	// UE 5.7 FIX: 5-param version
	UEdGraphPin& OP = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*MN, AH, InputType, FGuid(), FGuid());

	UNiagaraDataInterface* DIInst = nullptr;
	FNiagaraStackGraphUtilities::SetDataInterfaceValueForFunctionInput(OP, DIUClass, InputName, DIInst);

	if (DIInst && !DIConfigJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DIConfigJson);
		TSharedPtr<FJsonObject> Cfg;
		if (FJsonSerializer::Deserialize(Reader, Cfg) && Cfg.IsValid())
		{
			for (auto& Pair : Cfg->Values)
			{
				FProperty* Prop = DIUClass->FindPropertyByName(FName(*Pair.Key));
				if (!Prop) continue;
				void* Addr = Prop->ContainerPtrToValuePtr<void>(DIInst);
				if (FFloatProperty* FP = CastField<FFloatProperty>(Prop)) FP->SetPropertyValue(Addr, static_cast<float>(Pair.Value->AsNumber()));
				else if (FIntProperty* IP = CastField<FIntProperty>(Prop)) IP->SetPropertyValue(Addr, static_cast<int32>(Pair.Value->AsNumber()));
				else if (FBoolProperty* BP = CastField<FBoolProperty>(Prop)) BP->SetPropertyValue(Addr, Pair.Value->AsBool());
				else if (FStrProperty* SP = CastField<FStrProperty>(Prop)) SP->SetPropertyValue(Addr, Pair.Value->AsString());
			}
		}
	}

	GEditor->EndTransaction();
	System->RequestCompile(false);

	return SuccessStr(TEXT("DI set on module input"));
}

FMonolithActionResult FMonolithNiagaraActions::CreateScriptFromHLSL(const TSharedPtr<FJsonObject>& Params, ENiagaraScriptUsage Usage)
{
	// UNiagaraNodeCustomHlsl::SetCustomHlsl and RequestNewTypedPin are not exported from NiagaraEditor.
	// Use the unreal-niagara MCP server's Python-based HLSL creation instead.
	return FMonolithActionResult::Error(TEXT("HLSL script creation requires Python bridge (NiagaraEditor internal APIs not exported). Use unreal-niagara MCP create_niagara_module instead."));
}

FMonolithActionResult FMonolithNiagaraActions::HandleCreateModuleFromHLSL(const TSharedPtr<FJsonObject>& Params)
{
	return CreateScriptFromHLSL(Params, ENiagaraScriptUsage::Module);
}

FMonolithActionResult FMonolithNiagaraActions::HandleCreateFunctionFromHLSL(const TSharedPtr<FJsonObject>& Params)
{
	return CreateScriptFromHLSL(Params, ENiagaraScriptUsage::Function);
}

// ============================================================================
// Parameter Actions (9)
// ============================================================================

FMonolithActionResult FMonolithNiagaraActions::HandleGetAllParameters(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	TArray<TSharedPtr<FJsonValue>> All;
	FNiagaraUserRedirectionParameterStore& US = System->GetExposedParameters();
	CollectParametersFromStore(US, TEXT("User"), All);

	const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	for (const FNiagaraEmitterHandle& H : Handles)
	{
		FVersionedNiagaraEmitterData* ED = H.GetEmitterData();
		if (!ED) continue;
		FString EScope = FString::Printf(TEXT("Emitter.%s"), *H.GetName().ToString());
		static const ENiagaraScriptUsage Usages[] = {
			ENiagaraScriptUsage::EmitterSpawnScript, ENiagaraScriptUsage::EmitterUpdateScript,
			ENiagaraScriptUsage::ParticleSpawnScript, ENiagaraScriptUsage::ParticleUpdateScript,
		};
		for (ENiagaraScriptUsage U : Usages)
		{
			UNiagaraScript* S = ED->GetScript(U, FGuid());
			if (!S) continue;
			// UE 5.7 FIX: direct UPROPERTY access, no getter
			const FNiagaraParameterStore& PS = S->RapidIterationParameters;
			FString UStr = StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(static_cast<int64>(U));
			CollectParametersFromStore(PS, FString::Printf(TEXT("%s.%s"), *EScope, *UStr), All);
		}
	}
	return SuccessStr(JsonArrayToString(All));
}

FMonolithActionResult FMonolithNiagaraActions::HandleGetUserParameters(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	FNiagaraUserRedirectionParameterStore& US = System->GetExposedParameters();
	TArray<FNiagaraVariable> UP;
	US.GetUserParameters(UP);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FNiagaraVariable& P : UP)
	{
		TSharedRef<FJsonObject> O = MakeShared<FJsonObject>();
		O->SetStringField(TEXT("name"), P.GetName().ToString());
		O->SetStringField(TEXT("type"), P.GetType().GetName());
		O->SetStringField(TEXT("value"), SerializeParameterValue(P, US));
		Arr.Add(MakeShared<FJsonValueObject>(O));
	}
	return SuccessStr(JsonArrayToString(Arr));
}

FMonolithActionResult FMonolithNiagaraActions::HandleGetParameterValue(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString ParamName = Params->GetStringField(TEXT("parameter"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	FNiagaraUserRedirectionParameterStore& US = System->GetExposedParameters();
	TArray<FNiagaraVariable> UP;
	US.GetUserParameters(UP);

	FString Search = ParamName;
	if (!Search.StartsWith(TEXT("User."))) Search = TEXT("User.") + Search;

	for (const FNiagaraVariable& P : UP)
	{
		if (P.GetName().ToString() == Search || P.GetName().ToString() == ParamName)
		{
			TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
			R->SetStringField(TEXT("name"), P.GetName().ToString());
			R->SetStringField(TEXT("type"), P.GetType().GetName());
			R->SetStringField(TEXT("value"), SerializeParameterValue(P, US));
			return SuccessObj(R);
		}
	}
	return FMonolithActionResult::Error(TEXT("Parameter not found"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleGetParameterType(const TSharedPtr<FJsonObject>& Params)
{
	FString TypeName = Params->GetStringField(TEXT("type"));
	FNiagaraTypeDefinition TD = ResolveNiagaraType(TypeName);

	TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetStringField(TEXT("name"), TD.GetName());
	R->SetNumberField(TEXT("size"), TD.GetSize());
	R->SetBoolField(TEXT("is_float_primitive"), TD == FNiagaraTypeDefinition::GetFloatDef());
	R->SetBoolField(TEXT("is_data_interface"), TD.IsDataInterface());
	R->SetBoolField(TEXT("is_enum"), TD.IsEnum());
	R->SetBoolField(TEXT("is_valid"), TD.IsValid());
	if (TD.GetStruct()) R->SetStringField(TEXT("struct_name"), TD.GetStruct()->GetName());

	return SuccessObj(R);
}

FMonolithActionResult FMonolithNiagaraActions::HandleTraceParameterBinding(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString ParamName = Params->GetStringField(TEXT("parameter"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	FString Search = ParamName;
	if (!Search.StartsWith(TEXT("User."))) Search = TEXT("User.") + Search;

	TSharedRef<FJsonObject> Trace = MakeShared<FJsonObject>();
	Trace->SetStringField(TEXT("parameter"), Search);

	FNiagaraUserRedirectionParameterStore& US = System->GetExposedParameters();
	TArray<FNiagaraVariable> UP;
	US.GetUserParameters(UP);

	bool bFound = false;
	for (const FNiagaraVariable& P : UP)
	{
		if (P.GetName().ToString() == Search)
		{
			bFound = true;
			Trace->SetStringField(TEXT("type"), P.GetType().GetName());
			Trace->SetStringField(TEXT("source"), TEXT("ExposedParameters"));
			Trace->SetStringField(TEXT("value"), SerializeParameterValue(P, US));
			break;
		}
	}
	if (!bFound)
	{
		Trace->SetStringField(TEXT("error"), TEXT("Parameter not found"));
		return SuccessObj(Trace);
	}

	TArray<TSharedPtr<FJsonValue>> Bindings;
	const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();
	static const ENiagaraScriptUsage AllUsages[] = {
		ENiagaraScriptUsage::EmitterSpawnScript, ENiagaraScriptUsage::EmitterUpdateScript,
		ENiagaraScriptUsage::ParticleSpawnScript, ENiagaraScriptUsage::ParticleUpdateScript,
	};
	for (const FNiagaraEmitterHandle& H : Handles)
	{
		FString EN = H.GetName().ToString();
		for (ENiagaraScriptUsage U : AllUsages)
		{
			UNiagaraNodeOutput* Out = FindOutputNode(System, H.GetId().ToString(), U);
			if (!Out) continue;
			TArray<UNiagaraNodeFunctionCall*> Mods;
			MonolithNiagaraHelpers::GetOrderedModuleNodes(*Out, Mods);
			for (UNiagaraNodeFunctionCall* MN : Mods)
			{
				if (!MN) continue;
				for (UEdGraphPin* Pin : MN->Pins)
				{
					if (Pin->Direction != EGPD_Input) continue;
					for (UEdGraphPin* LP : Pin->LinkedTo)
					{
						FString LN = LP->PinName.ToString();
						if (LN.Contains(Search) || LN.Contains(ParamName))
						{
							TSharedRef<FJsonObject> BO = MakeShared<FJsonObject>();
							BO->SetStringField(TEXT("emitter"), EN);
							BO->SetStringField(TEXT("module"), MN->GetFunctionName());
							BO->SetStringField(TEXT("input_pin"), Pin->PinName.ToString());
							BO->SetStringField(TEXT("usage"), StaticEnum<ENiagaraScriptUsage>()->GetNameStringByValue(static_cast<int64>(U)));
							Bindings.Add(MakeShared<FJsonValueObject>(BO));
						}
					}
				}
			}
		}
	}
	Trace->SetArrayField(TEXT("bindings"), Bindings);
	return SuccessObj(Trace);
}

FMonolithActionResult FMonolithNiagaraActions::HandleAddUserParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString ParamName = Params->GetStringField(TEXT("name"));
	FString TypeName = Params->GetStringField(TEXT("type"));
	FString DefaultJson = Params->HasField(TEXT("default")) ? JsonValueToString(Params->TryGetField(TEXT("default"))) : FString();

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	FNiagaraTypeDefinition TD = ResolveNiagaraType(TypeName);
	FNiagaraVariable NV = MakeUserVariable(ParamName, TD);

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "AddUP", "Add User Parameter"));
	System->Modify();
	FNiagaraUserRedirectionParameterStore& US = System->GetExposedParameters();
	US.AddParameter(NV, true, false);

	if (!DefaultJson.IsEmpty())
	{
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(DefaultJson);
		TSharedPtr<FJsonValue> JV;
		if (FJsonSerializer::Deserialize(Reader, JV) && JV.IsValid())
			SetTypedParameterValue(US, NV, TD, JV);
	}

	GEditor->EndTransaction();
	return SuccessStr(FString::Printf(TEXT("Added user parameter '%s'"), *ParamName));
}

FMonolithActionResult FMonolithNiagaraActions::HandleRemoveUserParameter(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString ParamName = Params->GetStringField(TEXT("name"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	FString Search = ParamName;
	if (!Search.StartsWith(TEXT("User."))) Search = TEXT("User.") + Search;

	FNiagaraUserRedirectionParameterStore& US = System->GetExposedParameters();
	TArray<FNiagaraVariable> UP;
	US.GetUserParameters(UP);

	for (const FNiagaraVariable& P : UP)
	{
		if (P.GetName().ToString() == Search || P.GetName().ToString() == ParamName)
		{
			GEditor->BeginTransaction(NSLOCTEXT("Monolith", "RemUP", "Remove User Parameter"));
			System->Modify();
			US.RemoveParameter(P);
			GEditor->EndTransaction();
			return SuccessStr(FString::Printf(TEXT("Removed parameter '%s'"), *ParamName));
		}
	}
	return FMonolithActionResult::Error(TEXT("Parameter not found"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleSetParameterDefault(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString ParamName = Params->GetStringField(TEXT("parameter"));
	FString ValueJson = Params->HasField(TEXT("value")) ? JsonValueToString(Params->TryGetField(TEXT("value"))) : FString();

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	FString Search = ParamName;
	if (!Search.StartsWith(TEXT("User."))) Search = TEXT("User.") + Search;

	FNiagaraUserRedirectionParameterStore& US = System->GetExposedParameters();
	TArray<FNiagaraVariable> UP;
	US.GetUserParameters(UP);

	FNiagaraVariable Found;
	bool bFound = false;
	for (const FNiagaraVariable& P : UP)
	{
		if (P.GetName().ToString() == Search || P.GetName().ToString() == ParamName)
		{ Found = P; bFound = true; break; }
	}
	if (!bFound) return FMonolithActionResult::Error(TEXT("Parameter not found"));

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ValueJson);
	TSharedPtr<FJsonValue> JV;
	if (!FJsonSerializer::Deserialize(Reader, JV) || !JV.IsValid())
		return FMonolithActionResult::Error(TEXT("Failed to parse value JSON"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "SetPDef", "Set Parameter Default"));
	System->Modify();
	bool bOk = SetTypedParameterValue(US, Found, Found.GetType(), JV);
	GEditor->EndTransaction();

	return bOk ? SuccessStr(TEXT("Default set")) : FMonolithActionResult::Error(TEXT("Unsupported type"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleSetCurveValue(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	FString ModuleName = Params->GetStringField(TEXT("module"));
	FString InputName = Params->GetStringField(TEXT("input"));
	FString CurveKeysJson = Params->GetStringField(TEXT("keys"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(CurveKeysJson);
	TArray<TSharedPtr<FJsonValue>> Keys;
	if (!FJsonSerializer::Deserialize(Reader, Keys)) return FMonolithActionResult::Error(TEXT("Failed to parse keys"));

	TArray<FString> KS;
	for (const TSharedPtr<FJsonValue>& KV : Keys)
	{
		TSharedPtr<FJsonObject> KO = KV->AsObject();
		if (!KO) continue;
		float T = static_cast<float>(KO->GetNumberField(TEXT("time")));
		float V = static_cast<float>(KO->GetNumberField(TEXT("value")));
		float AT = KO->HasField(TEXT("arrive_tangent")) ? static_cast<float>(KO->GetNumberField(TEXT("arrive_tangent"))) : 0.f;
		float LT = KO->HasField(TEXT("leave_tangent")) ? static_cast<float>(KO->GetNumberField(TEXT("leave_tangent"))) : 0.f;
		KS.Add(FString::Printf(TEXT("(Time=%f,Value=%f,ArriveTangent=%f,LeaveTangent=%f)"), T, V, AT, LT));
	}
	FString CurveStr = TEXT("(") + FString::Join(KS, TEXT(",")) + TEXT(")");

	UNiagaraNodeFunctionCall* MN = FindModuleNode(System, EmitterHandleId, ModuleName);
	if (!MN) return FMonolithActionResult::Error(TEXT("Module not found"));

	// Find input type
	TArray<FNiagaraVariable> Inputs;
	MonolithNiagaraHelpers::GetStackFunctionInputs(*MN, Inputs);
	FNiagaraTypeDefinition InputType = FNiagaraTypeDefinition::GetFloatDef();
	for (const FNiagaraVariable& In : Inputs)
	{
		if (In.GetName() == FName(*InputName)) { InputType = In.GetType(); break; }
	}

	FNiagaraParameterHandle PH = FNiagaraParameterHandle::CreateAliasedModuleParameterHandle(
		FNiagaraParameterHandle(FName(*InputName)), MN);

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "SetCurve", "Set Curve"));
	System->Modify();

	// UE 5.7 FIX: 5-param version
	UEdGraphPin& OP = FNiagaraStackGraphUtilities::GetOrCreateStackFunctionInputOverridePin(
		*MN, PH, InputType, FGuid(), FGuid());
	OP.DefaultValue = CurveStr;

	GEditor->EndTransaction();
	System->RequestCompile(false);

	return SuccessStr(FString::Printf(TEXT("Set curve with %d keys"), Keys.Num()));
}

// ============================================================================
// Renderer Actions (6)
// ============================================================================

FMonolithActionResult FMonolithNiagaraActions::HandleAddRenderer(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	FString RendererClassStr = Params->GetStringField(TEXT("class"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UClass* RC = ResolveRendererClass(RendererClassStr);
	if (!RC) return FMonolithActionResult::Error(TEXT("Unknown renderer class"));

	int32 EIdx = FindEmitterHandleIndex(System, EmitterHandleId);
	if (EIdx == INDEX_NONE) return FMonolithActionResult::Error(TEXT("Emitter not found"));

	const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EIdx];
	FVersionedNiagaraEmitterData* ED = Handle.GetEmitterData();
	if (!ED) return FMonolithActionResult::Error(TEXT("No emitter data"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "AddRend", "Add Renderer"));
	System->Modify();

	FVersionedNiagaraEmitter EI = Handle.GetInstance();
	UNiagaraRendererProperties* NR = NewObject<UNiagaraRendererProperties>(EI.Emitter, RC, NAME_None, RF_Transactional);
	if (!NR) { GEditor->EndTransaction(); return FMonolithActionResult::Error(TEXT("Failed to create renderer")); }

	EI.Emitter->AddRenderer(NR, EI.Version);
	GEditor->EndTransaction();
	System->RequestCompile(false);

	int32 NewIdx = ED->GetRenderers().Num() - 1;
	TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
	R->SetNumberField(TEXT("renderer_index"), NewIdx);
	return SuccessObj(R);
}

FMonolithActionResult FMonolithNiagaraActions::HandleRemoveRenderer(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	int32 RendererIndex = static_cast<int32>(Params->GetNumberField(TEXT("renderer_index")));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	FVersionedNiagaraEmitterData* ED = nullptr;
	UNiagaraRendererProperties* Rend = GetRenderer(System, EmitterHandleId, RendererIndex, &ED);
	if (!Rend) return FMonolithActionResult::Error(TEXT("Renderer not found"));

	int32 EIdx = FindEmitterHandleIndex(System, EmitterHandleId);
	const FNiagaraEmitterHandle& Handle = System->GetEmitterHandles()[EIdx];
	FVersionedNiagaraEmitter EI = Handle.GetInstance();

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "RemRend", "Remove Renderer"));
	System->Modify();
	EI.Emitter->RemoveRenderer(Rend, EI.Version);
	GEditor->EndTransaction();
	System->RequestCompile(false);

	return SuccessStr(TEXT("Renderer removed"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleSetRendererMaterial(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	int32 RendererIndex = static_cast<int32>(Params->GetNumberField(TEXT("renderer_index")));
	FString MaterialPath = Params->GetStringField(TEXT("material"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UNiagaraRendererProperties* Rend = GetRenderer(System, EmitterHandleId, RendererIndex);
	if (!Rend) return FMonolithActionResult::Error(TEXT("Renderer not found"));

	UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
	if (!Mat) return FMonolithActionResult::Error(TEXT("Failed to load material"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "SetRendMat", "Set Renderer Material"));
	System->Modify();
	Rend->Modify();
	bool bOk = false;

	if (UNiagaraSpriteRendererProperties* S = Cast<UNiagaraSpriteRendererProperties>(Rend))
	{ S->Material = Mat; bOk = true; }
	else if (UNiagaraMeshRendererProperties* M = Cast<UNiagaraMeshRendererProperties>(Rend))
	{ M->bOverrideMaterials = true; M->OverrideMaterials.SetNum(1); M->OverrideMaterials[0].ExplicitMat = Mat; bOk = true; }
	else if (UNiagaraRibbonRendererProperties* Rib = Cast<UNiagaraRibbonRendererProperties>(Rend))
	{ Rib->Material = Mat; bOk = true; }
	else
	{
		FProperty* MP = Rend->GetClass()->FindPropertyByName(TEXT("Material"));
		if (FObjectProperty* OP = CastField<FObjectProperty>(MP))
		{ OP->SetObjectPropertyValue(OP->ContainerPtrToValuePtr<void>(Rend), Mat); bOk = true; }
	}

	GEditor->EndTransaction();
	if (bOk) System->RequestCompile(false);
	return bOk ? SuccessStr(TEXT("Material set")) : FMonolithActionResult::Error(TEXT("Unsupported renderer type"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleSetRendererProperty(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	int32 RendererIndex = static_cast<int32>(Params->GetNumberField(TEXT("renderer_index")));
	FString PropertyName = Params->GetStringField(TEXT("property"));
	FString ValueJson = Params->HasField(TEXT("value")) ? JsonValueToString(Params->TryGetField(TEXT("value"))) : FString();

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UNiagaraRendererProperties* Rend = GetRenderer(System, EmitterHandleId, RendererIndex);
	if (!Rend) return FMonolithActionResult::Error(TEXT("Renderer not found"));

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ValueJson);
	TSharedPtr<FJsonValue> JV;
	if (!FJsonSerializer::Deserialize(Reader, JV) || !JV.IsValid())
		return FMonolithActionResult::Error(TEXT("Failed to parse value"));

	FProperty* Prop = Rend->GetClass()->FindPropertyByName(FName(*PropertyName));
	if (!Prop) return FMonolithActionResult::Error(FString::Printf(TEXT("Property '%s' not found"), *PropertyName));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "SetRendProp", "Set Renderer Property"));
	System->Modify();
	Rend->Modify();

	void* Addr = Prop->ContainerPtrToValuePtr<void>(Rend);
	bool bOk = false;

	if (FFloatProperty* FP = CastField<FFloatProperty>(Prop)) { FP->SetPropertyValue(Addr, static_cast<float>(JV->AsNumber())); bOk = true; }
	else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop)) { DP->SetPropertyValue(Addr, JV->AsNumber()); bOk = true; }
	else if (FIntProperty* IP = CastField<FIntProperty>(Prop)) { IP->SetPropertyValue(Addr, static_cast<int32>(JV->AsNumber())); bOk = true; }
	else if (FBoolProperty* BP = CastField<FBoolProperty>(Prop)) { BP->SetPropertyValue(Addr, JV->AsBool()); bOk = true; }
	else if (FStrProperty* SP = CastField<FStrProperty>(Prop)) { SP->SetPropertyValue(Addr, JV->AsString()); bOk = true; }
	else if (FEnumProperty* EP = CastField<FEnumProperty>(Prop))
	{
		UEnum* E = EP->GetEnum();
		if (E)
		{
			int64 EV = E->GetValueByNameString(JV->AsString());
			if (EV == INDEX_NONE) EV = static_cast<int64>(JV->AsNumber());
			FNumericProperty* UP2 = EP->GetUnderlyingProperty();
			if (UP2) { UP2->SetIntPropertyValue(Addr, EV); bOk = true; }
		}
	}
	else if (FByteProperty* ByP = CastField<FByteProperty>(Prop))
	{
		if (ByP->Enum)
		{
			int64 EV = ByP->Enum->GetValueByNameString(JV->AsString());
			if (EV == INDEX_NONE) EV = static_cast<int64>(JV->AsNumber());
			ByP->SetPropertyValue(Addr, static_cast<uint8>(EV));
		}
		else ByP->SetPropertyValue(Addr, static_cast<uint8>(JV->AsNumber()));
		bOk = true;
	}
	else if (FObjectProperty* OP = CastField<FObjectProperty>(Prop))
	{
		UObject* Obj = LoadObject<UObject>(nullptr, *JV->AsString());
		if (Obj) { OP->SetObjectPropertyValue(Addr, Obj); bOk = true; }
	}
	else
	{
		bOk = Prop->ImportText_Direct(*JV->AsString(), Addr, Rend, PPF_None) != nullptr;
	}

	GEditor->EndTransaction();
	if (bOk) System->RequestCompile(false);
	return bOk ? SuccessStr(TEXT("Property set")) : FMonolithActionResult::Error(TEXT("Failed to set property"));
}

FMonolithActionResult FMonolithNiagaraActions::HandleGetRendererBindings(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	int32 RendererIndex = static_cast<int32>(Params->GetNumberField(TEXT("renderer_index")));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UNiagaraRendererProperties* Rend = GetRenderer(System, EmitterHandleId, RendererIndex);
	if (!Rend) return FMonolithActionResult::Error(TEXT("Renderer not found"));

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (TFieldIterator<FProperty> It(Rend->GetClass()); It; ++It)
	{
		FStructProperty* SP = CastField<FStructProperty>(*It);
		if (!SP || !SP->Struct->GetName().Contains(TEXT("Binding"))) continue;

		const void* Addr = SP->ContainerPtrToValuePtr<void>(Rend);
		TSharedRef<FJsonObject> BO = MakeShared<FJsonObject>();
		BO->SetStringField(TEXT("name"), (*It)->GetName());
		BO->SetStringField(TEXT("struct_type"), SP->Struct->GetName());
		FString Exported;
		SP->ExportTextItem_Direct(Exported, Addr, nullptr, Rend, PPF_None);
		BO->SetStringField(TEXT("value"), Exported);
		Arr.Add(MakeShared<FJsonValueObject>(BO));
	}
	return SuccessStr(JsonArrayToString(Arr));
}

FMonolithActionResult FMonolithNiagaraActions::HandleSetRendererBinding(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));
	int32 RendererIndex = static_cast<int32>(Params->GetNumberField(TEXT("renderer_index")));
	FString BindingName = Params->GetStringField(TEXT("binding_name"));
	FString AttributePath = Params->GetStringField(TEXT("attribute"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	UNiagaraRendererProperties* Rend = GetRenderer(System, EmitterHandleId, RendererIndex);
	if (!Rend) return FMonolithActionResult::Error(TEXT("Renderer not found"));

	FStructProperty* BP = nullptr;
	for (TFieldIterator<FProperty> It(Rend->GetClass()); It; ++It)
	{
		if ((*It)->GetName() == BindingName) { BP = CastField<FStructProperty>(*It); break; }
	}
	if (!BP) return FMonolithActionResult::Error(TEXT("Binding property not found"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "SetRendBind", "Set Renderer Binding"));
	System->Modify();
	Rend->Modify();

	void* Addr = BP->ContainerPtrToValuePtr<void>(Rend);
	FString ImportText = FString::Printf(TEXT("(BoundVariable=(Name=\"%s\"))"), *AttributePath);
	bool bOk = BP->ImportText_Direct(*ImportText, Addr, Rend, PPF_None) != nullptr;
	if (!bOk)
	{
		FString Fallback = FString::Printf(TEXT("(BoundVariable=(Name=\"%s\",TypeDefHandle=(RegisteredTypeIndex=-1)))"), *AttributePath);
		bOk = BP->ImportText_Direct(*Fallback, Addr, Rend, PPF_None) != nullptr;
	}

	GEditor->EndTransaction();
	if (bOk) System->RequestCompile(false);
	return bOk ? SuccessStr(TEXT("Binding set")) : FMonolithActionResult::Error(TEXT("Failed to set binding"));
}

// ============================================================================
// Batch Actions (2)
// ============================================================================

FMonolithActionResult FMonolithNiagaraActions::HandleBatchExecute(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString OpsJson = Params->GetStringField(TEXT("operations"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(OpsJson);
	TArray<TSharedPtr<FJsonValue>> Ops;
	if (!FJsonSerializer::Deserialize(Reader, Ops))
		return FMonolithActionResult::Error(TEXT("Failed to parse operations"));

	GEditor->BeginTransaction(NSLOCTEXT("Monolith", "BatchExec", "Batch Execute"));
	System->Modify();

	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Ok = 0, Fail = 0;

	for (int32 i = 0; i < Ops.Num(); ++i)
	{
		TSharedPtr<FJsonObject> Op = Ops[i]->AsObject();
		if (!Op.IsValid()) { Fail++; continue; }

		FString OpName = Op->GetStringField(TEXT("op"));
		TSharedRef<FJsonObject> RO = MakeShared<FJsonObject>();
		RO->SetNumberField(TEXT("index"), i);
		RO->SetStringField(TEXT("op"), OpName);

		// Delegate to individual handlers by constructing param objects
		TSharedRef<FJsonObject> SubParams = MakeShared<FJsonObject>();
		SubParams->SetStringField(TEXT("system_path"), SystemPath);

		// Copy all fields from Op to SubParams
		for (auto& Pair : Op->Values)
		{
			SubParams->SetField(Pair.Key, Pair.Value);
		}

		FMonolithActionResult SubResult = FMonolithActionResult::Error(TEXT("Unknown op"));

		if (OpName == TEXT("add_emitter")) SubResult = HandleAddEmitter(SubParams);
		else if (OpName == TEXT("remove_emitter")) SubResult = HandleRemoveEmitter(SubParams);
		else if (OpName == TEXT("add_module")) SubResult = HandleAddModule(SubParams);
		else if (OpName == TEXT("remove_module")) SubResult = HandleRemoveModule(SubParams);
		else if (OpName == TEXT("set_module_input")) SubResult = HandleSetModuleInputValue(SubParams);
		else if (OpName == TEXT("set_module_binding")) SubResult = HandleSetModuleInputBinding(SubParams);
		else if (OpName == TEXT("set_emitter_property")) SubResult = HandleSetEmitterProperty(SubParams);
		else if (OpName == TEXT("add_renderer")) SubResult = HandleAddRenderer(SubParams);
		else if (OpName == TEXT("remove_renderer")) SubResult = HandleRemoveRenderer(SubParams);
		else if (OpName == TEXT("set_renderer_material")) SubResult = HandleSetRendererMaterial(SubParams);
		else if (OpName == TEXT("set_renderer_property")) SubResult = HandleSetRendererProperty(SubParams);
		else if (OpName == TEXT("add_user_param")) SubResult = HandleAddUserParameter(SubParams);
		else if (OpName == TEXT("set_module_enabled")) SubResult = HandleSetModuleEnabled(SubParams);
		else if (OpName == TEXT("move_module")) SubResult = HandleMoveModule(SubParams);
		else if (OpName == TEXT("set_emitter_enabled")) SubResult = HandleSetEmitterEnabled(SubParams);

		RO->SetBoolField(TEXT("success"), SubResult.bSuccess);
		if (!SubResult.bSuccess) RO->SetStringField(TEXT("error"), SubResult.ErrorMessage);
		Results.Add(MakeShared<FJsonValueObject>(RO));
		if (SubResult.bSuccess) Ok++; else Fail++;
	}

	GEditor->EndTransaction();
	System->RequestCompile(false);

	TSharedRef<FJsonObject> Final = MakeShared<FJsonObject>();
	Final->SetBoolField(TEXT("success"), Fail == 0);
	Final->SetNumberField(TEXT("total"), Ops.Num());
	Final->SetNumberField(TEXT("succeeded"), Ok);
	Final->SetNumberField(TEXT("failed"), Fail);
	Final->SetArrayField(TEXT("results"), Results);
	return SuccessObj(Final);
}

FMonolithActionResult FMonolithNiagaraActions::HandleCreateSystemFromSpec(const TSharedPtr<FJsonObject>& Params)
{
	FString SpecJson = Params->GetStringField(TEXT("spec"));

	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(SpecJson);
	TSharedPtr<FJsonObject> Spec;
	if (!FJsonSerializer::Deserialize(Reader, Spec) || !Spec.IsValid())
		return FMonolithActionResult::Error(TEXT("Failed to parse spec"));

	FString SavePath = Spec->GetStringField(TEXT("save_path"));
	FString Template = Spec->HasField(TEXT("template")) ? Spec->GetStringField(TEXT("template")) : FString();
	if (SavePath.IsEmpty()) return FMonolithActionResult::Error(TEXT("save_path required"));

	// Create system
	TSharedRef<FJsonObject> CreateParams = MakeShared<FJsonObject>();
	CreateParams->SetStringField(TEXT("save_path"), SavePath);
	if (!Template.IsEmpty()) CreateParams->SetStringField(TEXT("template"), Template);
	FMonolithActionResult CreateResult = HandleCreateSystem(CreateParams);
	if (!CreateResult.bSuccess) return CreateResult;

	FString SystemPath = CreateResult.Result.IsValid() ? CreateResult.Result->GetStringField(TEXT("result")) : FString();
	int32 FailCount = 0;

	// Add user parameters
	if (Spec->HasField(TEXT("user_parameters")))
	{
		for (const TSharedPtr<FJsonValue>& PV : Spec->GetArrayField(TEXT("user_parameters")))
		{
			TSharedPtr<FJsonObject> PO = PV->AsObject();
			if (!PO) continue;
			TSharedRef<FJsonObject> AP = MakeShared<FJsonObject>();
			AP->SetStringField(TEXT("system_path"), SystemPath);
			AP->SetStringField(TEXT("name"), PO->GetStringField(TEXT("name")));
			AP->SetStringField(TEXT("type"), PO->GetStringField(TEXT("type")));
			if (PO->HasField(TEXT("default"))) AP->SetField(TEXT("default"), PO->TryGetField(TEXT("default")));
			if (!HandleAddUserParameter(AP).bSuccess) FailCount++;
		}
	}

	// Add emitters
	if (Spec->HasField(TEXT("emitters")))
	{
		for (const TSharedPtr<FJsonValue>& EV : Spec->GetArrayField(TEXT("emitters")))
		{
			TSharedPtr<FJsonObject> EO = EV->AsObject();
			if (!EO) continue;

			TSharedRef<FJsonObject> AEP = MakeShared<FJsonObject>();
			AEP->SetStringField(TEXT("system_path"), SystemPath);
			AEP->SetStringField(TEXT("emitter_asset"), EO->GetStringField(TEXT("asset")));
			if (EO->HasField(TEXT("name"))) AEP->SetStringField(TEXT("name"), EO->GetStringField(TEXT("name")));
			FMonolithActionResult AER = HandleAddEmitter(AEP);
			if (!AER.bSuccess) { FailCount++; continue; }

			// Parse handle_id from result
			FString EmitterId;
			if (AER.Result.IsValid())
				EmitterId = AER.Result->GetStringField(TEXT("handle_id"));
			if (EmitterId.IsEmpty()) continue;

			// Emitter properties
			if (EO->HasField(TEXT("properties")))
			{
				TSharedPtr<FJsonObject> Props = EO->GetObjectField(TEXT("properties"));
				for (auto& P : Props->Values)
				{
					TSharedRef<FJsonObject> SP = MakeShared<FJsonObject>();
					SP->SetStringField(TEXT("system_path"), SystemPath);
					SP->SetStringField(TEXT("emitter"), EmitterId);
					SP->SetStringField(TEXT("property"), P.Key);
					SP->SetField(TEXT("value"), P.Value);
					if (!HandleSetEmitterProperty(SP).bSuccess) FailCount++;
				}
			}

			// Modules
			if (EO->HasField(TEXT("modules")))
			{
				const TArray<TSharedPtr<FJsonValue>>& Mods = EO->GetArrayField(TEXT("modules"));
				for (int32 MI = 0; MI < Mods.Num(); ++MI)
				{
					TSharedPtr<FJsonObject> MO = Mods[MI]->AsObject();
					if (!MO) continue;
					TSharedRef<FJsonObject> AMP = MakeShared<FJsonObject>();
					AMP->SetStringField(TEXT("system_path"), SystemPath);
					AMP->SetStringField(TEXT("emitter"), EmitterId);
					AMP->SetStringField(TEXT("usage"), MO->GetStringField(TEXT("stage")));
					AMP->SetStringField(TEXT("module_script"), MO->GetStringField(TEXT("script")));
					AMP->SetNumberField(TEXT("index"), MO->HasField(TEXT("index")) ? MO->GetNumberField(TEXT("index")) : MI);
					FMonolithActionResult AMR = HandleAddModule(AMP);
					if (!AMR.bSuccess) { FailCount++; continue; }

					FString NodeGuid;
					if (AMR.Result.IsValid())
						NodeGuid = AMR.Result->GetStringField(TEXT("node_guid"));
					if (NodeGuid.IsEmpty()) continue;

					if (MO->HasField(TEXT("inputs")))
					{
						TSharedPtr<FJsonObject> Ins = MO->GetObjectField(TEXT("inputs"));
						for (auto& IP : Ins->Values)
						{
							TSharedRef<FJsonObject> SIP = MakeShared<FJsonObject>();
							SIP->SetStringField(TEXT("system_path"), SystemPath);
							SIP->SetStringField(TEXT("emitter"), EmitterId);
							SIP->SetStringField(TEXT("module_node"), NodeGuid);
							SIP->SetStringField(TEXT("input"), IP.Key);
							SIP->SetField(TEXT("value"), IP.Value);
							if (!HandleSetModuleInputValue(SIP).bSuccess) FailCount++;
						}
					}
					if (MO->HasField(TEXT("bindings")))
					{
						TSharedPtr<FJsonObject> Binds = MO->GetObjectField(TEXT("bindings"));
						for (auto& BP2 : Binds->Values)
						{
							TSharedRef<FJsonObject> SBP = MakeShared<FJsonObject>();
							SBP->SetStringField(TEXT("system_path"), SystemPath);
							SBP->SetStringField(TEXT("emitter"), EmitterId);
							SBP->SetStringField(TEXT("module_node"), NodeGuid);
							SBP->SetStringField(TEXT("input"), BP2.Key);
							SBP->SetStringField(TEXT("binding"), BP2.Value->AsString());
							if (!HandleSetModuleInputBinding(SBP).bSuccess) FailCount++;
						}
					}
				}
			}

			// Renderers
			if (EO->HasField(TEXT("renderers")))
			{
				for (const TSharedPtr<FJsonValue>& RV : EO->GetArrayField(TEXT("renderers")))
				{
					TSharedPtr<FJsonObject> RO = RV->AsObject();
					if (!RO) continue;
					TSharedRef<FJsonObject> ARP = MakeShared<FJsonObject>();
					ARP->SetStringField(TEXT("system_path"), SystemPath);
					ARP->SetStringField(TEXT("emitter"), EmitterId);
					ARP->SetStringField(TEXT("class"), RO->GetStringField(TEXT("class")));
					FMonolithActionResult ARR = HandleAddRenderer(ARP);
					if (!ARR.bSuccess) { FailCount++; continue; }

					int32 RIdx = -1;
					if (ARR.Result.IsValid())
						RIdx = static_cast<int32>(ARR.Result->GetNumberField(TEXT("renderer_index")));
					if (RIdx < 0) continue;

					if (RO->HasField(TEXT("material")))
					{
						TSharedRef<FJsonObject> SMP = MakeShared<FJsonObject>();
						SMP->SetStringField(TEXT("system_path"), SystemPath);
						SMP->SetStringField(TEXT("emitter"), EmitterId);
						SMP->SetNumberField(TEXT("renderer_index"), RIdx);
						SMP->SetStringField(TEXT("material"), RO->GetStringField(TEXT("material")));
						if (!HandleSetRendererMaterial(SMP).bSuccess) FailCount++;
					}
					if (RO->HasField(TEXT("properties")))
					{
						TSharedPtr<FJsonObject> RProps = RO->GetObjectField(TEXT("properties"));
						for (auto& RP : RProps->Values)
						{
							TSharedRef<FJsonObject> SRP = MakeShared<FJsonObject>();
							SRP->SetStringField(TEXT("system_path"), SystemPath);
							SRP->SetStringField(TEXT("emitter"), EmitterId);
							SRP->SetNumberField(TEXT("renderer_index"), RIdx);
							SRP->SetStringField(TEXT("property"), RP.Key);
							SRP->SetField(TEXT("value"), RP.Value);
							if (!HandleSetRendererProperty(SRP).bSuccess) FailCount++;
						}
					}
				}
			}
		}
	}

	// Final compile
	TSharedRef<FJsonObject> CP = MakeShared<FJsonObject>();
	CP->SetStringField(TEXT("system_path"), SystemPath);
	HandleRequestCompile(CP);

	TSharedRef<FJsonObject> Final = MakeShared<FJsonObject>();
	Final->SetBoolField(TEXT("success"), FailCount == 0);
	Final->SetStringField(TEXT("system_path"), SystemPath);
	Final->SetNumberField(TEXT("failed_steps"), FailCount);
	return SuccessObj(Final);
}

// ============================================================================
// DI Actions (1)
// ============================================================================

FMonolithActionResult FMonolithNiagaraActions::HandleGetDIFunctions(const TSharedPtr<FJsonObject>& Params)
{
	FString DIClassName = Params->GetStringField(TEXT("di_class"));

	FString ClassName = DIClassName;
	if (!ClassName.StartsWith(TEXT("U"))) ClassName = TEXT("U") + ClassName;
	if (!ClassName.Contains(TEXT("DataInterface"))) ClassName = TEXT("UNiagara") + DIClassName + TEXT("DataInterface");

	UClass* DIC = FindFirstObject<UClass>(*ClassName, EFindFirstObjectOptions::NativeFirst);
	if (!DIC) DIC = FindFirstObject<UClass>(*ClassName.Mid(1), EFindFirstObjectOptions::NativeFirst);
	if (!DIC) DIC = FindFirstObject<UClass>(*DIClassName, EFindFirstObjectOptions::NativeFirst);
	if (!DIC)
	{
		FString NP = TEXT("UNiagara") + DIClassName;
		DIC = FindFirstObject<UClass>(*NP, EFindFirstObjectOptions::NativeFirst);
		if (!DIC) DIC = FindFirstObject<UClass>(*NP.Mid(1), EFindFirstObjectOptions::NativeFirst);
	}
	if (!DIC || !DIC->IsChildOf(UNiagaraDataInterface::StaticClass()))
		return FMonolithActionResult::Error(TEXT("DI class not found"));

	UNiagaraDataInterface* TempDI = NewObject<UNiagaraDataInterface>(GetTransientPackage(), DIC);
	if (!TempDI) return FMonolithActionResult::Error(TEXT("Failed to create DI instance"));

	TArray<FNiagaraFunctionSignature> Sigs;
	TempDI->GetFunctionSignatures(Sigs);

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const FNiagaraFunctionSignature& Sig : Sigs)
	{
		TSharedRef<FJsonObject> SO = MakeShared<FJsonObject>();
		SO->SetStringField(TEXT("name"), Sig.Name.ToString());

		TArray<TSharedPtr<FJsonValue>> InsArr;
		for (const FNiagaraVariable& In : Sig.Inputs)
		{
			TSharedRef<FJsonObject> IO = MakeShared<FJsonObject>();
			IO->SetStringField(TEXT("name"), In.GetName().ToString());
			IO->SetStringField(TEXT("type"), In.GetType().GetName());
			InsArr.Add(MakeShared<FJsonValueObject>(IO));
		}
		SO->SetArrayField(TEXT("inputs"), InsArr);

		TArray<TSharedPtr<FJsonValue>> OutsArr;
		for (const FNiagaraVariableBase& Out : Sig.Outputs)
		{
			TSharedRef<FJsonObject> OO = MakeShared<FJsonObject>();
			OO->SetStringField(TEXT("name"), Out.GetName().ToString());
			OO->SetStringField(TEXT("type"), Out.GetType().GetName());
			OutsArr.Add(MakeShared<FJsonValueObject>(OO));
		}
		SO->SetArrayField(TEXT("outputs"), OutsArr);

		SO->SetBoolField(TEXT("requires_exec_pin"), Sig.bRequiresExecPin);
		SO->SetBoolField(TEXT("member_function"), Sig.bMemberFunction);
		SO->SetBoolField(TEXT("requires_context"), Sig.bRequiresContext);
		SO->SetBoolField(TEXT("supports_gpu"), Sig.bSupportsGPU);
		SO->SetBoolField(TEXT("supports_cpu"), Sig.bSupportsCPU);

		FText Desc = Sig.GetDescription();
		if (!Desc.IsEmpty()) SO->SetStringField(TEXT("description"), Desc.ToString());

		Arr.Add(MakeShared<FJsonValueObject>(SO));
	}
	return SuccessStr(JsonArrayToString(Arr));
}

// ============================================================================
// HLSL Actions (1)
// ============================================================================

FMonolithActionResult FMonolithNiagaraActions::HandleGetCompiledGPUHLSL(const TSharedPtr<FJsonObject>& Params)
{
	FString SystemPath = Params->GetStringField(TEXT("system_path"));
	FString EmitterHandleId = Params->GetStringField(TEXT("emitter"));

	UNiagaraSystem* System = LoadSystem(SystemPath);
	if (!System) return FMonolithActionResult::Error(TEXT("Failed to load system"));

	int32 EIdx = FindEmitterHandleIndex(System, EmitterHandleId);
	if (EIdx == INDEX_NONE) return FMonolithActionResult::Error(TEXT("Emitter not found"));

	FVersionedNiagaraEmitterData* ED = System->GetEmitterHandles()[EIdx].GetEmitterData();
	if (!ED) return FMonolithActionResult::Error(TEXT("No emitter data"));

	if (ED->SimTarget != ENiagaraSimTarget::GPUComputeSim)
		return FMonolithActionResult::Error(TEXT("Emitter is not GPU simulation"));

	UNiagaraScript* GPU = ED->GetGPUComputeScript();
	if (!GPU) return FMonolithActionResult::Error(TEXT("No GPU compute script"));

	if (System->HasOutstandingCompilationRequests())
		UE_LOG(LogMonolithNiagara, Warning, TEXT("System has outstanding compilation requests"));

	FString HLSL;
#if WITH_EDITORONLY_DATA
	const FNiagaraVMExecutableData& ExeData = GPU->GetVMExecutableData();
	if (!ExeData.LastHlslTranslationGPU.IsEmpty()) HLSL = ExeData.LastHlslTranslationGPU;
	else if (!ExeData.LastHlslTranslation.IsEmpty()) HLSL = ExeData.LastHlslTranslation;
	else if (!ExeData.LastAssemblyTranslation.IsEmpty()) HLSL = ExeData.LastAssemblyTranslation;
	else return FMonolithActionResult::Error(TEXT("No compiled HLSL available"));
#else
	return FMonolithActionResult::Error(TEXT("HLSL only available in editor builds"));
#endif

	return SuccessStr(HLSL);
}
