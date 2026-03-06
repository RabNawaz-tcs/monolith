"""CLI entry point for the Monolith source indexer."""

import argparse
import sys


def main():
    parser = argparse.ArgumentParser(
        description="Index Unreal Engine source code into SQLite+FTS5 database"
    )
    parser.add_argument(
        "engine_path",
        help="Path to UE source (e.g. C:/Program Files/Epic Games/UE_5.7/Engine/Source)",
    )
    parser.add_argument(
        "-o", "--output",
        default="EngineSource.db",
        help="Output database path (default: EngineSource.db)",
    )
    parser.add_argument(
        "--version-tag",
        default="5.7",
        help="Engine version tag for database naming",
    )

    args = parser.parse_args()

    # TODO: Implement indexing pipeline
    # 1. Walk engine source tree
    # 2. Parse each .h/.cpp with tree-sitter-cpp
    # 3. Extract symbols (classes, functions, enums, macros)
    # 4. Build SQLite+FTS5 database
    print(f"Monolith source indexer — targeting {args.engine_path}")
    print("Indexer not yet implemented. This is a scaffold placeholder.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
