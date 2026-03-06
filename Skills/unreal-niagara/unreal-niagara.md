---
name: unreal-niagara
description: Use when creating, editing, or inspecting Niagara particle systems via Monolith MCP. Covers systems, emitters, modules, parameters, renderers, HLSL generation, scalability, and procedural VFX authoring. Triggers on Niagara, particle, VFX, emitter, particle system.
---

# Unreal Niagara VFX Workflows

You have access to **Monolith** with 70 Niagara actions via `niagara.query()`.

## Discovery

```
monolith.discover({ namespace: "niagara" })
```

## Key Action Groups

### System Management
| Action | Purpose |
|--------|---------|
| `create_system` | Create a new Niagara system |
| `get_system_info` | System overview — emitters, parameters, renderers |
| `get_emitters` | List all emitters in a system |
| `set_system_property` | Modify system-level settings |
| `add_emitter` / `remove_emitter` | Add or remove emitters |
| `reorder_emitters` | Change emitter evaluation order |

### Emitter/Module Editing
| Action | Purpose |
|--------|---------|
| `add_module` / `remove_module` | Add or remove modules from emitter stages |
| `set_module_input` | Set a module input parameter value |
| `set_module_enabled` | Enable/disable a module |
| `reorder_modules` | Change module order within a stage |
| `set_emitter_property` | Modify emitter settings |

### Parameters
| Action | Purpose |
|--------|---------|
| `get_parameters` | All parameters (system, emitter, particle) |
| `get_user_parameters` | User-exposed parameters |
| `add_user_parameter` | Add a user parameter |
| `set_user_parameter_default` | Set default value |
| `trace_parameter_bindings` | Follow parameter binding chain |

### Renderers
| Action | Purpose |
|--------|---------|
| `get_renderers` | List all renderers |
| `add_renderer` / `remove_renderer` | Add or remove renderers |
| `set_renderer_property` | Modify renderer settings |
| `set_renderer_material` | Assign material to renderer |

### HLSL & Advanced
| Action | Purpose |
|--------|---------|
| `generate_module_hlsl` | Generate HLSL for a scratch module |
| `generate_dynamic_input_expression` | Create dynamic input expressions |
| `create_niagara_module` | Create a reusable module asset |

### Scalability & Performance
| Action | Purpose |
|--------|---------|
| `set_scalability` | Configure LOD/distance/budget settings |
| `audit_scalability` | Check scalability configuration |
| `audit_pooling` | Verify pooling settings |
| `preview_particle_count` | Estimate particle counts |

## Common Workflows

### Create a simple particle system
```
niagara.query({ action: "create_system", params: { asset: "/Game/VFX/NS_Sparks" } })
niagara.query({ action: "add_emitter", params: { system: "/Game/VFX/NS_Sparks", template: "FountainEmitter" } })
niagara.query({ action: "set_module_input", params: {
  system: "/Game/VFX/NS_Sparks", emitter: "FountainEmitter",
  module: "Initialize Particle", input: "Lifetime", value: { min: 0.5, max: 1.5 }
}})
```

### Add a custom HLSL module
```
niagara.query({ action: "generate_module_hlsl", params: {
  name: "SpiralMotion",
  inputs: [{ name: "Speed", type: "float", default: 1.0 }, { name: "Radius", type: "float", default: 50.0 }],
  body: "float Angle = Engine.Age * Speed;\nParticles.Position.x += cos(Angle) * Radius;\nParticles.Position.y += sin(Angle) * Radius;"
}})
```

### Set up scalability for performance
```
niagara.query({ action: "set_scalability", params: {
  system: "/Game/VFX/NS_Sparks",
  settings: { max_distance: 5000, budget_scaling: true, spawn_rate_scale_by_distance: true }
}})
```

## Rules

- Use `monolith.discover("niagara")` before guessing action names — there are 70 actions
- Module stages: `Emitter Spawn`, `Emitter Update`, `Particle Spawn`, `Particle Update`, `Render`
- User parameters are the main interface for Blueprint/C++ control of effects
- Always audit scalability (`audit_scalability`) for production VFX
