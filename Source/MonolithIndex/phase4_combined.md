# Phase 4: Deep Project Indexer (MonolithIndex)

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
