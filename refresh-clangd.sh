#!/usr/bin/env bash
# Regenerate compile_commands.json for clangd (run after adding/removing
# source files). UnrealBuildTool writes the database to the engine root,
# so we copy it back here where the clangd extension looks for it.
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
UE_ROOT="${UE_ROOT:-$HOME/UnrealEngine}"

if [[ ! -x "$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" ]]; then
    echo "error: Unreal Engine not found at $UE_ROOT (set UE_ROOT to override)" >&2
    exit 1
fi

"$UE_ROOT/Engine/Build/BatchFiles/Linux/Build.sh" \
    -mode=GenerateClangDatabase \
    -project="$PROJECT_DIR/ArchipelagoCommand.uproject" \
    ArchipelagoCommandEditor Linux Development

cp "$UE_ROOT/compile_commands.json" "$PROJECT_DIR/"
ENTRIES=$(python3 -c "import json; print(len(json.load(open('$PROJECT_DIR/compile_commands.json'))))")
echo "compile_commands.json refreshed: $ENTRIES translation units."
echo "clangd will re-index in the background; restart it in VSCode"
echo "(Ctrl+Shift+P -> 'clangd: Restart language server') to force it."
