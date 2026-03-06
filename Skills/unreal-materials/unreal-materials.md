---
name: unreal-materials
description: Use when creating, editing, or inspecting Unreal Engine materials via Monolith MCP. Covers PBR setup, graph building, material instances, templates, HLSL nodes, validation, and previews. Triggers on material, shader, PBR, texture, material instance, material graph.
---

# Unreal Material Workflows

You have access to **Monolith** with 46 material actions via `material.query()`.

## Discovery

```
monolith.discover({ namespace: "material" })
```

## Asset Path Conventions

All asset paths follow UE content browser format (no .uasset extension):

| Location | Path Format | Example |
|----------|------------|---------|
| Project Content/ | `/Game/Path/To/Asset` | `/Game/Materials/M_Rock` |
| Project Plugins/ | `/PluginName/Path/To/Asset` | `/CarnageFX/Materials/M_Blood` |
| Engine Plugins | `/PluginName/Path/To/Asset` | `/Niagara/DefaultAssets/SystemAssets/NS_Default` |

**Note:** For project plugins, the path starts with the plugin name as configured in the .uplugin file's "MountPoint" — which defaults to `/<PluginName>/`. Most plugins mount their Content/ folder there directly.

## Key Actions

| Action | Purpose |
|--------|---------|
| `get_all_expressions` | List all expression nodes in a material |
| `get_expression_details` | Inspect a specific node's properties and pins |
| `get_full_connection_graph` | Complete node/wire topology |
| `build_material_graph` | Create an entire graph from a JSON spec (fastest path) |
| `disconnect_expression` | Remove connections from a node |
| `create_custom_hlsl_node` | Add a Custom HLSL expression |
| `begin_transaction` / `end_transaction` | Wrap edits in undo groups |
| `export_material_graph` / `import_material_graph` | Serialize/deserialize graphs as JSON |
| `validate_material` | Check for broken connections, unused nodes, errors |
| `render_preview` | Trigger material compilation and preview |
| `get_thumbnail` | Get material thumbnail image |
| `get_layer_info` | Inspect material layer/blend stack |

## PBR Material Workflow

### 1. Create a material and build the full graph in one call
```
material.query({ action: "build_material_graph", params: {
  asset: "/Game/Materials/M_Rock",  // or "/MyPlugin/Materials/M_Rock" for plugin assets
  create_if_missing: true,
  nodes: [
    { type: "TextureSample", name: "BaseColor", params: { Texture: "/Game/Textures/T_Rock_D" } },
    { type: "TextureSample", name: "Normal", params: { Texture: "/Game/Textures/T_Rock_N", SamplerType: "Normal" } },
    { type: "TextureSample", name: "ORM", params: { Texture: "/Game/Textures/T_Rock_ORM" } }
  ],
  connections: [
    { from: "BaseColor.RGB", to: "Material.BaseColor" },
    { from: "Normal.RGB", to: "Material.Normal" },
    { from: "ORM.R", to: "Material.AmbientOcclusion" },
    { from: "ORM.G", to: "Material.Roughness" },
    { from: "ORM.B", to: "Material.Metallic" }
  ]
}})
```

### 2. Validate after changes
```
material.query({ action: "validate_material", params: { asset: "/Game/Materials/M_Rock" } })
```

## Editing Existing Materials

Always inspect before modifying:
```
material.query({ action: "get_all_expressions", params: { asset: "/Game/Materials/M_Skin" } })
material.query({ action: "get_full_connection_graph", params: { asset: "/Game/Materials/M_Skin" } })
```

Wrap modifications in transactions for undo support:
```
material.query({ action: "begin_transaction", params: { asset: "/Game/Materials/M_Skin", description: "Add emissive" } })
// ... make changes ...
material.query({ action: "end_transaction", params: { asset: "/Game/Materials/M_Skin" } })
```

## Rules

- **Graph editing only works on base Materials**, not MaterialInstanceConstants
- Always call `validate_material` after graph changes
- `build_material_graph` is the fastest way to create complex graphs — single JSON spec for all nodes + wires
- Use `export_material_graph` to snapshot a graph before making destructive changes
- Asset paths follow the conventions in the Asset Path Conventions section above
