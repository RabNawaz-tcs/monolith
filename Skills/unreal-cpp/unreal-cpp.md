---
name: unreal-cpp
description: Use when writing or debugging Unreal Engine C++ code via Monolith MCP — engine API lookup, signature verification, include paths, source reading, class hierarchies, config resolution. Triggers on C++, header, include, UCLASS, UFUNCTION, UPROPERTY, Build.cs, linker error.
---

# Unreal C++ Development Workflows

You have access to **Monolith** with engine source intelligence via `source.query()` and config resolution via `config.query()`.

## Discovery

```
monolith.discover({ namespace: "source" })
```

## Source Actions

| Action | Purpose |
|--------|---------|
| `search` | Find symbols (classes, functions, structs) across engine source |
| `read_source` | Read actual engine source code for a symbol |
| `get_class_hierarchy` | Class inheritance tree |
| `get_include_path` | Correct `#include` path for a symbol |
| `get_function_signature` | Full signature with params, return type, specifiers |
| `find_callers` | Who calls this function in engine code |
| `find_callees` | What does this function call |
| `find_references` | All references to a symbol |
| `get_module_info` | Module dependencies, build type |
| `get_deprecation_warnings` | Check if APIs are deprecated |

## Common Workflows

### Verify an API before using it
```
source.query({ action: "get_function_signature", params: { symbol: "UGameplayStatics::ApplyDamage" } })
source.query({ action: "get_include_path", params: { symbol: "UGameplayStatics" } })
```

### Understand how Epic uses an API
```
source.query({ action: "find_callers", params: { symbol: "UPrimitiveComponent::SetCollisionEnabled" } })
```

### Explore a class hierarchy
```
source.query({ action: "get_class_hierarchy", params: { symbol: "ACharacter" } })
```

### Read engine implementation details
```
source.query({ action: "read_source", params: { symbol: "UCharacterMovementComponent::PhysWalking" } })
```

### Check for deprecation
```
source.query({ action: "get_deprecation_warnings", params: { symbol: "UWorld::SpawnActor" } })
```

### Resolve config/CVar values
```
config.query({ action: "resolve_setting", params: { file: "DefaultEngine", section: "/Script/Engine.RendererSettings", key: "r.Lumen.TraceMeshSDFs" } })
config.query({ action: "explain_setting", params: { setting: "r.DefaultFeature.AntiAliasing" } })
```

## Build.cs Gotchas

Common linker errors and their fixes:

| Error | Fix |
|-------|-----|
| `LNK2019` unresolved external for `UDeveloperSettings` | Add `"DeveloperSettings"` to Build.cs — it's a separate module from `Engine` |
| `LNK2019` for any UE type | Check module with `source.query("get_module_info", ...)` and add to Build.cs |
| Missing `#include` | Use `source.query("get_include_path", ...)` — never guess include paths |
| Template instantiation errors | Check if the type needs explicit export (`_API` macro) |

## UE 5.7 API Notes

- `FSkinWeightInfo` uses `uint16` for `InfluenceWeights` (not `uint8`) and `FBoneIndexType` for bones
- `CreatePackage` with same path returns existing in-memory package — use unique names
- Live Coding only handles `.cpp` body changes — header changes require editor restart + full UBT build

## Tips

- **Never guess** `#include` paths or function signatures — always verify with `source.query`
- The source index covers engine Runtime, Editor, Developer modules + plugins + shaders (1M+ symbols)
- Use `find_callers` to learn idiomatic usage patterns from Epic's own code
- Combine `source.query` (engine) with project-level search for full picture
- Use `config.query("explain_setting")` before changing any unfamiliar CVar
