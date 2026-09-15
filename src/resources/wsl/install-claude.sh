#!/usr/bin/env bash
# Installs the Claude Code CLI globally using the nvm-managed Node.js in WSL.
# Bundled into ws2tcp-local-qt and run by MainWindow::installClaudeCodeCli().
set -eo pipefail

export NVM_DIR="$HOME/.nvm"
if [ ! -s "$NVM_DIR/nvm.sh" ]; then
  echo 'nvm/Node.js not found in WSL -- run "Install Node.js (via nvm) in WSL" first' >&2
  exit 1
fi
# shellcheck source=/dev/null
. "$NVM_DIR/nvm.sh"

npm install -g @anthropic-ai/claude-code
