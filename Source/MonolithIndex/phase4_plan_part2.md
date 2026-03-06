## Task 4.2 — UMonolithIndexSubsystem (EditorSubsystem)

**Files:**
- Create: `Source/MonolithIndex/Public/MonolithIndexSubsystem.h`
- Create: `Source/MonolithIndex/Private/MonolithIndexSubsystem.cpp`
- Create: `Source/MonolithIndex/Public/MonolithIndexer.h` (base indexer interface)

**Overview:** EditorSubsystem that owns the database, orchestrates indexing on first launch, and provides the query API. Indexing runs on a background thread via `FRunnable`. Registers indexers and dispatches each asset to the appropriate indexer based on class.

### Step 1: Create the base indexer interface

Create `Source/MonolithIndex/Public/MonolithIndexer.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "MonolithIndexDatabase.h"

class IAssetRegistry;
struct FAssetData;

/**
 * Base interface for all asset indexers.
 * Each indexer knows how to deeply inspect one or more asset types
 * and write structured data into the index database.
 */
class MONOLITHINDEX_API IMonolithIndexer
{
public:
	virtual ~IMonolithIndexer() = default;

	/** Return the asset classes this indexer handles (e.g. "Blueprint", "Material") */
	virtual TArray<FString> GetSupportedClasses() const = 0;

	/**
	 * Index a single asset. Called on a background thread.
	 * The asset is already loaded — inspect it and write to DB.
	 * @return true if indexing succeeded
	 */
	virtual bool IndexAsset(const FAssetData& AssetData, UObject* LoadedAsset, FMonolithIndexDatabase& DB, int64 AssetId) = 0;

	/** Human-readable name for logging */
	virtual FString GetName() const = 0;
};
```

### Step 2: Create the subsystem header

Create `Source/MonolithIndex/Public/MonolithIndexSubsystem.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "MonolithIndexDatabase.h"
#include "MonolithIndexer.h"
#include "MonolithIndexSubsystem.generated.h"

DECLARE_MULTICAST_DELEGATE_TwoParams(FOnIndexingProgress, int32 /*Current*/, int32 /*Total*/);
DECLARE_MULTICAST_DELEGATE_OneParam(FOnIndexingComplete, bool /*bSuccess*/);

/**
 * Editor subsystem that orchestrates the Monolith project index.
 * Owns the SQLite database, manages indexers, runs background indexing.
 */
UCLASS()
class MONOLITHINDEX_API UMonolithIndexSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	// --- UEditorSubsystem interface ---
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Trigger a full re-index (wipes DB, re-scans everything) */
	void StartFullIndex();

	/** Is indexing currently in progress? */
	bool IsIndexing() const { return bIsIndexing; }

	/** Get indexing progress (0.0 - 1.0) */
	float GetProgress() const;

	/** Get the database (for queries). May be null if not initialized. */
	FMonolithIndexDatabase* GetDatabase() { return Database.Get(); }

	// --- Query API (called by MCP actions) ---
	TArray<FSearchResult> Search(const FString& Query, int32 Limit = 50);
	TSharedPtr<FJsonObject> FindReferences(const FString& PackagePath);
	TArray<FIndexedAsset> FindByType(const FString& AssetClass, int32 Limit = 100, int32 Offset = 0);
	TSharedPtr<FJsonObject> GetStats();
	TSharedPtr<FJsonObject> GetAssetDetails(const FString& PackagePath);

	/** Register an indexer. Takes ownership. */
	void RegisterIndexer(TSharedPtr<IMonolithIndexer> Indexer);

	// --- Delegates ---
	FOnIndexingProgress OnProgress;
	FOnIndexingComplete OnComplete;

private:
	/** Background indexing task */
	class FIndexingTask : public FRunnable
	{
	public:
		FIndexingTask(UMonolithIndexSubsystem* InOwner);

		virtual bool Init() override { return true; }
		virtual uint32 Run() override;
		virtual void Stop() override { bShouldStop = true; }

		TAtomic<bool> bShouldStop{false};
		TAtomic<int32> CurrentIndex{0};
		TAtomic<int32> TotalAssets{0};

	private:
		UMonolithIndexSubsystem* Owner;
	};

	void OnIndexingFinished(bool bSuccess);
	void RegisterDefaultIndexers();
	FString GetDatabasePath() const;
	bool ShouldAutoIndex() const;

	TUniquePtr<FMonolithIndexDatabase> Database;
	TArray<TSharedPtr<IMonolithIndexer>> Indexers;
	TMap<FString, TSharedPtr<IMonolithIndexer>> ClassToIndexer; // class name -> indexer

	FRunnableThread* IndexingThread = nullptr;
	TUniquePtr<FIndexingTask> IndexingTaskPtr;
	TAtomic<bool> bIsIndexing{false};
};
```

### Step 3: Create the subsystem implementation

Create `Source/MonolithIndex/Private/MonolithIndexSubsystem.cpp`:

```cpp
#include "MonolithIndexSubsystem.h"
#include "MonolithIndexDatabase.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "HAL/RunnableThread.h"
#include "UObject/UObjectIterator.h"
#include "Async/Async.h"

// Forward-declare default indexers (implemented in Task 4.3)
#include "Indexers/BlueprintIndexer.h"
#include "Indexers/MaterialIndexer.h"
#include "Indexers/GenericAssetIndexer.h"
#include "Indexers/DependencyIndexer.h"

void UMonolithIndexSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	Database = MakeUnique<FMonolithIndexDatabase>();
	FString DbPath = GetDatabasePath();

	if (!Database->Open(DbPath))
	{
		UE_LOG(LogMonolithIndex, Error, TEXT("Failed to open index database at %s"), *DbPath);
		return;
	}

	RegisterDefaultIndexers();

	// Check if we should auto-index on first launch
	if (ShouldAutoIndex())
	{
		UE_LOG(LogMonolithIndex, Log, TEXT("First launch detected — starting full project index"));
		StartFullIndex();
	}
}

void UMonolithIndexSubsystem::Deinitialize()
{
	// Stop any running indexing
	if (IndexingTaskPtr.IsValid())
	{
		IndexingTaskPtr->Stop();
		if (IndexingThread)
		{
			IndexingThread->WaitForCompletion();
			delete IndexingThread;
			IndexingThread = nullptr;
		}
		IndexingTaskPtr.Reset();
	}

	if (Database.IsValid())
	{
		Database->Close();
	}

	Super::Deinitialize();
}

void UMonolithIndexSubsystem::RegisterIndexer(TSharedPtr<IMonolithIndexer> Indexer)
{
	if (!Indexer.IsValid()) return;

	Indexers.Add(Indexer);
	for (const FString& ClassName : Indexer->GetSupportedClasses())
	{
		ClassToIndexer.Add(ClassName, Indexer);
	}

	UE_LOG(LogMonolithIndex, Verbose, TEXT("Registered indexer: %s (%d classes)"),
		*Indexer->GetName(), Indexer->GetSupportedClasses().Num());
}

void UMonolithIndexSubsystem::RegisterDefaultIndexers()
{
	RegisterIndexer(MakeShared<FBlueprintIndexer>());
	RegisterIndexer(MakeShared<FMaterialIndexer>());
	RegisterIndexer(MakeShared<FGenericAssetIndexer>());
	RegisterIndexer(MakeShared<FDependencyIndexer>());
	// Additional indexers added in later tasks:
	// RegisterIndexer(MakeShared<FAnimationIndexer>());
	// RegisterIndexer(MakeShared<FNiagaraIndexer>());
	// RegisterIndexer(MakeShared<FDataTableIndexer>());
	// RegisterIndexer(MakeShared<FLevelIndexer>());
	// RegisterIndexer(MakeShared<FGameplayTagIndexer>());
	// RegisterIndexer(MakeShared<FConfigIndexer>());
	// RegisterIndexer(MakeShared<FCppIndexer>());
}

void UMonolithIndexSubsystem::StartFullIndex()
{
	if (bIsIndexing)
	{
		UE_LOG(LogMonolithIndex, Warning, TEXT("Indexing already in progress"));
		return;
	}

	bIsIndexing = true;

	// Reset the database for a full re-index
	Database->ResetDatabase();

	// Mark that we've done the initial index
	Database->BeginTransaction();
	FString SQL = TEXT("INSERT OR REPLACE INTO meta (key, value) VALUES ('last_full_index', datetime('now'));");
	Database->CommitTransaction();

	// Launch background thread
	IndexingTaskPtr = MakeUnique<FIndexingTask>(this);
	IndexingThread = FRunnableThread::Create(
		IndexingTaskPtr.Get(),
		TEXT("MonolithIndexing"),
		0, // stack size (default)
		TPri_BelowNormal
	);

	UE_LOG(LogMonolithIndex, Log, TEXT("Background indexing started"));
}

float UMonolithIndexSubsystem::GetProgress() const
{
	if (!IndexingTaskPtr.IsValid() || IndexingTaskPtr->TotalAssets == 0) return 0.0f;
	return static_cast<float>(IndexingTaskPtr->CurrentIndex) / static_cast<float>(IndexingTaskPtr->TotalAssets);
}

// ============================================================
// Query API wrappers
// ============================================================

TArray<FSearchResult> UMonolithIndexSubsystem::Search(const FString& Query, int32 Limit)
{
	if (!Database.IsValid() || !Database->IsOpen()) return {};
	return Database->FullTextSearch(Query, Limit);
}

TSharedPtr<FJsonObject> UMonolithIndexSubsystem::FindReferences(const FString& PackagePath)
{
	if (!Database.IsValid() || !Database->IsOpen()) return nullptr;
	return Database->FindReferences(PackagePath);
}

TArray<FIndexedAsset> UMonolithIndexSubsystem::FindByType(const FString& AssetClass, int32 Limit, int32 Offset)
{
	if (!Database.IsValid() || !Database->IsOpen()) return {};
	return Database->FindByType(AssetClass, Limit, Offset);
}

TSharedPtr<FJsonObject> UMonolithIndexSubsystem::GetStats()
{
	if (!Database.IsValid() || !Database->IsOpen()) return nullptr;
	return Database->GetStats();
}

TSharedPtr<FJsonObject> UMonolithIndexSubsystem::GetAssetDetails(const FString& PackagePath)
{
	if (!Database.IsValid() || !Database->IsOpen()) return nullptr;
	return Database->GetAssetDetails(PackagePath);
}

// ============================================================
// Background indexing task
// ============================================================

UMonolithIndexSubsystem::FIndexingTask::FIndexingTask(UMonolithIndexSubsystem* InOwner)
	: Owner(InOwner)
{
}

uint32 UMonolithIndexSubsystem::FIndexingTask::Run()
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	// Wait for asset registry to finish scanning
	if (!AssetRegistry.IsSearchAllAssets())
	{
		AssetRegistry.SearchAllAssets(true);
	}
	AssetRegistry.WaitForCompletion();

	// Get all project assets (exclude engine content)
	TArray<FAssetData> AllAssets;
	FARFilter Filter;
	Filter.PackagePaths.Add(FName(TEXT("/Game")));
	Filter.bRecursivePaths = true;
	AssetRegistry.GetAssets(Filter, AllAssets);

	TotalAssets = AllAssets.Num();
	UE_LOG(LogMonolithIndex, Log, TEXT("Indexing %d assets..."), TotalAssets.Load());

	FMonolithIndexDatabase* DB = Owner->Database.Get();
	if (!DB || !DB->IsOpen())
	{
		AsyncTask(ENamedThreads::GameThread, [this]()
		{
			Owner->OnIndexingFinished(false);
		});
		return 1;
	}

	DB->BeginTransaction();

	int32 BatchSize = 100;
	int32 Indexed = 0;
	int32 Errors = 0;

	for (int32 i = 0; i < AllAssets.Num(); ++i)
	{
		if (bShouldStop) break;

		const FAssetData& AssetData = AllAssets[i];
		CurrentIndex = i + 1;

		// Insert the base asset record
		FIndexedAsset IndexedAsset;
		IndexedAsset.PackagePath = AssetData.PackageName.ToString();
		IndexedAsset.AssetName = AssetData.AssetName.ToString();
		IndexedAsset.AssetClass = AssetData.AssetClassInfo.GetAssetClassName().ToString();

		int64 AssetId = DB->InsertAsset(IndexedAsset);
		if (AssetId < 0)
		{
			Errors++;
			continue;
		}

		// Find the right indexer for this asset class
		FString ClassName = IndexedAsset.AssetClass;
		TSharedPtr<IMonolithIndexer>* FoundIndexer = Owner->ClassToIndexer.Find(ClassName);

		if (FoundIndexer && FoundIndexer->IsValid())
		{
			// Load the asset on the game thread for deep inspection
			UObject* LoadedAsset = nullptr;

			// Use synchronous load — we're on a background thread but UObject loading
			// must happen on the game thread. Use Async to schedule and wait.
			FEvent* LoadEvent = FPlatformProcess::GetSynchEventFromPool(true);
			AsyncTask(ENamedThreads::GameThread, [&]()
			{
				LoadedAsset = AssetData.GetAsset();
				LoadEvent->Trigger();
			});
			LoadEvent->Wait();
			FPlatformProcess::ReturnSynchEventToPool(LoadEvent);

			if (LoadedAsset)
			{
				if (!(*FoundIndexer)->IndexAsset(AssetData, LoadedAsset, *DB, AssetId))
				{
					Errors++;
				}
			}
		}

		Indexed++;

		// Commit in batches
		if (Indexed % BatchSize == 0)
		{
			DB->CommitTransaction();
			DB->BeginTransaction();

			UE_LOG(LogMonolithIndex, Log, TEXT("Indexed %d / %d assets (%d errors)"),
				Indexed, TotalAssets.Load(), Errors);

			// Fire progress on game thread
			AsyncTask(ENamedThreads::GameThread, [this]()
			{
				Owner->OnProgress.Broadcast(CurrentIndex.Load(), TotalAssets.Load());
			});
		}
	}

	DB->CommitTransaction();

	UE_LOG(LogMonolithIndex, Log, TEXT("Indexing complete: %d assets indexed, %d errors"), Indexed, Errors);

	// Now run dependency indexer (needs all assets in DB first)
	TSharedPtr<IMonolithIndexer>* DepIndexer = Owner->ClassToIndexer.Find(TEXT("__Dependencies__"));
	if (DepIndexer && DepIndexer->IsValid())
	{
		UE_LOG(LogMonolithIndex, Log, TEXT("Running dependency indexer..."));
		DB->BeginTransaction();
		// Dependency indexer processes all assets at once
		FAssetData DummyData;
		(*DepIndexer)->IndexAsset(DummyData, nullptr, *DB, 0);
		DB->CommitTransaction();
	}

	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		Owner->OnIndexingFinished(!bShouldStop);
	});

	return 0;
}

void UMonolithIndexSubsystem::OnIndexingFinished(bool bSuccess)
{
	bIsIndexing = false;

	if (IndexingThread)
	{
		IndexingThread->WaitForCompletion();
		delete IndexingThread;
		IndexingThread = nullptr;
	}

	IndexingTaskPtr.Reset();

	OnComplete.Broadcast(bSuccess);

	UE_LOG(LogMonolithIndex, Log, TEXT("Indexing %s"),
		bSuccess ? TEXT("completed successfully") : TEXT("failed or was cancelled"));
}

FString UMonolithIndexSubsystem::GetDatabasePath() const
{
	// Default: Plugins/Monolith/Saved/ProjectIndex.db
	FString PluginDir = FPaths::ProjectPluginsDir() / TEXT("Monolith") / TEXT("Saved");
	return PluginDir / TEXT("ProjectIndex.db");
}

bool UMonolithIndexSubsystem::ShouldAutoIndex() const
{
	if (!Database.IsValid() || !Database->IsOpen()) return false;

	// Check meta table for last_full_index
	// If no entry exists, this is a first launch
	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database->GetDatabase(), TEXT("SELECT value FROM meta WHERE key = 'last_full_index';"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		return false; // Already indexed before
	}
	return true;
}
```

**Note:** The `ShouldAutoIndex` method uses `GetDatabase()` — we need to add that accessor. Add to `FMonolithIndexDatabase`:

```cpp
// In MonolithIndexDatabase.h, add public method:
FSQLiteDatabase* GetDatabase() const { return Database; }
```

### Step 4: Update MonolithIndexModule to register subsystem

Modify `Source/MonolithIndex/Private/MonolithIndexModule.cpp`:

```cpp
#include "MonolithIndexModule.h"

#define LOCTEXT_NAMESPACE "FMonolithIndexModule"

void FMonolithIndexModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("Monolith — Index module loaded (5 actions, SQLite+FTS5)"));
}

void FMonolithIndexModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithIndexModule, MonolithIndex)
```

No changes needed — `UEditorSubsystem` subclasses auto-register.

**Commit:** `feat(index): Add UMonolithIndexSubsystem — background indexing orchestrator`

---
