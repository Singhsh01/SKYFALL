#!/bin/bash
# Double-click to play. Runs the game from this folder so settings/logs stay next to it.
cd "$(dirname "$0")"
xattr -dr com.apple.quarantine . 2>/dev/null   # allow the unsigned download to run
exec ./skyfall "$@"
