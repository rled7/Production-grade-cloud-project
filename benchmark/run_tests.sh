#!/usr/bin/env bash
#
# Load-test the four language services through the public ALB endpoint and
# emit per-language latency / throughput / error metrics to a CSV summary.
#
# Requires: k6 (https://k6.io). Falls back to wrk if k6 is missing.
#
# Usage:
#   BASE_URL=https://api.example.com ./run_tests.sh
#   BASE_URL=... DURATION=60s VUS=200 LANGUAGES="js python c cpp" ./run_tests.sh
#
set -euo pipefail

BASE_URL="${BASE_URL:?BASE_URL is required, e.g. https://api.example.com}"
DURATION="${DURATION:-30s}"
VUS="${VUS:-200}"
LANGUAGES="${LANGUAGES:-js python c cpp}"
OUTPUT_DIR="${OUTPUT_DIR:-./results}"
mkdir -p "$OUTPUT_DIR"

TIMESTAMP="$(date -u +%Y%m%dT%H%M%SZ)"
SUMMARY_CSV="$OUTPUT_DIR/summary-${TIMESTAMP}.csv"
echo "lang,duration,vus,reqs,reqs_per_sec,p95_ms,p99_ms,error_rate" > "$SUMMARY_CSV"

if command -v k6 >/dev/null 2>&1; then
    DRIVER="k6"
elif command -v wrk >/dev/null 2>&1; then
    DRIVER="wrk"
else
    echo "FATAL: neither k6 nor wrk is installed." >&2
    exit 1
fi
echo "==> using $DRIVER"

# Build a temporary k6 script we can reuse per language.
if [ "$DRIVER" = "k6" ]; then
    K6_SCRIPT="$(mktemp --suffix=.js)"
    trap 'rm -f "$K6_SCRIPT"' EXIT
    cat > "$K6_SCRIPT" <<'JS'
import http from 'k6/http';
import { check } from 'k6';

const base = __ENV.BASE_URL;
const lang = __ENV.LANG_NAME;

export const options = {
    duration: __ENV.DURATION || '30s',
    vus: parseInt(__ENV.VUS || '200', 10),
    thresholds: {
        http_req_failed: ['rate<0.01'],
        http_req_duration: ['p(95)<500', 'p(99)<1000'],
    },
};

export default function () {
    // ~80% reads, 20% writes — exercises both cache hits and invalidation.
    const r = Math.random();
    if (r < 0.8) {
        const res = http.get(`${base}/api/${lang}/data`);
        check(res, { 'status 200': (r) => r.status === 200 });
    } else {
        const payload = JSON.stringify({ content: `bench-${Date.now()}` });
        const res = http.post(`${base}/api/${lang}/data`, payload, {
            headers: { 'content-type': 'application/json' },
        });
        check(res, { 'status 201': (r) => r.status === 201 });
    }
}
JS
fi

for lang in $LANGUAGES; do
    raw="$OUTPUT_DIR/${lang}-${TIMESTAMP}.json"
    echo
    echo "================================================================"
    echo "  Language: $lang  duration=$DURATION  vus=$VUS"
    echo "================================================================"

    if [ "$DRIVER" = "k6" ]; then
        BASE_URL="$BASE_URL" LANG_NAME="$lang" DURATION="$DURATION" VUS="$VUS" \
            k6 run --summary-export "$raw" "$K6_SCRIPT" || true

        reqs=$(jq -r '.metrics.http_reqs.count // 0' "$raw")
        rps=$(jq -r '.metrics.http_reqs.rate // 0' "$raw")
        p95=$(jq -r '.metrics.http_req_duration["p(95)"] // 0' "$raw")
        p99=$(jq -r '.metrics.http_req_duration["p(99)"] // 0' "$raw")
        err=$(jq -r '.metrics.http_req_failed.value // 0' "$raw")
        printf "%s,%s,%s,%s,%.2f,%.2f,%.2f,%.4f\n" \
            "$lang" "$DURATION" "$VUS" "$reqs" "$rps" "$p95" "$p99" "$err" \
            >> "$SUMMARY_CSV"
    else
        # wrk fallback: less rich metrics but a sane baseline.
        wrk -t8 -c"$VUS" -d"$DURATION" --latency \
            "$BASE_URL/api/$lang/data" | tee "$raw"
        printf "%s,%s,%s,,,,,\n" "$lang" "$DURATION" "$VUS" >> "$SUMMARY_CSV"
    fi
done

echo
echo "==> summary written to $SUMMARY_CSV"
column -s, -t < "$SUMMARY_CSV"
