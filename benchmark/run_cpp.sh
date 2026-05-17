#!/usr/bin/env bash
# Run the load test against the C++ service only.
#   LOCAL=1 API_KEY=local-dev-key ./run_cpp.sh
#   BASE_URL=https://api.example.com API_KEY=... ./run_cpp.sh
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LANGUAGES=cpp exec "$DIR/run_tests.sh" "$@"
