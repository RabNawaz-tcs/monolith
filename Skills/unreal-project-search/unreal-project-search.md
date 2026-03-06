---
name: unreal-project-search
description: Use when searching for assets, references, or dependencies across an Unreal project via Monolith MCP — FTS5 full-text search, asset discovery, reference tracing, type filtering. Triggers on find asset, search project, asset references, where is, dependencies.
---

# Unreal Project Search Workflows

You have access to **Monolith** with a deep project index via `project.query()`.

## Discovery

```
monolith.discover({ namespace: "project" })
```

## Action Reference

| Action | Purpose |
|--------|---------|
| `search` | Full-text search across all indexed assets, nodes, variables, parameters |
| `find_references` | Find all assets that reference a given asset |
| `find_by_type` | List all assets of a specific type |
| `get_asset_details` | Detailed metadata for a specific asset |
| `get_stats` | Index statistics — asset counts by type, index freshness |

## FTS5 Search Syntax

The `search` action uses SQLite FTS5 under the hood. Key syntax:

| Pattern | Meaning |
|---------|---------|
| `BP_Enemy` | Match exact token |
| `BP_*` | Prefix match |
| `"BP_Enemy Health"` | Exact phrase |
| `BP_Enemy OR BP_Ally` | Either term |
| `BP_Enemy NOT Health` | Exclude term |
| `BP_Enemy NEAR/3 Health` | Terms within 3 tokens |

## Common Workflows

### Find any asset by name
```
project.query({ action: "search", params: { query: "BP_Player*" } })
```

### Find all Blueprints in the project
```
project.query({ action: "find_by_type", params: { type: "Blueprint" } })
```

### Find all assets referencing a material
```
project.query({ action: "find_references", params: { asset: "/Game/Materials/M_Skin" } })
```

### Get detailed metadata for an asset
```
project.query({ action: "get_asset_details", params: { asset: "/Game/Blueprints/BP_Player" } })
```

### Check index health
```
project.query({ action: "get_stats", params: {} })
```

### Find all Niagara systems
```
project.query({ action: "find_by_type", params: { type: "NiagaraSystem" } })
```

### Find assets by variable or parameter name
```
project.query({ action: "search", params: { query: "Health" } })
```

## Supported Asset Types

The index covers these types for `find_by_type`:
- `Blueprint`, `WidgetBlueprint`, `AnimBlueprint`
- `Material`, `MaterialInstance`, `MaterialFunction`
- `NiagaraSystem`, `NiagaraEmitter`
- `AnimSequence`, `AnimMontage`, `BlendSpace`
- `Texture2D`, `StaticMesh`, `SkeletalMesh`
- `DataTable`, `CurveTable`, `SoundWave`

## Tips

- The index is built on first launch and auto-updates — use `monolith.reindex()` to force rebuild
- FTS5 search covers asset names, node names, variable names, parameter names, and comments
- Use `find_references` to understand dependency chains before deleting or renaming assets
- Combine with domain-specific tools: search first, then inspect with `blueprint.query`, `material.query`, etc.
- `get_stats` shows last index time — if stale, trigger `monolith.reindex()`
