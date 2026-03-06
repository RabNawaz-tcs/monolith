## Task 4.3 — Asset Indexers (Blueprint, Material, Generic, Dependency)

**Files:**
- Create: `Source/MonolithIndex/Private/Indexers/BlueprintIndexer.h`
- Create: `Source/MonolithIndex/Private/Indexers/BlueprintIndexer.cpp`
- Create: `Source/MonolithIndex/Private/Indexers/MaterialIndexer.h`
- Create: `Source/MonolithIndex/Private/Indexers/MaterialIndexer.cpp`
- Create: `Source/MonolithIndex/Private/Indexers/GenericAssetIndexer.h`
- Create: `Source/MonolithIndex/Private/Indexers/GenericAssetIndexer.cpp`
- Create: `Source/MonolithIndex/Private/Indexers/DependencyIndexer.h`
- Create: `Source/MonolithIndex/Private/Indexers/DependencyIndexer.cpp`

### Step 1: Blueprint Indexer — walks UEdGraph nodes, pins, connections, variables

Create `Source/MonolithIndex/Private/Indexers/BlueprintIndexer.h`:

```cpp
#pragma once

#include "MonolithIndexer.h"

/**
 * Indexes Blueprints: graphs, nodes, pins, connections, variables.
 * Walks every UEdGraph in the Blueprint, extracts node topology,
 * pin connections, and variable declarations.
 */
class FBlueprintIndexer : public IMonolithIndexer
{
public:
	virtual TArray<FString> GetSupportedClasses() const override
	{
		return { TEXT("Blueprint"), TEXT("WidgetBlueprint"), TEXT("AnimBlueprint") };
	}

	virtual bool IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId) override;
	virtual FString GetName() const override { return TEXT("BlueprintIndexer"); }

private:
	void IndexGraph(class UEdGraph* Graph, FMonolithIndexDatabase& DB, int64 AssetId);
	void IndexVariables(class UBlueprint* Blueprint, FMonolithIndexDatabase& DB, int64 AssetId);
};
```

Create `Source/MonolithIndex/Private/Indexers/BlueprintIndexer.cpp`:

```cpp
#include "Indexers/BlueprintIndexer.h"
#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_Variable.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

bool FBlueprintIndexer::IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId)
{
	UBlueprint* Blueprint = Cast<UBlueprint>(LoadedAsset);
	if (!Blueprint) return false;

	// Update description with parent class info
	FString ParentClass = Blueprint->ParentClass ? Blueprint->ParentClass->GetName() : TEXT("None");

	// Index all graphs
	TArray<UEdGraph*> AllGraphs;
	Blueprint->GetAllGraphs(AllGraphs);

	for (UEdGraph* Graph : AllGraphs)
	{
		if (Graph)
		{
			IndexGraph(Graph, DB, AssetId);
		}
	}

	// Index variables
	IndexVariables(Blueprint, DB, AssetId);

	return true;
}

void FBlueprintIndexer::IndexGraph(UEdGraph* Graph, FMonolithIndexDatabase& DB, int64 AssetId)
{
	if (!Graph) return;

	// Map from UEdGraphNode* to DB node ID for connection resolution
	TMap<UEdGraphNode*, int64> NodeIdMap;

	// Index all nodes
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		FIndexedNode IndexedNode;
		IndexedNode.AssetId = AssetId;
		IndexedNode.NodeName = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		IndexedNode.NodeClass = Node->GetClass()->GetName();
		IndexedNode.PosX = Node->NodePosX;
		IndexedNode.PosY = Node->NodePosY;

		// Determine node type
		if (Cast<UK2Node_Event>(Node))
		{
			IndexedNode.NodeType = TEXT("Event");
		}
		else if (UK2Node_CallFunction* FuncNode = Cast<UK2Node_CallFunction>(Node))
		{
			IndexedNode.NodeType = TEXT("FunctionCall");
			// Build properties JSON with function reference
			auto PropsObj = MakeShared<FJsonObject>();
			PropsObj->SetStringField(TEXT("function"),
				FuncNode->FunctionReference.GetMemberName().ToString());
			if (FuncNode->FunctionReference.GetMemberParentClass())
			{
				PropsObj->SetStringField(TEXT("target_class"),
					FuncNode->FunctionReference.GetMemberParentClass()->GetName());
			}
			FString PropsStr;
			auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&PropsStr);
			FJsonSerializer::Serialize(PropsObj.ToSharedRef(), Writer);
			IndexedNode.Properties = PropsStr;
		}
		else if (Cast<UK2Node_Variable>(Node))
		{
			IndexedNode.NodeType = TEXT("Variable");
		}
		else
		{
			IndexedNode.NodeType = TEXT("Other");
		}

		int64 NodeId = DB.InsertNode(IndexedNode);
		if (NodeId >= 0)
		{
			NodeIdMap.Add(Node, NodeId);
		}
	}

	// Index connections by walking output pins
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;

		int64* SourceNodeId = NodeIdMap.Find(Node);
		if (!SourceNodeId) continue;

		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (!Pin || Pin->Direction != EGPD_Output) continue;

			for (UEdGraphPin* LinkedPin : Pin->LinkedTo)
			{
				if (!LinkedPin || !LinkedPin->GetOwningNode()) continue;

				int64* TargetNodeId = NodeIdMap.Find(LinkedPin->GetOwningNode());
				if (!TargetNodeId) continue;

				FIndexedConnection Conn;
				Conn.SourceNodeId = *SourceNodeId;
				Conn.SourcePin = Pin->PinName.ToString();
				Conn.TargetNodeId = *TargetNodeId;
				Conn.TargetPin = LinkedPin->PinName.ToString();

				// Pin type
				if (Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
				{
					Conn.PinType = TEXT("Exec");
				}
				else
				{
					Conn.PinType = Pin->PinType.PinCategory.ToString();
				}

				DB.InsertConnection(Conn);
			}
		}
	}
}

void FBlueprintIndexer::IndexVariables(UBlueprint* Blueprint, FMonolithIndexDatabase& DB, int64 AssetId)
{
	if (!Blueprint) return;

	for (const FBPVariableDescription& VarDesc : Blueprint->NewVariables)
	{
		FIndexedVariable Var;
		Var.AssetId = AssetId;
		Var.VarName = VarDesc.VarName.ToString();
		Var.VarType = VarDesc.VarType.PinCategory.ToString();
		Var.Category = VarDesc.Category.ToString();
		Var.DefaultValue = VarDesc.DefaultValue;

		// Check property flags
		Var.bIsExposed = VarDesc.PropertyFlags & CPF_ExposeOnSpawn ? true : false;
		Var.bIsReplicated = VarDesc.PropertyFlags & CPF_Net ? true : false;

		DB.InsertVariable(Var);
	}
}
```

### Step 2: Material Indexer — walks UMaterialExpression tree, connections, parameters

Create `Source/MonolithIndex/Private/Indexers/MaterialIndexer.h`:

```cpp
#pragma once

#include "MonolithIndexer.h"

/**
 * Indexes Materials and Material Instances: expression nodes,
 * connections, parameters (scalar, vector, texture).
 */
class FMaterialIndexer : public IMonolithIndexer
{
public:
	virtual TArray<FString> GetSupportedClasses() const override
	{
		return {
			TEXT("Material"),
			TEXT("MaterialInstanceConstant"),
			TEXT("MaterialFunction")
		};
	}

	virtual bool IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId) override;
	virtual FString GetName() const override { return TEXT("MaterialIndexer"); }

private:
	void IndexMaterialExpressions(class UMaterial* Material, FMonolithIndexDatabase& DB, int64 AssetId);
	void IndexMaterialInstance(class UMaterialInstanceConstant* MIC, FMonolithIndexDatabase& DB, int64 AssetId);
};
```

Create `Source/MonolithIndex/Private/Indexers/MaterialIndexer.cpp`:

```cpp
#include "Indexers/MaterialIndexer.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureObjectParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

bool FMaterialIndexer::IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId)
{
	if (UMaterial* Material = Cast<UMaterial>(LoadedAsset))
	{
		IndexMaterialExpressions(Material, DB, AssetId);
		return true;
	}

	if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(LoadedAsset))
	{
		IndexMaterialInstance(MIC, DB, AssetId);
		return true;
	}

	// MaterialFunction — also has expressions
	if (UMaterial* MatFunc = Cast<UMaterial>(LoadedAsset))
	{
		IndexMaterialExpressions(MatFunc, DB, AssetId);
		return true;
	}

	return false;
}

void FMaterialIndexer::IndexMaterialExpressions(UMaterial* Material, FMonolithIndexDatabase& DB, int64 AssetId)
{
	if (!Material) return;

	// Map expression -> DB node ID for connection tracking
	TMap<UMaterialExpression*, int64> ExpressionIdMap;

	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		if (!Expr) continue;

		FIndexedNode Node;
		Node.AssetId = AssetId;
		Node.NodeName = Expr->GetName();
		Node.NodeClass = Expr->GetClass()->GetName();
		Node.PosX = Expr->MaterialExpressionEditorX;
		Node.PosY = Expr->MaterialExpressionEditorY;

		// Classify expression type and extract parameter info
		if (UMaterialExpressionScalarParameter* ScalarParam = Cast<UMaterialExpressionScalarParameter>(Expr))
		{
			Node.NodeType = TEXT("ScalarParameter");

			// Also insert as parameter
			FIndexedParameter Param;
			Param.AssetId = AssetId;
			Param.ParamName = ScalarParam->ParameterName.ToString();
			Param.ParamType = TEXT("Scalar");
			Param.ParamGroup = ScalarParam->Group.ToString();
			Param.DefaultValue = FString::SanitizeFloat(ScalarParam->DefaultValue);
			Param.Source = TEXT("Material");
			DB.InsertParameter(Param);

			auto Props = MakeShared<FJsonObject>();
			Props->SetStringField(TEXT("parameter_name"), ScalarParam->ParameterName.ToString());
			Props->SetNumberField(TEXT("default_value"), ScalarParam->DefaultValue);
			FString PropsStr;
			auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&PropsStr);
			FJsonSerializer::Serialize(Props.ToSharedRef(), Writer);
			Node.Properties = PropsStr;
		}
		else if (UMaterialExpressionVectorParameter* VecParam = Cast<UMaterialExpressionVectorParameter>(Expr))
		{
			Node.NodeType = TEXT("VectorParameter");

			FIndexedParameter Param;
			Param.AssetId = AssetId;
			Param.ParamName = VecParam->ParameterName.ToString();
			Param.ParamType = TEXT("Vector");
			Param.ParamGroup = VecParam->Group.ToString();
			Param.DefaultValue = VecParam->DefaultValue.ToString();
			Param.Source = TEXT("Material");
			DB.InsertParameter(Param);
		}
		else if (UMaterialExpressionTextureObjectParameter* TexParam = Cast<UMaterialExpressionTextureObjectParameter>(Expr))
		{
			Node.NodeType = TEXT("TextureParameter");

			FIndexedParameter Param;
			Param.AssetId = AssetId;
			Param.ParamName = TexParam->ParameterName.ToString();
			Param.ParamType = TEXT("Texture");
			Param.ParamGroup = TexParam->Group.ToString();
			Param.DefaultValue = TexParam->Texture ? TexParam->Texture->GetPathName() : TEXT("");
			Param.Source = TEXT("Material");
			DB.InsertParameter(Param);
		}
		else if (UMaterialExpressionStaticBoolParameter* BoolParam = Cast<UMaterialExpressionStaticBoolParameter>(Expr))
		{
			Node.NodeType = TEXT("StaticBoolParameter");

			FIndexedParameter Param;
			Param.AssetId = AssetId;
			Param.ParamName = BoolParam->ParameterName.ToString();
			Param.ParamType = TEXT("StaticBool");
			Param.ParamGroup = BoolParam->Group.ToString();
			Param.DefaultValue = BoolParam->DefaultValue ? TEXT("true") : TEXT("false");
			Param.Source = TEXT("Material");
			DB.InsertParameter(Param);
		}
		else if (Cast<UMaterialExpressionFunctionInput>(Expr))
		{
			Node.NodeType = TEXT("FunctionInput");
		}
		else if (Cast<UMaterialExpressionFunctionOutput>(Expr))
		{
			Node.NodeType = TEXT("FunctionOutput");
		}
		else
		{
			Node.NodeType = TEXT("Expression");
		}

		int64 NodeId = DB.InsertNode(Node);
		if (NodeId >= 0)
		{
			ExpressionIdMap.Add(Expr, NodeId);
		}
	}

	// Index connections between expressions
	for (UMaterialExpression* Expr : Material->GetExpressions())
	{
		if (!Expr) continue;

		int64* TargetNodeId = ExpressionIdMap.Find(Expr);
		if (!TargetNodeId) continue;

		// Walk inputs — each input may reference another expression's output
		for (int32 InputIdx = 0; InputIdx < Expr->GetInputs().Num(); ++InputIdx)
		{
			FExpressionInput* Input = &Expr->GetInputs()[InputIdx];
			if (!Input || !Input->Expression) continue;

			int64* SourceNodeId = ExpressionIdMap.Find(Input->Expression);
			if (!SourceNodeId) continue;

			FIndexedConnection Conn;
			Conn.SourceNodeId = *SourceNodeId;
			Conn.SourcePin = FString::Printf(TEXT("Output_%d"), Input->OutputIndex);
			Conn.TargetNodeId = *TargetNodeId;
			Conn.TargetPin = FString::Printf(TEXT("Input_%d"), InputIdx);
			Conn.PinType = TEXT("Material");

			DB.InsertConnection(Conn);
		}
	}
}

void FMaterialIndexer::IndexMaterialInstance(UMaterialInstanceConstant* MIC, FMonolithIndexDatabase& DB, int64 AssetId)
{
	if (!MIC) return;

	// Index scalar parameter overrides
	for (const FScalarParameterValue& ScalarParam : MIC->ScalarParameterValues)
	{
		FIndexedParameter Param;
		Param.AssetId = AssetId;
		Param.ParamName = ScalarParam.ParameterInfo.Name.ToString();
		Param.ParamType = TEXT("Scalar");
		Param.DefaultValue = FString::SanitizeFloat(ScalarParam.ParameterValue);
		Param.Source = TEXT("MaterialInstance");
		DB.InsertParameter(Param);
	}

	// Index vector parameter overrides
	for (const FVectorParameterValue& VecParam : MIC->VectorParameterValues)
	{
		FIndexedParameter Param;
		Param.AssetId = AssetId;
		Param.ParamName = VecParam.ParameterInfo.Name.ToString();
		Param.ParamType = TEXT("Vector");
		Param.DefaultValue = VecParam.ParameterValue.ToString();
		Param.Source = TEXT("MaterialInstance");
		DB.InsertParameter(Param);
	}

	// Index texture parameter overrides
	for (const FTextureParameterValue& TexParam : MIC->TextureParameterValues)
	{
		FIndexedParameter Param;
		Param.AssetId = AssetId;
		Param.ParamName = TexParam.ParameterInfo.Name.ToString();
		Param.ParamType = TEXT("Texture");
		Param.DefaultValue = TexParam.ParameterValue ? TexParam.ParameterValue->GetPathName() : TEXT("");
		Param.Source = TEXT("MaterialInstance");
		DB.InsertParameter(Param);
	}
}
```

### Step 3: Generic Asset Indexer — StaticMesh, SkeletalMesh, Texture, Sound metadata

Create `Source/MonolithIndex/Private/Indexers/GenericAssetIndexer.h`:

```cpp
#pragma once

#include "MonolithIndexer.h"

/**
 * Indexes generic asset types that don't need deep graph inspection:
 * StaticMesh, SkeletalMesh, Texture2D, SoundWave, SoundCue, etc.
 * Captures metadata (poly count, texture size, audio duration, etc.)
 */
class FGenericAssetIndexer : public IMonolithIndexer
{
public:
	virtual TArray<FString> GetSupportedClasses() const override
	{
		return {
			TEXT("StaticMesh"),
			TEXT("SkeletalMesh"),
			TEXT("Texture2D"),
			TEXT("TextureCube"),
			TEXT("SoundWave"),
			TEXT("SoundCue"),
			TEXT("PhysicsAsset"),
			TEXT("Skeleton")
		};
	}

	virtual bool IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId) override;
	virtual FString GetName() const override { return TEXT("GenericAssetIndexer"); }
};
```

Create `Source/MonolithIndex/Private/Indexers/GenericAssetIndexer.cpp`:

```cpp
#include "Indexers/GenericAssetIndexer.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Sound/SoundWave.h"
#include "Sound/SoundCue.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

bool FGenericAssetIndexer::IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId)
{
	if (!LoadedAsset) return false;

	// We store metadata as a node of type "Metadata" with properties JSON
	FIndexedNode MetaNode;
	MetaNode.AssetId = AssetId;
	MetaNode.NodeType = TEXT("Metadata");
	MetaNode.NodeName = LoadedAsset->GetName();
	MetaNode.NodeClass = LoadedAsset->GetClass()->GetName();

	auto Props = MakeShared<FJsonObject>();

	if (UStaticMesh* SM = Cast<UStaticMesh>(LoadedAsset))
	{
		if (SM->GetRenderData() && SM->GetRenderData()->LODResources.Num() > 0)
		{
			const FStaticMeshLODResources& LOD0 = SM->GetRenderData()->LODResources[0];
			Props->SetNumberField(TEXT("triangles"), LOD0.GetNumTriangles());
			Props->SetNumberField(TEXT("vertices"), LOD0.GetNumVertices());
			Props->SetNumberField(TEXT("sections"), LOD0.Sections.Num());
		}
		Props->SetNumberField(TEXT("lod_count"), SM->GetNumLODs());
		Props->SetNumberField(TEXT("material_slots"), SM->GetStaticMaterials().Num());

		// Bounds
		FBoxSphereBounds Bounds = SM->GetBounds();
		Props->SetStringField(TEXT("bounds_extent"),
			FString::Printf(TEXT("%.1f x %.1f x %.1f"),
				Bounds.BoxExtent.X * 2, Bounds.BoxExtent.Y * 2, Bounds.BoxExtent.Z * 2));

		// Collision
		Props->SetBoolField(TEXT("has_collision"), SM->GetBodySetup() != nullptr);
	}
	else if (USkeletalMesh* SK = Cast<USkeletalMesh>(LoadedAsset))
	{
		Props->SetNumberField(TEXT("lod_count"), SK->GetLODNum());
		Props->SetNumberField(TEXT("material_slots"), SK->GetMaterials().Num());

		if (SK->GetSkeleton())
		{
			Props->SetNumberField(TEXT("bone_count"), SK->GetSkeleton()->GetReferenceSkeleton().GetNum());
			Props->SetStringField(TEXT("skeleton"), SK->GetSkeleton()->GetPathName());
		}

		if (SK->GetPhysicsAsset())
		{
			Props->SetStringField(TEXT("physics_asset"), SK->GetPhysicsAsset()->GetPathName());
		}
	}
	else if (UTexture2D* Tex = Cast<UTexture2D>(LoadedAsset))
	{
		Props->SetNumberField(TEXT("width"), Tex->GetSizeX());
		Props->SetNumberField(TEXT("height"), Tex->GetSizeY());
		Props->SetStringField(TEXT("format"), GPixelFormats[Tex->GetPixelFormat()].Name);
		Props->SetNumberField(TEXT("mip_count"), Tex->GetNumMips());
		Props->SetBoolField(TEXT("srgb"), Tex->SRGB);
		Props->SetBoolField(TEXT("has_alpha"), Tex->HasAlphaChannel());
		Props->SetStringField(TEXT("compression"),
			UEnum::GetValueAsString(Tex->CompressionSettings));
		Props->SetStringField(TEXT("lod_group"),
			UEnum::GetValueAsString(Tex->LODGroup));
	}
	else if (USoundWave* Sound = Cast<USoundWave>(LoadedAsset))
	{
		Props->SetNumberField(TEXT("duration"), Sound->Duration);
		Props->SetNumberField(TEXT("sample_rate"), Sound->GetSampleRateForCurrentPlatform());
		Props->SetNumberField(TEXT("channels"), Sound->NumChannels);
		Props->SetBoolField(TEXT("looping"), Sound->bLooping);
	}

	// Serialize properties to JSON string
	FString PropsStr;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&PropsStr);
	FJsonSerializer::Serialize(Props.ToSharedRef(), Writer);
	MetaNode.Properties = PropsStr;

	DB.InsertNode(MetaNode);
	return true;
}
```

### Step 4: Dependency Indexer — Asset Registry dependency graph

Create `Source/MonolithIndex/Private/Indexers/DependencyIndexer.h`:

```cpp
#pragma once

#include "MonolithIndexer.h"

/**
 * Indexes the Asset Registry dependency graph.
 * Runs after all other indexers (needs all assets in DB).
 * Uses special class name "__Dependencies__" for dispatch.
 */
class FDependencyIndexer : public IMonolithIndexer
{
public:
	virtual TArray<FString> GetSupportedClasses() const override
	{
		return { TEXT("__Dependencies__") };
	}

	virtual bool IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId) override;
	virtual FString GetName() const override { return TEXT("DependencyIndexer"); }
};
```

Create `Source/MonolithIndex/Private/Indexers/DependencyIndexer.cpp`:

```cpp
#include "Indexers/DependencyIndexer.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

bool FDependencyIndexer::IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId)
{
	// This indexer ignores the individual asset params — it processes ALL assets at once
	IAssetRegistry& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	// Get all assets we've indexed
	TArray<FAssetData> AllAssets;
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(TEXT("/Game")));
	Filter.bRecursivePaths = true;
	Registry.GetAssets(Filter, AllAssets);

	int32 DepsInserted = 0;

	for (const FAssetData& Source : AllAssets)
	{
		int64 SourceId = DB.GetAssetId(Source.PackageName.ToString());
		if (SourceId < 0) continue;

		// Get hard dependencies
		TArray<FAssetIdentifier> HardDeps;
		Registry.GetDependencies(Source.PackageName, HardDeps,
			UE::AssetRegistry::EDependencyCategory::Package,
			UE::AssetRegistry::EDependencyQuery::Hard);

		for (const FAssetIdentifier& Dep : HardDeps)
		{
			FString DepPath = Dep.PackageName.ToString();
			// Only index project-internal deps
			if (!DepPath.StartsWith(TEXT("/Game/"))) continue;

			int64 TargetId = DB.GetAssetId(DepPath);
			if (TargetId < 0) continue;

			FIndexedDependency IndexedDep;
			IndexedDep.SourceAssetId = SourceId;
			IndexedDep.TargetAssetId = TargetId;
			IndexedDep.DependencyType = TEXT("Hard");
			DB.InsertDependency(IndexedDep);
			DepsInserted++;
		}

		// Get soft dependencies
		TArray<FAssetIdentifier> SoftDeps;
		Registry.GetDependencies(Source.PackageName, SoftDeps,
			UE::AssetRegistry::EDependencyCategory::Package,
			UE::AssetRegistry::EDependencyQuery::Soft);

		for (const FAssetIdentifier& Dep : SoftDeps)
		{
			FString DepPath = Dep.PackageName.ToString();
			if (!DepPath.StartsWith(TEXT("/Game/"))) continue;

			int64 TargetId = DB.GetAssetId(DepPath);
			if (TargetId < 0) continue;

			FIndexedDependency IndexedDep;
			IndexedDep.SourceAssetId = SourceId;
			IndexedDep.TargetAssetId = TargetId;
			IndexedDep.DependencyType = TEXT("Soft");
			DB.InsertDependency(IndexedDep);
			DepsInserted++;
		}
	}

	UE_LOG(LogMonolithIndex, Log, TEXT("DependencyIndexer: inserted %d dependency edges"), DepsInserted);
	return true;
}
```

### Step 5: Verify compilation

```
# Build MonolithIndex module
# Expected: compiles with all 4 indexers, no errors
```

**Commit:** `feat(index): Add Blueprint, Material, Generic, and Dependency indexers`

---
