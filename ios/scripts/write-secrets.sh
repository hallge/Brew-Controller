#!/usr/bin/env bash
# Writes ios/Config/Secrets.xcconfig from environment variables (Codemagic's bbw_release group).
# Adapted from the Sideline project's script of the same name.
set -euo pipefail
out="$(dirname "$0")/../Config/Secrets.xcconfig"
{
  echo "DEVELOPMENT_TEAM = ${TEAM_ID:-}"
  echo "CURRENT_PROJECT_VERSION = ${BUILD_NUMBER:-1}"
} > "$out"
echo "wrote $(basename "$out") (team set: $([ -n "${TEAM_ID:-}" ] && echo yes || echo no), build ${BUILD_NUMBER:-1})"
