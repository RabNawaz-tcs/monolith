# Project Monolith — Design Document

**Version:** 1.0
**Date:** 2026-03-06
**Author:** tumourlove
**Repository:** [github.com/tumourlove/monolith](https://github.com/tumourlove/monolith)
**Target:** Unreal Engine 5.7+ | Windows (Mac/Linux planned)

---

## Table of Contents

1. [Executive Summary](#1-executive-summary)
2. [Problem Statement](#2-problem-statement)
3. [Architecture Overview](#3-architecture-overview)
4. [Module Specifications](#4-module-specifications)
5. [Deep Project Indexer — MonolithIndex](#5-deep-project-indexer--monolithindex)
6. [Discovery & Dispatch Protocol](#6-discovery--dispatch-protocol)
7. [Auto-Update System](#7-auto-update-system)
8. [Plugin Settings](#8-plugin-settings)
9. [Skills & Templates](#9-skills--templates)
10. [User Experience](#10-user-experience)
11. [Migration Guide](#11-migration-guide)
12. [Attribution & Licensing](#12-attribution--licensing)
13. [Repository Structure](#13-repository-structure)
14. [Phased Implementation Roadmap](#14-phased-implementation-roadmap)

---

## 1. Executive Summary

Project Monolith consolidates **9 separate MCP servers**, **4 C++ editor plugins**, and **231 individual tool definitions** into a single, self-contained Unreal Engine plugin with an embedded MCP server.

The core thesis is simple: the current ecosystem works, but it doesn't scale. Every new MCP server adds another process to manage, another copy-paste of shared boilerplate, another chunk of AI context consumed by tool listings. Monolith eliminates this fragmentation by moving the entire tooling surface into C++ (for editor-facing operations) and retaining Python only where it genuinely excels (offline source indexing via tree-sitter).

The result is a single `git clone` into `Plugins/Monolith`, a single MCP endpoint, and a **95% reduction in context window consumption** — from 231 tool definitions down to ~14 namespace dispatchers.

Key deliverables:

- **C++ Core** — Embedded Streamable HTTP + SSE MCP server, tool registry, auto-updater, shared utilities
- **8 Domain Modules** — Blueprint, Material, Animation, Niagara, Editor, Config, Index, Source
- **Deep Project Indexer** — SQLite+FTS5 database indexing every asset type in the project
- **Engine Source Intelligence** — Bundled Python process for C++ source parsing and API lookup
- **Discovery/Dispatch Protocol** — Namespace-based tool routing that collapses 231 tools into ~14
- **Auto-Update System** — GitHub Releases integration with one-click editor updates
- **Skills & Templates** — Bundled Claude Code skills and project configuration templates

---

## 2. Problem Statement

### 2.1 Current Ecosystem Inventory

| Component | Type | Tools | Maintainer |
|---|---|---|---|
| unreal-blueprint-mcp | Python MCP Server | 5 | tumourlove |
| unreal-editor-mcp | Python MCP Server | 11 | tumourlove |
| unreal-material-mcp | Python MCP Server | 46 | tumourlove |
| unreal-niagara-mcp | Python MCP Server | 70 | tumourlove |
| unreal-animation-mcp | Python MCP Server | 62 | tumourlove |
| unreal-config-mcp | Python MCP Server | 6 | tumourlove |
| unreal-source-mcp | Python MCP Server | 9 | tumourlove |
| unreal-project-mcp | Python MCP Server | 17 | tumourlove |
| unreal-api-mcp | Python MCP Server | 5 | Codeturion |
| BlueprintReader | C++ Plugin | 5 UFUNCTIONs | tumourlove |
| MaterialMCPReader | C++ Plugin | 13 UFUNCTIONs | tumourlove |
| AnimationMCPReader | C++ Plugin | 23 UFUNCTIONs | tumourlove |
| NiagaraMCPBridge | C++ Plugin | 39 (7 classes) | tumourlove |
| **Total** | | **231** | |

### 2.2 Pain Points

**Copy-Paste Proliferation.** The `EditorBridge` module — a 224-line HTTP client for communicating with the C++ plugins — is copy-pasted verbatim across 6 Python servers. Similarly, `config.py` (server configuration) is duplicated 6 times. The `_call_plugin()` helper pattern appears in 3 servers. Every C++ plugin independently reimplements `ErrorJson` and `SuccessJson` response formatting. This duplication creates a maintenance multiplier: a single bug fix must be applied in up to 6 places.

**The 6-Hop Data Flow.** A typical AI-driven operation follows this path:

```
AI ──► Python MCP Server ──► HTTP Request ──► C++ Plugin (UFUNCTION)
                                                     │
AI ◄── Python MCP Server ◄── HTTP Response ◄─────────┘
```

Six hops for a single operation. The Python layer exists solely to translate MCP protocol into HTTP calls to C++ plugins that do the actual work. This adds latency, failure modes, and debugging complexity with zero functional benefit.

**Context Window Exhaustion.** MCP tool definitions are injected into the AI's context window at conversation start. With 231 tools across 9 servers, the tool listing alone consumes a significant portion of available context — context that should be used for understanding the user's project, not parsing tool signatures. This directly degrades AI reasoning quality.

**Process Management Overhead.** Running 9 separate Python processes means 9 things that can crash, hang, or fail to start. Each has its own port, its own logs, its own lifecycle. Users must configure `.mcp.json` with 9 separate server entries. Debugging "which server is down?" becomes a regular occurrence.

**Architectural Incoherence.** All 4 C++ plugins use the same pattern — `UBlueprintFunctionLibrary` subclass with `BlueprintCallable` static methods returning JSON strings — but share no common infrastructure. Each independently implements serialization, error handling, and response formatting. The 8 Python servers similarly share patterns without sharing code.

### 2.3 Design Goals

| Goal | Metric |
|---|---|
| Single install | 1 git clone, 0 additional setup |
| Minimal context | ~14 namespace tools vs 231 individual tools |
| Zero Python dependency for editor ops | 183 tools run in pure C++ |
| Self-updating | GitHub Releases → one-click update |
| Deep project awareness | Full-project SQLite index with FTS5 |
| Modular internals | Enable/disable individual domain modules |

---

## 3. Architecture Overview

### 3.1 High-Level Architecture

```
┌──────────────────────────────────────────────────────────────────────┐
│                          AI (Claude Code)                            │
│                                                                      │
│  Sees ~14 tools:  monolith.discover  blueprint.query  material.query │
│                   animation.query    niagara.query    editor.query   │
│                   config.query       source.query     project.query  │
│                   monolith.status    monolith.update  monolith.reindex│
└───────────────────────────────┬───────────────────────────────────────┘
                                │ JSON-RPC over Streamable HTTP / SSE
                                ▼
┌──────────────────────────────────────────────────────────────────────┐
│                       MonolithCore (C++)                             │
│                                                                      │
│  ┌─────────────┐  ┌──────────────┐  ┌───────────────┐               │
│  │ HTTP Server  │  │ Tool Registry│  │ Auto-Updater  │               │
│  │ (Port 9316)  │  │ & Dispatcher │  │ (GitHub API)  │               │
│  └──────┬──────┘  └──────┬───────┘  └───────────────┘               │
│         │                │                                           │
│         ▼                ▼                                           │
│  ┌─────────────────────────────────────────────────────────────┐     │
│  │                    Shared Utilities                          │     │
│  │  JSON Helpers · Asset Resolution · Error/Success Formatting │     │
│  └─────────────────────────────────────────────────────────────┘     │
└───────────────────────────────┬───────────────────────────────────────┘
                                │ Direct C++ calls (no HTTP, no Python)
                ┌───────────────┼───────────────┐
                ▼               ▼               ▼
┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐ ┌──────────┐
│Blueprint │ │Material  │ │Animation │ │Niagara   │ │Editor    │
│Module    │ │Module    │ │Module    │ │Module    │ │Module    │
│(5 acts)  │ │(46 acts) │ │(62 acts) │ │(70 acts) │ │(11 acts) │
└──────────┘ └──────────┘ └──────────┘ └──────────┘ └──────────┘

┌──────────┐ ┌──────────────────────────────┐ ┌──────────────────────┐
│Config    │ │MonolithIndex (C++)           │ │MonolithSource        │
│Module    │ │SQLite+FTS5 Project Database  │ │(Bundled Python)      │
│(6 acts)  │ │Plugins/Monolith/Saved/       │ │tree-sitter + SQLite  │
│          │ │  ProjectIndex.db             │ │Saved/EngineSource_   │
└──────────┘ └──────────────────────────────┘ │  {ver}.db            │
                                               └──────────────────────┘
```

### 3.2 The Hybrid Split

The architecture follows a strict principle: **C++ for everything that touches the editor, Python only for offline indexing.**

| Layer | Language | Rationale |
|---|---|---|
| MCP Server | C++ | Eliminates Python process management; direct UE API access |
| Editor Operations (183 tools) | C++ | No intermediary; direct `UEditorEngine`, `UBlueprint`, `UMaterialGraph` access |
| Tool Registry & Dispatch | C++ | Single-process routing; no HTTP overhead |
| Auto-Updater | C++ | `FHttpModule` for GitHub API; editor notifications via Slate |
| Project Indexer | C++ | Direct Asset Registry access; `FAssetData` iteration |
| Source Indexer | Python | tree-sitter C++ parsing requires Python bindings; runs as child process |

The Python component (`MCP/` directory) is bundled within the plugin and spawned as a child process by MonolithSource when source indexing is requested. It has no MCP server of its own — MonolithCore's C++ server handles all MCP protocol communication.

### 3.3 Protocol Support

The embedded HTTP server supports two MCP transport modes:

- **Streamable HTTP** (primary) — Modern MCP transport, bidirectional over HTTP POST/GET with server-sent events for streaming responses
- **Legacy SSE** (fallback) — For older MCP clients that only support the SSE transport

Both transports serve JSON-RPC 2.0 payloads and are available on the same port (default `9316`).

---

## 4. Module Specifications

All modules are **Editor-only** (`Type: Editor` in `.uplugin`). They have no runtime footprint and are stripped from packaged builds.

### 4.1 MonolithCore

**Responsibility:** MCP server, tool registry/dispatch, auto-updater, shared infrastructure, plugin settings.

| Aspect | Detail |
|---|---|
| Tool Count | 4 (discover, status, update, reindex) |
| Key Classes | `FMonolithHttpServer`, `FMonolithToolRegistry`, `UMonolithUpdateSubsystem`, `UMonolithSettings` |
| Dependencies | HTTP, Json, JsonUtilities, EditorSubsystem |

**FMonolithHttpServer** — Embedded HTTP server using `FHttpServerModule`. Listens on configurable port (default 9316). Routes incoming JSON-RPC requests to the tool registry. Handles Streamable HTTP and SSE transport negotiation.

**FMonolithToolRegistry** — Central registry where each domain module registers its actions at plugin load time. Maps `namespace.action` pairs to handler functions. Performs parameter validation and response formatting.

**UMonolithUpdateSubsystem** — `UEditorSubsystem` that initializes on editor startup. Checks GitHub Releases API for newer versions. Manages the staging/swap update lifecycle.

**UMonolithSettings** — `UDeveloperSettings` exposing plugin configuration in Project Settings.

**Shared Utilities:**
- `FMonolithJsonUtils` — Unified `SuccessJson()` / `ErrorJson()` replacing per-plugin implementations
- `FMonolithAssetUtils` — Asset path resolution, package loading, soft object reference helpers
- `FMonolithResponseBuilder` — Structured response construction with pagination support

### 4.2 MonolithBlueprint

**Responsibility:** Blueprint introspection — graph topology, variables, execution flow, node search.

| Aspect | Detail |
|---|---|
| Tool Count | 5 actions via `blueprint.query` |
| Key Classes | `FMonolithBlueprintActions` |
| Replaces | BlueprintReader plugin (5 UFUNCTIONs) + unreal-blueprint-mcp (5 tools) |

**Actions:**

| Action | Description |
|---|---|
| `list_graphs` | List all graphs (EventGraph, functions, macros) in a Blueprint |
| `get_graph_data` | Full graph topology — nodes, pins, connections, positions |
| `get_variables` | All variables with types, defaults, categories, replication flags |
| `get_execution_flow` | Execution wire trace from a given node |
| `search_nodes` | Search nodes by class, name, or comment across one or all Blueprints |

### 4.3 MonolithMaterial

**Responsibility:** Material inspection, editing, graph construction, templates, previews, and validation.

| Aspect | Detail |
|---|---|
| Tool Count | 46 actions via `material.query` |
| Key Classes | `FMonolithMaterialActions`, `FMonolithMaterialGraphBuilder` |
| Replaces | MaterialMCPReader plugin (13 UFUNCTIONs) + unreal-material-mcp (46 tools) |

**Action Categories:**
- **Inspection** — Get material info, list expressions, trace connections, read parameters
- **Editing** — Set parameter values, add/remove/connect expressions, modify blend modes
- **Graph Building** — Programmatic material construction from templates or specifications
- **Templates** — PBR, unlit, translucent, post-process, decal presets
- **Preview** — Thumbnail generation, material instance parameter sweeps
- **Validation** — Instruction count, texture streaming, platform compatibility checks

### 4.4 MonolithAnimation

**Responsibility:** Animation sequences, montages, blend spaces, Animation Blueprints, skeletons, and editing.

| Aspect | Detail |
|---|---|
| Tool Count | 62 actions via `animation.query` |
| Key Classes | `FMonolithAnimationActions`, `FMonolithSequenceEditor`, `FMonolithMontageBuilder` |
| Replaces | AnimationMCPReader plugin (23 UFUNCTIONs) + unreal-animation-mcp (62 tools) |

**Action Categories:**
- **Sequences** — List, inspect, create, modify animation sequences; curve/notify editing
- **Montages** — Section layout, slot assignment, blend in/out, composite construction
- **Blend Spaces** — 1D/2D blend space creation, sample point management, axis configuration
- **Animation Blueprints** — State machine inspection, transition rule reading, graph node listing
- **Skeletons** — Bone hierarchy, socket listing, retarget source management
- **Editing** — Batch operations, notify management, curve manipulation, rate scaling

### 4.5 MonolithNiagara

**Responsibility:** Niagara particle systems, emitters, modules, parameters, renderers, batch operations, HLSL.

| Aspect | Detail |
|---|---|
| Tool Count | 70 actions via `niagara.query` |
| Key Classes | `FMonolithNiagaraActions`, `FMonolithNiagaraSystemBuilder` (+ 5 helper classes) |
| Replaces | NiagaraMCPBridge plugin (39 UFUNCTIONs across 7 classes) + unreal-niagara-mcp (70 tools) |

**Action Categories:**
- **Systems** — Create, inspect, modify Niagara systems; emitter ordering, bounds
- **Emitters** — Add/remove/configure emitters; inheritance, versioning
- **Modules** — Module stack inspection, parameter bindings, scratch pad modules
- **Parameters** — System/emitter/particle parameter CRUD; namespace management
- **Renderers** — Sprite, mesh, ribbon, light renderer configuration
- **Batch** — Multi-system operations, template instantiation, parameter sweeps
- **HLSL** — Custom HLSL module creation, code injection, compilation validation

### 4.6 MonolithEditor

**Responsibility:** Build system integration, error/warning parsing, log monitoring, crash context.

| Aspect | Detail |
|---|---|
| Tool Count | 11 actions via `editor.query` |
| Key Classes | `FMonolithEditorActions` |
| Replaces | unreal-editor-mcp (11 tools) |

**Actions include:** trigger builds, get build errors/warnings, tail editor log, get crash context, list loaded plugins, get editor preferences, get project settings summary, map check, content validation, asset audit, commandlet execution.

### 4.7 MonolithConfig

**Responsibility:** Unreal Engine configuration file (.ini) inspection and search.

| Aspect | Detail |
|---|---|
| Tool Count | 6 actions via `config.query` |
| Key Classes | `FMonolithConfigActions` |
| Replaces | unreal-config-mcp (6 tools) |

**Actions:**

| Action | Description |
|---|---|
| `resolve` | Resolve the effective value of a config key across the config hierarchy |
| `explain` | Explain where a config value comes from (Base → Default → User → CLI) |
| `diff` | Diff two config layers to show overrides |
| `search` | Full-text search across all config files |
| `get_section` | Read an entire config section |
| `get_files` | List all config files with their hierarchy level |

### 4.8 MonolithIndex

See [Section 5](#5-deep-project-indexer--monolithindex) for full specification.

### 4.9 MonolithSource

**Responsibility:** Unreal Engine C++ source code intelligence — symbol lookup, call graphs, API search, deprecation warnings.

| Aspect | Detail |
|---|---|
| Tool Count | ~14 actions via `source.query` |
| Key Classes | `FMonolithSourceActions`, `FMonolithSourceProcess` |
| Replaces | unreal-source-mcp (9 tools) + unreal-api-mcp (5 tools, reimplemented) |

**FMonolithSourceProcess** — Manages the bundled Python child process (`MCP/` directory). Starts on first `source.query` call, communicates via stdin/stdout JSON protocol. The Python process performs tree-sitter C++ parsing and maintains the `Saved/EngineSource_{ver}.db` SQLite database.

**Actions include:** search symbols, get symbol context, read source file, find callers, find callees, find references, get class hierarchy, get module info, lookup function signature, resolve #include path, check deprecation status, search API by pattern, get API documentation summary.

**Note on unreal-api-mcp:** The functionality from Codeturion's unreal-api-mcp (signature lookup, deprecation warnings, #include resolution) is **reimplemented from scratch** under Monolith's MIT license. The original is under PolyForm Noncommercial and is not bundled or linked. Concept inspiration is credited in `ATTRIBUTION.md`.

---

## 5. Deep Project Indexer — MonolithIndex

MonolithIndex is the project's deep awareness engine. It creates a comprehensive, searchable index of every meaningful asset and code artifact in the Unreal project.

### 5.1 Overview

| Aspect | Detail |
|---|---|
| Tool Count | 5 actions via `project.query` |
| Database | SQLite + FTS5 at `Plugins/Monolith/Saved/ProjectIndex.db` |
| Index Trigger | First editor launch (automatic), or manual via Settings re-index button |
| Key Classes | `FMonolithIndexer`, `FMonolithIndexDatabase`, `UMonolithIndexSubsystem` |

### 5.2 What Gets Indexed

MonolithIndex indexes **deep** — not just asset names, but internal structure:

| Asset Type | What's Indexed |
|---|---|
| **C++ Source** | Classes, functions, properties, includes, module membership |
| **Blueprints** | Full graph topology — nodes, connections, variables, event graphs, functions |
| **Materials** | Expression trees, connections, parameter names, blend modes, domains |
| **Animation Sequences** | Curves, notifies, length, skeleton reference, rate scale |
| **Animation Montages** | Sections, slots, composite sequences, blend settings |
| **Animation Blueprints** | State machines, states, transitions, graph nodes |
| **Blend Spaces** | Axes, sample points, dimensions, interpolation |
| **Niagara Systems** | Emitter list, bounds, fixed tick, system parameters |
| **Niagara Emitters** | Module stacks, renderers, parameter bindings |
| **Data Tables** | Row struct schema, all row entries with field values |
| **Levels** | Actor list, component hierarchy, transform data |
| **Static Meshes** | LOD count, triangle count, material slots, bounds |
| **Skeletal Meshes** | Bone hierarchy, material slots, morph targets, physics assets |
| **Textures** | Resolution, format, compression, SRGB, LOD group |
| **Sound Assets** | Duration, channels, sample rate, compression, attenuation |
| **Gameplay Tags** | Full tag hierarchy, registered sources, referencing assets |
| **Asset Dependencies** | Complete reference graph (hard + soft references) |
| **Config/INI** | All config sections and keys across hierarchy |
| **Plugins** | Enabled/disabled, modules, dependencies, version |

### 5.3 SQLite Schema Design

```sql
-- Core asset table
CREATE TABLE assets (
    id          INTEGER PRIMARY KEY,
    path        TEXT NOT NULL UNIQUE,       -- /Game/Characters/Hero/BP_Hero
    name        TEXT NOT NULL,              -- BP_Hero
    class       TEXT NOT NULL,              -- Blueprint, Material, NiagaraSystem...
    module      TEXT,                       -- Source module or plugin
    disk_path   TEXT,                       -- Absolute path on disk
    last_modified INTEGER,                  -- Timestamp for incremental updates
    metadata    TEXT                        -- JSON blob for type-specific data
);

-- Full-text search index
CREATE VIRTUAL TABLE assets_fts USING fts5(
    name, path, class, content,
    content='assets',
    content_rowid='id'
);

-- Blueprint internals
CREATE TABLE blueprint_nodes (
    id          INTEGER PRIMARY KEY,
    asset_id    INTEGER REFERENCES assets(id),
    graph_name  TEXT NOT NULL,
    node_class  TEXT NOT NULL,
    node_name   TEXT,
    comment     TEXT,
    position_x  REAL,
    position_y  REAL
);

CREATE TABLE blueprint_connections (
    id          INTEGER PRIMARY KEY,
    asset_id    INTEGER REFERENCES assets(id),
    source_node INTEGER REFERENCES blueprint_nodes(id),
    source_pin  TEXT,
    target_node INTEGER REFERENCES blueprint_nodes(id),
    target_pin  TEXT
);

CREATE TABLE blueprint_variables (
    id          INTEGER PRIMARY KEY,
    asset_id    INTEGER REFERENCES assets(id),
    name        TEXT NOT NULL,
    type        TEXT NOT NULL,
    default_val TEXT,
    category    TEXT,
    is_replicated INTEGER DEFAULT 0
);

-- Material internals
CREATE TABLE material_expressions (
    id          INTEGER PRIMARY KEY,
    asset_id    INTEGER REFERENCES assets(id),
    class       TEXT NOT NULL,              -- MaterialExpressionTextureSample...
    name        TEXT,
    parameter   TEXT,                       -- Parameter name if applicable
    position_x  REAL,
    position_y  REAL
);

CREATE TABLE material_connections (
    id              INTEGER PRIMARY KEY,
    asset_id        INTEGER REFERENCES assets(id),
    source_expr     INTEGER REFERENCES material_expressions(id),
    source_output   TEXT,
    target_expr     INTEGER REFERENCES material_expressions(id),
    target_input    TEXT
);

-- Animation internals
CREATE TABLE animation_data (
    id          INTEGER PRIMARY KEY,
    asset_id    INTEGER REFERENCES assets(id),
    type        TEXT NOT NULL,              -- sequence, montage, blend_space, abp
    skeleton    TEXT,
    duration    REAL,
    num_curves  INTEGER,
    num_notifies INTEGER,
    details     TEXT                        -- JSON blob for type-specific fields
);

-- Niagara internals
CREATE TABLE niagara_data (
    id          INTEGER PRIMARY KEY,
    asset_id    INTEGER REFERENCES assets(id),
    type        TEXT NOT NULL,              -- system, emitter
    emitter_count INTEGER,
    module_list TEXT,                       -- JSON array of module names
    parameters  TEXT                        -- JSON object of parameter definitions
);

-- Asset dependency graph
CREATE TABLE dependencies (
    id          INTEGER PRIMARY KEY,
    source_id   INTEGER REFERENCES assets(id),
    target_id   INTEGER REFERENCES assets(id),
    type        TEXT NOT NULL DEFAULT 'hard' -- hard, soft
);

-- Gameplay tags
CREATE TABLE gameplay_tags (
    id          INTEGER PRIMARY KEY,
    tag         TEXT NOT NULL UNIQUE,
    parent_tag  TEXT,
    source      TEXT                        -- Where registered (ini, native, DataTable)
);

CREATE TABLE gameplay_tag_refs (
    id          INTEGER PRIMARY KEY,
    tag_id      INTEGER REFERENCES gameplay_tags(id),
    asset_id    INTEGER REFERENCES assets(id),
    context     TEXT                        -- variable default, node pin, config...
);

-- C++ symbols (project source only — engine source uses MonolithSource)
CREATE TABLE cpp_symbols (
    id          INTEGER PRIMARY KEY,
    name        TEXT NOT NULL,
    qualified   TEXT,                       -- Full qualified name
    kind        TEXT NOT NULL,              -- class, function, property, enum, macro
    file_path   TEXT,
    line_number INTEGER,
    signature   TEXT,
    parent_id   INTEGER REFERENCES cpp_symbols(id)
);

CREATE VIRTUAL TABLE cpp_symbols_fts USING fts5(
    name, qualified, signature,
    content='cpp_symbols',
    content_rowid='id'
);

-- Config entries
CREATE TABLE config_entries (
    id          INTEGER PRIMARY KEY,
    file        TEXT NOT NULL,
    section     TEXT NOT NULL,
    key         TEXT NOT NULL,
    value       TEXT,
    hierarchy   TEXT                        -- Base, Default, User, CommandLine
);

-- Indexes for common queries
CREATE INDEX idx_assets_class ON assets(class);
CREATE INDEX idx_assets_name ON assets(name);
CREATE INDEX idx_deps_source ON dependencies(source_id);
CREATE INDEX idx_deps_target ON dependencies(target_id);
CREATE INDEX idx_bp_nodes_asset ON blueprint_nodes(asset_id);
CREATE INDEX idx_mat_expr_asset ON material_expressions(asset_id);
CREATE INDEX idx_anim_asset ON animation_data(asset_id);
CREATE INDEX idx_niagara_asset ON niagara_data(asset_id);
CREATE INDEX idx_tag_refs_asset ON gameplay_tag_refs(asset_id);
CREATE INDEX idx_cpp_kind ON cpp_symbols(kind);
```

### 5.4 Exposed Actions

| Action | Description |
|---|---|
| `project.search` | Full-text search across all indexed assets and symbols |
| `project.find_references` | Find all assets referencing a given asset (uses dependency graph) |
| `project.find_by_type` | List assets filtered by class, with optional name/path pattern |
| `project.get_stats` | Project-wide statistics — asset counts by type, total nodes, dependency depth |
| `project.get_asset_details` | Deep detail for a single asset — all indexed internals |

### 5.5 Incremental Updates

The initial implementation uses full re-indexing. Future versions will leverage `FAssetRegistryModule` delegates (`OnAssetAdded`, `OnAssetRemoved`, `OnAssetRenamed`, `OnAssetUpdated`) to perform incremental index updates as assets change during the editing session.

---

## 6. Discovery & Dispatch Protocol

### 6.1 Design Rationale

Traditional MCP servers expose every tool as a top-level definition. With 231 tools, this means 231 JSON schema objects injected into the AI's context window — roughly 30-40K tokens of tool definitions alone.

Monolith's discovery/dispatch pattern replaces this with **namespace dispatchers**: one tool per domain that accepts an `action` parameter. The AI discovers available actions on-demand using `monolith.discover()`, and dispatches operations via `{namespace}.query(action, params)`.

### 6.2 Tool Surface

The complete MCP tool listing exposed to AI clients:

```json
{
  "tools": [
    {
      "name": "monolith_discover",
      "description": "List available actions, optionally filtered by namespace",
      "inputSchema": {
        "type": "object",
        "properties": {
          "namespace": {
            "type": "string",
            "description": "Filter to a specific namespace (blueprint, material, animation, niagara, editor, config, source, project). Omit to list all."
          }
        }
      }
    },
    {
      "name": "monolith_status",
      "description": "Get server health, index status, version, and enabled modules"
    },
    {
      "name": "monolith_update",
      "description": "Check for or install updates",
      "inputSchema": {
        "type": "object",
        "properties": {
          "action": {
            "type": "string",
            "enum": ["check", "install"]
          }
        }
      }
    },
    {
      "name": "monolith_reindex",
      "description": "Trigger a full project re-index"
    },
    {
      "name": "blueprint_query",
      "description": "Blueprint introspection — graphs, variables, execution flow, node search",
      "inputSchema": {
        "type": "object",
        "properties": {
          "action": { "type": "string" },
          "params": { "type": "object" }
        },
        "required": ["action"]
      }
    },
    {
      "name": "material_query",
      "description": "Material inspection, editing, graph building, templates, validation",
      "inputSchema": { "...same pattern..." }
    },
    {
      "name": "animation_query",
      "description": "Animation sequences, montages, blend spaces, ABPs, skeletons",
      "inputSchema": { "...same pattern..." }
    },
    {
      "name": "niagara_query",
      "description": "Niagara systems, emitters, modules, parameters, renderers, HLSL",
      "inputSchema": { "...same pattern..." }
    },
    {
      "name": "editor_query",
      "description": "Build system, errors, logs, crash context, editor state",
      "inputSchema": { "...same pattern..." }
    },
    {
      "name": "config_query",
      "description": "Config (.ini) resolution, diffing, search, hierarchy inspection",
      "inputSchema": { "...same pattern..." }
    },
    {
      "name": "source_query",
      "description": "Engine source search, symbols, call graphs, API lookup, deprecation checks",
      "inputSchema": { "...same pattern..." }
    },
    {
      "name": "project_query",
      "description": "Deep project search, references, asset details, statistics",
      "inputSchema": { "...same pattern..." }
    }
  ]
}
```

**Result: ~14 tool definitions instead of 231 — approximately 95% context reduction.**

### 6.3 JSON-RPC Flow

**Discovery request:**

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "method": "tools/call",
  "params": {
    "name": "monolith_discover",
    "arguments": { "namespace": "blueprint" }
  }
}
```

**Discovery response:**

```json
{
  "jsonrpc": "2.0",
  "id": 1,
  "result": {
    "content": [{
      "type": "text",
      "text": {
        "namespace": "blueprint",
        "actions": [
          {
            "name": "list_graphs",
            "description": "List all graphs in a Blueprint",
            "params": { "asset_path": "string (required)" }
          },
          {
            "name": "get_graph_data",
            "description": "Full graph topology — nodes, pins, connections",
            "params": {
              "asset_path": "string (required)",
              "graph_name": "string (optional, defaults to EventGraph)"
            }
          },
          {
            "name": "get_variables",
            "description": "All variables with types, defaults, categories",
            "params": { "asset_path": "string (required)" }
          },
          {
            "name": "get_execution_flow",
            "description": "Trace execution wires from a node",
            "params": {
              "asset_path": "string (required)",
              "node_name": "string (required)"
            }
          },
          {
            "name": "search_nodes",
            "description": "Search nodes by class, name, or comment",
            "params": {
              "query": "string (required)",
              "asset_path": "string (optional, search all if omitted)"
            }
          }
        ]
      }
    }]
  }
}
```

**Action dispatch request:**

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "method": "tools/call",
  "params": {
    "name": "blueprint_query",
    "arguments": {
      "action": "get_variables",
      "params": {
        "asset_path": "/Game/Characters/Hero/BP_Hero"
      }
    }
  }
}
```

**Action dispatch response:**

```json
{
  "jsonrpc": "2.0",
  "id": 2,
  "result": {
    "content": [{
      "type": "text",
      "text": {
        "success": true,
        "asset": "/Game/Characters/Hero/BP_Hero",
        "variables": [
          {
            "name": "Health",
            "type": "Float",
            "default": "100.0",
            "category": "Stats",
            "is_replicated": true
          },
          {
            "name": "MovementSpeed",
            "type": "Float",
            "default": "600.0",
            "category": "Movement",
            "is_replicated": false
          }
        ]
      }
    }]
  }
}
```

### 6.4 Error Handling

All errors follow a unified format from `FMonolithJsonUtils`:

```json
{
  "success": false,
  "error": "Asset not found: /Game/Characters/Hero/BP_Hero",
  "code": "ASSET_NOT_FOUND"
}
```

Error codes are standardized across all modules:
- `ASSET_NOT_FOUND` — Referenced asset doesn't exist
- `INVALID_ACTION` — Unknown action for the namespace
- `INVALID_PARAMS` — Missing or malformed parameters
- `MODULE_DISABLED` — The requested module is disabled in settings
- `INDEX_NOT_READY` — Project index hasn't completed initial build
- `INTERNAL_ERROR` — Unexpected failure (includes stack context in dev builds)

---

## 7. Auto-Update System

### 7.1 Components

- **UMonolithUpdateSubsystem** — `UEditorSubsystem` managing the update lifecycle
- **GitHub Releases API** — Source of truth for published versions
- **Semver Comparison** — `tag_name` from releases compared against `MONOLITH_VERSION` macro
- **Staging Directory** — `Plugins/Monolith_Staging/` for downloaded updates
- **Version Tracking** — `Saved/version.json` records current and pending versions

### 7.2 Full Update Flow

```
Editor Launch
     │
     ▼
UMonolithUpdateSubsystem::Initialize()
     │
     ├─► Read Saved/version.json
     │   ├─ If staging/ exists → Swap Phase (see 7.3)
     │   └─ If no staging → Continue to check
     │
     ▼
Check UMonolithSettings::bAutoUpdateEnabled
     │
     ├─ false → Skip, log "Auto-update disabled"
     │
     ▼ true
GET https://api.github.com/repos/tumourlove/monolith/releases/latest
     │
     ▼
Parse tag_name (e.g., "v1.2.0"), compare to MONOLITH_VERSION
     │
     ├─ Current ≥ Remote → Log "Up to date", done
     │
     ▼ Remote is newer
Show editor notification:
  "Monolith v1.2.0 available (current: v1.1.0) — [Update]"
     │
     ▼ User clicks [Update]
Download release .zip asset from GitHub
     │
     ▼
Extract to Plugins/Monolith_Staging/
     │
     ▼
Write pending version to Saved/version.json:
  { "current": "1.1.0", "pending": "1.2.0", "staging": true }
     │
     ▼
Show notification: "Update staged. Restart editor to apply."
```

### 7.3 Swap Phase (Next Editor Launch)

```
Editor Launch
     │
     ▼
UMonolithUpdateSubsystem::Initialize()
     │
     ▼
Detect Saved/version.json has "staging": true
     │
     ▼
Verify Monolith_Staging/ contents (check .uplugin exists)
     │
     ▼
Rename Monolith/ → Monolith_Old/
Rename Monolith_Staging/ → Monolith/
     │
     ▼
Update Saved/version.json:
  { "current": "1.2.0", "staging": false }
     │
     ▼
Delete Monolith_Old/ (deferred to background)
     │
     ▼
Log "Successfully updated to v1.2.0"
```

### 7.4 Safety Measures

- **Rollback** — `Monolith_Old/` is preserved until the updated plugin loads successfully. If the new version fails to load, the user can manually rename `Monolith_Old/` back to `Monolith/`.
- **Checksum Verification** — Release assets include SHA256 checksums; downloads are verified before extraction.
- **No Force Restarts** — The update is staged and applied on next natural editor launch. The editor is never force-restarted.
- **Rate Limiting** — Update checks are performed at most once per editor session, not on every module load.

---

## 8. Plugin Settings

### 8.1 UMonolithSettings (UDeveloperSettings)

Accessible via **Project Settings → Plugins → Monolith**.

| Field | Type | Default | Description |
|---|---|---|---|
| `ServerPort` | `int32` | `9316` | Port for the embedded MCP HTTP server |
| `bAutoUpdateEnabled` | `bool` | `true` | Check GitHub Releases for updates on editor startup |
| `DatabasePathOverride` | `FDirectoryPath` | *(empty)* | Override location for ProjectIndex.db (defaults to `Plugins/Monolith/Saved/`) |
| `LogVerbosity` | `EMonolithLogVerbosity` | `Normal` | Logging level: Quiet, Normal, Verbose, VeryVerbose |
| `bBlueprintModuleEnabled` | `bool` | `true` | Enable/disable Blueprint module |
| `bMaterialModuleEnabled` | `bool` | `true` | Enable/disable Material module |
| `bAnimationModuleEnabled` | `bool` | `true` | Enable/disable Animation module |
| `bNiagaraModuleEnabled` | `bool` | `true` | Enable/disable Niagara module |
| `bEditorModuleEnabled` | `bool` | `true` | Enable/disable Editor module |
| `bConfigModuleEnabled` | `bool` | `true` | Enable/disable Config module |
| `bSourceModuleEnabled` | `bool` | `true` | Enable/disable Source module |
| `bIndexModuleEnabled` | `bool` | `true` | Enable/disable Index module |

**Re-Index Button** — A "Re-Index Project" button in the settings panel triggers `UMonolithIndexSubsystem::RebuildIndex()`. Shows a progress dialog with asset counts and estimated completion.

### 8.2 Configuration File

Settings are persisted to `Config/MonolithSettings.ini` within the plugin directory, keeping project-level config clean. Project-level overrides can be placed in the project's `Config/DefaultMonolith.ini`.

---

## 9. Skills & Templates

### 9.1 Bundled Claude Code Skills

Monolith ships with domain-specific Claude Code skills that teach the AI how to effectively use Monolith's tools for common Unreal workflows:

| Skill Directory | Domain | Purpose |
|---|---|---|
| `Skills/unreal-blueprints/` | Blueprint | Blueprint creation, event wiring, variable setup, debugging flows |
| `Skills/unreal-materials/` | Material | PBR material creation, material instances, optimization, troubleshooting |
| `Skills/unreal-animation/` | Animation | Montage construction, ABP state machines, blend space setup |
| `Skills/unreal-niagara/` | Niagara | Particle system design, HLSL modules, performance optimization |
| `Skills/unreal-debugging/` | Editor | Build error diagnosis, log analysis, crash investigation |
| `Skills/unreal-performance/` | Performance | Draw call profiling, Lumen/VSM tuning, LOD optimization |
| `Skills/unreal-project-search/` | Index | Deep project search patterns, dependency analysis, asset auditing |
| `Skills/unreal-cpp/` | Source | UE C++ patterns, API lookup, source tracing, header navigation |

### 9.2 Templates

| Template | Purpose |
|---|---|
| `Templates/CLAUDE.md.example` | Example `CLAUDE.md` with Monolith-specific instructions — teaches the AI about available namespaces, common workflows, and project conventions |
| `Templates/.mcp.json.example` | MCP server configuration snippet for the project root |

### 9.3 Installation

Skills are installed by symlinking or copying the `Skills/` subdirectories into the user's Claude Code skills directory. The `CLAUDE.md.example` template can be copied to the project root and customized.

The `.mcp.json.example` provides the minimal configuration:

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

---

## 10. User Experience

### 10.1 Install Flow

```
1. git clone https://github.com/tumourlove/monolith.git
   → into YourProject/Plugins/Monolith/

2. Copy .mcp.json snippet into project root
   → cp Plugins/Monolith/.mcp.json.example .mcp.json

3. (Optional) Copy CLAUDE.md template
   → cp Plugins/Monolith/Templates/CLAUDE.md.example CLAUDE.md

4. Launch Unreal Editor
```

That's it. No Python environment setup, no pip install, no port configuration, no multi-server `.mcp.json`. One clone, one config file, editor launch.

### 10.2 First Launch

On the first editor launch after installation:

1. **MonolithCore** starts the embedded HTTP server on port 9316
2. **MonolithIndex** detects no `ProjectIndex.db` and begins full project indexing
   - Progress notification appears in the editor: "Monolith: Indexing project... (1,247 / 3,891 assets)"
   - Indexing runs on a background thread; the editor remains fully responsive
   - On completion: "Monolith: Project indexed — 3,891 assets, 47,203 nodes, 12,847 connections"
3. **MonolithSource** is dormant until first `source.query` call (lazy initialization)
4. **UMonolithUpdateSubsystem** checks GitHub Releases — on first install, confirms "Up to date"
5. All domain modules register their actions with the tool registry

### 10.3 Daily Use

The AI (Claude Code) interacts with Monolith through a natural discovery-first workflow:

```
User: "Show me all the variables in BP_Hero"

AI thinking: I need blueprint introspection. Let me check what's available.

AI calls: monolith_discover(namespace="blueprint")
  → Sees: list_graphs, get_graph_data, get_variables, get_execution_flow, search_nodes

AI calls: blueprint_query(action="get_variables", params={asset_path: "/Game/Characters/Hero/BP_Hero"})
  → Returns: Health (Float, 100.0), MovementSpeed (Float, 600.0), ...

AI: "BP_Hero has 5 variables: Health (Float, default 100.0, replicated), ..."
```

For cross-cutting queries, the project index provides instant answers:

```
User: "What references BP_Hero?"

AI calls: project_query(action="find_references", params={asset_path: "/Game/Characters/Hero/BP_Hero"})
  → Returns: BP_GameMode, BP_SpawnManager, L_MainLevel, DT_Characters, ...
```

### 10.4 Updates

```
[Editor notification bar]
  "Monolith v1.2.0 available (current: v1.1.0) — [Update]"

User clicks [Update]

[Notification]
  "Downloading Monolith v1.2.0... (2.4 MB)"

[Notification]
  "Update staged. Restart editor to complete update."

  --- User restarts editor when convenient ---

[Log]
  "Monolith: Successfully updated to v1.2.0"
```

---

## 11. Migration Guide

### 11.1 From Current 9-Server Setup to Monolith

**Phase 1: Install Monolith alongside existing servers**

1. Clone Monolith into `Plugins/Monolith/`
2. Add the Monolith entry to `.mcp.json` (keep existing servers)
3. Launch editor — both old and new systems coexist
4. Verify Monolith responds: test `monolith_status` in Claude Code

**Phase 2: Verify domain parity**

For each domain, verify Monolith returns equivalent results:

| Domain | Test | Old Server | Monolith Equivalent |
|---|---|---|---|
| Blueprint | List graphs | `list_blueprint_graphs` | `blueprint_query(action="list_graphs")` |
| Material | Get info | `get_material_info` | `material_query(action="get_info")` |
| Animation | List sequences | `list_animation_sequences` | `animation_query(action="list_sequences")` |
| Niagara | Get system | `get_niagara_system` | `niagara_query(action="get_system")` |
| Editor | Build errors | `get_build_errors` | `editor_query(action="get_build_errors")` |
| Config | Resolve | `resolve_config` | `config_query(action="resolve")` |
| Source | Search | `search_source` | `source_query(action="search")` |
| Project | Search | `search_project` | `project_query(action="search")` |

**Phase 3: Remove old infrastructure**

1. Remove old C++ plugins from project:
   - `Plugins/BlueprintReader/`
   - `Plugins/MaterialMCPReader/`
   - `Plugins/AnimationMCPReader/`
   - `Plugins/NiagaraMCPBridge/`

2. Remove old MCP server entries from `.mcp.json` (all 9 entries)

3. Remove old Python server directories (or archive them):
   - `unreal-blueprint-mcp/`
   - `unreal-editor-mcp/`
   - `unreal-material-mcp/`
   - `unreal-niagara-mcp/`
   - `unreal-animation-mcp/`
   - `unreal-config-mcp/`
   - `unreal-source-mcp/`
   - `unreal-project-mcp/`

4. Regenerate project files (right-click `.uproject` → "Generate Visual Studio project files")

**Phase 4: Update project documentation**

1. Replace `CLAUDE.md` with Monolith-aware version from `Templates/CLAUDE.md.example`
2. Install Monolith skills into Claude Code configuration
3. Update any CI/CD scripts that referenced old server processes

### 11.2 Tool Name Mapping

For teams with existing prompts or workflows referencing old tool names:

| Old Tool Name | New Equivalent |
|---|---|
| `list_blueprint_graphs` | `blueprint_query(action="list_graphs")` |
| `get_material_info` | `material_query(action="get_info")` |
| `create_material_from_template` | `material_query(action="create_from_template")` |
| `get_niagara_system_info` | `niagara_query(action="get_system_info")` |
| `list_animation_sequences` | `animation_query(action="list_sequences")` |
| `resolve_config_value` | `config_query(action="resolve")` |
| `search_engine_source` | `source_query(action="search")` |
| `get_build_errors` | `editor_query(action="get_build_errors")` |

The AI will naturally discover the new naming through `monolith_discover()` — explicit mapping is only needed for hardcoded automation scripts.

---

## 12. Attribution & Licensing

### 12.1 License

Monolith is released under the **MIT License**. See `LICENSE` in the repository root.

### 12.2 Attribution

An `ATTRIBUTION.md` file in the repository root credits:

- **Codeturion** — [unreal-api-mcp](https://github.com/Codeturion/unreal-api-mcp) for concept inspiration on API signature lookup, deprecation detection, and #include resolution. Monolith's implementation is original (not derived from Codeturion's PolyForm NC-licensed code), but the feature set was inspired by their work.

### 12.3 Third-Party Dependencies

| Dependency | License | Usage |
|---|---|---|
| SQLite | Public Domain | Project index and source index databases |
| FTS5 | Public Domain | Full-text search extension for SQLite |
| tree-sitter | MIT | C++ source parsing (bundled Python component) |
| tree-sitter-cpp | MIT | C++ grammar for tree-sitter |
| Unreal Engine | EULA | Host engine (plugin target) |

---

## 13. Repository Structure

```
monolith/
├── Monolith.uplugin                    # Plugin descriptor
├── README.md                           # Project overview, quick start
├── LICENSE                             # MIT License
├── ATTRIBUTION.md                      # Third-party credits (Codeturion, etc.)
├── .mcp.json.example                   # MCP config snippet for users
│
├── Config/
│   └── MonolithSettings.ini            # Default plugin settings
│
├── Saved/                              # Runtime data (gitignored except .gitkeep)
│   ├── .gitkeep
│   ├── ProjectIndex.db                 # [generated] Deep project index
│   ├── EngineSource_{ver}.db           # [generated] Engine source index
│   └── version.json                    # [generated] Update version tracking
│
├── Source/
│   ├── MonolithCore/
│   │   ├── MonolithCore.Build.cs
│   │   ├── Public/
│   │   │   ├── MonolithCore.h
│   │   │   ├── FMonolithHttpServer.h
│   │   │   ├── FMonolithToolRegistry.h
│   │   │   ├── UMonolithUpdateSubsystem.h
│   │   │   ├── UMonolithSettings.h
│   │   │   ├── FMonolithJsonUtils.h
│   │   │   ├── FMonolithAssetUtils.h
│   │   │   └── FMonolithResponseBuilder.h
│   │   └── Private/
│   │       ├── MonolithCoreModule.cpp
│   │       ├── FMonolithHttpServer.cpp
│   │       ├── FMonolithToolRegistry.cpp
│   │       ├── UMonolithUpdateSubsystem.cpp
│   │       ├── UMonolithSettings.cpp
│   │       ├── FMonolithJsonUtils.cpp
│   │       ├── FMonolithAssetUtils.cpp
│   │       └── FMonolithResponseBuilder.cpp
│   │
│   ├── MonolithBlueprint/
│   │   ├── MonolithBlueprint.Build.cs
│   │   ├── Public/
│   │   │   └── FMonolithBlueprintActions.h
│   │   └── Private/
│   │       ├── MonolithBlueprintModule.cpp
│   │       └── FMonolithBlueprintActions.cpp
│   │
│   ├── MonolithMaterial/
│   │   ├── MonolithMaterial.Build.cs
│   │   ├── Public/
│   │   │   ├── FMonolithMaterialActions.h
│   │   │   └── FMonolithMaterialGraphBuilder.h
│   │   └── Private/
│   │       ├── MonolithMaterialModule.cpp
│   │       ├── FMonolithMaterialActions.cpp
│   │       └── FMonolithMaterialGraphBuilder.cpp
│   │
│   ├── MonolithAnimation/
│   │   ├── MonolithAnimation.Build.cs
│   │   ├── Public/
│   │   │   ├── FMonolithAnimationActions.h
│   │   │   ├── FMonolithSequenceEditor.h
│   │   │   └── FMonolithMontageBuilder.h
│   │   └── Private/
│   │       ├── MonolithAnimationModule.cpp
│   │       ├── FMonolithAnimationActions.cpp
│   │       ├── FMonolithSequenceEditor.cpp
│   │       └── FMonolithMontageBuilder.cpp
│   │
│   ├── MonolithNiagara/
│   │   ├── MonolithNiagara.Build.cs
│   │   ├── Public/
│   │   │   ├── FMonolithNiagaraActions.h
│   │   │   └── FMonolithNiagaraSystemBuilder.h
│   │   └── Private/
│   │       ├── MonolithNiagaraModule.cpp
│   │       ├── FMonolithNiagaraActions.cpp
│   │       └── FMonolithNiagaraSystemBuilder.cpp
│   │
│   ├── MonolithEditor/
│   │   ├── MonolithEditor.Build.cs
│   │   ├── Public/
│   │   │   └── FMonolithEditorActions.h
│   │   └── Private/
│   │       ├── MonolithEditorModule.cpp
│   │       └── FMonolithEditorActions.cpp
│   │
│   ├── MonolithConfig/
│   │   ├── MonolithConfig.Build.cs
│   │   ├── Public/
│   │   │   └── FMonolithConfigActions.h
│   │   └── Private/
│   │       ├── MonolithConfigModule.cpp
│   │       └── FMonolithConfigActions.cpp
│   │
│   ├── MonolithIndex/
│   │   ├── MonolithIndex.Build.cs
│   │   ├── Public/
│   │   │   ├── FMonolithIndexer.h
│   │   │   ├── FMonolithIndexDatabase.h
│   │   │   └── UMonolithIndexSubsystem.h
│   │   └── Private/
│   │       ├── MonolithIndexModule.cpp
│   │       ├── FMonolithIndexer.cpp
│   │       ├── FMonolithIndexDatabase.cpp
│   │       └── UMonolithIndexSubsystem.cpp
│   │
│   └── MonolithSource/
│       ├── MonolithSource.Build.cs
│       ├── Public/
│       │   ├── FMonolithSourceActions.h
│       │   └── FMonolithSourceProcess.h
│       └── Private/
│           ├── MonolithSourceModule.cpp
│           ├── FMonolithSourceActions.cpp
│           └── FMonolithSourceProcess.cpp
│
├── MCP/                                # Bundled Python (source indexing only)
│   ├── requirements.txt
│   ├── source_indexer.py               # tree-sitter based C++ parser
│   └── schema.sql                      # Engine source DB schema
│
├── Skills/                             # Claude Code skills
│   ├── unreal-blueprints/
│   ├── unreal-materials/
│   ├── unreal-animation/
│   ├── unreal-niagara/
│   ├── unreal-debugging/
│   ├── unreal-performance/
│   ├── unreal-project-search/
│   └── unreal-cpp/
│
├── Templates/                          # User-facing templates
│   ├── CLAUDE.md.example
│   └── .mcp.json.example
│
└── Docs/                               # Documentation
    └── plans/
        └── 2026-03-06-monolith-design.md   # This document
```

---

## 14. Phased Implementation Roadmap

### Phase 0: Foundation (MonolithCore)

**Goal:** Embedded MCP server running, tool registry dispatching, plugin settings functional.

**Deliverables:**
- `FMonolithHttpServer` — Streamable HTTP + SSE on configurable port
- `FMonolithToolRegistry` — Action registration, namespace routing, parameter validation
- `UMonolithSettings` — All settings fields, Project Settings UI
- `FMonolithJsonUtils` — Unified `SuccessJson()` / `ErrorJson()`
- `FMonolithAssetUtils` — Shared asset resolution helpers
- Placeholder `monolith_discover` and `monolith_status` tools
- `.uplugin` with all 9 modules declared (stubs for unimplemented ones)
- `.mcp.json.example` and basic `README.md`

**Validation:** Claude Code connects to `localhost:9316`, calls `monolith_status`, gets a response.

---

### Phase 1: Blueprint + Editor (Quick Wins)

**Goal:** Port the two simplest domains to validate the architecture end-to-end.

**Deliverables:**
- `MonolithBlueprint` — All 5 actions, replacing BlueprintReader plugin + unreal-blueprint-mcp
- `MonolithEditor` — All 11 actions, replacing unreal-editor-mcp
- End-to-end test: `monolith_discover("blueprint")` → `blueprint_query("get_variables", ...)` → correct response

**Validation:** Side-by-side comparison with old servers confirms identical results.

---

### Phase 2: Material + Config

**Goal:** Port the material domain (largest reader plugin) and config (simplest standalone).

**Deliverables:**
- `MonolithMaterial` — All 46 actions, replacing MaterialMCPReader + unreal-material-mcp
- `FMonolithMaterialGraphBuilder` — Programmatic material construction
- `MonolithConfig` — All 6 actions, replacing unreal-config-mcp

**Validation:** Material template creation and config resolution match old server output.

---

### Phase 3: Animation + Niagara (Heavy Lifters)

**Goal:** Port the two largest domains with the most complex C++ plugin interactions.

**Deliverables:**
- `MonolithAnimation` — All 62 actions, replacing AnimationMCPReader + unreal-animation-mcp
- `MonolithNiagara` — All 70 actions, replacing NiagaraMCPBridge (7 classes) + unreal-niagara-mcp

**Validation:** Complex operations (montage building, Niagara HLSL injection) work identically to old pipeline.

---

### Phase 4: Deep Project Indexer (MonolithIndex)

**Goal:** Full project indexing with SQLite+FTS5.

**Deliverables:**
- `FMonolithIndexer` — Asset iteration and deep property extraction
- `FMonolithIndexDatabase` — SQLite schema creation, write, and query
- `UMonolithIndexSubsystem` — First-launch detection, background indexing, re-index trigger
- All 5 `project.query` actions
- Progress UI in editor notifications

**Validation:** Full project index builds on first launch; `project.search` returns accurate results across all asset types.

---

### Phase 5: Engine Source Intelligence (MonolithSource)

**Goal:** Absorb unreal-source-mcp and reimplement unreal-api-mcp functionality.

**Deliverables:**
- `FMonolithSourceProcess` — Python child process management
- `FMonolithSourceActions` — All ~14 actions
- Bundled Python in `MCP/` directory with tree-sitter pipeline
- New actions: signature lookup, #include resolution, deprecation warnings
- `ATTRIBUTION.md` crediting Codeturion

**Validation:** Source search, call graph, and API lookup produce correct results.

---

### Phase 6: Auto-Updater

**Goal:** Self-updating from GitHub Releases.

**Deliverables:**
- `UMonolithUpdateSubsystem` — Startup check, download, staging, swap
- `Saved/version.json` version tracking
- Editor notification UI for update availability
- Staging/swap logic with rollback safety

**Validation:** Publish a test release; confirm editor detects, downloads, stages, and applies the update on restart.

---

### Phase 7: Skills, Templates & Polish

**Goal:** Ship the complete package with documentation and AI teaching materials.

**Deliverables:**
- All 8 skill directories with tested Claude Code skills
- `CLAUDE.md.example` template with Monolith-specific instructions
- Complete `README.md` with install, usage, and troubleshooting
- `LICENSE` (MIT) and `ATTRIBUTION.md`
- Final QA pass across all 231 actions

**Validation:** Fresh clone → install → first launch → AI interaction works end-to-end with no manual intervention beyond the 3-step install.

---

### Post-Launch

- **Incremental Indexing** — Asset Registry delegate-based index updates (no full re-index needed)
- **Mac/Linux Support** — Platform-specific build targets and testing
- **Marketplace Distribution** — Optional Fab/Marketplace listing alongside GitHub
- **Community Skills** — User-contributed skill packs
- **Telemetry (opt-in)** — Anonymous usage analytics for prioritizing development

---

*This document is the master reference for Project Monolith. All implementation decisions should trace back to sections in this spec. For questions or proposed changes, open an issue on [github.com/tumourlove/monolith](https://github.com/tumourlove/monolith).*
