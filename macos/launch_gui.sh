#!/bin/bash
set -euo pipefail

# When launched from Finder/.app the shell PATH is not inherited, so Homebrew
# (in /opt/homebrew or /usr/local) and its Python are invisible. Restore PATH.
export PATH="/opt/homebrew/bin:/opt/homebrew/sbin:/usr/local/bin:/usr/local/sbin:$PATH"
if command -v brew >/dev/null 2>&1; then
  eval "$(brew shellenv)" || true
fi

TOOL_DIR="$(cd "$(dirname "$0")" && pwd)"
GUI_PATH="$TOOL_DIR/gui.py"
LOG_PATH="${TMPDIR:-/tmp}/obj-sequence-to-alembic-python-tk.log"

show_dialog() {
  local message="$1"
  local icon="${2:-note}"
  if command -v osascript >/dev/null 2>&1; then
    osascript -e "display dialog \"$message\" buttons {\"OK\"} default button \"OK\" with icon $icon" >/dev/null 2>&1 || true
  else
    echo "$message" >&2
  fi
}

ask_install_python_tk() {
  if ! command -v osascript >/dev/null 2>&1; then
    return 1
  fi

  osascript <<'OSA' >/dev/null
display dialog "This Mac's system Python/Tk cannot open the GUI. Install Homebrew python-tk now? This can take a few minutes." buttons {"Cancel", "Install"} default button "Install" with icon caution
OSA
}

candidate_pythons() {
  if [ -n "${OBJ2ABC_PYTHON:-}" ]; then
    printf '%s\n' "$OBJ2ABC_PYTHON"
  fi

  cat <<'PATHS'
/opt/homebrew/bin/python3.14
/opt/homebrew/bin/python3
/opt/homebrew/bin/python3.13
/opt/homebrew/bin/python3.12
/usr/local/bin/python3.14
/usr/local/bin/python3
/usr/local/bin/python3.13
/usr/local/bin/python3.12
/Library/Frameworks/Python.framework/Versions/Current/bin/python3
/usr/bin/python3
PATHS

  command -v python3 2>/dev/null || true
}

python_has_working_tk() {
  local python_path="$1"
  [ -x "$python_path" ] || return 1

  "$python_path" - <<'PY' >/dev/null 2>&1
import tkinter as tk
root = tk.Tk()
root.withdraw()
root.update_idletasks()
root.destroy()
PY
}

find_working_python() {
  local seen=":"
  local python_path
  while IFS= read -r python_path; do
    [ -n "$python_path" ] || continue
    case "$seen" in
      *":$python_path:"*) continue ;;
    esac
    seen="${seen}${python_path}:"

    if python_has_working_tk "$python_path"; then
      printf '%s\n' "$python_path"
      return 0
    fi
  done < <(candidate_pythons)

  return 1
}

install_python_tk() {
  if ! command -v brew >/dev/null 2>&1; then
    show_dialog "No working Python/Tk was found. Install Homebrew from https://brew.sh, then run: brew install python-tk" "stop"
    return 1
  fi

  if ! ask_install_python_tk; then
    return 1
  fi

  {
    echo "Installing python-tk with Homebrew..."
    date
    brew install python-tk
  } >"$LOG_PATH" 2>&1
}

main() {
  if [ ! -f "$GUI_PATH" ]; then
    show_dialog "gui.py was not found next to the launcher." "stop"
    exit 1
  fi

  local python_path=""
  if ! python_path="$(find_working_python)"; then
    install_python_tk || exit 1
    if ! python_path="$(find_working_python)"; then
      show_dialog "python-tk installed, but no working Tk Python could be launched. See: $LOG_PATH" "stop"
      exit 1
    fi
  fi

  if [ "${1:-}" = "--check" ]; then
    echo "$python_path"
    exit 0
  fi

  exec "$python_path" "$GUI_PATH"
}

main "$@"
