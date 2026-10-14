#!/usr/bin/env bash
# Scan repository source with CE. Findings, invalid rules and scan errors fail.
# Registry rules require network access; .semgrep.yml adds project-specific rules.
set -euo pipefail
configs=(--config p/ci)
if [[ -f .semgrep.yml ]]; then
  configs+=(--config .semgrep.yml)
fi
exec semgrep scan --oss-only "${configs[@]}" --metrics off \
  --disable-version-check --error --strict --jobs 2 "$@" .
