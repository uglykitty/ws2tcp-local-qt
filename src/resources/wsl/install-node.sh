#!/usr/bin/env bash
# Installs (or updates) nvm and uses it to install the latest Node.js LTS.
# Bundled into ws2tcp-local-qt and run inside WSL by MainWindow::installNodeViaNvm().
set -eo pipefail

NVM_INSTALL_URL="https://raw.githubusercontent.com/nvm-sh/nvm/v0.40.1/install.sh"

# nvm's installer can exit non-zero on its git-based "already installed,
# updating" path even when the update itself succeeded, so its exit code
# isn't a reliable success signal. The checks below are the real gate.
curl -fsS -o- "$NVM_INSTALL_URL" | bash || true

export NVM_DIR="$HOME/.nvm"
[ -s "$NVM_DIR/nvm.sh" ]
# shellcheck source=/dev/null
. "$NVM_DIR/nvm.sh"

nvm install --lts
