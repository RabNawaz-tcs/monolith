#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"

class UAnimMontage;
class UBlendSpace;
class UAnimBlueprint;
class UAnimSequence;
class USkeleton;
class USkeletalMesh;

/**
 * Animation domain action handlers for Monolith.
 * Ported from AnimationMCPReaderLibrary — 23 proven actions.
 */
class FMonolithAnimationActions
{
public:
	/** Register all animation actions with the tool registry */
	static void RegisterActions(FMonolithToolRegistry& Registry);

	// --- Montage Sections (4) ---
	static FMonolithActionResult HandleAddMontageSection(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleDeleteMontageSection(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetSectionNext(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetSectionTime(const TSharedPtr<FJsonObject>& Params);

	// --- BlendSpace Samples (3) ---
	static FMonolithActionResult HandleAddBlendSpaceSample(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleEditBlendSpaceSample(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleDeleteBlendSpaceSample(const TSharedPtr<FJsonObject>& Params);

	// --- ABP Graph Reading (7) ---
	static FMonolithActionResult HandleGetStateMachines(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetStateInfo(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetTransitions(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetBlendNodes(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetLinkedLayers(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetGraphs(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetNodes(const TSharedPtr<FJsonObject>& Params);

	// --- Notify Editing (2) ---
	static FMonolithActionResult HandleSetNotifyTime(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleSetNotifyDuration(const TSharedPtr<FJsonObject>& Params);

	// --- Bone Tracks (3) ---
	static FMonolithActionResult HandleSetBoneTrackKeys(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleAddBoneTrack(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleRemoveBoneTrack(const TSharedPtr<FJsonObject>& Params);

	// --- Virtual Bones (2) ---
	static FMonolithActionResult HandleAddVirtualBone(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleRemoveVirtualBones(const TSharedPtr<FJsonObject>& Params);

	// --- Skeleton Info (2) ---
	static FMonolithActionResult HandleGetSkeletonInfo(const TSharedPtr<FJsonObject>& Params);
	static FMonolithActionResult HandleGetSkeletalMeshInfo(const TSharedPtr<FJsonObject>& Params);
};
