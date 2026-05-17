#!/usr/bin/env bash
# Run the load test against the JavaScript service only.
#   LOCAL=1 API_KEY=local-dev-key ./run_js.sh
#   BASE_URL=https://api.example.com API_KEY=... ./run_js.sh
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LANGUAGES=js exec "$DIR/run_tests.sh" "$@"
