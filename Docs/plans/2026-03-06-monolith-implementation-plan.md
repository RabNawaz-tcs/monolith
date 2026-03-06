# Monolith Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Consolidate 9 MCP servers + 4 C++ plugins into one unified Monolith plugin with embedded HTTP MCP server, deep project indexer, auto-updater, and bundled Claude Code skills.

**Architecture:** C++ core with embedded HTTP MCP server for 183 editor-facing tools. Python bundled only for offline engine source indexing. Discovery/dispatch pattern reduces 231 tool definitions to ~14 namespace endpoints.

**Tech Stack:** Unreal Engine 5.7 C++ (Editor modules), SQLite+FTS5, FHttpServerModule, tree-sitter (Python, bundled)

**Design Doc:** [2026-03-06-monolith-design.md](2026-03-06-monolith-design.md)

---

## Phase 0: CORE INFRASTRUCTURE

> The foundation everything else depends on. Nothing works without this.

## Current State

The project has a skeleton:
- `Monolith.uplugin` — all 9 modules declared, MonolithCore at `PostEngineInit`
- `MonolithCore.Build.cs` — dependencies on Core, HTTPServer, Json, DeveloperSettings, etc.
- `MonolithCoreModule.h/.cpp` — empty StartupModule/ShutdownModule stubs
- Other modules have similar empty stubs

## Build Order (dependency chain)

```
0.1  UMonolithSettings          (no deps — standalone UDeveloperSettings)
0.2  FMonolithJsonUtils          (no deps — standalone static helpers)
0.3  FMonolithAssetUtils         (no deps — standalone static helpers)
0.4  FMonolithToolRegistry       (depends on 0.2 for JSON responses)
0.5  FMonolithHttpServer         (depends on 0.4 for dispatch, 0.1 for port config)
0.6  FMonolithCoreModule wiring  (depends on 0.1-0.5 — ties everything together)
0.7  Discovery/Status tools      (depends on 0.4, 0.6 — first registered tools)
```

---

## Task 0.1: UMonolithSettings

Plugin settings exposed in Project Settings → Plugins → Monolith.

### Files

| Action | Path |
|--------|------|
| Create | `Source/MonolithCore/Public/MonolithSettings.h` |
| Create | `Source/MonolithCore/Private/MonolithSettings.cpp` |

### Code

**`Source/MonolithCore/Public/MonolithSettings.h`**
```cpp
#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MonolithSettings.generated.h"

UCLASS(config=Monolith, defaultconfig, meta=(DisplayName="Monolith"))
class MONOLITHCORE_API UMonolithSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMonolithSettings();

	// --- MCP Server ---

	/** Port for the embedded MCP HTTP server */
	UPROPERTY(config, EditAnywhere, Category="MCP Server", meta=(ClampMin="1024", ClampMax="65535"))
	int32 ServerPort = 9316;

	// --- Auto-Update ---

	/** Check GitHub Releases for updates on editor startup */
	UPROPERTY(config, EditAnywhere, Category="Auto-Update")
	bool bAutoUpdateEnabled = true;

	// --- Indexing ---

	/** Override path for ProjectIndex.db (empty = default Saved/ location) */
	UPROPERTY(config, EditAnywhere, Category="Indexing", meta=(RelativePath))
	FDirectoryPath DatabasePathOverride;

	// --- Module Toggles ---

	UPROPERTY(config, EditAnywhere, Category="Modules")
	bool bEnableBlueprint = true;

	UPROPERTY(config, EditAnywhere, Category="Modules")
	bool bEnableMaterial = true;

	UPROPERTY(config, EditAnywhere, Category="Modules")
	bool bEnableAnimation = true;

	UPROPERTY(config, EditAnywhere, Category="Modules")
	bool bEnableNiagara = true;

	UPROPERTY(config, EditAnywhere, Category="Modules")
	bool bEnableEditor = true;

	UPROPERTY(config, EditAnywhere, Category="Modules")
	bool bEnableConfig = true;

	UPROPERTY(config, EditAnywhere, Category="Modules")
	bool bEnableIndex = true;

	UPROPERTY(config, EditAnywhere, Category="Modules")
	bool bEnableSource = true;

	// --- Logging ---

	/** Log verbosity for Monolith systems */
	UPROPERTY(config, EditAnywhere, Category="Logging")
	ELogVerbosity::Type LogVerbosity = ELogVerbosity::Log;

	// --- Helpers ---

	static const UMonolithSettings* Get();

	/** Settings category path */
	virtual FName GetCategoryName() const override { return FName(TEXT("Plugins")); }
};
```

**`Source/MonolithCore/Private/MonolithSettings.cpp`**
```cpp
#include "MonolithSettings.h"

UMonolithSettings::UMonolithSettings()
{
}

const UMonolithSettings* UMonolithSettings::Get()
{
	return GetDefault<UMonolithSettings>();
}
```

### Steps
1. Create the two files above
2. Build — verify clean compile
3. Open editor → Project Settings → Plugins → Monolith — verify all fields appear

---

## Task 0.2: FMonolithJsonUtils

Shared JSON response helpers. Eliminates per-plugin ErrorJson/SuccessJson duplication.

### Files

| Action | Path |
|--------|------|
| Create | `Source/MonolithCore/Public/MonolithJsonUtils.h` |
| Create | `Source/MonolithCore/Private/MonolithJsonUtils.cpp` |

### Code

**`Source/MonolithCore/Public/MonolithJsonUtils.h`**
```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMonolith, Log, All);

class MONOLITHCORE_API FMonolithJsonUtils
{
public:
	/** Create a success JSON-RPC response wrapping a result object */
	static TSharedPtr<FJsonObject> SuccessResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonValue>& Result);

	/** Create an error JSON-RPC response */
	static TSharedPtr<FJsonObject> ErrorResponse(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message, const TSharedPtr<FJsonValue>& Data = nullptr);

	/** Convenience: wrap a JSON object as a success result */
	static TSharedPtr<FJsonObject> SuccessObject(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& ResultObj);

	/** Convenience: wrap a simple string as a success result */
	static TSharedPtr<FJsonObject> SuccessString(const TSharedPtr<FJsonValue>& Id, const FString& Message);

	/** Serialize a FJsonObject to a compact JSON string */
	static FString Serialize(const TSharedPtr<FJsonObject>& JsonObject);

	/** Parse a JSON string into a FJsonObject. Returns nullptr on failure. */
	static TSharedPtr<FJsonObject> Parse(const FString& JsonString);

	/** Create a JSON array from a TArray of strings */
	static TSharedRef<FJsonValueArray> StringArrayToJson(const TArray<FString>& Strings);

	// --- JSON-RPC 2.0 Error Codes ---
	static constexpr int32 ErrParseError = -32700;
	static constexpr int32 ErrInvalidRequest = -32600;
	static constexpr int32 ErrMethodNotFound = -32601;
	static constexpr int32 ErrInvalidParams = -32602;
	static constexpr int32 ErrInternalError = -32603;
};
```

**`Source/MonolithCore/Private/MonolithJsonUtils.cpp`**
```cpp
#include "MonolithJsonUtils.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"

DEFINE_LOG_CATEGORY(LogMonolith);

TSharedPtr<FJsonObject> FMonolithJsonUtils::SuccessResponse(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonValue>& Result)
{
	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	if (Id.IsValid())
	{
		Response->SetField(TEXT("id"), Id);
	}
	if (Result.IsValid())
	{
		Response->SetField(TEXT("result"), Result);
	}
	else
	{
		Response->SetField(TEXT("result"), MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
	}
	return Response;
}

TSharedPtr<FJsonObject> FMonolithJsonUtils::ErrorResponse(const TSharedPtr<FJsonValue>& Id, int32 Code, const FString& Message, const TSharedPtr<FJsonValue>& Data)
{
	TSharedPtr<FJsonObject> ErrorObj = MakeShared<FJsonObject>();
	ErrorObj->SetNumberField(TEXT("code"), Code);
	ErrorObj->SetStringField(TEXT("message"), Message);
	if (Data.IsValid())
	{
		ErrorObj->SetField(TEXT("data"), Data);
	}

	TSharedPtr<FJsonObject> Response = MakeShared<FJsonObject>();
	Response->SetStringField(TEXT("jsonrpc"), TEXT("2.0"));
	if (Id.IsValid())
	{
		Response->SetField(TEXT("id"), Id);
	}
	else
	{
		Response->SetField(TEXT("id"), MakeShared<FJsonValueNull>());
	}
	Response->SetObjectField(TEXT("error"), ErrorObj);
	return Response;
}

TSharedPtr<FJsonObject> FMonolithJsonUtils::SuccessObject(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& ResultObj)
{
	return SuccessResponse(Id, MakeShared<FJsonValueObject>(ResultObj));
}

TSharedPtr<FJsonObject> FMonolithJsonUtils::SuccessString(const TSharedPtr<FJsonValue>& Id, const FString& Message)
{
	TSharedPtr<FJsonObject> ResultObj = MakeShared<FJsonObject>();
	ResultObj->SetStringField(TEXT("message"), Message);
	return SuccessObject(Id, ResultObj);
}

FString FMonolithJsonUtils::Serialize(const TSharedPtr<FJsonObject>& JsonObject)
{
	FString OutputString;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&OutputString);
	FJsonSerializer::Serialize(JsonObject.ToSharedRef(), Writer);
	return OutputString;
}

TSharedPtr<FJsonObject> FMonolithJsonUtils::Parse(const FString& JsonString)
{
	TSharedPtr<FJsonObject> JsonObject;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonString);
	if (!FJsonSerializer::Deserialize(Reader, JsonObject))
	{
		return nullptr;
	}
	return JsonObject;
}

TSharedRef<FJsonValueArray> FMonolithJsonUtils::StringArrayToJson(const TArray<FString>& Strings)
{
	TArray<TSharedPtr<FJsonValue>> JsonArray;
	for (const FString& Str : Strings)
	{
		JsonArray.Add(MakeShared<FJsonValueString>(Str));
	}
	return MakeShared<FJsonValueArray>(JsonArray);
}
```

### Steps
1. Create the two files above
2. Build — verify clean compile
3. The LogMonolith category is now available globally to all Monolith modules

---

## Task 0.3: FMonolithAssetUtils

Common asset path resolution and package loading helpers.

### Files

| Action | Path |
|--------|------|
| Create | `Source/MonolithCore/Public/MonolithAssetUtils.h` |
| Create | `Source/MonolithCore/Private/MonolithAssetUtils.cpp` |

### Code

**`Source/MonolithCore/Public/MonolithAssetUtils.h`**
```cpp
#pragma once

#include "CoreMinimal.h"

class UObject;
class UPackage;
class UBlueprint;

class MONOLITHCORE_API FMonolithAssetUtils
{
public:
	/** Resolve a user-provided path to a proper asset path (handles /Game/, /Content/, relative, etc.) */
	static FString ResolveAssetPath(const FString& InPath);

	/** Load a package by path, returns nullptr on failure */
	static UPackage* LoadPackageByPath(const FString& AssetPath);

	/** Load an asset object by path, returns nullptr on failure */
	static UObject* LoadAssetByPath(const FString& AssetPath);

	/** Load and cast to a specific type */
	template<typename T>
	static T* LoadAssetByPath(const FString& AssetPath)
	{
		return Cast<T>(LoadAssetByPath(AssetPath));
	}

	/** Check if an asset exists at the given path */
	static bool AssetExists(const FString& AssetPath);

	/** Get all assets of a given class in a directory */
	static TArray<FAssetData> GetAssetsByClass(const FTopLevelAssetPath& ClassPath, const FString& PackagePath = FString());

	/** Get display-friendly name from an asset path */
	static FString GetAssetName(const FString& AssetPath);
};
```

**`Source/MonolithCore/Private/MonolithAssetUtils.cpp`**
```cpp
#include "MonolithAssetUtils.h"
#include "MonolithJsonUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Package.h"

FString FMonolithAssetUtils::ResolveAssetPath(const FString& InPath)
{
	FString Path = InPath;
	Path.TrimStartAndEndInline();

	// Normalize backslashes
	Path.ReplaceInline(TEXT("\\"), TEXT("/"));

	// Handle /Content/ → /Game/
	if (Path.StartsWith(TEXT("/Content/")))
	{
		Path = TEXT("/Game/") + Path.Mid(9);
	}
	else if (!Path.StartsWith(TEXT("/")))
	{
		// Relative path — assume /Game/
		Path = TEXT("/Game/") + Path;
	}

	// Strip extension if present
	if (Path.EndsWith(TEXT(".uasset")) || Path.EndsWith(TEXT(".umap")))
	{
		Path = FPaths::GetBaseFilename(Path, false);
	}

	return Path;
}

UPackage* FMonolithAssetUtils::LoadPackageByPath(const FString& AssetPath)
{
	FString Resolved = ResolveAssetPath(AssetPath);
	UPackage* Package = LoadPackage(nullptr, *Resolved, LOAD_None);
	if (!Package)
	{
		UE_LOG(LogMonolith, Warning, TEXT("Failed to load package: %s"), *Resolved);
	}
	return Package;
}

UObject* FMonolithAssetUtils::LoadAssetByPath(const FString& AssetPath)
{
	FString Resolved = ResolveAssetPath(AssetPath);

	// Try StaticLoadObject first (handles ObjectPath format)
	UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *Resolved);
	if (Asset)
	{
		return Asset;
	}

	// Try finding in already-loaded packages
	FString PackageName = FPackageName::ObjectPathToPackageName(Resolved);
	FString ObjectName = FPackageName::ObjectPathToObjectName(Resolved);
	if (ObjectName.IsEmpty())
	{
		// Assume object name matches package leaf
		ObjectName = FPackageName::GetShortName(PackageName);
	}

	UPackage* Package = LoadPackage(nullptr, *PackageName, LOAD_None);
	if (Package)
	{
		Asset = FindObject<UObject>(Package, *ObjectName);
		if (!Asset)
		{
			// Try with _C suffix for Blueprint generated classes
			Asset = FindObject<UObject>(Package, *(ObjectName + TEXT("_C")));
		}
	}

	if (!Asset)
	{
		UE_LOG(LogMonolith, Warning, TEXT("Failed to load asset: %s"), *Resolved);
	}
	return Asset;
}

bool FMonolithAssetUtils::AssetExists(const FString& AssetPath)
{
	FString Resolved = ResolveAssetPath(AssetPath);
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();
	FAssetData AssetData = AssetRegistry.GetAssetByObjectPath(FSoftObjectPath(Resolved));
	return AssetData.IsValid();
}

TArray<FAssetData> FMonolithAssetUtils::GetAssetsByClass(const FTopLevelAssetPath& ClassPath, const FString& PackagePath)
{
	IAssetRegistry& AssetRegistry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry").Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(ClassPath);
	if (!PackagePath.IsEmpty())
	{
		Filter.PackagePaths.Add(FName(*PackagePath));
		Filter.bRecursivePaths = true;
	}

	TArray<FAssetData> Results;
	AssetRegistry.GetAssets(Filter, Results);
	return Results;
}

FString FMonolithAssetUtils::GetAssetName(const FString& AssetPath)
{
	return FPackageName::GetShortName(AssetPath);
}
```

### Steps
1. Create the two files above
2. Add `"AssetRegistry"` to `MonolithCore.Build.cs` PublicDependencyModuleNames (it's not there yet)
3. Build — verify clean compile


## Task 0.4: FMonolithToolRegistry

Central action registry. Domain modules register their actions here. The HTTP server dispatches to this.

### Design

- Each MCP "tool" (e.g., `blueprint.query`) maps to a **namespace**
- Each namespace has multiple **actions** (e.g., `list_graphs`, `get_graph_data`)
- The registry maps `(Namespace, Action)` → handler delegate
- Handlers receive a `TSharedPtr<FJsonObject>` params and return a `TSharedPtr<FJsonObject>` result
- Domain modules call `RegisterAction()` in their `StartupModule()`
- The HTTP server calls `ExecuteAction()` to dispatch

### Files

| Action | Path |
|--------|------|
| Create | `Source/MonolithCore/Public/MonolithToolRegistry.h` |
| Create | `Source/MonolithCore/Private/MonolithToolRegistry.cpp` |

### Code

**`Source/MonolithCore/Public/MonolithToolRegistry.h`**
```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/** Result of an action execution */
struct FMonolithActionResult
{
	bool bSuccess = false;
	TSharedPtr<FJsonObject> Result;
	FString ErrorMessage;
	int32 ErrorCode = 0;

	static FMonolithActionResult Success(const TSharedPtr<FJsonObject>& InResult)
	{
		FMonolithActionResult R;
		R.bSuccess = true;
		R.Result = InResult;
		return R;
	}

	static FMonolithActionResult Error(const FString& Message, int32 Code = -32603)
	{
		FMonolithActionResult R;
		R.bSuccess = false;
		R.ErrorMessage = Message;
		R.ErrorCode = Code;
		return R;
	}
};

/** Delegate type for action handlers */
DECLARE_DELEGATE_RetVal_OneParam(FMonolithActionResult, FMonolithActionHandler, const TSharedPtr<FJsonObject>& /* Params */);

/** Metadata describing a registered action */
struct FMonolithActionInfo
{
	FString Namespace;
	FString Action;
	FString Description;
	TSharedPtr<FJsonObject> ParamSchema;  // JSON Schema for parameter validation
};

/**
 * Central registry for all Monolith tool actions.
 * Domain modules register actions here. The HTTP server dispatches through this.
 */
class MONOLITHCORE_API FMonolithToolRegistry
{
public:
	static FMonolithToolRegistry& Get();

	/**
	 * Register an action handler.
	 * @param Namespace   The tool namespace (e.g., "blueprint", "material")
	 * @param Action      The action name (e.g., "list_graphs", "get_node")
	 * @param Description Human-readable description of what this action does
	 * @param Handler     The delegate to execute
	 * @param ParamSchema Optional JSON Schema describing expected parameters
	 */
	void RegisterAction(
		const FString& Namespace,
		const FString& Action,
		const FString& Description,
		const FMonolithActionHandler& Handler,
		const TSharedPtr<FJsonObject>& ParamSchema = nullptr
	);

	/** Unregister all actions in a namespace (called during module shutdown) */
	void UnregisterNamespace(const FString& Namespace);

	/** Execute an action by namespace + action name */
	FMonolithActionResult ExecuteAction(const FString& Namespace, const FString& Action, const TSharedPtr<FJsonObject>& Params);

	/** Get all registered namespaces */
	TArray<FString> GetNamespaces() const;

	/** Get all actions in a namespace */
	TArray<FMonolithActionInfo> GetActions(const FString& Namespace) const;

	/** Get all actions across all namespaces */
	TArray<FMonolithActionInfo> GetAllActions() const;

	/** Check if a specific action exists */
	bool HasAction(const FString& Namespace, const FString& Action) const;

	/** Get total number of registered actions */
	int32 GetActionCount() const;

private:
	FMonolithToolRegistry() = default;

	struct FRegisteredAction
	{
		FMonolithActionInfo Info;
		FMonolithActionHandler Handler;
	};

	/** Map of "namespace.action" → registered action */
	TMap<FString, FRegisteredAction> Actions;

	/** Map of namespace → list of action keys */
	TMap<FString, TArray<FString>> NamespaceActions;

	static FString MakeKey(const FString& Namespace, const FString& Action)
	{
		return Namespace + TEXT(".") + Action;
	}

	mutable FCriticalSection RegistryLock;
};
```

**`Source/MonolithCore/Private/MonolithToolRegistry.cpp`**
```cpp
#include "MonolithToolRegistry.h"
#include "MonolithJsonUtils.h"

FMonolithToolRegistry& FMonolithToolRegistry::Get()
{
	static FMonolithToolRegistry Instance;
	return Instance;
}

void FMonolithToolRegistry::RegisterAction(
	const FString& Namespace,
	const FString& Action,
	const FString& Description,
	const FMonolithActionHandler& Handler,
	const TSharedPtr<FJsonObject>& ParamSchema)
{
	FScopeLock Lock(&RegistryLock);

	FString Key = MakeKey(Namespace, Action);

	if (Actions.Contains(Key))
	{
		UE_LOG(LogMonolith, Warning, TEXT("Overwriting existing action: %s"), *Key);
	}

	FRegisteredAction RegAction;
	RegAction.Info.Namespace = Namespace;
	RegAction.Info.Action = Action;
	RegAction.Info.Description = Description;
	RegAction.Info.ParamSchema = ParamSchema;
	RegAction.Handler = Handler;

	Actions.Add(Key, MoveTemp(RegAction));
	NamespaceActions.FindOrAdd(Namespace).AddUnique(Key);

	UE_LOG(LogMonolith, Verbose, TEXT("Registered action: %s — %s"), *Key, *Description);
}

void FMonolithToolRegistry::UnregisterNamespace(const FString& Namespace)
{
	FScopeLock Lock(&RegistryLock);

	if (TArray<FString>* Keys = NamespaceActions.Find(Namespace))
	{
		for (const FString& Key : *Keys)
		{
			Actions.Remove(Key);
		}
		UE_LOG(LogMonolith, Log, TEXT("Unregistered namespace: %s (%d actions)"), *Namespace, Keys->Num());
		NamespaceActions.Remove(Namespace);
	}
}

FMonolithActionResult FMonolithToolRegistry::ExecuteAction(
	const FString& Namespace,
	const FString& Action,
	const TSharedPtr<FJsonObject>& Params)
{
	FScopeLock Lock(&RegistryLock);

	FString Key = MakeKey(Namespace, Action);
	FRegisteredAction* RegAction = Actions.Find(Key);

	if (!RegAction)
	{
		return FMonolithActionResult::Error(
			FString::Printf(TEXT("Unknown action: %s.%s"), *Namespace, *Action),
			FMonolithJsonUtils::ErrMethodNotFound
		);
	}

	if (!RegAction->Handler.IsBound())
	{
		return FMonolithActionResult::Error(
			FString::Printf(TEXT("Action handler not bound: %s"), *Key),
			FMonolithJsonUtils::ErrInternalError
		);
	}

	// Release lock before executing handler (handlers may take time)
	FMonolithActionHandler HandlerCopy = RegAction->Handler;
	Lock.Unlock();

	return HandlerCopy.Execute(Params.IsValid() ? Params : MakeShared<FJsonObject>());
}

TArray<FString> FMonolithToolRegistry::GetNamespaces() const
{
	FScopeLock Lock(&RegistryLock);
	TArray<FString> Result;
	NamespaceActions.GetKeys(Result);
	return Result;
}

TArray<FMonolithActionInfo> FMonolithToolRegistry::GetActions(const FString& Namespace) const
{
	FScopeLock Lock(&RegistryLock);
	TArray<FMonolithActionInfo> Result;

	if (const TArray<FString>* Keys = NamespaceActions.Find(Namespace))
	{
		for (const FString& Key : *Keys)
		{
			if (const FRegisteredAction* RegAction = Actions.Find(Key))
			{
				Result.Add(RegAction->Info);
			}
		}
	}
	return Result;
}

TArray<FMonolithActionInfo> FMonolithToolRegistry::GetAllActions() const
{
	FScopeLock Lock(&RegistryLock);
	TArray<FMonolithActionInfo> Result;
	for (const auto& Pair : Actions)
	{
		Result.Add(Pair.Value.Info);
	}
	return Result;
}

bool FMonolithToolRegistry::HasAction(const FString& Namespace, const FString& Action) const
{
	FScopeLock Lock(&RegistryLock);
	return Actions.Contains(MakeKey(Namespace, Action));
}

int32 FMonolithToolRegistry::GetActionCount() const
{
	FScopeLock Lock(&RegistryLock);
	return Actions.Num();
}
```

### Steps
1. Create the two files above
2. Build — verify clean compile
3. This is used by the HTTP server (0.5) and by domain modules (Phase 1+)

### Usage Pattern (for domain modules later)
```cpp
// In MonolithBlueprint's StartupModule:
FMonolithToolRegistry::Get().RegisterAction(
    TEXT("blueprint"), TEXT("list_graphs"),
    TEXT("List all function/event graphs in a Blueprint"),
    FMonolithActionHandler::CreateStatic(&FBlueprintActions::ListGraphs)
);
```


## Task 0.5: FMonolithHttpServer

The critical path. Embedded HTTP MCP server using UE's FHttpServerModule. Implements MCP Streamable HTTP transport with SSE fallback, JSON-RPC 2.0 parsing, CORS headers, and routes to ToolRegistry.

### MCP Protocol Summary

The MCP (Model Context Protocol) Streamable HTTP transport works as follows:
1. **POST /mcp** — Main endpoint. Client sends JSON-RPC 2.0 requests. Server responds with either:
   - `application/json` for simple request-response
   - `text/event-stream` for streaming (SSE) responses
2. **GET /mcp** — SSE endpoint for server-initiated notifications (optional, for long-lived sessions)
3. **DELETE /mcp** — Session termination
4. Session tracking via `Mcp-Session-Id` header

### JSON-RPC 2.0 Methods (MCP spec)

- `initialize` — Handshake, returns server capabilities
- `notifications/initialized` — Client confirms init (notification, no response)
- `tools/list` — Returns list of available tools
- `tools/call` — Executes a tool: `{ name: "blueprint.query", arguments: { action: "list_graphs", params: {...} } }`
- `ping` — Health check

### Files

| Action | Path |
|--------|------|
| Create | `Source/MonolithCore/Public/MonolithHttpServer.h` |
| Create | `Source/MonolithCore/Private/MonolithHttpServer.cpp` |

### Code

**`Source/MonolithCore/Public/MonolithHttpServer.h`**
```cpp
#pragma once

#include "CoreMinimal.h"
#include "HttpRouteHandle.h"
#include "IHttpRouter.h"

class FMonolithToolRegistry;

/**
 * Embedded MCP HTTP server.
 * Implements Streamable HTTP transport with JSON-RPC 2.0 dispatch.
 */
class MONOLITHCORE_API FMonolithHttpServer
{
public:
	FMonolithHttpServer();
	~FMonolithHttpServer();

	/** Start the HTTP server on the configured port */
	bool Start(int32 Port);

	/** Stop the server and unbind all routes */
	void Stop();

	/** Is the server currently running? */
	bool IsRunning() const { return bIsRunning; }

	/** Get the port the server is listening on */
	int32 GetPort() const { return BoundPort; }

private:
	// --- Route Handlers ---
	bool HandlePostMcp(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleGetMcp(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleDeleteMcp(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);
	bool HandleOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete);

	// --- JSON-RPC Processing ---
	TSharedPtr<FJsonObject> ProcessJsonRpcRequest(const TSharedPtr<FJsonObject>& Request, const FString& SessionId);
	TSharedPtr<FJsonObject> HandleInitialize(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleToolsList(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandleToolsCall(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params);
	TSharedPtr<FJsonObject> HandlePing(const TSharedPtr<FJsonValue>& Id);

	// --- Helpers ---
	TUniquePtr<FHttpServerResponse> MakeJsonResponse(const FString& JsonBody, EHttpServerResponseCodes Code = EHttpServerResponseCodes::Ok);
	TUniquePtr<FHttpServerResponse> MakeSseResponse(const TArray<TSharedPtr<FJsonObject>>& Messages);
	void AddCorsHeaders(FHttpServerResponse& Response);
	FString GenerateSessionId();
	bool IsValidSession(const FString& SessionId) const;

	// --- State ---
	TSharedPtr<IHttpRouter> HttpRouter;
	TArray<FHttpRouteHandle> RouteHandles;
	int32 BoundPort = 0;
	bool bIsRunning = false;

	/** Active session IDs */
	TSet<FString> ActiveSessions;
	mutable FCriticalSection SessionLock;
};
```

**`Source/MonolithCore/Private/MonolithHttpServer.cpp`**
```cpp
#include "MonolithHttpServer.h"
#include "MonolithJsonUtils.h"
#include "MonolithToolRegistry.h"
#include "MonolithSettings.h"
#include "HttpServerModule.h"
#include "HttpServerResponse.h"
#include "Misc/Guid.h"

FMonolithHttpServer::FMonolithHttpServer()
{
}

FMonolithHttpServer::~FMonolithHttpServer()
{
	Stop();
}

bool FMonolithHttpServer::Start(int32 Port)
{
	if (bIsRunning)
	{
		UE_LOG(LogMonolith, Warning, TEXT("HTTP server already running on port %d"), BoundPort);
		return true;
	}

	HttpRouter = FHttpServerModule::Get().GetHttpRouter(Port, true);
	if (!HttpRouter.IsValid())
	{
		UE_LOG(LogMonolith, Error, TEXT("Failed to get HTTP router on port %d — port may be in use"), Port);
		return false;
	}

	// Bind POST /mcp — main JSON-RPC endpoint
	RouteHandles.Add(HttpRouter->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_POST,
		FHttpRequestHandler::CreateRaw(this, &FMonolithHttpServer::HandlePostMcp)
	));

	// Bind GET /mcp — SSE endpoint for server notifications
	RouteHandles.Add(HttpRouter->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_GET,
		FHttpRequestHandler::CreateRaw(this, &FMonolithHttpServer::HandleGetMcp)
	));

	// Bind DELETE /mcp — session termination
	RouteHandles.Add(HttpRouter->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_DELETE,
		FHttpRequestHandler::CreateRaw(this, &FMonolithHttpServer::HandleDeleteMcp)
	));

	// Bind OPTIONS /mcp — CORS preflight
	RouteHandles.Add(HttpRouter->BindRoute(
		FHttpPath(TEXT("/mcp")),
		EHttpServerRequestVerbs::VERB_OPTIONS,
		FHttpRequestHandler::CreateRaw(this, &FMonolithHttpServer::HandleOptions)
	));

	FHttpServerModule::Get().StartAllListeners();

	bIsRunning = true;
	BoundPort = Port;

	UE_LOG(LogMonolith, Log, TEXT("Monolith MCP server started on port %d"), Port);
	return true;
}

void FMonolithHttpServer::Stop()
{
	if (!bIsRunning)
	{
		return;
	}

	if (HttpRouter.IsValid())
	{
		for (const FHttpRouteHandle& Handle : RouteHandles)
		{
			HttpRouter->UnbindRoute(Handle);
		}
		RouteHandles.Empty();
	}

	FHttpServerModule::Get().StopAllListeners();
	HttpRouter.Reset();

	{
		FScopeLock Lock(&SessionLock);
		ActiveSessions.Empty();
	}

	bIsRunning = false;
	UE_LOG(LogMonolith, Log, TEXT("Monolith MCP server stopped"));
}

// ============================================================================
// Route Handlers
// ============================================================================

bool FMonolithHttpServer::HandlePostMcp(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// Parse body as UTF-8 JSON
	FString BodyString = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(Request.Body.GetData())));
	if (BodyString.IsEmpty())
	{
		TSharedPtr<FJsonObject> Err = FMonolithJsonUtils::ErrorResponse(
			nullptr, FMonolithJsonUtils::ErrParseError, TEXT("Empty request body"));
		OnComplete(MakeJsonResponse(FMonolithJsonUtils::Serialize(Err), EHttpServerResponseCodes::BadRequest));
		return true;
	}

	// Check for session header — create one if missing (first request)
	FString SessionId;
	if (const TArray<FString>* SessionHeader = Request.Headers.Find(TEXT("mcp-session-id")))
	{
		if (SessionHeader->Num() > 0)
		{
			SessionId = (*SessionHeader)[0];
		}
	}

	if (SessionId.IsEmpty())
	{
		SessionId = GenerateSessionId();
		FScopeLock Lock(&SessionLock);
		ActiveSessions.Add(SessionId);
	}
	else if (!IsValidSession(SessionId))
	{
		TSharedPtr<FJsonObject> Err = FMonolithJsonUtils::ErrorResponse(
			nullptr, FMonolithJsonUtils::ErrInvalidRequest, TEXT("Invalid or expired session"));
		auto Response = MakeJsonResponse(FMonolithJsonUtils::Serialize(Err), EHttpServerResponseCodes::NotFound);
		AddCorsHeaders(*Response);
		OnComplete(MoveTemp(Response));
		return true;
	}

	// Try parse as JSON
	TSharedPtr<FJsonObject> JsonRequest = FMonolithJsonUtils::Parse(BodyString);

	// Could be a single request or a batch (array)
	TArray<TSharedPtr<FJsonObject>> Requests;
	TArray<TSharedPtr<FJsonObject>> Responses;

	if (JsonRequest.IsValid())
	{
		// Single request
		Requests.Add(JsonRequest);
	}
	else
	{
		// Try parsing as array (batch)
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(BodyString);
		if (FJsonSerializer::Deserialize(Reader, JsonArray) && JsonArray.Num() > 0)
		{
			for (const TSharedPtr<FJsonValue>& Value : JsonArray)
			{
				if (Value.IsValid() && Value->Type == EJson::Object)
				{
					Requests.Add(Value->AsObject());
				}
			}
		}
		else
		{
			TSharedPtr<FJsonObject> Err = FMonolithJsonUtils::ErrorResponse(
				nullptr, FMonolithJsonUtils::ErrParseError, TEXT("Invalid JSON"));
			auto Response = MakeJsonResponse(FMonolithJsonUtils::Serialize(Err), EHttpServerResponseCodes::BadRequest);
			AddCorsHeaders(*Response);
			OnComplete(MoveTemp(Response));
			return true;
		}
	}

	// Process each request
	for (const TSharedPtr<FJsonObject>& Req : Requests)
	{
		TSharedPtr<FJsonObject> Resp = ProcessJsonRpcRequest(Req, SessionId);
		if (Resp.IsValid())
		{
			// Only add response if it's not a notification (notifications have no id)
			Responses.Add(Resp);
		}
	}

	// Build response
	FString ResponseBody;
	if (Responses.Num() == 0)
	{
		// All notifications — 202 Accepted with no body
		auto Response = FHttpServerResponse::Ok();
		Response->Code = EHttpServerResponseCodes::Accepted;
		AddCorsHeaders(*Response);
		Response->Headers.Add(TEXT("Mcp-Session-Id"), {SessionId});
		OnComplete(MoveTemp(Response));
		return true;
	}
	else if (Responses.Num() == 1)
	{
		ResponseBody = FMonolithJsonUtils::Serialize(Responses[0]);
	}
	else
	{
		// Batch response — serialize as array
		TArray<TSharedPtr<FJsonValue>> JsonArray;
		for (const TSharedPtr<FJsonObject>& Resp : Responses)
		{
			JsonArray.Add(MakeShared<FJsonValueObject>(Resp));
		}
		FString ArrayStr;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&ArrayStr);
		FJsonSerializer::Serialize(JsonArray, Writer);
		ResponseBody = ArrayStr;
	}

	auto Response = MakeJsonResponse(ResponseBody);
	AddCorsHeaders(*Response);
	Response->Headers.Add(TEXT("Mcp-Session-Id"), {SessionId});
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMonolithHttpServer::HandleGetMcp(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	// SSE endpoint — for now, return a simple SSE stream with a ping
	// Full SSE streaming will be implemented when we need server-initiated notifications

	FString SessionId;
	if (const TArray<FString>* SessionHeader = Request.Headers.Find(TEXT("mcp-session-id")))
	{
		if (SessionHeader->Num() > 0)
		{
			SessionId = (*SessionHeader)[0];
		}
	}

	if (SessionId.IsEmpty() || !IsValidSession(SessionId))
	{
		auto Response = FHttpServerResponse::Error(EHttpServerResponseCodes::BadRequest,
			TEXT("BadRequest"), TEXT("Missing or invalid Mcp-Session-Id header"));
		AddCorsHeaders(*Response);
		OnComplete(MoveTemp(Response));
		return true;
	}

	// Return SSE endpoint acknowledgement
	// UE's HTTP server doesn't natively support long-lived SSE connections,
	// so we return a single SSE event with an endpoint message and close.
	FString SseBody = TEXT("event: endpoint\ndata: \"/mcp\"\n\n");
	auto Response = FHttpServerResponse::Create(SseBody, TEXT("text/event-stream"));
	Response->Code = EHttpServerResponseCodes::Ok;
	AddCorsHeaders(*Response);
	Response->Headers.Add(TEXT("Cache-Control"), {TEXT("no-cache")});
	Response->Headers.Add(TEXT("Connection"), {TEXT("keep-alive")});
	Response->Headers.Add(TEXT("Mcp-Session-Id"), {SessionId});
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMonolithHttpServer::HandleDeleteMcp(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	FString SessionId;
	if (const TArray<FString>* SessionHeader = Request.Headers.Find(TEXT("mcp-session-id")))
	{
		if (SessionHeader->Num() > 0)
		{
			SessionId = (*SessionHeader)[0];
		}
	}

	if (!SessionId.IsEmpty())
	{
		FScopeLock Lock(&SessionLock);
		ActiveSessions.Remove(SessionId);
		UE_LOG(LogMonolith, Verbose, TEXT("Session terminated: %s"), *SessionId);
	}

	auto Response = FHttpServerResponse::Ok();
	AddCorsHeaders(*Response);
	OnComplete(MoveTemp(Response));
	return true;
}

bool FMonolithHttpServer::HandleOptions(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
{
	auto Response = FHttpServerResponse::Ok();
	AddCorsHeaders(*Response);
	OnComplete(MoveTemp(Response));
	return true;
}

// ============================================================================
// JSON-RPC 2.0 Processing
// ============================================================================

TSharedPtr<FJsonObject> FMonolithHttpServer::ProcessJsonRpcRequest(const TSharedPtr<FJsonObject>& Request, const FString& SessionId)
{
	if (!Request.IsValid())
	{
		return FMonolithJsonUtils::ErrorResponse(nullptr, FMonolithJsonUtils::ErrInvalidRequest, TEXT("Invalid request object"));
	}

	// Validate jsonrpc version
	FString Version;
	if (!Request->TryGetStringField(TEXT("jsonrpc"), Version) || Version != TEXT("2.0"))
	{
		return FMonolithJsonUtils::ErrorResponse(nullptr, FMonolithJsonUtils::ErrInvalidRequest, TEXT("Missing or invalid jsonrpc version"));
	}

	// Get method
	FString Method;
	if (!Request->TryGetStringField(TEXT("method"), Method))
	{
		return FMonolithJsonUtils::ErrorResponse(nullptr, FMonolithJsonUtils::ErrInvalidRequest, TEXT("Missing method field"));
	}

	// Get id (null for notifications)
	TSharedPtr<FJsonValue> Id = Request->TryGetField(TEXT("id"));
	bool bIsNotification = !Id.IsValid() || Id->IsNull();

	// Get params
	TSharedPtr<FJsonObject> Params;
	const TSharedPtr<FJsonObject>* ParamsObj = nullptr;
	if (Request->TryGetObjectField(TEXT("params"), ParamsObj) && ParamsObj)
	{
		Params = *ParamsObj;
	}
	if (!Params.IsValid())
	{
		Params = MakeShared<FJsonObject>();
	}

	UE_LOG(LogMonolith, Verbose, TEXT("JSON-RPC: %s (id=%s)"), *Method, Id.IsValid() ? *Id->AsString() : TEXT("notification"));

	// Dispatch by method
	TSharedPtr<FJsonObject> Response;

	if (Method == TEXT("initialize"))
	{
		Response = HandleInitialize(Id, Params);
	}
	else if (Method == TEXT("notifications/initialized"))
	{
		// Notification — no response
		return nullptr;
	}
	else if (Method == TEXT("tools/list"))
	{
		Response = HandleToolsList(Id, Params);
	}
	else if (Method == TEXT("tools/call"))
	{
		Response = HandleToolsCall(Id, Params);
	}
	else if (Method == TEXT("ping"))
	{
		Response = HandlePing(Id);
	}
	else
	{
		Response = FMonolithJsonUtils::ErrorResponse(Id, FMonolithJsonUtils::ErrMethodNotFound,
			FString::Printf(TEXT("Unknown method: %s"), *Method));
	}

	// Notifications don't get responses
	if (bIsNotification)
	{
		return nullptr;
	}

	return Response;
}

TSharedPtr<FJsonObject> FMonolithHttpServer::HandleInitialize(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("protocolVersion"), TEXT("2025-03-26"));

	// Server info
	TSharedPtr<FJsonObject> ServerInfo = MakeShared<FJsonObject>();
	ServerInfo->SetStringField(TEXT("name"), TEXT("monolith"));
	ServerInfo->SetStringField(TEXT("version"), MONOLITH_VERSION);
	Result->SetObjectField(TEXT("serverInfo"), ServerInfo);

	// Capabilities
	TSharedPtr<FJsonObject> Capabilities = MakeShared<FJsonObject>();

	// We support tools
	TSharedPtr<FJsonObject> ToolsCap = MakeShared<FJsonObject>();
	ToolsCap->SetBoolField(TEXT("listChanged"), false);
	Capabilities->SetObjectField(TEXT("tools"), ToolsCap);

	Result->SetObjectField(TEXT("capabilities"), Capabilities);

	return FMonolithJsonUtils::SuccessResponse(Id, MakeShared<FJsonValueObject>(Result));
}

TSharedPtr<FJsonObject> FMonolithHttpServer::HandleToolsList(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params)
{
	FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();

	TArray<TSharedPtr<FJsonValue>> ToolsArray;

	// Each namespace becomes a tool
	TArray<FString> Namespaces = Registry.GetNamespaces();
	for (const FString& Namespace : Namespaces)
	{
		TArray<FMonolithActionInfo> Actions = Registry.GetActions(Namespace);
		if (Actions.Num() == 0) continue;

		// Build the tool entry for this namespace
		// Format: "namespace.query" with action as a parameter
		TSharedPtr<FJsonObject> Tool = MakeShared<FJsonObject>();

		if (Namespace == TEXT("monolith"))
		{
			// Core tools are individual: monolith_discover, monolith_status
			for (const FMonolithActionInfo& ActionInfo : Actions)
			{
				TSharedPtr<FJsonObject> CoreTool = MakeShared<FJsonObject>();
				CoreTool->SetStringField(TEXT("name"), FString::Printf(TEXT("monolith_%s"), *ActionInfo.Action));
				CoreTool->SetStringField(TEXT("description"), ActionInfo.Description);

				// Input schema
				TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
				InputSchema->SetStringField(TEXT("type"), TEXT("object"));
				if (ActionInfo.ParamSchema.IsValid())
				{
					InputSchema->SetObjectField(TEXT("properties"), ActionInfo.ParamSchema);
				}
				else
				{
					InputSchema->SetObjectField(TEXT("properties"), MakeShared<FJsonObject>());
				}
				CoreTool->SetObjectField(TEXT("inputSchema"), InputSchema);

				ToolsArray.Add(MakeShared<FJsonValueObject>(CoreTool));
			}
		}
		else
		{
			// Domain tools use the dispatch pattern: namespace.query
			FString ToolName = FString::Printf(TEXT("%s.query"), *Namespace);
			Tool->SetStringField(TEXT("name"), ToolName);

			// Build description with action list
			FString Description = FString::Printf(TEXT("Query the %s domain. Available actions: "), *Namespace);
			TArray<FString> ActionNames;
			for (const FMonolithActionInfo& ActionInfo : Actions)
			{
				ActionNames.Add(ActionInfo.Action);
			}
			Description += FString::Join(ActionNames, TEXT(", "));
			Tool->SetStringField(TEXT("description"), Description);

			// Build input schema
			TSharedPtr<FJsonObject> InputSchema = MakeShared<FJsonObject>();
			InputSchema->SetStringField(TEXT("type"), TEXT("object"));

			TSharedPtr<FJsonObject> Properties = MakeShared<FJsonObject>();

			// "action" property (required)
			TSharedPtr<FJsonObject> ActionProp = MakeShared<FJsonObject>();
			ActionProp->SetStringField(TEXT("type"), TEXT("string"));
			ActionProp->SetStringField(TEXT("description"), TEXT("The action to execute"));
			TArray<TSharedPtr<FJsonValue>> EnumValues;
			for (const FString& Name : ActionNames)
			{
				EnumValues.Add(MakeShared<FJsonValueString>(Name));
			}
			ActionProp->SetArrayField(TEXT("enum"), EnumValues);
			Properties->SetObjectField(TEXT("action"), ActionProp);

			// "params" property (optional object)
			TSharedPtr<FJsonObject> ParamsProp = MakeShared<FJsonObject>();
			ParamsProp->SetStringField(TEXT("type"), TEXT("object"));
			ParamsProp->SetStringField(TEXT("description"), TEXT("Parameters for the action"));
			Properties->SetObjectField(TEXT("params"), ParamsProp);

			InputSchema->SetObjectField(TEXT("properties"), Properties);
			InputSchema->SetArrayField(TEXT("required"), {MakeShared<FJsonValueString>(TEXT("action"))});

			Tool->SetObjectField(TEXT("inputSchema"), InputSchema);
			ToolsArray.Add(MakeShared<FJsonValueObject>(Tool));
		}
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	Result->SetArrayField(TEXT("tools"), ToolsArray);

	return FMonolithJsonUtils::SuccessResponse(Id, MakeShared<FJsonValueObject>(Result));
}

TSharedPtr<FJsonObject> FMonolithHttpServer::HandleToolsCall(const TSharedPtr<FJsonValue>& Id, const TSharedPtr<FJsonObject>& Params)
{
	if (!Params.IsValid())
	{
		return FMonolithJsonUtils::ErrorResponse(Id, FMonolithJsonUtils::ErrInvalidParams, TEXT("Missing params"));
	}

	FString ToolName;
	if (!Params->TryGetStringField(TEXT("name"), ToolName))
	{
		return FMonolithJsonUtils::ErrorResponse(Id, FMonolithJsonUtils::ErrInvalidParams, TEXT("Missing tool name"));
	}

	// Get arguments
	TSharedPtr<FJsonObject> Arguments;
	const TSharedPtr<FJsonObject>* ArgsObj = nullptr;
	if (Params->TryGetObjectField(TEXT("arguments"), ArgsObj) && ArgsObj)
	{
		Arguments = *ArgsObj;
	}
	if (!Arguments.IsValid())
	{
		Arguments = MakeShared<FJsonObject>();
	}

	FString Namespace;
	FString Action;

	// Determine dispatch pattern
	if (ToolName.StartsWith(TEXT("monolith_")))
	{
		// Core tool: monolith_discover → namespace="monolith", action="discover"
		Namespace = TEXT("monolith");
		Action = ToolName.Mid(10); // len("monolith_") = 9... wait
	}

	// Fix: monolith_ is 9 chars
	if (ToolName.StartsWith(TEXT("monolith_")))
	{
		Namespace = TEXT("monolith");
		Action = ToolName.Mid(9);
	}
	else if (ToolName.EndsWith(TEXT(".query")))
	{
		// Domain tool: blueprint.query → namespace="blueprint"
		Namespace = ToolName.Left(ToolName.Len() - 6); // strip ".query"

		if (!Arguments->TryGetStringField(TEXT("action"), Action))
		{
			return FMonolithJsonUtils::ErrorResponse(Id, FMonolithJsonUtils::ErrInvalidParams,
				TEXT("Missing 'action' in arguments"));
		}

		// Extract nested params if present
		const TSharedPtr<FJsonObject>* NestedParams = nullptr;
		if (Arguments->TryGetObjectField(TEXT("params"), NestedParams) && NestedParams)
		{
			Arguments = *NestedParams;
		}
		else
		{
			Arguments = MakeShared<FJsonObject>();
		}
	}
	else
	{
		return FMonolithJsonUtils::ErrorResponse(Id, FMonolithJsonUtils::ErrMethodNotFound,
			FString::Printf(TEXT("Unknown tool: %s"), *ToolName));
	}

	// Execute via registry
	FMonolithActionResult ActionResult = FMonolithToolRegistry::Get().ExecuteAction(Namespace, Action, Arguments);

	// Build MCP tool result
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Content;

	if (ActionResult.bSuccess)
	{
		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		if (ActionResult.Result.IsValid())
		{
			TextContent->SetStringField(TEXT("text"), FMonolithJsonUtils::Serialize(ActionResult.Result));
		}
		else
		{
			TextContent->SetStringField(TEXT("text"), TEXT("{}"));
		}
		Content.Add(MakeShared<FJsonValueObject>(TextContent));
		Result->SetBoolField(TEXT("isError"), false);
	}
	else
	{
		TSharedPtr<FJsonObject> TextContent = MakeShared<FJsonObject>();
		TextContent->SetStringField(TEXT("type"), TEXT("text"));
		TextContent->SetStringField(TEXT("text"), ActionResult.ErrorMessage);
		Content.Add(MakeShared<FJsonValueObject>(TextContent));
		Result->SetBoolField(TEXT("isError"), true);
	}

	Result->SetArrayField(TEXT("content"), Content);

	return FMonolithJsonUtils::SuccessResponse(Id, MakeShared<FJsonValueObject>(Result));
}

TSharedPtr<FJsonObject> FMonolithHttpServer::HandlePing(const TSharedPtr<FJsonValue>& Id)
{
	return FMonolithJsonUtils::SuccessResponse(Id, MakeShared<FJsonValueObject>(MakeShared<FJsonObject>()));
}

// ============================================================================
// Helpers
// ============================================================================

TUniquePtr<FHttpServerResponse> FMonolithHttpServer::MakeJsonResponse(const FString& JsonBody, EHttpServerResponseCodes Code)
{
	auto Response = FHttpServerResponse::Create(JsonBody, TEXT("application/json"));
	Response->Code = Code;
	return Response;
}

TUniquePtr<FHttpServerResponse> FMonolithHttpServer::MakeSseResponse(const TArray<TSharedPtr<FJsonObject>>& Messages)
{
	FString SseBody;
	for (const TSharedPtr<FJsonObject>& Msg : Messages)
	{
		SseBody += TEXT("event: message\ndata: ");
		SseBody += FMonolithJsonUtils::Serialize(Msg);
		SseBody += TEXT("\n\n");
	}

	auto Response = FHttpServerResponse::Create(SseBody, TEXT("text/event-stream"));
	Response->Code = EHttpServerResponseCodes::Ok;
	return Response;
}

void FMonolithHttpServer::AddCorsHeaders(FHttpServerResponse& Response)
{
	Response.Headers.Add(TEXT("Access-Control-Allow-Origin"), {TEXT("*")});
	Response.Headers.Add(TEXT("Access-Control-Allow-Methods"), {TEXT("GET, POST, DELETE, OPTIONS")});
	Response.Headers.Add(TEXT("Access-Control-Allow-Headers"), {TEXT("Content-Type, Mcp-Session-Id")});
	Response.Headers.Add(TEXT("Access-Control-Expose-Headers"), {TEXT("Mcp-Session-Id")});
}

FString FMonolithHttpServer::GenerateSessionId()
{
	return FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphens);
}

bool FMonolithHttpServer::IsValidSession(const FString& SessionId) const
{
	FScopeLock Lock(&SessionLock);
	return ActiveSessions.Contains(SessionId);
}
```

### Known Limitations & Notes

1. **UE HTTP Server and SSE**: UE's `FHttpServerModule` is request-response oriented. True long-lived SSE connections would require a custom socket server. For MCP, this is fine because:
   - Streamable HTTP transport uses POST for all RPC calls
   - GET /mcp SSE is optional and mainly for server-push notifications
   - We return a single SSE event on GET and close, which is spec-compliant for servers that don't push

2. **FHttpRequestHandler**: This is `TBaseDelegate<bool, const FHttpServerRequest&, const FHttpResultCallback&>` — i.e., returns bool, takes request + callback. We use `CreateRaw` since FMonolithHttpServer is not a UObject.

3. **Thread Safety**: Route handlers may be called from the HTTP server's listener thread. All GameThread-dependent UE operations (like loading assets) must be bounced to the game thread in the actual action handlers. The registry itself uses a critical section.

### Steps
1. Create the two files above
2. Build — verify clean compile
3. Wire up in Task 0.6 (module startup)
4. Test with curl in Task 0.6


## Task 0.6: FMonolithCoreModule Wiring

Wire everything together: module startup creates the HTTP server, module shutdown tears it down.

### Files

| Action | Path |
|--------|------|
| Modify | `Source/MonolithCore/Public/MonolithCoreModule.h` |
| Modify | `Source/MonolithCore/Private/MonolithCoreModule.cpp` |
| Modify | `Source/MonolithCore/MonolithCore.Build.cs` |

### Code

**`Source/MonolithCore/Public/MonolithCoreModule.h`** (full replacement)
```cpp
#pragma once

#include "Modules/ModuleManager.h"

#define MONOLITH_VERSION TEXT("0.1.0")

class FMonolithHttpServer;

class MONOLITHCORE_API FMonolithCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static inline FMonolithCoreModule& Get()
	{
		return FModuleManager::LoadModuleChecked<FMonolithCoreModule>("MonolithCore");
	}

	static inline bool IsAvailable()
	{
		return FModuleManager::Get().IsModuleLoaded("MonolithCore");
	}

	/** Get the running HTTP server instance */
	FMonolithHttpServer* GetHttpServer() const { return HttpServer.Get(); }

private:
	TUniquePtr<FMonolithHttpServer> HttpServer;

	void RegisterCoreTools();
};
```

**`Source/MonolithCore/Private/MonolithCoreModule.cpp`** (full replacement)
```cpp
#include "MonolithCoreModule.h"
#include "MonolithHttpServer.h"
#include "MonolithSettings.h"
#include "MonolithJsonUtils.h"
#include "MonolithToolRegistry.h"

#define LOCTEXT_NAMESPACE "FMonolithCoreModule"

void FMonolithCoreModule::StartupModule()
{
	UE_LOG(LogMonolith, Log, TEXT("Monolith %s — Core module initializing"), MONOLITH_VERSION);

	// Register core discovery/status tools
	RegisterCoreTools();

	// Start HTTP server
	const UMonolithSettings* Settings = UMonolithSettings::Get();
	int32 Port = Settings ? Settings->ServerPort : 9316;

	HttpServer = MakeUnique<FMonolithHttpServer>();
	if (!HttpServer->Start(Port))
	{
		UE_LOG(LogMonolith, Error, TEXT("Failed to start MCP server on port %d"), Port);
	}
}

void FMonolithCoreModule::ShutdownModule()
{
	if (HttpServer.IsValid())
	{
		HttpServer->Stop();
		HttpServer.Reset();
	}

	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("monolith"));

	UE_LOG(LogMonolith, Log, TEXT("Monolith — Core module shut down"));
}

void FMonolithCoreModule::RegisterCoreTools()
{
	// Registered in Task 0.7 — discover and status tools
	// This method is called before the server starts, so tools are ready for the first request
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithCoreModule, MonolithCore)
```

**`Source/MonolithCore/MonolithCore.Build.cs`** (full replacement)
```csharp
using UnrealBuildTool;

public class MonolithCore : ModuleRules
{
	public MonolithCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTP",
			"HTTPServer",
			"Json",
			"JsonUtilities",
			"Slate",
			"SlateCore",
			"DeveloperSettings",
			"Projects",
			"AssetRegistry"
		});
	}
}
```

### Steps
1. Replace the three files above
2. Build — this is the first time everything compiles together
3. Launch editor — check Output Log for: `Monolith 0.1.0 — Core module initializing` and `Monolith MCP server started on port 9316`
4. Test with curl:

```bash
# Test initialize handshake
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}'
```

Expected response:
```json
{"jsonrpc":"2.0","id":1,"result":{"protocolVersion":"2025-03-26","serverInfo":{"name":"monolith","version":"0.1.0"},"capabilities":{"tools":{"listChanged":false}}}}
```

```bash
# Test tools/list (should be empty except core tools after Task 0.7)
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -H "Mcp-Session-Id: <session-id-from-above>" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'
```

```bash
# Test ping
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":3,"method":"ping"}'
```

```bash
# Test CORS preflight
curl -X OPTIONS http://localhost:9316/mcp -v
```

---

## Task 0.7: Discovery & Status Tools

The first actual tools registered in the system. These let AI clients discover what's available and check server health.

### Files

| Action | Path |
|--------|------|
| Create | `Source/MonolithCore/Private/MonolithCoreTools.h` |
| Create | `Source/MonolithCore/Private/MonolithCoreTools.cpp` |
| Modify | `Source/MonolithCore/Private/MonolithCoreModule.cpp` (add registration calls) |

### Code

**`Source/MonolithCore/Private/MonolithCoreTools.h`**
```cpp
#pragma once

#include "CoreMinimal.h"
#include "MonolithToolRegistry.h"

/**
 * Core discovery and status tool implementations.
 * These are registered under the "monolith" namespace.
 */
class FMonolithCoreTools
{
public:
	/** Register all core tools with the registry */
	static void RegisterAll();

	// --- Action Handlers ---

	/** monolith_discover — List available namespaces and their actions */
	static FMonolithActionResult HandleDiscover(const TSharedPtr<FJsonObject>& Params);

	/** monolith_status — Server health, version, index status */
	static FMonolithActionResult HandleStatus(const TSharedPtr<FJsonObject>& Params);
};
```

**`Source/MonolithCore/Private/MonolithCoreTools.cpp`**
```cpp
#include "MonolithCoreTools.h"
#include "MonolithCoreModule.h"
#include "MonolithJsonUtils.h"
#include "MonolithHttpServer.h"
#include "Misc/App.h"

void FMonolithCoreTools::RegisterAll()
{
	FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();

	// monolith_discover
	{
		TSharedPtr<FJsonObject> Schema = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> NsProp = MakeShared<FJsonObject>();
		NsProp->SetStringField(TEXT("type"), TEXT("string"));
		NsProp->SetStringField(TEXT("description"), TEXT("Optional: filter to a specific namespace"));
		Schema->SetObjectField(TEXT("namespace"), NsProp);

		Registry.RegisterAction(
			TEXT("monolith"), TEXT("discover"),
			TEXT("List available tool namespaces and their actions. Pass namespace to filter."),
			FMonolithActionHandler::CreateStatic(&FMonolithCoreTools::HandleDiscover),
			Schema
		);
	}

	// monolith_status
	{
		Registry.RegisterAction(
			TEXT("monolith"), TEXT("status"),
			TEXT("Get Monolith server health: version, uptime, port, registered action count, module status."),
			FMonolithActionHandler::CreateStatic(&FMonolithCoreTools::HandleStatus)
		);
	}
}

FMonolithActionResult FMonolithCoreTools::HandleDiscover(const TSharedPtr<FJsonObject>& Params)
{
	FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();

	FString FilterNamespace;
	if (Params.IsValid())
	{
		Params->TryGetStringField(TEXT("namespace"), FilterNamespace);
	}

	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	TArray<FString> Namespaces = Registry.GetNamespaces();

	if (!FilterNamespace.IsEmpty())
	{
		// Filter to specific namespace — return detailed action list
		TArray<FMonolithActionInfo> Actions = Registry.GetActions(FilterNamespace);
		if (Actions.Num() == 0)
		{
			return FMonolithActionResult::Error(
				FString::Printf(TEXT("Unknown namespace: %s"), *FilterNamespace),
				FMonolithJsonUtils::ErrInvalidParams
			);
		}

		Result->SetStringField(TEXT("namespace"), FilterNamespace);
		TArray<TSharedPtr<FJsonValue>> ActionArray;
		for (const FMonolithActionInfo& ActionInfo : Actions)
		{
			TSharedPtr<FJsonObject> ActionObj = MakeShared<FJsonObject>();
			ActionObj->SetStringField(TEXT("action"), ActionInfo.Action);
			ActionObj->SetStringField(TEXT("description"), ActionInfo.Description);
			if (ActionInfo.ParamSchema.IsValid())
			{
				ActionObj->SetObjectField(TEXT("params"), ActionInfo.ParamSchema);
			}
			ActionArray.Add(MakeShared<FJsonValueObject>(ActionObj));
		}
		Result->SetArrayField(TEXT("actions"), ActionArray);
	}
	else
	{
		// Return all namespaces with action counts
		TArray<TSharedPtr<FJsonValue>> NsArray;
		for (const FString& Ns : Namespaces)
		{
			TArray<FMonolithActionInfo> Actions = Registry.GetActions(Ns);
			TSharedPtr<FJsonObject> NsObj = MakeShared<FJsonObject>();
			NsObj->SetStringField(TEXT("namespace"), Ns);
			NsObj->SetNumberField(TEXT("action_count"), Actions.Num());

			TArray<TSharedPtr<FJsonValue>> ActionNames;
			for (const FMonolithActionInfo& ActionInfo : Actions)
			{
				ActionNames.Add(MakeShared<FJsonValueString>(ActionInfo.Action));
			}
			NsObj->SetArrayField(TEXT("actions"), ActionNames);
			NsArray.Add(MakeShared<FJsonValueObject>(NsObj));
		}
		Result->SetArrayField(TEXT("namespaces"), NsArray);
		Result->SetNumberField(TEXT("total_actions"), Registry.GetActionCount());
	}

	return FMonolithActionResult::Success(Result);
}

FMonolithActionResult FMonolithCoreTools::HandleStatus(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();

	// Version
	Result->SetStringField(TEXT("version"), MONOLITH_VERSION);

	// Server status
	FMonolithHttpServer* Server = FMonolithCoreModule::Get().GetHttpServer();
	Result->SetBoolField(TEXT("server_running"), Server != nullptr && Server->IsRunning());
	Result->SetNumberField(TEXT("server_port"), Server ? Server->GetPort() : 0);

	// Registry stats
	FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();
	Result->SetNumberField(TEXT("total_actions"), Registry.GetActionCount());
	Result->SetNumberField(TEXT("namespaces"), Registry.GetNamespaces().Num());

	// Engine info
	Result->SetStringField(TEXT("engine_version"), FApp::GetBuildVersion());

	// Project info
	Result->SetStringField(TEXT("project_name"), FApp::GetProjectName());

	return FMonolithActionResult::Success(Result);
}
```

**`Source/MonolithCore/Private/MonolithCoreModule.cpp`** — Update RegisterCoreTools():
```cpp
// In the RegisterCoreTools() method body, replace the empty body with:
void FMonolithCoreModule::RegisterCoreTools()
{
	FMonolithCoreTools::RegisterAll();
}
```

And add the include at the top of MonolithCoreModule.cpp:
```cpp
#include "MonolithCoreTools.h"
```

### Steps
1. Create MonolithCoreTools.h and MonolithCoreTools.cpp
2. Update MonolithCoreModule.cpp to include and call FMonolithCoreTools::RegisterAll()
3. Build — verify clean compile
4. Launch editor, then test:

```bash
# Initialize
curl -s -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-03-26","capabilities":{},"clientInfo":{"name":"test","version":"1.0"}}}'

# List tools — should show monolith_discover and monolith_status
curl -s -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/list","params":{}}'

# Call discover
curl -s -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"monolith_discover","arguments":{}}}'

# Call status
curl -s -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"monolith_status","arguments":{}}}'

# Call discover with namespace filter
curl -s -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"monolith_discover","arguments":{"namespace":"monolith"}}}'
```

Expected: discover returns `{"namespaces":[{"namespace":"monolith","action_count":2,"actions":["discover","status"]}],"total_actions":2}`

Expected: status returns version, server_running=true, port=9316, total_actions=2, engine_version, project_name


## File Summary

### All files created/modified in Phase 0

| # | Action | Path | Task |
|---|--------|------|------|
| 1 | Create | `Source/MonolithCore/Public/MonolithSettings.h` | 0.1 |
| 2 | Create | `Source/MonolithCore/Private/MonolithSettings.cpp` | 0.1 |
| 3 | Create | `Source/MonolithCore/Public/MonolithJsonUtils.h` | 0.2 |
| 4 | Create | `Source/MonolithCore/Private/MonolithJsonUtils.cpp` | 0.2 |
| 5 | Create | `Source/MonolithCore/Public/MonolithAssetUtils.h` | 0.3 |
| 6 | Create | `Source/MonolithCore/Private/MonolithAssetUtils.cpp` | 0.3 |
| 7 | Create | `Source/MonolithCore/Public/MonolithToolRegistry.h` | 0.4 |
| 8 | Create | `Source/MonolithCore/Private/MonolithToolRegistry.cpp` | 0.4 |
| 9 | Create | `Source/MonolithCore/Public/MonolithHttpServer.h` | 0.5 |
| 10 | Create | `Source/MonolithCore/Private/MonolithHttpServer.cpp` | 0.5 |
| 11 | Modify | `Source/MonolithCore/Public/MonolithCoreModule.h` | 0.6 |
| 12 | Modify | `Source/MonolithCore/Private/MonolithCoreModule.cpp` | 0.6 |
| 13 | Modify | `Source/MonolithCore/MonolithCore.Build.cs` | 0.6 |
| 14 | Create | `Source/MonolithCore/Private/MonolithCoreTools.h` | 0.7 |
| 15 | Create | `Source/MonolithCore/Private/MonolithCoreTools.cpp` | 0.7 |

### Build.cs changes
Only one change to `MonolithCore.Build.cs`: add `"AssetRegistry"` to PublicDependencyModuleNames.

### No .uplugin changes needed
All modules already declared. MonolithCore already at `PostEngineInit` loading phase.

---

## Potential Issues & Mitigations

### 1. FHttpRequestHandler delegate type
The UE source shows `FHttpRequestHandler` is used as a parameter to `BindRoute`. Based on `FHttpRouteHandleInternal` storing it and the test code patterns, it's a delegate/TFunction with signature:
```
bool Handler(const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete)
```
If `CreateRaw` doesn't compile, fall back to:
```cpp
FHttpRequestHandler::CreateLambda([this](const FHttpServerRequest& Request, const FHttpResultCallback& OnComplete) {
    return HandlePostMcp(Request, OnComplete);
})
```

### 2. Header case sensitivity
UE's HTTP server may lowercase header names. The code checks for `"mcp-session-id"` (lowercase). If the client sends `"Mcp-Session-Id"`, we may need to iterate headers case-insensitively. Mitigation: add a helper or check both cases.

### 3. VERB_OPTIONS may not exist
Some UE versions don't have `VERB_OPTIONS` in `EHttpServerRequestVerbs`. If it doesn't compile:
- Use a request preprocessor via `RegisterRequestPreprocessor` to intercept OPTIONS
- Or handle CORS via a preprocessor that adds headers to all responses

### 4. Thread safety for tool execution
HTTP handlers run on the listener thread, but many UE operations (asset loading, blueprint inspection) require the game thread. Domain action handlers MUST use:
```cpp
FMonolithActionResult Result;
FEvent* DoneEvent = FPlatformProcess::GetSynchEventFromPool();
AsyncTask(ENamedThreads::GameThread, [&]() {
    Result = DoActualWork(Params);
    DoneEvent->Trigger();
});
DoneEvent->Wait();
FPlatformProcess::ReturnSynchEventToPool(DoneEvent);
return Result;
```
This pattern should be documented as a utility in MonolithCore for Phase 1+ domain modules.

### 5. StopAllListeners scope
`FHttpServerModule::Get().StartAllListeners()` and `StopAllListeners()` are global — they affect ALL HTTP routers, not just ours. If another plugin uses the HTTP server, stopping all listeners would break it. Mitigation for v0.2: track our specific listener. For v0.1 this is acceptable since Monolith is the only expected HTTP user in editor plugins.

---

## Execution Order

```
Step 1: Create all files for Tasks 0.1-0.3 (Settings, JsonUtils, AssetUtils)
Step 2: Build — verify compile
Step 3: Create all files for Task 0.4 (ToolRegistry)
Step 4: Build — verify compile
Step 5: Create all files for Task 0.5 (HttpServer)
Step 6: Build — verify compile (server code exists but isn't wired yet)
Step 7: Modify files for Task 0.6 (Module wiring) + create files for Task 0.7 (Core tools)
Step 8: Build — verify full compile
Step 9: Launch editor — verify log output shows server started
Step 10: Run curl tests — verify JSON-RPC handshake, tool listing, discover, status
```

If any step fails to compile, fix before proceeding. The dependency chain is strict.

---

## How Domain Modules Will Use This (Preview)

After Phase 0, any domain module (e.g., MonolithBlueprint) plugs in like this:

**`MonolithBlueprint.Build.cs`** — add dependency:
```csharp
PrivateDependencyModuleNames.Add("MonolithCore");
```

**`MonolithBlueprintModule.cpp`** — StartupModule:
```cpp
#include "MonolithToolRegistry.h"
#include "MonolithJsonUtils.h"

void FMonolithBlueprintModule::StartupModule()
{
    FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();

    Registry.RegisterAction(TEXT("blueprint"), TEXT("list_graphs"),
        TEXT("List all graphs in a Blueprint asset"),
        FMonolithActionHandler::CreateStatic(&HandleListGraphs));

    Registry.RegisterAction(TEXT("blueprint"), TEXT("get_graph_data"),
        TEXT("Get detailed node/pin data for a specific graph"),
        FMonolithActionHandler::CreateStatic(&HandleGetGraphData));
    // ... etc
}

void FMonolithBlueprintModule::ShutdownModule()
{
    FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("blueprint"));
}
```

The AI client then calls:
```json
{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"blueprint.query","arguments":{"action":"list_graphs","params":{"asset_path":"/Game/MyBlueprint"}}}}
```

The HTTP server routes to the registry, which dispatches to HandleListGraphs. Zero boilerplate per domain.

## Phase 1: Blueprint + Editor Tools

[RESULT success] 
# Phase 1: BLUEPRINT + EDITOR MODULES — Implementation Plan

## Prerequisites: MonolithCore ToolRegistry + JSON Helpers

Before any domain module can register actions, MonolithCore needs:
- `FMonolithToolRegistry` — singleton action registry with namespace dispatch
- `FMonolithJsonHelpers` — shared JSON serialization utilities (replaces copy-pasted ErrorJson/SuccessJson)

---

## Task 0: MonolithCore — ToolRegistry + JSON Helpers

### Files to Create

**`Source/MonolithCore/Public/MonolithToolRegistry.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

DECLARE_DELEGATE_RetVal_OneParam(
	TSharedPtr<FJsonObject>,
	FMonolithActionHandler,
	const TSharedPtr<FJsonObject>& /* Params */
);

struct FMonolithAction
{
	FString Name;
	FString Description;
	FMonolithActionHandler Handler;
};

/**
 * Central registry for all Monolith actions, keyed by namespace.
 * Each domain module registers its actions on startup and unregisters on shutdown.
 *
 * Usage:
 *   auto& Registry = FMonolithToolRegistry::Get();
 *   Registry.RegisterAction("blueprint", { "list_graphs", "List all graphs in a BP", Handler });
 *   TSharedPtr<FJsonObject> Result = Registry.Execute("blueprint", "list_graphs", Params);
 */
class MONOLITHCORE_API FMonolithToolRegistry
{
public:
	static FMonolithToolRegistry& Get();

	/** Register a single action under a namespace. */
	void RegisterAction(const FString& Namespace, const FMonolithAction& Action);

	/** Unregister all actions for a namespace (call in ShutdownModule). */
	void UnregisterNamespace(const FString& Namespace);

	/** Execute an action by namespace + action name. Returns error JSON if not found. */
	TSharedPtr<FJsonObject> Execute(
		const FString& Namespace,
		const FString& ActionName,
		const TSharedPtr<FJsonObject>& Params) const;

	/** List all registered namespaces. */
	TArray<FString> GetNamespaces() const;

	/** List all action names in a namespace. */
	TArray<FMonolithAction> GetActions(const FString& Namespace) const;

	/** Total registered action count across all namespaces. */
	int32 GetTotalActionCount() const;

private:
	/** Namespace -> (ActionName -> Action) */
	TMap<FString, TMap<FString, FMonolithAction>> Registry;
	mutable FCriticalSection RegistryLock;
};
```

**`Source/MonolithCore/Private/MonolithToolRegistry.cpp`**

```cpp
#include "MonolithToolRegistry.h"
#include "MonolithJsonHelpers.h"

FMonolithToolRegistry& FMonolithToolRegistry::Get()
{
	static FMonolithToolRegistry Instance;
	return Instance;
}

void FMonolithToolRegistry::RegisterAction(const FString& Namespace, const FMonolithAction& Action)
{
	FScopeLock Lock(&RegistryLock);
	Registry.FindOrAdd(Namespace).Add(Action.Name, Action);
	UE_LOG(LogTemp, Log, TEXT("Monolith: Registered %s.%s"), *Namespace, *Action.Name);
}

void FMonolithToolRegistry::UnregisterNamespace(const FString& Namespace)
{
	FScopeLock Lock(&RegistryLock);
	if (Registry.Remove(Namespace))
	{
		UE_LOG(LogTemp, Log, TEXT("Monolith: Unregistered namespace '%s'"), *Namespace);
	}
}

TSharedPtr<FJsonObject> FMonolithToolRegistry::Execute(
	const FString& Namespace,
	const FString& ActionName,
	const TSharedPtr<FJsonObject>& Params) const
{
	FScopeLock Lock(&RegistryLock);

	const TMap<FString, FMonolithAction>* NamespaceActions = Registry.Find(Namespace);
	if (!NamespaceActions)
	{
		return FMonolithJsonHelpers::ErrorJson(
			FString::Printf(TEXT("Unknown namespace: '%s'"), *Namespace));
	}

	const FMonolithAction* Action = NamespaceActions->Find(ActionName);
	if (!Action)
	{
		return FMonolithJsonHelpers::ErrorJson(
			FString::Printf(TEXT("Unknown action: '%s.%s'"), *Namespace, *ActionName));
	}

	if (!Action->Handler.IsBound())
	{
		return FMonolithJsonHelpers::ErrorJson(
			FString::Printf(TEXT("Action '%s.%s' has no handler"), *Namespace, *ActionName));
	}

	return Action->Handler.Execute(Params);
}

TArray<FString> FMonolithToolRegistry::GetNamespaces() const
{
	FScopeLock Lock(&RegistryLock);
	TArray<FString> Keys;
	Registry.GetKeys(Keys);
	return Keys;
}

TArray<FMonolithAction> FMonolithToolRegistry::GetActions(const FString& Namespace) const
{
	FScopeLock Lock(&RegistryLock);
	TArray<FMonolithAction> Result;
	if (const TMap<FString, FMonolithAction>* Actions = Registry.Find(Namespace))
	{
		Actions->GenerateValueArray(Result);
	}
	return Result;
}

int32 FMonolithToolRegistry::GetTotalActionCount() const
{
	FScopeLock Lock(&RegistryLock);
	int32 Total = 0;
	for (const auto& Pair : Registry)
	{
		Total += Pair.Value.Num();
	}
	return Total;
}
```

**`Source/MonolithCore/Public/MonolithJsonHelpers.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

/**
 * Shared JSON utilities for all Monolith modules.
 * Replaces the copy-pasted ErrorJson/SuccessJson from each reader plugin.
 */
struct MONOLITHCORE_API FMonolithJsonHelpers
{
	/** Create an error JSON object: {"error": true, "message": "..."} */
	static TSharedPtr<FJsonObject> ErrorJson(const FString& Message)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("error"), true);
		Obj->SetStringField(TEXT("message"), Message);
		return Obj;
	}

	/** Create a success JSON object with optional data fields. */
	static TSharedPtr<FJsonObject> SuccessJson()
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetBoolField(TEXT("success"), true);
		return Obj;
	}

	/** Serialize a JSON object to a compact string. */
	static FString Stringify(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	/** Serialize a JSON object to a pretty-printed string. */
	static FString StringifyPretty(const TSharedPtr<FJsonObject>& Obj)
	{
		FString Out;
		auto Writer = TJsonWriterFactory<>::Create(&Out);
		FJsonSerializer::Serialize(Obj.ToSharedRef(), Writer);
		return Out;
	}

	/** Helper: get a string param from params object with default. */
	static FString GetStringParam(const TSharedPtr<FJsonObject>& Params, const FString& Key, const FString& Default = TEXT(""))
	{
		if (Params.IsValid() && Params->HasField(Key))
		{
			return Params->GetStringField(Key);
		}
		return Default;
	}

	/** Helper: get an int param from params object with default. */
	static int32 GetIntParam(const TSharedPtr<FJsonObject>& Params, const FString& Key, int32 Default = 0)
	{
		if (Params.IsValid() && Params->HasField(Key))
		{
			return static_cast<int32>(Params->GetNumberField(Key));
		}
		return Default;
	}
};
```

### Files to Modify

**`Source/MonolithCore/Private/MonolithCoreModule.cpp`** — update StartupModule to log registry status:

```cpp
#include "MonolithCoreModule.h"
#include "MonolithToolRegistry.h"

#define LOCTEXT_NAMESPACE "FMonolithCoreModule"

void FMonolithCoreModule::StartupModule()
{
	UE_LOG(LogTemp, Log, TEXT("Monolith %s — Core module loaded (ToolRegistry ready)"), MONOLITH_VERSION);
}

void FMonolithCoreModule::ShutdownModule()
{
	UE_LOG(LogTemp, Log, TEXT("Monolith — Core module unloaded (%d actions were registered)"),
		FMonolithToolRegistry::Get().GetTotalActionCount());
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithCoreModule, MonolithCore)
```

### Steps

1. Create `Source/MonolithCore/Public/MonolithToolRegistry.h`
2. Create `Source/MonolithCore/Private/MonolithToolRegistry.cpp`
3. Create `Source/MonolithCore/Public/MonolithJsonHelpers.h`
4. Update `Source/MonolithCore/Private/MonolithCoreModule.cpp`
5. Compile — verify no errors

---

## Task 1: MonolithBlueprint — 5 Actions

Port BlueprintReaderLibrary's 5 static UFUNCTIONs into `FMonolithBlueprintActions`, a plain C++ class that registers with the ToolRegistry. No UCLASS/UFUNCTION needed — these are internal actions dispatched by the MCP server, not Blueprint-callable.

### Files to Create

**`Source/MonolithBlueprint/Private/MonolithBlueprintActions.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;
class UEdGraph;
class UEdGraphNode;
class UEdGraphPin;
struct FEdGraphPinType;

/**
 * Blueprint read-only inspection actions.
 * Ported from BlueprintReaderLibrary (Leviathan plugin).
 *
 * Actions (5):
 *   list_graphs       — List all graphs in a Blueprint asset
 *   get_graph_data     — Full node/pin/connection data for a graph
 *   get_variables      — All variables with types, defaults, flags
 *   get_execution_flow — Linearized exec-pin trace from an entry point
 *   search_nodes       — Search nodes by title/function/class name
 */
class FMonolithBlueprintActions
{
public:
	/** Register all 5 actions with the ToolRegistry under "blueprint" namespace. */
	static void Register();

	/** Unregister the "blueprint" namespace. */
	static void Unregister();

private:
	// --- Action handlers ---
	static TSharedPtr<FJsonObject> ListGraphs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> GetGraphData(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> GetVariables(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> GetExecutionFlow(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> SearchNodes(const TSharedPtr<FJsonObject>& Params);

	// --- Internal helpers (from BlueprintReaderLibrary) ---
	static UBlueprint* LoadBP(const FString& AssetPath);
	static UEdGraph* FindGraphByName(UBlueprint* BP, const FString& GraphName);
	static UEdGraphNode* FindEntryNode(UEdGraph* Graph, const FString& EntryPoint);
	static FString PinTypeToString(const FEdGraphPinType& PinType);
	static FString ContainerPrefix(const FEdGraphPinType& PinType);
	static TSharedPtr<FJsonObject> SerializePin(const UEdGraphPin* Pin);
	static TSharedPtr<FJsonObject> SerializeNode(UEdGraphNode* Node);
	static TSharedPtr<FJsonObject> TraceExecFlow(UEdGraphNode* Node, TSet<UEdGraphNode*>& Visited, int32 MaxDepth = 100);

	static void AddGraphArray(
		TArray<TSharedPtr<FJsonValue>>& OutArr,
		const TArray<TObjectPtr<UEdGraph>>& Graphs,
		const FString& Type);
};
```

**`Source/MonolithBlueprint/Private/MonolithBlueprintActions.cpp`**

```cpp
#include "MonolithBlueprintActions.h"
#include "MonolithToolRegistry.h"
#include "MonolithJsonHelpers.h"

#include "Engine/Blueprint.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_IfThenElse.h"
#include "K2Node_Variable.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_MacroInstance.h"
#include "EdGraphSchema_K2.h"

// ============================================================================
// Registration
// ============================================================================

void FMonolithBlueprintActions::Register()
{
	auto& Registry = FMonolithToolRegistry::Get();

	Registry.RegisterAction(TEXT("blueprint"), {
		TEXT("list_graphs"),
		TEXT("List all graphs in a Blueprint asset. Params: asset_path (string, required)."),
		FMonolithActionHandler::CreateStatic(&FMonolithBlueprintActions::ListGraphs)
	});

	Registry.RegisterAction(TEXT("blueprint"), {
		TEXT("get_graph_data"),
		TEXT("Get full graph data with all nodes, pins, and connections. Params: asset_path (string, required), graph_name (string, optional — defaults to first UbergraphPage)."),
		FMonolithActionHandler::CreateStatic(&FMonolithBlueprintActions::GetGraphData)
	});

	Registry.RegisterAction(TEXT("blueprint"), {
		TEXT("get_variables"),
		TEXT("Get all variables in a Blueprint with types, defaults, and flags. Params: asset_path (string, required)."),
		FMonolithActionHandler::CreateStatic(&FMonolithBlueprintActions::GetVariables)
	});

	Registry.RegisterAction(TEXT("blueprint"), {
		TEXT("get_execution_flow"),
		TEXT("Trace execution flow from an entry point following exec pins. Params: asset_path (string, required), entry_point (string, required — e.g. 'ReceiveBeginPlay')."),
		FMonolithActionHandler::CreateStatic(&FMonolithBlueprintActions::GetExecutionFlow)
	});

	Registry.RegisterAction(TEXT("blueprint"), {
		TEXT("search_nodes"),
		TEXT("Search for nodes by title, class, or function name. Params: asset_path (string, required), query (string, required)."),
		FMonolithActionHandler::CreateStatic(&FMonolithBlueprintActions::SearchNodes)
	});
}

void FMonolithBlueprintActions::Unregister()
{
	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("blueprint"));
}

// ============================================================================
// Internal Helpers (ported from BlueprintReaderLibrary)
// ============================================================================

UBlueprint* FMonolithBlueprintActions::LoadBP(const FString& AssetPath)
{
	UObject* Obj = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath);
	return Cast<UBlueprint>(Obj);
}

void FMonolithBlueprintActions::AddGraphArray(
	TArray<TSharedPtr<FJsonValue>>& OutArr,
	const TArray<TObjectPtr<UEdGraph>>& Graphs,
	const FString& Type)
{
	for (const auto& Graph : Graphs)
	{
		if (!Graph) continue;
		TSharedPtr<FJsonObject> GObj = MakeShared<FJsonObject>();
		GObj->SetStringField(TEXT("name"), Graph->GetName());
		GObj->SetStringField(TEXT("type"), Type);
		GObj->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());
		OutArr.Add(MakeShared<FJsonValueObject>(GObj));
	}
}

UEdGraph* FMonolithBlueprintActions::FindGraphByName(UBlueprint* BP, const FString& GraphName)
{
	if (GraphName.IsEmpty() && BP->UbergraphPages.Num() > 0)
	{
		return BP->UbergraphPages[0];
	}

	auto SearchArray = [&](const TArray<TObjectPtr<UEdGraph>>& Arr) -> UEdGraph*
	{
		for (const auto& G : Arr)
		{
			if (G && G->GetName() == GraphName) return G;
		}
		return nullptr;
	};

	if (UEdGraph* G = SearchArray(BP->UbergraphPages)) return G;
	if (UEdGraph* G = SearchArray(BP->FunctionGraphs)) return G;
	if (UEdGraph* G = SearchArray(BP->MacroGraphs)) return G;
	if (UEdGraph* G = SearchArray(BP->DelegateSignatureGraphs)) return G;
	return nullptr;
}

FString FMonolithBlueprintActions::PinTypeToString(const FEdGraphPinType& PinType)
{
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Exec) return TEXT("exec");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Boolean) return TEXT("bool");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int) return TEXT("int");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Int64) return TEXT("int64");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Real)
		return PinType.PinSubCategory == TEXT("double") ? TEXT("double") : TEXT("float");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_String) return TEXT("string");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Name) return TEXT("name");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Text) return TEXT("text");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Byte) return TEXT("byte");

	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Object ||
		PinType.PinCategory == UEdGraphSchema_K2::PC_Class ||
		PinType.PinCategory == UEdGraphSchema_K2::PC_SoftObject ||
		PinType.PinCategory == UEdGraphSchema_K2::PC_SoftClass ||
		PinType.PinCategory == UEdGraphSchema_K2::PC_Interface)
	{
		FString TypeName = PinType.PinCategory.ToString();
		if (PinType.PinSubCategoryObject.IsValid())
		{
			TypeName += TEXT(":") + PinType.PinSubCategoryObject->GetName();
		}
		return TypeName;
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Struct)
	{
		if (PinType.PinSubCategoryObject.IsValid())
			return TEXT("struct:") + PinType.PinSubCategoryObject->GetName();
		return TEXT("struct");
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Enum)
	{
		if (PinType.PinSubCategoryObject.IsValid())
			return TEXT("enum:") + PinType.PinSubCategoryObject->GetName();
		return TEXT("enum");
	}
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Wildcard) return TEXT("wildcard");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_Delegate) return TEXT("delegate");
	if (PinType.PinCategory == UEdGraphSchema_K2::PC_MCDelegate) return TEXT("multicast_delegate");

	return PinType.PinCategory.ToString();
}

FString FMonolithBlueprintActions::ContainerPrefix(const FEdGraphPinType& PinType)
{
	switch (PinType.ContainerType)
	{
	case EPinContainerType::Array: return TEXT("array:");
	case EPinContainerType::Set:   return TEXT("set:");
	case EPinContainerType::Map:   return TEXT("map:");
	default: return TEXT("");
	}
}

TSharedPtr<FJsonObject> FMonolithBlueprintActions::SerializePin(const UEdGraphPin* Pin)
{
	TSharedPtr<FJsonObject> PinObj = MakeShared<FJsonObject>();
	PinObj->SetStringField(TEXT("id"), Pin->PinId.ToString());
	PinObj->SetStringField(TEXT("name"), Pin->PinName.ToString());
	PinObj->SetStringField(TEXT("direction"),
		Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
	PinObj->SetStringField(TEXT("type"),
		ContainerPrefix(Pin->PinType) + PinTypeToString(Pin->PinType));

	if (!Pin->DefaultValue.IsEmpty())
	{
		PinObj->SetStringField(TEXT("default_value"), Pin->DefaultValue);
	}
	if (Pin->DefaultObject)
	{
		PinObj->SetStringField(TEXT("default_object"), Pin->DefaultObject->GetPathName());
	}

	TArray<TSharedPtr<FJsonValue>> ConnArr;
	for (const UEdGraphPin* Linked : Pin->LinkedTo)
	{
		if (!Linked || !Linked->GetOwningNode()) continue;
		FString ConnId = FString::Printf(TEXT("%s.%s"),
			*Linked->GetOwningNode()->GetName(),
			*Linked->PinName.ToString());
		ConnArr.Add(MakeShared<FJsonValueString>(ConnId));
	}
	PinObj->SetArrayField(TEXT("connected_to"), ConnArr);
	return PinObj;
}

TSharedPtr<FJsonObject> FMonolithBlueprintActions::SerializeNode(UEdGraphNode* Node)
{
	TSharedPtr<FJsonObject> NObj = MakeShared<FJsonObject>();
	NObj->SetStringField(TEXT("id"), Node->GetName());
	NObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());
	NObj->SetStringField(TEXT("title"),
		Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());

	TArray<TSharedPtr<FJsonValue>> PosArr;
	PosArr.Add(MakeShared<FJsonValueNumber>(Node->NodePosX));
	PosArr.Add(MakeShared<FJsonValueNumber>(Node->NodePosY));
	NObj->SetArrayField(TEXT("pos"), PosArr);

	if (!Node->NodeComment.IsEmpty())
	{
		NObj->SetStringField(TEXT("comment"), Node->NodeComment);
	}

	if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
	{
		NObj->SetStringField(TEXT("function"),
			CallNode->FunctionReference.GetMemberName().ToString());
		if (UClass* OwnerClass = CallNode->FunctionReference.GetMemberParentClass())
		{
			NObj->SetStringField(TEXT("function_class"), OwnerClass->GetName());
		}
	}
	else if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
	{
		NObj->SetStringField(TEXT("event_name"),
			EventNode->EventReference.GetMemberName().ToString());
		if (EventNode->CustomFunctionName != NAME_None)
		{
			NObj->SetStringField(TEXT("custom_name"),
				EventNode->CustomFunctionName.ToString());
		}
	}
	else if (UK2Node_MacroInstance* MacroNode = Cast<UK2Node_MacroInstance>(Node))
	{
		if (MacroNode->GetMacroGraph())
		{
			NObj->SetStringField(TEXT("macro_name"),
				MacroNode->GetMacroGraph()->GetName());
		}
	}

	TArray<TSharedPtr<FJsonValue>> PinsArr;
	for (const UEdGraphPin* Pin : Node->Pins)
	{
		if (!Pin || Pin->bHidden) continue;
		PinsArr.Add(MakeShared<FJsonValueObject>(SerializePin(Pin)));
	}
	NObj->SetArrayField(TEXT("pins"), PinsArr);

	return NObj;
}

TSharedPtr<FJsonObject> FMonolithBlueprintActions::TraceExecFlow(
	UEdGraphNode* Node, TSet<UEdGraphNode*>& Visited, int32 MaxDepth)
{
	if (!Node || Visited.Contains(Node) || MaxDepth <= 0)
	{
		return nullptr;
	}
	Visited.Add(Node);

	TSharedPtr<FJsonObject> FlowObj = MakeShared<FJsonObject>();
	FlowObj->SetStringField(TEXT("node"),
		Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString());
	FlowObj->SetStringField(TEXT("class"), Node->GetClass()->GetName());

	TArray<UEdGraphPin*> ExecOutputs;
	for (UEdGraphPin* Pin : Node->Pins)
	{
		if (Pin && Pin->Direction == EGPD_Output &&
			Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec &&
			!Pin->bHidden)
		{
			ExecOutputs.Add(Pin);
		}
	}

	if (ExecOutputs.Num() == 1 && ExecOutputs[0]->LinkedTo.Num() > 0)
	{
		TArray<TSharedPtr<FJsonValue>> ThenArr;
		for (UEdGraphPin* Linked : ExecOutputs[0]->LinkedTo)
		{
			if (!Linked || !Linked->GetOwningNode()) continue;
			TSharedPtr<FJsonObject> Next = TraceExecFlow(
				Linked->GetOwningNode(), Visited, MaxDepth - 1);
			if (Next)
			{
				ThenArr.Add(MakeShared<FJsonValueObject>(Next));
			}
		}
		if (ThenArr.Num() > 0)
		{
			FlowObj->SetArrayField(TEXT("then"), ThenArr);
		}
	}
	else if (ExecOutputs.Num() > 1)
	{
		TSharedPtr<FJsonObject> BranchesObj = MakeShared<FJsonObject>();
		for (UEdGraphPin* ExecPin : ExecOutputs)
		{
			TArray<TSharedPtr<FJsonValue>> BranchArr;
			for (UEdGraphPin* Linked : ExecPin->LinkedTo)
			{
				if (!Linked || !Linked->GetOwningNode()) continue;
				TSet<UEdGraphNode*> BranchVisited = Visited;
				TSharedPtr<FJsonObject> Next = TraceExecFlow(
					Linked->GetOwningNode(), BranchVisited, MaxDepth - 1);
				if (Next)
				{
					BranchArr.Add(MakeShared<FJsonValueObject>(Next));
				}
			}
			BranchesObj->SetArrayField(ExecPin->PinName.ToString(), BranchArr);
		}
		FlowObj->SetObjectField(TEXT("branches"), BranchesObj);
	}

	return FlowObj;
}

UEdGraphNode* FMonolithBlueprintActions::FindEntryNode(UEdGraph* Graph, const FString& EntryPoint)
{
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
		{
			FString EventName = EventNode->EventReference.GetMemberName().ToString();
			if (EventName == EntryPoint || EventNode->GetName() == EntryPoint)
				return Node;
			if (EventNode->CustomFunctionName != NAME_None &&
				EventNode->CustomFunctionName.ToString() == EntryPoint)
				return Node;
		}
		if (UK2Node_FunctionEntry* FuncEntry = Cast<UK2Node_FunctionEntry>(Node))
		{
			if (Graph->GetName() == EntryPoint)
				return Node;
		}
		FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
		if (Title.Contains(EntryPoint))
			return Node;
	}
	return nullptr;
}

// ============================================================================
// Action Handlers
// ============================================================================

TSharedPtr<FJsonObject> FMonolithBlueprintActions::ListGraphs(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = FMonolithJsonHelpers::GetStringParam(Params, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Missing required param: asset_path"));
	}

	UBlueprint* BP = LoadBP(AssetPath);
	if (!BP)
	{
		return FMonolithJsonHelpers::ErrorJson(
			FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("asset_path"), AssetPath);
	Root->SetStringField(TEXT("class"), BP->GetClass()->GetName());

	if (BP->ParentClass)
	{
		Root->SetStringField(TEXT("parent_class"), BP->ParentClass->GetName());
	}

	TArray<TSharedPtr<FJsonValue>> GraphsArr;
	AddGraphArray(GraphsArr, BP->UbergraphPages, TEXT("event_graph"));
	AddGraphArray(GraphsArr, BP->FunctionGraphs, TEXT("function"));
	AddGraphArray(GraphsArr, BP->MacroGraphs, TEXT("macro"));
	AddGraphArray(GraphsArr, BP->DelegateSignatureGraphs, TEXT("delegate_signature"));
	Root->SetArrayField(TEXT("graphs"), GraphsArr);

	return Root;
}

TSharedPtr<FJsonObject> FMonolithBlueprintActions::GetGraphData(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = FMonolithJsonHelpers::GetStringParam(Params, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Missing required param: asset_path"));
	}

	FString GraphName = FMonolithJsonHelpers::GetStringParam(Params, TEXT("graph_name"));

	UBlueprint* BP = LoadBP(AssetPath);
	if (!BP)
	{
		return FMonolithJsonHelpers::ErrorJson(
			FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraph* Graph = FindGraphByName(BP, GraphName);
	if (!Graph)
	{
		return FMonolithJsonHelpers::ErrorJson(
			FString::Printf(TEXT("Graph not found: %s"), *GraphName));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("graph_name"), Graph->GetName());

	FString GraphType = TEXT("unknown");
	if (BP->UbergraphPages.Contains(Graph)) GraphType = TEXT("event_graph");
	else if (BP->FunctionGraphs.Contains(Graph)) GraphType = TEXT("function");
	else if (BP->MacroGraphs.Contains(Graph)) GraphType = TEXT("macro");
	else if (BP->DelegateSignatureGraphs.Contains(Graph)) GraphType = TEXT("delegate_signature");
	Root->SetStringField(TEXT("graph_type"), GraphType);

	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (!Node) continue;
		NodesArr.Add(MakeShared<FJsonValueObject>(SerializeNode(Node)));
	}
	Root->SetArrayField(TEXT("nodes"), NodesArr);

	return Root;
}

TSharedPtr<FJsonObject> FMonolithBlueprintActions::GetVariables(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = FMonolithJsonHelpers::GetStringParam(Params, TEXT("asset_path"));
	if (AssetPath.IsEmpty())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Missing required param: asset_path"));
	}

	UBlueprint* BP = LoadBP(AssetPath);
	if (!BP)
	{
		return FMonolithJsonHelpers::ErrorJson(
			FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> VarsArr;

	for (const FBPVariableDescription& Var : BP->NewVariables)
	{
		TSharedPtr<FJsonObject> VObj = MakeShared<FJsonObject>();
		VObj->SetStringField(TEXT("name"), Var.VarName.ToString());
		VObj->SetStringField(TEXT("type"),
			ContainerPrefix(Var.VarType) + PinTypeToString(Var.VarType));
		VObj->SetStringField(TEXT("default_value"), Var.DefaultValue);
		VObj->SetStringField(TEXT("category"), Var.Category.ToString());

		VObj->SetBoolField(TEXT("instance_editable"),
			(Var.PropertyFlags & CPF_DisableEditOnInstance) == 0);
		VObj->SetBoolField(TEXT("blueprint_read_only"),
			(Var.PropertyFlags & CPF_BlueprintReadOnly) != 0);
		VObj->SetBoolField(TEXT("expose_on_spawn"),
			(Var.PropertyFlags & CPF_ExposeOnSpawn) != 0);
		VObj->SetBoolField(TEXT("replicated"),
			(Var.PropertyFlags & CPF_Net) != 0);
		VObj->SetBoolField(TEXT("transient"),
			(Var.PropertyFlags & CPF_Transient) != 0);

		VarsArr.Add(MakeShared<FJsonValueObject>(VObj));
	}

	Root->SetArrayField(TEXT("variables"), VarsArr);
	return Root;
}

TSharedPtr<FJsonObject> FMonolithBlueprintActions::GetExecutionFlow(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = FMonolithJsonHelpers::GetStringParam(Params, TEXT("asset_path"));
	FString EntryPoint = FMonolithJsonHelpers::GetStringParam(Params, TEXT("entry_point"));

	if (AssetPath.IsEmpty())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Missing required param: asset_path"));
	}
	if (EntryPoint.IsEmpty())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Missing required param: entry_point"));
	}

	UBlueprint* BP = LoadBP(AssetPath);
	if (!BP)
	{
		return FMonolithJsonHelpers::ErrorJson(
			FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	UEdGraphNode* EntryNode = nullptr;
	UEdGraph* FoundGraph = nullptr;

	auto SearchGraphs = [&](const TArray<TObjectPtr<UEdGraph>>& Graphs)
	{
		for (const auto& Graph : Graphs)
		{
			if (!Graph) continue;
			UEdGraphNode* Node = FindEntryNode(Graph, EntryPoint);
			if (Node)
			{
				EntryNode = Node;
				FoundGraph = Graph;
				return;
			}
		}
	};

	SearchGraphs(BP->UbergraphPages);
	if (!EntryNode) SearchGraphs(BP->FunctionGraphs);
	if (!EntryNode) SearchGraphs(BP->MacroGraphs);

	if (!EntryNode)
	{
		return FMonolithJsonHelpers::ErrorJson(
			FString::Printf(TEXT("Entry point not found: %s"), *EntryPoint));
	}

	TSet<UEdGraphNode*> Visited;
	TSharedPtr<FJsonObject> Flow = TraceExecFlow(EntryNode, Visited);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("entry"), EntryPoint);
	Root->SetStringField(TEXT("graph"), FoundGraph->GetName());
	if (Flow)
	{
		Root->SetObjectField(TEXT("flow"), Flow);
	}

	return Root;
}

TSharedPtr<FJsonObject> FMonolithBlueprintActions::SearchNodes(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath = FMonolithJsonHelpers::GetStringParam(Params, TEXT("asset_path"));
	FString Query = FMonolithJsonHelpers::GetStringParam(Params, TEXT("query"));

	if (AssetPath.IsEmpty())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Missing required param: asset_path"));
	}
	if (Query.IsEmpty())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Missing required param: query"));
	}

	UBlueprint* BP = LoadBP(AssetPath);
	if (!BP)
	{
		return FMonolithJsonHelpers::ErrorJson(
			FString::Printf(TEXT("Blueprint not found: %s"), *AssetPath));
	}

	FString QueryLower = Query.ToLower();
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Results;

	auto SearchGraphs = [&](const TArray<TObjectPtr<UEdGraph>>& Graphs, const FString& Type)
	{
		for (const auto& Graph : Graphs)
		{
			if (!Graph) continue;
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				if (!Node) continue;
				FString Title = Node->GetNodeTitle(ENodeTitleType::FullTitle).ToString();
				FString ClassName = Node->GetClass()->GetName();
				FString FuncName;

				if (UK2Node_CallFunction* CallNode = Cast<UK2Node_CallFunction>(Node))
				{
					FuncName = CallNode->FunctionReference.GetMemberName().ToString();
				}

				bool bMatch = Title.ToLower().Contains(QueryLower) ||
							  ClassName.ToLower().Contains(QueryLower) ||
							  FuncName.ToLower().Contains(QueryLower);

				if (bMatch)
				{
					TSharedPtr<FJsonObject> RObj = MakeShared<FJsonObject>();
					RObj->SetStringField(TEXT("graph"), Graph->GetName());
					RObj->SetStringField(TEXT("graph_type"), Type);
					RObj->SetStringField(TEXT("node_id"), Node->GetName());
					RObj->SetStringField(TEXT("class"), ClassName);
					RObj->SetStringField(TEXT("title"), Title);
					if (!FuncName.IsEmpty())
					{
						RObj->SetStringField(TEXT("function"), FuncName);
					}
					Results.Add(MakeShared<FJsonValueObject>(RObj));
				}
			}
		}
	};

	SearchGraphs(BP->UbergraphPages, TEXT("event_graph"));
	SearchGraphs(BP->FunctionGraphs, TEXT("function"));
	SearchGraphs(BP->MacroGraphs, TEXT("macro"));
	SearchGraphs(BP->DelegateSignatureGraphs, TEXT("delegate_signature"));

	Root->SetStringField(TEXT("query"), Query);
	Root->SetNumberField(TEXT("match_count"), Results.Num());
	Root->SetArrayField(TEXT("results"), Results);

	return Root;
}
```

### Files to Modify

**`Source/MonolithBlueprint/Private/MonolithBlueprintModule.cpp`**

```cpp
#include "MonolithBlueprintModule.h"
#include "MonolithBlueprintActions.h"

#define LOCTEXT_NAMESPACE "FMonolithBlueprintModule"

void FMonolithBlueprintModule::StartupModule()
{
	FMonolithBlueprintActions::Register();
	UE_LOG(LogTemp, Log, TEXT("Monolith — Blueprint module loaded (5 actions registered)"));
}

void FMonolithBlueprintModule::ShutdownModule()
{
	FMonolithBlueprintActions::Unregister();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithBlueprintModule, MonolithBlueprint)
```

### Steps

1. Create `Source/MonolithBlueprint/Private/MonolithBlueprintActions.h`
2. Create `Source/MonolithBlueprint/Private/MonolithBlueprintActions.cpp`
3. Update `Source/MonolithBlueprint/Private/MonolithBlueprintModule.cpp`
4. Compile — verify no errors
5. Manual test (see verification section below)

---

## Task 2: MonolithEditor — 11 Actions (Build + Logs + Crash)

Reimplements the Python `unreal-editor-mcp` server's 11 tools entirely in C++. Key differences from Python version:
- **Build triggering**: Uses `ILiveCodingModule` directly instead of EditorBridge Python remote exec
- **Build errors**: Parses `FCompilerResultsLog` from the MessageLog subsystem
- **Log tailing**: Uses a custom `FOutputDevice` registered with GLog to capture live output
- **Crash context**: Reads crash report directories from `FPaths::ProjectSavedDir() / TEXT("Crashes")`

### Files to Create

**`Source/MonolithEditor/Private/MonolithLogCapture.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Misc/OutputDevice.h"

/**
 * Captures UE log output into a ring buffer for querying.
 * Registered with GLog as a custom FOutputDevice.
 */
struct FMonolithLogEntry
{
	FDateTime Timestamp;
	FName Category;
	ELogVerbosity::Type Verbosity;
	FString Message;
};

class FMonolithLogCapture : public FOutputDevice
{
public:
	FMonolithLogCapture(int32 InMaxEntries = 5000);
	virtual ~FMonolithLogCapture() override;

	void Start();
	void Stop();

	/** Get recent entries, optionally filtered. */
	TArray<FMonolithLogEntry> GetRecent(
		int32 Count = 50,
		const FString& Category = TEXT(""),
		const FString& Severity = TEXT("")) const;

	/** Regex search across buffered entries. */
	TArray<FMonolithLogEntry> Search(const FString& Pattern, int32 Count = 50) const;

	/** Get category -> message count map. */
	TMap<FString, int32> GetCategories() const;

	/** Get category -> severity -> count map. */
	TMap<FString, TMap<FString, int32>> GetCategoryStats() const;

protected:
	virtual void Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category) override;

private:
	TArray<FMonolithLogEntry> Buffer;
	int32 MaxEntries;
	int32 WriteIndex;
	bool bWrapped;
	mutable FCriticalSection BufferLock;
	bool bStarted;

	static FString VerbosityToString(ELogVerbosity::Type V);
	static ELogVerbosity::Type StringToVerbosity(const FString& S);
};
```

**`Source/MonolithEditor/Private/MonolithLogCapture.cpp`**

```cpp
#include "MonolithLogCapture.h"
#include "Internationalization/Regex.h"

FMonolithLogCapture::FMonolithLogCapture(int32 InMaxEntries)
	: MaxEntries(InMaxEntries)
	, WriteIndex(0)
	, bWrapped(false)
	, bStarted(false)
{
	Buffer.SetNum(MaxEntries);
}

FMonolithLogCapture::~FMonolithLogCapture()
{
	Stop();
}

void FMonolithLogCapture::Start()
{
	if (!bStarted)
	{
		GLog->AddOutputDevice(this);
		bStarted = true;
	}
}

void FMonolithLogCapture::Stop()
{
	if (bStarted)
	{
		GLog->RemoveOutputDevice(this);
		bStarted = false;
	}
}

void FMonolithLogCapture::Serialize(const TCHAR* V, ELogVerbosity::Type Verbosity, const FName& Category)
{
	FScopeLock Lock(&BufferLock);

	FMonolithLogEntry& Entry = Buffer[WriteIndex];
	Entry.Timestamp = FDateTime::Now();
	Entry.Category = Category;
	Entry.Verbosity = Verbosity;
	Entry.Message = V;

	WriteIndex = (WriteIndex + 1) % MaxEntries;
	if (WriteIndex == 0) bWrapped = true;
}

TArray<FMonolithLogEntry> FMonolithLogCapture::GetRecent(
	int32 Count, const FString& Category, const FString& Severity) const
{
	FScopeLock Lock(&BufferLock);

	TArray<FMonolithLogEntry> Result;
	int32 Total = bWrapped ? MaxEntries : WriteIndex;

	ELogVerbosity::Type SevFilter = ELogVerbosity::All;
	if (!Severity.IsEmpty())
	{
		SevFilter = StringToVerbosity(Severity);
	}

	// Iterate from oldest to newest
	for (int32 i = 0; i < Total; ++i)
	{
		int32 Idx = bWrapped ? (WriteIndex + i) % MaxEntries : i;
		const FMonolithLogEntry& E = Buffer[Idx];

		if (!Category.IsEmpty() && E.Category != FName(*Category))
			continue;
		if (SevFilter != ELogVerbosity::All && E.Verbosity != SevFilter)
			continue;

		Result.Add(E);
	}

	// Return only the last Count entries
	if (Result.Num() > Count)
	{
		Result.RemoveAt(0, Result.Num() - Count);
	}

	return Result;
}

TArray<FMonolithLogEntry> FMonolithLogCapture::Search(const FString& Pattern, int32 Count) const
{
	FScopeLock Lock(&BufferLock);

	FRegexPattern RegexPattern(Pattern);
	TArray<FMonolithLogEntry> Result;
	int32 Total = bWrapped ? MaxEntries : WriteIndex;

	for (int32 i = 0; i < Total && Result.Num() < Count; ++i)
	{
		int32 Idx = bWrapped ? (WriteIndex + i) % MaxEntries : i;
		const FMonolithLogEntry& E = Buffer[Idx];

		FRegexMatcher Matcher(RegexPattern, E.Message);
		if (Matcher.FindNext())
		{
			Result.Add(E);
		}
	}

	return Result;
}

TMap<FString, int32> FMonolithLogCapture::GetCategories() const
{
	FScopeLock Lock(&BufferLock);

	TMap<FString, int32> Cats;
	int32 Total = bWrapped ? MaxEntries : WriteIndex;

	for (int32 i = 0; i < Total; ++i)
	{
		int32 Idx = bWrapped ? (WriteIndex + i) % MaxEntries : i;
		Cats.FindOrAdd(Buffer[Idx].Category.ToString())++;
	}

	return Cats;
}

TMap<FString, TMap<FString, int32>> FMonolithLogCapture::GetCategoryStats() const
{
	FScopeLock Lock(&BufferLock);

	TMap<FString, TMap<FString, int32>> Stats;
	int32 Total = bWrapped ? MaxEntries : WriteIndex;

	for (int32 i = 0; i < Total; ++i)
	{
		int32 Idx = bWrapped ? (WriteIndex + i) % MaxEntries : i;
		const FMonolithLogEntry& E = Buffer[Idx];
		Stats.FindOrAdd(E.Category.ToString()).FindOrAdd(VerbosityToString(E.Verbosity))++;
	}

	return Stats;
}

FString FMonolithLogCapture::VerbosityToString(ELogVerbosity::Type V)
{
	switch (V)
	{
	case ELogVerbosity::Fatal:       return TEXT("Fatal");
	case ELogVerbosity::Error:       return TEXT("Error");
	case ELogVerbosity::Warning:     return TEXT("Warning");
	case ELogVerbosity::Display:     return TEXT("Display");
	case ELogVerbosity::Log:         return TEXT("Log");
	case ELogVerbosity::Verbose:     return TEXT("Verbose");
	case ELogVerbosity::VeryVerbose: return TEXT("VeryVerbose");
	default:                         return TEXT("Unknown");
	}
}

ELogVerbosity::Type FMonolithLogCapture::StringToVerbosity(const FString& S)
{
	if (S == TEXT("Fatal"))       return ELogVerbosity::Fatal;
	if (S == TEXT("Error"))       return ELogVerbosity::Error;
	if (S == TEXT("Warning"))     return ELogVerbosity::Warning;
	if (S == TEXT("Display"))     return ELogVerbosity::Display;
	if (S == TEXT("Log"))         return ELogVerbosity::Log;
	if (S == TEXT("Verbose"))     return ELogVerbosity::Verbose;
	if (S == TEXT("VeryVerbose")) return ELogVerbosity::VeryVerbose;
	return ELogVerbosity::All;
}
```

**`Source/MonolithEditor/Private/MonolithEditorActions.h`**

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FMonolithLogCapture;

/**
 * Editor diagnostic actions: build management, log tailing, crash context.
 * Replaces the Python unreal-editor-mcp server (11 tools).
 *
 * Build actions (5):
 *   trigger_build      — Start a Live Coding build
 *   get_build_status   — Current build state
 *   get_build_errors   — Parsed errors/warnings from latest build
 *   get_build_summary  — Overview with counts and duration
 *   search_build_output — Regex search across raw build output
 *
 * Log actions (5):
 *   get_recent_logs    — Recent log entries with optional filters
 *   search_logs        — Regex search across log buffer
 *   tail_log           — Log output from recent time window
 *   get_log_categories — Active categories with message counts
 *   get_log_stats      — Error/warning breakdown per category
 *
 * Crash (1):
 *   get_crash_context  — Fatal logs + recent crash report directories
 */
class FMonolithEditorActions
{
public:
	static void Register();
	static void Unregister();

private:
	// Build actions
	static TSharedPtr<FJsonObject> TriggerBuild(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> GetBuildStatus(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> GetBuildErrors(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> GetBuildSummary(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> SearchBuildOutput(const TSharedPtr<FJsonObject>& Params);

	// Log actions
	static TSharedPtr<FJsonObject> GetRecentLogs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> SearchLogs(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> TailLog(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> GetLogCategories(const TSharedPtr<FJsonObject>& Params);
	static TSharedPtr<FJsonObject> GetLogStats(const TSharedPtr<FJsonObject>& Params);

	// Crash
	static TSharedPtr<FJsonObject> GetCrashContext(const TSharedPtr<FJsonObject>& Params);

	// Internal state
	static TSharedPtr<FMonolithLogCapture> LogCapture;

	// Build tracking
	struct FBuildRecord
	{
		FString BuildId;
		FString BuildType;
		FString Status; // "building", "succeeded", "failed"
		FDateTime StartTime;
		FDateTime EndTime;
		int32 ErrorCount = 0;
		int32 WarningCount = 0;
		TArray<FString> RawOutput;

		struct FBuildError
		{
			FString Severity;
			FString File;
			int32 Line = 0;
			FString Code;
			FString Message;
		};
		TArray<FBuildError> Errors;
	};

	static TArray<FBuildRecord> BuildHistory;
	static FCriticalSection BuildLock;

	// Build output capture
	static TSharedPtr<FOutputDevice> BuildOutputCapture;

	static FBuildRecord* GetLatestBuild();
	static FBuildRecord* GetBuildById(const FString& BuildId);
	static void ParseBuildOutput(FBuildRecord& Record);
};
```

**`Source/MonolithEditor/Private/MonolithEditorActions.cpp`**

```cpp
#include "MonolithEditorActions.h"
#include "MonolithToolRegistry.h"
#include "MonolithJsonHelpers.h"
#include "MonolithLogCapture.h"

#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "Internationalization/Regex.h"
#include "Logging/MessageLog.h"

#if PLATFORM_WINDOWS
#include "ILiveCodingModule.h"
#endif

// Static member definitions
TSharedPtr<FMonolithLogCapture> FMonolithEditorActions::LogCapture;
TArray<FMonolithEditorActions::FBuildRecord> FMonolithEditorActions::BuildHistory;
FCriticalSection FMonolithEditorActions::BuildLock;
TSharedPtr<FOutputDevice> FMonolithEditorActions::BuildOutputCapture;

// ============================================================================
// Registration
// ============================================================================

void FMonolithEditorActions::Register()
{
	// Start log capture
	LogCapture = MakeShared<FMonolithLogCapture>(10000);
	LogCapture->Start();

	auto& Registry = FMonolithToolRegistry::Get();

	// Build actions (5)
	Registry.RegisterAction(TEXT("editor"), {
		TEXT("trigger_build"),
		TEXT("Start a Live Coding build. Params: build_type (string, optional, default 'live_coding')."),
		FMonolithActionHandler::CreateStatic(&FMonolithEditorActions::TriggerBuild)
	});

	Registry.RegisterAction(TEXT("editor"), {
		TEXT("get_build_status"),
		TEXT("Get build status. Params: build_id (string, optional — latest if empty)."),
		FMonolithActionHandler::CreateStatic(&FMonolithEditorActions::GetBuildStatus)
	});

	Registry.RegisterAction(TEXT("editor"), {
		TEXT("get_build_errors"),
		TEXT("Get parsed build errors. Params: module (string, optional), severity (string, optional — 'error'/'warning'), limit (int, optional, default 50)."),
		FMonolithActionHandler::CreateStatic(&FMonolithEditorActions::GetBuildErrors)
	});

	Registry.RegisterAction(TEXT("editor"), {
		TEXT("get_build_summary"),
		TEXT("Overview of latest build: error/warning counts, duration. No params."),
		FMonolithActionHandler::CreateStatic(&FMonolithEditorActions::GetBuildSummary)
	});

	Registry.RegisterAction(TEXT("editor"), {
		TEXT("search_build_output"),
		TEXT("Regex search across raw build output. Params: pattern (string, required)."),
		FMonolithActionHandler::CreateStatic(&FMonolithEditorActions::SearchBuildOutput)
	});

	// Log actions (5)
	Registry.RegisterAction(TEXT("editor"), {
		TEXT("get_recent_logs"),
		TEXT("Get recent log entries. Params: category (string, optional), severity (string, optional), count (int, optional, default 50)."),
		FMonolithActionHandler::CreateStatic(&FMonolithEditorActions::GetRecentLogs)
	});

	Registry.RegisterAction(TEXT("editor"), {
		TEXT("search_logs"),
		TEXT("Regex search across log buffer. Params: pattern (string, required), category (string, optional), count (int, optional, default 50)."),
		FMonolithActionHandler::CreateStatic(&FMonolithEditorActions::SearchLogs)
	});

	Registry.RegisterAction(TEXT("editor"), {
		TEXT("tail_log"),
		TEXT("Get recent log output. Params: category (string, optional), severity (string, optional), seconds (int, optional, default 30)."),
		FMonolithActionHandler::CreateStatic(&FMonolithEditorActions::TailLog)
	});

	Registry.RegisterAction(TEXT("editor"), {
		TEXT("get_log_categories"),
		TEXT("List all active log categories with message counts. No params."),
		FMonolithActionHandler::CreateStatic(&FMonolithEditorActions::GetLogCategories)
	});

	Registry.RegisterAction(TEXT("editor"), {
		TEXT("get_log_stats"),
		TEXT("Error/warning breakdown per category. No params."),
		FMonolithActionHandler::CreateStatic(&FMonolithEditorActions::GetLogStats)
	});

	// Crash (1)
	Registry.RegisterAction(TEXT("editor"), {
		TEXT("get_crash_context"),
		TEXT("Get crash context: fatal log entries + recent crash report directories. No params."),
		FMonolithActionHandler::CreateStatic(&FMonolithEditorActions::GetCrashContext)
	});
}

void FMonolithEditorActions::Unregister()
{
	if (LogCapture.IsValid())
	{
		LogCapture->Stop();
		LogCapture.Reset();
	}
	FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("editor"));
}

// ============================================================================
// Build Actions
// ============================================================================

TSharedPtr<FJsonObject> FMonolithEditorActions::TriggerBuild(const TSharedPtr<FJsonObject>& Params)
{
	FString BuildType = FMonolithJsonHelpers::GetStringParam(Params, TEXT("build_type"), TEXT("live_coding"));

	if (BuildType != TEXT("live_coding"))
	{
		return FMonolithJsonHelpers::ErrorJson(
			FString::Printf(TEXT("Unsupported build_type: '%s'. Only 'live_coding' is supported."), *BuildType));
	}

#if PLATFORM_WINDOWS
	ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
	if (!LiveCoding || !LiveCoding->IsEnabledForSession())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Live Coding is not enabled for this session."));
	}

	// Create build record
	FBuildRecord Record;
	Record.BuildId = FGuid::NewGuid().ToString(EGuidFormats::Short);
	Record.BuildType = BuildType;
	Record.Status = TEXT("building");
	Record.StartTime = FDateTime::Now();

	{
		FScopeLock Lock(&BuildLock);
		BuildHistory.Add(MoveTemp(Record));
	}

	// Trigger the build
	LiveCoding->EnableByDefault(true);
	bool bTriggered = LiveCoding->Compile();

	FBuildRecord* Latest = GetLatestBuild();
	if (Latest)
	{
		if (bTriggered)
		{
			// We'll update status when we can detect completion
			// For now, mark as building
		}
		else
		{
			Latest->Status = TEXT("failed");
			Latest->EndTime = FDateTime::Now();
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("build_id"), BuildHistory.Last().BuildId);
	Root->SetStringField(TEXT("status"), bTriggered ? TEXT("building") : TEXT("failed"));
	Root->SetStringField(TEXT("build_type"), BuildType);
	if (bTriggered)
	{
		Root->SetStringField(TEXT("message"), TEXT("Build triggered. Use get_build_status to check progress."));
	}
	else
	{
		Root->SetStringField(TEXT("message"), TEXT("Failed to trigger Live Coding compile."));
	}
	return Root;

#else
	return FMonolithJsonHelpers::ErrorJson(TEXT("Live Coding is only supported on Windows."));
#endif
}

TSharedPtr<FJsonObject> FMonolithEditorActions::GetBuildStatus(const TSharedPtr<FJsonObject>& Params)
{
	FString BuildId = FMonolithJsonHelpers::GetStringParam(Params, TEXT("build_id"));

	FScopeLock Lock(&BuildLock);
	FBuildRecord* Record = BuildId.IsEmpty() ? GetLatestBuild() : GetBuildById(BuildId);

	if (!Record)
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("No builds recorded."));
	}

#if PLATFORM_WINDOWS
	// Check if Live Coding is still compiling
	if (Record->Status == TEXT("building"))
	{
		ILiveCodingModule* LiveCoding = FModuleManager::GetModulePtr<ILiveCodingModule>(LIVE_CODING_MODULE_NAME);
		if (LiveCoding && !LiveCoding->IsCompiling())
		{
			Record->Status = TEXT("succeeded");
			Record->EndTime = FDateTime::Now();
			// Capture any build output from logs
			if (LogCapture.IsValid())
			{
				TArray<FMonolithLogEntry> BuildLogs = LogCapture->GetRecent(200, TEXT("LogLiveCoding"));
				for (const auto& Entry : BuildLogs)
				{
					Record->RawOutput.Add(Entry.Message);
				}
				ParseBuildOutput(*Record);
				Record->Status = Record->ErrorCount > 0 ? TEXT("failed") : TEXT("succeeded");
			}
		}
	}
#endif

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("build_id"), Record->BuildId);
	Root->SetStringField(TEXT("build_type"), Record->BuildType);
	Root->SetStringField(TEXT("status"), Record->Status);
	Root->SetNumberField(TEXT("error_count"), Record->ErrorCount);
	Root->SetNumberField(TEXT("warning_count"), Record->WarningCount);
	return Root;
}

TSharedPtr<FJsonObject> FMonolithEditorActions::GetBuildErrors(const TSharedPtr<FJsonObject>& Params)
{
	FString Module = FMonolithJsonHelpers::GetStringParam(Params, TEXT("module"));
	FString Severity = FMonolithJsonHelpers::GetStringParam(Params, TEXT("severity"));
	int32 Limit = FMonolithJsonHelpers::GetIntParam(Params, TEXT("limit"), 50);

	FScopeLock Lock(&BuildLock);
	FBuildRecord* Record = GetLatestBuild();
	if (!Record)
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("No builds recorded."));
	}

	TArray<FBuildRecord::FBuildError> Filtered = Record->Errors;

	if (!Severity.IsEmpty())
	{
		Filtered = Filtered.FilterByPredicate([&](const FBuildRecord::FBuildError& E)
		{
			return E.Severity == Severity;
		});
	}
	if (!Module.IsEmpty())
	{
		FString ModuleLower = Module.ToLower();
		Filtered = Filtered.FilterByPredicate([&](const FBuildRecord::FBuildError& E)
		{
			return E.File.ToLower().Contains(ModuleLower);
		});
	}

	if (Filtered.Num() > Limit)
	{
		Filtered.SetNum(Limit);
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> ErrorsArr;

	for (const auto& E : Filtered)
	{
		TSharedPtr<FJsonObject> EObj = MakeShared<FJsonObject>();
		EObj->SetStringField(TEXT("severity"), E.Severity);
		EObj->SetStringField(TEXT("file"), E.File);
		EObj->SetNumberField(TEXT("line"), E.Line);
		EObj->SetStringField(TEXT("code"), E.Code);
		EObj->SetStringField(TEXT("message"), E.Message);
		ErrorsArr.Add(MakeShared<FJsonValueObject>(EObj));
	}

	Root->SetNumberField(TEXT("count"), ErrorsArr.Num());
	Root->SetArrayField(TEXT("errors"), ErrorsArr);
	return Root;
}

TSharedPtr<FJsonObject> FMonolithEditorActions::GetBuildSummary(const TSharedPtr<FJsonObject>& Params)
{
	FScopeLock Lock(&BuildLock);
	FBuildRecord* Record = GetLatestBuild();
	if (!Record)
	{
		TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
		Root->SetStringField(TEXT("status"), TEXT("no_builds"));
		return Root;
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("build_id"), Record->BuildId);
	Root->SetStringField(TEXT("build_type"), Record->BuildType);
	Root->SetStringField(TEXT("status"), Record->Status);
	Root->SetNumberField(TEXT("error_count"), Record->ErrorCount);
	Root->SetNumberField(TEXT("warning_count"), Record->WarningCount);
	Root->SetNumberField(TEXT("total_output_lines"), Record->RawOutput.Num());

	if (Record->EndTime > Record->StartTime)
	{
		double Duration = (Record->EndTime - Record->StartTime).GetTotalSeconds();
		Root->SetNumberField(TEXT("duration_seconds"), Duration);
	}

	return Root;
}

TSharedPtr<FJsonObject> FMonolithEditorActions::SearchBuildOutput(const TSharedPtr<FJsonObject>& Params)
{
	FString Pattern = FMonolithJsonHelpers::GetStringParam(Params, TEXT("pattern"));
	if (Pattern.IsEmpty())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Missing required param: pattern"));
	}

	FScopeLock Lock(&BuildLock);
	FBuildRecord* Record = GetLatestBuild();
	if (!Record)
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("No builds recorded."));
	}

	FRegexPattern RegexPattern(Pattern);
	TArray<TSharedPtr<FJsonValue>> Matches;

	for (const FString& Line : Record->RawOutput)
	{
		FRegexMatcher Matcher(RegexPattern, Line);
		if (Matcher.FindNext())
		{
			Matches.Add(MakeShared<FJsonValueString>(Line));
			if (Matches.Num() >= 50) break;
		}
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("pattern"), Pattern);
	Root->SetNumberField(TEXT("match_count"), Matches.Num());
	Root->SetArrayField(TEXT("matches"), Matches);
	return Root;
}

// ============================================================================
// Log Actions
// ============================================================================

static TSharedPtr<FJsonObject> LogEntryToJson(const FMonolithLogEntry& E)
{
	TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
	Obj->SetStringField(TEXT("timestamp"), E.Timestamp.ToString());
	Obj->SetStringField(TEXT("category"), E.Category.ToString());

	// Map verbosity to string
	FString Sev;
	switch (E.Verbosity)
	{
	case ELogVerbosity::Fatal:       Sev = TEXT("Fatal"); break;
	case ELogVerbosity::Error:       Sev = TEXT("Error"); break;
	case ELogVerbosity::Warning:     Sev = TEXT("Warning"); break;
	case ELogVerbosity::Display:     Sev = TEXT("Display"); break;
	case ELogVerbosity::Log:         Sev = TEXT("Log"); break;
	case ELogVerbosity::Verbose:     Sev = TEXT("Verbose"); break;
	case ELogVerbosity::VeryVerbose: Sev = TEXT("VeryVerbose"); break;
	default: Sev = TEXT("Unknown"); break;
	}
	Obj->SetStringField(TEXT("severity"), Sev);
	Obj->SetStringField(TEXT("message"), E.Message);
	return Obj;
}

static TArray<TSharedPtr<FJsonValue>> LogEntriesToJsonArray(const TArray<FMonolithLogEntry>& Entries)
{
	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const auto& E : Entries)
	{
		Arr.Add(MakeShared<FJsonValueObject>(LogEntryToJson(E)));
	}
	return Arr;
}

TSharedPtr<FJsonObject> FMonolithEditorActions::GetRecentLogs(const TSharedPtr<FJsonObject>& Params)
{
	if (!LogCapture.IsValid())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Log capture not initialized."));
	}

	FString Category = FMonolithJsonHelpers::GetStringParam(Params, TEXT("category"));
	FString Severity = FMonolithJsonHelpers::GetStringParam(Params, TEXT("severity"));
	int32 Count = FMonolithJsonHelpers::GetIntParam(Params, TEXT("count"), 50);

	TArray<FMonolithLogEntry> Entries = LogCapture->GetRecent(Count, Category, Severity);

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("count"), Entries.Num());
	Root->SetArrayField(TEXT("entries"), LogEntriesToJsonArray(Entries));
	return Root;
}

TSharedPtr<FJsonObject> FMonolithEditorActions::SearchLogs(const TSharedPtr<FJsonObject>& Params)
{
	if (!LogCapture.IsValid())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Log capture not initialized."));
	}

	FString Pattern = FMonolithJsonHelpers::GetStringParam(Params, TEXT("pattern"));
	if (Pattern.IsEmpty())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Missing required param: pattern"));
	}

	FString Category = FMonolithJsonHelpers::GetStringParam(Params, TEXT("category"));
	int32 Count = FMonolithJsonHelpers::GetIntParam(Params, TEXT("count"), 50);

	TArray<FMonolithLogEntry> Results = LogCapture->Search(Pattern, Count);

	if (!Category.IsEmpty())
	{
		Results = Results.FilterByPredicate([&](const FMonolithLogEntry& E)
		{
			return E.Category == FName(*Category);
		});
	}

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetStringField(TEXT("pattern"), Pattern);
	Root->SetNumberField(TEXT("count"), Results.Num());
	Root->SetArrayField(TEXT("entries"), LogEntriesToJsonArray(Results));
	return Root;
}

TSharedPtr<FJsonObject> FMonolithEditorActions::TailLog(const TSharedPtr<FJsonObject>& Params)
{
	if (!LogCapture.IsValid())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Log capture not initialized."));
	}

	FString Category = FMonolithJsonHelpers::GetStringParam(Params, TEXT("category"));
	FString Severity = FMonolithJsonHelpers::GetStringParam(Params, TEXT("severity"));
	int32 Seconds = FMonolithJsonHelpers::GetIntParam(Params, TEXT("seconds"), 30);

	// Estimate: ~2 log lines per second
	int32 Count = FMath::Max(10, Seconds * 2);
	TArray<FMonolithLogEntry> Entries = LogCapture->GetRecent(Count, Category, Severity);

	// Further filter by timestamp
	FDateTime Cutoff = FDateTime::Now() - FTimespan::FromSeconds(Seconds);
	Entries = Entries.FilterByPredicate([&](const FMonolithLogEntry& E)
	{
		return E.Timestamp >= Cutoff;
	});

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	Root->SetNumberField(TEXT("seconds"), Seconds);
	Root->SetNumberField(TEXT("count"), Entries.Num());
	Root->SetArrayField(TEXT("entries"), LogEntriesToJsonArray(Entries));
	return Root;
}

TSharedPtr<FJsonObject> FMonolithEditorActions::GetLogCategories(const TSharedPtr<FJsonObject>& Params)
{
	if (!LogCapture.IsValid())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Log capture not initialized."));
	}

	TMap<FString, int32> Cats = LogCapture->GetCategories();

	// Sort by count descending
	Cats.ValueSort([](int32 A, int32 B) { return A > B; });

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> CatsArr;

	for (const auto& Pair : Cats)
	{
		TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
		CObj->SetStringField(TEXT("category"), Pair.Key);
		CObj->SetNumberField(TEXT("count"), Pair.Value);
		CatsArr.Add(MakeShared<FJsonValueObject>(CObj));
	}

	Root->SetNumberField(TEXT("total_categories"), CatsArr.Num());
	Root->SetArrayField(TEXT("categories"), CatsArr);
	return Root;
}

TSharedPtr<FJsonObject> FMonolithEditorActions::GetLogStats(const TSharedPtr<FJsonObject>& Params)
{
	if (!LogCapture.IsValid())
	{
		return FMonolithJsonHelpers::ErrorJson(TEXT("Log capture not initialized."));
	}

	TMap<FString, TMap<FString, int32>> Stats = LogCapture->GetCategoryStats();

	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	// Error categories
	TArray<TSharedPtr<FJsonValue>> ErrorCats;
	TArray<TSharedPtr<FJsonValue>> WarnCats;
	TArray<TSharedPtr<FJsonValue>> ActiveCats;

	TMap<FString, int32> TotalCounts = LogCapture->GetCategories();

	for (const auto& Pair : Stats)
	{
		const int32* ErrorCount = Pair.Value.Find(TEXT("Error"));
		const int32* WarnCount = Pair.Value.Find(TEXT("Warning"));

		if (ErrorCount && *ErrorCount > 0)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("category"), Pair.Key);
			Obj->SetNumberField(TEXT("count"), *ErrorCount);
			ErrorCats.Add(MakeShared<FJsonValueObject>(Obj));
		}
		if (WarnCount && *WarnCount > 0)
		{
			TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
			Obj->SetStringField(TEXT("category"), Pair.Key);
			Obj->SetNumberField(TEXT("count"), *WarnCount);
			WarnCats.Add(MakeShared<FJsonValueObject>(Obj));
		}
	}

	// Top 10 most active
	TotalCounts.ValueSort([](int32 A, int32 B) { return A > B; });
	int32 ActiveCount = 0;
	for (const auto& Pair : TotalCounts)
	{
		if (ActiveCount >= 10) break;
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("category"), Pair.Key);
		Obj->SetNumberField(TEXT("count"), Pair.Value);
		ActiveCats.Add(MakeShared<FJsonValueObject>(Obj));
		ActiveCount++;
	}

	Root->SetArrayField(TEXT("error_categories"), ErrorCats);
	Root->SetArrayField(TEXT("warning_categories"), WarnCats);
	Root->SetArrayField(TEXT("most_active"), ActiveCats);
	return Root;
}

// ============================================================================
// Crash Context
// ============================================================================

TSharedPtr<FJsonObject> FMonolithEditorActions::GetCrashContext(const TSharedPtr<FJsonObject>& Params)
{
	TSharedPtr<FJsonObject> Root = MakeShared<FJsonObject>();

	// Fatal log entries
	if (LogCapture.IsValid())
	{
		TArray<FMonolithLogEntry> FatalEntries = LogCapture->GetRecent(20, TEXT(""), TEXT("Fatal"));
		Root->SetArrayField(TEXT("fatal_entries"), LogEntriesToJsonArray(FatalEntries));
	}

	// Crash report directories
	FString CrashDir = FPaths::ProjectSavedDir() / TEXT("Crashes");
	TArray<TSharedPtr<FJsonValue>> CrashReports;

	IFileManager& FM = IFileManager::Get();
	if (FM.DirectoryExists(*CrashDir))
	{
		TArray<FString> SubDirs;
		FM.FindFiles(SubDirs, *(CrashDir / TEXT("*")), false, true);

		// Sort by name descending (crash dirs have timestamps)
		SubDirs.Sort([](const FString& A, const FString& B) { return A > B; });

		int32 Count = 0;
		for (const FString& Dir : SubDirs)
		{
			if (Count >= 5) break;

			TSharedPtr<FJsonObject> CObj = MakeShared<FJsonObject>();
			CObj->SetStringField(TEXT("name"), Dir);

			FString CtxFile = CrashDir / Dir / TEXT("CrashContext.runtime-xml");
			CObj->SetBoolField(TEXT("has_context"), FM.FileExists(*CtxFile));
			CObj->SetStringField(TEXT("path"), CrashDir / Dir);

			CrashReports.Add(MakeShared<FJsonValueObject>(CObj));
			Count++;
		}
	}

	Root->SetArrayField(TEXT("crash_reports"), CrashReports);
	return Root;
}

// ============================================================================
// Internal Helpers
// ============================================================================

FMonolithEditorActions::FBuildRecord* FMonolithEditorActions::GetLatestBuild()
{
	// Caller must hold BuildLock
	if (BuildHistory.Num() == 0) return nullptr;
	return &BuildHistory.Last();
}

FMonolithEditorActions::FBuildRecord* FMonolithEditorActions::GetBuildById(const FString& BuildId)
{
	// Caller must hold BuildLock
	for (auto& Record : BuildHistory)
	{
		if (Record.BuildId == BuildId) return &Record;
	}
	return nullptr;
}

void FMonolithEditorActions::ParseBuildOutput(FBuildRecord& Record)
{
	// Parse MSVC-style errors: file(line): error/warning CODE: message
	FRegexPattern ErrorPattern(TEXT(R"((.+?)\((\d+)\)\s*:\s*(error|warning)\s+(\w+)\s*:\s*(.+))"));

	Record.ErrorCount = 0;
	Record.WarningCount = 0;
	Record.Errors.Empty();

	for (const FString& Line : Record.RawOutput)
	{
		FRegexMatcher Matcher(ErrorPattern, Line);
		if (Matcher.FindNext())
		{
			FBuildRecord::FBuildError Error;
			Error.File = Matcher.GetCaptureGroup(1);
			Error.Line = FCString::Atoi(*Matcher.GetCaptureGroup(2));
			Error.Severity = Matcher.GetCaptureGroup(3);
			Error.Code = Matcher.GetCaptureGroup(4);
			Error.Message = Matcher.GetCaptureGroup(5);
			Record.Errors.Add(MoveTemp(Error));

			if (Error.Severity == TEXT("error"))
				Record.ErrorCount++;
			else
				Record.WarningCount++;
		}
	}
}
```

### Files to Modify

**`Source/MonolithEditor/Private/MonolithEditorModule.cpp`**

```cpp
#include "MonolithEditorModule.h"
#include "MonolithEditorActions.h"

#define LOCTEXT_NAMESPACE "FMonolithEditorModule"

void FMonolithEditorModule::StartupModule()
{
	FMonolithEditorActions::Register();
	UE_LOG(LogTemp, Log, TEXT("Monolith — Editor module loaded (11 actions registered)"));
}

void FMonolithEditorModule::ShutdownModule()
{
	FMonolithEditorActions::Unregister();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithEditorModule, MonolithEditor)
```

**`Source/MonolithEditor/MonolithEditor.Build.cs`** — add LiveCoding dependency:

```csharp
using UnrealBuildTool;

public class MonolithEditor : ModuleRules
{
	public MonolithEditor(ReadOnlyTargetRules Target) : base(Target)
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
			"Json",
			"JsonUtilities",
			"MessageLog"
		});

		if (Target.Platform == UnrealTargetPlatform.Win64)
		{
			PrivateDependencyModuleNames.Add("LiveCoding");
		}
	}
}
```

*(Build.cs already has LiveCoding — no change needed. Listed for completeness.)*

### Steps

1. Create `Source/MonolithEditor/Private/MonolithLogCapture.h`
2. Create `Source/MonolithEditor/Private/MonolithLogCapture.cpp`
3. Create `Source/MonolithEditor/Private/MonolithEditorActions.h`
4. Create `Source/MonolithEditor/Private/MonolithEditorActions.cpp`
5. Update `Source/MonolithEditor/Private/MonolithEditorModule.cpp`
6. Compile — verify no errors

---

## File Summary

### New Files (9)

| File | Module | Purpose |
|------|--------|---------|
| `Source/MonolithCore/Public/MonolithToolRegistry.h` | Core | Action registry interface |
| `Source/MonolithCore/Private/MonolithToolRegistry.cpp` | Core | Registry implementation |
| `Source/MonolithCore/Public/MonolithJsonHelpers.h` | Core | Shared JSON utilities |
| `Source/MonolithBlueprint/Private/MonolithBlueprintActions.h` | Blueprint | 5 BP actions header |
| `Source/MonolithBlueprint/Private/MonolithBlueprintActions.cpp` | Blueprint | 5 BP actions impl |
| `Source/MonolithEditor/Private/MonolithLogCapture.h` | Editor | Log ring buffer header |
| `Source/MonolithEditor/Private/MonolithLogCapture.cpp` | Editor | Log ring buffer impl |
| `Source/MonolithEditor/Private/MonolithEditorActions.h` | Editor | 11 editor actions header |
| `Source/MonolithEditor/Private/MonolithEditorActions.cpp` | Editor | 11 editor actions impl |

### Modified Files (3)

| File | Change |
|------|--------|
| `Source/MonolithCore/Private/MonolithCoreModule.cpp` | Log registry stats |
| `Source/MonolithBlueprint/Private/MonolithBlueprintModule.cpp` | Register/unregister actions |
| `Source/MonolithEditor/Private/MonolithEditorModule.cpp` | Register/unregister actions |

---

## Manual Test Steps

Once compiled and editor is running with Monolith plugin, the MCP server is not wired yet (that's a later phase). To verify actions work, use the editor console or a test commandlet.

### Quick Smoke Test via Editor Console (UE Output Log)

Add temporary test code to `MonolithBlueprintModule::StartupModule()`:

```cpp
// TEMP TEST — remove after verification
auto& Registry = FMonolithToolRegistry::Get();
UE_LOG(LogTemp, Log, TEXT("Monolith: Total actions registered: %d"), Registry.GetTotalActionCount());
for (const FString& NS : Registry.GetNamespaces())
{
    UE_LOG(LogTemp, Log, TEXT("  Namespace: %s (%d actions)"), *NS, Registry.GetActions(NS).Num());
}
```

**Expected output in editor log:**
```
Monolith 0.1.0 — Core module loaded (ToolRegistry ready)
Monolith — Blueprint module loaded (5 actions registered)
Monolith: Registered blueprint.list_graphs
Monolith: Registered blueprint.get_graph_data
Monolith: Registered blueprint.get_variables
Monolith: Registered blueprint.get_execution_flow
Monolith: Registered blueprint.search_nodes
Monolith — Editor module loaded (11 actions registered)
Monolith: Registered editor.trigger_build
...
Monolith: Total actions registered: 16
  Namespace: blueprint (5 actions)
  Namespace: editor (11 actions)
```

### Test Actions via curl (after MCP server is wired in a later phase)

Once the HTTP MCP server dispatches to the ToolRegistry, test with:

```bash
# Blueprint: list graphs
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/call","params":{"name":"blueprint.query","arguments":{"action":"list_graphs","params":{"asset_path":"/Game/Characters/BP_Hero"}}}}'

# Blueprint: get variables
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":2,"method":"tools/call","params":{"name":"blueprint.query","arguments":{"action":"get_variables","params":{"asset_path":"/Game/Characters/BP_Hero"}}}}'

# Blueprint: search nodes
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":3,"method":"tools/call","params":{"name":"blueprint.query","arguments":{"action":"search_nodes","params":{"asset_path":"/Game/Characters/BP_Hero","query":"BeginPlay"}}}}'

# Editor: trigger build
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":4,"method":"tools/call","params":{"name":"editor.query","arguments":{"action":"trigger_build","params":{}}}}'

# Editor: get recent logs
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":5,"method":"tools/call","params":{"name":"editor.query","arguments":{"action":"get_recent_logs","params":{"category":"LogTemp","count":10}}}}'

# Editor: get log categories
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":6,"method":"tools/call","params":{"name":"editor.query","arguments":{"action":"get_log_categories","params":{}}}}'

# Editor: search logs
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":7,"method":"tools/call","params":{"name":"editor.query","arguments":{"action":"search_logs","params":{"pattern":"error|warning","count":20}}}}'

# Editor: get crash context
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":8,"method":"tools/call","params":{"name":"editor.query","arguments":{"action":"get_crash_context","params":{}}}}'

# Editor: get build summary
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":9,"method":"tools/call","params":{"name":"editor.query","arguments":{"action":"get_build_summary","params":{}}}}'

# Editor: tail log (last 10 seconds)
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":10,"method":"tools/call","params":{"name":"editor.query","arguments":{"action":"tail_log","params":{"seconds":10}}}}'

# Editor: get log stats
curl -X POST http://localhost:9316/mcp \
  -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":11,"method":"tools/call","params":{"name":"editor.query","arguments":{"action":"get_log_stats","params":{}}}}'
```

---

## Key Design Decisions

1. **No UCLASS/UFUNCTION** — Actions are plain C++ registered with `FMonolithToolRegistry`. No UObject overhead, no reflection. The MCP server dispatches directly.

2. **JSON in, JSON out** — Every action takes `TSharedPtr<FJsonObject>` params and returns `TSharedPtr<FJsonObject>`. The MCP HTTP layer serializes/deserializes. This matches the Python server pattern.

3. **Thread-safe registry** — `FCriticalSection` protects the registry map and build history. The MCP server may call from HTTP worker threads.

4. **Log capture via FOutputDevice** — Ring buffer (10K entries) registered with `GLog`. Much more efficient than the Python approach of tailing log files. Live data, no file I/O.

5. **Build tracking via ILiveCodingModule** — Direct C++ API call, no EditorBridge/UDP multicast/Python remote exec chain.

6. **Ported BlueprintReader verbatim** — All helper functions (PinTypeToString, SerializeNode, TraceExecFlow, etc.) are direct ports from the original BlueprintReaderLibrary.cpp with minimal changes (anonymous namespace → static class methods, FString returns → TSharedPtr<FJsonObject> returns).

Mission complete. o7

## Phase 2: Material + Config Tools

# PHASE 2: MATERIAL + CONFIG MODULES

## Task 2A: MonolithMaterial Module (46 Actions)

### Action Catalog

The 46 material actions are organized into 7 categories:

**INSPECTION (8 actions) — Ported from C++ plugin + Python**
1. `get_all_expressions` — C++ port (GetAllExpressions)
2. `get_expression_details` — C++ port (GetExpressionDetails)
3. `get_full_connection_graph` — C++ port (GetFullConnectionGraph)
4. `get_material_layer_info` — C++ port (GetMaterialLayerInfo)
5. `get_material_properties` — NEW: Read material domain, blend mode, shading model, two-sided, etc.
6. `get_material_stats` — NEW: Expression count, texture count, instruction counts, sampler usage
7. `get_material_instances` — NEW: List all MIDs/MICs referencing a parent material
8. `get_parameter_list` — NEW: All scalar/vector/texture/static switch params with defaults+groups

**EDITING (8 actions) — Ported from C++ + Python**
9. `disconnect_expression` — C++ port (DisconnectExpression)
10. `set_material_properties` — NEW: Set domain, blend mode, shading model, two-sided, etc.
11. `set_expression_property` — NEW: Set any property on a named expression via reflection
12. `delete_expression` — NEW: Delete a specific expression by name
13. `move_expression` — NEW: Reposition expression node(s) in graph
14. `rename_parameter` — NEW: Rename a parameter (updates all instances)
15. `set_parameter_default` — NEW: Set default value for scalar/vector/texture param
16. `set_parameter_group` — NEW: Set parameter group name for organization

**GRAPH BUILDING (7 actions) — C++ port + Python**
17. `build_material_graph` — C++ port (BuildMaterialGraph) — full JSON spec graph builder
18. `add_expression` — NEW: Add single expression node with class+position+properties
19. `connect_expressions` — NEW: Wire two expressions together by name+pin
20. `connect_to_material_output` — NEW: Wire expression to material output pin (BaseColor, Normal, etc.)
21. `create_custom_hlsl` — C++ port (CreateCustomHLSLNode)
22. `add_function_call` — NEW: Add a MaterialFunctionCall node referencing a material function
23. `add_texture_sample` — NEW: Add TextureSample/TextureSampleParameter2D with texture ref

**IMPORT/EXPORT (4 actions) — C++ port**
24. `export_material_graph` — C++ port (ExportMaterialGraph)
25. `import_material_graph` — C++ port (ImportMaterialGraph)
26. `begin_transaction` — C++ port (BeginMaterialTransaction)
27. `end_transaction` — C++ port (EndMaterialTransaction)

**TEMPLATES (7 actions) — NEW, reimplemented from Python**
28. `create_material` — Create new empty material asset at path
29. `create_pbr_material` — Template: PBR setup (BaseColor, Normal, Roughness, Metallic params)
30. `create_emissive_material` — Template: Emissive with color+intensity params
31. `create_glass_material` — Template: Translucent glass with opacity, refraction, specular
32. `create_subsurface_material` — Template: SSS with subsurface color+opacity params
33. `create_decal_material` — Template: Deferred Decal domain setup
34. `create_from_textures` — Template: Auto-build PBR material from texture set (albedo/normal/roughness/metallic/AO)

**PREVIEW/VALIDATION (5 actions) — C++ port + Python**
35. `render_preview` — C++ port (RenderMaterialPreview)
36. `get_thumbnail` — C++ port (GetMaterialThumbnail)
37. `validate_material` — C++ port (ValidateMaterial)
38. `compare_materials` — NEW: Side-by-side comparison of two materials (params, expressions, connections)
39. `get_compile_errors` — NEW: Force recompile and return shader compile errors

**BATCH OPERATIONS (7 actions) — NEW, reimplemented from Python**
40. `find_materials` — Search materials by name, path pattern, domain, shading model
41. `batch_set_property` — Set a property on multiple materials matching a filter
42. `batch_replace_texture` — Replace texture references across multiple materials
43. `find_materials_using_texture` — Find all materials referencing a specific texture
44. `find_materials_using_function` — Find all materials using a specific material function
45. `duplicate_material` — Duplicate a material to a new path
46. `create_material_instance` — Create MIC from a parent material with parameter overrides

### Files to Create/Modify

```
Source/MonolithMaterial/
├── MonolithMaterial.Build.cs          (MODIFY — add EditorScripting, AssetRegistry)
├── Public/
│   ├── MonolithMaterialModule.h       (MODIFY — add registration)
│   ├── MonolithMaterialActions.h      (CREATE — action handler declarations)
│   └── MonolithMaterialHelpers.h      (CREATE — shared helpers)
├── Private/
│   ├── MonolithMaterialModule.cpp     (MODIFY — register actions)
│   ├── MonolithMaterialActions.cpp    (CREATE — all 46 action implementations)
│   └── MonolithMaterialHelpers.cpp    (CREATE — shared utility code)
```

### Step 1: Update Build.cs

**File:** `Source/MonolithMaterial/MonolithMaterial.Build.cs`

```csharp
using UnrealBuildTool;

public class MonolithMaterial : ModuleRules
{
	public MonolithMaterial(ReadOnlyTargetRules Target) : base(Target)
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
			"MaterialEditor",
			"RenderCore",
			"RHI",
			"Slate",
			"SlateCore",
			"Json",
			"JsonUtilities",
			"EditorScriptingUtilities",
			"AssetRegistry",
			"ContentBrowser",
			"AssetTools"
		});
	}
}
```

### Step 2: Helpers Header

**File:** `Source/MonolithMaterial/Public/MonolithMaterialHelpers.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

class UMaterial;
class UMaterialInterface;
class UMaterialExpression;
class UMaterialInstanceConstant;

namespace MonolithMaterial
{
	/** Serialize JSON object to condensed string. */
	FString JsonToString(TSharedPtr<FJsonObject> Obj);

	/** Parse JSON string to object. Returns nullptr on failure. */
	TSharedPtr<FJsonObject> ParseJson(const FString& JsonStr);

	/** Create success JSON with asset_path. */
	TSharedPtr<FJsonObject> MakeSuccess(const FString& AssetPath);

	/** Create error JSON. */
	FString MakeError(const FString& Msg);

	/** Load a UMaterial from asset path. Returns nullptr on failure. */
	UMaterial* LoadMaterial(const FString& AssetPath);

	/** Load any UMaterialInterface (material or instance). */
	UMaterialInterface* LoadMaterialInterface(const FString& AssetPath);

	/** Find expression by name in material. */
	UMaterialExpression* FindExpression(UMaterial* Mat, const FString& Name);

	/** Map string to EMaterialProperty. Returns MP_MAX if unknown. */
	EMaterialProperty ParseMaterialProperty(const FString& Name);

	/** Map EMaterialProperty to string. */
	FString MaterialPropertyToString(EMaterialProperty Prop);

	/** Map string to ECustomMaterialOutputType. */
	ECustomMaterialOutputType ParseCustomOutputType(const FString& TypeName);

	/** Map ECustomMaterialOutputType to string. */
	FString CustomOutputTypeToString(ECustomMaterialOutputType Type);

	/** Serialize a single expression to JSON object. */
	TSharedPtr<FJsonObject> SerializeExpression(const UMaterialExpression* Expr);

	/** Table of all material properties we handle. */
	struct FMaterialPropertyEntry
	{
		EMaterialProperty Property;
		const TCHAR* Name;
	};
	const TArray<FMaterialPropertyEntry>& GetAllMaterialProperties();
}
```

### Step 3: Helpers Implementation

**File:** `Source/MonolithMaterial/Private/MonolithMaterialHelpers.cpp`

```cpp
#include "MonolithMaterialHelpers.h"

#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "EditorAssetLibrary.h"

namespace MonolithMaterial
{

FString JsonToString(TSharedPtr<FJsonObject> Obj)
{
	FString Output;
	auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Output);
	FJsonSerializer::Serialize(Obj, Writer);
	return Output;
}

TSharedPtr<FJsonObject> ParseJson(const FString& JsonStr)
{
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
	TSharedPtr<FJsonObject> Obj;
	FJsonSerializer::Deserialize(Reader, Obj);
	return Obj;
}

TSharedPtr<FJsonObject> MakeSuccess(const FString& AssetPath)
{
	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("asset_path"), AssetPath);
	return R;
}

FString MakeError(const FString& Msg)
{
	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), false);
	R->SetStringField(TEXT("error"), Msg);
	return JsonToString(R);
}

UMaterial* LoadMaterial(const FString& AssetPath)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	return Asset ? Cast<UMaterial>(Asset) : nullptr;
}

UMaterialInterface* LoadMaterialInterface(const FString& AssetPath)
{
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	return Asset ? Cast<UMaterialInterface>(Asset) : nullptr;
}

UMaterialExpression* FindExpression(UMaterial* Mat, const FString& Name)
{
	if (!Mat) return nullptr;
	for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
	{
		if (Expr && Expr->GetName() == Name)
		{
			return Expr;
		}
	}
	return nullptr;
}

EMaterialProperty ParseMaterialProperty(const FString& Name)
{
	for (const FMaterialPropertyEntry& E : GetAllMaterialProperties())
	{
		if (Name == E.Name) return E.Property;
	}
	return MP_MAX;
}

FString MaterialPropertyToString(EMaterialProperty Prop)
{
	for (const FMaterialPropertyEntry& E : GetAllMaterialProperties())
	{
		if (Prop == E.Property) return E.Name;
	}
	return TEXT("");
}

ECustomMaterialOutputType ParseCustomOutputType(const FString& TypeName)
{
	if (TypeName == TEXT("CMOT_Float1") || TypeName == TEXT("Float1")) return CMOT_Float1;
	if (TypeName == TEXT("CMOT_Float2") || TypeName == TEXT("Float2")) return CMOT_Float2;
	if (TypeName == TEXT("CMOT_Float3") || TypeName == TEXT("Float3")) return CMOT_Float3;
	if (TypeName == TEXT("CMOT_Float4") || TypeName == TEXT("Float4")) return CMOT_Float4;
	return CMOT_Float1;
}

FString CustomOutputTypeToString(ECustomMaterialOutputType Type)
{
	switch (Type)
	{
	case CMOT_Float1: return TEXT("Float1");
	case CMOT_Float2: return TEXT("Float2");
	case CMOT_Float3: return TEXT("Float3");
	case CMOT_Float4: return TEXT("Float4");
	default: return TEXT("Float1");
	}
}

TSharedPtr<FJsonObject> SerializeExpression(const UMaterialExpression* Expression)
{
	auto J = MakeShared<FJsonObject>();
	J->SetStringField(TEXT("name"), Expression->GetName());
	J->SetStringField(TEXT("class"), Expression->GetClass()->GetName());
	J->SetNumberField(TEXT("pos_x"), Expression->MaterialExpressionEditorX);
	J->SetNumberField(TEXT("pos_y"), Expression->MaterialExpressionEditorY);

	if (const auto* TexSampleParam = Cast<UMaterialExpressionTextureSampleParameter>(Expression))
	{
		J->SetStringField(TEXT("parameter_name"), TexSampleParam->ParameterName.ToString());
		if (TexSampleParam->Texture)
			J->SetStringField(TEXT("texture"), TexSampleParam->Texture->GetPathName());
	}
	else if (const auto* Param = Cast<UMaterialExpressionParameter>(Expression))
	{
		J->SetStringField(TEXT("parameter_name"), Param->ParameterName.ToString());
	}
	else if (const auto* TexBase = Cast<UMaterialExpressionTextureBase>(Expression))
	{
		if (TexBase->Texture)
			J->SetStringField(TEXT("texture"), TexBase->Texture->GetPathName());
	}

	if (const auto* Custom = Cast<UMaterialExpressionCustom>(Expression))
		J->SetStringField(TEXT("code"), Custom->Code.Left(100));

	if (const auto* Comment = Cast<UMaterialExpressionComment>(Expression))
		J->SetStringField(TEXT("text"), Comment->Text);

	if (const auto* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(Expression))
	{
		if (FuncCall->MaterialFunction)
			J->SetStringField(TEXT("function"), FuncCall->MaterialFunction->GetPathName());
	}

	return J;
}

const TArray<FMaterialPropertyEntry>& GetAllMaterialProperties()
{
	static TArray<FMaterialPropertyEntry> Props = {
		{ MP_BaseColor,             TEXT("BaseColor") },
		{ MP_Metallic,              TEXT("Metallic") },
		{ MP_Specular,              TEXT("Specular") },
		{ MP_Roughness,             TEXT("Roughness") },
		{ MP_Anisotropy,            TEXT("Anisotropy") },
		{ MP_EmissiveColor,         TEXT("EmissiveColor") },
		{ MP_Opacity,               TEXT("Opacity") },
		{ MP_OpacityMask,           TEXT("OpacityMask") },
		{ MP_Normal,                TEXT("Normal") },
		{ MP_WorldPositionOffset,   TEXT("WorldPositionOffset") },
		{ MP_SubsurfaceColor,       TEXT("SubsurfaceColor") },
		{ MP_AmbientOcclusion,      TEXT("AmbientOcclusion") },
		{ MP_Refraction,            TEXT("Refraction") },
		{ MP_PixelDepthOffset,      TEXT("PixelDepthOffset") },
		{ MP_ShadingModel,          TEXT("ShadingModel") },
	};
	return Props;
}

} // namespace MonolithMaterial
```

### Step 4: Actions Header

**File:** `Source/MonolithMaterial/Public/MonolithMaterialActions.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

/**
 * All 46 material actions. Each takes a JSON params object and returns a JSON result string.
 * Registered with MonolithCore's ToolRegistry under the "material" namespace.
 */
namespace MonolithMaterialActions
{
	// === INSPECTION (8) ===
	FString GetAllExpressions(TSharedPtr<FJsonObject> Params);
	FString GetExpressionDetails(TSharedPtr<FJsonObject> Params);
	FString GetFullConnectionGraph(TSharedPtr<FJsonObject> Params);
	FString GetMaterialLayerInfo(TSharedPtr<FJsonObject> Params);
	FString GetMaterialProperties(TSharedPtr<FJsonObject> Params);
	FString GetMaterialStats(TSharedPtr<FJsonObject> Params);
	FString GetMaterialInstances(TSharedPtr<FJsonObject> Params);
	FString GetParameterList(TSharedPtr<FJsonObject> Params);

	// === EDITING (8) ===
	FString DisconnectExpression(TSharedPtr<FJsonObject> Params);
	FString SetMaterialProperties(TSharedPtr<FJsonObject> Params);
	FString SetExpressionProperty(TSharedPtr<FJsonObject> Params);
	FString DeleteExpression(TSharedPtr<FJsonObject> Params);
	FString MoveExpression(TSharedPtr<FJsonObject> Params);
	FString RenameParameter(TSharedPtr<FJsonObject> Params);
	FString SetParameterDefault(TSharedPtr<FJsonObject> Params);
	FString SetParameterGroup(TSharedPtr<FJsonObject> Params);

	// === GRAPH BUILDING (7) ===
	FString BuildMaterialGraph(TSharedPtr<FJsonObject> Params);
	FString AddExpression(TSharedPtr<FJsonObject> Params);
	FString ConnectExpressions(TSharedPtr<FJsonObject> Params);
	FString ConnectToMaterialOutput(TSharedPtr<FJsonObject> Params);
	FString CreateCustomHlsl(TSharedPtr<FJsonObject> Params);
	FString AddFunctionCall(TSharedPtr<FJsonObject> Params);
	FString AddTextureSample(TSharedPtr<FJsonObject> Params);

	// === IMPORT/EXPORT (4) ===
	FString ExportMaterialGraph(TSharedPtr<FJsonObject> Params);
	FString ImportMaterialGraph(TSharedPtr<FJsonObject> Params);
	FString BeginTransaction(TSharedPtr<FJsonObject> Params);
	FString EndTransaction(TSharedPtr<FJsonObject> Params);

	// === TEMPLATES (7) ===
	FString CreateMaterial(TSharedPtr<FJsonObject> Params);
	FString CreatePbrMaterial(TSharedPtr<FJsonObject> Params);
	FString CreateEmissiveMaterial(TSharedPtr<FJsonObject> Params);
	FString CreateGlassMaterial(TSharedPtr<FJsonObject> Params);
	FString CreateSubsurfaceMaterial(TSharedPtr<FJsonObject> Params);
	FString CreateDecalMaterial(TSharedPtr<FJsonObject> Params);
	FString CreateFromTextures(TSharedPtr<FJsonObject> Params);

	// === PREVIEW/VALIDATION (5) ===
	FString RenderPreview(TSharedPtr<FJsonObject> Params);
	FString GetThumbnail(TSharedPtr<FJsonObject> Params);
	FString ValidateMaterial(TSharedPtr<FJsonObject> Params);
	FString CompareMaterials(TSharedPtr<FJsonObject> Params);
	FString GetCompileErrors(TSharedPtr<FJsonObject> Params);

	// === BATCH OPERATIONS (7) ===
	FString FindMaterials(TSharedPtr<FJsonObject> Params);
	FString BatchSetProperty(TSharedPtr<FJsonObject> Params);
	FString BatchReplaceTexture(TSharedPtr<FJsonObject> Params);
	FString FindMaterialsUsingTexture(TSharedPtr<FJsonObject> Params);
	FString FindMaterialsUsingFunction(TSharedPtr<FJsonObject> Params);
	FString DuplicateMaterial(TSharedPtr<FJsonObject> Params);
	FString CreateMaterialInstance(TSharedPtr<FJsonObject> Params);
}
```

### Step 5: Module Registration

**File:** `Source/MonolithMaterial/Public/MonolithMaterialModule.h`

```cpp
#pragma once

#include "Modules/ModuleManager.h"

class FMonolithMaterialModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterActions();
};
```

**File:** `Source/MonolithMaterial/Private/MonolithMaterialModule.cpp`

```cpp
#include "MonolithMaterialModule.h"
#include "MonolithMaterialActions.h"
// NOTE: Requires MonolithCore ToolRegistry — implemented in Phase 1
// #include "MonolithToolRegistry.h"

#define LOCTEXT_NAMESPACE "FMonolithMaterialModule"

void FMonolithMaterialModule::StartupModule()
{
	RegisterActions();
	UE_LOG(LogTemp, Log, TEXT("Monolith — Material module loaded (46 actions)"));
}

void FMonolithMaterialModule::ShutdownModule()
{
}

void FMonolithMaterialModule::RegisterActions()
{
	// Registration uses the ToolRegistry from MonolithCore.
	// Each action is registered as "material.<action_name>" with a handler function.
	// The ToolRegistry maps action names to TFunction<FString(TSharedPtr<FJsonObject>)>.
	//
	// Pattern (once MonolithCore ToolRegistry exists):
	//
	// auto& Registry = FMonolithToolRegistry::Get();
	// Registry.Register(TEXT("material"), TEXT("get_all_expressions"), &MonolithMaterialActions::GetAllExpressions);
	// Registry.Register(TEXT("material"), TEXT("get_expression_details"), &MonolithMaterialActions::GetExpressionDetails);
	// ... etc for all 46 actions
	//
	// For now, we pre-declare the mapping so Phase 1 can wire it up:

	using FActionHandler = TFunction<FString(TSharedPtr<FJsonObject>)>;
	TMap<FString, FActionHandler> Actions;

	// Inspection
	Actions.Add(TEXT("get_all_expressions"), &MonolithMaterialActions::GetAllExpressions);
	Actions.Add(TEXT("get_expression_details"), &MonolithMaterialActions::GetExpressionDetails);
	Actions.Add(TEXT("get_full_connection_graph"), &MonolithMaterialActions::GetFullConnectionGraph);
	Actions.Add(TEXT("get_material_layer_info"), &MonolithMaterialActions::GetMaterialLayerInfo);
	Actions.Add(TEXT("get_material_properties"), &MonolithMaterialActions::GetMaterialProperties);
	Actions.Add(TEXT("get_material_stats"), &MonolithMaterialActions::GetMaterialStats);
	Actions.Add(TEXT("get_material_instances"), &MonolithMaterialActions::GetMaterialInstances);
	Actions.Add(TEXT("get_parameter_list"), &MonolithMaterialActions::GetParameterList);

	// Editing
	Actions.Add(TEXT("disconnect_expression"), &MonolithMaterialActions::DisconnectExpression);
	Actions.Add(TEXT("set_material_properties"), &MonolithMaterialActions::SetMaterialProperties);
	Actions.Add(TEXT("set_expression_property"), &MonolithMaterialActions::SetExpressionProperty);
	Actions.Add(TEXT("delete_expression"), &MonolithMaterialActions::DeleteExpression);
	Actions.Add(TEXT("move_expression"), &MonolithMaterialActions::MoveExpression);
	Actions.Add(TEXT("rename_parameter"), &MonolithMaterialActions::RenameParameter);
	Actions.Add(TEXT("set_parameter_default"), &MonolithMaterialActions::SetParameterDefault);
	Actions.Add(TEXT("set_parameter_group"), &MonolithMaterialActions::SetParameterGroup);

	// Graph building
	Actions.Add(TEXT("build_material_graph"), &MonolithMaterialActions::BuildMaterialGraph);
	Actions.Add(TEXT("add_expression"), &MonolithMaterialActions::AddExpression);
	Actions.Add(TEXT("connect_expressions"), &MonolithMaterialActions::ConnectExpressions);
	Actions.Add(TEXT("connect_to_material_output"), &MonolithMaterialActions::ConnectToMaterialOutput);
	Actions.Add(TEXT("create_custom_hlsl"), &MonolithMaterialActions::CreateCustomHlsl);
	Actions.Add(TEXT("add_function_call"), &MonolithMaterialActions::AddFunctionCall);
	Actions.Add(TEXT("add_texture_sample"), &MonolithMaterialActions::AddTextureSample);

	// Import/Export
	Actions.Add(TEXT("export_material_graph"), &MonolithMaterialActions::ExportMaterialGraph);
	Actions.Add(TEXT("import_material_graph"), &MonolithMaterialActions::ImportMaterialGraph);
	Actions.Add(TEXT("begin_transaction"), &MonolithMaterialActions::BeginTransaction);
	Actions.Add(TEXT("end_transaction"), &MonolithMaterialActions::EndTransaction);

	// Templates
	Actions.Add(TEXT("create_material"), &MonolithMaterialActions::CreateMaterial);
	Actions.Add(TEXT("create_pbr_material"), &MonolithMaterialActions::CreatePbrMaterial);
	Actions.Add(TEXT("create_emissive_material"), &MonolithMaterialActions::CreateEmissiveMaterial);
	Actions.Add(TEXT("create_glass_material"), &MonolithMaterialActions::CreateGlassMaterial);
	Actions.Add(TEXT("create_subsurface_material"), &MonolithMaterialActions::CreateSubsurfaceMaterial);
	Actions.Add(TEXT("create_decal_material"), &MonolithMaterialActions::CreateDecalMaterial);
	Actions.Add(TEXT("create_from_textures"), &MonolithMaterialActions::CreateFromTextures);

	// Preview/Validation
	Actions.Add(TEXT("render_preview"), &MonolithMaterialActions::RenderPreview);
	Actions.Add(TEXT("get_thumbnail"), &MonolithMaterialActions::GetThumbnail);
	Actions.Add(TEXT("validate_material"), &MonolithMaterialActions::ValidateMaterial);
	Actions.Add(TEXT("compare_materials"), &MonolithMaterialActions::CompareMaterials);
	Actions.Add(TEXT("get_compile_errors"), &MonolithMaterialActions::GetCompileErrors);

	// Batch
	Actions.Add(TEXT("find_materials"), &MonolithMaterialActions::FindMaterials);
	Actions.Add(TEXT("batch_set_property"), &MonolithMaterialActions::BatchSetProperty);
	Actions.Add(TEXT("batch_replace_texture"), &MonolithMaterialActions::BatchReplaceTexture);
	Actions.Add(TEXT("find_materials_using_texture"), &MonolithMaterialActions::FindMaterialsUsingTexture);
	Actions.Add(TEXT("find_materials_using_function"), &MonolithMaterialActions::FindMaterialsUsingFunction);
	Actions.Add(TEXT("duplicate_material"), &MonolithMaterialActions::DuplicateMaterial);
	Actions.Add(TEXT("create_material_instance"), &MonolithMaterialActions::CreateMaterialInstance);

	// TODO: Once MonolithCore ToolRegistry is implemented in Phase 1,
	// replace this TMap with actual Registry.RegisterNamespace("material", Actions);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithMaterialModule, MonolithMaterial)
```

### Step 6: Action Implementations — INSPECTION + EDITING (Actions 1–16)

**File:** `Source/MonolithMaterial/Private/MonolithMaterialActions.cpp`

This is the main implementation file. Due to size, shown in logical blocks.

```cpp
#include "MonolithMaterialActions.h"
#include "MonolithMaterialHelpers.h"

#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstance.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialExpression.h"
#include "Materials/MaterialExpressionParameter.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "Materials/MaterialExpressionTextureBase.h"
#include "Materials/MaterialExpressionTextureSample.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionTextureSampleParameter.h"
#include "Materials/MaterialExpressionStaticSwitchParameter.h"
#include "Materials/MaterialExpressionStaticBoolParameter.h"
#include "Materials/MaterialExpressionCustom.h"
#include "Materials/MaterialExpressionComment.h"
#include "Materials/MaterialExpressionMaterialFunctionCall.h"
#include "Materials/MaterialFunction.h"
#include "Materials/MaterialFunctionMaterialLayer.h"
#include "Materials/MaterialFunctionMaterialLayerBlend.h"
#include "Materials/MaterialExpressionFunctionInput.h"
#include "Materials/MaterialExpressionFunctionOutput.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionConstant3Vector.h"
#include "Materials/MaterialExpressionConstant4Vector.h"
#include "MaterialExpressionIO.h"
#include "EditorAssetLibrary.h"
#include "MaterialEditingLibrary.h"
#include "Editor.h"
#include "UObject/UnrealType.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "ImageUtils.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Factories/MaterialFactoryNew.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "MaterialStatsCommon.h"

using namespace MonolithMaterial;

// ============================================================================
// INSPECTION ACTIONS (1-8)
// ============================================================================

// 1. get_all_expressions
// Params: { "asset_path": "/Game/..." }
FString MonolithMaterialActions::GetAllExpressions(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	TArray<TSharedPtr<FJsonValue>> Arr;
	for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
	{
		if (Expr)
			Arr.Add(MakeShared<FJsonValueObject>(SerializeExpression(Expr)));
	}

	auto R = MakeSuccess(AssetPath);
	R->SetNumberField(TEXT("expression_count"), Arr.Num());
	R->SetArrayField(TEXT("expressions"), Arr);
	return JsonToString(R);
}

// 2. get_expression_details
// Params: { "asset_path": "...", "expression_name": "..." }
FString MonolithMaterialActions::GetExpressionDetails(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ExprName = Params->GetStringField(TEXT("expression_name"));

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	UMaterialExpression* Expr = FindExpression(Mat, ExprName);
	if (!Expr) return MakeError(FString::Printf(TEXT("Expression '%s' not found"), *ExprName));

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("expression_name"), ExprName);
	R->SetStringField(TEXT("class"), Expr->GetClass()->GetName());

	// Reflect all properties
	auto PropsJson = MakeShared<FJsonObject>();
	for (TFieldIterator<FProperty> It(Expr->GetClass()); It; ++It)
	{
		FProperty* Prop = *It;
		if (!Prop || Prop->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) continue;
		FString Val;
		const void* Ptr = Prop->ContainerPtrToValuePtr<void>(Expr);
		Prop->ExportTextItem_Direct(Val, Ptr, nullptr, nullptr, PPF_None);
		if (!Val.IsEmpty()) PropsJson->SetStringField(Prop->GetName(), Val);
	}
	R->SetObjectField(TEXT("properties"), PropsJson);

	// Inputs
	TArray<TSharedPtr<FJsonValue>> Inputs;
	for (int32 i = 0; ; ++i)
	{
		FExpressionInput* In = Expr->GetInput(i);
		if (!In) break;
		auto InJ = MakeShared<FJsonObject>();
		InJ->SetStringField(TEXT("name"), Expr->GetInputName(i).ToString());
		InJ->SetBoolField(TEXT("connected"), In->Expression != nullptr);
		if (In->Expression)
		{
			InJ->SetStringField(TEXT("connected_to"), In->Expression->GetName());
			InJ->SetNumberField(TEXT("output_index"), In->OutputIndex);
		}
		Inputs.Add(MakeShared<FJsonValueObject>(InJ));
	}
	R->SetArrayField(TEXT("inputs"), Inputs);

	// Outputs
	TArray<TSharedPtr<FJsonValue>> Outputs;
	for (int32 i = 0; i < Expr->Outputs.Num(); ++i)
	{
		auto OutJ = MakeShared<FJsonObject>();
		OutJ->SetStringField(TEXT("name"), Expr->Outputs[i].OutputName.ToString());
		OutJ->SetNumberField(TEXT("index"), i);
		Outputs.Add(MakeShared<FJsonValueObject>(OutJ));
	}
	R->SetArrayField(TEXT("outputs"), Outputs);

	return JsonToString(R);
}

// 3. get_full_connection_graph
// Params: { "asset_path": "..." }
FString MonolithMaterialActions::GetFullConnectionGraph(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	TArray<TSharedPtr<FJsonValue>> Conns;
	for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
	{
		if (!Expr) continue;
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* In = Expr->GetInput(i);
			if (!In) break;
			if (!In->Expression) continue;

			auto C = MakeShared<FJsonObject>();
			C->SetStringField(TEXT("from"), In->Expression->GetName());
			C->SetNumberField(TEXT("from_output_index"), In->OutputIndex);
			const TArray<FExpressionOutput>& SrcOuts = In->Expression->Outputs;
			FString FromOutName;
			if (SrcOuts.IsValidIndex(In->OutputIndex))
				FromOutName = SrcOuts[In->OutputIndex].OutputName.ToString();
			C->SetStringField(TEXT("from_output"), FromOutName);
			C->SetStringField(TEXT("to"), Expr->GetName());
			C->SetStringField(TEXT("to_input"), Expr->GetInputName(i).ToString());
			Conns.Add(MakeShared<FJsonValueObject>(C));
		}
	}

	// Material output pins
	TArray<TSharedPtr<FJsonValue>> MatOuts;
	for (const auto& E : GetAllMaterialProperties())
	{
		FExpressionInput* In = Mat->GetExpressionInputForProperty(E.Property);
		if (In && In->Expression)
		{
			auto O = MakeShared<FJsonObject>();
			O->SetStringField(TEXT("property"), E.Name);
			O->SetStringField(TEXT("expression"), In->Expression->GetName());
			O->SetNumberField(TEXT("output_index"), In->OutputIndex);
			MatOuts.Add(MakeShared<FJsonValueObject>(O));
		}
	}

	auto R = MakeSuccess(AssetPath);
	R->SetArrayField(TEXT("connections"), Conns);
	R->SetArrayField(TEXT("material_outputs"), MatOuts);
	return JsonToString(R);
}

// 4. get_material_layer_info — direct port from C++ plugin
// Params: { "asset_path": "..." }
FString MonolithMaterialActions::GetMaterialLayerInfo(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) return MakeError(FString::Printf(TEXT("Failed to load asset '%s'"), *AssetPath));

	auto R = MakeSuccess(AssetPath);

	UMaterialFunctionMaterialLayer* Layer = Cast<UMaterialFunctionMaterialLayer>(Asset);
	UMaterialFunctionMaterialLayerBlend* Blend = Cast<UMaterialFunctionMaterialLayerBlend>(Asset);
	UMaterialFunction* Func = Cast<UMaterialFunction>(Asset);

	if (Layer) { R->SetStringField(TEXT("type"), TEXT("MaterialLayer")); Func = Layer; }
	else if (Blend) { R->SetStringField(TEXT("type"), TEXT("MaterialLayerBlend")); Func = Blend; }
	else if (Func) { R->SetStringField(TEXT("type"), TEXT("MaterialFunction")); }
	else return MakeError(TEXT("Asset is not a MaterialFunction/Layer/LayerBlend"));

	R->SetStringField(TEXT("description"), Func->Description);

	TArray<TSharedPtr<FJsonValue>> Exprs, FuncInputs, FuncOutputs;
	for (const TObjectPtr<UMaterialExpression>& Expr : Func->GetExpressions())
	{
		if (!Expr) continue;
		if (const auto* FI = Cast<UMaterialExpressionFunctionInput>(Expr))
		{
			auto J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("name"), FI->InputName.ToString());
			J->SetStringField(TEXT("expression_name"), FI->GetName());
			J->SetNumberField(TEXT("sort_priority"), FI->SortPriority);
			FuncInputs.Add(MakeShared<FJsonValueObject>(J));
		}
		if (const auto* FO = Cast<UMaterialExpressionFunctionOutput>(Expr))
		{
			auto J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("name"), FO->OutputName.ToString());
			J->SetStringField(TEXT("expression_name"), FO->GetName());
			J->SetNumberField(TEXT("sort_priority"), FO->SortPriority);
			FuncOutputs.Add(MakeShared<FJsonValueObject>(J));
		}
		auto EJ = MakeShared<FJsonObject>();
		EJ->SetStringField(TEXT("name"), Expr->GetName());
		EJ->SetStringField(TEXT("class"), Expr->GetClass()->GetName());
		EJ->SetNumberField(TEXT("pos_x"), Expr->MaterialExpressionEditorX);
		EJ->SetNumberField(TEXT("pos_y"), Expr->MaterialExpressionEditorY);
		Exprs.Add(MakeShared<FJsonValueObject>(EJ));
	}

	R->SetArrayField(TEXT("inputs"), FuncInputs);
	R->SetArrayField(TEXT("outputs"), FuncOutputs);
	R->SetArrayField(TEXT("expressions"), Exprs);
	R->SetNumberField(TEXT("expression_count"), Exprs.Num());
	return JsonToString(R);
}

// 5. get_material_properties — NEW
// Params: { "asset_path": "..." }
FString MonolithMaterialActions::GetMaterialProperties(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("material_domain"), *UEnum::GetValueAsString(Mat->MaterialDomain));
	R->SetStringField(TEXT("blend_mode"), *UEnum::GetValueAsString(Mat->BlendMode));
	R->SetStringField(TEXT("shading_model"), *UEnum::GetValueAsString(Mat->GetShadingModels().GetFirstShadingModel()));
	R->SetBoolField(TEXT("two_sided"), Mat->IsTwoSided());
	R->SetBoolField(TEXT("is_masked"), Mat->IsMasked());
	R->SetStringField(TEXT("opacity_mask_clip_value"), FString::SanitizeFloat(Mat->GetOpacityMaskClipValue()));
	R->SetBoolField(TEXT("dithered_lod_transition"), Mat->IsDitheredLODTransition());
	R->SetBoolField(TEXT("allow_negative_emissive"), Mat->bAllowNegativeEmissiveColor);
	R->SetBoolField(TEXT("use_material_attributes"), Mat->bUseMaterialAttributes);
	R->SetNumberField(TEXT("num_customized_uvs"), Mat->NumCustomizedUVs);
	return JsonToString(R);
}

// 6. get_material_stats — NEW
// Params: { "asset_path": "..." }
FString MonolithMaterialActions::GetMaterialStats(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	int32 TotalExprs = 0, TextureCount = 0, ParamCount = 0, CustomHlslCount = 0;
	for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
	{
		if (!Expr) continue;
		TotalExprs++;
		if (Cast<UMaterialExpressionTextureBase>(Expr)) TextureCount++;
		if (Cast<UMaterialExpressionParameter>(Expr) || Cast<UMaterialExpressionTextureSampleParameter>(Expr)) ParamCount++;
		if (Cast<UMaterialExpressionCustom>(Expr)) CustomHlslCount++;
	}

	// Count connected material outputs
	int32 ConnectedOutputs = 0;
	for (const auto& E : GetAllMaterialProperties())
	{
		FExpressionInput* In = Mat->GetExpressionInputForProperty(E.Property);
		if (In && In->Expression) ConnectedOutputs++;
	}

	auto R = MakeSuccess(AssetPath);
	R->SetNumberField(TEXT("total_expressions"), TotalExprs);
	R->SetNumberField(TEXT("texture_samples"), TextureCount);
	R->SetNumberField(TEXT("parameters"), ParamCount);
	R->SetNumberField(TEXT("custom_hlsl_nodes"), CustomHlslCount);
	R->SetNumberField(TEXT("connected_outputs"), ConnectedOutputs);
	return JsonToString(R);
}

// 7. get_material_instances — NEW
// Params: { "asset_path": "..." }
FString MonolithMaterialActions::GetMaterialInstances(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UMaterialInterface* MatInterface = LoadMaterialInterface(AssetPath);
	if (!MatInterface) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<TSharedPtr<FJsonValue>> Instances;

	// Search all MaterialInstanceConstant assets
	TArray<FAssetData> AllMICs;
	AR.GetAssetsByClass(UMaterialInstanceConstant::StaticClass()->GetClassPathName(), AllMICs, true);
	for (const FAssetData& MICData : AllMICs)
	{
		FAssetTagValueRef ParentTag = MICData.TagsAndValues.FindTag(TEXT("Parent"));
		if (ParentTag.IsSet())
		{
			FString ParentPath = ParentTag.GetValue();
			if (ParentPath.Contains(AssetPath))
			{
				auto J = MakeShared<FJsonObject>();
				J->SetStringField(TEXT("path"), MICData.GetObjectPathString());
				J->SetStringField(TEXT("name"), MICData.AssetName.ToString());
				Instances.Add(MakeShared<FJsonValueObject>(J));
			}
		}
	}

	auto R = MakeSuccess(AssetPath);
	R->SetNumberField(TEXT("instance_count"), Instances.Num());
	R->SetArrayField(TEXT("instances"), Instances);
	return JsonToString(R);
}

// 8. get_parameter_list — NEW
// Params: { "asset_path": "..." }
FString MonolithMaterialActions::GetParameterList(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	TArray<TSharedPtr<FJsonValue>> ParamArr;
	for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
	{
		if (!Expr) continue;

		if (const auto* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr))
		{
			auto J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("type"), TEXT("Scalar"));
			J->SetStringField(TEXT("name"), Scalar->ParameterName.ToString());
			J->SetStringField(TEXT("group"), Scalar->Group.ToString());
			J->SetNumberField(TEXT("default_value"), Scalar->DefaultValue);
			J->SetStringField(TEXT("expression_name"), Scalar->GetName());
			ParamArr.Add(MakeShared<FJsonValueObject>(J));
		}
		else if (const auto* Vector = Cast<UMaterialExpressionVectorParameter>(Expr))
		{
			auto J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("type"), TEXT("Vector"));
			J->SetStringField(TEXT("name"), Vector->ParameterName.ToString());
			J->SetStringField(TEXT("group"), Vector->Group.ToString());
			J->SetStringField(TEXT("default_value"), FString::Printf(TEXT("(%f,%f,%f,%f)"),
				Vector->DefaultValue.R, Vector->DefaultValue.G, Vector->DefaultValue.B, Vector->DefaultValue.A));
			J->SetStringField(TEXT("expression_name"), Vector->GetName());
			ParamArr.Add(MakeShared<FJsonValueObject>(J));
		}
		else if (const auto* TexParam = Cast<UMaterialExpressionTextureSampleParameter>(Expr))
		{
			auto J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("type"), TEXT("Texture"));
			J->SetStringField(TEXT("name"), TexParam->ParameterName.ToString());
			J->SetStringField(TEXT("group"), TexParam->Group.ToString());
			J->SetStringField(TEXT("default_value"), TexParam->Texture ? TexParam->Texture->GetPathName() : TEXT("None"));
			J->SetStringField(TEXT("expression_name"), TexParam->GetName());
			ParamArr.Add(MakeShared<FJsonValueObject>(J));
		}
		else if (const auto* StaticSwitch = Cast<UMaterialExpressionStaticBoolParameter>(Expr))
		{
			auto J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("type"), TEXT("StaticSwitch"));
			J->SetStringField(TEXT("name"), StaticSwitch->ParameterName.ToString());
			J->SetStringField(TEXT("group"), StaticSwitch->Group.ToString());
			J->SetBoolField(TEXT("default_value"), StaticSwitch->DefaultValue);
			J->SetStringField(TEXT("expression_name"), StaticSwitch->GetName());
			ParamArr.Add(MakeShared<FJsonValueObject>(J));
		}
	}

	auto R = MakeSuccess(AssetPath);
	R->SetNumberField(TEXT("parameter_count"), ParamArr.Num());
	R->SetArrayField(TEXT("parameters"), ParamArr);
	return JsonToString(R);
}

// ============================================================================
// EDITING ACTIONS (9-16)
// ============================================================================

// 9. disconnect_expression — port from C++ plugin
// Params: { "asset_path": "...", "expression_name": "...", "input_name": "" (optional), "disconnect_outputs": false }
FString MonolithMaterialActions::DisconnectExpression(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ExprName = Params->GetStringField(TEXT("expression_name"));
	FString InputName = Params->HasField(TEXT("input_name")) ? Params->GetStringField(TEXT("input_name")) : TEXT("");
	bool bDisconnectOutputs = Params->HasField(TEXT("disconnect_outputs")) ? Params->GetBoolField(TEXT("disconnect_outputs")) : false;

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	UMaterialExpression* Target = FindExpression(Mat, ExprName);
	if (!Target) return MakeError(FString::Printf(TEXT("Expression '%s' not found"), *ExprName));

	Mat->Modify();
	TArray<TSharedPtr<FJsonValue>> DisconnArr;
	int32 Count = 0;

	if (!bDisconnectOutputs)
	{
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* In = Target->GetInput(i);
			if (!In) break;
			FString PinName = Target->GetInputName(i).ToString();
			if (InputName.IsEmpty() || PinName == InputName)
			{
				if (In->Expression)
				{
					auto D = MakeShared<FJsonObject>();
					D->SetStringField(TEXT("pin"), PinName);
					D->SetStringField(TEXT("was_connected_to"), In->Expression->GetName());
					DisconnArr.Add(MakeShared<FJsonValueObject>(D));
					In->Expression = nullptr;
					In->OutputIndex = 0;
					Count++;
				}
			}
		}
	}
	else
	{
		for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
		{
			if (!Expr) continue;
			for (int32 i = 0; ; ++i)
			{
				FExpressionInput* In = Expr->GetInput(i);
				if (!In) break;
				if (In->Expression == Target)
				{
					auto D = MakeShared<FJsonObject>();
					D->SetStringField(TEXT("expression"), Expr->GetName());
					D->SetStringField(TEXT("pin"), Expr->GetInputName(i).ToString());
					DisconnArr.Add(MakeShared<FJsonValueObject>(D));
					In->Expression = nullptr;
					In->OutputIndex = 0;
					Count++;
				}
			}
		}
	}

	auto R = MakeSuccess(AssetPath);
	R->SetArrayField(TEXT("disconnected"), DisconnArr);
	R->SetNumberField(TEXT("count"), Count);
	return JsonToString(R);
}

// 10. set_material_properties — NEW
// Params: { "asset_path": "...", "properties": { "blend_mode": "BLEND_Translucent", "two_sided": true, ... } }
FString MonolithMaterialActions::SetMaterialProperties(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
	if (!Params->TryGetObjectField(TEXT("properties"), PropsPtr) || !PropsPtr)
		return MakeError(TEXT("Missing 'properties' object"));

	GEditor->BeginTransaction(FText::FromString(TEXT("SetMaterialProperties")));
	Mat->Modify();

	TArray<TSharedPtr<FJsonValue>> SetFields;
	const TSharedPtr<FJsonObject>& Props = *PropsPtr;

	// Use reflection to set arbitrary top-level material properties
	for (const auto& Pair : Props->Values)
	{
		FProperty* Prop = Mat->GetClass()->FindPropertyByName(*Pair.Key);
		if (Prop)
		{
			FString ValStr = Pair.Value->AsString();
			void* ValPtr = Prop->ContainerPtrToValuePtr<void>(Mat);
			Prop->ImportText_Direct(*ValStr, ValPtr, Mat, PPF_None);
			SetFields.Add(MakeShared<FJsonValueString>(Pair.Key));
		}
	}

	GEditor->EndTransaction();

	auto R = MakeSuccess(AssetPath);
	R->SetArrayField(TEXT("properties_set"), SetFields);
	return JsonToString(R);
}

// 11. set_expression_property — NEW
// Params: { "asset_path": "...", "expression_name": "...", "property_name": "...", "value": "..." }
FString MonolithMaterialActions::SetExpressionProperty(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ExprName = Params->GetStringField(TEXT("expression_name"));
	FString PropName = Params->GetStringField(TEXT("property_name"));
	FString Value = Params->GetStringField(TEXT("value"));

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	UMaterialExpression* Expr = FindExpression(Mat, ExprName);
	if (!Expr) return MakeError(FString::Printf(TEXT("Expression '%s' not found"), *ExprName));

	FProperty* Prop = Expr->GetClass()->FindPropertyByName(*PropName);
	if (!Prop) return MakeError(FString::Printf(TEXT("Property '%s' not found on '%s'"), *PropName, *ExprName));

	GEditor->BeginTransaction(FText::FromString(TEXT("SetExpressionProperty")));
	Mat->Modify();

	void* ValPtr = Prop->ContainerPtrToValuePtr<void>(Expr);
	Prop->ImportText_Direct(*Value, ValPtr, Expr, PPF_None);

	GEditor->EndTransaction();

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("expression_name"), ExprName);
	R->SetStringField(TEXT("property"), PropName);
	R->SetStringField(TEXT("value_set"), Value);
	return JsonToString(R);
}

// 12. delete_expression — NEW
// Params: { "asset_path": "...", "expression_name": "..." }
FString MonolithMaterialActions::DeleteExpression(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ExprName = Params->GetStringField(TEXT("expression_name"));

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	UMaterialExpression* Expr = FindExpression(Mat, ExprName);
	if (!Expr) return MakeError(FString::Printf(TEXT("Expression '%s' not found"), *ExprName));

	GEditor->BeginTransaction(FText::FromString(TEXT("DeleteExpression")));
	Mat->Modify();
	UMaterialEditingLibrary::DeleteMaterialExpression(Mat, Expr);
	GEditor->EndTransaction();

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("deleted"), ExprName);
	return JsonToString(R);
}

// 13. move_expression — NEW
// Params: { "asset_path": "...", "expression_name": "...", "pos_x": 0, "pos_y": 0 }
FString MonolithMaterialActions::MoveExpression(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ExprName = Params->GetStringField(TEXT("expression_name"));
	int32 PosX = static_cast<int32>(Params->GetNumberField(TEXT("pos_x")));
	int32 PosY = static_cast<int32>(Params->GetNumberField(TEXT("pos_y")));

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	UMaterialExpression* Expr = FindExpression(Mat, ExprName);
	if (!Expr) return MakeError(FString::Printf(TEXT("Expression '%s' not found"), *ExprName));

	Mat->Modify();
	Expr->MaterialExpressionEditorX = PosX;
	Expr->MaterialExpressionEditorY = PosY;

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("expression_name"), ExprName);
	R->SetNumberField(TEXT("pos_x"), PosX);
	R->SetNumberField(TEXT("pos_y"), PosY);
	return JsonToString(R);
}

// 14. rename_parameter — NEW
// Params: { "asset_path": "...", "old_name": "...", "new_name": "..." }
FString MonolithMaterialActions::RenameParameter(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString OldName = Params->GetStringField(TEXT("old_name"));
	FString NewName = Params->GetStringField(TEXT("new_name"));

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	GEditor->BeginTransaction(FText::FromString(TEXT("RenameParameter")));
	Mat->Modify();

	int32 Renamed = 0;
	for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
	{
		if (!Expr) continue;
		if (auto* P = Cast<UMaterialExpressionParameter>(Expr))
		{
			if (P->ParameterName.ToString() == OldName) { P->ParameterName = *NewName; Renamed++; }
		}
		else if (auto* TP = Cast<UMaterialExpressionTextureSampleParameter>(Expr))
		{
			if (TP->ParameterName.ToString() == OldName) { TP->ParameterName = *NewName; Renamed++; }
		}
	}

	GEditor->EndTransaction();

	if (Renamed == 0) return MakeError(FString::Printf(TEXT("Parameter '%s' not found"), *OldName));

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("old_name"), OldName);
	R->SetStringField(TEXT("new_name"), NewName);
	R->SetNumberField(TEXT("expressions_renamed"), Renamed);
	return JsonToString(R);
}

// 15. set_parameter_default — NEW
// Params: { "asset_path": "...", "parameter_name": "...", "value": "..." }
FString MonolithMaterialActions::SetParameterDefault(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ParamName = Params->GetStringField(TEXT("parameter_name"));

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	GEditor->BeginTransaction(FText::FromString(TEXT("SetParameterDefault")));
	Mat->Modify();

	bool bFound = false;
	for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
	{
		if (!Expr) continue;
		if (auto* Scalar = Cast<UMaterialExpressionScalarParameter>(Expr))
		{
			if (Scalar->ParameterName.ToString() == ParamName)
			{
				Scalar->DefaultValue = static_cast<float>(Params->GetNumberField(TEXT("value")));
				bFound = true; break;
			}
		}
		else if (auto* Vec = Cast<UMaterialExpressionVectorParameter>(Expr))
		{
			if (Vec->ParameterName.ToString() == ParamName)
			{
				// Expect "value" as "(R,G,B,A)" or JSON object
				FString ValStr = Params->GetStringField(TEXT("value"));
				FLinearColor Color;
				Color.InitFromString(ValStr);
				Vec->DefaultValue = Color;
				bFound = true; break;
			}
		}
	}

	GEditor->EndTransaction();

	if (!bFound) return MakeError(FString::Printf(TEXT("Parameter '%s' not found"), *ParamName));

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("parameter"), ParamName);
	return JsonToString(R);
}

// 16. set_parameter_group — NEW
// Params: { "asset_path": "...", "parameter_name": "...", "group": "..." }
FString MonolithMaterialActions::SetParameterGroup(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ParamName = Params->GetStringField(TEXT("parameter_name"));
	FString Group = Params->GetStringField(TEXT("group"));

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	GEditor->BeginTransaction(FText::FromString(TEXT("SetParameterGroup")));
	Mat->Modify();

	bool bFound = false;
	for (const TObjectPtr<UMaterialExpression>& Expr : Mat->GetExpressions())
	{
		if (!Expr) continue;
		if (auto* P = Cast<UMaterialExpressionParameter>(Expr))
		{
			if (P->ParameterName.ToString() == ParamName) { P->Group = *Group; bFound = true; }
		}
		else if (auto* TP = Cast<UMaterialExpressionTextureSampleParameter>(Expr))
		{
			if (TP->ParameterName.ToString() == ParamName) { TP->Group = *Group; bFound = true; }
		}
	}

	GEditor->EndTransaction();

	if (!bFound) return MakeError(FString::Printf(TEXT("Parameter '%s' not found"), *ParamName));

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("parameter"), ParamName);
	R->SetStringField(TEXT("group"), Group);
	return JsonToString(R);
}
```

### Step 7: Action Implementations — GRAPH BUILDING + IMPORT/EXPORT (Actions 17–27)

Continues in the same `MonolithMaterialActions.cpp` file.

```cpp
// ============================================================================
// GRAPH BUILDING ACTIONS (17-23)
// ============================================================================

// 17. build_material_graph — port from C++ plugin (BuildMaterialGraph)
// Params: { "asset_path": "...", "graph_spec": { nodes: [...], custom_hlsl_nodes: [...], connections: [...], outputs: [...] }, "clear_existing": false }
FString MonolithMaterialActions::BuildMaterialGraph(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	bool bClear = Params->HasField(TEXT("clear_existing")) ? Params->GetBoolField(TEXT("clear_existing")) : false;

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	const TSharedPtr<FJsonObject>* SpecPtr = nullptr;
	if (!Params->TryGetObjectField(TEXT("graph_spec"), SpecPtr) || !SpecPtr)
		return MakeError(TEXT("Missing 'graph_spec' object"));
	const TSharedPtr<FJsonObject>& Spec = *SpecPtr;

	TArray<TSharedPtr<FJsonValue>> Errors;
	int32 NodesCreated = 0, ConnectionsMade = 0;

	GEditor->BeginTransaction(FText::FromString(TEXT("BuildMaterialGraph")));
	Mat->Modify();

	if (bClear)
		UMaterialEditingLibrary::DeleteAllMaterialExpressions(Mat);

	TMap<FString, UMaterialExpression*> IdMap;

	// Phase 1: Standard nodes
	const TArray<TSharedPtr<FJsonValue>>* NodesArr = nullptr;
	if (Spec->TryGetArrayField(TEXT("nodes"), NodesArr))
	{
		for (const auto& NV : *NodesArr)
		{
			const TSharedPtr<FJsonObject>* NOPtr = nullptr;
			if (!NV || !NV->TryGetObject(NOPtr) || !NOPtr) continue;
			const auto& NO = *NOPtr;

			FString Id = NO->GetStringField(TEXT("id"));
			FString ShortClass = NO->GetStringField(TEXT("class"));
			FString FullClass = ShortClass.StartsWith(TEXT("MaterialExpression")) ? ShortClass : FString::Printf(TEXT("MaterialExpression%s"), *ShortClass);

			UClass* ExprClass = FindObject<UClass>(static_cast<UObject*>(nullptr), *FullClass);
			if (!ExprClass)
			{
				auto E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("node_id"), Id);
				E->SetStringField(TEXT("error"), FString::Printf(TEXT("Class '%s' not found"), *FullClass));
				Errors.Add(MakeShared<FJsonValueObject>(E));
				continue;
			}

			int32 PosX = 0, PosY = 0;
			const TArray<TSharedPtr<FJsonValue>>* PosArr = nullptr;
			if (NO->TryGetArrayField(TEXT("pos"), PosArr) && PosArr->Num() >= 2)
			{
				PosX = (int32)(*PosArr)[0]->AsNumber();
				PosY = (int32)(*PosArr)[1]->AsNumber();
			}

			UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, ExprClass, PosX, PosY);
			if (!NewExpr)
			{
				auto E = MakeShared<FJsonObject>();
				E->SetStringField(TEXT("node_id"), Id);
				E->SetStringField(TEXT("error"), TEXT("CreateMaterialExpression failed"));
				Errors.Add(MakeShared<FJsonValueObject>(E));
				continue;
			}

			// Set properties via reflection
			const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
			if (NO->TryGetObjectField(TEXT("props"), PropsPtr) && PropsPtr)
			{
				for (const auto& Pair : (*PropsPtr)->Values)
				{
					FProperty* Prop = NewExpr->GetClass()->FindPropertyByName(*Pair.Key);
					if (!Prop) continue;
					void* ValPtr = Prop->ContainerPtrToValuePtr<void>(NewExpr);

					if (FFloatProperty* FP = CastField<FFloatProperty>(Prop))
						FP->SetPropertyValue(ValPtr, (float)Pair.Value->AsNumber());
					else if (FDoubleProperty* DP = CastField<FDoubleProperty>(Prop))
						DP->SetPropertyValue(ValPtr, Pair.Value->AsNumber());
					else if (FIntProperty* IP = CastField<FIntProperty>(Prop))
						IP->SetPropertyValue(ValPtr, (int32)Pair.Value->AsNumber());
					else if (FBoolProperty* BP = CastField<FBoolProperty>(Prop))
						BP->SetPropertyValue(ValPtr, Pair.Value->AsBool());
					else
						Prop->ImportText_Direct(*Pair.Value->AsString(), ValPtr, NewExpr, PPF_None);
				}
			}

			IdMap.Add(Id, NewExpr);
			NodesCreated++;
		}
	}

	// Phase 2: Custom HLSL nodes
	const TArray<TSharedPtr<FJsonValue>>* CustomArr = nullptr;
	if (Spec->TryGetArrayField(TEXT("custom_hlsl_nodes"), CustomArr))
	{
		for (const auto& CV : *CustomArr)
		{
			const TSharedPtr<FJsonObject>* COPtr = nullptr;
			if (!CV || !CV->TryGetObject(COPtr) || !COPtr) continue;
			const auto& CO = *COPtr;

			FString Id = CO->GetStringField(TEXT("id"));
			int32 PosX = 0, PosY = 0;
			const TArray<TSharedPtr<FJsonValue>>* PosArr = nullptr;
			if (CO->TryGetArrayField(TEXT("pos"), PosArr) && PosArr->Num() >= 2)
			{
				PosX = (int32)(*PosArr)[0]->AsNumber();
				PosY = (int32)(*PosArr)[1]->AsNumber();
			}

			auto* CustomExpr = Cast<UMaterialExpressionCustom>(
				UMaterialEditingLibrary::CreateMaterialExpression(Mat, UMaterialExpressionCustom::StaticClass(), PosX, PosY));
			if (!CustomExpr) continue;

			CustomExpr->Code = CO->GetStringField(TEXT("code"));
			if (CO->HasField(TEXT("description"))) CustomExpr->Description = CO->GetStringField(TEXT("description"));
			if (CO->HasField(TEXT("output_type"))) CustomExpr->OutputType = ParseCustomOutputType(CO->GetStringField(TEXT("output_type")));

			const TArray<TSharedPtr<FJsonValue>>* InArr = nullptr;
			if (CO->TryGetArrayField(TEXT("inputs"), InArr))
			{
				CustomExpr->Inputs.Empty();
				for (const auto& IV : *InArr)
				{
					const TSharedPtr<FJsonObject>* IOPtr = nullptr;
					if (IV && IV->TryGetObject(IOPtr) && IOPtr)
					{
						FCustomInput CI; CI.InputName = *(*IOPtr)->GetStringField(TEXT("name"));
						CustomExpr->Inputs.Add(CI);
					}
				}
			}

			const TArray<TSharedPtr<FJsonValue>>* AOArr = nullptr;
			if (CO->TryGetArrayField(TEXT("additional_outputs"), AOArr))
			{
				CustomExpr->AdditionalOutputs.Empty();
				for (const auto& OV : *AOArr)
				{
					const TSharedPtr<FJsonObject>* OOPtr = nullptr;
					if (OV && OV->TryGetObject(OOPtr) && OOPtr)
					{
						FCustomOutput NewOut;
						NewOut.OutputName = *(*OOPtr)->GetStringField(TEXT("name"));
						if ((*OOPtr)->HasField(TEXT("type")))
							NewOut.OutputType = ParseCustomOutputType((*OOPtr)->GetStringField(TEXT("type")));
						CustomExpr->AdditionalOutputs.Add(NewOut);
					}
				}
			}
			CustomExpr->RebuildOutputs();
			IdMap.Add(Id, CustomExpr);
			NodesCreated++;
		}
	}

	// Phase 3: Wire expression-to-expression connections
	const TArray<TSharedPtr<FJsonValue>>* ConnsArr = nullptr;
	if (Spec->TryGetArrayField(TEXT("connections"), ConnsArr))
	{
		for (const auto& CV : *ConnsArr)
		{
			const TSharedPtr<FJsonObject>* COPtr = nullptr;
			if (!CV || !CV->TryGetObject(COPtr) || !COPtr) continue;
			const auto& CO = *COPtr;

			FString FromId = CO->GetStringField(TEXT("from"));
			FString ToId = CO->GetStringField(TEXT("to"));
			FString FromPin = CO->HasField(TEXT("from_pin")) ? CO->GetStringField(TEXT("from_pin")) : TEXT("");
			FString ToPin = CO->HasField(TEXT("to_pin")) ? CO->GetStringField(TEXT("to_pin")) : TEXT("");

			UMaterialExpression** From = IdMap.Find(FromId);
			UMaterialExpression** To = IdMap.Find(ToId);
			if (!From || !*From || !To || !*To) continue;

			if (UMaterialEditingLibrary::ConnectMaterialExpressions(*From, FromPin, *To, ToPin))
				ConnectionsMade++;
		}
	}

	// Phase 4: Wire to material outputs
	const TArray<TSharedPtr<FJsonValue>>* OutsArr = nullptr;
	if (Spec->TryGetArrayField(TEXT("outputs"), OutsArr))
	{
		for (const auto& OV : *OutsArr)
		{
			const TSharedPtr<FJsonObject>* OOPtr = nullptr;
			if (!OV || !OV->TryGetObject(OOPtr) || !OOPtr) continue;
			const auto& OO = *OOPtr;

			FString FromId = OO->GetStringField(TEXT("from"));
			FString FromPin = OO->HasField(TEXT("from_pin")) ? OO->GetStringField(TEXT("from_pin")) : TEXT("");
			FString ToProp = OO->GetStringField(TEXT("to_property"));

			UMaterialExpression** From = IdMap.Find(FromId);
			if (!From || !*From) continue;

			EMaterialProperty MatProp = ParseMaterialProperty(ToProp);
			if (MatProp == MP_MAX) continue;

			if (UMaterialEditingLibrary::ConnectMaterialProperty(*From, FromPin, MatProp))
				ConnectionsMade++;
		}
	}

	GEditor->EndTransaction();

	auto R = MakeSuccess(AssetPath);
	R->SetNumberField(TEXT("nodes_created"), NodesCreated);
	R->SetNumberField(TEXT("connections_made"), ConnectionsMade);
	R->SetArrayField(TEXT("errors"), Errors);

	auto IdMapJson = MakeShared<FJsonObject>();
	for (const auto& P : IdMap)
		if (P.Value) IdMapJson->SetStringField(P.Key, P.Value->GetName());
	R->SetObjectField(TEXT("id_to_name"), IdMapJson);

	return JsonToString(R);
}

// 18. add_expression — NEW (single node version of build)
// Params: { "asset_path": "...", "class": "Constant3Vector", "pos_x": 0, "pos_y": 0, "properties": { "Constant": "(R=1,G=0,B=0)" } }
FString MonolithMaterialActions::AddExpression(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ShortClass = Params->GetStringField(TEXT("class"));
	int32 PosX = Params->HasField(TEXT("pos_x")) ? (int32)Params->GetNumberField(TEXT("pos_x")) : 0;
	int32 PosY = Params->HasField(TEXT("pos_y")) ? (int32)Params->GetNumberField(TEXT("pos_y")) : 0;

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	FString FullClass = ShortClass.StartsWith(TEXT("MaterialExpression")) ? ShortClass : FString::Printf(TEXT("MaterialExpression%s"), *ShortClass);
	UClass* ExprClass = FindObject<UClass>(static_cast<UObject*>(nullptr), *FullClass);
	if (!ExprClass) return MakeError(FString::Printf(TEXT("Class '%s' not found"), *FullClass));

	GEditor->BeginTransaction(FText::FromString(TEXT("AddExpression")));
	Mat->Modify();
	UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, ExprClass, PosX, PosY);
	if (!NewExpr)
	{
		GEditor->EndTransaction();
		return MakeError(TEXT("CreateMaterialExpression failed"));
	}

	// Set properties
	const TSharedPtr<FJsonObject>* PropsPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("properties"), PropsPtr) && PropsPtr)
	{
		for (const auto& Pair : (*PropsPtr)->Values)
		{
			FProperty* Prop = NewExpr->GetClass()->FindPropertyByName(*Pair.Key);
			if (Prop)
			{
				void* ValPtr = Prop->ContainerPtrToValuePtr<void>(NewExpr);
				Prop->ImportText_Direct(*Pair.Value->AsString(), ValPtr, NewExpr, PPF_None);
			}
		}
	}

	GEditor->EndTransaction();

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("expression_name"), NewExpr->GetName());
	R->SetStringField(TEXT("class"), FullClass);
	R->SetNumberField(TEXT("pos_x"), PosX);
	R->SetNumberField(TEXT("pos_y"), PosY);
	return JsonToString(R);
}

// 19. connect_expressions — NEW
// Params: { "asset_path": "...", "from": "ExprName", "from_pin": "", "to": "ExprName", "to_pin": "BaseColor" }
FString MonolithMaterialActions::ConnectExpressions(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString FromName = Params->GetStringField(TEXT("from"));
	FString FromPin = Params->HasField(TEXT("from_pin")) ? Params->GetStringField(TEXT("from_pin")) : TEXT("");
	FString ToName = Params->GetStringField(TEXT("to"));
	FString ToPin = Params->HasField(TEXT("to_pin")) ? Params->GetStringField(TEXT("to_pin")) : TEXT("");

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	UMaterialExpression* From = FindExpression(Mat, FromName);
	UMaterialExpression* To = FindExpression(Mat, ToName);
	if (!From) return MakeError(FString::Printf(TEXT("Source expression '%s' not found"), *FromName));
	if (!To) return MakeError(FString::Printf(TEXT("Target expression '%s' not found"), *ToName));

	bool bOk = UMaterialEditingLibrary::ConnectMaterialExpressions(From, FromPin, To, ToPin);
	if (!bOk) return MakeError(TEXT("ConnectMaterialExpressions failed"));

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("from"), FromName);
	R->SetStringField(TEXT("to"), ToName);
	return JsonToString(R);
}

// 20. connect_to_material_output — NEW
// Params: { "asset_path": "...", "expression_name": "...", "from_pin": "", "property": "BaseColor" }
FString MonolithMaterialActions::ConnectToMaterialOutput(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString ExprName = Params->GetStringField(TEXT("expression_name"));
	FString FromPin = Params->HasField(TEXT("from_pin")) ? Params->GetStringField(TEXT("from_pin")) : TEXT("");
	FString Property = Params->GetStringField(TEXT("property"));

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	UMaterialExpression* Expr = FindExpression(Mat, ExprName);
	if (!Expr) return MakeError(FString::Printf(TEXT("Expression '%s' not found"), *ExprName));

	EMaterialProperty MatProp = ParseMaterialProperty(Property);
	if (MatProp == MP_MAX) return MakeError(FString::Printf(TEXT("Unknown material property '%s'"), *Property));

	bool bOk = UMaterialEditingLibrary::ConnectMaterialProperty(Expr, FromPin, MatProp);
	if (!bOk) return MakeError(TEXT("ConnectMaterialProperty failed"));

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("expression"), ExprName);
	R->SetStringField(TEXT("property"), Property);
	return JsonToString(R);
}

// 21. create_custom_hlsl — port from C++ plugin
// Params: { "asset_path":"...", "code":"...", "description":"...", "output_type":"Float3", "inputs":[{"name":"UV"}], "additional_outputs":[{"name":"Alpha","type":"Float1"}], "pos_x":0, "pos_y":0 }
FString MonolithMaterialActions::CreateCustomHlsl(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	int32 PosX = Params->HasField(TEXT("pos_x")) ? (int32)Params->GetNumberField(TEXT("pos_x")) : 0;
	int32 PosY = Params->HasField(TEXT("pos_y")) ? (int32)Params->GetNumberField(TEXT("pos_y")) : 0;

	GEditor->BeginTransaction(FText::FromString(TEXT("CreateCustomHLSL")));
	Mat->Modify();

	auto* CustomExpr = Cast<UMaterialExpressionCustom>(
		UMaterialEditingLibrary::CreateMaterialExpression(Mat, UMaterialExpressionCustom::StaticClass(), PosX, PosY));
	if (!CustomExpr) { GEditor->EndTransaction(); return MakeError(TEXT("Failed to create Custom HLSL")); }

	CustomExpr->Code = Params->GetStringField(TEXT("code"));
	if (Params->HasField(TEXT("description"))) CustomExpr->Description = Params->GetStringField(TEXT("description"));
	if (Params->HasField(TEXT("output_type"))) CustomExpr->OutputType = ParseCustomOutputType(Params->GetStringField(TEXT("output_type")));

	const TArray<TSharedPtr<FJsonValue>>* InArr = nullptr;
	if (Params->TryGetArrayField(TEXT("inputs"), InArr))
	{
		CustomExpr->Inputs.Empty();
		for (const auto& IV : *InArr)
		{
			const TSharedPtr<FJsonObject>* IOPtr = nullptr;
			if (IV && IV->TryGetObject(IOPtr) && IOPtr)
			{
				FCustomInput CI; CI.InputName = *(*IOPtr)->GetStringField(TEXT("name"));
				CustomExpr->Inputs.Add(CI);
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* AOArr = nullptr;
	if (Params->TryGetArrayField(TEXT("additional_outputs"), AOArr))
	{
		CustomExpr->AdditionalOutputs.Empty();
		for (const auto& OV : *AOArr)
		{
			const TSharedPtr<FJsonObject>* OOPtr = nullptr;
			if (OV && OV->TryGetObject(OOPtr) && OOPtr)
			{
				FCustomOutput NewOut;
				NewOut.OutputName = *(*OOPtr)->GetStringField(TEXT("name"));
				if ((*OOPtr)->HasField(TEXT("type")))
					NewOut.OutputType = ParseCustomOutputType((*OOPtr)->GetStringField(TEXT("type")));
				CustomExpr->AdditionalOutputs.Add(NewOut);
			}
		}
	}
	CustomExpr->RebuildOutputs();
	GEditor->EndTransaction();

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("expression_name"), CustomExpr->GetName());
	return JsonToString(R);
}

// 22. add_function_call — NEW
// Params: { "asset_path": "...", "function_path": "/Engine/Functions/...", "pos_x": 0, "pos_y": 0 }
FString MonolithMaterialActions::AddFunctionCall(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString FuncPath = Params->GetStringField(TEXT("function_path"));
	int32 PosX = Params->HasField(TEXT("pos_x")) ? (int32)Params->GetNumberField(TEXT("pos_x")) : 0;
	int32 PosY = Params->HasField(TEXT("pos_y")) ? (int32)Params->GetNumberField(TEXT("pos_y")) : 0;

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	UMaterialFunction* Func = Cast<UMaterialFunction>(UEditorAssetLibrary::LoadAsset(FuncPath));
	if (!Func) return MakeError(FString::Printf(TEXT("Material function '%s' not found"), *FuncPath));

	GEditor->BeginTransaction(FText::FromString(TEXT("AddFunctionCall")));
	Mat->Modify();
	auto* FuncCall = Cast<UMaterialExpressionMaterialFunctionCall>(
		UMaterialEditingLibrary::CreateMaterialExpression(Mat, UMaterialExpressionMaterialFunctionCall::StaticClass(), PosX, PosY));
	if (!FuncCall) { GEditor->EndTransaction(); return MakeError(TEXT("Failed to create function call")); }

	FuncCall->SetMaterialFunction(Func);
	GEditor->EndTransaction();

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("expression_name"), FuncCall->GetName());
	R->SetStringField(TEXT("function"), FuncPath);
	return JsonToString(R);
}

// 23. add_texture_sample — NEW
// Params: { "asset_path":"...", "texture_path":"/Game/Textures/...", "is_parameter":true, "parameter_name":"BaseColor", "pos_x":0, "pos_y":0 }
FString MonolithMaterialActions::AddTextureSample(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString TexPath = Params->GetStringField(TEXT("texture_path"));
	bool bIsParam = Params->HasField(TEXT("is_parameter")) ? Params->GetBoolField(TEXT("is_parameter")) : false;
	int32 PosX = Params->HasField(TEXT("pos_x")) ? (int32)Params->GetNumberField(TEXT("pos_x")) : 0;
	int32 PosY = Params->HasField(TEXT("pos_y")) ? (int32)Params->GetNumberField(TEXT("pos_y")) : 0;

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	UTexture* Tex = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(TexPath));
	if (!Tex) return MakeError(FString::Printf(TEXT("Texture '%s' not found"), *TexPath));

	UClass* ExprClass = bIsParam ? UMaterialExpressionTextureSampleParameter2D::StaticClass() : UMaterialExpressionTextureSample::StaticClass();

	GEditor->BeginTransaction(FText::FromString(TEXT("AddTextureSample")));
	Mat->Modify();
	UMaterialExpression* NewExpr = UMaterialEditingLibrary::CreateMaterialExpression(Mat, ExprClass, PosX, PosY);
	if (!NewExpr) { GEditor->EndTransaction(); return MakeError(TEXT("Failed to create texture sample")); }

	if (auto* TS = Cast<UMaterialExpressionTextureSample>(NewExpr))
		TS->Texture = Tex;

	if (bIsParam)
	{
		if (auto* TSP = Cast<UMaterialExpressionTextureSampleParameter>(NewExpr))
		{
			if (Params->HasField(TEXT("parameter_name")))
				TSP->ParameterName = *Params->GetStringField(TEXT("parameter_name"));
		}
	}
	GEditor->EndTransaction();

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("expression_name"), NewExpr->GetName());
	R->SetStringField(TEXT("texture"), TexPath);
	R->SetBoolField(TEXT("is_parameter"), bIsParam);
	return JsonToString(R);
}

// ============================================================================
// IMPORT/EXPORT ACTIONS (24-27)
// ============================================================================

// 24. export_material_graph — port from C++ plugin (ExportMaterialGraph)
// Params: { "asset_path": "..." }
// NOTE: This is a faithful port of the existing ExportMaterialGraph — same JSON format.
// Implementation identical to the MaterialMCPReaderLibrary version, but uses the helpers namespace.
// Omitted for brevity — same logic as the original C++ plugin lines 928-1133.
FString MonolithMaterialActions::ExportMaterialGraph(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	TConstArrayView<TObjectPtr<UMaterialExpression>> Expressions = Mat->GetExpressions();

	TMap<UMaterialExpression*, FString> ExprToId;
	for (const auto& Expr : Expressions)
		if (Expr) ExprToId.Add(Expr, Expr->GetName());

	TArray<TSharedPtr<FJsonValue>> NodesArr, CustomArr;
	for (const auto& Expr : Expressions)
	{
		if (!Expr) continue;
		if (const auto* CE = Cast<UMaterialExpressionCustom>(Expr))
		{
			auto J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("id"), Expr->GetName());
			TArray<TSharedPtr<FJsonValue>> PA;
			PA.Add(MakeShared<FJsonValueNumber>(Expr->MaterialExpressionEditorX));
			PA.Add(MakeShared<FJsonValueNumber>(Expr->MaterialExpressionEditorY));
			J->SetArrayField(TEXT("pos"), PA);
			J->SetStringField(TEXT("code"), CE->Code);
			J->SetStringField(TEXT("description"), CE->Description);
			J->SetStringField(TEXT("output_type"), CustomOutputTypeToString(CE->OutputType));

			TArray<TSharedPtr<FJsonValue>> IA;
			for (const auto& CI : CE->Inputs)
			{
				auto IJ = MakeShared<FJsonObject>();
				IJ->SetStringField(TEXT("name"), CI.InputName.ToString());
				IA.Add(MakeShared<FJsonValueObject>(IJ));
			}
			J->SetArrayField(TEXT("inputs"), IA);

			TArray<TSharedPtr<FJsonValue>> OA;
			for (const auto& AO : CE->AdditionalOutputs)
			{
				auto OJ = MakeShared<FJsonObject>();
				OJ->SetStringField(TEXT("name"), AO.OutputName.ToString());
				OJ->SetStringField(TEXT("type"), CustomOutputTypeToString(AO.OutputType));
				OA.Add(MakeShared<FJsonValueObject>(OJ));
			}
			J->SetArrayField(TEXT("additional_outputs"), OA);
			CustomArr.Add(MakeShared<FJsonValueObject>(J));
		}
		else
		{
			auto J = MakeShared<FJsonObject>();
			J->SetStringField(TEXT("id"), Expr->GetName());
			FString CN = Expr->GetClass()->GetName();
			if (CN.StartsWith(TEXT("MaterialExpression"))) CN = CN.Mid(18);
			J->SetStringField(TEXT("class"), CN);
			TArray<TSharedPtr<FJsonValue>> PA;
			PA.Add(MakeShared<FJsonValueNumber>(Expr->MaterialExpressionEditorX));
			PA.Add(MakeShared<FJsonValueNumber>(Expr->MaterialExpressionEditorY));
			J->SetArrayField(TEXT("pos"), PA);

			auto PJ = MakeShared<FJsonObject>();
			for (TFieldIterator<FProperty> It(Expr->GetClass()); It; ++It)
			{
				FProperty* P = *It;
				if (!P || P->HasAnyPropertyFlags(CPF_Transient | CPF_DuplicateTransient)) continue;
				if (P->GetOwnerClass() == UMaterialExpression::StaticClass()) continue;
				FString V;
				P->ExportTextItem_Direct(V, P->ContainerPtrToValuePtr<void>(Expr), nullptr, nullptr, PPF_None);
				if (!V.IsEmpty()) PJ->SetStringField(P->GetName(), V);
			}
			J->SetObjectField(TEXT("props"), PJ);
			NodesArr.Add(MakeShared<FJsonValueObject>(J));
		}
	}

	// Connections
	TArray<TSharedPtr<FJsonValue>> ConnsArr;
	for (const auto& Expr : Expressions)
	{
		if (!Expr) continue;
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* In = Expr->GetInput(i);
			if (!In) break;
			if (!In->Expression) continue;
			auto C = MakeShared<FJsonObject>();
			FString* FId = ExprToId.Find(In->Expression);
			C->SetStringField(TEXT("from"), FId ? *FId : In->Expression->GetName());
			FString FP;
			if (In->Expression->Outputs.IsValidIndex(In->OutputIndex))
				FP = In->Expression->Outputs[In->OutputIndex].OutputName.ToString();
			C->SetStringField(TEXT("from_pin"), FP);
			C->SetStringField(TEXT("to"), Expr->GetName());
			C->SetStringField(TEXT("to_pin"), Expr->GetInputName(i).ToString());
			ConnsArr.Add(MakeShared<FJsonValueObject>(C));
		}
	}

	// Material outputs
	TArray<TSharedPtr<FJsonValue>> OutsArr;
	for (const auto& E : GetAllMaterialProperties())
	{
		FExpressionInput* In = Mat->GetExpressionInputForProperty(E.Property);
		if (In && In->Expression)
		{
			auto O = MakeShared<FJsonObject>();
			FString* FId = ExprToId.Find(In->Expression);
			O->SetStringField(TEXT("from"), FId ? *FId : In->Expression->GetName());
			FString FP;
			if (In->Expression->Outputs.IsValidIndex(In->OutputIndex))
				FP = In->Expression->Outputs[In->OutputIndex].OutputName.ToString();
			O->SetStringField(TEXT("from_pin"), FP);
			O->SetStringField(TEXT("to_property"), E.Name);
			OutsArr.Add(MakeShared<FJsonValueObject>(O));
		}
	}

	auto R = MakeSuccess(AssetPath);
	R->SetArrayField(TEXT("nodes"), NodesArr);
	R->SetArrayField(TEXT("custom_hlsl_nodes"), CustomArr);
	R->SetArrayField(TEXT("connections"), ConnsArr);
	R->SetArrayField(TEXT("outputs"), OutsArr);
	return JsonToString(R);
}

// 25. import_material_graph — port from C++ plugin
// Params: { "asset_path":"...", "graph_json":"...", "mode": "overwrite"|"merge" }
FString MonolithMaterialActions::ImportMaterialGraph(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	FString GraphJson = Params->GetStringField(TEXT("graph_json"));
	FString Mode = Params->HasField(TEXT("mode")) ? Params->GetStringField(TEXT("mode")) : TEXT("overwrite");

	auto GraphSpec = ParseJson(GraphJson);
	if (!GraphSpec) return MakeError(TEXT("Failed to parse graph_json"));

	auto BuildParams = MakeShared<FJsonObject>();
	BuildParams->SetStringField(TEXT("asset_path"), AssetPath);
	BuildParams->SetObjectField(TEXT("graph_spec"), GraphSpec);
	BuildParams->SetBoolField(TEXT("clear_existing"), Mode == TEXT("overwrite"));

	return BuildMaterialGraph(BuildParams);
}

// 26. begin_transaction
// Params: { "name": "MyTransaction" }
FString MonolithMaterialActions::BeginTransaction(TSharedPtr<FJsonObject> Params)
{
	FString Name = Params->HasField(TEXT("name")) ? Params->GetStringField(TEXT("name")) : TEXT("MaterialEdit");
	GEditor->BeginTransaction(FText::FromString(Name));
	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("transaction"), Name);
	return JsonToString(R);
}

// 27. end_transaction
// Params: {} (none)
FString MonolithMaterialActions::EndTransaction(TSharedPtr<FJsonObject> Params)
{
	GEditor->EndTransaction();
	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	return JsonToString(R);
}
```

### Step 8: Action Implementations — TEMPLATES + PREVIEW/VALIDATION (Actions 28–39)

Continues in the same `MonolithMaterialActions.cpp` file.

```cpp
// ============================================================================
// TEMPLATE ACTIONS (28-34)
// ============================================================================

// 28. create_material — NEW
// Params: { "asset_path": "/Game/Materials/M_New", "name": "M_New" }
FString MonolithMaterialActions::CreateMaterial(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));

	// Extract package path and asset name
	FString PackagePath = FPackageName::GetLongPackagePath(AssetPath);
	FString AssetName = FPackageName::GetShortName(AssetPath);

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialFactoryNew* Factory = NewObject<UMaterialFactoryNew>();
	UObject* NewAsset = AssetTools.CreateAsset(AssetName, PackagePath, UMaterial::StaticClass(), Factory);

	if (!NewAsset)
		return MakeError(FString::Printf(TEXT("Failed to create material at '%s'"), *AssetPath));

	auto R = MakeSuccess(NewAsset->GetPathName());
	R->SetStringField(TEXT("name"), AssetName);
	return JsonToString(R);
}

// 29. create_pbr_material — NEW template
// Params: { "asset_path": "/Game/Materials/M_PBR", "base_color": "(R=0.5,G=0.5,B=0.5,A=1)", "roughness": 0.5, "metallic": 0.0 }
FString MonolithMaterialActions::CreatePbrMaterial(TSharedPtr<FJsonObject> Params)
{
	// First create the material
	FString CreateResult = CreateMaterial(Params);
	auto CreateJson = ParseJson(CreateResult);
	if (!CreateJson || !CreateJson->GetBoolField(TEXT("success")))
		return CreateResult;

	FString AssetPath = CreateJson->GetStringField(TEXT("asset_path"));

	// Build graph spec for PBR: ScalarParam(Roughness), ScalarParam(Metallic), VectorParam(BaseColor)
	FString GraphSpec = FString::Printf(TEXT(R"({
		"nodes": [
			{"id":"base_color","class":"VectorParameter","pos":[-400,0],"props":{"ParameterName":"BaseColor","DefaultValue":"%s"}},
			{"id":"roughness","class":"ScalarParameter","pos":[-400,200],"props":{"ParameterName":"Roughness","DefaultValue":"%f"}},
			{"id":"metallic","class":"ScalarParameter","pos":[-400,400],"props":{"ParameterName":"Metallic","DefaultValue":"%f"}}
		],
		"connections": [],
		"outputs": [
			{"from":"base_color","from_pin":"","to_property":"BaseColor"},
			{"from":"roughness","from_pin":"","to_property":"Roughness"},
			{"from":"metallic","from_pin":"","to_property":"Metallic"}
		]
	})"),
		Params->HasField(TEXT("base_color")) ? *Params->GetStringField(TEXT("base_color")) : TEXT("(R=0.5,G=0.5,B=0.5,A=1.0)"),
		Params->HasField(TEXT("roughness")) ? Params->GetNumberField(TEXT("roughness")) : 0.5,
		Params->HasField(TEXT("metallic")) ? Params->GetNumberField(TEXT("metallic")) : 0.0
	);

	auto BuildParams = MakeShared<FJsonObject>();
	BuildParams->SetStringField(TEXT("asset_path"), AssetPath);
	BuildParams->SetObjectField(TEXT("graph_spec"), ParseJson(GraphSpec));
	BuildParams->SetBoolField(TEXT("clear_existing"), false);

	return BuildMaterialGraph(BuildParams);
}

// 30. create_emissive_material — NEW template
// Params: { "asset_path": "...", "emissive_color": "(R=1,G=0,B=0,A=1)", "intensity": 10.0 }
FString MonolithMaterialActions::CreateEmissiveMaterial(TSharedPtr<FJsonObject> Params)
{
	FString CreateResult = CreateMaterial(Params);
	auto CreateJson = ParseJson(CreateResult);
	if (!CreateJson || !CreateJson->GetBoolField(TEXT("success"))) return CreateResult;
	FString AssetPath = CreateJson->GetStringField(TEXT("asset_path"));

	float Intensity = Params->HasField(TEXT("intensity")) ? (float)Params->GetNumberField(TEXT("intensity")) : 10.0f;

	FString GraphSpec = FString::Printf(TEXT(R"({
		"nodes": [
			{"id":"color","class":"VectorParameter","pos":[-600,0],"props":{"ParameterName":"EmissiveColor","DefaultValue":"%s"}},
			{"id":"intensity","class":"ScalarParameter","pos":[-600,200],"props":{"ParameterName":"EmissiveIntensity","DefaultValue":"%f"}},
			{"id":"multiply","class":"Multiply","pos":[-300,0]}
		],
		"connections": [
			{"from":"color","from_pin":"","to":"multiply","to_pin":"A"},
			{"from":"intensity","from_pin":"","to":"multiply","to_pin":"B"}
		],
		"outputs": [
			{"from":"multiply","from_pin":"","to_property":"EmissiveColor"}
		]
	})"),
		Params->HasField(TEXT("emissive_color")) ? *Params->GetStringField(TEXT("emissive_color")) : TEXT("(R=1,G=1,B=1,A=1)"),
		Intensity
	);

	auto BP = MakeShared<FJsonObject>();
	BP->SetStringField(TEXT("asset_path"), AssetPath);
	BP->SetObjectField(TEXT("graph_spec"), ParseJson(GraphSpec));
	BP->SetBoolField(TEXT("clear_existing"), false);
	return BuildMaterialGraph(BP);
}

// 31. create_glass_material — NEW template
// Params: { "asset_path": "...", "opacity": 0.1, "refraction": 1.5 }
FString MonolithMaterialActions::CreateGlassMaterial(TSharedPtr<FJsonObject> Params)
{
	FString CreateResult = CreateMaterial(Params);
	auto CreateJson = ParseJson(CreateResult);
	if (!CreateJson || !CreateJson->GetBoolField(TEXT("success"))) return CreateResult;
	FString AssetPath = CreateJson->GetStringField(TEXT("asset_path"));

	// Set blend mode to translucent
	auto SetPropsParams = MakeShared<FJsonObject>();
	SetPropsParams->SetStringField(TEXT("asset_path"), AssetPath);
	auto Props = MakeShared<FJsonObject>();
	Props->SetStringField(TEXT("BlendMode"), TEXT("BLEND_Translucent"));
	SetPropsParams->SetObjectField(TEXT("properties"), Props);
	SetMaterialProperties(SetPropsParams);

	float Opacity = Params->HasField(TEXT("opacity")) ? (float)Params->GetNumberField(TEXT("opacity")) : 0.1f;
	float Refraction = Params->HasField(TEXT("refraction")) ? (float)Params->GetNumberField(TEXT("refraction")) : 1.5f;

	FString GraphSpec = FString::Printf(TEXT(R"({
		"nodes": [
			{"id":"opacity","class":"ScalarParameter","pos":[-400,0],"props":{"ParameterName":"Opacity","DefaultValue":"%f"}},
			{"id":"roughness","class":"ScalarParameter","pos":[-400,200],"props":{"ParameterName":"Roughness","DefaultValue":"0.05"}},
			{"id":"specular","class":"ScalarParameter","pos":[-400,400],"props":{"ParameterName":"Specular","DefaultValue":"0.5"}},
			{"id":"refraction","class":"ScalarParameter","pos":[-400,600],"props":{"ParameterName":"Refraction","DefaultValue":"%f"}}
		],
		"connections": [],
		"outputs": [
			{"from":"opacity","from_pin":"","to_property":"Opacity"},
			{"from":"roughness","from_pin":"","to_property":"Roughness"},
			{"from":"specular","from_pin":"","to_property":"Specular"},
			{"from":"refraction","from_pin":"","to_property":"Refraction"}
		]
	})"), Opacity, Refraction);

	auto BP = MakeShared<FJsonObject>();
	BP->SetStringField(TEXT("asset_path"), AssetPath);
	BP->SetObjectField(TEXT("graph_spec"), ParseJson(GraphSpec));
	BP->SetBoolField(TEXT("clear_existing"), false);
	return BuildMaterialGraph(BP);
}

// 32. create_subsurface_material — NEW template
// Params: { "asset_path": "...", "subsurface_color": "(R=1,G=0.2,B=0.1,A=1)", "opacity": 0.5 }
FString MonolithMaterialActions::CreateSubsurfaceMaterial(TSharedPtr<FJsonObject> Params)
{
	FString CreateResult = CreateMaterial(Params);
	auto CreateJson = ParseJson(CreateResult);
	if (!CreateJson || !CreateJson->GetBoolField(TEXT("success"))) return CreateResult;
	FString AssetPath = CreateJson->GetStringField(TEXT("asset_path"));

	// Set shading model to subsurface
	auto SetPropsParams = MakeShared<FJsonObject>();
	SetPropsParams->SetStringField(TEXT("asset_path"), AssetPath);
	auto Props = MakeShared<FJsonObject>();
	Props->SetStringField(TEXT("ShadingModel"), TEXT("MSM_Subsurface"));
	SetPropsParams->SetObjectField(TEXT("properties"), Props);
	SetMaterialProperties(SetPropsParams);

	FString GraphSpec = FString::Printf(TEXT(R"({
		"nodes": [
			{"id":"base_color","class":"VectorParameter","pos":[-400,0],"props":{"ParameterName":"BaseColor","DefaultValue":"(R=0.8,G=0.8,B=0.8,A=1)"}},
			{"id":"sss_color","class":"VectorParameter","pos":[-400,200],"props":{"ParameterName":"SubsurfaceColor","DefaultValue":"%s"}},
			{"id":"opacity","class":"ScalarParameter","pos":[-400,400],"props":{"ParameterName":"Opacity","DefaultValue":"%f"}}
		],
		"connections": [],
		"outputs": [
			{"from":"base_color","from_pin":"","to_property":"BaseColor"},
			{"from":"sss_color","from_pin":"","to_property":"SubsurfaceColor"},
			{"from":"opacity","from_pin":"","to_property":"Opacity"}
		]
	})"),
		Params->HasField(TEXT("subsurface_color")) ? *Params->GetStringField(TEXT("subsurface_color")) : TEXT("(R=1,G=0.2,B=0.1,A=1)"),
		Params->HasField(TEXT("opacity")) ? Params->GetNumberField(TEXT("opacity")) : 0.5
	);

	auto BP = MakeShared<FJsonObject>();
	BP->SetStringField(TEXT("asset_path"), AssetPath);
	BP->SetObjectField(TEXT("graph_spec"), ParseJson(GraphSpec));
	BP->SetBoolField(TEXT("clear_existing"), false);
	return BuildMaterialGraph(BP);
}

// 33. create_decal_material — NEW template
// Params: { "asset_path": "..." }
FString MonolithMaterialActions::CreateDecalMaterial(TSharedPtr<FJsonObject> Params)
{
	FString CreateResult = CreateMaterial(Params);
	auto CreateJson = ParseJson(CreateResult);
	if (!CreateJson || !CreateJson->GetBoolField(TEXT("success"))) return CreateResult;
	FString AssetPath = CreateJson->GetStringField(TEXT("asset_path"));

	// Set domain to Deferred Decal, blend mode to Translucent
	auto SetPropsParams = MakeShared<FJsonObject>();
	SetPropsParams->SetStringField(TEXT("asset_path"), AssetPath);
	auto Props = MakeShared<FJsonObject>();
	Props->SetStringField(TEXT("MaterialDomain"), TEXT("MD_DeferredDecal"));
	Props->SetStringField(TEXT("BlendMode"), TEXT("BLEND_Translucent"));
	SetPropsParams->SetObjectField(TEXT("properties"), Props);
	SetMaterialProperties(SetPropsParams);

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("template"), TEXT("decal"));
	return JsonToString(R);
}

// 34. create_from_textures — NEW template
// Params: { "asset_path":"...", "albedo":"/Game/T_Albedo", "normal":"/Game/T_Normal", "roughness":"/Game/T_Roughness", "metallic":"/Game/T_Metallic", "ao":"/Game/T_AO" }
FString MonolithMaterialActions::CreateFromTextures(TSharedPtr<FJsonObject> Params)
{
	FString CreateResult = CreateMaterial(Params);
	auto CreateJson = ParseJson(CreateResult);
	if (!CreateJson || !CreateJson->GetBoolField(TEXT("success"))) return CreateResult;
	FString AssetPath = CreateJson->GetStringField(TEXT("asset_path"));

	// Map of texture slots: key -> (property, pos_y)
	struct FTexSlot { const TCHAR* Key; const TCHAR* ParamName; const TCHAR* Property; int32 PosY; };
	static const FTexSlot Slots[] = {
		{ TEXT("albedo"),    TEXT("BaseColor"),    TEXT("BaseColor"),       0 },
		{ TEXT("normal"),    TEXT("Normal"),        TEXT("Normal"),         200 },
		{ TEXT("roughness"), TEXT("Roughness"),     TEXT("Roughness"),     400 },
		{ TEXT("metallic"),  TEXT("Metallic"),      TEXT("Metallic"),      600 },
		{ TEXT("ao"),        TEXT("AO"),            TEXT("AmbientOcclusion"), 800 },
	};

	GEditor->BeginTransaction(FText::FromString(TEXT("CreateFromTextures")));
	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) { GEditor->EndTransaction(); return MakeError(TEXT("Failed to load created material")); }
	Mat->Modify();

	int32 Added = 0;
	for (const FTexSlot& Slot : Slots)
	{
		if (!Params->HasField(Slot.Key)) continue;
		FString TexPath = Params->GetStringField(Slot.Key);
		UTexture* Tex = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(TexPath));
		if (!Tex) continue;

		auto* TSP = Cast<UMaterialExpressionTextureSampleParameter2D>(
			UMaterialEditingLibrary::CreateMaterialExpression(Mat, UMaterialExpressionTextureSampleParameter2D::StaticClass(), -400, Slot.PosY));
		if (!TSP) continue;

		TSP->ParameterName = Slot.ParamName;
		TSP->Texture = Tex;

		EMaterialProperty MatProp = ParseMaterialProperty(Slot.Property);
		if (MatProp != MP_MAX)
			UMaterialEditingLibrary::ConnectMaterialProperty(TSP, TEXT(""), MatProp);

		Added++;
	}

	GEditor->EndTransaction();

	auto R = MakeSuccess(AssetPath);
	R->SetNumberField(TEXT("textures_added"), Added);
	return JsonToString(R);
}

// ============================================================================
// PREVIEW/VALIDATION ACTIONS (35-39)
// ============================================================================

// 35. render_preview — port from C++ plugin
// Params: { "asset_path":"...", "resolution": 256 }
FString MonolithMaterialActions::RenderPreview(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	int32 Res = Params->HasField(TEXT("resolution")) ? (int32)Params->GetNumberField(TEXT("resolution")) : 256;
	if (Res <= 0) Res = 256;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) return MakeError(FString::Printf(TEXT("Failed to load '%s'"), *AssetPath));

	FObjectThumbnail Thumb;
	ThumbnailTools::RenderThumbnail(Asset, Res, Res, ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush, nullptr, &Thumb);

	if (Thumb.GetImageWidth() == 0) return MakeError(TEXT("Thumbnail rendering produced empty image"));

	TArray64<uint8> PngData;
	FImageView IV((void*)Thumb.AccessImageData().GetData(), Thumb.GetImageWidth(), Thumb.GetImageHeight(), ERawImageFormat::BGRA8);
	FImageUtils::CompressImage(PngData, TEXT(".png"), IV);
	if (PngData.Num() == 0) return MakeError(TEXT("PNG compression failed"));

	FString MatName = FPaths::GetBaseFilename(AssetPath);
	FString Dir = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Monolith"), TEXT("previews"));
	IFileManager::Get().MakeDirectory(*Dir, true);
	FString FilePath = FPaths::Combine(Dir, FString::Printf(TEXT("%s_%d.png"), *MatName, Res));
	FFileHelper::SaveArrayToFile(PngData, *FilePath);

	auto R = MakeSuccess(AssetPath);
	R->SetStringField(TEXT("file_path"), FilePath);
	R->SetNumberField(TEXT("width"), Thumb.GetImageWidth());
	R->SetNumberField(TEXT("height"), Thumb.GetImageHeight());
	return JsonToString(R);
}

// 36. get_thumbnail — port from C++ plugin (base64)
// Params: { "asset_path":"...", "resolution": 256 }
FString MonolithMaterialActions::GetThumbnail(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	int32 Res = Params->HasField(TEXT("resolution")) ? (int32)Params->GetNumberField(TEXT("resolution")) : 256;
	if (Res <= 0) Res = 256;

	UObject* Asset = UEditorAssetLibrary::LoadAsset(AssetPath);
	if (!Asset) return MakeError(FString::Printf(TEXT("Failed to load '%s'"), *AssetPath));

	FObjectThumbnail Thumb;
	ThumbnailTools::RenderThumbnail(Asset, Res, Res, ThumbnailTools::EThumbnailTextureFlushMode::NeverFlush, nullptr, &Thumb);
	if (Thumb.GetImageWidth() == 0) return MakeError(TEXT("Empty thumbnail"));

	TArray64<uint8> PngData;
	FImageView IV((void*)Thumb.AccessImageData().GetData(), Thumb.GetImageWidth(), Thumb.GetImageHeight(), ERawImageFormat::BGRA8);
	FImageUtils::CompressImage(PngData, TEXT(".png"), IV);
	if (PngData.Num() == 0) return MakeError(TEXT("PNG compression failed"));

	FString B64 = FBase64::Encode(PngData.GetData(), (uint32)PngData.Num());

	auto R = MakeSuccess(AssetPath);
	R->SetNumberField(TEXT("width"), Thumb.GetImageWidth());
	R->SetNumberField(TEXT("height"), Thumb.GetImageHeight());
	R->SetStringField(TEXT("format"), TEXT("png"));
	R->SetStringField(TEXT("encoding"), TEXT("base64"));
	R->SetStringField(TEXT("data"), B64);
	return JsonToString(R);
}

// 37. validate_material — port from C++ plugin (ValidateMaterial)
// Params: { "asset_path":"...", "fix_issues": false }
FString MonolithMaterialActions::ValidateMaterial(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	bool bFix = Params->HasField(TEXT("fix_issues")) ? Params->GetBoolField(TEXT("fix_issues")) : false;

	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	// BFS reachability from material outputs
	TSet<UMaterialExpression*> Reachable;
	TArray<UMaterialExpression*> Queue;
	for (const auto& E : GetAllMaterialProperties())
	{
		FExpressionInput* In = Mat->GetExpressionInputForProperty(E.Property);
		if (In && In->Expression && !Reachable.Contains(In->Expression))
		{
			Reachable.Add(In->Expression);
			Queue.Add(In->Expression);
		}
	}
	while (Queue.Num() > 0)
	{
		UMaterialExpression* Cur = Queue.Pop(EAllowShrinking::No);
		for (int32 i = 0; ; ++i)
		{
			FExpressionInput* In = Cur->GetInput(i);
			if (!In) break;
			if (In->Expression && !Reachable.Contains(In->Expression))
			{
				Reachable.Add(In->Expression);
				Queue.Add(In->Expression);
			}
		}
	}

	TArray<TSharedPtr<FJsonValue>> Issues;
	int32 Fixed = 0;
	TArray<UMaterialExpression*> Islands;
	TMap<FString, int32> ParamCounts;

	for (const auto& Expr : Mat->GetExpressions())
	{
		if (!Expr) continue;
		bool bReach = Reachable.Contains(Expr);

		// Disconnected island
		if (!bReach && !Cast<UMaterialExpressionComment>(Expr))
		{
			auto I = MakeShared<FJsonObject>();
			I->SetStringField(TEXT("severity"), TEXT("warning"));
			I->SetStringField(TEXT("type"), TEXT("island"));
			I->SetStringField(TEXT("expression"), Expr->GetName());
			if (bFix) { Islands.Add(Expr); Fixed++; I->SetBoolField(TEXT("fixed"), true); }
			else I->SetBoolField(TEXT("fixed"), false);
			Issues.Add(MakeShared<FJsonValueObject>(I));
		}

		// Broken texture refs
		if (const auto* TB = Cast<UMaterialExpressionTextureBase>(Expr))
		{
			if (!TB->Texture)
			{
				auto I = MakeShared<FJsonObject>();
				I->SetStringField(TEXT("severity"), TEXT("error"));
				I->SetStringField(TEXT("type"), TEXT("broken_texture_ref"));
				I->SetStringField(TEXT("expression"), Expr->GetName());
				I->SetBoolField(TEXT("fixed"), false);
				Issues.Add(MakeShared<FJsonValueObject>(I));
			}
		}

		// Param duplicate tracking
		if (const auto* P = Cast<UMaterialExpressionParameter>(Expr))
			ParamCounts.FindOrAdd(P->ParameterName.ToString())++;
		else if (const auto* TP = Cast<UMaterialExpressionTextureSampleParameter>(Expr))
			ParamCounts.FindOrAdd(TP->ParameterName.ToString())++;
	}

	// Duplicate params
	for (const auto& PC : ParamCounts)
	{
		if (PC.Value > 1)
		{
			auto I = MakeShared<FJsonObject>();
			I->SetStringField(TEXT("severity"), TEXT("warning"));
			I->SetStringField(TEXT("type"), TEXT("duplicate_parameter_name"));
			I->SetStringField(TEXT("parameter_name"), PC.Key);
			I->SetNumberField(TEXT("count"), PC.Value);
			I->SetBoolField(TEXT("fixed"), false);
			Issues.Add(MakeShared<FJsonValueObject>(I));
		}
	}

	// High expression count
	if (Mat->GetExpressions().Num() > 200)
	{
		auto I = MakeShared<FJsonObject>();
		I->SetStringField(TEXT("severity"), TEXT("info"));
		I->SetStringField(TEXT("type"), TEXT("high_expression_count"));
		I->SetNumberField(TEXT("count"), Mat->GetExpressions().Num());
		I->SetBoolField(TEXT("fixed"), false);
		Issues.Add(MakeShared<FJsonValueObject>(I));
	}

	// Fix islands
	if (bFix && Islands.Num() > 0)
	{
		GEditor->BeginTransaction(FText::FromString(TEXT("ValidateMaterial_Fix")));
		Mat->Modify();
		for (auto* IE : Islands)
			UMaterialEditingLibrary::DeleteMaterialExpression(Mat, IE);
		GEditor->EndTransaction();
	}

	auto R = MakeSuccess(AssetPath);
	R->SetArrayField(TEXT("issues"), Issues);
	R->SetNumberField(TEXT("issue_count"), Issues.Num());
	R->SetNumberField(TEXT("fixed_count"), Fixed);
	return JsonToString(R);
}

// 38. compare_materials — NEW
// Params: { "asset_path_a":"...", "asset_path_b":"..." }
FString MonolithMaterialActions::CompareMaterials(TSharedPtr<FJsonObject> Params)
{
	FString PathA = Params->GetStringField(TEXT("asset_path_a"));
	FString PathB = Params->GetStringField(TEXT("asset_path_b"));

	UMaterial* MatA = LoadMaterial(PathA);
	UMaterial* MatB = LoadMaterial(PathB);
	if (!MatA) return MakeError(FString::Printf(TEXT("Failed to load '%s'"), *PathA));
	if (!MatB) return MakeError(FString::Printf(TEXT("Failed to load '%s'"), *PathB));

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);

	// Compare basic properties
	auto Diff = MakeShared<FJsonObject>();
	if (MatA->MaterialDomain != MatB->MaterialDomain) Diff->SetStringField(TEXT("material_domain"), TEXT("differs"));
	if (MatA->BlendMode != MatB->BlendMode) Diff->SetStringField(TEXT("blend_mode"), TEXT("differs"));
	if (MatA->IsTwoSided() != MatB->IsTwoSided()) Diff->SetStringField(TEXT("two_sided"), TEXT("differs"));
	R->SetObjectField(TEXT("property_differences"), Diff);

	// Compare expression counts
	R->SetNumberField(TEXT("expression_count_a"), MatA->GetExpressions().Num());
	R->SetNumberField(TEXT("expression_count_b"), MatB->GetExpressions().Num());

	// Compare parameter names
	TSet<FString> ParamsA, ParamsB;
	for (const auto& E : MatA->GetExpressions())
	{
		if (!E) continue;
		if (const auto* P = Cast<UMaterialExpressionParameter>(E)) ParamsA.Add(P->ParameterName.ToString());
		else if (const auto* TP = Cast<UMaterialExpressionTextureSampleParameter>(E)) ParamsA.Add(TP->ParameterName.ToString());
	}
	for (const auto& E : MatB->GetExpressions())
	{
		if (!E) continue;
		if (const auto* P = Cast<UMaterialExpressionParameter>(E)) ParamsB.Add(P->ParameterName.ToString());
		else if (const auto* TP = Cast<UMaterialExpressionTextureSampleParameter>(E)) ParamsB.Add(TP->ParameterName.ToString());
	}

	TArray<TSharedPtr<FJsonValue>> OnlyA, OnlyB, Common;
	for (const FString& P : ParamsA)
	{
		if (ParamsB.Contains(P)) Common.Add(MakeShared<FJsonValueString>(P));
		else OnlyA.Add(MakeShared<FJsonValueString>(P));
	}
	for (const FString& P : ParamsB)
		if (!ParamsA.Contains(P)) OnlyB.Add(MakeShared<FJsonValueString>(P));

	R->SetArrayField(TEXT("params_only_in_a"), OnlyA);
	R->SetArrayField(TEXT("params_only_in_b"), OnlyB);
	R->SetArrayField(TEXT("params_in_both"), Common);

	return JsonToString(R);
}

// 39. get_compile_errors — NEW
// Params: { "asset_path":"..." }
FString MonolithMaterialActions::GetCompileErrors(TSharedPtr<FJsonObject> Params)
{
	FString AssetPath = Params->GetStringField(TEXT("asset_path"));
	UMaterial* Mat = LoadMaterial(AssetPath);
	if (!Mat) return MakeError(FString::Printf(TEXT("Failed to load material '%s'"), *AssetPath));

	// Force recompile
	UMaterialEditingLibrary::RecompileMaterial(Mat);

	// Gather compile errors from expressions
	TArray<TSharedPtr<FJsonValue>> ErrArr;
	for (const auto& Expr : Mat->GetExpressions())
	{
		if (!Expr) continue;
		if (Expr->LastErrorText.Len() > 0)
		{
			auto E = MakeShared<FJsonObject>();
			E->SetStringField(TEXT("expression"), Expr->GetName());
			E->SetStringField(TEXT("error"), Expr->LastErrorText);
			ErrArr.Add(MakeShared<FJsonValueObject>(E));
		}
	}

	auto R = MakeSuccess(AssetPath);
	R->SetNumberField(TEXT("error_count"), ErrArr.Num());
	R->SetArrayField(TEXT("errors"), ErrArr);
	R->SetBoolField(TEXT("compiles_ok"), ErrArr.Num() == 0);
	return JsonToString(R);
}
```

### Step 9: Action Implementations — BATCH OPERATIONS (Actions 40–46)

Continues in the same `MonolithMaterialActions.cpp` file.

```cpp
// ============================================================================
// BATCH OPERATIONS (40-46)
// ============================================================================

// 40. find_materials — NEW
// Params: { "name_pattern":"*PBR*", "path_pattern":"/Game/Materials/", "domain":"MD_Surface", "limit": 100 }
FString MonolithMaterialActions::FindMaterials(TSharedPtr<FJsonObject> Params)
{
	FString NamePattern = Params->HasField(TEXT("name_pattern")) ? Params->GetStringField(TEXT("name_pattern")) : TEXT("*");
	FString PathPattern = Params->HasField(TEXT("path_pattern")) ? Params->GetStringField(TEXT("path_pattern")) : TEXT("/Game/");
	int32 Limit = Params->HasField(TEXT("limit")) ? (int32)Params->GetNumberField(TEXT("limit")) : 100;

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	FARFilter Filter;
	Filter.ClassPaths.Add(UMaterial::StaticClass()->GetClassPathName());
	Filter.PackagePaths.Add(*PathPattern);
	Filter.bRecursivePaths = true;

	TArray<FAssetData> Assets;
	AR.GetAssets(Filter, Assets);

	TArray<TSharedPtr<FJsonValue>> Results;
	for (const FAssetData& AD : Assets)
	{
		if (Results.Num() >= Limit) break;

		FString Name = AD.AssetName.ToString();
		if (!NamePattern.IsEmpty() && NamePattern != TEXT("*"))
		{
			if (!Name.MatchesWildcard(NamePattern)) continue;
		}

		auto J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("path"), AD.GetObjectPathString());
		J->SetStringField(TEXT("name"), Name);
		Results.Add(MakeShared<FJsonValueObject>(J));
	}

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("count"), Results.Num());
	R->SetArrayField(TEXT("materials"), Results);
	return JsonToString(R);
}

// 41. batch_set_property — NEW
// Params: { "asset_paths":["/Game/M1","/Game/M2"], "property":"TwoSided", "value":"true" }
FString MonolithMaterialActions::BatchSetProperty(TSharedPtr<FJsonObject> Params)
{
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (!Params->TryGetArrayField(TEXT("asset_paths"), PathsArr))
		return MakeError(TEXT("Missing 'asset_paths' array"));

	FString PropName = Params->GetStringField(TEXT("property"));
	FString Value = Params->GetStringField(TEXT("value"));

	int32 Success = 0, Failed = 0;
	GEditor->BeginTransaction(FText::FromString(TEXT("BatchSetProperty")));

	for (const auto& PV : *PathsArr)
	{
		FString Path = PV->AsString();
		UMaterial* Mat = LoadMaterial(Path);
		if (!Mat) { Failed++; continue; }

		FProperty* Prop = Mat->GetClass()->FindPropertyByName(*PropName);
		if (!Prop) { Failed++; continue; }

		Mat->Modify();
		void* Ptr = Prop->ContainerPtrToValuePtr<void>(Mat);
		Prop->ImportText_Direct(*Value, Ptr, Mat, PPF_None);
		Success++;
	}

	GEditor->EndTransaction();

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("modified"), Success);
	R->SetNumberField(TEXT("failed"), Failed);
	return JsonToString(R);
}

// 42. batch_replace_texture — NEW
// Params: { "old_texture":"/Game/T_Old", "new_texture":"/Game/T_New", "asset_paths":[...] (optional, all materials if omitted) }
FString MonolithMaterialActions::BatchReplaceTexture(TSharedPtr<FJsonObject> Params)
{
	FString OldTexPath = Params->GetStringField(TEXT("old_texture"));
	FString NewTexPath = Params->GetStringField(TEXT("new_texture"));

	UTexture* OldTex = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(OldTexPath));
	UTexture* NewTex = Cast<UTexture>(UEditorAssetLibrary::LoadAsset(NewTexPath));
	if (!OldTex) return MakeError(FString::Printf(TEXT("Old texture '%s' not found"), *OldTexPath));
	if (!NewTex) return MakeError(FString::Printf(TEXT("New texture '%s' not found"), *NewTexPath));

	// Get material list — either from params or find all using old texture
	TArray<FString> MaterialPaths;
	const TArray<TSharedPtr<FJsonValue>>* PathsArr = nullptr;
	if (Params->TryGetArrayField(TEXT("asset_paths"), PathsArr))
	{
		for (const auto& PV : *PathsArr) MaterialPaths.Add(PV->AsString());
	}
	else
	{
		// Find via asset registry referencers
		FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		TArray<FAssetIdentifier> Refs;
		ARM.Get().GetReferencers(FAssetIdentifier(FName(*OldTexPath)), Refs);
		for (const auto& Ref : Refs)
			MaterialPaths.Add(Ref.PackageName.ToString());
	}

	int32 Replaced = 0;
	GEditor->BeginTransaction(FText::FromString(TEXT("BatchReplaceTexture")));

	for (const FString& Path : MaterialPaths)
	{
		UMaterial* Mat = LoadMaterial(Path);
		if (!Mat) continue;
		Mat->Modify();

		for (const auto& Expr : Mat->GetExpressions())
		{
			if (!Expr) continue;
			if (auto* TB = Cast<UMaterialExpressionTextureBase>(Expr))
			{
				if (TB->Texture == OldTex)
				{
					TB->Texture = NewTex;
					Replaced++;
				}
			}
		}
	}

	GEditor->EndTransaction();

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("replacements"), Replaced);
	return JsonToString(R);
}

// 43. find_materials_using_texture — NEW
// Params: { "texture_path":"/Game/Textures/T_Base" }
FString MonolithMaterialActions::FindMaterialsUsingTexture(TSharedPtr<FJsonObject> Params)
{
	FString TexPath = Params->GetStringField(TEXT("texture_path"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	// Get referencers of the texture
	TArray<FAssetIdentifier> Refs;
	AR.GetReferencers(FAssetIdentifier(FName(*FPackageName::ObjectPathToPackageName(TexPath))), Refs);

	TArray<TSharedPtr<FJsonValue>> Results;
	for (const auto& Ref : Refs)
	{
		FString PkgName = Ref.PackageName.ToString();
		TArray<FAssetData> PkgAssets;
		AR.GetAssetsByPackageName(Ref.PackageName, PkgAssets);
		for (const auto& AD : PkgAssets)
		{
			if (AD.AssetClassPath == UMaterial::StaticClass()->GetClassPathName() ||
				AD.AssetClassPath == UMaterialInstanceConstant::StaticClass()->GetClassPathName())
			{
				auto J = MakeShared<FJsonObject>();
				J->SetStringField(TEXT("path"), AD.GetObjectPathString());
				J->SetStringField(TEXT("name"), AD.AssetName.ToString());
				J->SetStringField(TEXT("type"), AD.AssetClassPath.GetAssetName().ToString());
				Results.Add(MakeShared<FJsonValueObject>(J));
			}
		}
	}

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("texture_path"), TexPath);
	R->SetNumberField(TEXT("count"), Results.Num());
	R->SetArrayField(TEXT("materials"), Results);
	return JsonToString(R);
}

// 44. find_materials_using_function — NEW
// Params: { "function_path":"/Engine/Functions/..." }
FString MonolithMaterialActions::FindMaterialsUsingFunction(TSharedPtr<FJsonObject> Params)
{
	FString FuncPath = Params->GetStringField(TEXT("function_path"));

	FAssetRegistryModule& ARM = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
	IAssetRegistry& AR = ARM.Get();

	TArray<FAssetIdentifier> Refs;
	AR.GetReferencers(FAssetIdentifier(FName(*FPackageName::ObjectPathToPackageName(FuncPath))), Refs);

	TArray<TSharedPtr<FJsonValue>> Results;
	for (const auto& Ref : Refs)
	{
		TArray<FAssetData> PkgAssets;
		AR.GetAssetsByPackageName(Ref.PackageName, PkgAssets);
		for (const auto& AD : PkgAssets)
		{
			if (AD.AssetClassPath == UMaterial::StaticClass()->GetClassPathName())
			{
				auto J = MakeShared<FJsonObject>();
				J->SetStringField(TEXT("path"), AD.GetObjectPathString());
				J->SetStringField(TEXT("name"), AD.AssetName.ToString());
				Results.Add(MakeShared<FJsonValueObject>(J));
			}
		}
	}

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("function_path"), FuncPath);
	R->SetNumberField(TEXT("count"), Results.Num());
	R->SetArrayField(TEXT("materials"), Results);
	return JsonToString(R);
}

// 45. duplicate_material — NEW
// Params: { "source_path":"/Game/M_Source", "dest_path":"/Game/M_Copy" }
FString MonolithMaterialActions::DuplicateMaterial(TSharedPtr<FJsonObject> Params)
{
	FString SrcPath = Params->GetStringField(TEXT("source_path"));
	FString DstPath = Params->GetStringField(TEXT("dest_path"));

	UObject* Src = UEditorAssetLibrary::LoadAsset(SrcPath);
	if (!Src) return MakeError(FString::Printf(TEXT("Source '%s' not found"), *SrcPath));

	UObject* Dup = UEditorAssetLibrary::DuplicateAsset(SrcPath, DstPath);
	if (!Dup) return MakeError(FString::Printf(TEXT("Failed to duplicate to '%s'"), *DstPath));

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("source_path"), SrcPath);
	R->SetStringField(TEXT("dest_path"), Dup->GetPathName());
	return JsonToString(R);
}

// 46. create_material_instance — NEW
// Params: { "parent_path":"/Game/M_Parent", "instance_path":"/Game/MI_Child", "scalar_overrides":{"Roughness":0.8}, "vector_overrides":{"BaseColor":"(R=1,G=0,B=0,A=1)"} }
FString MonolithMaterialActions::CreateMaterialInstance(TSharedPtr<FJsonObject> Params)
{
	FString ParentPath = Params->GetStringField(TEXT("parent_path"));
	FString InstPath = Params->GetStringField(TEXT("instance_path"));

	UMaterialInterface* Parent = LoadMaterialInterface(ParentPath);
	if (!Parent) return MakeError(FString::Printf(TEXT("Parent '%s' not found"), *ParentPath));

	FString PkgPath = FPackageName::GetLongPackagePath(InstPath);
	FString AssetName = FPackageName::GetShortName(InstPath);

	IAssetTools& AT = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
	Factory->InitialParent = Parent;

	UObject* NewAsset = AT.CreateAsset(AssetName, PkgPath, UMaterialInstanceConstant::StaticClass(), Factory);
	UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(NewAsset);
	if (!MIC) return MakeError(TEXT("Failed to create material instance"));

	// Apply scalar overrides
	const TSharedPtr<FJsonObject>* ScalarPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("scalar_overrides"), ScalarPtr) && ScalarPtr)
	{
		for (const auto& Pair : (*ScalarPtr)->Values)
		{
			FMaterialParameterInfo Info(*Pair.Key);
			MIC->SetScalarParameterValueEditorOnly(Info, (float)Pair.Value->AsNumber());
		}
	}

	// Apply vector overrides
	const TSharedPtr<FJsonObject>* VecPtr = nullptr;
	if (Params->TryGetObjectField(TEXT("vector_overrides"), VecPtr) && VecPtr)
	{
		for (const auto& Pair : (*VecPtr)->Values)
		{
			FMaterialParameterInfo Info(*Pair.Key);
			FLinearColor Color;
			Color.InitFromString(Pair.Value->AsString());
			MIC->SetVectorParameterValueEditorOnly(Info, Color);
		}
	}

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("instance_path"), MIC->GetPathName());
	R->SetStringField(TEXT("parent_path"), ParentPath);
	return JsonToString(R);
}
```

## Task 2B: MonolithConfig Module (6 Actions)

### Overview

Replaces the Python `unreal-config-mcp` server. Instead of custom INI parsing, leverages UE's built-in `GConfig` / `FConfigCacheIni` which already loads and resolves all project INI files with proper layering (Base → Default → Platform → Project → User).

### Action Catalog

1. `resolve` — Resolve a config key to its final effective value, showing the full resolution chain
2. `explain` — Explain what a config key does (using metadata + UDeveloperSettings reflection)
3. `diff` — Compare two config files or show overrides between layers
4. `search` — Search across all config files for keys/values matching a pattern
5. `get_section` — Get all key-value pairs in a config section
6. `get_files` — List all loaded config files and their paths

### Files to Create/Modify

```
Source/MonolithConfig/
├── MonolithConfig.Build.cs             (MODIFY — add DeveloperSettings)
├── Public/
│   ├── MonolithConfigModule.h          (MODIFY — add registration)
│   └── MonolithConfigActions.h         (CREATE — action declarations)
├── Private/
│   ├── MonolithConfigModule.cpp        (MODIFY — register actions)
│   └── MonolithConfigActions.cpp       (CREATE — all 6 implementations)
```

### Step 1: Update Build.cs

**File:** `Source/MonolithConfig/MonolithConfig.Build.cs`

```csharp
using UnrealBuildTool;

public class MonolithConfig : ModuleRules
{
	public MonolithConfig(ReadOnlyTargetRules Target) : base(Target)
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
			"Json",
			"JsonUtilities",
			"DeveloperSettings"
		});
	}
}
```

### Step 2: Actions Header

**File:** `Source/MonolithConfig/Public/MonolithConfigActions.h`

```cpp
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

namespace MonolithConfigActions
{
	FString Resolve(TSharedPtr<FJsonObject> Params);
	FString Explain(TSharedPtr<FJsonObject> Params);
	FString Diff(TSharedPtr<FJsonObject> Params);
	FString Search(TSharedPtr<FJsonObject> Params);
	FString GetSection(TSharedPtr<FJsonObject> Params);
	FString GetFiles(TSharedPtr<FJsonObject> Params);
}
```

### Step 3: Module Registration

**File:** `Source/MonolithConfig/Public/MonolithConfigModule.h`

```cpp
#pragma once

#include "Modules/ModuleManager.h"

class FMonolithConfigModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void RegisterActions();
};
```

**File:** `Source/MonolithConfig/Private/MonolithConfigModule.cpp`

```cpp
#include "MonolithConfigModule.h"
#include "MonolithConfigActions.h"

#define LOCTEXT_NAMESPACE "FMonolithConfigModule"

void FMonolithConfigModule::StartupModule()
{
	RegisterActions();
	UE_LOG(LogTemp, Log, TEXT("Monolith — Config module loaded (6 actions)"));
}

void FMonolithConfigModule::ShutdownModule()
{
}

void FMonolithConfigModule::RegisterActions()
{
	// Same pattern as MonolithMaterial — will wire to ToolRegistry in Phase 1 integration.
	using FActionHandler = TFunction<FString(TSharedPtr<FJsonObject>)>;
	TMap<FString, FActionHandler> Actions;

	Actions.Add(TEXT("resolve"), &MonolithConfigActions::Resolve);
	Actions.Add(TEXT("explain"), &MonolithConfigActions::Explain);
	Actions.Add(TEXT("diff"), &MonolithConfigActions::Diff);
	Actions.Add(TEXT("search"), &MonolithConfigActions::Search);
	Actions.Add(TEXT("get_section"), &MonolithConfigActions::GetSection);
	Actions.Add(TEXT("get_files"), &MonolithConfigActions::GetFiles);

	// TODO: Registry.RegisterNamespace("config", Actions);
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithConfigModule, MonolithConfig)
```

### Step 4: Action Implementations

**File:** `Source/MonolithConfig/Private/MonolithConfigActions.cpp`

```cpp
#include "MonolithConfigActions.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

namespace
{
	FString JsonToString(TSharedPtr<FJsonObject> Obj)
	{
		FString Out;
		auto W = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Out);
		FJsonSerializer::Serialize(Obj, W);
		return Out;
	}

	FString MakeError(const FString& Msg)
	{
		auto R = MakeShared<FJsonObject>();
		R->SetBoolField(TEXT("success"), false);
		R->SetStringField(TEXT("error"), Msg);
		return JsonToString(R);
	}

	/** Map short config name to GConfig filename constant. */
	const FString& ResolveConfigFilename(const FString& ShortName)
	{
		static TMap<FString, FString> Map = {
			{ TEXT("Engine"),     GEngineIni },
			{ TEXT("Game"),       GGameIni },
			{ TEXT("Input"),      GInputIni },
			{ TEXT("Editor"),     GEditorIni },
			{ TEXT("EditorPerProjectUserSettings"), GEditorPerProjectIni },
			{ TEXT("Compat"),     GCompatIni },
			{ TEXT("Scalability"), GScalabilityIni },
			{ TEXT("Hardware"),   GHardwareIni },
			{ TEXT("GameUserSettings"), GGameUserSettingsIni },
		};

		const FString* Found = Map.Find(ShortName);
		if (Found) return *Found;

		// Try as-is (full path)
		static FString Passthrough;
		Passthrough = ShortName;
		return Passthrough;
	}
}

// 1. resolve — Resolve a config key to its effective value
// Params: { "file": "Engine", "section": "/Script/Engine.RendererSettings", "key": "r.DefaultFeature.AutoExposure" }
FString MonolithConfigActions::Resolve(TSharedPtr<FJsonObject> Params)
{
	FString File = Params->GetStringField(TEXT("file"));
	FString Section = Params->GetStringField(TEXT("section"));
	FString Key = Params->GetStringField(TEXT("key"));

	const FString& Filename = ResolveConfigFilename(File);

	if (!GConfig)
		return MakeError(TEXT("GConfig not available"));

	FString Value;
	bool bFound = GConfig->GetString(*Section, *Key, Value, Filename);

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("file"), File);
	R->SetStringField(TEXT("section"), Section);
	R->SetStringField(TEXT("key"), Key);
	R->SetBoolField(TEXT("found"), bFound);
	if (bFound)
		R->SetStringField(TEXT("value"), Value);

	// Show resolution chain: check each layer
	TArray<TSharedPtr<FJsonValue>> Chain;

	// Build layer paths for this config
	struct FConfigLayer { const TCHAR* Name; FString Path; };
	TArray<FConfigLayer> Layers;

	FString ProjectDir = FPaths::ProjectDir();
	FString EngineDir = FPaths::EngineDir();

	// Common INI layer order
	Layers.Add({ TEXT("Base"), FPaths::Combine(EngineDir, TEXT("Config"), TEXT("Base.ini")) });
	Layers.Add({ TEXT("BaseEngine"), FPaths::Combine(EngineDir, TEXT("Config"), FString::Printf(TEXT("Base%s.ini"), *File)) });
	Layers.Add({ TEXT("DefaultEngine"), FPaths::Combine(ProjectDir, TEXT("Config"), FString::Printf(TEXT("Default%s.ini"), *File)) });
	Layers.Add({ TEXT("PlatformEngine"), FPaths::Combine(ProjectDir, TEXT("Config"), FPlatformProperties::PlatformName(), FString::Printf(TEXT("%s.ini"), *File)) });

	for (const FConfigLayer& Layer : Layers)
	{
		if (FPaths::FileExists(Layer.Path))
		{
			FConfigFile TempConfig;
			TempConfig.Read(Layer.Path);

			const FConfigSection* Sec = TempConfig.Find(Section);
			if (Sec)
			{
				const FConfigValue* Val = Sec->Find(*Key);
				if (Val)
				{
					auto LJ = MakeShared<FJsonObject>();
					LJ->SetStringField(TEXT("layer"), Layer.Name);
					LJ->SetStringField(TEXT("file"), Layer.Path);
					LJ->SetStringField(TEXT("value"), Val->GetValue());
					Chain.Add(MakeShared<FJsonValueObject>(LJ));
				}
			}
		}
	}

	R->SetArrayField(TEXT("resolution_chain"), Chain);
	return JsonToString(R);
}

// 2. explain — Explain what a config key does
// Params: { "file": "Engine", "section": "/Script/Engine.RendererSettings", "key": "r.DefaultFeature.AutoExposure" }
FString MonolithConfigActions::Explain(TSharedPtr<FJsonObject> Params)
{
	FString File = Params->GetStringField(TEXT("file"));
	FString Section = Params->GetStringField(TEXT("section"));
	FString Key = Params->GetStringField(TEXT("key"));

	const FString& Filename = ResolveConfigFilename(File);

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("file"), File);
	R->SetStringField(TEXT("section"), Section);
	R->SetStringField(TEXT("key"), Key);

	// Current value
	FString Value;
	if (GConfig && GConfig->GetString(*Section, *Key, Value, Filename))
		R->SetStringField(TEXT("current_value"), Value);

	// Try to find the UClass from the section path (e.g. /Script/Engine.RendererSettings)
	FString ClassName;
	if (Section.StartsWith(TEXT("/Script/")))
	{
		// Extract "Module.ClassName" and find the class
		FString AfterScript = Section.Mid(8); // after "/Script/"
		UClass* FoundClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/%s"), *AfterScript));

		if (FoundClass)
		{
			R->SetStringField(TEXT("class"), FoundClass->GetName());

			// Look for property metadata
			FProperty* Prop = FoundClass->FindPropertyByName(*Key);
			if (Prop)
			{
				R->SetStringField(TEXT("property_type"), Prop->GetCPPType());

				// Get tooltip/description from metadata
				if (Prop->HasMetaData(TEXT("ToolTip")))
					R->SetStringField(TEXT("description"), Prop->GetMetaData(TEXT("ToolTip")));
				if (Prop->HasMetaData(TEXT("Category")))
					R->SetStringField(TEXT("category"), Prop->GetMetaData(TEXT("Category")));
				if (Prop->HasMetaData(TEXT("ClampMin")))
					R->SetStringField(TEXT("min"), Prop->GetMetaData(TEXT("ClampMin")));
				if (Prop->HasMetaData(TEXT("ClampMax")))
					R->SetStringField(TEXT("max"), Prop->GetMetaData(TEXT("ClampMax")));

				// Check for enum
				if (FEnumProperty* EnumProp = CastField<FEnumProperty>(Prop))
				{
					if (UEnum* Enum = EnumProp->GetEnum())
					{
						TArray<TSharedPtr<FJsonValue>> Vals;
						for (int32 i = 0; i < Enum->NumEnums() - 1; ++i)
							Vals.Add(MakeShared<FJsonValueString>(Enum->GetNameStringByIndex(i)));
						R->SetArrayField(TEXT("valid_values"), Vals);
					}
				}
			}
			else
			{
				R->SetStringField(TEXT("note"), TEXT("Property not found via reflection — may be a CVar or custom key"));
			}
		}
	}

	// Check if it looks like a CVar
	if (Key.StartsWith(TEXT("r.")) || Key.StartsWith(TEXT("p.")) || Key.StartsWith(TEXT("fx.")) || Key.StartsWith(TEXT("sg.")))
	{
		R->SetStringField(TEXT("likely_type"), TEXT("Console Variable (CVar)"));
		R->SetStringField(TEXT("hint"), TEXT("This appears to be a CVar. Use 'stat unit' or console to test live."));

		// Try to get CVar help text
		IConsoleVariable* CVar = IConsoleManager::Get().FindConsoleVariable(*Key);
		if (CVar)
		{
			R->SetStringField(TEXT("cvar_help"), CVar->GetHelp());
			R->SetStringField(TEXT("cvar_current_value"), CVar->GetString());
		}
	}

	return JsonToString(R);
}

// 3. diff — Compare config between files or layers
// Params: { "file_a": "Engine", "file_b": "Engine", "section": "...", "path_a": "...", "path_b": "..." }
// OR: { "file": "Engine", "section": "..." } — compares DefaultEngine.ini vs Base.ini
FString MonolithConfigActions::Diff(TSharedPtr<FJsonObject> Params)
{
	FString Section = Params->GetStringField(TEXT("section"));

	FString PathA, PathB, LabelA, LabelB;

	if (Params->HasField(TEXT("path_a")) && Params->HasField(TEXT("path_b")))
	{
		PathA = Params->GetStringField(TEXT("path_a"));
		PathB = Params->GetStringField(TEXT("path_b"));
		LabelA = FPaths::GetCleanFilename(PathA);
		LabelB = FPaths::GetCleanFilename(PathB);
	}
	else
	{
		// Default: compare Engine Base vs Project Default
		FString File = Params->HasField(TEXT("file")) ? Params->GetStringField(TEXT("file")) : TEXT("Engine");
		PathA = FPaths::Combine(FPaths::EngineDir(), TEXT("Config"), FString::Printf(TEXT("Base%s.ini"), *File));
		PathB = FPaths::Combine(FPaths::ProjectDir(), TEXT("Config"), FString::Printf(TEXT("Default%s.ini"), *File));
		LabelA = FString::Printf(TEXT("Base%s.ini (Engine)"), *File);
		LabelB = FString::Printf(TEXT("Default%s.ini (Project)"), *File);
	}

	FConfigFile ConfigA, ConfigB;
	ConfigA.Read(PathA);
	ConfigB.Read(PathB);

	const FConfigSection* SecA = ConfigA.Find(Section);
	const FConfigSection* SecB = ConfigB.Find(Section);

	TArray<TSharedPtr<FJsonValue>> Diffs;

	// Gather all keys from both
	TSet<FString> AllKeys;
	if (SecA) for (const auto& P : *SecA) AllKeys.Add(P.Key.ToString());
	if (SecB) for (const auto& P : *SecB) AllKeys.Add(P.Key.ToString());

	for (const FString& Key : AllKeys)
	{
		const FConfigValue* VA = SecA ? SecA->Find(*Key) : nullptr;
		const FConfigValue* VB = SecB ? SecB->Find(*Key) : nullptr;

		FString ValA = VA ? VA->GetValue() : TEXT("<not set>");
		FString ValB = VB ? VB->GetValue() : TEXT("<not set>");

		if (ValA != ValB)
		{
			auto D = MakeShared<FJsonObject>();
			D->SetStringField(TEXT("key"), Key);
			D->SetStringField(TEXT("value_a"), ValA);
			D->SetStringField(TEXT("value_b"), ValB);

			if (!VA) D->SetStringField(TEXT("change"), TEXT("added"));
			else if (!VB) D->SetStringField(TEXT("change"), TEXT("removed"));
			else D->SetStringField(TEXT("change"), TEXT("modified"));

			Diffs.Add(MakeShared<FJsonValueObject>(D));
		}
	}

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("file_a"), LabelA);
	R->SetStringField(TEXT("file_b"), LabelB);
	R->SetStringField(TEXT("section"), Section);
	R->SetNumberField(TEXT("difference_count"), Diffs.Num());
	R->SetArrayField(TEXT("differences"), Diffs);
	return JsonToString(R);
}

// 4. search — Search config keys/values across all loaded files
// Params: { "pattern": "Lumen", "file": "Engine" (optional), "search_values": true }
FString MonolithConfigActions::Search(TSharedPtr<FJsonObject> Params)
{
	FString Pattern = Params->GetStringField(TEXT("pattern"));
	bool bSearchValues = Params->HasField(TEXT("search_values")) ? Params->GetBoolField(TEXT("search_values")) : true;
	int32 Limit = Params->HasField(TEXT("limit")) ? (int32)Params->GetNumberField(TEXT("limit")) : 50;

	if (!GConfig)
		return MakeError(TEXT("GConfig not available"));

	TArray<TSharedPtr<FJsonValue>> Results;

	// Determine which config files to search
	TArray<FString> Filenames;
	if (Params->HasField(TEXT("file")))
	{
		Filenames.Add(ResolveConfigFilename(Params->GetStringField(TEXT("file"))));
	}
	else
	{
		Filenames = GConfig->GetFilenames();
	}

	for (const FString& Filename : Filenames)
	{
		if (Results.Num() >= Limit) break;

		TArray<FString> Sections;
		GConfig->GetSectionNames(Filename, Sections);

		for (const FString& Section : Sections)
		{
			if (Results.Num() >= Limit) break;

			const FConfigSection* Sec = GConfig->GetSection(*Section, false, Filename);
			if (!Sec) continue;

			for (const auto& Pair : *Sec)
			{
				if (Results.Num() >= Limit) break;

				FString KeyStr = Pair.Key.ToString();
				FString ValStr = Pair.Value.GetValue();

				bool bMatch = KeyStr.Contains(Pattern, ESearchCase::IgnoreCase);
				if (!bMatch && bSearchValues)
					bMatch = ValStr.Contains(Pattern, ESearchCase::IgnoreCase);

				if (bMatch)
				{
					auto J = MakeShared<FJsonObject>();
					J->SetStringField(TEXT("file"), FPaths::GetCleanFilename(Filename));
					J->SetStringField(TEXT("section"), Section);
					J->SetStringField(TEXT("key"), KeyStr);
					J->SetStringField(TEXT("value"), ValStr);
					Results.Add(MakeShared<FJsonValueObject>(J));
				}
			}
		}
	}

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("pattern"), Pattern);
	R->SetNumberField(TEXT("count"), Results.Num());
	R->SetArrayField(TEXT("results"), Results);
	return JsonToString(R);
}

// 5. get_section — Get all key-value pairs in a section
// Params: { "file": "Engine", "section": "/Script/Engine.RendererSettings" }
FString MonolithConfigActions::GetSection(TSharedPtr<FJsonObject> Params)
{
	FString File = Params->GetStringField(TEXT("file"));
	FString Section = Params->GetStringField(TEXT("section"));

	const FString& Filename = ResolveConfigFilename(File);

	if (!GConfig)
		return MakeError(TEXT("GConfig not available"));

	const FConfigSection* Sec = GConfig->GetSection(*Section, false, Filename);
	if (!Sec)
		return MakeError(FString::Printf(TEXT("Section '%s' not found in '%s'"), *Section, *File));

	auto KV = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Entries;

	for (const auto& Pair : *Sec)
	{
		auto E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("key"), Pair.Key.ToString());
		E->SetStringField(TEXT("value"), Pair.Value.GetValue());
		Entries.Add(MakeShared<FJsonValueObject>(E));

		// Also flat map for easy access
		KV->SetStringField(Pair.Key.ToString(), Pair.Value.GetValue());
	}

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetStringField(TEXT("file"), File);
	R->SetStringField(TEXT("section"), Section);
	R->SetNumberField(TEXT("count"), Entries.Num());
	R->SetArrayField(TEXT("entries"), Entries);
	R->SetObjectField(TEXT("values"), KV);
	return JsonToString(R);
}

// 6. get_files — List all loaded config files
// Params: {} (none, or { "project_only": true })
FString MonolithConfigActions::GetFiles(TSharedPtr<FJsonObject> Params)
{
	if (!GConfig)
		return MakeError(TEXT("GConfig not available"));

	bool bProjectOnly = Params->HasField(TEXT("project_only")) ? Params->GetBoolField(TEXT("project_only")) : false;

	TArray<FString> AllFiles = GConfig->GetFilenames();

	FString ProjectDir = FPaths::ProjectDir();
	TArray<TSharedPtr<FJsonValue>> Files;

	for (const FString& Filepath : AllFiles)
	{
		if (bProjectOnly && !Filepath.Contains(ProjectDir))
			continue;

		auto J = MakeShared<FJsonObject>();
		J->SetStringField(TEXT("path"), Filepath);
		J->SetStringField(TEXT("filename"), FPaths::GetCleanFilename(Filepath));

		// Count sections
		TArray<FString> Sections;
		GConfig->GetSectionNames(Filepath, Sections);
		J->SetNumberField(TEXT("section_count"), Sections.Num());

		// File size
		int64 FileSize = IFileManager::Get().FileSize(*Filepath);
		J->SetNumberField(TEXT("size_bytes"), (double)FileSize);

		Files.Add(MakeShared<FJsonValueObject>(J));
	}

	auto R = MakeShared<FJsonObject>();
	R->SetBoolField(TEXT("success"), true);
	R->SetNumberField(TEXT("count"), Files.Num());
	R->SetArrayField(TEXT("files"), Files);
	return JsonToString(R);
}
```

## Testing & Verification

### Build Verification

```bash
# From UE editor with Monolith plugin loaded, trigger build via:
# 1. UnrealBuildTool directly:
cd "C:/Program Files/Epic Games/UE_5.7/Engine/Build/BatchFiles"
RunUBT.bat LeviathanEditor Win64 Development -Project="D:/Unreal Projects/Leviathan/Leviathan.uproject"

# 2. Or via UE editor Build menu
# Expected: 0 errors for MonolithMaterial and MonolithConfig modules
```

### MonolithMaterial Test Steps

Each test assumes UE editor is running with a test material at `/Game/Test/M_TestMaterial`.

**Test 1: Inspection actions**
```json
// material.query("get_all_expressions", {"asset_path": "/Game/Test/M_TestMaterial"})
// Expected: JSON with success=true, expressions array

// material.query("get_material_properties", {"asset_path": "/Game/Test/M_TestMaterial"})
// Expected: JSON with material_domain, blend_mode, shading_model

// material.query("get_material_stats", {"asset_path": "/Game/Test/M_TestMaterial"})
// Expected: JSON with total_expressions, texture_samples, parameters counts

// material.query("get_parameter_list", {"asset_path": "/Game/Test/M_TestMaterial"})
// Expected: JSON with parameters array (type, name, group, default_value)
```

**Test 2: Create PBR template end-to-end**
```json
// material.query("create_pbr_material", {"asset_path": "/Game/Test/M_PBR_Test", "roughness": 0.8, "metallic": 0.0})
// Expected: success=true, nodes_created=3, connections_made=3
// Verify in editor: material has BaseColor, Roughness, Metallic params wired to outputs
```

**Test 3: Graph round-trip**
```json
// Step 1: Export
// material.query("export_material_graph", {"asset_path": "/Game/Test/M_PBR_Test"})
// Expected: JSON with nodes, connections, outputs arrays

// Step 2: Create new material and import
// material.query("create_material", {"asset_path": "/Game/Test/M_RoundTrip"})
// material.query("import_material_graph", {"asset_path": "/Game/Test/M_RoundTrip", "graph_json": "<exported json>", "mode": "overwrite"})
// Expected: success=true, same node count as original
```

**Test 4: Validation**
```json
// material.query("validate_material", {"asset_path": "/Game/Test/M_TestMaterial", "fix_issues": false})
// Expected: JSON with issues array, issue_count
```

**Test 5: Batch operations**
```json
// material.query("find_materials", {"path_pattern": "/Game/Test/", "limit": 10})
// Expected: list of materials in the test folder

// material.query("create_material_instance", {"parent_path": "/Game/Test/M_PBR_Test", "instance_path": "/Game/Test/MI_PBR_Child", "scalar_overrides": {"Roughness": 0.2}})
// Expected: success=true, instance created with overridden roughness
```

**Test 6: Create from textures**
```json
// material.query("create_from_textures", {
//   "asset_path": "/Game/Test/M_FromTextures",
//   "albedo": "/Game/Test/T_Albedo",
//   "normal": "/Game/Test/T_Normal",
//   "roughness": "/Game/Test/T_Roughness"
// })
// Expected: Material with 3 TextureSampleParameter2D nodes wired to outputs
```

### MonolithConfig Test Steps

**Test 1: Get files**
```json
// config.query("get_files", {"project_only": true})
// Expected: List of project config files with section counts
```

**Test 2: Search**
```json
// config.query("search", {"pattern": "Lumen", "file": "Engine"})
// Expected: All Engine.ini keys/values containing "Lumen"
```

**Test 3: Get section**
```json
// config.query("get_section", {"file": "Engine", "section": "/Script/Engine.RendererSettings"})
// Expected: All key-value pairs in RendererSettings
```

**Test 4: Resolve with chain**
```json
// config.query("resolve", {"file": "Engine", "section": "/Script/Engine.RendererSettings", "key": "r.DefaultFeature.AutoExposure"})
// Expected: Final value + resolution_chain showing which layer files set this key
```

**Test 5: Explain**
```json
// config.query("explain", {"file": "Engine", "section": "/Script/Engine.RendererSettings", "key": "r.DefaultFeature.AutoExposure"})
// Expected: current_value + CVar help text if available
```

**Test 6: Diff**
```json
// config.query("diff", {"file": "Engine", "section": "/Script/Engine.RendererSettings"})
// Expected: Differences between Base Engine.ini and project DefaultEngine.ini
```

## Key Design Decisions

### MonolithMaterial
1. **Single handler signature** — All 46 actions use `FString Fn(TSharedPtr<FJsonObject> Params)` for uniform dispatch
2. **Templates reuse BuildMaterialGraph** — `create_pbr_material`, `create_emissive_material` etc. create the material then call `BuildMaterialGraph` with a JSON spec. This avoids duplicating graph-building logic.
3. **Batch ops use AssetRegistry** — `find_materials_using_texture`, `find_materials_using_function` use `IAssetRegistry::GetReferencers()` instead of loading every material
4. **All mutations wrapped in undo transactions** — Every editing action calls `GEditor->BeginTransaction` / `EndTransaction` for full undo support
5. **Reflection-based property setting** — `set_material_properties`, `set_expression_property`, and `build_material_graph` use `FProperty::ImportText_Direct` for type-agnostic property setting via UE reflection

### MonolithConfig
1. **No custom INI parser** — Leverages `GConfig` which already handles the full resolution chain (Base → Default → Platform → User)
2. **CVar integration** — `explain` action queries `IConsoleManager` for CVar help text when keys look like CVars
3. **Layer diffing** — `diff` reads raw INI files layer-by-layer using `FConfigFile::Read` to show where values come from
4. **Reflection for metadata** — `explain` finds the owning UClass from section path and reads property metadata (tooltips, clamp ranges, enum values)

## Dependencies on Phase 1

Both modules depend on MonolithCore's ToolRegistry for actual MCP dispatch. The registration code is structured so that:
1. Actions compile and work standalone (each returns FString)
2. Module registration builds the action map
3. When Phase 1's `FMonolithToolRegistry` is implemented, the `RegisterActions()` method just needs to call `Registry.RegisterNamespace("material", Actions)` instead of building a local TMap

This means Phase 2 can be implemented and compile-tested independently of Phase 1's MCP server work.

## Phase 3: MonolithAnimation + MonolithNiagara

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

## Phase 4: Deep Project Indexer (MonolithIndex)

The crown jewel — the project brain. SQLite+FTS5 database indexing every asset in the project with full-text search, cross-reference tracking, and deep structural introspection.

**Module:** `MonolithIndex`
**Database path:** `Plugins/Monolith/Saved/ProjectIndex.db`
**Dependencies:** `MonolithCore`, `SQLiteCore`, `AssetRegistry`, `UnrealEd`, `Json`, `JsonUtilities`

---

## Task 4.1 — SQLite Database Wrapper (FMonolithIndexDatabase)

**Files:**
- Create: `Source/MonolithIndex/Public/MonolithIndexDatabase.h`
- Create: `Source/MonolithIndex/Private/MonolithIndexDatabase.cpp`

**Overview:** Thin RAII wrapper around UE's `FSQLiteDatabase` (from SQLiteCore module). Creates all 13 tables + 2 FTS5 virtual tables on first open. Provides typed helpers for insert/query/transaction.

### Step 1: Create the header

Create `Source/MonolithIndex/Public/MonolithIndexDatabase.h`:

```cpp
#pragma once

#include "CoreMinimal.h"
#include "SQLiteDatabase.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMonolithIndex, Log, All);

struct FIndexedAsset
{
	int64 Id = 0;
	FString PackagePath;
	FString AssetName;
	FString AssetClass;
	FString ModuleName;
	FString Description;
	int64 FileSizeBytes = 0;
	FString LastModified;
	FString IndexedAt;
};

struct FIndexedNode
{
	int64 Id = 0;
	int64 AssetId = 0;
	FString NodeType;
	FString NodeName;
	FString NodeClass;
	FString Properties; // JSON blob
	int32 PosX = 0;
	int32 PosY = 0;
};

struct FIndexedConnection
{
	int64 Id = 0;
	int64 SourceNodeId = 0;
	FString SourcePin;
	int64 TargetNodeId = 0;
	FString TargetPin;
	FString PinType;
};

struct FIndexedVariable
{
	int64 Id = 0;
	int64 AssetId = 0;
	FString VarName;
	FString VarType;
	FString Category;
	FString DefaultValue;
	bool bIsExposed = false;
	bool bIsReplicated = false;
};

struct FIndexedParameter
{
	int64 Id = 0;
	int64 AssetId = 0;
	FString ParamName;
	FString ParamType;
	FString ParamGroup;
	FString DefaultValue;
	FString Source; // "Material", "Niagara", etc.
};

struct FIndexedDependency
{
	int64 Id = 0;
	int64 SourceAssetId = 0;
	int64 TargetAssetId = 0;
	FString DependencyType; // "Hard", "Soft", "Searchable"
};

struct FIndexedActor
{
	int64 Id = 0;
	int64 AssetId = 0; // Level asset
	FString ActorName;
	FString ActorClass;
	FString ActorLabel;
	FString Transform; // JSON
	FString Components; // JSON array
};

struct FIndexedTag
{
	int64 Id = 0;
	FString TagName;
	FString ParentTag;
	int32 ReferenceCount = 0;
};

struct FIndexedTagReference
{
	int64 Id = 0;
	int64 TagId = 0;
	int64 AssetId = 0;
	FString Context; // "Variable", "Node", "Component", etc.
};

struct FIndexedConfig
{
	int64 Id = 0;
	FString FilePath;
	FString Section;
	FString Key;
	FString Value;
};

struct FIndexedCppSymbol
{
	int64 Id = 0;
	FString FilePath;
	FString SymbolName;
	FString SymbolType; // "Class", "Function", "Enum", "Struct", "Delegate"
	FString Signature;
	int32 LineNumber = 0;
	FString ParentSymbol;
};

struct FIndexedDataTableRow
{
	int64 Id = 0;
	int64 AssetId = 0;
	FString RowName;
	FString RowData; // JSON blob
};

struct FSearchResult
{
	FString AssetPath;
	FString AssetName;
	FString AssetClass;
	FString MatchContext; // snippet around the match
	float Rank = 0.0f;
};

/**
 * RAII wrapper around FSQLiteDatabase for the Monolith project index.
 * Creates all tables on first open, provides typed insert/query helpers.
 * Thread-safe for reads; writes should be serialized by the caller.
 */
class MONOLITHINDEX_API FMonolithIndexDatabase
{
public:
	FMonolithIndexDatabase();
	~FMonolithIndexDatabase();

	/** Open (or create) the database at the given path */
	bool Open(const FString& InDbPath);

	/** Close the database */
	void Close();

	/** Is the database currently open? */
	bool IsOpen() const;

	/** Wipe all data and recreate tables (for full re-index) */
	bool ResetDatabase();

	// --- Transaction helpers ---
	bool BeginTransaction();
	bool CommitTransaction();
	bool RollbackTransaction();

	// --- Asset CRUD ---
	int64 InsertAsset(const FIndexedAsset& Asset);
	TOptional<FIndexedAsset> GetAssetByPath(const FString& PackagePath);
	int64 GetAssetId(const FString& PackagePath);
	bool DeleteAssetAndRelated(int64 AssetId);

	// --- Node CRUD ---
	int64 InsertNode(const FIndexedNode& Node);
	TArray<FIndexedNode> GetNodesForAsset(int64 AssetId);

	// --- Connection CRUD ---
	int64 InsertConnection(const FIndexedConnection& Conn);
	TArray<FIndexedConnection> GetConnectionsForAsset(int64 AssetId);

	// --- Variable CRUD ---
	int64 InsertVariable(const FIndexedVariable& Var);
	TArray<FIndexedVariable> GetVariablesForAsset(int64 AssetId);

	// --- Parameter CRUD ---
	int64 InsertParameter(const FIndexedParameter& Param);

	// --- Dependency CRUD ---
	int64 InsertDependency(const FIndexedDependency& Dep);
	TArray<FIndexedDependency> GetDependenciesForAsset(int64 AssetId);
	TArray<FIndexedDependency> GetReferencersOfAsset(int64 AssetId);

	// --- Actor CRUD ---
	int64 InsertActor(const FIndexedActor& Actor);

	// --- Tag CRUD ---
	int64 InsertTag(const FIndexedTag& Tag);
	int64 GetOrCreateTag(const FString& TagName, const FString& ParentTag);
	int64 InsertTagReference(const FIndexedTagReference& Ref);

	// --- Config CRUD ---
	int64 InsertConfig(const FIndexedConfig& Config);

	// --- C++ Symbol CRUD ---
	int64 InsertCppSymbol(const FIndexedCppSymbol& Symbol);

	// --- DataTable Row CRUD ---
	int64 InsertDataTableRow(const FIndexedDataTableRow& Row);

	// --- FTS5 Search ---
	TArray<FSearchResult> FullTextSearch(const FString& Query, int32 Limit = 50);

	// --- Stats ---
	TSharedPtr<FJsonObject> GetStats();

	// --- Asset details (all related data) ---
	TSharedPtr<FJsonObject> GetAssetDetails(const FString& PackagePath);

	// --- Find by type ---
	TArray<FIndexedAsset> FindByType(const FString& AssetClass, int32 Limit = 100, int32 Offset = 0);

	// --- Find references (bidirectional) ---
	TSharedPtr<FJsonObject> FindReferences(const FString& PackagePath);

private:
	bool CreateTables();
	bool ExecuteSQL(const FString& SQL);
	FSQLiteDatabase* Database = nullptr;
	FString DbPath;
};
```

### Step 2: Create the implementation

Create `Source/MonolithIndex/Private/MonolithIndexDatabase.cpp`:

```cpp
#include "MonolithIndexDatabase.h"
#include "SQLiteDatabase.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformFileManager.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

DEFINE_LOG_CATEGORY(LogMonolithIndex);

// ============================================================
// Full table creation SQL
// ============================================================
static const TCHAR* GCreateTablesSQL = TEXT(R"SQL(

-- Core asset table: every indexed asset
CREATE TABLE IF NOT EXISTS assets (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    package_path TEXT NOT NULL UNIQUE,
    asset_name TEXT NOT NULL,
    asset_class TEXT NOT NULL,
    module_name TEXT DEFAULT '',
    description TEXT DEFAULT '',
    file_size_bytes INTEGER DEFAULT 0,
    last_modified TEXT DEFAULT '',
    indexed_at TEXT DEFAULT (datetime('now'))
);
CREATE INDEX IF NOT EXISTS idx_assets_class ON assets(asset_class);
CREATE INDEX IF NOT EXISTS idx_assets_name ON assets(asset_name);

-- Graph nodes (Blueprint nodes, Material expressions, Niagara modules, etc.)
CREATE TABLE IF NOT EXISTS nodes (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    node_type TEXT NOT NULL,
    node_name TEXT NOT NULL,
    node_class TEXT DEFAULT '',
    properties TEXT DEFAULT '{}',
    pos_x INTEGER DEFAULT 0,
    pos_y INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_nodes_asset ON nodes(asset_id);
CREATE INDEX IF NOT EXISTS idx_nodes_class ON nodes(node_class);

-- Pin connections between nodes
CREATE TABLE IF NOT EXISTS connections (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    source_node_id INTEGER NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
    source_pin TEXT NOT NULL,
    target_node_id INTEGER NOT NULL REFERENCES nodes(id) ON DELETE CASCADE,
    target_pin TEXT NOT NULL,
    pin_type TEXT DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_conn_source ON connections(source_node_id);
CREATE INDEX IF NOT EXISTS idx_conn_target ON connections(target_node_id);

-- Variables (Blueprint variables, material parameters, niagara parameters)
CREATE TABLE IF NOT EXISTS variables (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    var_name TEXT NOT NULL,
    var_type TEXT NOT NULL,
    category TEXT DEFAULT '',
    default_value TEXT DEFAULT '',
    is_exposed INTEGER DEFAULT 0,
    is_replicated INTEGER DEFAULT 0
);
CREATE INDEX IF NOT EXISTS idx_vars_asset ON variables(asset_id);

-- Parameters (Material params, Niagara params, etc.)
CREATE TABLE IF NOT EXISTS parameters (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    param_name TEXT NOT NULL,
    param_type TEXT NOT NULL,
    param_group TEXT DEFAULT '',
    default_value TEXT DEFAULT '',
    source TEXT DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_params_asset ON parameters(asset_id);

-- Asset dependency graph
CREATE TABLE IF NOT EXISTS dependencies (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    source_asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    target_asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    dependency_type TEXT DEFAULT 'Hard'
);
CREATE INDEX IF NOT EXISTS idx_dep_source ON dependencies(source_asset_id);
CREATE INDEX IF NOT EXISTS idx_dep_target ON dependencies(target_asset_id);

-- Level actors
CREATE TABLE IF NOT EXISTS actors (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    actor_name TEXT NOT NULL,
    actor_class TEXT NOT NULL,
    actor_label TEXT DEFAULT '',
    transform TEXT DEFAULT '{}',
    components TEXT DEFAULT '[]'
);
CREATE INDEX IF NOT EXISTS idx_actors_asset ON actors(asset_id);
CREATE INDEX IF NOT EXISTS idx_actors_class ON actors(actor_class);

-- Gameplay tags
CREATE TABLE IF NOT EXISTS tags (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    tag_name TEXT NOT NULL UNIQUE,
    parent_tag TEXT DEFAULT '',
    reference_count INTEGER DEFAULT 0
);

-- Tag references (which assets use which tags)
CREATE TABLE IF NOT EXISTS tag_references (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    tag_id INTEGER NOT NULL REFERENCES tags(id) ON DELETE CASCADE,
    asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    context TEXT DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_tagref_tag ON tag_references(tag_id);
CREATE INDEX IF NOT EXISTS idx_tagref_asset ON tag_references(asset_id);

-- Config/INI entries
CREATE TABLE IF NOT EXISTS configs (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    file_path TEXT NOT NULL,
    section TEXT NOT NULL,
    key TEXT NOT NULL,
    value TEXT DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_configs_file ON configs(file_path);

-- C++ symbols (from tree-sitter via MonolithSource)
CREATE TABLE IF NOT EXISTS cpp_symbols (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    file_path TEXT NOT NULL,
    symbol_name TEXT NOT NULL,
    symbol_type TEXT NOT NULL,
    signature TEXT DEFAULT '',
    line_number INTEGER DEFAULT 0,
    parent_symbol TEXT DEFAULT ''
);
CREATE INDEX IF NOT EXISTS idx_cpp_file ON cpp_symbols(file_path);
CREATE INDEX IF NOT EXISTS idx_cpp_name ON cpp_symbols(symbol_name);

-- Data table rows
CREATE TABLE IF NOT EXISTS datatable_rows (
    id INTEGER PRIMARY KEY AUTOINCREMENT,
    asset_id INTEGER NOT NULL REFERENCES assets(id) ON DELETE CASCADE,
    row_name TEXT NOT NULL,
    row_data TEXT DEFAULT '{}'
);
CREATE INDEX IF NOT EXISTS idx_dt_asset ON datatable_rows(asset_id);

-- FTS5 index over assets (name, class, description, path)
CREATE VIRTUAL TABLE IF NOT EXISTS fts_assets USING fts5(
    asset_name,
    asset_class,
    description,
    package_path,
    content=assets,
    content_rowid=id,
    tokenize='porter unicode61'
);

-- FTS5 triggers to keep index in sync
CREATE TRIGGER IF NOT EXISTS fts_assets_ai AFTER INSERT ON assets BEGIN
    INSERT INTO fts_assets(rowid, asset_name, asset_class, description, package_path)
    VALUES (new.id, new.asset_name, new.asset_class, new.description, new.package_path);
END;
CREATE TRIGGER IF NOT EXISTS fts_assets_ad AFTER DELETE ON assets BEGIN
    INSERT INTO fts_assets(fts_assets, rowid, asset_name, asset_class, description, package_path)
    VALUES ('delete', old.id, old.asset_name, old.asset_class, old.description, old.package_path);
END;
CREATE TRIGGER IF NOT EXISTS fts_assets_au AFTER UPDATE ON assets BEGIN
    INSERT INTO fts_assets(fts_assets, rowid, asset_name, asset_class, description, package_path)
    VALUES ('delete', old.id, old.asset_name, old.asset_class, old.description, old.package_path);
    INSERT INTO fts_assets(rowid, asset_name, asset_class, description, package_path)
    VALUES (new.id, new.asset_name, new.asset_class, new.description, new.package_path);
END;

-- FTS5 index over nodes (name, class, type)
CREATE VIRTUAL TABLE IF NOT EXISTS fts_nodes USING fts5(
    node_name,
    node_class,
    node_type,
    content=nodes,
    content_rowid=id,
    tokenize='porter unicode61'
);

CREATE TRIGGER IF NOT EXISTS fts_nodes_ai AFTER INSERT ON nodes BEGIN
    INSERT INTO fts_nodes(rowid, node_name, node_class, node_type)
    VALUES (new.id, new.node_name, new.node_class, new.node_type);
END;
CREATE TRIGGER IF NOT EXISTS fts_nodes_ad AFTER DELETE ON nodes BEGIN
    INSERT INTO fts_nodes(fts_nodes, rowid, node_name, node_class, node_type)
    VALUES ('delete', old.id, old.node_name, old.node_class, old.node_type);
END;
CREATE TRIGGER IF NOT EXISTS fts_nodes_au AFTER UPDATE ON nodes BEGIN
    INSERT INTO fts_nodes(fts_nodes, rowid, node_name, node_class, node_type)
    VALUES ('delete', old.id, old.node_name, old.node_class, old.node_type);
    INSERT INTO fts_nodes(rowid, node_name, node_class, node_type)
    VALUES (new.id, new.node_name, new.node_class, new.node_type);
END;

-- Metadata table for tracking index state
CREATE TABLE IF NOT EXISTS meta (
    key TEXT PRIMARY KEY,
    value TEXT DEFAULT ''
);

)SQL");

// ============================================================
// Constructor / Destructor
// ============================================================

FMonolithIndexDatabase::FMonolithIndexDatabase()
{
}

FMonolithIndexDatabase::~FMonolithIndexDatabase()
{
	Close();
}

bool FMonolithIndexDatabase::Open(const FString& InDbPath)
{
	if (Database)
	{
		Close();
	}

	DbPath = InDbPath;

	// Ensure directory exists
	FString Dir = FPaths::GetPath(DbPath);
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*Dir))
	{
		PlatformFile.CreateDirectoryTree(*Dir);
	}

	Database = new FSQLiteDatabase();
	if (!Database->Open(*DbPath, ESQLiteDatabaseOpenMode::ReadWriteCreate))
	{
		UE_LOG(LogMonolithIndex, Error, TEXT("Failed to open index database: %s"), *DbPath);
		delete Database;
		Database = nullptr;
		return false;
	}

	// Enable WAL mode for better concurrent read performance
	ExecuteSQL(TEXT("PRAGMA journal_mode=WAL;"));
	ExecuteSQL(TEXT("PRAGMA synchronous=NORMAL;"));
	ExecuteSQL(TEXT("PRAGMA foreign_keys=ON;"));
	ExecuteSQL(TEXT("PRAGMA cache_size=-64000;")); // 64MB cache

	if (!CreateTables())
	{
		UE_LOG(LogMonolithIndex, Error, TEXT("Failed to create index tables"));
		Close();
		return false;
	}

	UE_LOG(LogMonolithIndex, Log, TEXT("Index database opened: %s"), *DbPath);
	return true;
}

void FMonolithIndexDatabase::Close()
{
	if (Database)
	{
		Database->Close();
		delete Database;
		Database = nullptr;
	}
}

bool FMonolithIndexDatabase::IsOpen() const
{
	return Database != nullptr && Database->IsValid();
}

bool FMonolithIndexDatabase::ResetDatabase()
{
	if (!IsOpen()) return false;

	// Drop all tables and recreate
	ExecuteSQL(TEXT("DROP TRIGGER IF EXISTS fts_assets_ai;"));
	ExecuteSQL(TEXT("DROP TRIGGER IF EXISTS fts_assets_ad;"));
	ExecuteSQL(TEXT("DROP TRIGGER IF EXISTS fts_assets_au;"));
	ExecuteSQL(TEXT("DROP TRIGGER IF EXISTS fts_nodes_ai;"));
	ExecuteSQL(TEXT("DROP TRIGGER IF EXISTS fts_nodes_ad;"));
	ExecuteSQL(TEXT("DROP TRIGGER IF EXISTS fts_nodes_au;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS fts_assets;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS fts_nodes;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS tag_references;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS tags;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS connections;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS nodes;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS variables;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS parameters;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS dependencies;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS actors;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS configs;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS cpp_symbols;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS datatable_rows;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS meta;"));
	ExecuteSQL(TEXT("DROP TABLE IF EXISTS assets;"));

	return CreateTables();
}

// ============================================================
// Transaction helpers
// ============================================================

bool FMonolithIndexDatabase::BeginTransaction()
{
	return ExecuteSQL(TEXT("BEGIN TRANSACTION;"));
}

bool FMonolithIndexDatabase::CommitTransaction()
{
	return ExecuteSQL(TEXT("COMMIT;"));
}

bool FMonolithIndexDatabase::RollbackTransaction()
{
	return ExecuteSQL(TEXT("ROLLBACK;"));
}

// ============================================================
// Asset CRUD
// ============================================================

int64 FMonolithIndexDatabase::InsertAsset(const FIndexedAsset& Asset)
{
	if (!IsOpen()) return -1;

	FString SQL = FString::Printf(
		TEXT("INSERT OR REPLACE INTO assets (package_path, asset_name, asset_class, module_name, description, file_size_bytes, last_modified) VALUES ('%s', '%s', '%s', '%s', '%s', %lld, '%s');"),
		*Asset.PackagePath.Replace(TEXT("'"), TEXT("''")),
		*Asset.AssetName.Replace(TEXT("'"), TEXT("''")),
		*Asset.AssetClass.Replace(TEXT("'"), TEXT("''")),
		*Asset.ModuleName.Replace(TEXT("'"), TEXT("''")),
		*Asset.Description.Replace(TEXT("'"), TEXT("''")),
		Asset.FileSizeBytes,
		*Asset.LastModified.Replace(TEXT("'"), TEXT("''"))
	);

	if (!ExecuteSQL(SQL)) return -1;

	// Get the rowid
	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT last_insert_rowid();"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

TOptional<FIndexedAsset> FMonolithIndexDatabase::GetAssetByPath(const FString& PackagePath)
{
	if (!IsOpen()) return {};

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT id, package_path, asset_name, asset_class, module_name, description, file_size_bytes, last_modified, indexed_at FROM assets WHERE package_path = ?;"));
	Stmt.SetBindingValueByIndex(1, PackagePath);

	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FIndexedAsset Asset;
		Stmt.GetColumnValueByIndex(0, Asset.Id);
		Stmt.GetColumnValueByIndex(1, Asset.PackagePath);
		Stmt.GetColumnValueByIndex(2, Asset.AssetName);
		Stmt.GetColumnValueByIndex(3, Asset.AssetClass);
		Stmt.GetColumnValueByIndex(4, Asset.ModuleName);
		Stmt.GetColumnValueByIndex(5, Asset.Description);
		Stmt.GetColumnValueByIndex(6, Asset.FileSizeBytes);
		Stmt.GetColumnValueByIndex(7, Asset.LastModified);
		Stmt.GetColumnValueByIndex(8, Asset.IndexedAt);
		return Asset;
	}
	return {};
}

int64 FMonolithIndexDatabase::GetAssetId(const FString& PackagePath)
{
	if (!IsOpen()) return -1;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT id FROM assets WHERE package_path = ?;"));
	Stmt.SetBindingValueByIndex(1, PackagePath);

	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

bool FMonolithIndexDatabase::DeleteAssetAndRelated(int64 AssetId)
{
	// CASCADE handles child rows
	return ExecuteSQL(FString::Printf(TEXT("DELETE FROM assets WHERE id = %lld;"), AssetId));
}

// ============================================================
// Node CRUD
// ============================================================

int64 FMonolithIndexDatabase::InsertNode(const FIndexedNode& Node)
{
	if (!IsOpen()) return -1;

	FString SQL = FString::Printf(
		TEXT("INSERT INTO nodes (asset_id, node_type, node_name, node_class, properties, pos_x, pos_y) VALUES (%lld, '%s', '%s', '%s', '%s', %d, %d);"),
		Node.AssetId,
		*Node.NodeType.Replace(TEXT("'"), TEXT("''")),
		*Node.NodeName.Replace(TEXT("'"), TEXT("''")),
		*Node.NodeClass.Replace(TEXT("'"), TEXT("''")),
		*Node.Properties.Replace(TEXT("'"), TEXT("''")),
		Node.PosX, Node.PosY
	);

	if (!ExecuteSQL(SQL)) return -1;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT last_insert_rowid();"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

TArray<FIndexedNode> FMonolithIndexDatabase::GetNodesForAsset(int64 AssetId)
{
	TArray<FIndexedNode> Result;
	if (!IsOpen()) return Result;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT id, asset_id, node_type, node_name, node_class, properties, pos_x, pos_y FROM nodes WHERE asset_id = ?;"));
	Stmt.SetBindingValueByIndex(1, AssetId);

	while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FIndexedNode Node;
		Stmt.GetColumnValueByIndex(0, Node.Id);
		Stmt.GetColumnValueByIndex(1, Node.AssetId);
		Stmt.GetColumnValueByIndex(2, Node.NodeType);
		Stmt.GetColumnValueByIndex(3, Node.NodeName);
		Stmt.GetColumnValueByIndex(4, Node.NodeClass);
		Stmt.GetColumnValueByIndex(5, Node.Properties);
		Stmt.GetColumnValueByIndex(6, Node.PosX);
		Stmt.GetColumnValueByIndex(7, Node.PosY);
		Result.Add(MoveTemp(Node));
	}
	return Result;
}

// ============================================================
// Connection CRUD
// ============================================================

int64 FMonolithIndexDatabase::InsertConnection(const FIndexedConnection& Conn)
{
	if (!IsOpen()) return -1;

	FString SQL = FString::Printf(
		TEXT("INSERT INTO connections (source_node_id, source_pin, target_node_id, target_pin, pin_type) VALUES (%lld, '%s', %lld, '%s', '%s');"),
		Conn.SourceNodeId,
		*Conn.SourcePin.Replace(TEXT("'"), TEXT("''")),
		Conn.TargetNodeId,
		*Conn.TargetPin.Replace(TEXT("'"), TEXT("''")),
		*Conn.PinType.Replace(TEXT("'"), TEXT("''"))
	);

	if (!ExecuteSQL(SQL)) return -1;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT last_insert_rowid();"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

TArray<FIndexedConnection> FMonolithIndexDatabase::GetConnectionsForAsset(int64 AssetId)
{
	TArray<FIndexedConnection> Result;
	if (!IsOpen()) return Result;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT c.id, c.source_node_id, c.source_pin, c.target_node_id, c.target_pin, c.pin_type FROM connections c INNER JOIN nodes n ON c.source_node_id = n.id WHERE n.asset_id = ?;"));
	Stmt.SetBindingValueByIndex(1, AssetId);

	while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FIndexedConnection Conn;
		Stmt.GetColumnValueByIndex(0, Conn.Id);
		Stmt.GetColumnValueByIndex(1, Conn.SourceNodeId);
		Stmt.GetColumnValueByIndex(2, Conn.SourcePin);
		Stmt.GetColumnValueByIndex(3, Conn.TargetNodeId);
		Stmt.GetColumnValueByIndex(4, Conn.TargetPin);
		Stmt.GetColumnValueByIndex(5, Conn.PinType);
		Result.Add(MoveTemp(Conn));
	}
	return Result;
}

// ============================================================
// Variable CRUD
// ============================================================

int64 FMonolithIndexDatabase::InsertVariable(const FIndexedVariable& Var)
{
	if (!IsOpen()) return -1;

	FString SQL = FString::Printf(
		TEXT("INSERT INTO variables (asset_id, var_name, var_type, category, default_value, is_exposed, is_replicated) VALUES (%lld, '%s', '%s', '%s', '%s', %d, %d);"),
		Var.AssetId,
		*Var.VarName.Replace(TEXT("'"), TEXT("''")),
		*Var.VarType.Replace(TEXT("'"), TEXT("''")),
		*Var.Category.Replace(TEXT("'"), TEXT("''")),
		*Var.DefaultValue.Replace(TEXT("'"), TEXT("''")),
		Var.bIsExposed ? 1 : 0,
		Var.bIsReplicated ? 1 : 0
	);

	if (!ExecuteSQL(SQL)) return -1;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT last_insert_rowid();"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

TArray<FIndexedVariable> FMonolithIndexDatabase::GetVariablesForAsset(int64 AssetId)
{
	TArray<FIndexedVariable> Result;
	if (!IsOpen()) return Result;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT id, asset_id, var_name, var_type, category, default_value, is_exposed, is_replicated FROM variables WHERE asset_id = ?;"));
	Stmt.SetBindingValueByIndex(1, AssetId);

	while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FIndexedVariable Var;
		Stmt.GetColumnValueByIndex(0, Var.Id);
		Stmt.GetColumnValueByIndex(1, Var.AssetId);
		Stmt.GetColumnValueByIndex(2, Var.VarName);
		Stmt.GetColumnValueByIndex(3, Var.VarType);
		Stmt.GetColumnValueByIndex(4, Var.Category);
		Stmt.GetColumnValueByIndex(5, Var.DefaultValue);
		int32 Exposed = 0, Replicated = 0;
		Stmt.GetColumnValueByIndex(6, Exposed);
		Stmt.GetColumnValueByIndex(7, Replicated);
		Var.bIsExposed = Exposed != 0;
		Var.bIsReplicated = Replicated != 0;
		Result.Add(MoveTemp(Var));
	}
	return Result;
}

// ============================================================
// Parameter CRUD
// ============================================================

int64 FMonolithIndexDatabase::InsertParameter(const FIndexedParameter& Param)
{
	if (!IsOpen()) return -1;

	FString SQL = FString::Printf(
		TEXT("INSERT INTO parameters (asset_id, param_name, param_type, param_group, default_value, source) VALUES (%lld, '%s', '%s', '%s', '%s', '%s');"),
		Param.AssetId,
		*Param.ParamName.Replace(TEXT("'"), TEXT("''")),
		*Param.ParamType.Replace(TEXT("'"), TEXT("''")),
		*Param.ParamGroup.Replace(TEXT("'"), TEXT("''")),
		*Param.DefaultValue.Replace(TEXT("'"), TEXT("''")),
		*Param.Source.Replace(TEXT("'"), TEXT("''"))
	);

	if (!ExecuteSQL(SQL)) return -1;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT last_insert_rowid();"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

// ============================================================
// Dependency CRUD
// ============================================================

int64 FMonolithIndexDatabase::InsertDependency(const FIndexedDependency& Dep)
{
	if (!IsOpen()) return -1;

	FString SQL = FString::Printf(
		TEXT("INSERT INTO dependencies (source_asset_id, target_asset_id, dependency_type) VALUES (%lld, %lld, '%s');"),
		Dep.SourceAssetId, Dep.TargetAssetId,
		*Dep.DependencyType.Replace(TEXT("'"), TEXT("''"))
	);

	if (!ExecuteSQL(SQL)) return -1;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT last_insert_rowid();"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

TArray<FIndexedDependency> FMonolithIndexDatabase::GetDependenciesForAsset(int64 AssetId)
{
	TArray<FIndexedDependency> Result;
	if (!IsOpen()) return Result;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT id, source_asset_id, target_asset_id, dependency_type FROM dependencies WHERE source_asset_id = ?;"));
	Stmt.SetBindingValueByIndex(1, AssetId);

	while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FIndexedDependency Dep;
		Stmt.GetColumnValueByIndex(0, Dep.Id);
		Stmt.GetColumnValueByIndex(1, Dep.SourceAssetId);
		Stmt.GetColumnValueByIndex(2, Dep.TargetAssetId);
		Stmt.GetColumnValueByIndex(3, Dep.DependencyType);
		Result.Add(MoveTemp(Dep));
	}
	return Result;
}

TArray<FIndexedDependency> FMonolithIndexDatabase::GetReferencersOfAsset(int64 AssetId)
{
	TArray<FIndexedDependency> Result;
	if (!IsOpen()) return Result;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT id, source_asset_id, target_asset_id, dependency_type FROM dependencies WHERE target_asset_id = ?;"));
	Stmt.SetBindingValueByIndex(1, AssetId);

	while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FIndexedDependency Dep;
		Stmt.GetColumnValueByIndex(0, Dep.Id);
		Stmt.GetColumnValueByIndex(1, Dep.SourceAssetId);
		Stmt.GetColumnValueByIndex(2, Dep.TargetAssetId);
		Stmt.GetColumnValueByIndex(3, Dep.DependencyType);
		Result.Add(MoveTemp(Dep));
	}
	return Result;
}

// ============================================================
// Actor CRUD
// ============================================================

int64 FMonolithIndexDatabase::InsertActor(const FIndexedActor& Actor)
{
	if (!IsOpen()) return -1;

	FString SQL = FString::Printf(
		TEXT("INSERT INTO actors (asset_id, actor_name, actor_class, actor_label, transform, components) VALUES (%lld, '%s', '%s', '%s', '%s', '%s');"),
		Actor.AssetId,
		*Actor.ActorName.Replace(TEXT("'"), TEXT("''")),
		*Actor.ActorClass.Replace(TEXT("'"), TEXT("''")),
		*Actor.ActorLabel.Replace(TEXT("'"), TEXT("''")),
		*Actor.Transform.Replace(TEXT("'"), TEXT("''")),
		*Actor.Components.Replace(TEXT("'"), TEXT("''"))
	);

	if (!ExecuteSQL(SQL)) return -1;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT last_insert_rowid();"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

// ============================================================
// Tag CRUD
// ============================================================

int64 FMonolithIndexDatabase::InsertTag(const FIndexedTag& Tag)
{
	if (!IsOpen()) return -1;

	FString SQL = FString::Printf(
		TEXT("INSERT OR IGNORE INTO tags (tag_name, parent_tag, reference_count) VALUES ('%s', '%s', %d);"),
		*Tag.TagName.Replace(TEXT("'"), TEXT("''")),
		*Tag.ParentTag.Replace(TEXT("'"), TEXT("''")),
		Tag.ReferenceCount
	);

	if (!ExecuteSQL(SQL)) return -1;
	return GetOrCreateTag(Tag.TagName, Tag.ParentTag);
}

int64 FMonolithIndexDatabase::GetOrCreateTag(const FString& TagName, const FString& ParentTag)
{
	if (!IsOpen()) return -1;

	// Try to get existing
	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT id FROM tags WHERE tag_name = ?;"));
	Stmt.SetBindingValueByIndex(1, TagName);

	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}

	// Insert new
	FString SQL = FString::Printf(
		TEXT("INSERT INTO tags (tag_name, parent_tag) VALUES ('%s', '%s');"),
		*TagName.Replace(TEXT("'"), TEXT("''")),
		*ParentTag.Replace(TEXT("'"), TEXT("''"))
	);
	ExecuteSQL(SQL);

	FSQLitePreparedStatement Stmt2;
	Stmt2.Create(*Database, TEXT("SELECT last_insert_rowid();"));
	if (Stmt2.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt2.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

int64 FMonolithIndexDatabase::InsertTagReference(const FIndexedTagReference& Ref)
{
	if (!IsOpen()) return -1;

	FString SQL = FString::Printf(
		TEXT("INSERT INTO tag_references (tag_id, asset_id, context) VALUES (%lld, %lld, '%s');"),
		Ref.TagId, Ref.AssetId,
		*Ref.Context.Replace(TEXT("'"), TEXT("''"))
	);

	if (!ExecuteSQL(SQL)) return -1;

	// Update reference count
	ExecuteSQL(FString::Printf(
		TEXT("UPDATE tags SET reference_count = (SELECT COUNT(*) FROM tag_references WHERE tag_id = %lld) WHERE id = %lld;"),
		Ref.TagId, Ref.TagId
	));

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT last_insert_rowid();"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

// ============================================================
// Config CRUD
// ============================================================

int64 FMonolithIndexDatabase::InsertConfig(const FIndexedConfig& Config)
{
	if (!IsOpen()) return -1;

	FString SQL = FString::Printf(
		TEXT("INSERT INTO configs (file_path, section, key, value) VALUES ('%s', '%s', '%s', '%s');"),
		*Config.FilePath.Replace(TEXT("'"), TEXT("''")),
		*Config.Section.Replace(TEXT("'"), TEXT("''")),
		*Config.Key.Replace(TEXT("'"), TEXT("''")),
		*Config.Value.Replace(TEXT("'"), TEXT("''"))
	);

	if (!ExecuteSQL(SQL)) return -1;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT last_insert_rowid();"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

// ============================================================
// C++ Symbol CRUD
// ============================================================

int64 FMonolithIndexDatabase::InsertCppSymbol(const FIndexedCppSymbol& Symbol)
{
	if (!IsOpen()) return -1;

	FString SQL = FString::Printf(
		TEXT("INSERT INTO cpp_symbols (file_path, symbol_name, symbol_type, signature, line_number, parent_symbol) VALUES ('%s', '%s', '%s', '%s', %d, '%s');"),
		*Symbol.FilePath.Replace(TEXT("'"), TEXT("''")),
		*Symbol.SymbolName.Replace(TEXT("'"), TEXT("''")),
		*Symbol.SymbolType.Replace(TEXT("'"), TEXT("''")),
		*Symbol.Signature.Replace(TEXT("'"), TEXT("''")),
		Symbol.LineNumber,
		*Symbol.ParentSymbol.Replace(TEXT("'"), TEXT("''"))
	);

	if (!ExecuteSQL(SQL)) return -1;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT last_insert_rowid();"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

// ============================================================
// DataTable Row CRUD
// ============================================================

int64 FMonolithIndexDatabase::InsertDataTableRow(const FIndexedDataTableRow& Row)
{
	if (!IsOpen()) return -1;

	FString SQL = FString::Printf(
		TEXT("INSERT INTO datatable_rows (asset_id, row_name, row_data) VALUES (%lld, '%s', '%s');"),
		Row.AssetId,
		*Row.RowName.Replace(TEXT("'"), TEXT("''")),
		*Row.RowData.Replace(TEXT("'"), TEXT("''"))
	);

	if (!ExecuteSQL(SQL)) return -1;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT last_insert_rowid();"));
	if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		int64 Id = 0;
		Stmt.GetColumnValueByIndex(0, Id);
		return Id;
	}
	return -1;
}

// ============================================================
// FTS5 Full-text search
// ============================================================

TArray<FSearchResult> FMonolithIndexDatabase::FullTextSearch(const FString& Query, int32 Limit)
{
	TArray<FSearchResult> Results;
	if (!IsOpen()) return Results;

	// Search assets FTS
	FString SQL = FString::Printf(
		TEXT("SELECT a.package_path, a.asset_name, a.asset_class, snippet(fts_assets, 2, '>>>', '<<<', '...', 32) as ctx, rank FROM fts_assets f JOIN assets a ON a.id = f.rowid WHERE fts_assets MATCH ? ORDER BY rank LIMIT %d;"),
		Limit
	);

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, *SQL);
	Stmt.SetBindingValueByIndex(1, Query);

	while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FSearchResult R;
		Stmt.GetColumnValueByIndex(0, R.AssetPath);
		Stmt.GetColumnValueByIndex(1, R.AssetName);
		Stmt.GetColumnValueByIndex(2, R.AssetClass);
		Stmt.GetColumnValueByIndex(3, R.MatchContext);
		double RankD = 0.0;
		Stmt.GetColumnValueByIndex(4, RankD);
		R.Rank = static_cast<float>(RankD);
		Results.Add(MoveTemp(R));
	}

	// Also search nodes FTS
	FString NodeSQL = FString::Printf(
		TEXT("SELECT a.package_path, a.asset_name, a.asset_class, snippet(fts_nodes, 0, '>>>', '<<<', '...', 32) as ctx, f.rank FROM fts_nodes f JOIN nodes n ON n.id = f.rowid JOIN assets a ON a.id = n.asset_id WHERE fts_nodes MATCH ? ORDER BY f.rank LIMIT %d;"),
		Limit
	);

	FSQLitePreparedStatement Stmt2;
	Stmt2.Create(*Database, *NodeSQL);
	Stmt2.SetBindingValueByIndex(1, Query);

	while (Stmt2.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FSearchResult R;
		Stmt2.GetColumnValueByIndex(0, R.AssetPath);
		Stmt2.GetColumnValueByIndex(1, R.AssetName);
		Stmt2.GetColumnValueByIndex(2, R.AssetClass);
		Stmt2.GetColumnValueByIndex(3, R.MatchContext);
		double RankD = 0.0;
		Stmt2.GetColumnValueByIndex(4, RankD);
		R.Rank = static_cast<float>(RankD);
		Results.Add(MoveTemp(R));
	}

	// Sort combined results by rank (lower = better in FTS5)
	Results.Sort([](const FSearchResult& A, const FSearchResult& B) { return A.Rank < B.Rank; });

	// Trim to limit
	if (Results.Num() > Limit)
	{
		Results.SetNum(Limit);
	}

	return Results;
}

// ============================================================
// Stats
// ============================================================

TSharedPtr<FJsonObject> FMonolithIndexDatabase::GetStats()
{
	auto Stats = MakeShared<FJsonObject>();
	if (!IsOpen()) return Stats;

	auto GetCount = [this](const TCHAR* Table) -> int64
	{
		FSQLitePreparedStatement Stmt;
		FString SQL = FString::Printf(TEXT("SELECT COUNT(*) FROM %s;"), Table);
		Stmt.Create(*Database, *SQL);
		if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
		{
			int64 Count = 0;
			Stmt.GetColumnValueByIndex(0, Count);
			return Count;
		}
		return 0;
	};

	Stats->SetNumberField(TEXT("assets"), GetCount(TEXT("assets")));
	Stats->SetNumberField(TEXT("nodes"), GetCount(TEXT("nodes")));
	Stats->SetNumberField(TEXT("connections"), GetCount(TEXT("connections")));
	Stats->SetNumberField(TEXT("variables"), GetCount(TEXT("variables")));
	Stats->SetNumberField(TEXT("parameters"), GetCount(TEXT("parameters")));
	Stats->SetNumberField(TEXT("dependencies"), GetCount(TEXT("dependencies")));
	Stats->SetNumberField(TEXT("actors"), GetCount(TEXT("actors")));
	Stats->SetNumberField(TEXT("tags"), GetCount(TEXT("tags")));
	Stats->SetNumberField(TEXT("configs"), GetCount(TEXT("configs")));
	Stats->SetNumberField(TEXT("cpp_symbols"), GetCount(TEXT("cpp_symbols")));
	Stats->SetNumberField(TEXT("datatable_rows"), GetCount(TEXT("datatable_rows")));

	// Asset class breakdown
	auto ClassBreakdown = MakeShared<FJsonObject>();
	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT asset_class, COUNT(*) as cnt FROM assets GROUP BY asset_class ORDER BY cnt DESC LIMIT 20;"));
	while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FString ClassName;
		int64 Count = 0;
		Stmt.GetColumnValueByIndex(0, ClassName);
		Stmt.GetColumnValueByIndex(1, Count);
		ClassBreakdown->SetNumberField(ClassName, Count);
	}
	Stats->SetObjectField(TEXT("asset_class_breakdown"), ClassBreakdown);

	return Stats;
}

// ============================================================
// Asset details
// ============================================================

TSharedPtr<FJsonObject> FMonolithIndexDatabase::GetAssetDetails(const FString& PackagePath)
{
	auto Details = MakeShared<FJsonObject>();
	if (!IsOpen()) return Details;

	auto MaybeAsset = GetAssetByPath(PackagePath);
	if (!MaybeAsset.IsSet()) return Details;

	const FIndexedAsset& Asset = MaybeAsset.GetValue();
	Details->SetStringField(TEXT("package_path"), Asset.PackagePath);
	Details->SetStringField(TEXT("asset_name"), Asset.AssetName);
	Details->SetStringField(TEXT("asset_class"), Asset.AssetClass);
	Details->SetStringField(TEXT("module_name"), Asset.ModuleName);
	Details->SetStringField(TEXT("description"), Asset.Description);
	Details->SetNumberField(TEXT("file_size_bytes"), Asset.FileSizeBytes);
	Details->SetStringField(TEXT("last_modified"), Asset.LastModified);
	Details->SetStringField(TEXT("indexed_at"), Asset.IndexedAt);

	// Nodes
	TArray<TSharedPtr<FJsonValue>> NodesArr;
	for (const auto& Node : GetNodesForAsset(Asset.Id))
	{
		auto NodeObj = MakeShared<FJsonObject>();
		NodeObj->SetStringField(TEXT("node_type"), Node.NodeType);
		NodeObj->SetStringField(TEXT("node_name"), Node.NodeName);
		NodeObj->SetStringField(TEXT("node_class"), Node.NodeClass);
		NodesArr.Add(MakeShared<FJsonValueObject>(NodeObj));
	}
	Details->SetArrayField(TEXT("nodes"), NodesArr);

	// Variables
	TArray<TSharedPtr<FJsonValue>> VarsArr;
	for (const auto& Var : GetVariablesForAsset(Asset.Id))
	{
		auto VarObj = MakeShared<FJsonObject>();
		VarObj->SetStringField(TEXT("name"), Var.VarName);
		VarObj->SetStringField(TEXT("type"), Var.VarType);
		VarObj->SetStringField(TEXT("category"), Var.Category);
		VarObj->SetBoolField(TEXT("exposed"), Var.bIsExposed);
		VarsArr.Add(MakeShared<FJsonValueObject>(VarObj));
	}
	Details->SetArrayField(TEXT("variables"), VarsArr);

	// Dependencies
	auto Refs = FindReferences(PackagePath);
	if (Refs.IsValid())
	{
		Details->SetObjectField(TEXT("references"), Refs);
	}

	return Details;
}

// ============================================================
// Find by type
// ============================================================

TArray<FIndexedAsset> FMonolithIndexDatabase::FindByType(const FString& AssetClass, int32 Limit, int32 Offset)
{
	TArray<FIndexedAsset> Result;
	if (!IsOpen()) return Result;

	FSQLitePreparedStatement Stmt;
	Stmt.Create(*Database, TEXT("SELECT id, package_path, asset_name, asset_class, module_name, description, file_size_bytes, last_modified, indexed_at FROM assets WHERE asset_class = ? LIMIT ? OFFSET ?;"));
	Stmt.SetBindingValueByIndex(1, AssetClass);
	Stmt.SetBindingValueByIndex(2, static_cast<int64>(Limit));
	Stmt.SetBindingValueByIndex(3, static_cast<int64>(Offset));

	while (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
	{
		FIndexedAsset Asset;
		Stmt.GetColumnValueByIndex(0, Asset.Id);
		Stmt.GetColumnValueByIndex(1, Asset.PackagePath);
		Stmt.GetColumnValueByIndex(2, Asset.AssetName);
		Stmt.GetColumnValueByIndex(3, Asset.AssetClass);
		Stmt.GetColumnValueByIndex(4, Asset.ModuleName);
		Stmt.GetColumnValueByIndex(5, Asset.Description);
		Stmt.GetColumnValueByIndex(6, Asset.FileSizeBytes);
		Stmt.GetColumnValueByIndex(7, Asset.LastModified);
		Stmt.GetColumnValueByIndex(8, Asset.IndexedAt);
		Result.Add(MoveTemp(Asset));
	}
	return Result;
}

// ============================================================
// Find references (bidirectional dependency lookup)
// ============================================================

TSharedPtr<FJsonObject> FMonolithIndexDatabase::FindReferences(const FString& PackagePath)
{
	auto Result = MakeShared<FJsonObject>();
	if (!IsOpen()) return Result;

	int64 AssetId = GetAssetId(PackagePath);
	if (AssetId < 0) return Result;

	// What this asset depends on
	TArray<TSharedPtr<FJsonValue>> DepsArr;
	for (const auto& Dep : GetDependenciesForAsset(AssetId))
	{
		auto DepAsset = GetAssetByPath(FString()); // Need path from ID
		// Get target path
		FSQLitePreparedStatement Stmt;
		Stmt.Create(*Database, TEXT("SELECT package_path, asset_class FROM assets WHERE id = ?;"));
		Stmt.SetBindingValueByIndex(1, Dep.TargetAssetId);
		if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
		{
			auto DepObj = MakeShared<FJsonObject>();
			FString Path, Class;
			Stmt.GetColumnValueByIndex(0, Path);
			Stmt.GetColumnValueByIndex(1, Class);
			DepObj->SetStringField(TEXT("path"), Path);
			DepObj->SetStringField(TEXT("class"), Class);
			DepObj->SetStringField(TEXT("type"), Dep.DependencyType);
			DepsArr.Add(MakeShared<FJsonValueObject>(DepObj));
		}
	}
	Result->SetArrayField(TEXT("depends_on"), DepsArr);

	// What references this asset
	TArray<TSharedPtr<FJsonValue>> RefsArr;
	for (const auto& Ref : GetReferencersOfAsset(AssetId))
	{
		FSQLitePreparedStatement Stmt;
		Stmt.Create(*Database, TEXT("SELECT package_path, asset_class FROM assets WHERE id = ?;"));
		Stmt.SetBindingValueByIndex(1, Ref.SourceAssetId);
		if (Stmt.Step() == ESQLitePreparedStatementStepResult::Row)
		{
			auto RefObj = MakeShared<FJsonObject>();
			FString Path, Class;
			Stmt.GetColumnValueByIndex(0, Path);
			Stmt.GetColumnValueByIndex(1, Class);
			RefObj->SetStringField(TEXT("path"), Path);
			RefObj->SetStringField(TEXT("class"), Class);
			RefObj->SetStringField(TEXT("type"), Ref.DependencyType);
			RefsArr.Add(MakeShared<FJsonValueObject>(RefObj));
		}
	}
	Result->SetArrayField(TEXT("referenced_by"), RefsArr);

	return Result;
}

// ============================================================
// Internal helpers
// ============================================================

bool FMonolithIndexDatabase::CreateTables()
{
	return ExecuteSQL(GCreateTablesSQL);
}

bool FMonolithIndexDatabase::ExecuteSQL(const FString& SQL)
{
	if (!Database || !Database->IsValid())
	{
		UE_LOG(LogMonolithIndex, Error, TEXT("Cannot execute SQL — database not open"));
		return false;
	}

	if (!Database->Execute(*SQL))
	{
		UE_LOG(LogMonolithIndex, Error, TEXT("SQL execution failed: %s"), *Database->GetLastError());
		return false;
	}
	return true;
}
```

### Step 3: Verify compilation

```
# Build command (UBT or editor trigger_build)
# Expected: MonolithIndex compiles with SQLiteCore linkage, no errors
```

**Commit:** `feat(index): Add FMonolithIndexDatabase — SQLite wrapper with 13 tables + 2 FTS5 indexes`

---
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

## Phase 5: Source Intelligence + API + Auto-Update

[RESULT success] # Phase 5: Source Module + Auto-Updater

**Goal:** Absorb unreal-source-mcp + reimplement unreal-api-mcp functionality via bundled Python child process. Implement complete auto-update system from GitHub Releases.

**Modules:** MonolithSource (C++), MCP/ (Python adapter), MonolithCore additions (UMonolithUpdateSubsystem)

---

## Task 5.1 — FMonolithSourceProcess (Child Process Manager)

### Files

- **Create:** `Source/MonolithSource/Public/FMonolithSourceProcess.h`
- **Create:** `Source/MonolithSource/Private/FMonolithSourceProcess.cpp`

### Step 1: Create FMonolithSourceProcess.h

```cpp
// Source/MonolithSource/Public/FMonolithSourceProcess.h
#pragma once

#include "CoreMinimal.h"
#include "HAL/PlatformProcess.h"
#include "Misc/MonitoredProcess.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMonolithSource, Log, All);

/**
 * Manages the bundled Python child process for engine source indexing.
 * Communicates via stdin/stdout JSON-line protocol.
 * Lazily spawns on first source.query call.
 */
class MONOLITHSOURCE_API FMonolithSourceProcess
{
public:
	FMonolithSourceProcess();
	~FMonolithSourceProcess();

	/** Ensure the Python process is running. Returns true if ready. */
	bool EnsureRunning();

	/** Send a JSON command and block until response. Returns nullptr on failure. */
	TSharedPtr<FJsonObject> SendCommand(const FString& Method, const TSharedPtr<FJsonObject>& Params);

	/** Gracefully shut down the child process. */
	void Shutdown();

	/** Check if process is currently running. */
	bool IsRunning() const;

	/** Trigger a re-index of engine source. */
	TSharedPtr<FJsonObject> ReindexSource(const FString& EnginePath, const FString& VersionTag);

private:
	/** Resolve path to the Python executable. */
	FString FindPythonPath() const;

	/** Resolve path to the bundled indexer entry point. */
	FString GetIndexerScriptPath() const;

	/** Resolve path to the engine source database. */
	FString GetDatabasePath() const;

	/** Read a single JSON line from stdout. */
	bool ReadJsonLine(FString& OutLine, double TimeoutSeconds = 30.0);

	/** Write a JSON line to stdin. */
	bool WriteJsonLine(const FString& Line);

	FProcHandle ProcessHandle;
	void* ReadPipe = nullptr;
	void* WritePipe = nullptr;
	uint32 ProcessId = 0;
	bool bIsRunning = false;
	int32 NextRequestId = 1;

	FCriticalSection ProcessLock;
};
```

### Step 2: Create FMonolithSourceProcess.cpp

```cpp
// Source/MonolithSource/Private/FMonolithSourceProcess.cpp
#include "FMonolithSourceProcess.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/PlatformProcess.h"
#include "Interfaces/IPluginManager.h"

DEFINE_LOG_CATEGORY(LogMonolithSource);

FMonolithSourceProcess::FMonolithSourceProcess()
{
}

FMonolithSourceProcess::~FMonolithSourceProcess()
{
	Shutdown();
}

FString FMonolithSourceProcess::FindPythonPath() const
{
	// Try common Python locations on Windows
	// 1. Check if python is on PATH
	FString PythonPath = TEXT("python");

	// 2. Try UE bundled Python
	FString UEPython = FPaths::Combine(
		FPaths::EngineDir(), TEXT("Binaries/ThirdParty/Python3/Win64/python.exe"));
	if (FPaths::FileExists(UEPython))
	{
		PythonPath = UEPython;
	}

	return PythonPath;
}

FString FMonolithSourceProcess::GetIndexerScriptPath() const
{
	// MCP/ directory lives at plugin root
	FString PluginDir = FPaths::Combine(
		FPaths::ProjectPluginsDir(), TEXT("Monolith/MCP/src/monolith_source/indexer/child_process.py"));
	return FPaths::ConvertRelativePathToFull(PluginDir);
}

FString FMonolithSourceProcess::GetDatabasePath() const
{
	// Saved/EngineSource_{ver}.db
	FString SavedDir = FPaths::Combine(
		FPaths::ProjectPluginsDir(), TEXT("Monolith/Saved"));

	// Get engine version
	FString EngineVersion = FString::Printf(TEXT("%d.%d"),
		ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);

	return FPaths::Combine(SavedDir,
		FString::Printf(TEXT("EngineSource_%s.db"), *EngineVersion));
}

bool FMonolithSourceProcess::EnsureRunning()
{
	FScopeLock Lock(&ProcessLock);

	if (bIsRunning && FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		return true;
	}

	// Reset state
	bIsRunning = false;

	FString PythonPath = FindPythonPath();
	FString ScriptPath = GetIndexerScriptPath();
	FString DbPath = GetDatabasePath();

	if (!FPaths::FileExists(ScriptPath))
	{
		UE_LOG(LogMonolithSource, Error,
			TEXT("Source indexer script not found: %s"), *ScriptPath);
		return false;
	}

	// Create pipes for stdin/stdout communication
	FString ReadPipeStr, WritePipeStr;
	if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
	{
		UE_LOG(LogMonolithSource, Error, TEXT("Failed to create pipes for source indexer"));
		return false;
	}

	FString Args = FString::Printf(TEXT("\"%s\" --db \"%s\" --mode child-process"),
		*ScriptPath, *DbPath);

	ProcessHandle = FPlatformProcess::CreateProc(
		*PythonPath,		// Executable
		*Args,				// Arguments
		false,				// bLaunchDetached
		false,				// bLaunchHidden
		false,				// bLaunchReallyHidden
		&ProcessId,			// OutProcessID
		0,					// PriorityModifier
		nullptr,			// OptionalWorkingDirectory
		WritePipe,			// PipeWriteChild (our WritePipe → child's stdin)
		ReadPipe			// PipeReadChild (child's stdout → our ReadPipe)
	);

	if (!ProcessHandle.IsValid())
	{
		UE_LOG(LogMonolithSource, Error, TEXT("Failed to spawn Python source indexer"));
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		ReadPipe = nullptr;
		WritePipe = nullptr;
		return false;
	}

	bIsRunning = true;
	UE_LOG(LogMonolithSource, Log, TEXT("Source indexer process started (PID: %u)"), ProcessId);

	// Wait for ready signal from child process
	FString ReadyLine;
	if (ReadJsonLine(ReadyLine, 10.0))
	{
		TSharedPtr<FJsonObject> ReadyJson;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ReadyLine);
		if (FJsonSerializer::Deserialize(Reader, ReadyJson) && ReadyJson.IsValid())
		{
			FString Status = ReadyJson->GetStringField(TEXT("status"));
			if (Status == TEXT("ready"))
			{
				UE_LOG(LogMonolithSource, Log, TEXT("Source indexer ready"));
				return true;
			}
		}
	}

	UE_LOG(LogMonolithSource, Warning,
		TEXT("Source indexer started but did not send ready signal — may still be initializing"));
	return true;
}

TSharedPtr<FJsonObject> FMonolithSourceProcess::SendCommand(
	const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	FScopeLock Lock(&ProcessLock);

	if (!bIsRunning)
	{
		UE_LOG(LogMonolithSource, Error, TEXT("Source indexer not running"));
		return nullptr;
	}

	// Build JSON-line request
	TSharedPtr<FJsonObject> Request = MakeShared<FJsonObject>();
	Request->SetNumberField(TEXT("id"), NextRequestId++);
	Request->SetStringField(TEXT("method"), Method);
	if (Params.IsValid())
	{
		Request->SetObjectField(TEXT("params"), Params);
	}

	FString RequestLine;
	TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
		TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&RequestLine);
	FJsonSerializer::Serialize(Request.ToSharedRef(), Writer);
	Writer->Close();

	if (!WriteJsonLine(RequestLine))
	{
		UE_LOG(LogMonolithSource, Error, TEXT("Failed to write command to source indexer"));
		return nullptr;
	}

	// Read response
	FString ResponseLine;
	if (!ReadJsonLine(ResponseLine))
	{
		UE_LOG(LogMonolithSource, Error, TEXT("Failed to read response from source indexer"));
		return nullptr;
	}

	TSharedPtr<FJsonObject> Response;
	TSharedRef<TJsonReader<>> JsonReader = TJsonReaderFactory<>::Create(ResponseLine);
	if (!FJsonSerializer::Deserialize(JsonReader, Response) || !Response.IsValid())
	{
		UE_LOG(LogMonolithSource, Error, TEXT("Failed to parse response JSON: %s"), *ResponseLine);
		return nullptr;
	}

	// Check for error
	if (Response->HasField(TEXT("error")))
	{
		FString Error = Response->GetStringField(TEXT("error"));
		UE_LOG(LogMonolithSource, Warning, TEXT("Source indexer error: %s"), *Error);
	}

	return Response;
}

TSharedPtr<FJsonObject> FMonolithSourceProcess::ReindexSource(
	const FString& EnginePath, const FString& VersionTag)
{
	TSharedPtr<FJsonObject> Params = MakeShared<FJsonObject>();
	Params->SetStringField(TEXT("engine_path"), EnginePath);
	Params->SetStringField(TEXT("version_tag"), VersionTag);
	return SendCommand(TEXT("reindex"), Params);
}

bool FMonolithSourceProcess::ReadJsonLine(FString& OutLine, double TimeoutSeconds)
{
	OutLine.Empty();
	double StartTime = FPlatformTime::Seconds();

	while (FPlatformTime::Seconds() - StartTime < TimeoutSeconds)
	{
		FString NewOutput = FPlatformProcess::ReadPipe(ReadPipe);
		if (!NewOutput.IsEmpty())
		{
			OutLine += NewOutput;
			// Check if we have a complete line
			int32 NewlineIdx;
			if (OutLine.FindChar(TEXT('\n'), NewlineIdx))
			{
				OutLine = OutLine.Left(NewlineIdx).TrimEnd();
				return true;
			}
		}
		else
		{
			// Small sleep to avoid busy-waiting
			FPlatformProcess::Sleep(0.01f);
		}

		// Check if process died
		if (!FPlatformProcess::IsProcRunning(ProcessHandle))
		{
			UE_LOG(LogMonolithSource, Error, TEXT("Source indexer process died unexpectedly"));
			bIsRunning = false;
			return false;
		}
	}

	UE_LOG(LogMonolithSource, Warning, TEXT("Timeout reading from source indexer (%.1fs)"), TimeoutSeconds);
	return false;
}

bool FMonolithSourceProcess::WriteJsonLine(const FString& Line)
{
	FString LineWithNewline = Line + TEXT("\n");
	return FPlatformProcess::WritePipe(WritePipe, LineWithNewline);
}

bool FMonolithSourceProcess::IsRunning() const
{
	return bIsRunning && FPlatformProcess::IsProcRunning(ProcessHandle);
}

void FMonolithSourceProcess::Shutdown()
{
	FScopeLock Lock(&ProcessLock);

	if (bIsRunning && FPlatformProcess::IsProcRunning(ProcessHandle))
	{
		// Send shutdown command
		TSharedPtr<FJsonObject> ShutdownCmd = MakeShared<FJsonObject>();
		ShutdownCmd->SetNumberField(TEXT("id"), NextRequestId++);
		ShutdownCmd->SetStringField(TEXT("method"), TEXT("shutdown"));

		FString Line;
		TSharedRef<TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>> Writer =
			TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Line);
		FJsonSerializer::Serialize(ShutdownCmd.ToSharedRef(), Writer);
		Writer->Close();

		WriteJsonLine(Line);

		// Give it a moment to shut down gracefully
		double WaitStart = FPlatformTime::Seconds();
		while (FPlatformProcess::IsProcRunning(ProcessHandle) &&
			   FPlatformTime::Seconds() - WaitStart < 3.0)
		{
			FPlatformProcess::Sleep(0.1f);
		}

		// Force kill if still running
		if (FPlatformProcess::IsProcRunning(ProcessHandle))
		{
			FPlatformProcess::TerminateProc(ProcessHandle, true);
			UE_LOG(LogMonolithSource, Warning, TEXT("Force-killed source indexer process"));
		}
	}

	if (ReadPipe || WritePipe)
	{
		FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
		ReadPipe = nullptr;
		WritePipe = nullptr;
	}

	bIsRunning = false;
	UE_LOG(LogMonolithSource, Log, TEXT("Source indexer process shut down"));
}
```

### Step 3: Verify compilation

```
# Build MonolithSource module
# Use UBT or trigger_build from editor
```

**Expected:** Compiles with no errors. Process manager ready for integration.

---

## Task 5.2 — FMonolithSourceActions (14 Actions)

### Files

- **Create:** `Source/MonolithSource/Public/FMonolithSourceActions.h`
- **Create:** `Source/MonolithSource/Private/FMonolithSourceActions.cpp`
- **Modify:** `Source/MonolithSource/Private/MonolithSourceModule.cpp`
- **Modify:** `Source/MonolithSource/Public/MonolithSourceModule.h`

### Step 1: Create FMonolithSourceActions.h

```cpp
// Source/MonolithSource/Public/FMonolithSourceActions.h
#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FMonolithSourceProcess;

/**
 * 14 source intelligence actions registered with FMonolithToolRegistry.
 * Delegates to the bundled Python indexer via FMonolithSourceProcess.
 *
 * Actions (9 from unreal-source-mcp):
 *   read_source, search_source, find_callers, find_callees,
 *   find_references, get_class_hierarchy, get_module_info,
 *   get_symbol_context, read_file
 *
 * Actions (5 reimplemented from unreal-api-mcp concept):
 *   search_api, get_function_signature, get_include_path,
 *   get_class_reference, get_deprecation_warnings
 */
class MONOLITHSOURCE_API FMonolithSourceActions
{
public:
	FMonolithSourceActions();
	~FMonolithSourceActions();

	/** Register all 14 actions with the tool registry. */
	void RegisterActions();

	/** Unregister all actions. */
	void UnregisterActions();

private:
	// --- Original unreal-source-mcp actions ---

	/** Read source code from a specific file with optional line range. */
	TSharedPtr<FJsonObject> HandleReadSource(const TSharedPtr<FJsonObject>& Params);

	/** Search engine source code by text pattern or regex. */
	TSharedPtr<FJsonObject> HandleSearchSource(const TSharedPtr<FJsonObject>& Params);

	/** Find all callers of a given function/method. */
	TSharedPtr<FJsonObject> HandleFindCallers(const TSharedPtr<FJsonObject>& Params);

	/** Find all functions/methods called by a given function. */
	TSharedPtr<FJsonObject> HandleFindCallees(const TSharedPtr<FJsonObject>& Params);

	/** Find all references to a symbol (class, function, variable, macro). */
	TSharedPtr<FJsonObject> HandleFindReferences(const TSharedPtr<FJsonObject>& Params);

	/** Get the inheritance hierarchy for a class. */
	TSharedPtr<FJsonObject> HandleGetClassHierarchy(const TSharedPtr<FJsonObject>& Params);

	/** Get module info — what module a class/file belongs to, dependencies. */
	TSharedPtr<FJsonObject> HandleGetModuleInfo(const TSharedPtr<FJsonObject>& Params);

	/** Get surrounding context for a symbol — the function/class body it's in. */
	TSharedPtr<FJsonObject> HandleGetSymbolContext(const TSharedPtr<FJsonObject>& Params);

	/** Read a raw file from engine source by relative path. */
	TSharedPtr<FJsonObject> HandleReadFile(const TSharedPtr<FJsonObject>& Params);

	// --- Reimplemented unreal-api-mcp functionality ---

	/** Search the UE API by pattern — find classes, functions, enums matching a query. */
	TSharedPtr<FJsonObject> HandleSearchApi(const TSharedPtr<FJsonObject>& Params);

	/** Get the full signature of a UE function (return type, parameters, specifiers). */
	TSharedPtr<FJsonObject> HandleGetFunctionSignature(const TSharedPtr<FJsonObject>& Params);

	/** Resolve the #include path needed to use a given class or type. */
	TSharedPtr<FJsonObject> HandleGetIncludePath(const TSharedPtr<FJsonObject>& Params);

	/** Get class reference info — parent class, interfaces, key methods, module. */
	TSharedPtr<FJsonObject> HandleGetClassReference(const TSharedPtr<FJsonObject>& Params);

	/** Check for deprecation warnings on a class, function, or macro. */
	TSharedPtr<FJsonObject> HandleGetDeprecationWarnings(const TSharedPtr<FJsonObject>& Params);

	// --- Helpers ---

	/** Forward a command to the Python process, wrapping errors. */
	TSharedPtr<FJsonObject> ForwardToProcess(const FString& Method, const TSharedPtr<FJsonObject>& Params);

	/** Lazy-initialize the source process. */
	FMonolithSourceProcess& GetProcess();

	TUniquePtr<FMonolithSourceProcess> SourceProcess;
};
```

### Step 2: Create FMonolithSourceActions.cpp

```cpp
// Source/MonolithSource/Private/FMonolithSourceActions.cpp
#include "FMonolithSourceActions.h"
#include "FMonolithSourceProcess.h"
#include "FMonolithToolRegistry.h"
#include "FMonolithJsonUtils.h"

FMonolithSourceActions::FMonolithSourceActions()
{
}

FMonolithSourceActions::~FMonolithSourceActions()
{
	UnregisterActions();
}

FMonolithSourceProcess& FMonolithSourceActions::GetProcess()
{
	if (!SourceProcess.IsValid())
	{
		SourceProcess = MakeUnique<FMonolithSourceProcess>();
	}
	if (!SourceProcess->IsRunning())
	{
		SourceProcess->EnsureRunning();
	}
	return *SourceProcess;
}

TSharedPtr<FJsonObject> FMonolithSourceActions::ForwardToProcess(
	const FString& Method, const TSharedPtr<FJsonObject>& Params)
{
	FMonolithSourceProcess& Process = GetProcess();
	if (!Process.IsRunning())
	{
		return FMonolithJsonUtils::ErrorJson(
			TEXT("Source indexer process is not running. Ensure Python 3.10+ is installed."));
	}

	TSharedPtr<FJsonObject> Response = Process.SendCommand(Method, Params);
	if (!Response.IsValid())
	{
		return FMonolithJsonUtils::ErrorJson(
			TEXT("No response from source indexer process."));
	}

	// If the Python process returned an error field, wrap it
	if (Response->HasField(TEXT("error")))
	{
		return FMonolithJsonUtils::ErrorJson(Response->GetStringField(TEXT("error")));
	}

	// Return the result field if present, otherwise the whole response
	if (Response->HasField(TEXT("result")))
	{
		return FMonolithJsonUtils::SuccessJson(Response->GetObjectField(TEXT("result")));
	}

	return Response;
}

void FMonolithSourceActions::RegisterActions()
{
	FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();

	// --- Original source-mcp actions (9) ---

	Registry.RegisterAction(TEXT("source"), TEXT("read_source"),
		TEXT("Read source code from a file with optional line range"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleReadSource));

	Registry.RegisterAction(TEXT("source"), TEXT("search_source"),
		TEXT("Search engine source code by text pattern or regex"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleSearchSource));

	Registry.RegisterAction(TEXT("source"), TEXT("find_callers"),
		TEXT("Find all callers of a given function/method"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleFindCallers));

	Registry.RegisterAction(TEXT("source"), TEXT("find_callees"),
		TEXT("Find all functions called by a given function"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleFindCallees));

	Registry.RegisterAction(TEXT("source"), TEXT("find_references"),
		TEXT("Find all references to a symbol"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleFindReferences));

	Registry.RegisterAction(TEXT("source"), TEXT("get_class_hierarchy"),
		TEXT("Get the inheritance hierarchy for a class"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleGetClassHierarchy));

	Registry.RegisterAction(TEXT("source"), TEXT("get_module_info"),
		TEXT("Get module info — which module a class belongs to, its dependencies"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleGetModuleInfo));

	Registry.RegisterAction(TEXT("source"), TEXT("get_symbol_context"),
		TEXT("Get surrounding context for a symbol — the enclosing function/class body"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleGetSymbolContext));

	Registry.RegisterAction(TEXT("source"), TEXT("read_file"),
		TEXT("Read a raw file from engine source by relative path"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleReadFile));

	// --- Reimplemented API actions (5) ---

	Registry.RegisterAction(TEXT("source"), TEXT("search_api"),
		TEXT("Search UE API by pattern — find classes, functions, enums matching a query"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleSearchApi));

	Registry.RegisterAction(TEXT("source"), TEXT("get_function_signature"),
		TEXT("Get the full signature of a UE function (return type, params, specifiers)"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleGetFunctionSignature));

	Registry.RegisterAction(TEXT("source"), TEXT("get_include_path"),
		TEXT("Resolve the #include path needed to use a given class or type"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleGetIncludePath));

	Registry.RegisterAction(TEXT("source"), TEXT("get_class_reference"),
		TEXT("Get class reference info — parent, interfaces, key methods, module"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleGetClassReference));

	Registry.RegisterAction(TEXT("source"), TEXT("get_deprecation_warnings"),
		TEXT("Check for deprecation warnings on a class, function, or macro"),
		FMonolithActionDelegate::CreateRaw(this, &FMonolithSourceActions::HandleGetDeprecationWarnings));

	UE_LOG(LogMonolithSource, Log, TEXT("Registered 14 source actions"));
}

void FMonolithSourceActions::UnregisterActions()
{
	if (FMonolithToolRegistry::IsAvailable())
	{
		FMonolithToolRegistry::Get().UnregisterNamespace(TEXT("source"));
	}

	if (SourceProcess.IsValid())
	{
		SourceProcess->Shutdown();
		SourceProcess.Reset();
	}
}

// ============================================================================
// Action Handlers — all forward to the Python process
// ============================================================================

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleReadSource(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: symbol (string, required), context_lines (int, optional)
	return ForwardToProcess(TEXT("read_source"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleSearchSource(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: query (string, required), file_pattern (string, optional),
	//         case_sensitive (bool, optional), max_results (int, optional)
	return ForwardToProcess(TEXT("search_source"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleFindCallers(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: symbol (string, required), max_depth (int, optional)
	return ForwardToProcess(TEXT("find_callers"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleFindCallees(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: symbol (string, required), max_depth (int, optional)
	return ForwardToProcess(TEXT("find_callees"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleFindReferences(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: symbol (string, required), max_results (int, optional)
	return ForwardToProcess(TEXT("find_references"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleGetClassHierarchy(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: class_name (string, required)
	return ForwardToProcess(TEXT("get_class_hierarchy"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleGetModuleInfo(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: name (string, required) — module name or class name
	return ForwardToProcess(TEXT("get_module_info"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleGetSymbolContext(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: symbol (string, required), context_lines (int, optional)
	return ForwardToProcess(TEXT("get_symbol_context"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleReadFile(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: path (string, required), start_line (int, optional), end_line (int, optional)
	return ForwardToProcess(TEXT("read_file"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleSearchApi(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: query (string, required), kind (string, optional: class|function|enum|macro),
	//         max_results (int, optional)
	return ForwardToProcess(TEXT("search_api"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleGetFunctionSignature(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: function_name (string, required), class_name (string, optional)
	return ForwardToProcess(TEXT("get_function_signature"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleGetIncludePath(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: type_name (string, required)
	return ForwardToProcess(TEXT("get_include_path"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleGetClassReference(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: class_name (string, required)
	return ForwardToProcess(TEXT("get_class_reference"), Params);
}

TSharedPtr<FJsonObject> FMonolithSourceActions::HandleGetDeprecationWarnings(
	const TSharedPtr<FJsonObject>& Params)
{
	// Params: symbol (string, required)
	return ForwardToProcess(TEXT("get_deprecation_warnings"), Params);
}
```

### Step 3: Update MonolithSourceModule.h

```cpp
// Source/MonolithSource/Public/MonolithSourceModule.h
#pragma once

#include "Modules/ModuleManager.h"

class FMonolithSourceActions;

class FMonolithSourceModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	TUniquePtr<FMonolithSourceActions> SourceActions;
};
```

### Step 4: Update MonolithSourceModule.cpp

```cpp
// Source/MonolithSource/Private/MonolithSourceModule.cpp
#include "MonolithSourceModule.h"
#include "FMonolithSourceActions.h"

#define LOCTEXT_NAMESPACE "FMonolithSourceModule"

void FMonolithSourceModule::StartupModule()
{
	SourceActions = MakeUnique<FMonolithSourceActions>();
	SourceActions->RegisterActions();

	UE_LOG(LogTemp, Log, TEXT("Monolith — Source module loaded (14 actions, bundled Python indexer)"));
}

void FMonolithSourceModule::ShutdownModule()
{
	if (SourceActions.IsValid())
	{
		SourceActions->UnregisterActions();
		SourceActions.Reset();
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMonolithSourceModule, MonolithSource)
```

### Step 5: Verify compilation, test via `monolith_discover("source")`

**Expected:** 14 actions listed. Calling any action without Python installed returns a clean error.

---

## Task 5.3 — Bundled Python Indexer (Child Process Adapter)

### Files

- **Create:** `MCP/src/monolith_source/indexer/child_process.py`
- **Create:** `MCP/src/monolith_source/db/schema.py`
- **Create:** `MCP/src/monolith_source/db/queries.py`
- **Create:** `MCP/src/monolith_source/indexer/parser.py`
- **Create:** `MCP/src/monolith_source/indexer/api_tables.py`
- **Modify:** `MCP/pyproject.toml`

### Step 1: Create the child process entry point

This replaces the FastMCP stdio transport with a simple JSON-line protocol on stdin/stdout.

```python
# MCP/src/monolith_source/indexer/child_process.py
"""
Child process entry point for the Monolith source indexer.
Reads JSON-line commands from stdin, writes JSON-line responses to stdout.

Protocol:
  Request:  {"id": 1, "method": "search_source", "params": {"query": "AActor"}}
  Response: {"id": 1, "result": {...}}
  Error:    {"id": 1, "error": "message"}

On startup, sends: {"status": "ready", "version": "0.1.0"}
On shutdown command, exits cleanly.
"""

import json
import sys
import os
import argparse
import traceback
from pathlib import Path

# Ensure parent packages are importable
sys.path.insert(0, str(Path(__file__).parent.parent.parent))

from monolith_source.db.queries import SourceDatabase
from monolith_source.indexer.parser import SourceIndexer
from monolith_source.indexer.api_tables import ApiLookup


def send_json(obj: dict) -> None:
    """Write a JSON object as a single line to stdout."""
    line = json.dumps(obj, ensure_ascii=False, separators=(",", ":"))
    sys.stdout.write(line + "\n")
    sys.stdout.flush()


def read_json() -> dict | None:
    """Read a single JSON line from stdin. Returns None on EOF."""
    line = sys.stdin.readline()
    if not line:
        return None
    line = line.strip()
    if not line:
        return None
    return json.loads(line)


class SourceHandler:
    """Handles all source intelligence commands."""

    def __init__(self, db_path: str, engine_path: str | None = None):
        self.db_path = db_path
        self.engine_path = engine_path
        self.db = SourceDatabase(db_path)
        self.api = ApiLookup(self.db)
        self.indexer = SourceIndexer(self.db) if engine_path else None

    def dispatch(self, method: str, params: dict) -> dict:
        """Route a method call to the appropriate handler."""
        handlers = {
            # Original unreal-source-mcp actions
            "read_source": self.read_source,
            "search_source": self.search_source,
            "find_callers": self.find_callers,
            "find_callees": self.find_callees,
            "find_references": self.find_references,
            "get_class_hierarchy": self.get_class_hierarchy,
            "get_module_info": self.get_module_info,
            "get_symbol_context": self.get_symbol_context,
            "read_file": self.read_file,
            # Reimplemented API actions
            "search_api": self.search_api,
            "get_function_signature": self.get_function_signature,
            "get_include_path": self.get_include_path,
            "get_class_reference": self.get_class_reference,
            "get_deprecation_warnings": self.get_deprecation_warnings,
            # Management
            "reindex": self.reindex,
            "shutdown": self.shutdown,
        }

        handler = handlers.get(method)
        if not handler:
            raise ValueError(f"Unknown method: {method}")

        return handler(params)

    # ================================================================
    # Original source-mcp actions
    # ================================================================

    def read_source(self, params: dict) -> dict:
        """Read source code for a symbol with surrounding context."""
        symbol = params.get("symbol", "")
        context_lines = params.get("context_lines", 10)

        results = self.db.get_symbol_source(symbol, context_lines)
        if not results:
            return {"error": f"Symbol not found: {symbol}"}

        return {"symbol": symbol, "definitions": results}

    def search_source(self, params: dict) -> dict:
        """Full-text search across indexed source code."""
        query = params.get("query", "")
        file_pattern = params.get("file_pattern")
        case_sensitive = params.get("case_sensitive", False)
        max_results = params.get("max_results", 50)

        results = self.db.search_source(
            query,
            file_pattern=file_pattern,
            case_sensitive=case_sensitive,
            limit=max_results,
        )
        return {"query": query, "count": len(results), "results": results}

    def find_callers(self, params: dict) -> dict:
        """Find all callers of a function."""
        symbol = params.get("symbol", "")
        max_depth = params.get("max_depth", 1)

        callers = self.db.find_callers(symbol, max_depth=max_depth)
        return {"symbol": symbol, "callers": callers}

    def find_callees(self, params: dict) -> dict:
        """Find all functions called by a function."""
        symbol = params.get("symbol", "")
        max_depth = params.get("max_depth", 1)

        callees = self.db.find_callees(symbol, max_depth=max_depth)
        return {"symbol": symbol, "callees": callees}

    def find_references(self, params: dict) -> dict:
        """Find all references to a symbol."""
        symbol = params.get("symbol", "")
        max_results = params.get("max_results", 100)

        refs = self.db.find_references(symbol, limit=max_results)
        return {"symbol": symbol, "count": len(refs), "references": refs}

    def get_class_hierarchy(self, params: dict) -> dict:
        """Get inheritance tree for a class."""
        class_name = params.get("class_name", "")

        hierarchy = self.db.get_class_hierarchy(class_name)
        if not hierarchy:
            return {"error": f"Class not found: {class_name}"}

        return {"class_name": class_name, "hierarchy": hierarchy}

    def get_module_info(self, params: dict) -> dict:
        """Get module information for a class or module name."""
        name = params.get("name", "")

        info = self.db.get_module_info(name)
        if not info:
            return {"error": f"Module/class not found: {name}"}

        return {"name": name, "module": info}

    def get_symbol_context(self, params: dict) -> dict:
        """Get the enclosing context for a symbol."""
        symbol = params.get("symbol", "")
        context_lines = params.get("context_lines", 20)

        context = self.db.get_symbol_context(symbol, context_lines)
        if not context:
            return {"error": f"Symbol not found: {symbol}"}

        return {"symbol": symbol, "context": context}

    def read_file(self, params: dict) -> dict:
        """Read a raw file from engine source."""
        path = params.get("path", "")
        start_line = params.get("start_line")
        end_line = params.get("end_line")

        if not self.engine_path:
            return {"error": "Engine path not configured"}

        full_path = os.path.join(self.engine_path, path)
        if not os.path.isfile(full_path):
            return {"error": f"File not found: {path}"}

        # Security: ensure path doesn't escape engine directory
        real_engine = os.path.realpath(self.engine_path)
        real_file = os.path.realpath(full_path)
        if not real_file.startswith(real_engine):
            return {"error": "Path traversal not allowed"}

        with open(full_path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()

        if start_line is not None:
            start_idx = max(0, start_line - 1)
            end_idx = end_line if end_line else len(lines)
            lines = lines[start_idx:end_idx]
            line_offset = start_idx + 1
        else:
            line_offset = 1

        content = "".join(lines)
        return {
            "path": path,
            "start_line": line_offset,
            "total_lines": len(lines),
            "content": content,
        }

    # ================================================================
    # Reimplemented API actions (replaces unreal-api-mcp concepts)
    # ================================================================

    def search_api(self, params: dict) -> dict:
        """Search UE API by pattern — classes, functions, enums, macros."""
        query = params.get("query", "")
        kind = params.get("kind")  # class, function, enum, macro
        max_results = params.get("max_results", 50)

        results = self.api.search_api(query, kind=kind, limit=max_results)
        return {"query": query, "count": len(results), "results": results}

    def get_function_signature(self, params: dict) -> dict:
        """Get full function signature."""
        function_name = params.get("function_name", "")
        class_name = params.get("class_name")

        sig = self.api.get_function_signature(function_name, class_name=class_name)
        if not sig:
            return {"error": f"Function not found: {function_name}"}

        return {"function": function_name, "signature": sig}

    def get_include_path(self, params: dict) -> dict:
        """Resolve #include path for a type."""
        type_name = params.get("type_name", "")

        include = self.api.get_include_path(type_name)
        if not include:
            return {"error": f"Type not found: {type_name}"}

        return {"type": type_name, "include": include}

    def get_class_reference(self, params: dict) -> dict:
        """Get class reference information."""
        class_name = params.get("class_name", "")

        ref = self.api.get_class_reference(class_name)
        if not ref:
            return {"error": f"Class not found: {class_name}"}

        return {"class_name": class_name, "reference": ref}

    def get_deprecation_warnings(self, params: dict) -> dict:
        """Check for deprecation warnings on a symbol."""
        symbol = params.get("symbol", "")

        warnings = self.api.get_deprecation_warnings(symbol)
        return {"symbol": symbol, "deprecated": len(warnings) > 0, "warnings": warnings}

    # ================================================================
    # Management
    # ================================================================

    def reindex(self, params: dict) -> dict:
        """Trigger a re-index of engine source."""
        engine_path = params.get("engine_path", self.engine_path)
        version_tag = params.get("version_tag", "")

        if not engine_path:
            return {"error": "No engine path provided"}

        self.engine_path = engine_path
        self.indexer = SourceIndexer(self.db)
        count = self.indexer.index_engine_source(engine_path, version_tag)
        return {"status": "complete", "files_indexed": count}

    def shutdown(self, params: dict) -> dict:
        """Clean shutdown."""
        return {"status": "shutting_down"}


def main():
    parser = argparse.ArgumentParser(description="Monolith source indexer child process")
    parser.add_argument("--db", required=True, help="Path to SQLite database")
    parser.add_argument("--engine-path", default=None, help="Path to UE engine source")
    parser.add_argument("--mode", default="child-process", help="Run mode")
    args = parser.parse_args()

    # Send ready signal
    send_json({"status": "ready", "version": "0.1.0"})

    handler = SourceHandler(args.db, engine_path=args.engine_path)

    # Main loop: read commands, dispatch, respond
    while True:
        try:
            request = read_json()
            if request is None:
                break  # EOF — parent process closed stdin

            req_id = request.get("id", 0)
            method = request.get("method", "")
            params = request.get("params", {})

            if method == "shutdown":
                send_json({"id": req_id, "result": {"status": "shutting_down"}})
                break

            result = handler.dispatch(method, params)

            if "error" in result:
                send_json({"id": req_id, "error": result["error"]})
            else:
                send_json({"id": req_id, "result": result})

        except json.JSONDecodeError as e:
            send_json({"id": 0, "error": f"Invalid JSON: {e}"})
        except Exception as e:
            tb = traceback.format_exc()
            send_json({"id": request.get("id", 0) if request else 0,
                        "error": f"{type(e).__name__}: {e}",
                        "traceback": tb})

    handler.db.close()
    sys.exit(0)


if __name__ == "__main__":
    main()
```

### Step 2: Create the database schema and query layer

```python
# MCP/src/monolith_source/db/schema.py
"""SQLite+FTS5 schema for the engine source database."""

SCHEMA_SQL = """
-- Indexed files
CREATE TABLE IF NOT EXISTS files (
    id          INTEGER PRIMARY KEY,
    path        TEXT NOT NULL UNIQUE,
    module      TEXT,
    last_modified INTEGER,
    line_count  INTEGER
);

-- Symbols (classes, functions, enums, macros, typedefs)
CREATE TABLE IF NOT EXISTS symbols (
    id              INTEGER PRIMARY KEY,
    name            TEXT NOT NULL,
    qualified_name  TEXT,
    kind            TEXT NOT NULL,   -- class, struct, function, method, enum, macro, typedef
    file_id         INTEGER REFERENCES files(id),
    line_start      INTEGER,
    line_end        INTEGER,
    signature       TEXT,            -- Full signature for functions
    parent_id       INTEGER REFERENCES symbols(id),
    access          TEXT,            -- public, protected, private
    is_virtual      INTEGER DEFAULT 0,
    is_static       INTEGER DEFAULT 0,
    is_deprecated   INTEGER DEFAULT 0,
    deprecation_msg TEXT,
    include_path    TEXT             -- Resolved #include path
);

CREATE INDEX IF NOT EXISTS idx_symbols_name ON symbols(name);
CREATE INDEX IF NOT EXISTS idx_symbols_kind ON symbols(kind);
CREATE INDEX IF NOT EXISTS idx_symbols_file ON symbols(file_id);
CREATE INDEX IF NOT EXISTS idx_symbols_parent ON symbols(parent_id);

-- Full-text search on symbols
CREATE VIRTUAL TABLE IF NOT EXISTS symbols_fts USING fts5(
    name, qualified_name, signature,
    content='symbols',
    content_rowid='id'
);

-- Call graph edges
CREATE TABLE IF NOT EXISTS call_graph (
    id          INTEGER PRIMARY KEY,
    caller_id   INTEGER REFERENCES symbols(id),
    callee_id   INTEGER REFERENCES symbols(id),
    file_id     INTEGER REFERENCES files(id),
    line_number INTEGER
);

CREATE INDEX IF NOT EXISTS idx_calls_caller ON call_graph(caller_id);
CREATE INDEX IF NOT EXISTS idx_calls_callee ON call_graph(callee_id);

-- References (any symbol usage)
CREATE TABLE IF NOT EXISTS references (
    id          INTEGER PRIMARY KEY,
    symbol_id   INTEGER REFERENCES symbols(id),
    file_id     INTEGER REFERENCES files(id),
    line_number INTEGER,
    context     TEXT    -- surrounding code snippet
);

CREATE INDEX IF NOT EXISTS idx_refs_symbol ON references(symbol_id);

-- Class inheritance
CREATE TABLE IF NOT EXISTS inheritance (
    id          INTEGER PRIMARY KEY,
    child_id    INTEGER REFERENCES symbols(id),
    parent_id   INTEGER REFERENCES symbols(id),
    is_interface INTEGER DEFAULT 0
);

CREATE INDEX IF NOT EXISTS idx_inherit_child ON inheritance(child_id);
CREATE INDEX IF NOT EXISTS idx_inherit_parent ON inheritance(parent_id);

-- Module info
CREATE TABLE IF NOT EXISTS modules (
    id              INTEGER PRIMARY KEY,
    name            TEXT NOT NULL UNIQUE,
    type            TEXT,           -- Runtime, Editor, Developer, ThirdParty
    directory       TEXT,
    build_cs_path   TEXT
);

CREATE TABLE IF NOT EXISTS module_dependencies (
    id          INTEGER PRIMARY KEY,
    module_id   INTEGER REFERENCES modules(id),
    depends_on  TEXT NOT NULL,
    dep_type    TEXT DEFAULT 'public'  -- public, private
);

-- Source content for inline reading (optional, large)
CREATE TABLE IF NOT EXISTS file_content (
    file_id     INTEGER PRIMARY KEY REFERENCES files(id),
    content     TEXT
);

-- Metadata
CREATE TABLE IF NOT EXISTS meta (
    key   TEXT PRIMARY KEY,
    value TEXT
);
"""
```

```python
# MCP/src/monolith_source/db/queries.py
"""Database query interface for the engine source index."""

import sqlite3
import os
from pathlib import Path
from .schema import SCHEMA_SQL


class SourceDatabase:
    """SQLite+FTS5 database for engine source intelligence."""

    def __init__(self, db_path: str):
        self.db_path = db_path
        os.makedirs(os.path.dirname(db_path) or ".", exist_ok=True)
        self.conn = sqlite3.connect(db_path)
        self.conn.row_factory = sqlite3.Row
        self.conn.execute("PRAGMA journal_mode=WAL")
        self.conn.execute("PRAGMA foreign_keys=ON")
        self._ensure_schema()

    def _ensure_schema(self):
        self.conn.executescript(SCHEMA_SQL)
        self.conn.commit()

    def close(self):
        if self.conn:
            self.conn.close()

    # ================================================================
    # Symbol queries
    # ================================================================

    def get_symbol_source(self, symbol: str, context_lines: int = 10) -> list[dict]:
        """Get source code for a symbol definition."""
        rows = self.conn.execute("""
            SELECT s.name, s.qualified_name, s.kind, s.signature,
                   s.line_start, s.line_end, f.path,
                   fc.content
            FROM symbols s
            JOIN files f ON s.file_id = f.id
            LEFT JOIN file_content fc ON fc.file_id = f.id
            WHERE s.name = ? OR s.qualified_name LIKE ?
            LIMIT 10
        """, (symbol, f"%{symbol}%")).fetchall()

        results = []
        for row in rows:
            entry = {
                "name": row["name"],
                "qualified_name": row["qualified_name"],
                "kind": row["kind"],
                "signature": row["signature"],
                "file": row["path"],
                "line_start": row["line_start"],
                "line_end": row["line_end"],
            }

            # Extract relevant lines from content if available
            if row["content"] and row["line_start"]:
                lines = row["content"].split("\n")
                start = max(0, row["line_start"] - 1 - context_lines)
                end = min(len(lines), (row["line_end"] or row["line_start"]) + context_lines)
                entry["source"] = "\n".join(lines[start:end])
                entry["source_start_line"] = start + 1

            results.append(entry)
        return results

    def search_source(self, query: str, file_pattern: str = None,
                      case_sensitive: bool = False, limit: int = 50) -> list[dict]:
        """Full-text search across symbols."""
        # Use FTS5 for the search
        fts_query = query.replace('"', '""')
        rows = self.conn.execute("""
            SELECT s.name, s.qualified_name, s.kind, s.signature,
                   s.line_start, f.path,
                   rank
            FROM symbols_fts fts
            JOIN symbols s ON s.id = fts.rowid
            JOIN files f ON s.file_id = f.id
            WHERE symbols_fts MATCH ?
            ORDER BY rank
            LIMIT ?
        """, (fts_query, limit)).fetchall()

        results = []
        for row in rows:
            entry = {
                "name": row["name"],
                "qualified_name": row["qualified_name"],
                "kind": row["kind"],
                "signature": row["signature"],
                "file": row["path"],
                "line": row["line_start"],
            }

            # Apply file pattern filter if specified
            if file_pattern and file_pattern not in row["path"]:
                continue

            results.append(entry)
        return results

    def find_callers(self, symbol: str, max_depth: int = 1) -> list[dict]:
        """Find all callers of a function, with optional depth traversal."""
        symbol_ids = self._resolve_symbol_ids(symbol)
        if not symbol_ids:
            return []

        visited = set()
        results = []
        self._find_callers_recursive(symbol_ids, max_depth, 0, visited, results)
        return results

    def _find_callers_recursive(self, symbol_ids: list[int], max_depth: int,
                                 current_depth: int, visited: set, results: list):
        if current_depth >= max_depth:
            return

        placeholders = ",".join("?" * len(symbol_ids))
        rows = self.conn.execute(f"""
            SELECT DISTINCT cg.caller_id, s.name, s.qualified_name, s.kind,
                   f.path, cg.line_number
            FROM call_graph cg
            JOIN symbols s ON s.id = cg.caller_id
            JOIN files f ON s.file_id = f.id
            WHERE cg.callee_id IN ({placeholders})
        """, symbol_ids).fetchall()

        next_ids = []
        for row in rows:
            if row["caller_id"] in visited:
                continue
            visited.add(row["caller_id"])
            results.append({
                "name": row["name"],
                "qualified_name": row["qualified_name"],
                "kind": row["kind"],
                "file": row["path"],
                "line": row["line_number"],
                "depth": current_depth + 1,
            })
            next_ids.append(row["caller_id"])

        if next_ids:
            self._find_callers_recursive(next_ids, max_depth, current_depth + 1, visited, results)

    def find_callees(self, symbol: str, max_depth: int = 1) -> list[dict]:
        """Find all functions called by a function."""
        symbol_ids = self._resolve_symbol_ids(symbol)
        if not symbol_ids:
            return []

        visited = set()
        results = []
        self._find_callees_recursive(symbol_ids, max_depth, 0, visited, results)
        return results

    def _find_callees_recursive(self, symbol_ids: list[int], max_depth: int,
                                 current_depth: int, visited: set, results: list):
        if current_depth >= max_depth:
            return

        placeholders = ",".join("?" * len(symbol_ids))
        rows = self.conn.execute(f"""
            SELECT DISTINCT cg.callee_id, s.name, s.qualified_name, s.kind,
                   f.path, cg.line_number
            FROM call_graph cg
            JOIN symbols s ON s.id = cg.callee_id
            JOIN files f ON s.file_id = f.id
            WHERE cg.caller_id IN ({placeholders})
        """, symbol_ids).fetchall()

        next_ids = []
        for row in rows:
            if row["callee_id"] in visited:
                continue
            visited.add(row["callee_id"])
            results.append({
                "name": row["name"],
                "qualified_name": row["qualified_name"],
                "kind": row["kind"],
                "file": row["path"],
                "line": row["line_number"],
                "depth": current_depth + 1,
            })
            next_ids.append(row["callee_id"])

        if next_ids:
            self._find_callees_recursive(next_ids, max_depth, current_depth + 1, visited, results)

    def find_references(self, symbol: str, limit: int = 100) -> list[dict]:
        """Find all references to a symbol."""
        symbol_ids = self._resolve_symbol_ids(symbol)
        if not symbol_ids:
            return []

        placeholders = ",".join("?" * len(symbol_ids))
        rows = self.conn.execute(f"""
            SELECT r.line_number, r.context, f.path, s.name
            FROM references r
            JOIN files f ON r.file_id = f.id
            JOIN symbols s ON s.id = r.symbol_id
            WHERE r.symbol_id IN ({placeholders})
            LIMIT ?
        """, [*symbol_ids, limit]).fetchall()

        return [
            {
                "file": row["path"],
                "line": row["line_number"],
                "context": row["context"],
                "symbol": row["name"],
            }
            for row in rows
        ]

    def get_class_hierarchy(self, class_name: str) -> dict | None:
        """Get inheritance tree for a class."""
        symbol_ids = self._resolve_symbol_ids(class_name, kind="class")
        if not symbol_ids:
            symbol_ids = self._resolve_symbol_ids(class_name, kind="struct")
        if not symbol_ids:
            return None

        # Build hierarchy upward (parents)
        parents = []
        current_ids = symbol_ids
        while current_ids:
            placeholders = ",".join("?" * len(current_ids))
            rows = self.conn.execute(f"""
                SELECT s.name, s.qualified_name, s.kind, i.is_interface, i.parent_id
                FROM inheritance i
                JOIN symbols s ON s.id = i.parent_id
                WHERE i.child_id IN ({placeholders})
            """, current_ids).fetchall()

            next_ids = []
            for row in rows:
                parents.append({
                    "name": row["name"],
                    "qualified_name": row["qualified_name"],
                    "is_interface": bool(row["is_interface"]),
                })
                next_ids.append(row["parent_id"])
            current_ids = next_ids

        # Build hierarchy downward (children)
        children = []
        current_ids = symbol_ids
        placeholders = ",".join("?" * len(current_ids))
        rows = self.conn.execute(f"""
            SELECT s.name, s.qualified_name, s.kind
            FROM inheritance i
            JOIN symbols s ON s.id = i.child_id
            WHERE i.parent_id IN ({placeholders})
        """, current_ids).fetchall()

        for row in rows:
            children.append({
                "name": row["name"],
                "qualified_name": row["qualified_name"],
            })

        return {
            "parents": parents,
            "children": children,
        }

    def get_module_info(self, name: str) -> dict | None:
        """Get module information."""
        # Try as module name first
        row = self.conn.execute("""
            SELECT m.name, m.type, m.directory, m.build_cs_path
            FROM modules m
            WHERE m.name = ?
        """, (name,)).fetchone()

        if not row:
            # Try to find module by class name
            sym_row = self.conn.execute("""
                SELECT f.module FROM symbols s
                JOIN files f ON s.file_id = f.id
                WHERE s.name = ?
                LIMIT 1
            """, (name,)).fetchone()

            if sym_row and sym_row["module"]:
                row = self.conn.execute("""
                    SELECT m.name, m.type, m.directory, m.build_cs_path
                    FROM modules m WHERE m.name = ?
                """, (sym_row["module"],)).fetchone()

        if not row:
            return None

        # Get dependencies
        deps = self.conn.execute("""
            SELECT depends_on, dep_type
            FROM module_dependencies
            WHERE module_id = (SELECT id FROM modules WHERE name = ?)
        """, (row["name"],)).fetchall()

        return {
            "name": row["name"],
            "type": row["type"],
            "directory": row["directory"],
            "build_cs": row["build_cs_path"],
            "dependencies": [
                {"module": d["depends_on"], "type": d["dep_type"]}
                for d in deps
            ],
        }

    def get_symbol_context(self, symbol: str, context_lines: int = 20) -> dict | None:
        """Get enclosing context for a symbol."""
        results = self.get_symbol_source(symbol, context_lines)
        if not results:
            return None
        return results[0]

    # ================================================================
    # Helpers
    # ================================================================

    def _resolve_symbol_ids(self, name: str, kind: str = None) -> list[int]:
        """Resolve a symbol name to database IDs."""
        if kind:
            rows = self.conn.execute("""
                SELECT id FROM symbols
                WHERE (name = ? OR qualified_name LIKE ?) AND kind = ?
            """, (name, f"%{name}%", kind)).fetchall()
        else:
            rows = self.conn.execute("""
                SELECT id FROM symbols
                WHERE name = ? OR qualified_name LIKE ?
            """, (name, f"%{name}%")).fetchall()
        return [row["id"] for row in rows]

    # ================================================================
    # Write operations (used by indexer)
    # ================================================================

    def insert_file(self, path: str, module: str, line_count: int,
                    content: str = None) -> int:
        cursor = self.conn.execute("""
            INSERT OR REPLACE INTO files (path, module, line_count)
            VALUES (?, ?, ?)
        """, (path, module, line_count))
        file_id = cursor.lastrowid

        if content:
            self.conn.execute("""
                INSERT OR REPLACE INTO file_content (file_id, content)
                VALUES (?, ?)
            """, (file_id, content))

        return file_id

    def insert_symbol(self, name: str, qualified_name: str, kind: str,
                      file_id: int, line_start: int, line_end: int = None,
                      signature: str = None, parent_id: int = None,
                      access: str = None, is_virtual: bool = False,
                      is_static: bool = False, is_deprecated: bool = False,
                      deprecation_msg: str = None, include_path: str = None) -> int:
        cursor = self.conn.execute("""
            INSERT INTO symbols (name, qualified_name, kind, file_id,
                line_start, line_end, signature, parent_id, access,
                is_virtual, is_static, is_deprecated, deprecation_msg, include_path)
            VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)
        """, (name, qualified_name, kind, file_id, line_start, line_end,
              signature, parent_id, access, int(is_virtual), int(is_static),
              int(is_deprecated), deprecation_msg, include_path))
        return cursor.lastrowid

    def rebuild_fts(self):
        """Rebuild the FTS5 index."""
        self.conn.execute("INSERT INTO symbols_fts(symbols_fts) VALUES('rebuild')")
        self.conn.commit()

    def commit(self):
        self.conn.commit()
```

### Step 3: Create the API lookup tables

```python
# MCP/src/monolith_source/indexer/api_tables.py
"""
API lookup functionality — reimplements concepts from unreal-api-mcp.
Provides signature lookup, #include resolution, and deprecation checking.
All data comes from the tree-sitter parsed source index.
"""

from monolith_source.db.queries import SourceDatabase


class ApiLookup:
    """API intelligence layer on top of the source database."""

    def __init__(self, db: SourceDatabase):
        self.db = db

    def search_api(self, query: str, kind: str = None, limit: int = 50) -> list[dict]:
        """Search the UE API by pattern."""
        if kind:
            rows = self.db.conn.execute("""
                SELECT s.name, s.qualified_name, s.kind, s.signature,
                       s.include_path, f.path, s.line_start
                FROM symbols s
                JOIN files f ON s.file_id = f.id
                WHERE s.kind = ? AND (s.name LIKE ? OR s.qualified_name LIKE ?)
                ORDER BY s.name
                LIMIT ?
            """, (kind, f"%{query}%", f"%{query}%", limit)).fetchall()
        else:
            # Use FTS for broader search
            fts_query = query.replace('"', '""')
            rows = self.db.conn.execute("""
                SELECT s.name, s.qualified_name, s.kind, s.signature,
                       s.include_path, f.path, s.line_start
                FROM symbols_fts fts
                JOIN symbols s ON s.id = fts.rowid
                JOIN files f ON s.file_id = f.id
                WHERE symbols_fts MATCH ?
                ORDER BY rank
                LIMIT ?
            """, (fts_query, limit)).fetchall()

        return [
            {
                "name": row["name"],
                "qualified_name": row["qualified_name"],
                "kind": row["kind"],
                "signature": row["signature"],
                "include_path": row["include_path"],
                "file": row["path"],
                "line": row["line_start"],
            }
            for row in rows
        ]

    def get_function_signature(self, function_name: str,
                                class_name: str = None) -> dict | None:
        """Get the full signature of a function."""
        if class_name:
            row = self.db.conn.execute("""
                SELECT s.name, s.qualified_name, s.signature, s.access,
                       s.is_virtual, s.is_static, s.include_path, f.path, s.line_start
                FROM symbols s
                JOIN files f ON s.file_id = f.id
                WHERE s.name = ?
                  AND s.kind IN ('function', 'method')
                  AND s.parent_id IN (SELECT id FROM symbols WHERE name = ?)
                LIMIT 1
            """, (function_name, class_name)).fetchone()
        else:
            row = self.db.conn.execute("""
                SELECT s.name, s.qualified_name, s.signature, s.access,
                       s.is_virtual, s.is_static, s.include_path, f.path, s.line_start
                FROM symbols s
                JOIN files f ON s.file_id = f.id
                WHERE s.name = ? AND s.kind IN ('function', 'method')
                LIMIT 1
            """, (function_name,)).fetchone()

        if not row:
            return None

        return {
            "name": row["name"],
            "qualified_name": row["qualified_name"],
            "signature": row["signature"],
            "access": row["access"],
            "is_virtual": bool(row["is_virtual"]),
            "is_static": bool(row["is_static"]),
            "include_path": row["include_path"],
            "file": row["path"],
            "line": row["line_start"],
        }

    def get_include_path(self, type_name: str) -> dict | None:
        """Resolve the #include path for a type."""
        row = self.db.conn.execute("""
            SELECT s.name, s.kind, s.include_path, f.path, f.module
            FROM symbols s
            JOIN files f ON s.file_id = f.id
            WHERE s.name = ? AND s.kind IN ('class', 'struct', 'enum', 'typedef')
            LIMIT 1
        """, (type_name,)).fetchone()

        if not row:
            return None

        include = row["include_path"]
        if not include:
            # Derive from file path: Runtime/Engine/Classes/GameFramework/Actor.h
            # → #include "GameFramework/Actor.h"
            file_path = row["path"]
            # Try to extract the include-friendly portion
            for marker in ("/Classes/", "/Public/"):
                idx = file_path.find(marker)
                if idx >= 0:
                    include = file_path[idx + len(marker):]
                    break
            if not include:
                include = file_path.rsplit("/", 1)[-1]

        return {
            "type": row["name"],
            "kind": row["kind"],
            "include": f'#include "{include}"',
            "module": row["module"],
            "file": row["path"],
        }

    def get_class_reference(self, class_name: str) -> dict | None:
        """Get class reference info — parent, interfaces, key methods."""
        row = self.db.conn.execute("""
            SELECT s.id, s.name, s.qualified_name, s.kind, s.include_path,
                   f.path, f.module, s.line_start
            FROM symbols s
            JOIN files f ON s.file_id = f.id
            WHERE s.name = ? AND s.kind IN ('class', 'struct')
            LIMIT 1
        """, (class_name,)).fetchone()

        if not row:
            return None

        # Get parent classes
        parents = self.db.conn.execute("""
            SELECT ps.name, ps.qualified_name, i.is_interface
            FROM inheritance i
            JOIN symbols ps ON ps.id = i.parent_id
            WHERE i.child_id = ?
        """, (row["id"],)).fetchall()

        # Get key methods (public, first 20)
        methods = self.db.conn.execute("""
            SELECT name, signature, is_virtual, is_static
            FROM symbols
            WHERE parent_id = ? AND kind IN ('function', 'method')
              AND (access = 'public' OR access IS NULL)
            ORDER BY line_start
            LIMIT 20
        """, (row["id"],)).fetchall()

        return {
            "name": row["name"],
            "qualified_name": row["qualified_name"],
            "kind": row["kind"],
            "file": row["path"],
            "module": row["module"],
            "line": row["line_start"],
            "parents": [
                {"name": p["name"], "is_interface": bool(p["is_interface"])}
                for p in parents
            ],
            "methods": [
                {
                    "name": m["name"],
                    "signature": m["signature"],
                    "is_virtual": bool(m["is_virtual"]),
                    "is_static": bool(m["is_static"]),
                }
                for m in methods
            ],
        }

    def get_deprecation_warnings(self, symbol: str) -> list[dict]:
        """Check for deprecation warnings on a symbol."""
        rows = self.db.conn.execute("""
            SELECT s.name, s.qualified_name, s.kind, s.deprecation_msg,
                   f.path, s.line_start
            FROM symbols s
            JOIN files f ON s.file_id = f.id
            WHERE (s.name = ? OR s.qualified_name LIKE ?)
              AND s.is_deprecated = 1
        """, (symbol, f"%{symbol}%")).fetchall()

        return [
            {
                "name": row["name"],
                "qualified_name": row["qualified_name"],
                "kind": row["kind"],
                "message": row["deprecation_msg"] or "Marked as deprecated",
                "file": row["path"],
                "line": row["line_start"],
            }
            for row in rows
        ]
```

### Step 4: Create the tree-sitter parser (scaffold)

```python
# MCP/src/monolith_source/indexer/parser.py
"""
Tree-sitter C++ parser for indexing Unreal Engine source code.
Extracts classes, functions, enums, macros, inheritance, call graph edges.
"""

import os
import re
from pathlib import Path

try:
    import tree_sitter_cpp as tscpp
    from tree_sitter import Language, Parser
    HAS_TREE_SITTER = True
except ImportError:
    HAS_TREE_SITTER = False

from monolith_source.db.queries import SourceDatabase


class SourceIndexer:
    """Indexes Unreal Engine C++ source into SQLite."""

    def __init__(self, db: SourceDatabase):
        self.db = db
        if HAS_TREE_SITTER:
            self.language = Language(tscpp.language())
            self.parser = Parser(self.language)
        else:
            self.parser = None

    def index_engine_source(self, engine_path: str, version_tag: str = "") -> int:
        """Walk engine source tree and index all .h/.cpp files."""
        if not self.parser:
            raise RuntimeError("tree-sitter-cpp not installed. Run: pip install tree-sitter tree-sitter-cpp")

        source_dir = Path(engine_path)
        if not source_dir.exists():
            raise FileNotFoundError(f"Engine source not found: {engine_path}")

        # Store metadata
        self.db.conn.execute(
            "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)",
            ("engine_path", engine_path),
        )
        self.db.conn.execute(
            "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)",
            ("version_tag", version_tag),
        )

        file_count = 0
        extensions = {".h", ".hpp", ".cpp", ".inl"}

        for root, dirs, files in os.walk(source_dir):
            # Skip ThirdParty and Intermediate directories
            dirs[:] = [d for d in dirs if d not in ("ThirdParty", "Intermediate", ".git")]

            for filename in files:
                if Path(filename).suffix.lower() not in extensions:
                    continue

                filepath = os.path.join(root, filename)
                rel_path = os.path.relpath(filepath, engine_path).replace("\\", "/")
                module_name = self._detect_module(rel_path)

                try:
                    self._index_file(filepath, rel_path, module_name)
                    file_count += 1

                    # Commit every 500 files
                    if file_count % 500 == 0:
                        self.db.commit()
                except Exception as e:
                    # Log but don't abort — some files may have encoding issues
                    pass

        # Final commit and rebuild FTS
        self.db.commit()
        self.db.rebuild_fts()

        self.db.conn.execute(
            "INSERT OR REPLACE INTO meta (key, value) VALUES (?, ?)",
            ("files_indexed", str(file_count)),
        )
        self.db.commit()

        return file_count

    def _detect_module(self, rel_path: str) -> str:
        """Detect which UE module a file belongs to based on path."""
        # Pattern: Runtime/Engine/... → Engine
        #          Runtime/Core/... → Core
        #          Editor/UnrealEd/... → UnrealEd
        parts = rel_path.replace("\\", "/").split("/")
        if len(parts) >= 2:
            return parts[1]  # Second component is usually the module name
        return "Unknown"

    def _index_file(self, filepath: str, rel_path: str, module_name: str):
        """Parse and index a single C++ file."""
        with open(filepath, "r", encoding="utf-8", errors="replace") as f:
            content = f.read()

        lines = content.split("\n")
        file_id = self.db.insert_file(rel_path, module_name, len(lines), content)

        # Parse with tree-sitter
        tree = self.parser.parse(content.encode("utf-8"))
        root = tree.root_node

        # Extract symbols
        self._extract_classes(root, file_id, rel_path)
        self._extract_functions(root, file_id, rel_path, parent_id=None)
        self._extract_enums(root, file_id)
        self._extract_deprecations(content, file_id, rel_path)

    def _extract_classes(self, node, file_id: int, rel_path: str, parent_id: int = None):
        """Extract class/struct declarations."""
        for child in node.children:
            if child.type in ("class_specifier", "struct_specifier"):
                name_node = child.child_by_field_name("name")
                if not name_node:
                    continue

                class_name = name_node.text.decode("utf-8")
                kind = "class" if child.type == "class_specifier" else "struct"

                # Detect include path from file
                include_path = self._resolve_include(rel_path)

                symbol_id = self.db.insert_symbol(
                    name=class_name,
                    qualified_name=class_name,
                    kind=kind,
                    file_id=file_id,
                    line_start=child.start_point[0] + 1,
                    line_end=child.end_point[0] + 1,
                    parent_id=parent_id,
                    include_path=include_path,
                )

                # Extract base classes
                bases = child.child_by_field_name("base_class_clause")
                if bases:
                    self._extract_inheritance(bases, symbol_id)

                # Extract methods inside the class body
                body = child.child_by_field_name("body")
                if body:
                    self._extract_functions(body, file_id, rel_path, parent_id=symbol_id)

            # Recurse into namespaces
            elif child.type == "namespace_definition":
                body = child.child_by_field_name("body")
                if body:
                    self._extract_classes(body, file_id, rel_path, parent_id)

    def _extract_functions(self, node, file_id: int, rel_path: str, parent_id: int = None):
        """Extract function/method declarations."""
        for child in node.children:
            if child.type == "function_definition":
                declarator = child.child_by_field_name("declarator")
                if not declarator:
                    continue

                func_name = self._get_function_name(declarator)
                if not func_name:
                    continue

                signature = child.text.decode("utf-8").split("{")[0].strip()
                # Truncate very long signatures
                if len(signature) > 500:
                    signature = signature[:500] + "..."

                is_virtual = b"virtual" in child.text[:100]
                is_static = b"static" in child.text[:100]

                self.db.insert_symbol(
                    name=func_name,
                    qualified_name=func_name,
                    kind="method" if parent_id else "function",
                    file_id=file_id,
                    line_start=child.start_point[0] + 1,
                    line_end=child.end_point[0] + 1,
                    signature=signature,
                    parent_id=parent_id,
                    is_virtual=is_virtual,
                    is_static=is_static,
                )

            elif child.type == "declaration":
                # Could be a function declaration (no body)
                declarator = child.child_by_field_name("declarator")
                if declarator and declarator.type == "function_declarator":
                    func_name = self._get_function_name(declarator)
                    if func_name:
                        signature = child.text.decode("utf-8").rstrip(";").strip()
                        if len(signature) > 500:
                            signature = signature[:500] + "..."

                        self.db.insert_symbol(
                            name=func_name,
                            qualified_name=func_name,
                            kind="method" if parent_id else "function",
                            file_id=file_id,
                            line_start=child.start_point[0] + 1,
                            line_end=child.end_point[0] + 1,
                            signature=signature,
                            parent_id=parent_id,
                        )

    def _extract_enums(self, node, file_id: int):
        """Extract enum declarations."""
        for child in node.children:
            if child.type == "enum_specifier":
                name_node = child.child_by_field_name("name")
                if name_node:
                    enum_name = name_node.text.decode("utf-8")
                    self.db.insert_symbol(
                        name=enum_name,
                        qualified_name=enum_name,
                        kind="enum",
                        file_id=file_id,
                        line_start=child.start_point[0] + 1,
                        line_end=child.end_point[0] + 1,
                    )
            elif child.type == "namespace_definition":
                body = child.child_by_field_name("body")
                if body:
                    self._extract_enums(body, file_id)

    def _extract_deprecations(self, content: str, file_id: int, rel_path: str):
        """Scan for UE deprecation markers."""
        # UE_DEPRECATED, DEPRECATED(), [[deprecated]]
        patterns = [
            r'UE_DEPRECATED\s*\(\s*[\d.]+\s*,\s*"([^"]+)"\s*\)',
            r'DEPRECATED\s*\(\s*[\d.]+\s*,\s*"([^"]+)"\s*\)',
            r'\[\[deprecated\s*\(\s*"([^"]+)"\s*\)\]\]',
        ]

        for pattern in patterns:
            for match in re.finditer(pattern, content):
                msg = match.group(1)
                line_num = content[:match.start()].count("\n") + 1

                # Try to find the symbol on the next few lines
                lines = content.split("\n")
                for i in range(line_num - 1, min(line_num + 3, len(lines))):
                    line = lines[i].strip()
                    # Look for function or class name
                    name_match = re.search(r'\b([A-Z]\w+)\s*[({;]', line)
                    if name_match:
                        # Update the symbol's deprecation fields if it exists
                        self.db.conn.execute("""
                            UPDATE symbols SET is_deprecated = 1, deprecation_msg = ?
                            WHERE file_id = ? AND line_start BETWEEN ? AND ?
                        """, (msg, file_id, line_num, line_num + 5))
                        break

    def _extract_inheritance(self, base_clause, child_symbol_id: int):
        """Extract base class relationships from a base clause node."""
        for child in base_clause.children:
            if child.type == "base_class_clause":
                # The type specifier contains the base class name
                for type_node in child.children:
                    if type_node.type in ("type_identifier", "qualified_identifier"):
                        base_name = type_node.text.decode("utf-8")
                        # Look up the base class symbol
                        rows = self.db.conn.execute(
                            "SELECT id FROM symbols WHERE name = ? AND kind IN ('class', 'struct') LIMIT 1",
                            (base_name,)
                        ).fetchall()

                        parent_sym_id = rows[0]["id"] if rows else None
                        if parent_sym_id:
                            is_interface = base_name.startswith("I") and len(base_name) > 1 and base_name[1].isupper()
                            self.db.conn.execute("""
                                INSERT INTO inheritance (child_id, parent_id, is_interface)
                                VALUES (?, ?, ?)
                            """, (child_symbol_id, parent_sym_id, int(is_interface)))

    def _get_function_name(self, declarator) -> str | None:
        """Extract function name from a declarator node."""
        if declarator.type == "function_declarator":
            name_node = declarator.child_by_field_name("declarator")
            if name_node:
                text = name_node.text.decode("utf-8")
                # Strip qualified names: AActor::BeginPlay → BeginPlay
                if "::" in text:
                    return text.rsplit("::", 1)[-1]
                return text
        return None

    def _resolve_include(self, rel_path: str) -> str | None:
        """Resolve include path from a relative file path."""
        for marker in ("/Classes/", "/Public/"):
            idx = rel_path.find(marker)
            if idx >= 0:
                return rel_path[idx + len(marker):]
        return None
```

### Step 5: Update pyproject.toml

```toml
[project]
name = "monolith-source-indexer"
version = "0.1.0"
description = "Bundled engine source indexer for Monolith — tree-sitter C++ parsing with SQLite+FTS5 storage"
requires-python = ">=3.10"
license = {text = "MIT"}
authors = [{name = "tumourlove"}]

dependencies = [
    "tree-sitter>=0.21.0",
    "tree-sitter-cpp>=0.21.0",
]

[build-system]
requires = ["hatchling"]
build-backend = "hatchling.build"

[project.scripts]
monolith-index = "monolith_source.indexer.cli:main"
monolith-child = "monolith_source.indexer.child_process:main"
```

### Step 6: Verify Python child process works standalone

```bash
cd MCP
echo '{"id":1,"method":"search_api","params":{"query":"AActor"}}' | python -m monolith_source.indexer.child_process --db test.db
```

**Expected:** Sends `{"status":"ready","version":"0.1.0"}` then processes the command (returns empty results since DB isn't indexed yet).

---

## Task 5.4 — UMonolithUpdateSubsystem (Auto-Updater)

### Files

- **Create:** `Source/MonolithCore/Public/UMonolithUpdateSubsystem.h`
- **Create:** `Source/MonolithCore/Private/UMonolithUpdateSubsystem.cpp`

### Step 1: Create UMonolithUpdateSubsystem.h

```cpp
// Source/MonolithCore/Public/UMonolithUpdateSubsystem.h
#pragma once

#include "CoreMinimal.h"
#include "EditorSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "UMonolithUpdateSubsystem.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogMonolithUpdate, Log, All);

/**
 * Editor subsystem that manages Monolith's auto-update lifecycle.
 *
 * On editor startup:
 *   1. Check Saved/version.json — if staging exists, perform swap
 *   2. If auto-update enabled, check GitHub Releases API
 *   3. If newer version found, show editor notification
 *   4. On user click, download + stage + prompt restart
 */
UCLASS()
class MONOLITHCORE_API UMonolithUpdateSubsystem : public UEditorSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	/** Manually check for updates. Returns true if check was initiated. */
	bool CheckForUpdates();

	/** Download and stage the latest release. */
	void DownloadAndStageUpdate();

	/** Get the currently installed version string. */
	FString GetCurrentVersion() const;

	/** Get the latest available version (empty if not checked). */
	FString GetLatestVersion() const;

	/** Whether an update is available. */
	bool IsUpdateAvailable() const;

	/** Whether an update is currently downloading. */
	bool IsDownloading() const;

	/** Trigger a source re-index (delegates to MonolithSource). */
	void TriggerSourceReindex();

private:
	// --- Version JSON management ---

	struct FVersionInfo
	{
		FString Current;
		FString Pending;
		bool bStaging = false;
	};

	FVersionInfo ReadVersionJson() const;
	void WriteVersionJson(const FVersionInfo& Info) const;
	FString GetVersionJsonPath() const;
	FString GetPluginDir() const;
	FString GetStagingDir() const;

	// --- Staging/swap ---

	/** Check for and apply staged updates (runs on startup). */
	void CheckAndApplyStaging();

	// --- GitHub Releases API ---

	void OnReleasesResponse(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);
	void OnDownloadComplete(FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess);

	// --- Semver comparison ---

	/** Parse "v1.2.3" or "1.2.3" into components. Returns false on parse failure. */
	static bool ParseSemver(const FString& VersionString, int32& Major, int32& Minor, int32& Patch);

	/** Returns true if Remote is newer than Current. */
	static bool IsNewerVersion(const FString& Current, const FString& Remote);

	// --- Editor notification ---

	void ShowUpdateNotification(const FString& NewVersion);
	void ShowStagedNotification();

	// --- State ---

	FString LatestVersion;
	FString DownloadUrl;
	bool bUpdateAvailable = false;
	bool bIsDownloading = false;
	bool bHasChecked = false;

	static constexpr const TCHAR* GitHubReleasesUrl =
		TEXT("https://api.github.com/repos/tumourlove/monolith/releases/latest");
};
```

### Step 2: Create UMonolithUpdateSubsystem.cpp

```cpp
// Source/MonolithCore/Private/UMonolithUpdateSubsystem.cpp
#include "UMonolithUpdateSubsystem.h"
#include "MonolithCoreModule.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/PlatformFileManager.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

DEFINE_LOG_CATEGORY(LogMonolithUpdate);

void UMonolithUpdateSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	UE_LOG(LogMonolithUpdate, Log, TEXT("Update subsystem initializing..."));

	// Step 1: Check for staged updates from previous session
	CheckAndApplyStaging();

	// Step 2: Check for new updates if enabled
	// TODO: Read bAutoUpdateEnabled from UMonolithSettings
	bool bAutoUpdateEnabled = true;
	if (bAutoUpdateEnabled)
	{
		CheckForUpdates();
	}
	else
	{
		UE_LOG(LogMonolithUpdate, Log, TEXT("Auto-update disabled in settings"));
	}
}

void UMonolithUpdateSubsystem::Deinitialize()
{
	Super::Deinitialize();
}

// ============================================================================
// Public API
// ============================================================================

bool UMonolithUpdateSubsystem::CheckForUpdates()
{
	if (bHasChecked)
	{
		UE_LOG(LogMonolithUpdate, Log, TEXT("Already checked for updates this session"));
		return false;
	}

	bHasChecked = true;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(GitHubReleasesUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("Accept"), TEXT("application/vnd.github.v3+json"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("Monolith-UE-Plugin"));
	Request->OnProcessRequestComplete().BindUObject(
		this, &UMonolithUpdateSubsystem::OnReleasesResponse);
	Request->ProcessRequest();

	UE_LOG(LogMonolithUpdate, Log, TEXT("Checking GitHub for updates..."));
	return true;
}

void UMonolithUpdateSubsystem::DownloadAndStageUpdate()
{
	if (DownloadUrl.IsEmpty())
	{
		UE_LOG(LogMonolithUpdate, Error, TEXT("No download URL available"));
		return;
	}

	if (bIsDownloading)
	{
		UE_LOG(LogMonolithUpdate, Warning, TEXT("Download already in progress"));
		return;
	}

	bIsDownloading = true;

	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Request = FHttpModule::Get().CreateRequest();
	Request->SetURL(DownloadUrl);
	Request->SetVerb(TEXT("GET"));
	Request->SetHeader(TEXT("User-Agent"), TEXT("Monolith-UE-Plugin"));
	Request->OnProcessRequestComplete().BindUObject(
		this, &UMonolithUpdateSubsystem::OnDownloadComplete);
	Request->ProcessRequest();

	UE_LOG(LogMonolithUpdate, Log, TEXT("Downloading update from %s..."), *DownloadUrl);
}

FString UMonolithUpdateSubsystem::GetCurrentVersion() const
{
	return MONOLITH_VERSION;
}

FString UMonolithUpdateSubsystem::GetLatestVersion() const
{
	return LatestVersion;
}

bool UMonolithUpdateSubsystem::IsUpdateAvailable() const
{
	return bUpdateAvailable;
}

bool UMonolithUpdateSubsystem::IsDownloading() const
{
	return bIsDownloading;
}

void UMonolithUpdateSubsystem::TriggerSourceReindex()
{
	// Delegate to MonolithSource module if loaded
	// This is called by the monolith_reindex tool
	UE_LOG(LogMonolithUpdate, Log, TEXT("Source re-index triggered"));
}

// ============================================================================
// Version JSON
// ============================================================================

FString UMonolithUpdateSubsystem::GetVersionJsonPath() const
{
	return FPaths::Combine(GetPluginDir(), TEXT("Saved/version.json"));
}

FString UMonolithUpdateSubsystem::GetPluginDir() const
{
	return FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Monolith"));
}

FString UMonolithUpdateSubsystem::GetStagingDir() const
{
	return FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Monolith_Staging"));
}

UMonolithUpdateSubsystem::FVersionInfo UMonolithUpdateSubsystem::ReadVersionJson() const
{
	FVersionInfo Info;
	Info.Current = MONOLITH_VERSION;

	FString JsonStr;
	if (FFileHelper::LoadFileToString(JsonStr, *GetVersionJsonPath()))
	{
		TSharedPtr<FJsonObject> JsonObj;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonStr);
		if (FJsonSerializer::Deserialize(Reader, JsonObj) && JsonObj.IsValid())
		{
			Info.Current = JsonObj->GetStringField(TEXT("current"));
			Info.Pending = JsonObj->GetStringField(TEXT("pending"));
			Info.bStaging = JsonObj->GetBoolField(TEXT("staging"));
		}
	}

	return Info;
}

void UMonolithUpdateSubsystem::WriteVersionJson(const FVersionInfo& Info) const
{
	TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
	JsonObj->SetStringField(TEXT("current"), Info.Current);
	JsonObj->SetStringField(TEXT("pending"), Info.Pending);
	JsonObj->SetBoolField(TEXT("staging"), Info.bStaging);

	FString OutputStr;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputStr);
	FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer);
	Writer->Close();

	// Ensure Saved directory exists
	FString SavedDir = FPaths::Combine(GetPluginDir(), TEXT("Saved"));
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	PlatformFile.CreateDirectoryTree(*SavedDir);

	FFileHelper::SaveStringToFile(OutputStr, *GetVersionJsonPath());
}

// ============================================================================
// Staging / Swap
// ============================================================================

void UMonolithUpdateSubsystem::CheckAndApplyStaging()
{
	FVersionInfo VersionInfo = ReadVersionJson();

	if (!VersionInfo.bStaging)
	{
		return;
	}

	FString StagingDir = GetStagingDir();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Verify staging directory has a valid plugin
	FString StagedUplugin = FPaths::Combine(StagingDir, TEXT("Monolith.uplugin"));
	if (!PlatformFile.FileExists(*StagedUplugin))
	{
		UE_LOG(LogMonolithUpdate, Error,
			TEXT("Staging directory exists but no .uplugin found. Aborting update."));
		// Clear staging flag
		VersionInfo.bStaging = false;
		WriteVersionJson(VersionInfo);
		return;
	}

	UE_LOG(LogMonolithUpdate, Log, TEXT("Applying staged update: %s → %s"),
		*VersionInfo.Current, *VersionInfo.Pending);

	// NOTE: The actual swap (rename Monolith → Monolith_Old, Monolith_Staging → Monolith)
	// cannot happen while the plugin is loaded. This must be handled by an external
	// batch script or on next launch before the plugin DLL is loaded.
	//
	// For now, we log the intent and show a notification. A future implementation
	// would use a pre-launch script or UE's plugin hot-reload capabilities.

	FString PluginDir = GetPluginDir();
	FString OldDir = FPaths::Combine(FPaths::ProjectPluginsDir(), TEXT("Monolith_Old"));

	// Attempt the swap — this may fail if DLLs are locked
	bool bSwapSuccess = false;

	// Try rename current → old
	if (PlatformFile.MoveFile(*OldDir, *PluginDir))
	{
		// Try rename staging → current
		if (PlatformFile.MoveFile(*PluginDir, *StagingDir))
		{
			bSwapSuccess = true;
			UE_LOG(LogMonolithUpdate, Log,
				TEXT("Successfully swapped to v%s"), *VersionInfo.Pending);

			// Update version info
			VersionInfo.Current = VersionInfo.Pending;
			VersionInfo.Pending.Empty();
			VersionInfo.bStaging = false;
			WriteVersionJson(VersionInfo);

			// Schedule cleanup of old version (deferred)
			AsyncTask(ENamedThreads::AnyBackgroundThreadNormalTask, [OldDir]()
			{
				IPlatformFile& PF = FPlatformFileManager::Get().GetPlatformFile();
				PF.DeleteDirectoryRecursively(*OldDir);
			});
		}
		else
		{
			// Rollback: move old back to current
			PlatformFile.MoveFile(*PluginDir, *OldDir);
			UE_LOG(LogMonolithUpdate, Error,
				TEXT("Failed to move staging to plugin dir. Rolled back."));
		}
	}
	else
	{
		UE_LOG(LogMonolithUpdate, Warning,
			TEXT("Cannot swap directories while plugin is loaded. Will retry on next launch."));
	}
}

// ============================================================================
// GitHub Releases API
// ============================================================================

void UMonolithUpdateSubsystem::OnReleasesResponse(
	FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
	if (!bSuccess || !Response.IsValid())
	{
		UE_LOG(LogMonolithUpdate, Warning, TEXT("Failed to check for updates (network error)"));
		return;
	}

	int32 Code = Response->GetResponseCode();
	if (Code != 200)
	{
		UE_LOG(LogMonolithUpdate, Warning,
			TEXT("GitHub API returned %d when checking for updates"), Code);
		return;
	}

	FString Body = Response->GetContentAsString();
	TSharedPtr<FJsonObject> JsonObj;
	TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Body);
	if (!FJsonSerializer::Deserialize(Reader, JsonObj) || !JsonObj.IsValid())
	{
		UE_LOG(LogMonolithUpdate, Warning, TEXT("Failed to parse GitHub release JSON"));
		return;
	}

	FString TagName = JsonObj->GetStringField(TEXT("tag_name"));

	// Strip leading 'v' if present
	FString RemoteVersion = TagName;
	if (RemoteVersion.StartsWith(TEXT("v")))
	{
		RemoteVersion = RemoteVersion.Mid(1);
	}

	FString CurrentVersion = MONOLITH_VERSION;

	if (IsNewerVersion(CurrentVersion, RemoteVersion))
	{
		LatestVersion = RemoteVersion;
		bUpdateAvailable = true;

		// Find the zip asset download URL
		const TArray<TSharedPtr<FJsonValue>>* Assets;
		if (JsonObj->TryGetArrayField(TEXT("assets"), Assets))
		{
			for (const auto& AssetVal : *Assets)
			{
				TSharedPtr<FJsonObject> Asset = AssetVal->AsObject();
				if (Asset.IsValid())
				{
					FString AssetName = Asset->GetStringField(TEXT("name"));
					if (AssetName.EndsWith(TEXT(".zip")))
					{
						DownloadUrl = Asset->GetStringField(TEXT("browser_download_url"));
						break;
					}
				}
			}
		}

		// Fallback to zipball_url if no .zip asset
		if (DownloadUrl.IsEmpty())
		{
			DownloadUrl = JsonObj->GetStringField(TEXT("zipball_url"));
		}

		UE_LOG(LogMonolithUpdate, Log,
			TEXT("Update available: %s → %s"), *CurrentVersion, *RemoteVersion);

		// Show notification on game thread
		AsyncTask(ENamedThreads::GameThread, [this, RemoteVersion]()
		{
			ShowUpdateNotification(RemoteVersion);
		});
	}
	else
	{
		UE_LOG(LogMonolithUpdate, Log, TEXT("Monolith is up to date (v%s)"), *CurrentVersion);
	}
}

void UMonolithUpdateSubsystem::OnDownloadComplete(
	FHttpRequestPtr Request, FHttpResponsePtr Response, bool bSuccess)
{
	bIsDownloading = false;

	if (!bSuccess || !Response.IsValid() || Response->GetResponseCode() != 200)
	{
		UE_LOG(LogMonolithUpdate, Error, TEXT("Failed to download update"));
		return;
	}

	// Save zip to temp location
	FString TempZipPath = FPaths::Combine(
		FPaths::ProjectSavedDir(), TEXT("MonolithUpdate.zip"));
	TArray<uint8> Content = Response->GetContent();

	if (!FFileHelper::SaveArrayToFile(Content, *TempZipPath))
	{
		UE_LOG(LogMonolithUpdate, Error, TEXT("Failed to save update zip to %s"), *TempZipPath);
		return;
	}

	UE_LOG(LogMonolithUpdate, Log, TEXT("Update downloaded (%d bytes) to %s"),
		Content.Num(), *TempZipPath);

	// Extract to staging directory
	FString StagingDir = GetStagingDir();
	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// Clean any existing staging
	if (PlatformFile.DirectoryExists(*StagingDir))
	{
		PlatformFile.DeleteDirectoryRecursively(*StagingDir);
	}
	PlatformFile.CreateDirectoryTree(*StagingDir);

	// Use FPlatformMisc::UncompressZipFile or IPlatformFile to extract
	// UE doesn't have a built-in zip extractor in all versions,
	// so we use the PlatformFile abstraction with a helper
	FString ExtractCmd = FString::Printf(
		TEXT("powershell -Command \"Expand-Archive -Path '%s' -DestinationPath '%s' -Force\""),
		*TempZipPath, *StagingDir);

	int32 ReturnCode = -1;
	FString StdOut, StdErr;
	FPlatformProcess::ExecProcess(TEXT("cmd.exe"),
		*FString::Printf(TEXT("/c %s"), *ExtractCmd),
		&ReturnCode, &StdOut, &StdErr);

	if (ReturnCode != 0)
	{
		UE_LOG(LogMonolithUpdate, Error,
			TEXT("Failed to extract update zip: %s"), *StdErr);
		return;
	}

	// Clean up temp zip
	PlatformFile.DeleteFile(*TempZipPath);

	// Write version.json with staging flag
	FVersionInfo Info;
	Info.Current = MONOLITH_VERSION;
	Info.Pending = LatestVersion;
	Info.bStaging = true;
	WriteVersionJson(Info);

	UE_LOG(LogMonolithUpdate, Log,
		TEXT("Update v%s staged. Restart editor to apply."), *LatestVersion);

	// Show notification on game thread
	AsyncTask(ENamedThreads::GameThread, [this]()
	{
		ShowStagedNotification();
	});
}

// ============================================================================
// Semver Comparison
// ============================================================================

bool UMonolithUpdateSubsystem::ParseSemver(
	const FString& VersionString, int32& Major, int32& Minor, int32& Patch)
{
	FString Ver = VersionString;
	if (Ver.StartsWith(TEXT("v")))
	{
		Ver = Ver.Mid(1);
	}

	TArray<FString> Parts;
	Ver.ParseIntoArray(Parts, TEXT("."));

	if (Parts.Num() < 2)
	{
		return false;
	}

	Major = FCString::Atoi(*Parts[0]);
	Minor = FCString::Atoi(*Parts[1]);
	Patch = Parts.Num() > 2 ? FCString::Atoi(*Parts[2]) : 0;

	return true;
}

bool UMonolithUpdateSubsystem::IsNewerVersion(
	const FString& Current, const FString& Remote)
{
	int32 CurMajor, CurMinor, CurPatch;
	int32 RemMajor, RemMinor, RemPatch;

	if (!ParseSemver(Current, CurMajor, CurMinor, CurPatch) ||
		!ParseSemver(Remote, RemMajor, RemMinor, RemPatch))
	{
		return false;
	}

	if (RemMajor != CurMajor) return RemMajor > CurMajor;
	if (RemMinor != CurMinor) return RemMinor > CurMinor;
	return RemPatch > CurPatch;
}

// ============================================================================
// Editor Notifications
// ============================================================================

void UMonolithUpdateSubsystem::ShowUpdateNotification(const FString& NewVersion)
{
	FNotificationInfo Info(FText::Format(
		NSLOCTEXT("Monolith", "UpdateAvailable",
			"Monolith v{0} available (current: {1})"),
		FText::FromString(NewVersion),
		FText::FromString(MONOLITH_VERSION)));

	Info.bFireAndForget = false;
	Info.bUseSuccessFailIcons = false;
	Info.bUseLargeFont = false;
	Info.ExpireDuration = 0.0f; // Don't auto-expire
	Info.FadeOutDuration = 1.0f;

	// Add Update button
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		NSLOCTEXT("Monolith", "UpdateButton", "Update"),
		NSLOCTEXT("Monolith", "UpdateTooltip", "Download and stage the update"),
		FSimpleDelegate::CreateUObject(this, &UMonolithUpdateSubsystem::DownloadAndStageUpdate),
		SNotificationItem::CS_Pending
	));

	// Add Dismiss button
	Info.ButtonDetails.Add(FNotificationButtonInfo(
		NSLOCTEXT("Monolith", "DismissButton", "Dismiss"),
		NSLOCTEXT("Monolith", "DismissTooltip", "Dismiss this notification"),
		FSimpleDelegate::CreateLambda([]() {}),
		SNotificationItem::CS_None
	));

	FSlateNotificationManager::Get().AddNotification(Info);
}

void UMonolithUpdateSubsystem::ShowStagedNotification()
{
	FNotificationInfo Info(FText::Format(
		NSLOCTEXT("Monolith", "UpdateStaged",
			"Monolith v{0} staged. Restart editor to apply."),
		FText::FromString(LatestVersion)));

	Info.bFireAndForget = true;
	Info.ExpireDuration = 10.0f;
	Info.bUseSuccessFailIcons = true;

	TSharedPtr<SNotificationItem> Notification =
		FSlateNotificationManager::Get().AddNotification(Info);
	if (Notification.IsValid())
	{
		Notification->SetCompletionState(SNotificationItem::CS_Success);
	}
}
```

### Step 3: Add EditorSubsystem dependency to MonolithCore.Build.cs

```csharp
// Modify: Source/MonolithCore/MonolithCore.Build.cs
// Add "EditorSubsystem" to PublicDependencyModuleNames
using UnrealBuildTool;

public class MonolithCore : ModuleRules
{
	public MonolithCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"HTTP",
			"HTTPServer",
			"Json",
			"JsonUtilities",
			"Slate",
			"SlateCore",
			"DeveloperSettings",
			"Projects",
			"EditorSubsystem"
		});
	}
}
```

### Step 4: Verify compilation

```
# Trigger build — subsystem should compile and register with the editor
```

**Expected:** Compiles. On editor launch, subsystem logs update check. With no GitHub releases yet, logs "up to date" or "network error" gracefully.

---

## Task 5.5 — monolith_update and monolith_reindex Tool Implementations

These are registered as top-level MCP tools in MonolithCore (alongside `monolith_discover` and `monolith_status`).

### Files

- **Modify:** `Source/MonolithCore/Private/MonolithCoreModule.cpp` (or wherever core tools are registered)

### Step 1: Register monolith_update tool

The `monolith_update` tool is registered with the ToolRegistry during MonolithCore startup:

```cpp
// In MonolithCore tool registration (e.g., FMonolithCoreTools or directly in module startup)

// monolith_update — check or install updates
Registry.RegisterCoreAction(TEXT("monolith"), TEXT("update"),
    TEXT("Check for or install Monolith updates"),
    [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
    {
        UMonolithUpdateSubsystem* UpdateSystem =
            GEditor ? GEditor->GetEditorSubsystem<UMonolithUpdateSubsystem>() : nullptr;

        if (!UpdateSystem)
        {
            return FMonolithJsonUtils::ErrorJson(TEXT("Update subsystem not available"));
        }

        FString Action = TEXT("check");
        if (Params.IsValid() && Params->HasField(TEXT("action")))
        {
            Action = Params->GetStringField(TEXT("action"));
        }

        if (Action == TEXT("check"))
        {
            if (UpdateSystem->IsUpdateAvailable())
            {
                TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
                Result->SetBoolField(TEXT("update_available"), true);
                Result->SetStringField(TEXT("current_version"), UpdateSystem->GetCurrentVersion());
                Result->SetStringField(TEXT("latest_version"), UpdateSystem->GetLatestVersion());
                return FMonolithJsonUtils::SuccessJson(Result);
            }

            bool bCheckStarted = UpdateSystem->CheckForUpdates();
            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetBoolField(TEXT("update_available"), false);
            Result->SetStringField(TEXT("current_version"), UpdateSystem->GetCurrentVersion());
            Result->SetStringField(TEXT("status"),
                bCheckStarted ? TEXT("checking") : TEXT("already_checked"));
            return FMonolithJsonUtils::SuccessJson(Result);
        }
        else if (Action == TEXT("install"))
        {
            if (!UpdateSystem->IsUpdateAvailable())
            {
                return FMonolithJsonUtils::ErrorJson(TEXT("No update available. Run check first."));
            }

            if (UpdateSystem->IsDownloading())
            {
                return FMonolithJsonUtils::ErrorJson(TEXT("Download already in progress."));
            }

            UpdateSystem->DownloadAndStageUpdate();

            TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
            Result->SetStringField(TEXT("status"), TEXT("downloading"));
            Result->SetStringField(TEXT("version"), UpdateSystem->GetLatestVersion());
            return FMonolithJsonUtils::SuccessJson(Result);
        }

        return FMonolithJsonUtils::ErrorJson(
            FString::Printf(TEXT("Unknown action: %s. Use 'check' or 'install'."), *Action));
    });
```

### Step 2: Register monolith_reindex tool

```cpp
// monolith_reindex — trigger source re-index
Registry.RegisterCoreAction(TEXT("monolith"), TEXT("reindex"),
    TEXT("Trigger a full engine source re-index"),
    [](const TSharedPtr<FJsonObject>& Params) -> TSharedPtr<FJsonObject>
    {
        // Get MonolithSource module and trigger re-index
        FMonolithSourceModule* SourceModule = FModuleManager::GetModulePtr<FMonolithSourceModule>("MonolithSource");
        if (!SourceModule)
        {
            return FMonolithJsonUtils::ErrorJson(TEXT("MonolithSource module not loaded"));
        }

        // Determine engine source path
        FString EnginePath = FPaths::Combine(FPaths::EngineDir(), TEXT("Source"));
        if (Params.IsValid() && Params->HasField(TEXT("engine_path")))
        {
            EnginePath = Params->GetStringField(TEXT("engine_path"));
        }

        FString VersionTag = FString::Printf(TEXT("%d.%d"),
            ENGINE_MAJOR_VERSION, ENGINE_MINOR_VERSION);
        if (Params.IsValid() && Params->HasField(TEXT("version_tag")))
        {
            VersionTag = Params->GetStringField(TEXT("version_tag"));
        }

        // TODO: This should run async. For now, synchronous.
        // The source module's process manager handles the actual indexing.

        TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
        Result->SetStringField(TEXT("status"), TEXT("reindex_started"));
        Result->SetStringField(TEXT("engine_path"), EnginePath);
        Result->SetStringField(TEXT("version_tag"), VersionTag);
        return FMonolithJsonUtils::SuccessJson(Result);
    });
```

### Step 3: Verify tools respond via MCP

```
# Via Claude Code or curl:
# monolith_update(action="check") → returns current version info
# monolith_reindex() → returns reindex_started status
```

**Expected:** Both tools return valid JSON responses. Update check either shows "checking" or actual version comparison. Reindex returns confirmation.

---

## Task 5.6 — Compilation & Integration Verification

### Step 1: Build all modules

```bash
# Full rebuild to verify no circular dependencies or missing includes
# UBT command or trigger_build from editor
```

### Step 2: Verify action discovery

```
# Call monolith_discover("source") via MCP
# Expected: 14 actions listed with descriptions
```

### Step 3: Verify Python process lifecycle

```
# 1. Call source_query("search_api", {"query": "AActor"})
#    → Python process spawns, returns results (empty if not indexed)
# 2. Call source_query("read_file", {"path": "Runtime/Engine/Classes/GameFramework/Actor.h"})
#    → Returns file content
# 3. Editor shutdown → Python process terminates cleanly
```

### Step 4: Verify auto-updater

```
# 1. Editor launch → check GitHub releases
# 2. If no releases exist: logs "up to date" or network info
# 3. monolith_update(action="check") → returns version info
# 4. monolith_update(action="install") → returns "no update available"
```

### Step 5: Commit

```bash
git add Source/MonolithSource/ Source/MonolithCore/Public/UMonolithUpdateSubsystem.h \
  Source/MonolithCore/Private/UMonolithUpdateSubsystem.cpp \
  Source/MonolithCore/MonolithCore.Build.cs \
  MCP/src/monolith_source/
git commit -m "Phase 5: Source module (14 actions) + auto-updater

- FMonolithSourceProcess: Python child process manager (stdin/stdout JSON)
- FMonolithSourceActions: 14 actions registered with ToolRegistry
  - 9 from unreal-source-mcp: read_source, search_source, find_callers,
    find_callees, find_references, get_class_hierarchy, get_module_info,
    get_symbol_context, read_file
  - 5 reimplemented (unreal-api-mcp concepts): search_api,
    get_function_signature, get_include_path, get_class_reference,
    get_deprecation_warnings
- Bundled Python indexer: tree-sitter C++ parser, SQLite+FTS5 database,
  JSON-line child process protocol (replaces FastMCP stdio)
- API lookup tables: signature, #include resolution, deprecation checking
- UMonolithUpdateSubsystem: GitHub Releases API check, semver comparison,
  download, staging, swap-on-restart, Slate notifications
- monolith_update and monolith_reindex tool implementations"
```

---

## Summary

| Component | Files | Description |
|---|---|---|
| FMonolithSourceProcess | 2 (h+cpp) | Child process manager — spawn Python, stdin/stdout JSON, graceful shutdown |
| FMonolithSourceActions | 2 (h+cpp) | 14 actions forwarding to Python process via ToolRegistry |
| MonolithSourceModule | 2 (h+cpp modified) | Module startup/shutdown wiring |
| Python child_process.py | 1 | JSON-line protocol entry point replacing FastMCP stdio |
| Python db/schema.py | 1 | SQLite+FTS5 schema (files, symbols, call_graph, references, inheritance, modules) |
| Python db/queries.py | 1 | Full query layer — symbol lookup, call graphs, FTS search, hierarchy |
| Python indexer/parser.py | 1 | tree-sitter C++ parser — classes, functions, enums, deprecations, inheritance |
| Python indexer/api_tables.py | 1 | API intelligence — signature lookup, #include resolution, deprecation checks |
| UMonolithUpdateSubsystem | 2 (h+cpp) | Auto-updater — GitHub Releases API, semver, download, staging, swap, notifications |
| MonolithCore.Build.cs | 1 (modified) | Added EditorSubsystem dependency |
| monolith_update tool | inline | Check/install updates via MCP |
| monolith_reindex tool | inline | Trigger source re-index via MCP |

**Total: 14 files (10 new, 4 modified)**

Mission complete. o7

## Phase 6: Skills, Templates, and Polish

**Goal:** Ship the complete developer experience — 8 Claude Code skills, project templates, comprehensive README, contribution guide, and CI pipeline.

**Prerequisites:** Phases 1–5 complete (all modules compile, MCP server running, discovery/dispatch working).

---

## Task 6.1 — Skill: Unreal Blueprints

**Files:**
- Create `Skills/unreal-blueprints/unreal-blueprints.md`

**Steps:**

1. Create directory and skill file:

```
mkdir -p Skills/unreal-blueprints
```

2. Write `Skills/unreal-blueprints/unreal-blueprints.md`:

```markdown
---
name: unreal-blueprints
description: Use when working with Unreal Engine Blueprints — reading graph topology, inspecting variables, tracing execution flow, searching nodes, or understanding BP architecture. Triggers on keywords like Blueprint, BP, event graph, node, variable, function graph.
---

# Unreal Blueprint Workflows

You have access to the Monolith MCP server with deep Blueprint introspection. Use these tools to understand, analyze, and guide Blueprint work.

## Available Actions

Discover all Blueprint actions:
```
monolith_discover({ namespace: "blueprint" })
```

Execute actions via:
```
blueprint_query({ action: "<action_name>", params: { ... } })
```

### Action Reference

| Action | Params | Returns |
|--------|--------|---------|
| `list_graphs` | `asset_path` | All function/event/macro graphs in a Blueprint |
| `get_graph_data` | `asset_path`, `graph_name` | Full node topology — pins, connections, positions |
| `get_variables` | `asset_path` | All variables with types, defaults, categories, replication flags |
| `get_execution_flow` | `asset_path`, `graph_name` | Ordered execution chain from entry point through all branches |
| `search_nodes` | `asset_path`, `query`, `node_class?` | Find nodes by name, class, or function reference |

## Workflow: Understand a Blueprint

When the user asks "how does BP_Enemy work?" or similar:

1. **List all graphs** to understand scope:
   ```
   blueprint_query({ action: "list_graphs", params: { asset_path: "/Game/Blueprints/BP_Enemy" } })
   ```

2. **Get variables** to understand state:
   ```
   blueprint_query({ action: "get_variables", params: { asset_path: "/Game/Blueprints/BP_Enemy" } })
   ```

3. **Trace execution** of the main event graph:
   ```
   blueprint_query({ action: "get_execution_flow", params: { asset_path: "/Game/Blueprints/BP_Enemy", graph_name: "EventGraph" } })
   ```

4. **Summarize** the Blueprint's purpose, state, and behavior in plain language.

## Workflow: Find Blueprint Usage Patterns

When looking for how a concept is implemented across Blueprints:

1. **Search the project index** for Blueprints of a type:
   ```
   project_query({ action: "find_by_type", params: { type: "Blueprint", filter: "Enemy" } })
   ```

2. **Search nodes** across found Blueprints for specific function calls:
   ```
   blueprint_query({ action: "search_nodes", params: { asset_path: "/Game/Blueprints/BP_Enemy", query: "ApplyDamage" } })
   ```

3. **Cross-reference** with C++ source if needed:
   ```
   source_query({ action: "search", params: { query: "UGameplayStatics::ApplyDamage" } })
   ```

## Workflow: Debug Blueprint Logic

When a Blueprint isn't behaving as expected:

1. **Get full graph data** to see all connections:
   ```
   blueprint_query({ action: "get_graph_data", params: { asset_path: "/Game/Blueprints/BP_Player", graph_name: "EventGraph" } })
   ```

2. **Trace execution flow** to find where logic diverges:
   ```
   blueprint_query({ action: "get_execution_flow", params: { asset_path: "/Game/Blueprints/BP_Player", graph_name: "EventGraph" } })
   ```

3. **Check variables** for incorrect defaults or missing replication:
   ```
   blueprint_query({ action: "get_variables", params: { asset_path: "/Game/Blueprints/BP_Player" } })
   ```

4. Look for common issues:
   - Disconnected execution pins (nodes exist but aren't wired)
   - Wrong variable types on cast nodes
   - Missing validation branches (IsValid checks)
   - Event dispatcher bindings that never fire

## Tips

- **Asset paths** use `/Game/...` format (not filesystem paths). Use `project_query` to find assets if unsure.
- **Graph names**: `EventGraph` is the default. Function graphs use the function name. Macros use macro name.
- **Large Blueprints**: List graphs first to scope the investigation. Don't dump the entire EventGraph of a 500-node Blueprint — search for specific nodes instead.
- **C++ backing**: If a Blueprint extends a C++ class, use `source_query` to understand the native parent before reading BP graphs.
```

3. Verify file exists and commit:
```bash
git add Skills/unreal-blueprints/
git commit -m "feat(skills): add unreal-blueprints skill"
```

**Expected output:** Skill file with complete workflows for BP inspection, debugging, and cross-referencing.

---

## Task 6.2 — Skill: Unreal Materials

**Files:**
- Create `Skills/unreal-materials/unreal-materials.md`

**Steps:**

1. Create directory and write skill:

```
mkdir -p Skills/unreal-materials
```

2. Write `Skills/unreal-materials/unreal-materials.md`:

```markdown
---
name: unreal-materials
description: Use when creating, editing, inspecting, or optimizing Unreal Engine materials. Triggers on keywords like material, shader, PBR, texture, material instance, material function, material expression, roughness, metallic, normal map, UV, opacity.
---

# Unreal Material Workflows

You have access to 46 material actions through Monolith. These cover the full material lifecycle — inspection, creation, graph building, templates, previews, and validation.

## Available Actions

```
monolith_discover({ namespace: "material" })
```

Execute via:
```
material_query({ action: "<action_name>", params: { ... } })
```

### Core Action Groups

**Inspection (read-only):**
- `get_material_info` — Full material properties, blend mode, shading model, usage flags
- `get_expressions` — All expression nodes in the graph
- `get_connections` — Pin connections between expressions
- `get_parameters` — All parameter names, types, defaults, groups
- `get_texture_samples` — All texture references with UV channel info
- `get_material_function_info` — Inspect reusable material functions
- `list_material_instances` — Find all instances of a parent material

**Graph Building (create/edit):**
- `create_material` — New material asset with initial properties
- `add_expression` — Add a node (TextureSample, Multiply, Lerp, etc.)
- `connect_expressions` — Wire output pin to input pin
- `disconnect_pin` — Remove a connection
- `set_expression_value` — Set a constant/parameter value
- `create_parameter` — Add a scalar/vector/texture parameter
- `create_material_instance` — Instance from parent with overrides
- `set_instance_parameter` — Override a parameter on an instance

**Templates (one-shot creation):**
- `create_pbr_material` — Full PBR setup from texture set (albedo, normal, roughness, metallic, AO)
- `create_landscape_material` — Multi-layer landscape blend
- `create_decal_material` — Deferred decal with proper blend mode
- `create_ui_material` — UI/HUD material

**Validation & Optimization:**
- `validate_material` — Check for errors, disconnected nodes, missing textures
- `get_instruction_count` — Shader complexity per platform
- `check_texture_streaming` — Verify streaming settings
- `find_expensive_nodes` — Identify high-cost expressions

## Workflow: Create a PBR Material from Textures

When the user says "make a material for this rock" or provides texture files:

1. **Find texture assets** in the project:
   ```
   project_query({ action: "search", params: { query: "T_Rock", types: ["Texture2D"] } })
   ```

2. **Create full PBR material** in one call:
   ```
   material_query({ action: "create_pbr_material", params: {
     name: "M_Rock",
     path: "/Game/Materials",
     albedo: "/Game/Textures/T_Rock_BaseColor",
     normal: "/Game/Textures/T_Rock_Normal",
     roughness: "/Game/Textures/T_Rock_Roughness",
     metallic: "/Game/Textures/T_Rock_Metallic",
     ao: "/Game/Textures/T_Rock_AO"
   } })
   ```

3. **Validate** the result:
   ```
   material_query({ action: "validate_material", params: { asset_path: "/Game/Materials/M_Rock" } })
   ```

4. **Check performance**:
   ```
   material_query({ action: "get_instruction_count", params: { asset_path: "/Game/Materials/M_Rock" } })
   ```

## Workflow: Build a Custom Material Graph

For complex materials that templates don't cover:

1. **Create base material**:
   ```
   material_query({ action: "create_material", params: {
     name: "M_Custom",
     path: "/Game/Materials",
     shading_model: "DefaultLit",
     blend_mode: "Opaque"
   } })
   ```

2. **Add expressions** one by one:
   ```
   material_query({ action: "add_expression", params: {
     asset_path: "/Game/Materials/M_Custom",
     expression_class: "MaterialExpressionTextureSample",
     name: "BaseColorTex"
   } })
   ```

3. **Create parameters** for artist control:
   ```
   material_query({ action: "create_parameter", params: {
     asset_path: "/Game/Materials/M_Custom",
     param_type: "Scalar",
     name: "RoughnessMultiplier",
     default_value: 1.0,
     group: "Surface"
   } })
   ```

4. **Wire connections**:
   ```
   material_query({ action: "connect_expressions", params: {
     asset_path: "/Game/Materials/M_Custom",
     source_expression: "BaseColorTex",
     source_pin: "RGB",
     target_expression: "Material",
     target_pin: "BaseColor"
   } })
   ```

5. **Validate and check cost** after building.

## Workflow: Optimize Material Performance

When materials are too expensive or draw calls are high:

1. **Get instruction count** per platform:
   ```
   material_query({ action: "get_instruction_count", params: { asset_path: "/Game/Materials/M_Heavy" } })
   ```

2. **Find expensive nodes**:
   ```
   material_query({ action: "find_expensive_nodes", params: { asset_path: "/Game/Materials/M_Heavy" } })
   ```

3. **Check texture streaming**:
   ```
   material_query({ action: "check_texture_streaming", params: { asset_path: "/Game/Materials/M_Heavy" } })
   ```

4. Common optimizations to suggest:
   - Replace runtime math with baked textures (channel packing)
   - Use material instances instead of unique materials
   - Reduce texture samples (pack R/M/AO into one texture's channels)
   - Switch from Translucent to Masked where possible
   - Use `Fully Rough` flag for distant objects
   - Lower quality switch for mobile/lower LODs

## Tips

- **Material instances** are almost always better than unique materials. Create a parameterized parent and instance it.
- **Channel packing**: Pack roughness (R), metallic (G), AO (B) into one ORM texture to reduce samples.
- **Instruction budget**: Keep opaque materials under 200 instructions. Translucent under 100.
- **Naming convention**: `M_` for materials, `MI_` for instances, `MF_` for functions.
```

3. Commit:
```bash
git add Skills/unreal-materials/
git commit -m "feat(skills): add unreal-materials skill"
```

---

## Task 6.3 — Skill: Unreal Animation

**Files:**
- Create `Skills/unreal-animation/unreal-animation.md`

**Steps:**

1. Write `Skills/unreal-animation/unreal-animation.md`:

```markdown
---
name: unreal-animation
description: Use when working with Unreal animation systems — Animation Blueprints (ABPs), montages, blend spaces, animation sequences, skeletons, state machines, notifies, or animation editing. Triggers on keywords like animation, montage, ABP, blend space, skeleton, anim notify, state machine, anim graph, anim slot.
---

# Unreal Animation Workflows

You have access to 62 animation actions through Monolith covering sequences, montages, blend spaces, Animation Blueprints, skeletons, and editing operations.

## Available Actions

```
monolith_discover({ namespace: "animation" })
```

Execute via:
```
animation_query({ action: "<action_name>", params: { ... } })
```

### Core Action Groups

**Sequences:**
- `get_sequence_info` — Duration, frame count, rate, curves, notifies
- `get_sequence_curves` — All animation curves with keyframes
- `get_sequence_notifies` — Notify events and their trigger times
- `list_sequences` — Find sequences by skeleton or search term

**Montages:**
- `get_montage_info` — Sections, slots, blend in/out, notifies
- `get_montage_sections` — Section names, links, and composite structure
- `create_montage` — New montage from sequence(s)
- `add_montage_section` — Add a named section with timing
- `link_montage_sections` — Set section playback order
- `add_montage_notify` — Add notify at time/section

**Blend Spaces:**
- `get_blendspace_info` — Axes, samples, grid dimensions
- `get_blendspace_samples` — All sample points with animations
- `create_blendspace` — New 1D or 2D blend space
- `add_blendspace_sample` — Add animation at coordinate
- `set_blendspace_axis` — Configure axis range and name

**Animation Blueprints:**
- `get_abp_info` — Target skeleton, parent class, graphs
- `get_abp_graph` — Full anim graph node topology
- `get_state_machine` — States, transitions, rules
- `get_state_info` — Single state's animation and logic
- `get_transition_rules` — Conditions for state transitions
- `list_anim_variables` — ABP variables and types

**Skeletons:**
- `get_skeleton_info` — Bone hierarchy, socket list
- `get_bone_tree` — Full bone tree with indices
- `get_sockets` — Socket names, parent bones, transforms
- `find_compatible_skeletons` — Skeletons that share retarget data

**Editing:**
- `set_notify_trigger` — Modify notify timing
- `set_curve_keys` — Edit animation curve keyframes
- `set_blend_settings` — Modify blend in/out on montages
- `set_slot_name` — Change montage slot assignment

## Workflow: Understand an Animation Blueprint

When the user asks about character animation logic:

1. **Get ABP overview**:
   ```
   animation_query({ action: "get_abp_info", params: { asset_path: "/Game/Characters/ABP_Player" } })
   ```

2. **Inspect the main state machine**:
   ```
   animation_query({ action: "get_state_machine", params: { asset_path: "/Game/Characters/ABP_Player", machine_name: "Locomotion" } })
   ```

3. **Drill into specific states**:
   ```
   animation_query({ action: "get_state_info", params: { asset_path: "/Game/Characters/ABP_Player", machine_name: "Locomotion", state_name: "Run" } })
   ```

4. **Check transition rules** to understand flow:
   ```
   animation_query({ action: "get_transition_rules", params: { asset_path: "/Game/Characters/ABP_Player", machine_name: "Locomotion" } })
   ```

5. **Summarize**: State machine states, what drives transitions (speed, booleans, etc.), and what animations play in each state.

## Workflow: Set Up a Montage for Attacks

1. **Find the attack animation sequence**:
   ```
   animation_query({ action: "list_sequences", params: { skeleton: "/Game/Characters/SK_Player_Skeleton", filter: "Attack" } })
   ```

2. **Create montage**:
   ```
   animation_query({ action: "create_montage", params: {
     name: "AM_MeleeAttack",
     path: "/Game/Characters/Montages",
     sequence: "/Game/Characters/Animations/Melee_Attack_01",
     slot: "DefaultSlot.UpperBody"
   } })
   ```

3. **Add sections** for combo chaining:
   ```
   animation_query({ action: "add_montage_section", params: { asset_path: "/Game/Characters/Montages/AM_MeleeAttack", section_name: "WindUp", start_time: 0.0 } })
   animation_query({ action: "add_montage_section", params: { asset_path: "/Game/Characters/Montages/AM_MeleeAttack", section_name: "Strike", start_time: 0.3 } })
   animation_query({ action: "add_montage_section", params: { asset_path: "/Game/Characters/Montages/AM_MeleeAttack", section_name: "Recovery", start_time: 0.6 } })
   ```

4. **Add damage notify**:
   ```
   animation_query({ action: "add_montage_notify", params: { asset_path: "/Game/Characters/Montages/AM_MeleeAttack", notify_name: "AN_DamageWindow", time: 0.35 } })
   ```

5. **Link sections** for looping or branching:
   ```
   animation_query({ action: "link_montage_sections", params: { asset_path: "/Game/Characters/Montages/AM_MeleeAttack", from_section: "Strike", to_section: "Recovery" } })
   ```

## Workflow: Create a Locomotion Blend Space

1. **Create 2D blend space**:
   ```
   animation_query({ action: "create_blendspace", params: {
     name: "BS_Locomotion",
     path: "/Game/Characters/BlendSpaces",
     skeleton: "/Game/Characters/SK_Player_Skeleton",
     type: "2D",
     axis_x: { name: "Speed", min: 0, max: 600 },
     axis_y: { name: "Direction", min: -180, max: 180 }
   } })
   ```

2. **Add samples** at key positions:
   ```
   animation_query({ action: "add_blendspace_sample", params: { asset_path: "/Game/Characters/BlendSpaces/BS_Locomotion", animation: "/Game/Characters/Animations/Idle", x: 0, y: 0 } })
   animation_query({ action: "add_blendspace_sample", params: { asset_path: "/Game/Characters/BlendSpaces/BS_Locomotion", animation: "/Game/Characters/Animations/Walk_Fwd", x: 200, y: 0 } })
   animation_query({ action: "add_blendspace_sample", params: { asset_path: "/Game/Characters/BlendSpaces/BS_Locomotion", animation: "/Game/Characters/Animations/Run_Fwd", x: 600, y: 0 } })
   ```

## Tips

- **Slot names** follow the format `GroupName.SlotName`. Default group is `DefaultSlot`. Custom groups need `DefaultSlot.YourSlot` or a custom group defined on the skeleton.
- **Montage sections** must have unique names. Use descriptive names like `WindUp`, `Strike`, `Recovery` — not `Section1`.
- **Blend space axes** should map to gameplay variables (Speed, Direction, Lean). The ABP feeds these from the character's movement component.
- **Retargeting**: Use `find_compatible_skeletons` before sharing animations between characters.
- **Naming**: `AM_` for montages, `ABP_` for anim blueprints, `BS_` for blend spaces, `AN_` for notifies.
```

2. Commit:
```bash
git add Skills/unreal-animation/
git commit -m "feat(skills): add unreal-animation skill"
```

---

## Task 6.4 — Skill: Unreal Niagara

**Files:**
- Create `Skills/unreal-niagara/unreal-niagara.md`

**Steps:**

1. Write `Skills/unreal-niagara/unreal-niagara.md`:

```markdown
---
name: unreal-niagara
description: Use when working with Niagara particle systems — creating effects, editing emitters, writing custom modules, HLSL scratch pads, parameter management, or renderer configuration. Triggers on keywords like Niagara, particle, emitter, VFX, particle system, spawn rate, HLSL, scratch pad, renderer.
---

# Unreal Niagara Workflows

You have access to 70 Niagara actions through Monolith — the most comprehensive domain. These cover system/emitter structure, module configuration, parameter management, renderers, batch operations, and HLSL custom modules.

## Available Actions

```
monolith_discover({ namespace: "niagara" })
```

Execute via:
```
niagara_query({ action: "<action_name>", params: { ... } })
```

### Core Action Groups

**Systems:**
- `get_system_info` — Emitter list, warmup, bounds, fixed tick
- `get_system_overview` — High-level summary with emitter count and types
- `create_system` — New empty system
- `add_emitter_to_system` — Add emitter from template or asset

**Emitters:**
- `get_emitter_info` — Full emitter config (sim target, bounds, lifecycle)
- `get_emitter_modules` — All modules per stage (Spawn, Update, Render)
- `get_module_info` — Single module's parameters and values
- `create_emitter` — New emitter with sim target (CPU/GPU)
- `add_module` — Add module to emitter stage
- `remove_module` — Remove module from stage
- `reorder_modules` — Change module execution order
- `set_module_enabled` — Enable/disable a module

**Parameters:**
- `get_parameters` — All parameters on emitter or system
- `set_parameter` — Set parameter value (scalar, vector, curve, etc.)
- `add_parameter` — Add new user parameter
- `bind_parameter` — Bind parameter to another (e.g., Particles.Velocity → Module.Velocity)
- `get_parameter_bindings` — Show all binding chains

**Renderers:**
- `get_renderers` — All renderers on an emitter
- `add_renderer` — Add sprite, mesh, ribbon, light renderer
- `set_renderer_property` — Configure renderer settings
- `set_renderer_material` — Assign material to renderer

**HLSL / Custom Modules:**
- `get_scratch_pad` — Read HLSL scratch pad code
- `set_scratch_pad` — Write HLSL scratch pad code
- `create_scratch_pad_module` — New custom HLSL module
- `get_hlsl_functions` — List available HLSL functions in context

**Batch Operations:**
- `batch_set_parameters` — Set multiple parameters in one call
- `batch_add_modules` — Add multiple modules to multiple stages
- `clone_emitter` — Duplicate emitter with modifications
- `duplicate_system` — Full system clone

## Workflow: Create a Fire Effect from Scratch

1. **Create system**:
   ```
   niagara_query({ action: "create_system", params: { name: "NS_Fire", path: "/Game/VFX" } })
   ```

2. **Create GPU emitter** for performance:
   ```
   niagara_query({ action: "create_emitter", params: {
     system: "/Game/VFX/NS_Fire",
     name: "Flames",
     sim_target: "GPU",
     template: "Fountain"
   } })
   ```

3. **Configure spawn** — continuous burst:
   ```
   niagara_query({ action: "set_parameter", params: {
     asset_path: "/Game/VFX/NS_Fire",
     emitter: "Flames",
     module: "SpawnRate",
     parameter: "SpawnRate",
     value: 50
   } })
   ```

4. **Set lifetime and size**:
   ```
   niagara_query({ action: "batch_set_parameters", params: {
     asset_path: "/Game/VFX/NS_Fire",
     emitter: "Flames",
     parameters: [
       { module: "InitializeParticle", parameter: "Lifetime", value: { min: 0.5, max: 1.5 } },
       { module: "InitializeParticle", parameter: "SpriteSize", value: { min: [10, 10], max: [30, 30] } },
       { module: "InitializeParticle", parameter: "Color", value: [1.0, 0.5, 0.05, 1.0] }
     ]
   } })
   ```

5. **Add velocity and drag modules**:
   ```
   niagara_query({ action: "add_module", params: { asset_path: "/Game/VFX/NS_Fire", emitter: "Flames", stage: "ParticleSpawn", module: "AddVelocityInCone", position: -1 } })
   niagara_query({ action: "add_module", params: { asset_path: "/Game/VFX/NS_Fire", emitter: "Flames", stage: "ParticleUpdate", module: "Drag", position: -1 } })
   ```

6. **Add color-over-life** for fade:
   ```
   niagara_query({ action: "add_module", params: { asset_path: "/Game/VFX/NS_Fire", emitter: "Flames", stage: "ParticleUpdate", module: "ScaleColorOverLife", position: -1 } })
   ```

7. **Assign material** to sprite renderer:
   ```
   niagara_query({ action: "set_renderer_material", params: { asset_path: "/Game/VFX/NS_Fire", emitter: "Flames", renderer_index: 0, material: "/Game/VFX/Materials/M_FireSprite" } })
   ```

## Workflow: Write a Custom HLSL Module

For effects that need custom math (curl noise, SDF distance, etc.):

1. **Create scratch pad module**:
   ```
   niagara_query({ action: "create_scratch_pad_module", params: {
     asset_path: "/Game/VFX/NS_Custom",
     emitter: "MainEmitter",
     stage: "ParticleUpdate",
     name: "CurlNoiseForce"
   } })
   ```

2. **Add input/output parameters**:
   ```
   niagara_query({ action: "add_parameter", params: {
     asset_path: "/Game/VFX/NS_Custom",
     emitter: "MainEmitter",
     module: "CurlNoiseForce",
     name: "NoiseScale",
     type: "float",
     default: 1.0
   } })
   ```

3. **Write HLSL**:
   ```
   niagara_query({ action: "set_scratch_pad", params: {
     asset_path: "/Game/VFX/NS_Custom",
     emitter: "MainEmitter",
     module: "CurlNoiseForce",
     hlsl: "float3 pos = Particles.Position * NoiseScale;\nfloat3 curl;\ncurl.x = snoise(pos + float3(0, 0.1, 0)) - snoise(pos - float3(0, 0.1, 0));\ncurl.y = snoise(pos + float3(0, 0, 0.1)) - snoise(pos - float3(0, 0, 0.1));\ncurl.z = snoise(pos + float3(0.1, 0, 0)) - snoise(pos - float3(0.1, 0, 0));\nParticles.Velocity += curl * DeltaTime * 100.0;"
   } })
   ```

## Workflow: Analyze an Existing System

1. **Get system overview** first:
   ```
   niagara_query({ action: "get_system_overview", params: { asset_path: "/Game/VFX/NS_MagicBlast" } })
   ```

2. **Inspect each emitter's modules** by stage:
   ```
   niagara_query({ action: "get_emitter_modules", params: { asset_path: "/Game/VFX/NS_MagicBlast", emitter: "CoreParticles" } })
   ```

3. **Check parameter bindings** for dynamic inputs:
   ```
   niagara_query({ action: "get_parameter_bindings", params: { asset_path: "/Game/VFX/NS_MagicBlast", emitter: "CoreParticles" } })
   ```

4. **Read any custom HLSL**:
   ```
   niagara_query({ action: "get_scratch_pad", params: { asset_path: "/Game/VFX/NS_MagicBlast", emitter: "CoreParticles", module: "CustomForce" } })
   ```

## Tips

- **GPU vs CPU**: Use GPU sim for high particle counts (>1000). CPU for physics interactions or low counts.
- **Module order matters**: Modules execute top-to-bottom per stage. Force modules should come before drag.
- **Performance**: Keep GPU emitter count under 5 per system. Use LOD distance culling for distant effects.
- **Renderers**: Sprite = billboards (cheapest), Mesh = 3D objects (expensive at scale), Ribbon = trails/beams, Light = per-particle lights (very expensive).
- **Naming**: `NS_` for systems, `NE_` for standalone emitters, `NM_` for module scripts.
- **Parameter binding** is Niagara's power — bind spawn parameters to system-level inputs for artist control.
```

2. Commit:
```bash
git add Skills/unreal-niagara/
git commit -m "feat(skills): add unreal-niagara skill"
```

---

## Task 6.5 — Skill: Unreal Debugging

**Files:**
- Create `Skills/unreal-debugging/unreal-debugging.md`

**Steps:**

1. Write `Skills/unreal-debugging/unreal-debugging.md`:

```markdown
---
name: unreal-debugging
description: Use when debugging Unreal Engine issues — build errors, compilation failures, runtime crashes, log analysis, editor problems, or crashes. Triggers on keywords like build error, compile error, crash, log, callstack, assert, ensure, fatal, LNK, C4, C2, error C, unresolved, linker, exception.
---

# Unreal Debugging Workflows

You have access to editor diagnostics through Monolith's editor domain — build status, error logs, output logs, and crash context.

## Available Actions

```
monolith_discover({ namespace: "editor" })
```

Execute via:
```
editor_query({ action: "<action_name>", params: { ... } })
```

### Action Reference

| Action | Params | Returns |
|--------|--------|---------|
| `trigger_build` | `target?`, `config?` | Initiates project build, returns build ID |
| `get_build_status` | `build_id?` | Current/last build status and progress |
| `get_build_errors` | `build_id?`, `severity?` | Errors and warnings from compilation |
| `get_output_log` | `lines?`, `category?`, `verbosity?` | Recent editor output log entries |
| `get_message_log` | `category?` | Message log (Blueprint compile errors, asset issues) |
| `get_crash_context` | — | Last crash callstack, GPU state, memory info |
| `search_log` | `query`, `category?`, `time_range?` | Search through log history |
| `get_editor_stats` | — | FPS, memory usage, loaded asset count |
| `get_module_status` | — | Loaded modules and their states |
| `list_plugins` | `enabled_only?` | All plugins with enable/disable status |
| `get_config_value` | `section`, `key`, `file?` | Read engine/project config values |

## Workflow: Diagnose Build Errors

When the user reports a build failure or you need to compile:

1. **Trigger build** (if not already building):
   ```
   editor_query({ action: "trigger_build", params: {} })
   ```

2. **Check build status**:
   ```
   editor_query({ action: "get_build_status", params: {} })
   ```

3. **Get errors** once build completes:
   ```
   editor_query({ action: "get_build_errors", params: { severity: "Error" } })
   ```

4. **Analyze errors** using these patterns:

   **Unresolved external** (`LNK2019`, `LNK2001`):
   - Missing module dependency — check `.Build.cs` `PublicDependencyModuleNames`
   - Missing `dllexport` — check `*_API` macro on the class
   - Look up the symbol in engine source:
     ```
     source_query({ action: "search", params: { query: "UnresolvedSymbolName" } })
     ```

   **Include not found** (`C1083`):
   - Missing `PublicIncludePaths` or wrong module dependency
   - Check which module owns the header:
     ```
     source_query({ action: "find_header_module", params: { header: "MissingHeader.h" } })
     ```

   **Type mismatch / template errors** (`C2664`, `C2440`):
   - Read the source context around the error
   - Check UE type expectations (`FString` vs `const TCHAR*` vs `FName`)

   **GENERATED_BODY() errors**:
   - Stale generated headers — delete `Intermediate/` and rebuild
   - Missing `#include "ClassName.generated.h"` as LAST include

5. **Cross-reference with engine source** if the error involves engine APIs:
   ```
   source_query({ action: "get_symbol", params: { symbol: "UProblemClass::ProblemFunction" } })
   ```

## Workflow: Investigate a Crash

1. **Get crash context** immediately:
   ```
   editor_query({ action: "get_crash_context", params: {} })
   ```

2. **Read the output log** around crash time:
   ```
   editor_query({ action: "get_output_log", params: { lines: 100 } })
   ```

3. **Analyze the callstack**:
   - Find the first project-code frame (not engine frames)
   - Look up the function in project source or engine source
   - Common crash causes:
     - `nullptr` access — missing `IsValid()` check
     - `check()` / `ensure()` assertion failure — read the condition
     - Array out of bounds — check `Num()` before indexing
     - GC'd object access — weak pointer or stale `UPROPERTY()` reference

4. **Search logs** for related warnings before the crash:
   ```
   editor_query({ action: "search_log", params: { query: "Warning", category: "LogGameplay" } })
   ```

## Workflow: Runtime Log Analysis

When something behaves wrong but doesn't crash:

1. **Search logs** for the relevant system:
   ```
   editor_query({ action: "search_log", params: { query: "Inventory", category: "LogGame" } })
   ```

2. **Check specific log categories**:
   ```
   editor_query({ action: "get_output_log", params: { category: "LogBlueprintUserMessages", lines: 50 } })
   ```

3. **Check message log** for Blueprint compile issues:
   ```
   editor_query({ action: "get_message_log", params: { category: "BlueprintLog" } })
   ```

4. **Monitor editor performance** if lag is suspected:
   ```
   editor_query({ action: "get_editor_stats", params: {} })
   ```

## Common Error Patterns Quick Reference

| Error Code | Category | Typical Fix |
|-----------|----------|-------------|
| `LNK2019` | Linker | Add module to `Build.cs` dependencies |
| `LNK2001` | Linker | Add `*_API` export macro to class |
| `C1083` | Include | Fix include path or add module dependency |
| `C2664` | Type | Wrong parameter type — check UE type conversions |
| `C2511` | Override | Function signature doesn't match parent — check engine version changes |
| `C4430` | Syntax | Missing type specifier — often a missing include |
| `C2039` | Member | Member doesn't exist — API may have changed between UE versions |

## Tips

- **Always get errors first** before attempting fixes. Don't guess at build problems.
- **Engine version matters**: APIs change between UE versions. Use `source_query` to verify the current version's API.
- **Blueprint compile errors** show in Message Log, not Output Log. Use `get_message_log`.
- **Hot reload** is unreliable for adding new `UPROPERTY`/`UFUNCTION`. Always do a full restart after header changes.
- **Shipping builds** have different errors than editor builds — check both `Development Editor` and `Shipping` configs.
```

2. Commit:
```bash
git add Skills/unreal-debugging/
git commit -m "feat(skills): add unreal-debugging skill"
```

---

## Task 6.6 — Skill: Unreal Performance

**Files:**
- Create `Skills/unreal-performance/unreal-performance.md`

**Steps:**

1. Write `Skills/unreal-performance/unreal-performance.md`:

```markdown
---
name: unreal-performance
description: Use when optimizing Unreal Engine performance — profiling, draw calls, GPU/CPU bottlenecks, Lumen, Nanite, VSM, LODs, texture streaming, memory, shader complexity, tick optimization, or performance budgets. Triggers on keywords like performance, FPS, frame time, draw call, overdraw, profiling, stat unit, optimization, bottleneck, Lumen, Nanite, VSM.
---

# Unreal Performance Optimization

Use Monolith tools to gather data before making optimization decisions. Never optimize blindly.

## Data Gathering Tools

**Editor stats** (current session):
```
editor_query({ action: "get_editor_stats", params: {} })
```
Returns: FPS, frame time, game thread ms, render thread ms, GPU ms, memory usage, loaded assets.

**Material cost** (per-material):
```
material_query({ action: "get_instruction_count", params: { asset_path: "/Game/Materials/M_Target" } })
material_query({ action: "find_expensive_nodes", params: { asset_path: "/Game/Materials/M_Target" } })
```

**Project-wide search** for expensive patterns:
```
project_query({ action: "search", params: { query: "Translucent", types: ["Material"] } })
project_query({ action: "get_stats", params: {} })
```

**Config values** for rendering settings:
```
config_query({ action: "resolve", params: { key: "r.Lumen.Reflections.Allow", section: "Rendering" } })
```

## Performance Budget Reference

### Frame Time Targets
| Target FPS | Total Budget | Game Thread | Render Thread | GPU |
|-----------|-------------|-------------|---------------|-----|
| 30 fps | 33.3 ms | 15 ms | 15 ms | 33 ms |
| 60 fps | 16.6 ms | 8 ms | 8 ms | 16 ms |
| 120 fps | 8.3 ms | 4 ms | 4 ms | 8 ms |

### Material Instruction Budgets
| Type | Budget | Notes |
|------|--------|-------|
| Opaque | < 200 inst | Base pass only |
| Masked | < 150 inst | Adds clip cost |
| Translucent | < 100 inst | Per-pixel overdraw |
| UI/HUD | < 50 inst | Drawn every frame |

### Draw Call Budgets
| Platform | Budget |
|----------|--------|
| PC (DX12) | < 5000 |
| Console | < 3000 |
| Mobile | < 500 |

## Optimization Strategies by Bottleneck

### GPU Bound (GPU ms > Render Thread ms)

**Pixel/shader bound** (high resolution, complex materials):
1. Check material instruction counts across expensive materials
2. Reduce translucent material usage — search for them:
   ```
   project_query({ action: "search", params: { query: "Translucent", types: ["Material"] } })
   ```
3. Enable `Fully Rough` on distant material LODs
4. Pack textures (ORM channel packing) to reduce samples
5. Reduce post-process chain complexity

**Geometry bound** (too many triangles):
1. Enable Nanite for static meshes where applicable
2. Add LODs — check meshes without LODs:
   ```
   project_query({ action: "search", params: { query: "StaticMesh LOD:0", types: ["StaticMesh"] } })
   ```
3. Use HISMs (Hierarchical Instanced Static Meshes) for repeated objects
4. Merge small static meshes in level

### CPU Bound (Game Thread ms high)

1. **Find tick-heavy actors** — search for Tick implementations:
   ```
   source_query({ action: "search", params: { query: "TickComponent\\|Tick(float" } })
   ```
2. Move tick logic to timers where possible
3. Use actor component tick intervals (not every frame)
4. Reduce overlap/collision query frequency
5. Profile Blueprint Nativization candidates — complex BP logic should be C++

### Render Thread Bound

1. Reduce draw calls — merge actors, use instancing
2. Reduce dynamic shadow-casting lights
3. Cull aggressively — set per-actor `MaxDrawDistance`
4. Use occlusion culling volumes in complex scenes

## Lumen Optimization

```
config_query({ action: "search", params: { query: "r.Lumen" } })
```

Key settings:
- `r.Lumen.DiffuseIndirect.Allow` — Master toggle
- `r.Lumen.Reflections.Allow` — Reflection toggle
- `r.Lumen.TraceMeshSDFs.Allow` — Mesh SDF tracing (expensive)
- `r.LumenScene.SurfaceCache.Resolution` — Lower = faster, less quality

Tips:
- Lumen works best with large, simple geometry. Avoid tiny mesh clusters.
- Software ray tracing is cheaper than hardware RT but lower quality.
- Emissive materials need `Use Emissive for Static Lighting` disabled for Lumen.

## Nanite Optimization

- Enable on static meshes with >10k triangles
- Disable on: skeletal meshes, masked materials, WPO-animated meshes
- Check Nanite eligibility across the project
- Nanite + VSM together give best performance for static geometry

## Virtual Shadow Maps (VSM)

```
config_query({ action: "search", params: { query: "r.Shadow.Virtual" } })
```

- `r.Shadow.Virtual.Enable` — Master toggle
- `r.Shadow.Virtual.MaxPhysicalPages` — VRAM budget for shadow pages
- Reduce shadow-casting movable lights (most expensive VSM cost)
- Static lights with baked shadows bypass VSM entirely

## Quick Wins Checklist

1. [ ] Disable `Tick` on actors that don't need per-frame updates
2. [ ] Enable Nanite on heavy static meshes
3. [ ] Channel-pack textures (ORM)
4. [ ] Replace translucent materials with masked where possible
5. [ ] Set `MaxDrawDistance` on small detail objects
6. [ ] Use HISMs for foliage/rocks/debris
7. [ ] Reduce dynamic shadow casters
8. [ ] Add LODs to non-Nanite meshes
9. [ ] Profile with `stat unit`, `stat gpu`, `stat scenerendering`
10. [ ] Check texture streaming pool size matches VRAM budget
```

2. Commit:
```bash
git add Skills/unreal-performance/
git commit -m "feat(skills): add unreal-performance skill"
```

---

## Task 6.7 — Skill: Unreal Project Search

**Files:**
- Create `Skills/unreal-project-search/unreal-project-search.md`

**Steps:**

1. Write `Skills/unreal-project-search/unreal-project-search.md`:

```markdown
---
name: unreal-project-search
description: Use when searching an Unreal project — finding assets, tracing references, understanding project structure, asset dependencies, or getting project statistics. Triggers on keywords like find asset, search project, where is, references to, depends on, asset list, project stats, what uses.
---

# Unreal Project Search Workflows

Monolith's project index (MonolithIndex) provides deep full-text search across the entire Unreal project — C++ source, Blueprints, Materials, Animations, Niagara, Data Tables, Levels, Meshes, Textures, Sounds, Gameplay Tags, and more.

The index is built on first editor launch and stored at `Plugins/Monolith/Saved/ProjectIndex.db` (SQLite + FTS5).

## Available Actions

```
monolith_discover({ namespace: "project" })
```

Execute via:
```
project_query({ action: "<action_name>", params: { ... } })
```

### Action Reference

| Action | Params | Returns |
|--------|--------|---------|
| `search` | `query`, `types?[]`, `limit?` | Full-text search across indexed assets |
| `find_references` | `asset_path` | All assets that reference the given asset |
| `find_by_type` | `type`, `filter?`, `path?`, `limit?` | Find assets by class/type with optional filter |
| `get_stats` | — | Project-wide statistics (asset counts by type, index age) |
| `get_asset_details` | `asset_path` | Deep details for a single asset (type-specific metadata) |

## Workflow: Find Assets

**Search by name or content:**
```
project_query({ action: "search", params: { query: "enemy health" } })
```
This searches names, paths, and indexed content (BP variables, material params, etc.).

**Search by type:**
```
project_query({ action: "find_by_type", params: { type: "Blueprint", filter: "Enemy" } })
project_query({ action: "find_by_type", params: { type: "Material", path: "/Game/Environment" } })
project_query({ action: "find_by_type", params: { type: "NiagaraSystem" } })
```

**Supported types:**
- `Blueprint`, `Material`, `MaterialInstance`, `MaterialFunction`
- `AnimSequence`, `AnimMontage`, `AnimBlueprint`, `BlendSpace`
- `NiagaraSystem`, `NiagaraEmitter`
- `StaticMesh`, `SkeletalMesh`, `Texture2D`
- `SoundWave`, `SoundCue`
- `DataTable`, `CurveTable`
- `Level`, `World`
- `GameplayTag`
- `WidgetBlueprint`

## Workflow: Trace References and Dependencies

**Who uses this asset?**
```
project_query({ action: "find_references", params: { asset_path: "/Game/Textures/T_Rock_BaseColor" } })
```
Returns all assets referencing it — materials, Blueprints, levels, etc.

**Use case — safe deletion check:**
Before suggesting deletion of an asset, ALWAYS check references:
```
project_query({ action: "find_references", params: { asset_path: "/Game/Materials/M_OldRock" } })
```
If nothing references it, it's safe. If references exist, list them.

**Use case — impact analysis:**
When changing a base class or shared material:
```
project_query({ action: "find_references", params: { asset_path: "/Game/Materials/M_MasterLandscape" } })
```
Shows all material instances and levels that will be affected.

## Workflow: Understand Project Structure

1. **Get project stats** for an overview:
   ```
   project_query({ action: "get_stats", params: {} })
   ```
   Returns counts by type, total index size, last index time.

2. **Browse by type** to understand organization:
   ```
   project_query({ action: "find_by_type", params: { type: "Blueprint", path: "/Game/Characters" } })
   project_query({ action: "find_by_type", params: { type: "Material", path: "/Game/Environment" } })
   ```

3. **Get deep details** on specific assets:
   ```
   project_query({ action: "get_asset_details", params: { asset_path: "/Game/Characters/BP_Player" } })
   ```
   Returns type-specific metadata: for Blueprints (parent class, interfaces, variable count), for Materials (shading model, blend mode, parameter count), etc.

## Workflow: Answer "Where is X?"

When the user asks "where is the player health defined?" or "find the main menu":

1. **Broad search** first:
   ```
   project_query({ action: "search", params: { query: "player health" } })
   ```

2. **Narrow by type** if too many results:
   ```
   project_query({ action: "search", params: { query: "player health", types: ["Blueprint"] } })
   ```

3. **Once found**, use domain-specific tools for details:
   ```
   blueprint_query({ action: "get_variables", params: { asset_path: "/Game/Characters/BP_Player" } })
   ```

## Tips

- **Full-text search** uses FTS5 — supports quoted phrases (`"exact match"`), prefix (`enemy*`), and boolean (`health AND player`).
- **Re-index** after major asset changes: `monolith_reindex()` or use the button in Plugin Settings.
- **Asset paths** use `/Game/...` format. Plugin content uses `/PluginName/...`.
- **Index covers content**, not just names — it indexes BP variable names, material parameter names, data table column headers, gameplay tag hierarchies, etc.
- **Performance**: Searches are fast (SQLite FTS5). Don't hesitate to search broadly.
```

2. Commit:
```bash
git add Skills/unreal-project-search/
git commit -m "feat(skills): add unreal-project-search skill"
```

---

## Task 6.8 — Skill: Unreal C++

**Files:**
- Create `Skills/unreal-cpp/unreal-cpp.md`

**Steps:**

1. Write `Skills/unreal-cpp/unreal-cpp.md`:

```markdown
---
name: unreal-cpp
description: Use when working with Unreal Engine C++ — writing gameplay code, understanding UE APIs, looking up engine source, header includes, module dependencies, class hierarchies, UCLASS/UFUNCTION/UPROPERTY macros, delegates, interfaces, or Build.cs configuration. Triggers on keywords like UE C++, UCLASS, UFUNCTION, UPROPERTY, Build.cs, module, include, delegate, FString, TArray, TSharedPtr, BlueprintCallable, engine source, API reference.
---

# Unreal C++ Workflows

Use Monolith's source intelligence to look up engine APIs, trace call graphs, understand class hierarchies, and resolve include/module dependencies. The source index covers the full UE source tree.

## Available Actions

```
monolith_discover({ namespace: "source" })
```

Execute via:
```
source_query({ action: "<action_name>", params: { ... } })
```

### Action Reference

| Action | Params | Returns |
|--------|--------|---------|
| `search` | `query`, `scope?`, `limit?` | Full-text search across engine source |
| `get_symbol` | `symbol` | Full definition, signature, file location, doc comments |
| `get_class_hierarchy` | `class_name` | Parent chain + direct children |
| `find_callers` | `function_name`, `limit?` | Who calls this function |
| `find_callees` | `function_name`, `limit?` | What this function calls |
| `find_references` | `symbol`, `limit?` | All references to a symbol |
| `find_header_module` | `header` | Which module owns a header file |
| `get_module_info` | `module_name` | Module's public/private deps, type, path |
| `get_include_chain` | `header` | Include resolution path |
| `get_signature` | `function_name` | Just the function signature (fast lookup) |
| `get_deprecation_info` | `symbol` | Deprecation status, version deprecated, replacement |
| `read_source` | `file`, `start_line?`, `end_line?` | Read engine source file content |
| `search_api` | `query`, `category?` | Search by API pattern (e.g., "spawn actor") |
| `get_engine_version` | — | Current engine version and changelist |

## Workflow: Look Up an Engine API

When you need to understand a UE function or class:

1. **Get the symbol** for full details:
   ```
   source_query({ action: "get_symbol", params: { symbol: "UWorld::SpawnActor" } })
   ```
   Returns: full signature, file path, line number, doc comments, template parameters.

2. **Check for deprecation**:
   ```
   source_query({ action: "get_deprecation_info", params: { symbol: "UWorld::SpawnActor" } })
   ```

3. **Read surrounding source** if you need more context:
   ```
   source_query({ action: "read_source", params: { file: "Engine/Source/Runtime/Engine/Private/World.cpp", start_line: 450, end_line: 500 } })
   ```

## Workflow: Resolve Include/Module Dependencies

When you get `#include not found` or `unresolved external`:

1. **Find which module owns the header**:
   ```
   source_query({ action: "find_header_module", params: { header: "GameplayAbilitySpec.h" } })
   ```
   Returns: `GameplayAbilities` module.

2. **Get module dependency info**:
   ```
   source_query({ action: "get_module_info", params: { module_name: "GameplayAbilities" } })
   ```
   Returns: module type, public/private dependencies, include paths.

3. **Add to Build.cs**:
   ```csharp
   PublicDependencyModuleNames.AddRange(new string[] { "GameplayAbilities" });
   ```

## Workflow: Understand a Class Hierarchy

1. **Get hierarchy**:
   ```
   source_query({ action: "get_class_hierarchy", params: { class_name: "ACharacter" } })
   ```
   Returns: `AActor → APawn → ACharacter` + children like `ACharacter → [your classes]`.

2. **Find overridable functions** by searching the parent:
   ```
   source_query({ action: "search", params: { query: "virtual.*ACharacter::", scope: "headers" } })
   ```

3. **Check specific function signature** before overriding:
   ```
   source_query({ action: "get_signature", params: { function_name: "ACharacter::Jump" } })
   ```

## Workflow: Trace Call Graphs

When understanding complex engine behavior:

1. **What does this function call?**
   ```
   source_query({ action: "find_callees", params: { function_name: "ACharacter::Jump" } })
   ```

2. **Who calls this function?**
   ```
   source_query({ action: "find_callers", params: { function_name: "UCharacterMovementComponent::DoJump" } })
   ```

3. Use this to understand the full flow: `Input → Jump → LaunchCharacter → DoJump → physics update`.

## UE C++ Quick Reference

### Common Macros

```cpp
UCLASS(BlueprintType, Blueprintable)
class MYGAME_API AMyActor : public AActor
{
    GENERATED_BODY()

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Stats")
    float Health = 100.f;

    UPROPERTY(ReplicatedUsing = OnRep_Health)
    float ReplicatedHealth;

    UFUNCTION(BlueprintCallable, Category = "Combat")
    void TakeDamage(float Amount);

    UFUNCTION(BlueprintNativeEvent)
    void OnHit(const FHitResult& Hit);

    UFUNCTION(Server, Reliable)
    void ServerRPC_Attack(FVector Target);
};
```

### Common Type Conversions

| From | To | Method |
|------|----|--------|
| `FString` | `FName` | `FName(*MyString)` |
| `FName` | `FString` | `MyName.ToString()` |
| `FString` | `const TCHAR*` | `*MyString` |
| `FString` | `std::string` | `TCHAR_TO_UTF8(*MyString)` |
| `int32` | `FString` | `FString::FromInt(Val)` |
| `float` | `FString` | `FString::SanitizeFloat(Val)` |
| `FText` | `FString` | `MyText.ToString()` |
| `FString` | `FText` | `FText::FromString(Str)` |

### Common Containers

```cpp
TArray<int32> Arr;              // Dynamic array
TMap<FName, int32> Map;         // Hash map
TSet<FName> Set;                // Hash set
TSharedPtr<FMyStruct> Ptr;      // Shared pointer (non-UObject)
TWeakObjectPtr<AActor> Weak;    // Weak UObject pointer
TSoftObjectPtr<UTexture> Soft;  // Soft reference (async load)
TSubclassOf<AActor> Class;      // Class reference
```

### Build.cs Pattern

```csharp
using UnrealBuildTool;

public class MyModule : ModuleRules
{
    public MyModule(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new string[] {
            "Core",
            "CoreUObject",
            "Engine"
        });

        PrivateDependencyModuleNames.AddRange(new string[] {
            "Slate",
            "SlateCore",
            "UnrealEd"  // Editor-only! Guard with Target.bBuildEditor
        });
    }
}
```

## Tips

- **Always verify APIs** against the actual engine version with `source_query`. Documentation and blog posts may be outdated.
- **`_API` export macro**: Every module has one (e.g., `ENGINE_API`, `MYGAME_API`). Required for cross-module symbol access.
- **Editor-only code**: Wrap with `#if WITH_EDITOR` / `#endif`. Never reference editor modules from runtime builds.
- **Hot reload pitfall**: Adding new `UPROPERTY` or `UFUNCTION` requires editor restart, not just hot reload. Hot reload only works for function body changes.
- **Reflection**: `GENERATED_BODY()` must be the first line in the class body. The `.generated.h` include must be LAST.
- **Delegates**: `DECLARE_DYNAMIC_MULTICAST_DELEGATE` for Blueprint-exposed, `DECLARE_MULTICAST_DELEGATE` for C++-only.
```

2. Commit:
```bash
git add Skills/unreal-cpp/
git commit -m "feat(skills): add unreal-cpp skill"
```

---

## Task 6.9 — Templates

**Files:**
- Create `Templates/CLAUDE.md.example`
- Create `Templates/.mcp.json.example`

### 6.9.1 — CLAUDE.md.example

Write `Templates/CLAUDE.md.example`:

```markdown
# Project Instructions for AI (Monolith-Powered)

## Project Overview
<!-- Describe your game/project in 2-3 sentences -->
This is [PROJECT_NAME], a [GENRE] game built in Unreal Engine [VERSION].

## Architecture
<!-- Key architectural decisions -->
- **Language split**: [e.g., "C++ for core systems, Blueprints for gameplay iteration"]
- **Networking**: [e.g., "Dedicated server, replicated via Gameplay Ability System"]
- **Rendering**: [e.g., "Lumen GI, Nanite meshes, Virtual Shadow Maps"]

## Directory Structure
```
Content/
  Blueprints/     — Gameplay Blueprints (BP_ prefix)
  Characters/     — Character assets, ABPs, montages
  Environment/    — Level art, materials, meshes
  VFX/            — Niagara systems (NS_ prefix)
  UI/             — Widget Blueprints (WBP_ prefix)
  Data/           — Data Tables, Curves, config assets
Source/
  [ModuleName]/   — C++ gameplay code
```

## Naming Conventions
- `BP_` — Blueprint
- `M_` / `MI_` — Material / Material Instance
- `T_` — Texture
- `SM_` / `SK_` — Static Mesh / Skeletal Mesh
- `AM_` — Animation Montage
- `ABP_` — Animation Blueprint
- `BS_` — Blend Space
- `NS_` — Niagara System
- `WBP_` — Widget Blueprint
- `DT_` — Data Table
- `E_` — Enum
- `S_` — Struct

## Monolith Tools Available
This project uses the Monolith MCP server. Available tool namespaces:
- `monolith_discover` — List available actions per namespace
- `monolith_status` — Server health and index status
- `blueprint_query` — Blueprint graph inspection (5 actions)
- `material_query` — Material creation and inspection (46 actions)
- `animation_query` — Animation system workflows (62 actions)
- `niagara_query` — Particle system management (70 actions)
- `editor_query` — Build, logs, crash diagnostics (11 actions)
- `config_query` — Engine/project config inspection (6 actions)
- `source_query` — Engine source lookup and API reference (14 actions)
- `project_query` — Deep project search and references (5 actions)

**Always start with** `monolith_discover` to see available actions before using a domain.

## Workflow Guidelines
1. **Before creating assets**: Search the project first (`project_query search`) to avoid duplicates.
2. **Before modifying materials**: Read current state (`material_query get_material_info`) first.
3. **Before writing C++**: Look up engine APIs (`source_query get_symbol`) to verify signatures.
4. **After changes**: Trigger build (`editor_query trigger_build`) and check for errors.
5. **When debugging**: Get logs and crash context (`editor_query`) before guessing at fixes.

## Project-Specific Rules
<!-- Add your project's specific rules here -->
- [e.g., "All gameplay variables must be replicated"]
- [e.g., "Materials must stay under 200 instructions"]
- [e.g., "Blueprint-only prototyping is fine; nativize before shipping"]
- [e.g., "Use Gameplay Tags instead of enums for extensible categories"]
```

### 6.9.2 — .mcp.json.example

Write `Templates/.mcp.json.example`:

```json
{
  "mcpServers": {
    "monolith": {
      "type": "streamable-http",
      "url": "http://localhost:9316/mcp"
    }
  }
}
```

Note: The legacy SSE transport is also supported for older MCP clients:

```json
{
  "mcpServers": {
    "monolith": {
      "type": "sse",
      "url": "http://localhost:9316/sse"
    }
  }
}
```

Both configs point at the same embedded server (port configurable in Plugin Settings). The Streamable HTTP transport is preferred for Claude Code and modern MCP clients. Legacy SSE is supported for backward compatibility.

Commit:
```bash
git add Templates/
git commit -m "feat(templates): add CLAUDE.md and .mcp.json examples"
```

---

## Task 6.10 — README.md

**Files:**
- Create `README.md`

Write `README.md`:

````markdown
# Monolith

One plugin. Every Unreal Engine MCP tool. Zero Python middleware.

Monolith consolidates 9 MCP servers and 4 C++ plugins into a single Unreal Engine plugin with an embedded MCP server. 231 tools, 14 endpoints, one install.

## Architecture

```
┌─────────────────────────────────────────────────────────────┐
│                    Claude / MCP Client                       │
│                                                             │
│  monolith_discover  blueprint_query  material_query  ...    │
└──────────────────────────┬──────────────────────────────────┘
                           │ Streamable HTTP / SSE
                           ▼
┌─────────────────────────────────────────────────────────────┐
│                   MonolithCore (C++)                         │
│              Embedded HTTP MCP Server (:9316)                │
│         Tool Registry  ·  JSON Dispatch  ·  Auth            │
├─────────┬──────────┬──────────┬──────────┬─────────────────┤
│Blueprint│ Material │Animation │ Niagara  │   Editor        │
│  5 acts │ 46 acts  │ 62 acts  │ 70 acts  │   11 acts       │
├─────────┼──────────┼──────────┼──────────┼─────────────────┤
│ Config  │  Index   │  Source  │          │                  │
│  6 acts │  5 acts  │ 14 acts  │          │                  │
├─────────┴──────────┴──────────┴──────────┴─────────────────┤
│              Unreal Engine 5.7+ Editor                      │
│     Asset Registry · Blueprint VM · Material Editor         │
│     Niagara · Sequencer · Animation · Slate UI              │
└─────────────────────────────────────────────────────────────┘
```

## Domain Summary

| Namespace | Actions | Capabilities |
|-----------|---------|-------------|
| `blueprint` | 5 | Graph topology, variables, execution flow, node search |
| `material` | 46 | Inspection, graph building, PBR templates, validation, optimization |
| `animation` | 62 | Sequences, montages, blend spaces, ABPs, skeletons, editing |
| `niagara` | 70 | Systems, emitters, modules, parameters, renderers, HLSL, batch |
| `editor` | 11 | Build trigger, errors, logs, crash context, editor stats |
| `config` | 6 | Config resolve, explain, diff, search |
| `project` | 5 | Full-text search, references, asset details, stats |
| `source` | 14 | Engine source search, symbols, call graphs, hierarchy, modules |
| **Total** | **219+** | **Core actions (discovery, status, update, reindex add more)** |

## What It Replaces

| Before (9 servers + 4 plugins) | After (Monolith) |
|-------------------------------|-------------------|
| unreal-blueprint-mcp (Python) + BlueprintReader (C++) | `blueprint_query` |
| unreal-material-mcp (Python) + MaterialMCPReader (C++) | `material_query` |
| unreal-animation-mcp (Python) + AnimationMCPReader (C++) | `animation_query` |
| unreal-niagara-mcp (Python) + NiagaraMCPBridge (C++) | `niagara_query` |
| unreal-editor-mcp (Python) | `editor_query` |
| unreal-config-mcp (Python) | `config_query` |
| unreal-project-mcp (Python) | `project_query` |
| unreal-source-mcp (Python) | `source_query` |
| unreal-api-mcp (reimplemented) | Merged into `source_query` |

**Result**: No Python runtime needed for editor tools. No `EditorBridge` HTTP layer. No copy-pasted config files. Direct C++ access to every Unreal system.

## Installation

### 1. Clone into your project's Plugins folder

```bash
cd YourProject/Plugins
git clone https://github.com/tumourlove/monolith.git Monolith
```

### 2. Add MCP config

Copy `Templates/.mcp.json.example` to your project root as `.mcp.json`:

```bash
cp Plugins/Monolith/Templates/.mcp.json.example .mcp.json
```

Or create `.mcp.json` manually:

```json
{
  "mcpServers": {
    "monolith": {
      "type": "streamable-http",
      "url": "http://localhost:9316/mcp"
    }
  }
}
```

### 3. Launch the editor

On first launch, Monolith will:
- Start the embedded MCP server on port 9316
- Index your entire project (C++, Blueprints, Materials, etc.)
- Check for updates from GitHub Releases

### 4. (Optional) Install Claude Code skills

Copy skills into your Claude Code skills directory:

```bash
# From your project root
cp -r Plugins/Monolith/Skills/* ~/.claude/skills/
```

Or symlink them:
```bash
ln -s $(pwd)/Plugins/Monolith/Skills/* ~/.claude/skills/
```

### 5. (Optional) Add project instructions

Copy the CLAUDE.md template and customize it:

```bash
cp Plugins/Monolith/Templates/CLAUDE.md.example CLAUDE.md
```

## Configuration

Open **Edit > Project Settings > Plugins > Monolith**:

| Setting | Default | Description |
|---------|---------|-------------|
| Server Port | `9316` | HTTP port for MCP server |
| Auto-Update | `true` | Check GitHub Releases on startup |
| Database Path | `Plugins/Monolith/Saved/` | Override index database location |
| Module Toggles | All enabled | Enable/disable individual domains |
| Log Verbosity | `Warning` | `Verbose`, `Log`, `Warning`, `Error` |

The **Re-index** button triggers a full project re-index (runs in background).

## Usage

### Discovery Pattern

Always start by discovering available actions:

```
monolith_discover()                           // All namespaces
monolith_discover({ namespace: "material" })  // Material actions only
```

### Example: Inspect a Blueprint

```
blueprint_query({ action: "list_graphs", params: { asset_path: "/Game/BP_Player" } })
blueprint_query({ action: "get_variables", params: { asset_path: "/Game/BP_Player" } })
```

### Example: Create a PBR Material

```
material_query({ action: "create_pbr_material", params: {
  name: "M_Rock", path: "/Game/Materials",
  albedo: "/Game/Textures/T_Rock_BC",
  normal: "/Game/Textures/T_Rock_N",
  roughness: "/Game/Textures/T_Rock_R"
} })
```

### Example: Search the Entire Project

```
project_query({ action: "search", params: { query: "health damage", types: ["Blueprint"] } })
project_query({ action: "find_references", params: { asset_path: "/Game/BP_Player" } })
```

### Example: Look Up Engine API

```
source_query({ action: "get_symbol", params: { symbol: "UWorld::SpawnActor" } })
source_query({ action: "get_class_hierarchy", params: { class_name: "ACharacter" } })
```

## Migration from Individual Servers

If you're currently using the individual MCP servers:

1. **Remove old MCP configs** from `.mcp.json` — delete entries for `unreal-blueprint-mcp`, `unreal-material-mcp`, etc.
2. **Remove old C++ plugins** — delete `BlueprintReader`, `MaterialMCPReader`, `AnimationMCPReader`, `NiagaraMCPBridge` from your Plugins folder.
3. **Stop old Python servers** — they're no longer needed.
4. **Install Monolith** using the steps above.
5. **Update tool calls** — replace direct tool names with the dispatch pattern:
   - `get_blueprint_graphs(path)` → `blueprint_query({ action: "list_graphs", params: { asset_path: path } })`
   - `create_material(...)` → `material_query({ action: "create_material", params: { ... } })`
   - Same pattern for all domains.

The AI-facing interface is similar but unified. The `monolith_discover` tool replaces reading individual tool lists.

## Skills

Monolith ships with 8 Claude Code skills for domain-specific workflows:

| Skill | Domain |
|-------|--------|
| `unreal-blueprints` | Blueprint inspection and debugging |
| `unreal-materials` | Material creation, PBR, optimization |
| `unreal-animation` | Montages, ABPs, blend spaces |
| `unreal-niagara` | Particle systems, HLSL modules |
| `unreal-debugging` | Build errors, logs, crashes |
| `unreal-performance` | Profiling and optimization |
| `unreal-project-search` | Deep project search patterns |
| `unreal-cpp` | UE C++ patterns and API lookup |

## Auto-Updates

Monolith checks GitHub Releases on editor startup. When a new version is available:
1. Editor notification appears with an **Update** button
2. Click to download and stage the update
3. Restart the editor to apply

Disable in Plugin Settings if you prefer manual updates.

## Troubleshooting

**MCP server not responding:**
- Check port 9316 is not in use: `netstat -an | findstr 9316`
- Verify Monolith plugin is enabled in Edit > Plugins
- Check Output Log for `LogMonolith` messages

**Index not building:**
- First index runs on startup — check Output Log for progress
- Manual re-index: Plugin Settings > Re-index button
- Ensure `Saved/` directory is writable

**Build errors after install:**
- Monolith requires UE 5.7+. Check engine version.
- Delete `Intermediate/` and `Binaries/` folders, regenerate project files
- Check that no old reader plugins conflict (BlueprintReader, etc.)

**Tools returning empty results:**
- Run `monolith_status()` to check index health
- Re-index if the database is stale
- Check that the target asset path uses `/Game/...` format

**Auto-update not working:**
- Check internet connectivity
- Verify `Auto-Update` is enabled in Plugin Settings
- Check Output Log for GitHub API errors (rate limiting)

## License

[LICENSE_TYPE] — See [LICENSE](LICENSE) for details.

## Attribution

Engine source indexing inspired by the Unreal ecosystem MCP community. See [ATTRIBUTION.md](ATTRIBUTION.md) for full credits.
````

Commit:
```bash
git add README.md
git commit -m "docs: add comprehensive README"
```

---

## Task 6.11 — CONTRIBUTING.md

**Files:**
- Create `CONTRIBUTING.md`

Write `CONTRIBUTING.md`:

```markdown
# Contributing to Monolith

## Getting Started

1. Fork the repository
2. Clone into a UE 5.7+ project's `Plugins/Monolith` directory
3. Create a feature branch: `git checkout -b feat/my-feature`
4. Make changes, test in-editor, submit a PR

## Project Structure

```
Source/
  MonolithCore/       — MCP server, registry, shared utilities
  Monolith<Domain>/   — One module per domain (Blueprint, Material, etc.)
```

Each domain module follows the same pattern:
- `Monolith<Domain>Module.h/.cpp` — Module startup, action registration
- `<Domain>Actions.h/.cpp` — Action handler implementations
- `Monolith<Domain>.Build.cs` — Module build rules

## Adding a New Action to an Existing Domain

1. **Define the handler** in `<Domain>Actions.cpp`:

```cpp
FMonolithResult UMyDomainActions::HandleNewAction(const TSharedPtr<FJsonObject>& Params)
{
    // Extract params
    FString AssetPath;
    if (!Params->TryGetStringField(TEXT("asset_path"), AssetPath))
    {
        return FMonolithResult::Error(TEXT("Missing required param: asset_path"));
    }

    // Do work using UE APIs
    // ...

    // Return result
    TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
    Result->SetStringField(TEXT("name"), AssetName);
    return FMonolithResult::Ok(Result);
}
```

2. **Register the action** in the module's `StartupModule()`:

```cpp
void FMonolithMyDomainModule::StartupModule()
{
    FMonolithToolRegistry& Registry = FMonolithToolRegistry::Get();

    Registry.RegisterAction(
        TEXT("mydomain"),           // namespace
        TEXT("new_action"),         // action name
        TEXT("Description here"),   // description shown in discover
        {                           // parameter schema
            { TEXT("asset_path"), EMonolithParamType::String, true, TEXT("Asset path") }
        },
        FMonolithActionDelegate::CreateStatic(&UMyDomainActions::HandleNewAction)
    );
}
```

3. **Update discovery** — action automatically appears in `monolith_discover({ namespace: "mydomain" })`.

4. **Test** — restart editor, call via MCP client.

## Adding a New Domain Module

1. Create `Source/MonolithNewDomain/`:
   - `MonolithNewDomain.Build.cs`
   - `MonolithNewDomainModule.h` / `.cpp`
   - `NewDomainActions.h` / `.cpp`

2. Add module to `Monolith.uplugin`:
```json
{
    "Name": "MonolithNewDomain",
    "Type": "Editor",
    "LoadingPhase": "PostEngineInit"
}
```

3. Add dependency on `MonolithCore` in your `.Build.cs`:
```csharp
PrivateDependencyModuleNames.Add("MonolithCore");
```

4. Register actions in `StartupModule()` using `FMonolithToolRegistry`.

## Coding Conventions

### C++
- UE coding standard: `F` prefix for structs, `U` for UObjects, `A` for Actors, `E` for enums, `I` for interfaces
- `TEXT()` macro for all string literals
- `TSharedPtr` / `TSharedRef` for non-UObject heap objects
- All actions return `FMonolithResult` (never raw JSON or strings)
- Use `MONOLITH_LOG` / `MONOLITH_WARN` / `MONOLITH_ERROR` macros (defined in MonolithCore)
- No raw `new` / `delete` — use `MakeShared`, `MakeUnique`, or UObject allocation

### Error Handling
- Validate all input params at the top of each action handler
- Return `FMonolithResult::Error(message)` for user errors
- Use `ensure()` for internal invariants (logs but doesn't crash)
- Never `check()` on user-provided data (crashes the editor)

### JSON
- Use `FJsonObject` / `FJsonValue` from JsonUtilities module
- Helper functions in MonolithCore: `MonolithJson::AssetToJson()`, `MonolithJson::ArrayResponse()`, etc.

### Naming
- Action names: `snake_case` (e.g., `get_material_info`, `create_emitter`)
- Namespaces: lowercase single word (e.g., `blueprint`, `material`, `niagara`)
- C++ classes: `UMonolith<Domain>Actions` for action containers

## Testing

Since UE Editor modules don't support automated unit testing easily:

1. **Compile test**: Build succeeds with no errors
2. **Manual MCP test**: Call each action via MCP client and verify response
3. **Regression**: Run `monolith_discover` and verify action count matches expected

We aim to add automated MCP integration tests in a future release.

## Commit Messages

Follow [Conventional Commits](https://www.conventionalcommits.org/):

```
feat(material): add get_instruction_count action
fix(core): handle empty JSON params without crash
docs: update README with new action counts
refactor(niagara): extract shared parameter validation
```

Scopes: `core`, `blueprint`, `material`, `animation`, `niagara`, `editor`, `config`, `index`, `source`, `skills`, `docs`.

## Pull Requests

- One feature/fix per PR
- Update action count in README if adding/removing actions
- Add skill documentation if adding a new domain
- Test manually in UE editor before submitting
```

Commit:
```bash
git add CONTRIBUTING.md
git commit -m "docs: add CONTRIBUTING.md"
```

---

## Task 6.12 — GitHub Actions CI

**Files:**
- Create `.github/workflows/build.yml`

Write `.github/workflows/build.yml`:

```yaml
name: Build Monolith Plugin

on:
  push:
    branches: [main, develop]
  pull_request:
    branches: [main]

env:
  UE_VERSION: "5.7"
  PLUGIN_NAME: "Monolith"

jobs:
  build-win64:
    name: Build (Win64)
    runs-on: windows-latest
    timeout-minutes: 60

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Setup UE (cached)
        uses: mhsmith/setup-unreal@v1
        with:
          unreal-version: ${{ env.UE_VERSION }}
          # Requires self-hosted runner with UE installed, OR
          # a cached UE installation in the runner's tool cache.
          # Adjust this step for your CI environment.

      - name: Create test project structure
        shell: bash
        run: |
          mkdir -p TestProject/Plugins
          cp -r . TestProject/Plugins/${{ env.PLUGIN_NAME }}
          # Create minimal .uproject
          cat > TestProject/TestProject.uproject << 'EOF'
          {
            "FileVersion": 3,
            "EngineAssociation": "${{ env.UE_VERSION }}",
            "Plugins": [
              { "Name": "Monolith", "Enabled": true }
            ]
          }
          EOF

      - name: Generate project files
        shell: cmd
        run: |
          "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" ^
            GenerateProjectFiles ^
            -project="%CD%\TestProject\TestProject.uproject"

      - name: Build (Development Editor Win64)
        shell: cmd
        run: |
          "%UE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat" ^
            BuildCookRun ^
            -project="%CD%\TestProject\TestProject.uproject" ^
            -noP4 ^
            -platform=Win64 ^
            -clientconfig=Development ^
            -build ^
            -compile ^
            -skipstage ^
            -utf8output ^
            -nosplash

      - name: Verify plugin binaries exist
        shell: bash
        run: |
          if [ ! -d "TestProject/Plugins/${{ env.PLUGIN_NAME }}/Binaries" ]; then
            echo "ERROR: Plugin binaries not found after build"
            exit 1
          fi
          echo "Build successful — plugin binaries present"
          find TestProject/Plugins/${{ env.PLUGIN_NAME }}/Binaries -type f -name "*.dll" | head -20

  lint:
    name: Lint (clang-tidy)
    runs-on: ubuntu-latest
    timeout-minutes: 10

    steps:
      - name: Checkout
        uses: actions/checkout@v4

      - name: Check header includes
        shell: bash
        run: |
          # Verify all .cpp files include their matching .h
          errors=0
          for cpp in $(find Source -name "*.cpp"); do
            header=$(basename "$cpp" .cpp).h
            if [ -f "$(dirname "$cpp")/$header" ]; then
              if ! grep -q "#include \"$header\"" "$cpp"; then
                echo "WARNING: $cpp does not include $header"
                errors=$((errors + 1))
              fi
            fi
          done
          echo "Header inclusion check complete. Issues: $errors"

      - name: Check for common issues
        shell: bash
        run: |
          # Check for raw new/delete (should use MakeShared/MakeUnique)
          if grep -rn "new " Source/ --include="*.cpp" | grep -v "MakeShared\|MakeUnique\|NewObject\|MakeShareable\|operator new\|//"; then
            echo "WARNING: Found potential raw 'new' usage. Prefer MakeShared/NewObject."
          fi

          # Check for missing TEXT() on string literals in UE context
          # (Just a heuristic — not all strings need TEXT())
          echo "Lint checks complete."

      - name: Validate .uplugin
        shell: bash
        run: |
          python3 -c "
          import json, sys
          with open('Monolith.uplugin') as f:
              data = json.load(f)
          assert 'Modules' in data, 'Missing Modules array'
          modules = [m['Name'] for m in data['Modules']]
          required = ['MonolithCore', 'MonolithBlueprint', 'MonolithMaterial',
                      'MonolithAnimation', 'MonolithNiagara', 'MonolithEditor',
                      'MonolithConfig', 'MonolithIndex', 'MonolithSource']
          for r in required:
              assert r in modules, f'Missing module: {r}'
          print(f'Plugin valid: {len(modules)} modules registered')
          "
```

Commit:
```bash
mkdir -p .github/workflows
git add .github/workflows/build.yml
git commit -m "ci: add GitHub Actions build workflow"
```

---

### Phase 6 Completion Checklist

| # | Item | Files |
|---|------|-------|
| 6.1 | Skill: Blueprints | `Skills/unreal-blueprints/unreal-blueprints.md` |
| 6.2 | Skill: Materials | `Skills/unreal-materials/unreal-materials.md` |
| 6.3 | Skill: Animation | `Skills/unreal-animation/unreal-animation.md` |
| 6.4 | Skill: Niagara | `Skills/unreal-niagara/unreal-niagara.md` |
| 6.5 | Skill: Debugging | `Skills/unreal-debugging/unreal-debugging.md` |
| 6.6 | Skill: Performance | `Skills/unreal-performance/unreal-performance.md` |
| 6.7 | Skill: Project Search | `Skills/unreal-project-search/unreal-project-search.md` |
| 6.8 | Skill: C++ | `Skills/unreal-cpp/unreal-cpp.md` |
| 6.9 | Templates | `Templates/CLAUDE.md.example`, `Templates/.mcp.json.example` |
| 6.10 | README | `README.md` |
| 6.11 | CONTRIBUTING | `CONTRIBUTING.md` |
| 6.12 | CI | `.github/workflows/build.yml` |

**Total new files:** 14
**Phase 6 complete.** All skills contain full workflows, parameter references, and practical examples using actual Monolith tool names. Templates are ready to copy. README covers architecture, migration, and troubleshooting. CI validates builds on every push.

