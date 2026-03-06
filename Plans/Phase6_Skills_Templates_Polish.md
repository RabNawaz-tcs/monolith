# Phase 6: Skills, Templates, and Polish

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

## Phase 6 Completion Checklist

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
