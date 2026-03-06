---
name: unreal-animation
description: Use when inspecting or editing Unreal animation assets via Monolith MCP — sequences, montages, blend spaces, animation blueprints, notifies, curves, sync markers, skeletons. Triggers on animation, montage, ABP, blend space, notify, anim sequence, skeleton.
---

# Unreal Animation Workflows

You have access to **Monolith** with animation inspection and editing via `animation.query()`.

## Discovery

```
monolith.discover({ namespace: "animation" })
```

## Asset Path Conventions

All asset paths follow UE content browser format (no .uasset extension):

| Location | Path Format | Example |
|----------|------------|---------|
| Project Content/ | `/Game/Path/To/Asset` | `/Game/Materials/M_Rock` |
| Project Plugins/ | `/PluginName/Path/To/Asset` | `/CarnageFX/Materials/M_Blood` |
| Engine Plugins | `/PluginName/Path/To/Asset` | `/Niagara/DefaultAssets/SystemAssets/NS_Default` |

**Note:** For project plugins, the path starts with the plugin name as configured in the .uplugin file's "MountPoint" — which defaults to `/<PluginName>/`. Most plugins mount their Content/ folder there directly.

## Action Categories

### Montage Editing
| Action | Purpose |
|--------|---------|
| `add_montage_section` | Add a named section to a montage |
| `delete_montage_section` | Remove a section |
| `set_section_next` | Set section playback order |
| `set_section_time` | Move a section to a specific time |

### Blend Space Samples
| Action | Purpose |
|--------|---------|
| `add_blendspace_sample` | Add an animation at X/Y coordinates |
| `edit_blendspace_sample` | Move an existing sample |
| `delete_blendspace_sample` | Remove a sample point |

### Animation Blueprint (ABP) Reading
| Action | Purpose |
|--------|---------|
| `get_state_machines` | List all state machines in an ABP |
| `get_state_info` | Details of a specific state |
| `get_transitions` | Transition rules between states |
| `get_blend_nodes` | Blend node trees |
| `get_linked_layers` | Linked anim layers |
| `get_graphs` | All graphs in the ABP |
| `get_nodes` | Nodes within a specific graph |

### Notify Editing
| Action | Purpose |
|--------|---------|
| `set_notify_time` | Move a notify to a specific time |
| `set_notify_duration` | Set duration of a notify state |

### Bone Tracks
| Action | Purpose |
|--------|---------|
| `set_bone_track_keys` | Set keyframes for a bone track |
| `add_bone_track` | Add a new bone track |
| `remove_bone_track` | Remove a bone track |

### Skeleton
| Action | Purpose |
|--------|---------|
| `add_virtual_bone` | Create a virtual bone between two bones |
| `remove_virtual_bones` | Remove virtual bones |
| `get_skeleton_info` | Bone hierarchy, sockets, virtual bones |
| `get_skeletal_mesh_info` | Mesh details, LODs, materials |

## Common Workflows

### Inspect an ABP's state machines
```
animation.query({ action: "get_state_machines", params: { asset: "/Game/Animations/ABP_Player" } })
animation.query({ action: "get_transitions", params: { asset: "/Game/Animations/ABP_Player", state_machine: "Locomotion" } })
```

### Set up montage section flow (intro -> loop -> outro)
```
animation.query({ action: "add_montage_section", params: { asset: "/Game/Animations/AM_Attack", name: "Intro", time: 0.0 } })
animation.query({ action: "add_montage_section", params: { asset: "/Game/Animations/AM_Attack", name: "Loop", time: 0.5 } })
animation.query({ action: "add_montage_section", params: { asset: "/Game/Animations/AM_Attack", name: "Outro", time: 1.2 } })
animation.query({ action: "set_section_next", params: { asset: "/Game/Animations/AM_Attack", section: "Intro", next: "Loop" } })
animation.query({ action: "set_section_next", params: { asset: "/Game/Animations/AM_Attack", section: "Loop", next: "Outro" } })
```

### Inspect skeleton structure
```
animation.query({ action: "get_skeleton_info", params: { asset: "/Game/Characters/SK_Mannequin" } })
animation.query({ action: "get_skeletal_mesh_info", params: { asset: "/Game/Characters/SKM_Mannequin" } })
```

## Rules

- Editing tools modify assets **live in the editor** — changes are immediate
- Asset paths follow the conventions in the Asset Path Conventions section above
- Use `project.query("search", { query: "AM_*" })` to find animation assets first
- ABP reading is read-only — state machine logic must be edited in the BP editor
