#!/bin/bash
# Double-click in Finder: builds SKYFALL once (Release), installs SKYFALL.app in ~/Applications
# and puts a SKYFALL icon on the Desktop. Run it again after updating the game.
cd "$(dirname "$0")" || exit 1
./scripts/make_app.sh
echo
echo "Done. Double-click SKYFALL on your Desktop to play. You can close this window."
read -n 1 -s -r -p "Press any key to close..."
