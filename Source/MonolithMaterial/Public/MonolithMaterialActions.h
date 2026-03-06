#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"

class UMaterial;
class UMaterialExpression;

/**
 * Material domain action handlers for Monolith.
 * Ported from MaterialMCPReaderLibrary — 14 proven actions.
 */
class FMonolithMaterialActions
{
public:
	/** Register all material actions with the tool registry */
	static void RegisterActions(FMonolithToolRegistry& Registry);

	// --- Action handlers (each takes JSON params, returns FMonolithActionResult) ---
	static FMonolithActionResult GetAllExpressions(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult GetExpressionDetails(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult GetFullConnectionGraph(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult DisconnectExpression(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult BuildMaterialGraph(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult BeginTransaction(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult EndTransaction(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult ExportMaterialGraph(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult ImportMaterialGraph(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult ValidateMaterial(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult RenderPreview(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult GetThumbnail(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult CreateCustomHLSLNode(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult GetLayerInfo(const TSharedPtr<FJsonObject>& Params);

private:
	/** Load a UMaterial from an asset path. Returns nullptr on failure. */
	static UMaterial* LoadBaseMaterial(const FString& AssetPath);

	/** Serialize a single expression node to JSON. */
	static TSharedPtr<FJsonObject> SerializeExpression(const UMaterialExpression* Expression);
};
