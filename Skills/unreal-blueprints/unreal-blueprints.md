---
name: unreal-blueprints
description: Use when working with Unreal Engine Blueprints via Monolith MCP — reading graph topology, inspecting variables, tracing execution flow, searching nodes, or understanding Blueprint architecture. Triggers on Blueprint, BP, event graph, node, variable, function graph.
---

# Unreal Blueprint Workflows

You have access to **Monolith** with deep Blueprint introspection via `blueprint.query()`.

## Discovery

Always discover available actions first:
```
monolith.discover({ namespace: "blueprint" })
```

## Action Reference

| Action | Key Params | Purpose |
|--------|-----------|---------|
| `list_graphs` | `asset` | List all event/function/macro graphs in a Blueprint |
| `get_graph_data` | `asset`, `graph` | Full node topology — pins, connections, positions |
| `get_variables` | `asset` | Variables with types, defaults, categories, replication |
| `get_execution_flow` | `asset`, `graph` | Trace execution wires from entry to terminal nodes |
| `search_nodes` | `asset`, `query` | Find nodes by class name, display name, or comment |

## Asset Path Conventions

All asset paths follow UE content browser format (no .uasset extension):

| Location | Path Format | Example |
|----------|------------|---------|
| Project Content/ | `/Game/Path/To/Asset` | `/Game/Materials/M_Rock` |
| Project Plugins/ | `/PluginName/Path/To/Asset` | `/CarnageFX/Materials/M_Blood` |
| Engine Plugins | `/PluginName/Path/To/Asset` | `/Niagara/DefaultAssets/SystemAssets/NS_Default` |

**Note:** For project plugins, the path starts with the plugin name as configured in the .uplugin file's "MountPoint" — which defaults to `/<PluginName>/`. Most plugins mount their Content/ folder there directly.

```
blueprint.query({ action: "list_graphs", params: { asset: "/Game/Blueprints/BP_Player" } })
```

## Common Workflows

### Understand a Blueprint's structure
```
blueprint.query({ action: "list_graphs", params: { asset: "/Game/Blueprints/BP_Enemy" } })
blueprint.query({ action: "get_variables", params: { asset: "/Game/Blueprints/BP_Enemy" } })
```

### Trace logic flow
```
blueprint.query({ action: "get_execution_flow", params: { asset: "/Game/Blueprints/BP_Enemy", graph: "EventGraph" } })
```

### Find where a function is called
```
blueprint.query({ action: "search_nodes", params: { asset: "/Game/Blueprints/BP_Enemy", query: "TakeDamage" } })
```

### Find Blueprints across the project
Use the project index to locate BPs before inspecting them:
```
project.query({ action: "search", params: { query: "BP_Enemy", type: "Blueprint" } })
```

## Tips

- **Graph names** are returned by `list_graphs` — use exact names for drill-down calls
- **Pin connections** in `get_graph_data` show both execution (white) and data (colored) wires
- **Execution flow** traces only follow white exec pins — data flow is shown in graph data
- **Variables** include replication flags (`Replicated`, `RepNotify`) and `EditAnywhere`/`BlueprintReadOnly` specifiers
- For C++ parent class analysis, combine with `source.query("get_class_hierarchy", ...)`
