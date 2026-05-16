#!/usr/bin/env bash
# Convenience wrapper: run the load test against all four languages and print
# a tidy comparison table. Forwards to run_tests.sh; the CSV summary is
# written to ./results/summary-<ts>.csv.
#
#   LOCAL=1 API_KEY=local-dev-key ./run_compare.sh
#   BASE_URL=https://api.example.com API_KEY=... DURATION=60s VUS=200 ./run_compare.sh
set -euo pipefail
DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
LANGUAGES="js python c cpp" "$DIR/run_tests.sh" "$@"

latest="$(ls -1t "${OUTPUT_DIR:-./results}"/summary-*.csv 2>/dev/null | head -1 || true)"
if [ -n "$latest" ]; then
    echo
    echo "==> ranked by reqs_per_sec (descending):"
    # skip header, sort numerically on column 6 (reqs_per_sec), then re-emit
    {
        head -1 "$latest"
        tail -n +2 "$latest" | sort -t, -k6 -nr
    } | column -s, -t
fi
