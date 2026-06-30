#!/bin/bash
set -euo pipefail

DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$DIR"

if [ ! -x "$DIR/bin/Objs2Abc" ]; then
  echo "Objs2Abc is not built yet. Building now..."
  "$DIR/build.sh"
fi

"$DIR/launch_gui.sh"
