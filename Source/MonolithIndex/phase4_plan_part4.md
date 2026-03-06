## Task 4.4 — Query Actions (MCP tool handlers)

**Files:**
- Create: `Source/MonolithIndex/Private/Actions/ProjectSearchAction.h`
- Create: `Source/MonolithIndex/Private/Actions/ProjectSearchAction.cpp`
- Create: `Source/MonolithIndex/Private/Actions/ProjectFindReferencesAction.h`
- Create: `Source/MonolithIndex/Private/Actions/ProjectFindReferencesAction.cpp`
- Create: `Source/MonolithIndex/Private/Actions/ProjectFindByTypeAction.h`
- Create: `Source/MonolithIndex/Private/Actions/ProjectFindByTypeAction.cpp`
- Create: `Source/MonolithIndex/Private/Actions/ProjectGetStatsAction.h`
- Create: `Source/MonolithIndex/Private/Actions/ProjectGetStatsAction.cpp`
- Create: `Source/MonolithIndex/Private/Actions/ProjectGetAssetDetailsAction.h`
- Create: `Source/MonolithIndex/Private/Actions/ProjectGetAssetDetailsAction.cpp`

**Overview:** Each action is a static handler function that reads params from a `TSharedPtr<FJsonObject>`, calls the subsystem query API, and returns a JSON result. These get registered in the MonolithIndex module startup via the tool registry from MonolithCore.

### Step 1: project.search — FTS5 full-text search

Create `Source/MonolithIndex/Private/Actions/ProjectSearchAction.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FProjectSearchAction
{
public:
	static TSharedPtr<FJsonObject> Execute(const TSharedPtr<FJsonObject>& Params);
	static FString GetName() { return TEXT("search"); }
	static FString GetDescription() { return TEXT("Full-text search across all indexed project assets, nodes, variables, and parameters"); }
	static TSharedPtr<FJsonObject> GetSchema();
};
```

Create `Source/MonolithIndex/Private/Actions/ProjectSearchAction.cpp`:

```cpp
#include "Actions/ProjectSearchAction.h"
#include "MonolithIndexSubsystem.h"
#include "Editor.h"

TSharedPtr<FJsonObject> FProjectSearchAction::Execute(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MakeShared<FJsonObject>();

	FString Query = Params->GetStringField(TEXT("query"));
	int32 Limit = Params->HasField(TEXT("limit")) ? Params->GetIntegerField(TEXT("limit")) : 50;

	if (Query.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("'query' parameter is required"));
		return Result;
	}

	UMonolithIndexSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMonolithIndexSubsystem>();
	if (!Subsystem)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Index subsystem not available"));
		return Result;
	}

	if (Subsystem->IsIndexing())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Indexing is currently in progress"));
		Result->SetNumberField(TEXT("progress"), Subsystem->GetProgress());
		return Result;
	}

	TArray<FSearchResult> SearchResults = Subsystem->Search(Query, Limit);

	TArray<TSharedPtr<FJsonValue>> ResultsArr;
	for (const FSearchResult& SR : SearchResults)
	{
		auto Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("asset_path"), SR.AssetPath);
		Entry->SetStringField(TEXT("asset_name"), SR.AssetName);
		Entry->SetStringField(TEXT("asset_class"), SR.AssetClass);
		Entry->SetStringField(TEXT("match_context"), SR.MatchContext);
		Entry->SetNumberField(TEXT("rank"), SR.Rank);
		ResultsArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("results"), ResultsArr);
	Result->SetNumberField(TEXT("count"), SearchResults.Num());
	return Result;
}

TSharedPtr<FJsonObject> FProjectSearchAction::GetSchema()
{
	auto Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	auto Properties = MakeShared<FJsonObject>();

	auto QueryProp = MakeShared<FJsonObject>();
	QueryProp->SetStringField(TEXT("type"), TEXT("string"));
	QueryProp->SetStringField(TEXT("description"), TEXT("FTS5 search query (supports AND, OR, NOT, prefix*)"));
	Properties->SetObjectField(TEXT("query"), QueryProp);

	auto LimitProp = MakeShared<FJsonObject>();
	LimitProp->SetStringField(TEXT("type"), TEXT("integer"));
	LimitProp->SetStringField(TEXT("description"), TEXT("Maximum results to return (default 50)"));
	Properties->SetObjectField(TEXT("limit"), LimitProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("query")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}
```

### Step 2: project.find_references — bidirectional dependency lookup

Create `Source/MonolithIndex/Private/Actions/ProjectFindReferencesAction.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FProjectFindReferencesAction
{
public:
	static TSharedPtr<FJsonObject> Execute(const TSharedPtr<FJsonObject>& Params);
	static FString GetName() { return TEXT("find_references"); }
	static FString GetDescription() { return TEXT("Find all assets that reference or are referenced by the given asset"); }
	static TSharedPtr<FJsonObject> GetSchema();
};
```

Create `Source/MonolithIndex/Private/Actions/ProjectFindReferencesAction.cpp`:

```cpp
#include "Actions/ProjectFindReferencesAction.h"
#include "MonolithIndexSubsystem.h"
#include "Editor.h"

TSharedPtr<FJsonObject> FProjectFindReferencesAction::Execute(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MakeShared<FJsonObject>();

	FString PackagePath = Params->GetStringField(TEXT("asset_path"));
	if (PackagePath.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("'asset_path' parameter is required"));
		return Result;
	}

	UMonolithIndexSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMonolithIndexSubsystem>();
	if (!Subsystem)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Index subsystem not available"));
		return Result;
	}

	TSharedPtr<FJsonObject> Refs = Subsystem->FindReferences(PackagePath);
	if (!Refs.IsValid())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Asset not found in index"));
		return Result;
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetStringField(TEXT("asset_path"), PackagePath);
	Result->SetObjectField(TEXT("references"), Refs);
	return Result;
}

TSharedPtr<FJsonObject> FProjectFindReferencesAction::GetSchema()
{
	auto Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	auto Properties = MakeShared<FJsonObject>();
	auto PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Package path of the asset (e.g. /Game/Characters/BP_Hero)"));
	Properties->SetObjectField(TEXT("asset_path"), PathProp);
	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("asset_path")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}
```

### Step 3: project.find_by_type — filter assets by class

Create `Source/MonolithIndex/Private/Actions/ProjectFindByTypeAction.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FProjectFindByTypeAction
{
public:
	static TSharedPtr<FJsonObject> Execute(const TSharedPtr<FJsonObject>& Params);
	static FString GetName() { return TEXT("find_by_type"); }
	static FString GetDescription() { return TEXT("Find all assets of a given type (e.g. Blueprint, Material, StaticMesh)"); }
	static TSharedPtr<FJsonObject> GetSchema();
};
```

Create `Source/MonolithIndex/Private/Actions/ProjectFindByTypeAction.cpp`:

```cpp
#include "Actions/ProjectFindByTypeAction.h"
#include "MonolithIndexSubsystem.h"
#include "Editor.h"

TSharedPtr<FJsonObject> FProjectFindByTypeAction::Execute(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MakeShared<FJsonObject>();

	FString AssetClass = Params->GetStringField(TEXT("asset_type"));
	int32 Limit = Params->HasField(TEXT("limit")) ? Params->GetIntegerField(TEXT("limit")) : 100;
	int32 Offset = Params->HasField(TEXT("offset")) ? Params->GetIntegerField(TEXT("offset")) : 0;

	if (AssetClass.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("'asset_type' parameter is required"));
		return Result;
	}

	UMonolithIndexSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMonolithIndexSubsystem>();
	if (!Subsystem)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Index subsystem not available"));
		return Result;
	}

	TArray<FIndexedAsset> Assets = Subsystem->FindByType(AssetClass, Limit, Offset);

	TArray<TSharedPtr<FJsonValue>> AssetsArr;
	for (const FIndexedAsset& Asset : Assets)
	{
		auto Entry = MakeShared<FJsonObject>();
		Entry->SetStringField(TEXT("package_path"), Asset.PackagePath);
		Entry->SetStringField(TEXT("asset_name"), Asset.AssetName);
		Entry->SetStringField(TEXT("asset_class"), Asset.AssetClass);
		Entry->SetNumberField(TEXT("file_size_bytes"), Asset.FileSizeBytes);
		Entry->SetStringField(TEXT("indexed_at"), Asset.IndexedAt);
		AssetsArr.Add(MakeShared<FJsonValueObject>(Entry));
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetArrayField(TEXT("assets"), AssetsArr);
	Result->SetNumberField(TEXT("count"), Assets.Num());
	Result->SetNumberField(TEXT("offset"), Offset);
	Result->SetNumberField(TEXT("limit"), Limit);
	return Result;
}

TSharedPtr<FJsonObject> FProjectFindByTypeAction::GetSchema()
{
	auto Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	auto Properties = MakeShared<FJsonObject>();

	auto TypeProp = MakeShared<FJsonObject>();
	TypeProp->SetStringField(TEXT("type"), TEXT("string"));
	TypeProp->SetStringField(TEXT("description"), TEXT("Asset class name (e.g. Blueprint, Material, StaticMesh, Texture2D, SoundWave)"));
	Properties->SetObjectField(TEXT("asset_type"), TypeProp);

	auto LimitProp = MakeShared<FJsonObject>();
	LimitProp->SetStringField(TEXT("type"), TEXT("integer"));
	LimitProp->SetStringField(TEXT("description"), TEXT("Maximum results (default 100)"));
	Properties->SetObjectField(TEXT("limit"), LimitProp);

	auto OffsetProp = MakeShared<FJsonObject>();
	OffsetProp->SetStringField(TEXT("type"), TEXT("integer"));
	OffsetProp->SetStringField(TEXT("description"), TEXT("Pagination offset (default 0)"));
	Properties->SetObjectField(TEXT("offset"), OffsetProp);

	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("asset_type")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}
```

### Step 4: project.get_stats — index statistics

Create `Source/MonolithIndex/Private/Actions/ProjectGetStatsAction.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FProjectGetStatsAction
{
public:
	static TSharedPtr<FJsonObject> Execute(const TSharedPtr<FJsonObject>& Params);
	static FString GetName() { return TEXT("get_stats"); }
	static FString GetDescription() { return TEXT("Get project index statistics — total counts by table and asset class breakdown"); }
	static TSharedPtr<FJsonObject> GetSchema();
};
```

Create `Source/MonolithIndex/Private/Actions/ProjectGetStatsAction.cpp`:

```cpp
#include "Actions/ProjectGetStatsAction.h"
#include "MonolithIndexSubsystem.h"
#include "Editor.h"

TSharedPtr<FJsonObject> FProjectGetStatsAction::Execute(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MakeShared<FJsonObject>();

	UMonolithIndexSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMonolithIndexSubsystem>();
	if (!Subsystem)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Index subsystem not available"));
		return Result;
	}

	TSharedPtr<FJsonObject> Stats = Subsystem->GetStats();
	if (!Stats.IsValid())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Failed to retrieve stats"));
		return Result;
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetBoolField(TEXT("indexing"), Subsystem->IsIndexing());
	if (Subsystem->IsIndexing())
	{
		Result->SetNumberField(TEXT("progress"), Subsystem->GetProgress());
	}
	Result->SetObjectField(TEXT("stats"), Stats);
	return Result;
}

TSharedPtr<FJsonObject> FProjectGetStatsAction::GetSchema()
{
	auto Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));
	auto Properties = MakeShared<FJsonObject>();
	Schema->SetObjectField(TEXT("properties"), Properties);
	return Schema;
}
```

### Step 5: project.get_asset_details — deep asset inspection

Create `Source/MonolithIndex/Private/Actions/ProjectGetAssetDetailsAction.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FProjectGetAssetDetailsAction
{
public:
	static TSharedPtr<FJsonObject> Execute(const TSharedPtr<FJsonObject>& Params);
	static FString GetName() { return TEXT("get_asset_details"); }
	static FString GetDescription() { return TEXT("Get deep details for a specific asset — nodes, variables, parameters, dependencies"); }
	static TSharedPtr<FJsonObject> GetSchema();
};
```

Create `Source/MonolithIndex/Private/Actions/ProjectGetAssetDetailsAction.cpp`:

```cpp
#include "Actions/ProjectGetAssetDetailsAction.h"
#include "MonolithIndexSubsystem.h"
#include "Editor.h"

TSharedPtr<FJsonObject> FProjectGetAssetDetailsAction::Execute(const TSharedPtr<FJsonObject>& Params)
{
	auto Result = MakeShared<FJsonObject>();

	FString PackagePath = Params->GetStringField(TEXT("asset_path"));
	if (PackagePath.IsEmpty())
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("'asset_path' parameter is required"));
		return Result;
	}

	UMonolithIndexSubsystem* Subsystem = GEditor->GetEditorSubsystem<UMonolithIndexSubsystem>();
	if (!Subsystem)
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Index subsystem not available"));
		return Result;
	}

	TSharedPtr<FJsonObject> Details = Subsystem->GetAssetDetails(PackagePath);
	if (!Details.IsValid() || !Details->HasField(TEXT("asset_name")))
	{
		Result->SetBoolField(TEXT("success"), false);
		Result->SetStringField(TEXT("error"), TEXT("Asset not found in index"));
		return Result;
	}

	Result->SetBoolField(TEXT("success"), true);
	Result->SetObjectField(TEXT("asset"), Details);
	return Result;
}

TSharedPtr<FJsonObject> FProjectGetAssetDetailsAction::GetSchema()
{
	auto Schema = MakeShared<FJsonObject>();
	Schema->SetStringField(TEXT("type"), TEXT("object"));

	auto Properties = MakeShared<FJsonObject>();
	auto PathProp = MakeShared<FJsonObject>();
	PathProp->SetStringField(TEXT("type"), TEXT("string"));
	PathProp->SetStringField(TEXT("description"), TEXT("Package path of the asset (e.g. /Game/Characters/BP_Hero)"));
	Properties->SetObjectField(TEXT("asset_path"), PathProp);
	Schema->SetObjectField(TEXT("properties"), Properties);

	TArray<TSharedPtr<FJsonValue>> Required;
	Required.Add(MakeShared<FJsonValueString>(TEXT("asset_path")));
	Schema->SetArrayField(TEXT("required"), Required);

	return Schema;
}
```

### Step 6: Register actions in module startup

Modify `Source/MonolithIndex/Private/MonolithIndexModule.cpp`:

```cpp
#include "MonolithIndexModule.h"
// Include MonolithCore tool registry (defined in Phase 1)
// #include "MonolithToolRegistry.h"
#include "Actions/ProjectSearchAction.h"
#include "Actions/ProjectFindReferencesAction.h"
#include "Actions/ProjectFindByTypeAction.h"
#include "Actions/ProjectGetStatsAction.h"
#include "Actions/ProjectGetAssetDetailsAction.h"

#define LOCTEXT_NAMESPACE "FMonolithIndexModule"

void FMonolithIndexModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("Monolith — Index module loaded (5 actions, SQLite+FTS5)"));

	// Register project.* actions with the tool registry
	// (Actual registration depends on MonolithCore's FMonolithToolRegistry API from Phase 1)
	// FMonolithToolRegistry::Get().RegisterAction(TEXT("project"), TEXT("search"),
	//     FProjectSearchAction::GetDescription(), FProjectSearchAction::GetSchema(),
	//     &FProjectSearchAction::Execute);
	// FMonolithToolRegistry::Get().RegisterAction(TEXT("project"), TEXT("find_references"), ...);
	// FMonolithToolRegistry::Get().RegisterAction(TEXT("project"), TEXT("find_by_type"), ...);
	// FMonolithToolRegistry::Get().RegisterAction(TEXT("project"), TEXT("get_stats"), ...);
	// FMonolithToolRegistry::Get().RegisterAction(TEXT("project"), TEXT("get_asset_details"), ...);
}

void FMonolithIndexModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithIndexModule, MonolithIndex)
```

**Commit:** `feat(index): Add 5 project.* query actions — search, find_references, find_by_type, get_stats, get_asset_details`

---

## Task 4.5 — Progress Reporting (Slate Notification Bar)

**Files:**
- Create: `Source/MonolithIndex/Private/MonolithIndexNotification.h`
- Create: `Source/MonolithIndex/Private/MonolithIndexNotification.cpp`
- Modify: `Source/MonolithIndex/Private/MonolithIndexSubsystem.cpp` (hook up notification)

**Overview:** Uses `FNotificationInfo` + `SNotificationItem` to show a non-blocking notification bar in the editor with a progress bar during indexing. Updates every batch tick.

### Step 1: Create the notification handler

Create `Source/MonolithIndex/Private/MonolithIndexNotification.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Framework/Notifications/NotificationManager.h"

/**
 * Manages the Slate notification for indexing progress.
 * Shows a persistent notification with progress updates,
 * auto-dismisses on completion.
 */
class FMonolithIndexNotification
{
public:
	/** Show the indexing notification */
	void Start();

	/** Update progress (0.0 - 1.0) with current/total counts */
	void UpdateProgress(int32 Current, int32 Total);

	/** Mark indexing as complete */
	void Finish(bool bSuccess);

private:
	TWeakPtr<SNotificationItem> NotificationItem;
};
```

Create `Source/MonolithIndex/Private/MonolithIndexNotification.cpp`:

```cpp
#include "MonolithIndexNotification.h"

void FMonolithIndexNotification::Start()
{
	// Must be on game thread
	check(IsInGameThread());

	FNotificationInfo Info(FText::FromString(TEXT("Monolith: Indexing project...")));
	Info.bFireAndForget = false;
	Info.bUseThrobber = true;
	Info.bUseSuccessFailIcons = true;
	Info.ExpireDuration = 0.0f; // Don't auto-expire
	Info.FadeOutDuration = 1.0f;

	NotificationItem = FSlateNotificationManager::Get().AddNotification(Info);
	if (auto Pinned = NotificationItem.Pin())
	{
		Pinned->SetCompletionState(SNotificationItem::CS_Pending);
	}
}

void FMonolithIndexNotification::UpdateProgress(int32 Current, int32 Total)
{
	if (!IsInGameThread()) return;

	if (auto Pinned = NotificationItem.Pin())
	{
		float Pct = Total > 0 ? (static_cast<float>(Current) / static_cast<float>(Total)) * 100.0f : 0.0f;
		FText ProgressText = FText::FromString(
			FString::Printf(TEXT("Monolith: Indexing %d / %d assets (%.0f%%)"), Current, Total, Pct));
		Pinned->SetText(ProgressText);
	}
}

void FMonolithIndexNotification::Finish(bool bSuccess)
{
	if (!IsInGameThread()) return;

	if (auto Pinned = NotificationItem.Pin())
	{
		if (bSuccess)
		{
			Pinned->SetText(FText::FromString(TEXT("Monolith: Project indexing complete")));
			Pinned->SetCompletionState(SNotificationItem::CS_Success);
		}
		else
		{
			Pinned->SetText(FText::FromString(TEXT("Monolith: Project indexing failed")));
			Pinned->SetCompletionState(SNotificationItem::CS_Fail);
		}
		Pinned->ExpireAndFadeout();
	}
}
```

### Step 2: Hook up notification in the subsystem

Add to `MonolithIndexSubsystem.h`:

```cpp
// Add to private section:
#include "MonolithIndexNotification.h"
TUniquePtr<FMonolithIndexNotification> Notification;
```

Modify `MonolithIndexSubsystem.cpp`:

In `StartFullIndex()`, after creating the thread:
```cpp
// Show notification
Notification = MakeUnique<FMonolithIndexNotification>();
Notification->Start();

// Bind progress delegate
OnProgress.AddLambda([this](int32 Current, int32 Total)
{
    if (Notification.IsValid())
    {
        Notification->UpdateProgress(Current, Total);
    }
});
```

In `OnIndexingFinished()`:
```cpp
// Dismiss notification
if (Notification.IsValid())
{
    Notification->Finish(bSuccess);
    // Release after fade
    FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
        [this](float) -> bool
        {
            Notification.Reset();
            return false;
        }), 3.0f);
}
```

**Commit:** `feat(index): Add Slate notification for indexing progress`

---

## Task 4.6 — Update Build.cs and Verify Full Build

### Step 1: Verify MonolithIndex.Build.cs has all dependencies

The existing `Build.cs` already has `SQLiteCore`, `AssetRegistry`, `UnrealEd`, `Json`, `JsonUtilities`. We need to add a few more for the Blueprint/Material indexers:

```csharp
using UnrealBuildTool;

public class MonolithIndex : ModuleRules
{
	public MonolithIndex(ReadOnlyTargetRules Target) : base(Target)
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
			"AssetRegistry",
			"Json",
			"JsonUtilities",
			"SQLiteCore",
			"Slate",
			"SlateCore",
			"BlueprintGraph",    // For UK2Node types
			"KismetCompiler",    // For Blueprint graph utilities
			"EditorSubsystem"    // For UEditorSubsystem base
		});
	}
}
```

### Step 2: Full build verification

```
# Build the MonolithIndex module
# Expected output: Success with 0 errors
# Verify: All files compile, SQLiteCore links correctly
```

**Commit:** `feat(index): Update Build.cs with full dependency list`

---

## Summary — Phase 4 File List

| File | Type | Description |
|------|------|-------------|
| `Public/MonolithIndexDatabase.h` | Create | SQLite wrapper — 13 structs, CRUD + FTS5 search API |
| `Private/MonolithIndexDatabase.cpp` | Create | Full implementation — table creation SQL, all CRUD methods |
| `Public/MonolithIndexer.h` | Create | `IMonolithIndexer` base interface |
| `Public/MonolithIndexSubsystem.h` | Create | `UMonolithIndexSubsystem` — EditorSubsystem orchestrator |
| `Private/MonolithIndexSubsystem.cpp` | Create | Background FRunnable, auto-index on first launch |
| `Private/Indexers/BlueprintIndexer.h/.cpp` | Create | Walks UEdGraph nodes, pins, connections, variables |
| `Private/Indexers/MaterialIndexer.h/.cpp` | Create | Walks UMaterialExpression tree, parameters |
| `Private/Indexers/GenericAssetIndexer.h/.cpp` | Create | StaticMesh/SkeletalMesh/Texture/Sound metadata |
| `Private/Indexers/DependencyIndexer.h/.cpp` | Create | Asset Registry dependency graph edges |
| `Private/Actions/ProjectSearchAction.h/.cpp` | Create | `project.search` — FTS5 full-text search |
| `Private/Actions/ProjectFindReferencesAction.h/.cpp` | Create | `project.find_references` — bidirectional deps |
| `Private/Actions/ProjectFindByTypeAction.h/.cpp` | Create | `project.find_by_type` — filter by class |
| `Private/Actions/ProjectGetStatsAction.h/.cpp` | Create | `project.get_stats` — index statistics |
| `Private/Actions/ProjectGetAssetDetailsAction.h/.cpp` | Create | `project.get_asset_details` — deep inspection |
| `Private/MonolithIndexNotification.h/.cpp` | Create | Slate progress notification bar |
| `Private/MonolithIndexModule.cpp` | Modify | Register 5 actions |
| `MonolithIndex.Build.cs` | Modify | Add BlueprintGraph, KismetCompiler, EditorSubsystem deps |

**Total: 22 new files, 2 modified files, 5 MCP query actions, 4 indexers, 13 DB tables + 2 FTS5 indexes**

### Remaining indexers (stub for later tasks):
- `FAnimationIndexer` — sequences, montages, blend spaces, ABPs
- `FNiagaraIndexer` — systems, emitters, modules, parameters
- `FDataTableIndexer` — schema + row data
- `FLevelIndexer` — actors, components
- `FGameplayTagIndexer` — tag hierarchy + references
- `FConfigIndexer` — INI file entries
- `FCppIndexer` — delegates to MonolithSource Python for tree-sitter parsing

These follow the same `IMonolithIndexer` pattern and plug into the subsystem via `RegisterIndexer()`.
