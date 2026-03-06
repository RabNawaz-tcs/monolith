# Monolith

**One plugin. Every Unreal domain. Zero Python bridges.**

[![UE 5.7+](https://img.shields.io/badge/Unreal-5.7%2B-blue)](https://unrealengine.com)
[![License: MIT](https://img.shields.io/badge/License-MIT-green.svg)](LICENSE)
[![MCP](https://img.shields.io/badge/Protocol-MCP-purple)](https://modelcontextprotocol.io)

Monolith consolidates **9 separate MCP servers** and **4 C++ plugins** into a single Unreal Engine editor plugin with an embedded MCP server. 231 tools reduced to ~14 namespace endpoints with 95% context reduction for AI assistants.

## Features

- **9 domains** — Blueprints, Materials, Animation, Niagara, Editor, Config, Project Index, Engine Source
- **Deep project indexer** — SQLite FTS5 full-text search across all asset types
- **Auto-updater** — Checks GitHub Releases on editor startup, one-click update
- **8 Claude Code skills** — Domain-specific workflow guides bundled with the plugin
- **Streamable HTTP + SSE** — Modern MCP transport with legacy fallback
- **Pure C++** — Direct UE API access, no Python intermediary for editor tools

## Quick Start

### 1. Install the plugin

```bash
cd YourProject/Plugins
git clone https://github.com/tumourlove/monolith.git Monolith
```

### 2. Configure MCP

Copy the example config to your project root:

```bash
cp Plugins/Monolith/Templates/.mcp.json.example .mcp.json
```

This creates a `.mcp.json` pointing Claude Code to `http://localhost:9316/mcp`.

### 3. Launch the editor

Monolith starts automatically and indexes your project on first launch.

### 4. Verify

```bash
curl -s http://localhost:9316/mcp -H "Content-Type: application/json" \
  -d '{"jsonrpc":"2.0","id":1,"method":"tools/list","params":{}}'
```

### 5. (Optional) Install skills

Copy the bundled skills into your Claude Code skills directory:

```bash
cp -r Plugins/Monolith/Skills/* ~/.claude/skills/
```

## Architecture

```
Monolith.uplugin
  MonolithCore          — HTTP server, tool registry, discovery, auto-updater
  MonolithBlueprint     — Blueprint graph reading (5 actions)
  MonolithMaterial      — Material inspection + graph editing (46 actions)
  MonolithAnimation     — Animation sequences, montages, ABPs (62 actions)
  MonolithNiagara       — Niagara particle systems (70 actions)
  MonolithEditor        — Build triggers, log capture, crash context (11 actions)
  MonolithConfig        — Config/INI resolution and search (6 actions)
  MonolithIndex         — SQLite FTS5 deep project indexer (5 actions)
  MonolithSource        — Engine source + API lookup (14 actions)
```

**Discovery/dispatch pattern:** `monolith.discover()` lists available actions per namespace. Each domain exposes a single `{namespace}.query(action, params)` tool. This reduces tool count from 231 to ~14, cutting context consumption by 95%.

## Tool Reference

| Namespace | Tool | Actions | Description |
|-----------|------|---------|-------------|
| `monolith` | `monolith.discover` | — | List available actions per namespace |
| `monolith` | `monolith.status` | — | Server health, version, index status |
| `monolith` | `monolith.reindex` | — | Trigger full project re-index |
| `monolith` | `monolith.update` | — | Check or install updates |
| `blueprint` | `blueprint.query` | 5 | Graph topology, variables, execution flow, node search |
| `material` | `material.query` | 46 | Inspection, editing, graph building, templates, previews |
| `animation` | `animation.query` | 62 | Sequences, montages, blend spaces, ABPs, skeletons |
| `niagara` | `niagara.query` | 70 | Systems, emitters, modules, parameters, renderers, HLSL |
| `editor` | `editor.query` | 11 | Build triggers, error logs, crash context |
| `config` | `config.query` | 6 | INI resolution, explain, diff, search |
| `project` | `project.query` | 5 | Deep project search — Blueprints, Materials, C++, assets |
| `source` | `source.query` | 14 | Engine source + API lookup, signatures, deprecation |

## Skills

Monolith bundles 8 Claude Code skills in `Skills/` for domain-specific workflows:

| Skill | File | Description |
|-------|------|-------------|
| Blueprints | `unreal-blueprints/` | Graph reading, variable inspection, execution flow |
| Materials | `unreal-materials/` | PBR setup, graph building, templates, validation |
| Animation | `unreal-animation/` | Montages, ABP state machines, blend spaces |
| Niagara | `unreal-niagara/` | Particle system creation, HLSL modules, scalability |
| Debugging | `unreal-debugging/` | Build errors, log search, crash context |
| Performance | `unreal-performance/` | Config auditing, shader stats, INI tuning |
| Project Search | `unreal-project-search/` | FTS5 search syntax, reference tracing |
| C++ | `unreal-cpp/` | API lookup, include paths, Build.cs gotchas |

## Configuration

Plugin settings are available in **Editor Preferences > Plugins > Monolith**:

- MCP server port (default: 9316)
- Auto-update toggle
- Module enable/disable toggles
- Database path overrides
- Re-index button

## Requirements

- Unreal Engine 5.7+
- Windows (Mac/Linux support planned)
- Python 3.10+ (only for engine source indexing)

## License

[MIT](LICENSE) — See [ATTRIBUTION.md](ATTRIBUTION.md) for credits.
