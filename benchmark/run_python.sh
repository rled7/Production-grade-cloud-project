#!/usr/bin/env bash
# Run the load test against the Python service only.
#   LOCAL=1 API_KEY=local-dev-key ./run_python.sh
#   BASE_URL=https://api.example.com API_KEY=... ./run_python.sh
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LANGUAGES=python exec "$DIR/run_tests.sh" "$@"
