# Phase 3: MonolithAnimation + MonolithNiagara

**The two largest modules.** Animation has 62 actions, Niagara has 70. Both port existing C++ plugins and absorb their Python MCP server counterparts.

---

## Task 3.1 — MonolithAnimation Module (62 actions)

### What We're Porting

**From AnimationMCPReader C++ plugin (23 UFUNCTIONs):**

| Category | Functions | Source |
|---|---|---|
| Montage Sections | `AddMontageSection`, `DeleteMontageSection`, `SetSectionNext`, `SetSectionTime` | AnimationMCPReaderLibrary.h:14-24 |
| BlendSpace Samples | `AddBlendSpaceSample`, `EditBlendSpaceSample`, `DeleteBlendSpaceSample` | AnimationMCPReaderLibrary.h:27-34 |
| ABP Graph Reading | `GetStateMachines`, `GetStateInfo`, `GetTransitions`, `GetBlendNodes`, `GetLinkedLayers`, `GetGraphs`, `GetNodes` | AnimationMCPReaderLibrary.h:37-56 |
| Notify Editing | `SetNotifyTime`, `SetNotifyDuration` | AnimationMCPReaderLibrary.h:59-63 |
| Virtual Bones | `AddVirtualBone`, `RemoveVirtualBones` | AnimationMCPReaderLibrary.h:67-70 |
| Skeleton Info | `GetSkeletonInfo`, `GetSkeletalMeshInfo` | AnimationMCPReaderLibrary.h:73-77 |
| Bone Tracks | `SetBoneTrackKeys`, `AddBoneTrack`, `RemoveBoneTrack` | AnimationMCPReaderLibrary.h:81-87 |

**From unreal-animation-mcp Python server (39 additional tools):**

| Category | Action Names | Count |
|---|---|---|
| Sequence Inspection | `list_sequences`, `get_sequence_info`, `get_sequence_curves`, `get_sequence_notifies`, `get_sequence_metadata` | 5 |
| Sequence Creation | `create_sequence`, `duplicate_sequence`, `set_sequence_length`, `set_sequence_rate_scale` | 4 |
| Montage Operations | `list_montages`, `get_montage_info`, `create_montage`, `set_montage_blend_in`, `set_montage_blend_out`, `add_montage_slot`, `set_montage_slot` | 7 |
| BlendSpace Operations | `list_blend_spaces`, `get_blend_space_info`, `create_blend_space`, `create_blend_space_1d`, `set_blend_space_axis` | 5 |
| ABP Operations | `list_anim_blueprints`, `get_abp_info`, `create_anim_blueprint`, `set_abp_skeleton` | 4 |
| Notify Management | `list_notifies`, `add_notify`, `add_notify_state`, `remove_notify`, `set_notify_track` | 5 |
| Curve Operations | `list_curves`, `add_curve`, `remove_curve`, `set_curve_keys`, `get_curve_keys` | 5 |
| Skeleton Operations | `list_skeletons`, `get_bone_tree`, `add_socket`, `remove_socket` | 4 |

**Total: 23 ported + 39 new = 62 actions**

### Files

**Create:**
- `Source/MonolithAnimation/MonolithAnimation.Build.cs`
- `Source/MonolithAnimation/Public/MonolithAnimationModule.h`
- `Source/MonolithAnimation/Private/MonolithAnimationModule.cpp`
- `Source/MonolithAnimation/Public/MonolithAnimationActions.h`
- `Source/MonolithAnimation/Private/MonolithAnimationActions.cpp`

### Step 1: Build.cs

**File:** `Source/MonolithAnimation/MonolithAnimation.Build.cs`

```csharp
using UnrealBuildTool;

public class MonolithAnimation : ModuleRules
{
    public MonolithAnimation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "MonolithCore",
            "UnrealEd",
            "AnimGraph",
            "AnimGraphRuntime",
            "BlueprintGraph",
            "Json",
            "JsonUtilities",
            "Persona"
        });
    }
}
```

### Step 2: Module Header & Cpp

**File:** `Source/MonolithAnimation/Public/MonolithAnimationModule.h`

```cpp
#pragma once

#include "Modules/ModuleManager.h"

class FMonolithAnimationModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
```

**File:** `Source/MonolithAnimation/Private/MonolithAnimationModule.cpp`

```cpp
#include "MonolithAnimationModule.h"
#include "MonolithAnimationActions.h"

#define LOCTEXT_NAMESPACE "FMonolithAnimationModule"

void FMonolithAnimationModule::StartupModule()
{
    FMonolithAnimationActions::RegisterActions();
    UE_LOG(LogTemp, Log, TEXT("Monolith — Animation module loaded (62 actions)"));
}

void FMonolithAnimationModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithAnimationModule, MonolithAnimation)
```

### Step 3: Actions Header

**File:** `Source/MonolithAnimation/Public/MonolithAnimationActions.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UAnimSequence;
class UAnimMontage;
class UBlendSpace;
class UAnimBlueprint;
class USkeleton;
class USkeletalMesh;

/**
 * All 62 animation actions dispatched via animation.query(action, params).
 *
 * Ported from: AnimationMCPReader (23 UFUNCTIONs) + unreal-animation-mcp (39 Python tools)
 *
 * Categories:
 *   Sequence (9): list, info, curves, notifies, metadata, create, duplicate, set_length, set_rate_scale
 *   Montage (11): list, info, create, blend_in, blend_out, add_slot, set_slot, add_section, delete_section, set_section_next, set_section_time
 *   BlendSpace (8): list, info, create, create_1d, set_axis, add_sample, edit_sample, delete_sample
 *   ABP (9): list, info, create, set_skeleton, get_state_machines, get_state_info, get_transitions, get_blend_nodes, get_linked_layers, get_graphs, get_nodes
 *   Notify (7): list, add, add_state, remove, set_track, set_time, set_duration
 *   Curve (5): list, add, remove, set_keys, get_keys
 *   Skeleton (8): list, info, mesh_info, bone_tree, add_socket, remove_socket, add_virtual_bone, remove_virtual_bones
 *   BoneTrack (3): add, remove, set_keys
 */
class MONOLITHANIMATION_API FMonolithAnimationActions
{
public:
    /** Register all 62 actions with MonolithCore's ToolRegistry. */
    static void RegisterActions();

private:
    // ── Dispatch Entry Point ────────────────────────────────────────────
    static TSharedPtr<FJsonObject> Dispatch(const FString& Action, const TSharedPtr<FJsonObject>& Params);

    // ── Sequence Actions (9) ────────────────────────────────────────────
    static TSharedPtr<FJsonObject> ListSequences(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetSequenceInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetSequenceCurves(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetSequenceNotifies(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetSequenceMetadata(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> CreateSequence(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> DuplicateSequence(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetSequenceLength(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetSequenceRateScale(const TSharedPtr<FJsonObject>& Params);

    // ── Montage Actions (11) ────────────────────────────────────────────
    static TSharedPtr<FJsonObject> ListMontages(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetMontageInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> CreateMontage(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetMontageBlendIn(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetMontageBlendOut(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> AddMontageSlot(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetMontageSlot(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> AddMontageSection(const TSharedPtr<FJsonObject>& Params);   // PORT from AnimationMCPReader
    static TSharedPtr<FJsonObject> DeleteMontageSection(const TSharedPtr<FJsonObject>& Params); // PORT
    static TSharedPtr<FJsonObject> SetSectionNext(const TSharedPtr<FJsonObject>& Params);       // PORT
    static TSharedPtr<FJsonObject> SetSectionTime(const TSharedPtr<FJsonObject>& Params);       // PORT

    // ── BlendSpace Actions (8) ──────────────────────────────────────────
    static TSharedPtr<FJsonObject> ListBlendSpaces(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetBlendSpaceInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> CreateBlendSpace(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> CreateBlendSpace1D(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetBlendSpaceAxis(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> AddBlendSpaceSample(const TSharedPtr<FJsonObject>& Params);   // PORT
    static TSharedPtr<FJsonObject> EditBlendSpaceSample(const TSharedPtr<FJsonObject>& Params);  // PORT
    static TSharedPtr<FJsonObject> DeleteBlendSpaceSample(const TSharedPtr<FJsonObject>& Params); // PORT

    // ── ABP Actions (11) ────────────────────────────────────────────────
    static TSharedPtr<FJsonObject> ListAnimBlueprints(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetAbpInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> CreateAnimBlueprint(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetAbpSkeleton(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetStateMachines(const TSharedPtr<FJsonObject>& Params);       // PORT
    static TSharedPtr<FJsonObject> GetStateInfo(const TSharedPtr<FJsonObject>& Params);            // PORT
    static TSharedPtr<FJsonObject> GetTransitions(const TSharedPtr<FJsonObject>& Params);          // PORT
    static TSharedPtr<FJsonObject> GetBlendNodes(const TSharedPtr<FJsonObject>& Params);           // PORT
    static TSharedPtr<FJsonObject> GetLinkedLayers(const TSharedPtr<FJsonObject>& Params);         // PORT
    static TSharedPtr<FJsonObject> GetGraphs(const TSharedPtr<FJsonObject>& Params);               // PORT
    static TSharedPtr<FJsonObject> GetNodes(const TSharedPtr<FJsonObject>& Params);                // PORT

    // ── Notify Actions (7) ──────────────────────────────────────────────
    static TSharedPtr<FJsonObject> ListNotifies(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> AddNotify(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> AddNotifyState(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> RemoveNotify(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetNotifyTrack(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetNotifyTime(const TSharedPtr<FJsonObject>& Params);       // PORT
    static TSharedPtr<FJsonObject> SetNotifyDuration(const TSharedPtr<FJsonObject>& Params);   // PORT

    // ── Curve Actions (5) ───────────────────────────────────────────────
    static TSharedPtr<FJsonObject> ListCurves(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> AddCurve(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> RemoveCurve(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetCurveKeys(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetCurveKeys(const TSharedPtr<FJsonObject>& Params);

    // ── Skeleton Actions (8) ────────────────────────────────────────────
    static TSharedPtr<FJsonObject> ListSkeletons(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetSkeletonInfo(const TSharedPtr<FJsonObject>& Params);     // PORT
    static TSharedPtr<FJsonObject> GetSkeletalMeshInfo(const TSharedPtr<FJsonObject>& Params); // PORT
    static TSharedPtr<FJsonObject> GetBoneTree(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> AddSocket(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> RemoveSocket(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> AddVirtualBone(const TSharedPtr<FJsonObject>& Params);       // PORT
    static TSharedPtr<FJsonObject> RemoveVirtualBones(const TSharedPtr<FJsonObject>& Params);   // PORT

    // ── BoneTrack Actions (3) ───────────────────────────────────────────
    static TSharedPtr<FJsonObject> AddBoneTrack(const TSharedPtr<FJsonObject>& Params);         // PORT
    static TSharedPtr<FJsonObject> RemoveBoneTrack(const TSharedPtr<FJsonObject>& Params);      // PORT
    static TSharedPtr<FJsonObject> SetBoneTrackKeys(const TSharedPtr<FJsonObject>& Params);     // PORT

    // ── Asset Loaders (shared) ──────────────────────────────────────────
    static UAnimSequence* LoadAnimSequence(const FString& AssetPath);
    static UAnimMontage* LoadMontage(const FString& AssetPath);
    static UBlendSpace* LoadBlendSpace(const FString& AssetPath);
    static UAnimBlueprint* LoadAnimBlueprint(const FString& AssetPath);
    static USkeleton* LoadSkeleton(const FString& AssetPath);
    static USkeletalMesh* LoadSkeletalMesh(const FString& AssetPath);
};
```

### Step 4: Actions Implementation — Registration & Dispatch

**File:** `Source/MonolithAnimation/Private/MonolithAnimationActions.cpp`

This is the core file. It's large, so we structure it with clear sections.

```cpp
#include "MonolithAnimationActions.h"

// MonolithCore shared utilities (replaces per-plugin ErrorJson/SuccessJson)
// #include "MonolithToolRegistry.h"  // FMonolithToolRegistry
// #include "MonolithJsonUtils.h"     // FMonolithJsonUtils::ErrorJson/SuccessJson

// UE Animation headers — same as AnimationMCPReaderLibrary.cpp
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/BlendSpace1D.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimBlueprintGeneratedClass.h"
#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "AnimGraphNode_Base.h"
#include "AnimGraphNode_StateMachine.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimationStateMachineGraph.h"
#include "AnimStateNode.h"
#include "AnimStateTransitionNode.h"
#include "AnimStateEntryNode.h"

// Asset creation
#include "AssetToolsModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/BlendSpaceFactoryNew.h"
#include "Factories/BlendSpaceFactory1D.h"
#include "Factories/AnimBlueprintFactory.h"

// JSON
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Editor.h"

// ─────────────────────────────────────────────────────────────────────────────
// Registration
// ─────────────────────────────────────────────────────────────────────────────

void FMonolithAnimationActions::RegisterActions()
{
    // NOTE: FMonolithToolRegistry is defined in MonolithCore (Phase 1).
    // Registration pattern: Registry.Register("animation", "action_name", Description, ParamSchema, &Handler)
    // The dispatch function maps action strings to handlers.
    //
    // For now, register the namespace dispatcher:
    // FMonolithToolRegistry::Get().RegisterNamespace(
    //     TEXT("animation"),
    //     TEXT("Animation sequences, montages, blend spaces, ABPs, skeletons"),
    //     &FMonolithAnimationActions::Dispatch
    // );
    //
    // Action discovery metadata (returned by monolith.discover("animation")):

    // --- Sequence (9) ---
    // list_sequences       — List animation sequences, optional path filter
    //   params: { path_filter?: string, skeleton_filter?: string }
    // get_sequence_info    — Detailed info for an animation sequence
    //   params: { asset_path: string }
    // get_sequence_curves  — List all curves in a sequence
    //   params: { asset_path: string }
    // get_sequence_notifies — List all notifies in a sequence
    //   params: { asset_path: string }
    // get_sequence_metadata — Get metadata (length, rate, skeleton, etc.)
    //   params: { asset_path: string }
    // create_sequence      — Create a new animation sequence
    //   params: { save_path: string, skeleton_path: string, length?: float }
    // duplicate_sequence   — Duplicate an existing sequence
    //   params: { source_path: string, dest_path: string }
    // set_sequence_length  — Set the play length of a sequence
    //   params: { asset_path: string, length: float }
    // set_sequence_rate_scale — Set the rate scale multiplier
    //   params: { asset_path: string, rate_scale: float }

    // --- Montage (11) ---
    // list_montages        — List montage assets
    //   params: { path_filter?: string }
    // get_montage_info     — Detailed montage inspection
    //   params: { asset_path: string }
    // create_montage       — Create a montage from a sequence
    //   params: { save_path: string, sequence_path: string }
    // set_montage_blend_in — Set blend in time/curve
    //   params: { asset_path: string, blend_time: float, blend_option?: string }
    // set_montage_blend_out — Set blend out time/curve
    //   params: { asset_path: string, blend_time: float, blend_option?: string }
    // add_montage_slot     — Add a slot track to a montage
    //   params: { asset_path: string, slot_name: string }
    // set_montage_slot     — Set the slot group for a track
    //   params: { asset_path: string, track_index: int, slot_name: string }
    // add_montage_section  — Add a named section at a time [PORTED]
    //   params: { asset_path: string, section_name: string, start_time: float }
    // delete_montage_section — Remove a section by index [PORTED]
    //   params: { asset_path: string, section_index: int }
    // set_section_next     — Link section to next section [PORTED]
    //   params: { asset_path: string, section_name: string, next_section_name: string }
    // set_section_time     — Move a section's start time [PORTED]
    //   params: { asset_path: string, section_name: string, new_time: float }

    // --- BlendSpace (8) ---
    // list_blend_spaces    — List blend space assets
    //   params: { path_filter?: string }
    // get_blend_space_info — Inspect blend space (axes, samples, dimensions)
    //   params: { asset_path: string }
    // create_blend_space   — Create a new 2D blend space
    //   params: { save_path: string, skeleton_path: string }
    // create_blend_space_1d — Create a new 1D blend space
    //   params: { save_path: string, skeleton_path: string }
    // set_blend_space_axis — Configure axis label/range
    //   params: { asset_path: string, axis: string("x"|"y"), label: string, min: float, max: float }
    // add_blend_space_sample — Add a sample point [PORTED]
    //   params: { asset_path: string, anim_path: string, x: float, y: float }
    // edit_blend_space_sample — Move/change a sample [PORTED]
    //   params: { asset_path: string, sample_index: int, x: float, y: float, anim_path?: string }
    // delete_blend_space_sample — Remove a sample [PORTED]
    //   params: { asset_path: string, sample_index: int }

    // --- ABP (11) ---
    // list_anim_blueprints — List Animation Blueprint assets
    //   params: { path_filter?: string }
    // get_abp_info         — High-level ABP summary
    //   params: { asset_path: string }
    // create_anim_blueprint — Create a new ABP
    //   params: { save_path: string, skeleton_path: string, parent_class?: string }
    // set_abp_skeleton     — Change the target skeleton
    //   params: { asset_path: string, skeleton_path: string }
    // get_state_machines   — List all state machines in ABP [PORTED]
    //   params: { asset_path: string }
    // get_state_info       — Detail for one state [PORTED]
    //   params: { asset_path: string, machine_name: string, state_name: string }
    // get_transitions      — Transitions for a state machine [PORTED]
    //   params: { asset_path: string, machine_name: string }
    // get_blend_nodes      — Blend nodes in a graph [PORTED]
    //   params: { asset_path: string, graph_name?: string }
    // get_linked_layers    — Linked anim layers [PORTED]
    //   params: { asset_path: string }
    // get_graphs           — List all graphs in ABP [PORTED]
    //   params: { asset_path: string }
    // get_nodes            — Search/list nodes by class filter [PORTED]
    //   params: { asset_path: string, node_class_filter?: string }

    // --- Notify (7) ---
    // list_notifies        — List notifies on a sequence/montage
    //   params: { asset_path: string }
    // add_notify           — Add a notify event
    //   params: { asset_path: string, notify_class: string, time: float, track_index?: int }
    // add_notify_state     — Add a notify state (with duration)
    //   params: { asset_path: string, notify_class: string, time: float, duration: float, track_index?: int }
    // remove_notify        — Remove notify by index
    //   params: { asset_path: string, notify_index: int }
    // set_notify_track     — Move notify to different track
    //   params: { asset_path: string, notify_index: int, track_index: int }
    // set_notify_time      — Set notify trigger time [PORTED]
    //   params: { asset_path: string, notify_index: int, new_time: float }
    // set_notify_duration  — Set notify state duration [PORTED]
    //   params: { asset_path: string, notify_index: int, new_duration: float }

    // --- Curve (5) ---
    // list_curves          — List animation curves on a sequence
    //   params: { asset_path: string }
    // add_curve            — Add a curve to a sequence
    //   params: { asset_path: string, curve_name: string, curve_type?: string }
    // remove_curve         — Remove a curve
    //   params: { asset_path: string, curve_name: string }
    // set_curve_keys       — Set keyframes on a curve
    //   params: { asset_path: string, curve_name: string, keys_json: string }
    // get_curve_keys       — Read keyframes from a curve
    //   params: { asset_path: string, curve_name: string }

    // --- Skeleton (8) ---
    // list_skeletons       — List skeleton assets
    //   params: { path_filter?: string }
    // get_skeleton_info    — Full skeleton hierarchy [PORTED]
    //   params: { asset_path: string }
    // get_skeletal_mesh_info — Mesh info with morphs, sockets, LODs [PORTED]
    //   params: { asset_path: string }
    // get_bone_tree        — Hierarchical bone tree
    //   params: { asset_path: string, root_bone?: string }
    // add_socket           — Add a socket to skeleton
    //   params: { asset_path: string, socket_name: string, bone_name: string, offset?: object }
    // remove_socket        — Remove a socket
    //   params: { asset_path: string, socket_name: string }
    // add_virtual_bone     — Add a virtual bone [PORTED]
    //   params: { asset_path: string, source_bone: string, target_bone: string }
    // remove_virtual_bones — Remove virtual bones [PORTED]
    //   params: { asset_path: string, bone_names?: array }

    // --- BoneTrack (3) ---
    // add_bone_track       — Add a bone track to sequence [PORTED]
    //   params: { asset_path: string, bone_name: string }
    // remove_bone_track    — Remove bone track(s) [PORTED]
    //   params: { asset_path: string, bone_name: string, include_children?: bool }
    // set_bone_track_keys  — Set position/rotation/scale keys [PORTED]
    //   params: { asset_path: string, bone_name: string, positions_json: string, rotations_json: string, scales_json: string }
}

// ─────────────────────────────────────────────────────────────────────────────
// Dispatch
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMonolithAnimationActions::Dispatch(const FString& Action, const TSharedPtr<FJsonObject>& Params)
{
    // Sequence
    if (Action == TEXT("list_sequences"))       return ListSequences(Params);
    if (Action == TEXT("get_sequence_info"))    return GetSequenceInfo(Params);
    if (Action == TEXT("get_sequence_curves"))  return GetSequenceCurves(Params);
    if (Action == TEXT("get_sequence_notifies")) return GetSequenceNotifies(Params);
    if (Action == TEXT("get_sequence_metadata")) return GetSequenceMetadata(Params);
    if (Action == TEXT("create_sequence"))      return CreateSequence(Params);
    if (Action == TEXT("duplicate_sequence"))   return DuplicateSequence(Params);
    if (Action == TEXT("set_sequence_length"))  return SetSequenceLength(Params);
    if (Action == TEXT("set_sequence_rate_scale")) return SetSequenceRateScale(Params);

    // Montage
    if (Action == TEXT("list_montages"))        return ListMontages(Params);
    if (Action == TEXT("get_montage_info"))     return GetMontageInfo(Params);
    if (Action == TEXT("create_montage"))       return CreateMontage(Params);
    if (Action == TEXT("set_montage_blend_in")) return SetMontageBlendIn(Params);
    if (Action == TEXT("set_montage_blend_out")) return SetMontageBlendOut(Params);
    if (Action == TEXT("add_montage_slot"))     return AddMontageSlot(Params);
    if (Action == TEXT("set_montage_slot"))     return SetMontageSlot(Params);
    if (Action == TEXT("add_montage_section"))  return AddMontageSection(Params);
    if (Action == TEXT("delete_montage_section")) return DeleteMontageSection(Params);
    if (Action == TEXT("set_section_next"))     return SetSectionNext(Params);
    if (Action == TEXT("set_section_time"))     return SetSectionTime(Params);

    // BlendSpace
    if (Action == TEXT("list_blend_spaces"))    return ListBlendSpaces(Params);
    if (Action == TEXT("get_blend_space_info")) return GetBlendSpaceInfo(Params);
    if (Action == TEXT("create_blend_space"))   return CreateBlendSpace(Params);
    if (Action == TEXT("create_blend_space_1d")) return CreateBlendSpace1D(Params);
    if (Action == TEXT("set_blend_space_axis")) return SetBlendSpaceAxis(Params);
    if (Action == TEXT("add_blend_space_sample")) return AddBlendSpaceSample(Params);
    if (Action == TEXT("edit_blend_space_sample")) return EditBlendSpaceSample(Params);
    if (Action == TEXT("delete_blend_space_sample")) return DeleteBlendSpaceSample(Params);

    // ABP
    if (Action == TEXT("list_anim_blueprints")) return ListAnimBlueprints(Params);
    if (Action == TEXT("get_abp_info"))         return GetAbpInfo(Params);
    if (Action == TEXT("create_anim_blueprint")) return CreateAnimBlueprint(Params);
    if (Action == TEXT("set_abp_skeleton"))     return SetAbpSkeleton(Params);
    if (Action == TEXT("get_state_machines"))   return GetStateMachines(Params);
    if (Action == TEXT("get_state_info"))       return GetStateInfo(Params);
    if (Action == TEXT("get_transitions"))      return GetTransitions(Params);
    if (Action == TEXT("get_blend_nodes"))      return GetBlendNodes(Params);
    if (Action == TEXT("get_linked_layers"))    return GetLinkedLayers(Params);
    if (Action == TEXT("get_graphs"))           return GetGraphs(Params);
    if (Action == TEXT("get_nodes"))            return GetNodes(Params);

    // Notify
    if (Action == TEXT("list_notifies"))        return ListNotifies(Params);
    if (Action == TEXT("add_notify"))           return AddNotify(Params);
    if (Action == TEXT("add_notify_state"))     return AddNotifyState(Params);
    if (Action == TEXT("remove_notify"))        return RemoveNotify(Params);
    if (Action == TEXT("set_notify_track"))     return SetNotifyTrack(Params);
    if (Action == TEXT("set_notify_time"))      return SetNotifyTime(Params);
    if (Action == TEXT("set_notify_duration"))  return SetNotifyDuration(Params);

    // Curve
    if (Action == TEXT("list_curves"))          return ListCurves(Params);
    if (Action == TEXT("add_curve"))            return AddCurve(Params);
    if (Action == TEXT("remove_curve"))         return RemoveCurve(Params);
    if (Action == TEXT("set_curve_keys"))       return SetCurveKeys(Params);
    if (Action == TEXT("get_curve_keys"))       return GetCurveKeys(Params);

    // Skeleton
    if (Action == TEXT("list_skeletons"))       return ListSkeletons(Params);
    if (Action == TEXT("get_skeleton_info"))    return GetSkeletonInfo(Params);
    if (Action == TEXT("get_skeletal_mesh_info")) return GetSkeletalMeshInfo(Params);
    if (Action == TEXT("get_bone_tree"))        return GetBoneTree(Params);
    if (Action == TEXT("add_socket"))           return AddSocket(Params);
    if (Action == TEXT("remove_socket"))        return RemoveSocket(Params);
    if (Action == TEXT("add_virtual_bone"))     return AddVirtualBone(Params);
    if (Action == TEXT("remove_virtual_bones")) return RemoveVirtualBones(Params);

    // BoneTrack
    if (Action == TEXT("add_bone_track"))       return AddBoneTrack(Params);
    if (Action == TEXT("remove_bone_track"))    return RemoveBoneTrack(Params);
    if (Action == TEXT("set_bone_track_keys"))  return SetBoneTrackKeys(Params);

    // Unknown action
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetBoolField(TEXT("success"), false);
    Error->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown animation action: %s"), *Action));
    Error->SetStringField(TEXT("code"), TEXT("INVALID_ACTION"));
    return Error;
}

// ─────────────────────────────────────────────────────────────────────────────
// Asset Loaders (same as AnimationMCPReaderLibrary, using StaticLoadObject)
// ─────────────────────────────────────────────────────────────────────────────

UAnimSequence* FMonolithAnimationActions::LoadAnimSequence(const FString& AssetPath)
{
    return LoadObject<UAnimSequence>(nullptr, *AssetPath);
}

UAnimMontage* FMonolithAnimationActions::LoadMontage(const FString& AssetPath)
{
    return LoadObject<UAnimMontage>(nullptr, *AssetPath);
}

UBlendSpace* FMonolithAnimationActions::LoadBlendSpace(const FString& AssetPath)
{
    return LoadObject<UBlendSpace>(nullptr, *AssetPath);
}

UAnimBlueprint* FMonolithAnimationActions::LoadAnimBlueprint(const FString& AssetPath)
{
    return LoadObject<UAnimBlueprint>(nullptr, *AssetPath);
}

USkeleton* FMonolithAnimationActions::LoadSkeleton(const FString& AssetPath)
{
    return LoadObject<USkeleton>(nullptr, *AssetPath);
}

USkeletalMesh* FMonolithAnimationActions::LoadSkeletalMesh(const FString& AssetPath)
{
    return LoadObject<USkeletalMesh>(nullptr, *AssetPath);
}

// ─────────────────────────────────────────────────────────────────────────────
// PORTED ACTIONS — Direct copy from AnimationMCPReaderLibrary.cpp
//
// Porting strategy:
// 1. Change signature: static FString Foo(params...) -> static TSharedPtr<FJsonObject> Foo(TSharedPtr<FJsonObject>& Params)
// 2. Extract params from Params JSON object instead of function args
// 3. Use FMonolithJsonUtils::ErrorJson/SuccessJson instead of local copies
// 4. Function body logic stays IDENTICAL — copy the working code
// ─────────────────────────────────────────────────────────────────────────────

// ── Example: AddMontageSection (ported from AnimationMCPReaderLibrary.cpp:89-109) ──

TSharedPtr<FJsonObject> FMonolithAnimationActions::AddMontageSection(const TSharedPtr<FJsonObject>& Params)
{
    FString AssetPath = Params->GetStringField(TEXT("asset_path"));
    FString SectionName = Params->GetStringField(TEXT("section_name"));
    double StartTime = Params->GetNumberField(TEXT("start_time"));

    UAnimMontage* Montage = LoadMontage(AssetPath);
    if (!Montage)
    {
        TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
        Err->SetBoolField(TEXT("success"), false);
        Err->SetStringField(TEXT("error"), FString::Printf(TEXT("Montage not found: %s"), *AssetPath));
        return Err;
    }

    // >>> BODY COPIED FROM AnimationMCPReaderLibrary.cpp:94-108 <<<
    GEditor->BeginTransaction(FText::FromString(TEXT("Add Montage Section")));
    Montage->Modify();

    int32 Index = Montage->AddAnimCompositeSection(FName(*SectionName), StartTime);

    GEditor->EndTransaction();

    if (Index == INDEX_NONE)
    {
        TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
        Err->SetBoolField(TEXT("success"), false);
        Err->SetStringField(TEXT("error"), FString::Printf(TEXT("Failed to add section '%s'"), *SectionName));
        return Err;
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("success"), true);
    Root->SetStringField(TEXT("section_name"), SectionName);
    Root->SetNumberField(TEXT("index"), Index);
    Root->SetNumberField(TEXT("start_time"), StartTime);
    return Root;
}

// ─────────────────────────────────────────────────────────────────────────────
// PORTING PATTERN FOR REMAINING 22 AnimationMCPReader FUNCTIONS
//
// Each follows the same transformation:
//
// BEFORE (AnimationMCPReaderLibrary.cpp):
//   FString UAnimationMCPReaderLibrary::GetStateMachines(const FString& AssetPath)
//   {
//       UAnimBlueprint* ABP = LoadAnimBlueprint(AssetPath);
//       if (!ABP) return ErrorJson(...);
//       // ... logic building TSharedPtr<FJsonObject> ...
//       return SuccessJson(Root);
//   }
//
// AFTER (MonolithAnimationActions.cpp):
//   TSharedPtr<FJsonObject> FMonolithAnimationActions::GetStateMachines(const TSharedPtr<FJsonObject>& Params)
//   {
//       FString AssetPath = Params->GetStringField(TEXT("asset_path"));
//       UAnimBlueprint* ABP = LoadAnimBlueprint(AssetPath);
//       if (!ABP) { return ErrorResponse(...); }
//       // ... IDENTICAL logic building TSharedPtr<FJsonObject> ...
//       Root->SetBoolField(TEXT("success"), true);
//       return Root;
//   }
//
// The function bodies from AnimationMCPReaderLibrary.cpp are copied verbatim.
// The ONLY changes are:
//   1. Extract params from JSON instead of function arguments
//   2. Return TSharedPtr<FJsonObject> instead of FString
//   3. Use MonolithCore's error/success helpers
//
// Source line references for each port:
//   DeleteMontageSection  → AnimationMCPReaderLibrary.cpp:111-135
//   SetSectionNext        → AnimationMCPReaderLibrary.cpp:137-158
//   SetSectionTime        → AnimationMCPReaderLibrary.cpp:160-182
//   AddBlendSpaceSample   → AnimationMCPReaderLibrary.cpp:188-213
//   EditBlendSpaceSample  → AnimationMCPReaderLibrary.cpp:215-248
//   DeleteBlendSpaceSample → AnimationMCPReaderLibrary.cpp:250-271
//   SetNotifyTime         → AnimationMCPReaderLibrary.cpp:277-297
//   SetNotifyDuration     → AnimationMCPReaderLibrary.cpp:299-321
//   AddBoneTrack          → AnimationMCPReaderLibrary.cpp:327-344
//   RemoveBoneTrack       → AnimationMCPReaderLibrary.cpp:346-364
//   SetBoneTrackKeys      → AnimationMCPReaderLibrary.cpp:366-445
//   AddVirtualBone        → AnimationMCPReaderLibrary.cpp:451-473
//   RemoveVirtualBones    → AnimationMCPReaderLibrary.cpp:475-517
//   GetSkeletonInfo       → AnimationMCPReaderLibrary.cpp:523-563
//   GetSkeletalMeshInfo   → AnimationMCPReaderLibrary.cpp:565-624
//   GetStateMachines      → AnimationMCPReaderLibrary.cpp:630-724
//   GetStateInfo          → AnimationMCPReaderLibrary.cpp:726-785
//   GetTransitions        → AnimationMCPReaderLibrary.cpp:787-857
//   GetBlendNodes         → AnimationMCPReaderLibrary.cpp:859-904
//   GetGraphs             → AnimationMCPReaderLibrary.cpp:906-946
//   GetNodes              → AnimationMCPReaderLibrary.cpp:948-1002
//   GetLinkedLayers       → AnimationMCPReaderLibrary.cpp:1004-1033
// ─────────────────────────────────────────────────────────────────────────────

// ─────────────────────────────────────────────────────────────────────────────
// NEW ACTIONS — Python-only tools now implemented in C++
//
// These were previously Python tools that called the C++ plugin via HTTP.
// Now they operate directly on UE assets via C++ API.
// ─────────────────────────────────────────────────────────────────────────────

// ── Example: ListSequences (new — was Python-only) ──

TSharedPtr<FJsonObject> FMonolithAnimationActions::ListSequences(const TSharedPtr<FJsonObject>& Params)
{
    FString PathFilter = Params->HasField(TEXT("path_filter"))
        ? Params->GetStringField(TEXT("path_filter")) : TEXT("/Game");

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    IAssetRegistry& AR = ARM.Get();

    TArray<FAssetData> Assets;
    AR.GetAssetsByClass(UAnimSequence::StaticClass()->GetClassPathName(), Assets, true);

    TArray<TSharedPtr<FJsonValue>> ResultsArr;
    for (const FAssetData& Asset : Assets)
    {
        FString Path = Asset.GetObjectPathString();
        if (!PathFilter.IsEmpty() && !Path.StartsWith(PathFilter))
            continue;

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("path"), Path);
        Item->SetStringField(TEXT("name"), Asset.AssetName.ToString());
        ResultsArr.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("success"), true);
    Root->SetArrayField(TEXT("sequences"), ResultsArr);
    Root->SetNumberField(TEXT("count"), ResultsArr.Num());
    return Root;
}

// ── Example: CreateMontage (new — was Python-only) ──

TSharedPtr<FJsonObject> FMonolithAnimationActions::CreateMontage(const TSharedPtr<FJsonObject>& Params)
{
    FString SavePath = Params->GetStringField(TEXT("save_path"));
    FString SequencePath = Params->GetStringField(TEXT("sequence_path"));

    UAnimSequence* SourceSeq = LoadAnimSequence(SequencePath);
    if (!SourceSeq)
    {
        TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
        Err->SetBoolField(TEXT("success"), false);
        Err->SetStringField(TEXT("error"), FString::Printf(TEXT("Source sequence not found: %s"), *SequencePath));
        return Err;
    }

    IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();

    FString PackagePath = FPackageName::GetLongPackagePath(SavePath);
    FString AssetName = FPackageName::GetShortName(SavePath);

    UAnimMontageFactory* Factory = NewObject<UAnimMontageFactory>();
    Factory->SourceAnimation = SourceSeq;

    UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UAnimMontage::StaticClass(), Factory);
    if (!NewAsset)
    {
        TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
        Err->SetBoolField(TEXT("success"), false);
        Err->SetStringField(TEXT("error"), TEXT("Failed to create montage asset"));
        return Err;
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("success"), true);
    Root->SetStringField(TEXT("asset_path"), NewAsset->GetPathName());
    Root->SetStringField(TEXT("source_sequence"), SequencePath);
    return Root;
}

// ─────────────────────────────────────────────────────────────────────────────
// REMAINING NEW ACTIONS — Implementation pattern
//
// All new actions follow the same pattern as ListSequences/CreateMontage above:
//   1. Extract params from JSON
//   2. Load asset via Asset Registry or LoadObject
//   3. Wrap mutation in GEditor->BeginTransaction / EndTransaction
//   4. Build JSON response
//
// The Python implementations provide the logic reference — we just call UE API directly.
//
// New action implementation references (from unreal-animation-mcp Python):
//   GetSequenceInfo       → Load UAnimSequence, read length/rate/skeleton/num_frames
//   GetSequenceCurves     → Iterate Seq->GetCurveData() for all curves
//   GetSequenceNotifies   → Iterate Seq->Notifies array
//   GetSequenceMetadata   → Combine GetSequenceInfo fields
//   DuplicateSequence     → AssetTools.DuplicateAsset()
//   SetSequenceLength     → Controller.SetPlayLength()
//   SetSequenceRateScale  → Seq->RateScale = value
//   ListMontages          → AR.GetAssetsByClass(UAnimMontage)
//   GetMontageInfo        → Read sections, slots, blend in/out, composite sections
//   SetMontageBlendIn     → Montage->BlendIn.SetBlendTime()
//   SetMontageBlendOut    → Montage->BlendOut.SetBlendTime()
//   AddMontageSlot        → Montage->AddSlot(FName)
//   SetMontageSlot        → Modify slot track group assignment
//   ListBlendSpaces       → AR.GetAssetsByClass(UBlendSpace)
//   GetBlendSpaceInfo     → Read axes, samples, dimensions
//   CreateBlendSpace      → Factory + AssetTools.CreateAsset
//   CreateBlendSpace1D    → UBlendSpaceFactory1D
//   SetBlendSpaceAxis     → BS->GetBlendParameter(axis).DisplayName = label, etc.
//   ListAnimBlueprints    → AR.GetAssetsByClass(UAnimBlueprint)
//   GetAbpInfo            → Read skeleton, parent class, graph count, state machines
//   CreateAnimBlueprint   → UAnimBlueprintFactory + AssetTools
//   SetAbpSkeleton        → ABP->TargetSkeleton = LoadSkeleton(path)
//   ListNotifies          → Iterate Seq->Notifies, return name/time/duration/class
//   AddNotify             → Seq->Notifies.Add(FAnimNotifyEvent) + set class
//   AddNotifyState        → Same + set NotifyStateClass + Duration
//   RemoveNotify          → Seq->Notifies.RemoveAt(index)
//   SetNotifyTrack        → Notify.TrackIndex = value
//   ListCurves            → IAnimationDataModel curves enumeration
//   AddCurve              → Controller.AddCurve(FName, ERawCurveTrackTypes)
//   RemoveCurve           → Controller.RemoveCurve(FAnimationCurveIdentifier)
//   SetCurveKeys          → Controller.SetCurveKeys(Id, TArray<FRichCurveKey>)
//   GetCurveKeys          → Model.GetCurve(Id).GetConstCurveData()
//   ListSkeletons         → AR.GetAssetsByClass(USkeleton)
//   GetBoneTree           → Walk FReferenceSkeleton recursively from root_bone
//   AddSocket             → Skeleton->AddSocket(NewObject<USkeletalMeshSocket>)
//   RemoveSocket          → Find + remove from array
// ─────────────────────────────────────────────────────────────────────────────

// STUB: Remaining implementations follow the exact same patterns shown above.
// Each is 15-60 lines of straightforward UE API calls.
// Full implementations will be written during Phase 3 execution.
```

### Step 5: Add to .uplugin

Add to `Monolith.uplugin` Modules array:

```json
{
    "Name": "MonolithAnimation",
    "Type": "Editor",
    "LoadingPhase": "Default",
    "Dependencies": ["MonolithCore"]
}
```

### Step 6: Verification

```
# 1. Compile
Build MonolithAnimation module (editor must be closed for UBT, or use editor hot-reload)

# 2. Check registration
Call monolith.discover("animation") → should return 62 actions

# 3. Test ported actions (pick 3 representative ones)
animation.query("get_state_machines", {"asset_path": "/Game/Characters/ABP_Hero"})
animation.query("add_montage_section", {"asset_path": "/Game/.../AM_Attack", "section_name": "WindUp", "start_time": 0.5})
animation.query("get_skeleton_info", {"asset_path": "/Game/.../SK_Mannequin"})

# 4. Test new actions
animation.query("list_sequences", {"path_filter": "/Game/Characters"})
animation.query("create_montage", {"save_path": "/Game/Test/AM_Test", "sequence_path": "/Game/.../AS_Idle"})

# 5. Verify undo works for mutation actions
Edit → Undo after add_montage_section should revert
```

---

## Task 3.2 — MonolithNiagara Module (70 actions)

### What We're Porting

**From NiagaraMCPBridge C++ plugin (39 UFUNCTIONs across 7 classes):**

| Class | Functions | Count |
|---|---|---|
| `UNiagaraMCPSystemLibrary` | `AddEmitter`, `RemoveEmitter`, `DuplicateEmitter`, `SetEmitterEnabled`, `ReorderEmitters`, `SetEmitterProperty`, `RequestCompile`, `CreateNiagaraSystem` | 8 |
| `UNiagaraMCPModuleLibrary` | `GetOrderedModules`, `GetModuleInputs`, `GetModuleGraph`, `AddModule`, `RemoveModule`, `MoveModule`, `SetModuleEnabled`, `SetModuleInputValue`, `SetModuleInputBinding`, `SetModuleInputDI`, `CreateModuleFromHLSL`, `CreateFunctionFromHLSL` | 12 |
| `UNiagaraMCPParameterLibrary` | `GetAllParameters`, `GetUserParameters`, `GetParameterValue`, `GetParameterType`, `TraceParameterBinding`, `AddUserParameter`, `RemoveUserParameter`, `SetParameterDefault`, `SetCurveValue` | 9 |
| `UNiagaraMCPRendererLibrary` | `AddRenderer`, `RemoveRenderer`, `SetRendererMaterial`, `SetRendererProperty`, `GetRendererBindings`, `SetRendererBinding` | 6 |
| `UNiagaraMCPBatchLibrary` | `BatchExecute`, `CreateSystemFromSpec` | 2 |
| `UNiagaraMCPDILibrary` | `GetDataInterfaceFunctions` | 1 |
| `UNiagaraMCPHLSLLibrary` | `GetCompiledGPUHLSL` | 1 |

**From unreal-niagara-mcp Python server (31 additional tools):**

| Category | Action Names | Count |
|---|---|---|
| System Inspection | `list_systems`, `get_system_info`, `get_system_emitters`, `get_system_bounds`, `get_system_fixed_bounds` | 5 |
| System Configuration | `set_system_fixed_bounds`, `set_system_warmup`, `set_system_determinism`, `set_system_scalability` | 4 |
| Emitter Inspection | `get_emitter_info`, `get_emitter_summary`, `list_emitter_assets` | 3 |
| Emitter Configuration | `set_emitter_sim_target`, `set_emitter_local_space`, `set_emitter_fixed_bounds`, `set_emitter_scalability` | 4 |
| Module Queries | `list_available_modules`, `search_modules`, `get_module_description` | 3 |
| Parameter Queries | `list_parameter_types`, `get_parameter_namespace_info` | 2 |
| Renderer Queries | `list_renderer_types`, `get_renderer_info`, `list_renderer_bindings_schema` | 3 |
| DI Queries | `list_data_interfaces`, `get_di_info`, `get_di_properties` | 3 |
| Validation | `validate_system`, `get_compilation_status`, `get_compile_errors`, `check_gpu_compatibility` | 4 |

**Total: 39 ported + 31 new = 70 actions**

### Architecture Decision: Preserve Multi-Class Structure

The NiagaraMCPBridge plugin wisely splits functionality across 7 specialized classes. Monolith preserves this architecture as **helper classes** under a single dispatch entry point:

```
FMonolithNiagaraActions (dispatch + registration)
├── FMonolithNiagaraSystemOps     ← ports UNiagaraMCPSystemLibrary (8) + new (9)
├── FMonolithNiagaraModuleOps     ← ports UNiagaraMCPModuleLibrary (12) + new (3)
├── FMonolithNiagaraParameterOps  ← ports UNiagaraMCPParameterLibrary (9) + new (2)
├── FMonolithNiagaraRendererOps   ← ports UNiagaraMCPRendererLibrary (6) + new (3)
├── FMonolithNiagaraBatchOps      ← ports UNiagaraMCPBatchLibrary (2)
├── FMonolithNiagaraDIOps         ← ports UNiagaraMCPDILibrary (1) + new (3)
├── FMonolithNiagaraHLSLOps       ← ports UNiagaraMCPHLSLLibrary (1)
└── FMonolithNiagaraValidationOps ← new (4)
```

### Files

**Create:**
- `Source/MonolithNiagara/MonolithNiagara.Build.cs`
- `Source/MonolithNiagara/Public/MonolithNiagaraModule.h`
- `Source/MonolithNiagara/Private/MonolithNiagaraModule.cpp`
- `Source/MonolithNiagara/Public/MonolithNiagaraActions.h`
- `Source/MonolithNiagara/Private/MonolithNiagaraActions.cpp`
- `Source/MonolithNiagara/Public/MonolithNiagaraSystemOps.h`
- `Source/MonolithNiagara/Private/MonolithNiagaraSystemOps.cpp`
- `Source/MonolithNiagara/Public/MonolithNiagaraModuleOps.h`
- `Source/MonolithNiagara/Private/MonolithNiagaraModuleOps.cpp`
- `Source/MonolithNiagara/Public/MonolithNiagaraParameterOps.h`
- `Source/MonolithNiagara/Private/MonolithNiagaraParameterOps.cpp`
- `Source/MonolithNiagara/Public/MonolithNiagaraRendererOps.h`
- `Source/MonolithNiagara/Private/MonolithNiagaraRendererOps.cpp`
- `Source/MonolithNiagara/Public/MonolithNiagaraBatchOps.h`
- `Source/MonolithNiagara/Private/MonolithNiagaraBatchOps.cpp`
- `Source/MonolithNiagara/Public/MonolithNiagaraDIOps.h`
- `Source/MonolithNiagara/Private/MonolithNiagaraDIOps.cpp`
- `Source/MonolithNiagara/Public/MonolithNiagaraHLSLOps.h`
- `Source/MonolithNiagara/Private/MonolithNiagaraHLSLOps.cpp`
- `Source/MonolithNiagara/Public/MonolithNiagaraValidationOps.h`
- `Source/MonolithNiagara/Private/MonolithNiagaraValidationOps.cpp`

### Step 1: Build.cs

**File:** `Source/MonolithNiagara/MonolithNiagara.Build.cs`

```csharp
using UnrealBuildTool;

public class MonolithNiagara : ModuleRules
{
    public MonolithNiagara(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[]
        {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[]
        {
            "MonolithCore",
            "UnrealEd",
            "NiagaraCore",
            "Niagara",
            "NiagaraEditor",
            "NiagaraShader",
            "Json",
            "JsonUtilities",
            "AssetTools",
            "AssetRegistry"
        });
    }
}
```

### Step 2: Module Header & Cpp

**File:** `Source/MonolithNiagara/Public/MonolithNiagaraModule.h`

```cpp
#pragma once

#include "Modules/ModuleManager.h"

class FMonolithNiagaraModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;
};
```

**File:** `Source/MonolithNiagara/Private/MonolithNiagaraModule.cpp`

```cpp
#include "MonolithNiagaraModule.h"
#include "MonolithNiagaraActions.h"

#define LOCTEXT_NAMESPACE "FMonolithNiagaraModule"

void FMonolithNiagaraModule::StartupModule()
{
    FMonolithNiagaraActions::RegisterActions();
    UE_LOG(LogTemp, Log, TEXT("Monolith — Niagara module loaded (70 actions)"));
}

void FMonolithNiagaraModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithNiagaraModule, MonolithNiagara)
```

### Step 3: Actions Header — Top-Level Dispatcher

**File:** `Source/MonolithNiagara/Public/MonolithNiagaraActions.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Top-level dispatcher for all 70 Niagara actions via niagara.query(action, params).
 *
 * Ported from: NiagaraMCPBridge (39 UFUNCTIONs across 7 classes) + unreal-niagara-mcp (31 Python tools)
 *
 * Action Categories (70 total):
 *   System (17):    list, info, emitters, bounds, fixed_bounds, create, compile,
 *                   add_emitter, remove_emitter, duplicate_emitter, enable_emitter,
 *                   reorder_emitters, set_emitter_property, set_fixed_bounds,
 *                   set_warmup, set_determinism, set_scalability
 *   Emitter (7):    info, summary, list_assets, set_sim_target, set_local_space,
 *                   set_fixed_bounds, set_scalability
 *   Module (15):    ordered, inputs, graph, add, remove, move, enable,
 *                   set_input_value, set_input_binding, set_input_di,
 *                   create_from_hlsl, create_function, list_available, search, description
 *   Parameter (11): all, user, value, type, trace_binding, add_user, remove_user,
 *                   set_default, set_curve, list_types, namespace_info
 *   Renderer (9):   add, remove, set_material, set_property, get_bindings, set_binding,
 *                   list_types, info, bindings_schema
 *   Batch (2):      execute, create_from_spec
 *   DI (4):         functions, list, info, properties
 *   HLSL (1):       get_compiled_gpu
 *   Validation (4): validate, compilation_status, compile_errors, gpu_compatibility
 */
class MONOLITHNIAGARA_API FMonolithNiagaraActions
{
public:
    static void RegisterActions();

private:
    static TSharedPtr<FJsonObject> Dispatch(const FString& Action, const TSharedPtr<FJsonObject>& Params);
};
```

### Step 4: Sub-Operation Headers

Each sub-ops class mirrors the original NiagaraMCPBridge class it replaces.

**File:** `Source/MonolithNiagara/Public/MonolithNiagaraSystemOps.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UNiagaraSystem;
struct FNiagaraEmitterHandle;

/**
 * System-level operations. Ports UNiagaraMCPSystemLibrary (8 functions) + 9 new Python tools.
 * Total: 17 actions.
 */
class FMonolithNiagaraSystemOps
{
public:
    // ── Ported from UNiagaraMCPSystemLibrary ─────────────────────────────
    static TSharedPtr<FJsonObject> AddEmitter(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> RemoveEmitter(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> DuplicateEmitter(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetEmitterEnabled(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> ReorderEmitters(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetEmitterProperty(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> RequestCompile(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> CreateNiagaraSystem(const TSharedPtr<FJsonObject>& Params);

    // ── New (from Python unreal-niagara-mcp) ─────────────────────────────
    static TSharedPtr<FJsonObject> ListSystems(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetSystemInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetSystemEmitters(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetSystemBounds(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetSystemFixedBounds(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetSystemFixedBounds(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetSystemWarmup(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetSystemDeterminism(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetSystemScalability(const TSharedPtr<FJsonObject>& Params);

    // ── Shared helpers (ported from UNiagaraMCPSystemLibrary) ────────────
    static UNiagaraSystem* LoadSystem(const FString& SystemPath);
    static int32 FindEmitterHandleIndex(UNiagaraSystem* System, const FString& HandleIdOrName);
};
```

**File:** `Source/MonolithNiagara/Public/MonolithNiagaraModuleOps.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "NiagaraCommon.h"
#include "Dom/JsonObject.h"

class UNiagaraSystem;
class UNiagaraNodeOutput;
class UNiagaraNodeFunctionCall;
class UNiagaraGraph;

/**
 * Module stack operations. Ports UNiagaraMCPModuleLibrary (12 functions) + 3 new.
 * Total: 15 actions.
 */
class FMonolithNiagaraModuleOps
{
public:
    // ── Ported from UNiagaraMCPModuleLibrary ─────────────────────────────
    static TSharedPtr<FJsonObject> GetOrderedModules(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetModuleInputs(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetModuleGraph(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> AddModule(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> RemoveModule(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> MoveModule(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetModuleEnabled(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetModuleInputValue(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetModuleInputBinding(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetModuleInputDI(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> CreateModuleFromHLSL(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> CreateFunctionFromHLSL(const TSharedPtr<FJsonObject>& Params);

    // ── New (from Python) ────────────────────────────────────────────────
    static TSharedPtr<FJsonObject> ListAvailableModules(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SearchModules(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetModuleDescription(const TSharedPtr<FJsonObject>& Params);

    // ── Shared helpers (ported from UNiagaraMCPModuleLibrary) ────────────
    static ENiagaraScriptUsage ResolveScriptUsage(const FString& UsageString);
    static UNiagaraNodeOutput* FindOutputNode(UNiagaraSystem* System, const FString& EmitterHandleId, ENiagaraScriptUsage Usage);
    static UNiagaraNodeFunctionCall* FindModuleNode(UNiagaraSystem* System, const FString& EmitterHandleId,
        const FString& NodeGuidStr, ENiagaraScriptUsage* OutUsage = nullptr);
    static UNiagaraGraph* GetGraphForUsage(UNiagaraSystem* System, const FString& EmitterHandleId, ENiagaraScriptUsage Usage);
};
```

**File:** `Source/MonolithNiagara/Public/MonolithNiagaraParameterOps.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "NiagaraTypes.h"
#include "Dom/JsonObject.h"

class UNiagaraSystem;
struct FNiagaraVariable;
struct FNiagaraParameterStore;

/**
 * Parameter store operations. Ports UNiagaraMCPParameterLibrary (9 functions) + 2 new.
 * Total: 11 actions.
 */
class FMonolithNiagaraParameterOps
{
public:
    // ── Ported ───────────────────────────────────────────────────────────
    static TSharedPtr<FJsonObject> GetAllParameters(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetUserParameters(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetParameterValue(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetParameterType(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> TraceParameterBinding(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> AddUserParameter(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> RemoveUserParameter(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetParameterDefault(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetCurveValue(const TSharedPtr<FJsonObject>& Params);

    // ── New ──────────────────────────────────────────────────────────────
    static TSharedPtr<FJsonObject> ListParameterTypes(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetParameterNamespaceInfo(const TSharedPtr<FJsonObject>& Params);

    // ── Shared helpers (ported from UNiagaraMCPParameterLibrary) ─────────
    static FNiagaraTypeDefinition ResolveNiagaraType(const FString& TypeName);
    static FString SerializeParameterValue(const FNiagaraVariable& Variable, const FNiagaraParameterStore& Store);
    static FNiagaraVariable MakeUserVariable(const FString& ParamName, const FNiagaraTypeDefinition& TypeDef);
};
```

**File:** `Source/MonolithNiagara/Public/MonolithNiagaraRendererOps.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UNiagaraSystem;
class UNiagaraRendererProperties;
struct FVersionedNiagaraEmitterData;

/**
 * Renderer operations. Ports UNiagaraMCPRendererLibrary (6 functions) + 3 new.
 * Total: 9 actions.
 */
class FMonolithNiagaraRendererOps
{
public:
    // ── Ported ───────────────────────────────────────────────────────────
    static TSharedPtr<FJsonObject> AddRenderer(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> RemoveRenderer(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetRendererMaterial(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetRendererProperty(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetRendererBindings(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> SetRendererBinding(const TSharedPtr<FJsonObject>& Params);

    // ── New ──────────────────────────────────────────────────────────────
    static TSharedPtr<FJsonObject> ListRendererTypes(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetRendererInfo(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> ListRendererBindingsSchema(const TSharedPtr<FJsonObject>& Params);

    // ── Helpers ──────────────────────────────────────────────────────────
    static UClass* ResolveRendererClass(const FString& RendererClass);
    static UNiagaraRendererProperties* GetRenderer(UNiagaraSystem* System, const FString& EmitterHandleId,
        int32 RendererIndex, FVersionedNiagaraEmitterData** OutEmitterData = nullptr);
};
```

**File:** `Source/MonolithNiagara/Public/MonolithNiagaraBatchOps.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Batch operations. Ports UNiagaraMCPBatchLibrary (2 functions).
 * Total: 2 actions.
 */
class FMonolithNiagaraBatchOps
{
public:
    static TSharedPtr<FJsonObject> BatchExecute(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> CreateSystemFromSpec(const TSharedPtr<FJsonObject>& Params);
};
```

**File:** `Source/MonolithNiagara/Public/MonolithNiagaraDIOps.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Data Interface operations. Ports UNiagaraMCPDILibrary (1 function) + 3 new.
 * Total: 4 actions.
 */
class FMonolithNiagaraDIOps
{
public:
    static TSharedPtr<FJsonObject> GetDataInterfaceFunctions(const TSharedPtr<FJsonObject>& Params);  // PORTED
    static TSharedPtr<FJsonObject> ListDataInterfaces(const TSharedPtr<FJsonObject>& Params);         // NEW
    static TSharedPtr<FJsonObject> GetDIInfo(const TSharedPtr<FJsonObject>& Params);                  // NEW
    static TSharedPtr<FJsonObject> GetDIProperties(const TSharedPtr<FJsonObject>& Params);            // NEW
};
```

**File:** `Source/MonolithNiagara/Public/MonolithNiagaraHLSLOps.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * HLSL operations. Ports UNiagaraMCPHLSLLibrary (1 function).
 * Total: 1 action.
 */
class FMonolithNiagaraHLSLOps
{
public:
    static TSharedPtr<FJsonObject> GetCompiledGPUHLSL(const TSharedPtr<FJsonObject>& Params);  // PORTED
};
```

**File:** `Source/MonolithNiagara/Public/MonolithNiagaraValidationOps.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * Validation operations. All new (4 from Python server).
 * Total: 4 actions.
 */
class FMonolithNiagaraValidationOps
{
public:
    static TSharedPtr<FJsonObject> ValidateSystem(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetCompilationStatus(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> GetCompileErrors(const TSharedPtr<FJsonObject>& Params);
    static TSharedPtr<FJsonObject> CheckGpuCompatibility(const TSharedPtr<FJsonObject>& Params);
};
```

### Step 5: Actions Implementation — Dispatch

**File:** `Source/MonolithNiagara/Private/MonolithNiagaraActions.cpp`

```cpp
#include "MonolithNiagaraActions.h"
#include "MonolithNiagaraSystemOps.h"
#include "MonolithNiagaraModuleOps.h"
#include "MonolithNiagaraParameterOps.h"
#include "MonolithNiagaraRendererOps.h"
#include "MonolithNiagaraBatchOps.h"
#include "MonolithNiagaraDIOps.h"
#include "MonolithNiagaraHLSLOps.h"
#include "MonolithNiagaraValidationOps.h"

void FMonolithNiagaraActions::RegisterActions()
{
    // Register "niagara" namespace dispatcher with MonolithCore ToolRegistry.
    // FMonolithToolRegistry::Get().RegisterNamespace(
    //     TEXT("niagara"),
    //     TEXT("Niagara systems, emitters, modules, parameters, renderers, HLSL"),
    //     &FMonolithNiagaraActions::Dispatch
    // );

    // Discovery metadata: 70 actions across 9 sub-categories
    // (Listed in header doc comment)
}

TSharedPtr<FJsonObject> FMonolithNiagaraActions::Dispatch(const FString& Action, const TSharedPtr<FJsonObject>& Params)
{
    // ── System (17) ─────────────────────────────────────────────────────
    if (Action == TEXT("list_systems"))          return FMonolithNiagaraSystemOps::ListSystems(Params);
    if (Action == TEXT("get_system_info"))       return FMonolithNiagaraSystemOps::GetSystemInfo(Params);
    if (Action == TEXT("get_system_emitters"))   return FMonolithNiagaraSystemOps::GetSystemEmitters(Params);
    if (Action == TEXT("get_system_bounds"))     return FMonolithNiagaraSystemOps::GetSystemBounds(Params);
    if (Action == TEXT("get_system_fixed_bounds")) return FMonolithNiagaraSystemOps::GetSystemFixedBounds(Params);
    if (Action == TEXT("create_system"))         return FMonolithNiagaraSystemOps::CreateNiagaraSystem(Params);
    if (Action == TEXT("compile"))               return FMonolithNiagaraSystemOps::RequestCompile(Params);
    if (Action == TEXT("add_emitter"))           return FMonolithNiagaraSystemOps::AddEmitter(Params);
    if (Action == TEXT("remove_emitter"))        return FMonolithNiagaraSystemOps::RemoveEmitter(Params);
    if (Action == TEXT("duplicate_emitter"))     return FMonolithNiagaraSystemOps::DuplicateEmitter(Params);
    if (Action == TEXT("set_emitter_enabled"))   return FMonolithNiagaraSystemOps::SetEmitterEnabled(Params);
    if (Action == TEXT("reorder_emitters"))      return FMonolithNiagaraSystemOps::ReorderEmitters(Params);
    if (Action == TEXT("set_emitter_property"))  return FMonolithNiagaraSystemOps::SetEmitterProperty(Params);
    if (Action == TEXT("set_system_fixed_bounds")) return FMonolithNiagaraSystemOps::SetSystemFixedBounds(Params);
    if (Action == TEXT("set_system_warmup"))     return FMonolithNiagaraSystemOps::SetSystemWarmup(Params);
    if (Action == TEXT("set_system_determinism")) return FMonolithNiagaraSystemOps::SetSystemDeterminism(Params);
    if (Action == TEXT("set_system_scalability")) return FMonolithNiagaraSystemOps::SetSystemScalability(Params);

    // ── Emitter (7) ─────────────────────────────────────────────────────
    if (Action == TEXT("get_emitter_info"))      return FMonolithNiagaraSystemOps::GetSystemInfo(Params); // reuses with emitter focus
    if (Action == TEXT("get_emitter_summary"))   return FMonolithNiagaraSystemOps::GetSystemEmitters(Params);
    if (Action == TEXT("list_emitter_assets"))   return FMonolithNiagaraSystemOps::ListSystems(Params); // filters by emitter class
    if (Action == TEXT("set_emitter_sim_target")) return FMonolithNiagaraSystemOps::SetEmitterProperty(Params);
    if (Action == TEXT("set_emitter_local_space")) return FMonolithNiagaraSystemOps::SetEmitterProperty(Params);
    if (Action == TEXT("set_emitter_fixed_bounds")) return FMonolithNiagaraSystemOps::SetEmitterProperty(Params);
    if (Action == TEXT("set_emitter_scalability")) return FMonolithNiagaraSystemOps::SetEmitterProperty(Params);

    // ── Module (15) ─────────────────────────────────────────────────────
    if (Action == TEXT("get_ordered_modules"))   return FMonolithNiagaraModuleOps::GetOrderedModules(Params);
    if (Action == TEXT("get_module_inputs"))     return FMonolithNiagaraModuleOps::GetModuleInputs(Params);
    if (Action == TEXT("get_module_graph"))      return FMonolithNiagaraModuleOps::GetModuleGraph(Params);
    if (Action == TEXT("add_module"))            return FMonolithNiagaraModuleOps::AddModule(Params);
    if (Action == TEXT("remove_module"))         return FMonolithNiagaraModuleOps::RemoveModule(Params);
    if (Action == TEXT("move_module"))           return FMonolithNiagaraModuleOps::MoveModule(Params);
    if (Action == TEXT("set_module_enabled"))    return FMonolithNiagaraModuleOps::SetModuleEnabled(Params);
    if (Action == TEXT("set_module_input_value")) return FMonolithNiagaraModuleOps::SetModuleInputValue(Params);
    if (Action == TEXT("set_module_input_binding")) return FMonolithNiagaraModuleOps::SetModuleInputBinding(Params);
    if (Action == TEXT("set_module_input_di"))   return FMonolithNiagaraModuleOps::SetModuleInputDI(Params);
    if (Action == TEXT("create_module_from_hlsl")) return FMonolithNiagaraModuleOps::CreateModuleFromHLSL(Params);
    if (Action == TEXT("create_function_from_hlsl")) return FMonolithNiagaraModuleOps::CreateFunctionFromHLSL(Params);
    if (Action == TEXT("list_available_modules")) return FMonolithNiagaraModuleOps::ListAvailableModules(Params);
    if (Action == TEXT("search_modules"))        return FMonolithNiagaraModuleOps::SearchModules(Params);
    if (Action == TEXT("get_module_description")) return FMonolithNiagaraModuleOps::GetModuleDescription(Params);

    // ── Parameter (11) ──────────────────────────────────────────────────
    if (Action == TEXT("get_all_parameters"))    return FMonolithNiagaraParameterOps::GetAllParameters(Params);
    if (Action == TEXT("get_user_parameters"))   return FMonolithNiagaraParameterOps::GetUserParameters(Params);
    if (Action == TEXT("get_parameter_value"))   return FMonolithNiagaraParameterOps::GetParameterValue(Params);
    if (Action == TEXT("get_parameter_type"))    return FMonolithNiagaraParameterOps::GetParameterType(Params);
    if (Action == TEXT("trace_parameter_binding")) return FMonolithNiagaraParameterOps::TraceParameterBinding(Params);
    if (Action == TEXT("add_user_parameter"))    return FMonolithNiagaraParameterOps::AddUserParameter(Params);
    if (Action == TEXT("remove_user_parameter")) return FMonolithNiagaraParameterOps::RemoveUserParameter(Params);
    if (Action == TEXT("set_parameter_default")) return FMonolithNiagaraParameterOps::SetParameterDefault(Params);
    if (Action == TEXT("set_curve_value"))       return FMonolithNiagaraParameterOps::SetCurveValue(Params);
    if (Action == TEXT("list_parameter_types"))  return FMonolithNiagaraParameterOps::ListParameterTypes(Params);
    if (Action == TEXT("get_parameter_namespace_info")) return FMonolithNiagaraParameterOps::GetParameterNamespaceInfo(Params);

    // ── Renderer (9) ────────────────────────────────────────────────────
    if (Action == TEXT("add_renderer"))          return FMonolithNiagaraRendererOps::AddRenderer(Params);
    if (Action == TEXT("remove_renderer"))       return FMonolithNiagaraRendererOps::RemoveRenderer(Params);
    if (Action == TEXT("set_renderer_material")) return FMonolithNiagaraRendererOps::SetRendererMaterial(Params);
    if (Action == TEXT("set_renderer_property")) return FMonolithNiagaraRendererOps::SetRendererProperty(Params);
    if (Action == TEXT("get_renderer_bindings")) return FMonolithNiagaraRendererOps::GetRendererBindings(Params);
    if (Action == TEXT("set_renderer_binding"))  return FMonolithNiagaraRendererOps::SetRendererBinding(Params);
    if (Action == TEXT("list_renderer_types"))   return FMonolithNiagaraRendererOps::ListRendererTypes(Params);
    if (Action == TEXT("get_renderer_info"))     return FMonolithNiagaraRendererOps::GetRendererInfo(Params);
    if (Action == TEXT("list_renderer_bindings_schema")) return FMonolithNiagaraRendererOps::ListRendererBindingsSchema(Params);

    // ── Batch (2) ───────────────────────────────────────────────────────
    if (Action == TEXT("batch_execute"))         return FMonolithNiagaraBatchOps::BatchExecute(Params);
    if (Action == TEXT("create_from_spec"))      return FMonolithNiagaraBatchOps::CreateSystemFromSpec(Params);

    // ── DI (4) ──────────────────────────────────────────────────────────
    if (Action == TEXT("get_di_functions"))      return FMonolithNiagaraDIOps::GetDataInterfaceFunctions(Params);
    if (Action == TEXT("list_data_interfaces"))  return FMonolithNiagaraDIOps::ListDataInterfaces(Params);
    if (Action == TEXT("get_di_info"))           return FMonolithNiagaraDIOps::GetDIInfo(Params);
    if (Action == TEXT("get_di_properties"))     return FMonolithNiagaraDIOps::GetDIProperties(Params);

    // ── HLSL (1) ────────────────────────────────────────────────────────
    if (Action == TEXT("get_compiled_gpu_hlsl")) return FMonolithNiagaraHLSLOps::GetCompiledGPUHLSL(Params);

    // ── Validation (4) ──────────────────────────────────────────────────
    if (Action == TEXT("validate_system"))       return FMonolithNiagaraValidationOps::ValidateSystem(Params);
    if (Action == TEXT("get_compilation_status")) return FMonolithNiagaraValidationOps::GetCompilationStatus(Params);
    if (Action == TEXT("get_compile_errors"))    return FMonolithNiagaraValidationOps::GetCompileErrors(Params);
    if (Action == TEXT("check_gpu_compatibility")) return FMonolithNiagaraValidationOps::CheckGpuCompatibility(Params);

    // ── Unknown ─────────────────────────────────────────────────────────
    TSharedPtr<FJsonObject> Error = MakeShared<FJsonObject>();
    Error->SetBoolField(TEXT("success"), false);
    Error->SetStringField(TEXT("error"), FString::Printf(TEXT("Unknown niagara action: %s"), *Action));
    Error->SetStringField(TEXT("code"), TEXT("INVALID_ACTION"));
    return Error;
}
```

### Step 6: Sub-Operations Implementation — Porting Strategy

**File:** `Source/MonolithNiagara/Private/MonolithNiagaraSystemOps.cpp`

```cpp
#include "MonolithNiagaraSystemOps.h"

// Same includes as NiagaraSystemLibrary.cpp
#include "NiagaraSystem.h"
#include "NiagaraEmitter.h"
#include "NiagaraEmitterHandle.h"
#include "NiagaraScriptSourceBase.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Editor.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

// ─────────────────────────────────────────────────────────────────────────────
// Helpers — COPIED from NiagaraSystemLibrary.cpp
// ─────────────────────────────────────────────────────────────────────────────

UNiagaraSystem* FMonolithNiagaraSystemOps::LoadSystem(const FString& SystemPath)
{
    // >>> COPY from NiagaraSystemLibrary.cpp:31-38 <<<
    return LoadObject<UNiagaraSystem>(nullptr, *SystemPath);
}

int32 FMonolithNiagaraSystemOps::FindEmitterHandleIndex(UNiagaraSystem* System, const FString& HandleIdOrName)
{
    // >>> COPY from NiagaraSystemLibrary.cpp:41-end of function <<<
    // Tries GUID first, then falls back to name match
    if (!System) return INDEX_NONE;

    const TArray<FNiagaraEmitterHandle>& Handles = System->GetEmitterHandles();

    // Try GUID first
    FGuid TestGuid;
    if (FGuid::Parse(HandleIdOrName, TestGuid))
    {
        for (int32 i = 0; i < Handles.Num(); ++i)
        {
            if (Handles[i].GetId() == TestGuid) return i;
        }
    }

    // Fall back to name
    for (int32 i = 0; i < Handles.Num(); ++i)
    {
        if (Handles[i].GetName().ToString() == HandleIdOrName) return i;
    }

    return INDEX_NONE;
}

// ─────────────────────────────────────────────────────────────────────────────
// PORTED ACTIONS
//
// Porting pattern (same as Animation module):
//   1. FString/bool return → TSharedPtr<FJsonObject> return
//   2. Direct params → extract from Params JSON
//   3. Body logic copied verbatim from NiagaraSystemLibrary.cpp
//
// Source references:
//   AddEmitter           → NiagaraSystemLibrary.cpp (AddEmitter function)
//   RemoveEmitter        → NiagaraSystemLibrary.cpp (RemoveEmitter function)
//   DuplicateEmitter     → NiagaraSystemLibrary.cpp (DuplicateEmitter function)
//   SetEmitterEnabled    → NiagaraSystemLibrary.cpp (SetEmitterEnabled function)
//   ReorderEmitters      → NiagaraSystemLibrary.cpp (ReorderEmitters function)
//   SetEmitterProperty   → NiagaraSystemLibrary.cpp (SetEmitterProperty function)
//   RequestCompile       → NiagaraSystemLibrary.cpp (RequestCompile function)
//   CreateNiagaraSystem  → NiagaraSystemLibrary.cpp (CreateNiagaraSystem function)
// ─────────────────────────────────────────────────────────────────────────────

// Example ported action:
TSharedPtr<FJsonObject> FMonolithNiagaraSystemOps::AddEmitter(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath = Params->GetStringField(TEXT("system_path"));
    FString EmitterAssetPath = Params->GetStringField(TEXT("emitter_asset_path"));
    FString EmitterName = Params->HasField(TEXT("emitter_name"))
        ? Params->GetStringField(TEXT("emitter_name")) : TEXT("");

    UNiagaraSystem* System = LoadSystem(SystemPath);
    if (!System)
    {
        TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
        Err->SetBoolField(TEXT("success"), false);
        Err->SetStringField(TEXT("error"), FString::Printf(TEXT("System not found: %s"), *SystemPath));
        return Err;
    }

    // >>> BODY from NiagaraSystemLibrary.cpp AddEmitter <<<
    // Load emitter asset, add handle, set name, return GUID
    // (exact code copied from existing implementation)

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("success"), true);
    // Root->SetStringField(TEXT("emitter_handle_id"), NewHandle.GetId().ToString());
    return Root;
}

// ─────────────────────────────────────────────────────────────────────────────
// NEW ACTIONS
// ─────────────────────────────────────────────────────────────────────────────

TSharedPtr<FJsonObject> FMonolithNiagaraSystemOps::ListSystems(const TSharedPtr<FJsonObject>& Params)
{
    FString PathFilter = Params->HasField(TEXT("path_filter"))
        ? Params->GetStringField(TEXT("path_filter")) : TEXT("/Game");

    FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
    TArray<FAssetData> Assets;
    ARM.Get().GetAssetsByClass(UNiagaraSystem::StaticClass()->GetClassPathName(), Assets, true);

    TArray<TSharedPtr<FJsonValue>> ResultsArr;
    for (const FAssetData& Asset : Assets)
    {
        FString Path = Asset.GetObjectPathString();
        if (!PathFilter.IsEmpty() && !Path.StartsWith(PathFilter)) continue;

        TSharedPtr<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("path"), Path);
        Item->SetStringField(TEXT("name"), Asset.AssetName.ToString());
        ResultsArr.Add(MakeShared<FJsonValueObject>(Item));
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("success"), true);
    Root->SetArrayField(TEXT("systems"), ResultsArr);
    Root->SetNumberField(TEXT("count"), ResultsArr.Num());
    return Root;
}

TSharedPtr<FJsonObject> FMonolithNiagaraSystemOps::GetSystemInfo(const TSharedPtr<FJsonObject>& Params)
{
    FString SystemPath = Params->GetStringField(TEXT("system_path"));
    UNiagaraSystem* System = LoadSystem(SystemPath);
    if (!System)
    {
        TSharedPtr<FJsonObject> Err = MakeShared<FJsonObject>();
        Err->SetBoolField(TEXT("success"), false);
        Err->SetStringField(TEXT("error"), FString::Printf(TEXT("System not found: %s"), *SystemPath));
        return Err;
    }

    TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("success"), true);
    Root->SetStringField(TEXT("path"), SystemPath);
    Root->SetNumberField(TEXT("emitter_count"), System->GetEmitterHandles().Num());
    Root->SetBoolField(TEXT("has_fixed_bounds"), System->bFixedBounds);

    // Emitter list
    TArray<TSharedPtr<FJsonValue>> EmittersArr;
    for (const FNiagaraEmitterHandle& Handle : System->GetEmitterHandles())
    {
        TSharedPtr<FJsonObject> EmitterObj = MakeShared<FJsonObject>();
        EmitterObj->SetStringField(TEXT("id"), Handle.GetId().ToString());
        EmitterObj->SetStringField(TEXT("name"), Handle.GetName().ToString());
        EmitterObj->SetBoolField(TEXT("enabled"), Handle.GetIsEnabled());
        EmittersArr.Add(MakeShared<FJsonValueObject>(EmitterObj));
    }
    Root->SetArrayField(TEXT("emitters"), EmittersArr);

    return Root;
}

// Remaining new actions (GetSystemEmitters, GetSystemBounds, GetSystemFixedBounds,
// SetSystemFixedBounds, SetSystemWarmup, SetSystemDeterminism, SetSystemScalability)
// follow the same pattern: load system, read/write properties, return JSON.
```

### Step 7: Remaining Sub-Ops Implementation Pattern

Each sub-ops `.cpp` file follows the same structure:

```
1. Include its header + matching NiagaraMCPBridge header for reference
2. Copy helpers verbatim (LoadSystem, FindEmitterHandleIndex, etc.)
3. Port each UFUNCTION by:
   - Changing signature to TSharedPtr<FJsonObject>(TSharedPtr<FJsonObject>&)
   - Extracting params from JSON
   - Copying function body logic
4. Implement new actions using UE API directly
```

**Source references for each sub-ops file:**

| File | Ports From | Lines to Copy |
|---|---|---|
| `MonolithNiagaraModuleOps.cpp` | `NiagaraModuleLibrary.cpp` | All 12 UFUNCTION bodies + 4 helpers |
| `MonolithNiagaraParameterOps.cpp` | `NiagaraParameterLibrary.cpp` | All 9 UFUNCTION bodies + 3 helpers |
| `MonolithNiagaraRendererOps.cpp` | `NiagaraRendererLibrary.cpp` | All 6 UFUNCTION bodies + 2 helpers |
| `MonolithNiagaraBatchOps.cpp` | `NiagaraBatchLibrary.cpp` | Both UFUNCTION bodies (BatchExecute is the largest ~200 lines) |
| `MonolithNiagaraDIOps.cpp` | `NiagaraDILibrary.cpp` | 1 UFUNCTION body |
| `MonolithNiagaraHLSLOps.cpp` | `NiagaraHLSLLibrary.cpp` | 1 UFUNCTION body |
| `MonolithNiagaraValidationOps.cpp` | (new) | 4 new implementations |

### Step 8: Add to .uplugin

```json
{
    "Name": "MonolithNiagara",
    "Type": "Editor",
    "LoadingPhase": "Default",
    "Dependencies": ["MonolithCore"]
}
```

### Step 9: Verification

```
# 1. Compile
Build MonolithNiagara module

# 2. Check registration
Call monolith.discover("niagara") → should return 70 actions

# 3. Test ported system ops
niagara.query("get_system_info", {"system_path": "/Game/VFX/NS_Fire"})
niagara.query("add_emitter", {"system_path": "/Game/VFX/NS_Fire", "emitter_asset_path": "/Niagara/DefaultAssets/...", "emitter_name": "TestEmitter"})

# 4. Test ported module ops
niagara.query("get_ordered_modules", {"system_path": "/Game/VFX/NS_Fire", "emitter_handle_id": "Sparks", "script_usage": "particle_update"})
niagara.query("set_module_input_value", {"system_path": "/Game/VFX/NS_Fire", "emitter_handle_id": "Sparks", "module_node_guid": "...", "input_name": "Lifetime", "value_json": "2.0"})

# 5. Test ported parameter ops
niagara.query("get_user_parameters", {"system_path": "/Game/VFX/NS_Fire"})
niagara.query("add_user_parameter", {"system_path": "/Game/VFX/NS_Fire", "param_name": "SpawnRate", "type_name": "float", "default_value_json": "100.0"})

# 6. Test batch ops
niagara.query("batch_execute", {"system_path": "/Game/VFX/NS_Fire", "operations_json": "[{\"op\":\"add_renderer\",\"emitter\":\"Sparks\",\"class\":\"sprite\"}]"})

# 7. Test new actions
niagara.query("list_systems", {"path_filter": "/Game/VFX"})
niagara.query("validate_system", {"system_path": "/Game/VFX/NS_Fire"})

# 8. Verify undo for mutations
Edit → Undo after add_emitter should revert
```

---

## Summary — Phase 3 Totals

| Module | Ported UFUNCTIONs | New C++ Actions | Total | Files Created |
|---|---|---|---|---|
| MonolithAnimation | 23 | 39 | **62** | 5 |
| MonolithNiagara | 39 | 31 | **70** | 21 |
| **Phase 3 Total** | **62** | **70** | **132** | **26** |

### Key Porting Rules

1. **Function bodies are copied verbatim** from existing C++ plugins. The UE API calls, transaction patterns, and JSON serialization logic are proven and tested.

2. **Only the interface changes**: `FString Foo(params...)` → `TSharedPtr<FJsonObject> Foo(TSharedPtr<FJsonObject>& Params)`. Extract params from JSON, return JSON object instead of string.

3. **ErrorJson/SuccessJson unified** through MonolithCore's `FMonolithJsonUtils` — no more per-plugin copies.

4. **Asset loading helpers** (LoadSystem, LoadMontage, etc.) are copied into each Actions class as private statics. They're trivial one-liners (`LoadObject<T>(nullptr, *Path)`) so duplication is acceptable.

5. **GEditor transaction pattern preserved** — all mutation actions wrap changes in `BeginTransaction`/`EndTransaction` for undo support.

6. **Niagara multi-class architecture preserved** as sub-ops classes to keep files manageable (the batch ops alone is ~200 lines, module ops ~400 lines).

### Dependencies Between Phases

- Phase 3 depends on **Phase 1** (MonolithCore) for `FMonolithToolRegistry`, `FMonolithJsonUtils`, `FMonolithAssetUtils`
- Phase 3 does NOT depend on Phase 2 (Material/Blueprint)
- Phase 3 modules are independent of each other (Animation and Niagara can be built in parallel)
