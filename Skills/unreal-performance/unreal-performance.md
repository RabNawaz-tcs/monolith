---
name: unreal-performance
description: Use when analyzing or optimizing Unreal Engine performance via Monolith MCP — config auditing, material shader stats, Niagara scalability, draw call analysis, INI tuning. Triggers on performance, optimization, FPS, frame time, GPU, draw calls, shader complexity.
---

# Unreal Performance Analysis Workflows

You have access to **Monolith** with cross-domain performance tools.

## Key Tools by Domain

### Config Auditing (`config.query`)
```
monolith.discover({ namespace: "config" })
```

| Action | Purpose |
|--------|---------|
| `resolve_setting` | Get the effective value of any CVar/config setting |
| `explain_setting` | Understand what a setting does before changing it |
| `diff_from_default` | See all project customizations vs engine defaults |
| `search_config` | Find settings by keyword |

### Material Performance (`material.query`)
| Action | Purpose |
|--------|---------|
| `validate_material` | Check for errors, unused nodes, broken connections |
| `get_all_expressions` | Count instruction/texture samples per material |
| `render_preview` | Trigger compilation to get shader stats |

### Niagara Scalability (`niagara.query`)
| Action | Purpose |
|--------|---------|
| `audit_scalability` | Check LOD, distance culling, budget settings |
| `audit_pooling` | Verify effect pooling configuration |
| `preview_particle_count` | Estimate particle counts at runtime |
| `get_stats` | Emitter/module/renderer counts |

## Common Workflows

### Audit INI performance settings
```
config.query({ action: "diff_from_default", params: { file: "DefaultEngine" } })
config.query({ action: "resolve_setting", params: { file: "DefaultEngine", section: "/Script/Engine.RendererSettings", key: "r.Lumen.TraceMeshSDFs" } })
config.query({ action: "explain_setting", params: { setting: "r.Lumen.TraceMeshSDFs" } })
```

### Check material shader complexity
```
material.query({ action: "get_all_expressions", params: { asset: "/Game/Materials/M_Character" } })
material.query({ action: "validate_material", params: { asset: "/Game/Materials/M_Character" } })
```

### Audit Niagara effect scalability
```
niagara.query({ action: "audit_scalability", params: { system: "/Game/VFX/NS_Blood" } })
niagara.query({ action: "audit_pooling", params: { system: "/Game/VFX/NS_Blood" } })
```

### Find expensive config settings
```
config.query({ action: "search_config", params: { query: "Lumen", file: "DefaultEngine" } })
config.query({ action: "search_config", params: { query: "Shadow", file: "DefaultEngine" } })
config.query({ action: "search_config", params: { query: "TSR", file: "DefaultEngine" } })
```

## High-Impact INI Settings

These are the most impactful performance CVars to audit:

| Setting | Impact | Notes |
|---------|--------|-------|
| `r.Lumen.TraceMeshSDFs` | ~1-2ms GPU | Set to 0 if not using mesh SDF tracing |
| `r.Shadow.Virtual.SMRT.RayCountDirectional` | ~0.5ms GPU | 8 is default, 4 is often sufficient |
| `gc.IncrementalBeginDestroyEnabled` | Frame spikes | Enable to eliminate GC hitches |
| `r.StochasticInterpolation` | ~0.5ms GPU | Set to 2 for better perf |
| `r.AntiAliasingMethod` | Varies | TSR handles aliasing — MSAA often redundant |
| `r.Lumen.Reflections.AsyncCompute` | White flash | UE-354891 bug, keep at 0 until 5.7.2 |

## Tips

- Use `explain_setting` before changing any unfamiliar CVar
- `diff_from_default` is the fastest way to see all project customizations
- Material instruction counts from `get_all_expressions` correlate with pixel shader cost
- Niagara `audit_scalability` flags common mistakes like missing distance culling
