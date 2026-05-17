#!/usr/bin/env bash
#
# Cross-language load test. Runs k6 against each language's API,
# logging in once to acquire a session cookie before the run, then sending
# ~80% GET / ~20% POST traffic. Emits a per-language CSV with throughput
# and p95/p99 latency.
#
# Two deployment shapes are supported:
#
#   1. Single ALB endpoint (production):
#        BASE_URL=https://api.example.com API_KEY=... ./run_tests.sh
#
#   2. Per-language endpoints (local docker-compose):
#        LOCAL=1 API_KEY=local-dev-key ./run_tests.sh
#      which sets BASE_URL_<LANG>=http://localhost:808X for js/python/c/cpp.
#      You can also pass BASE_URL_JS=... directly for any language.
#
# Auth: requires a real user in the users table. docker-compose seeds
# admin@local/supersecret; override with EMAIL and PASSWORD env vars.
#
# Knobs:
#   DURATION  (default 30s)        — k6 run length
#   VUS       (default 50)         — virtual users
#   LANGUAGES (default "js python c cpp")
#
set -euo pipefail

LOCAL="${LOCAL:-0}"
LANGUAGES="${LANGUAGES:-js python c cpp}"
DURATION="${DURATION:-30s}"
VUS="${VUS:-50}"
API_KEY="${API_KEY:?API_KEY required (the value services were deployed with)}"
EMAIL="${EMAIL:-admin@local}"
PASSWORD="${PASSWORD:-supersecret}"
OUTPUT_DIR="${OUTPUT_DIR:-./results}"
mkdir -p "$OUTPUT_DIR"

resolve_url() {
    local lang="$1"
    local var="BASE_URL_$(echo "$lang" | tr '[:lower:]' '[:upper:]')"
    if [ -n "${!var-}" ]; then
        echo "${!var}"
        return
    fi
    if [ "$LOCAL" = "1" ]; then
        case "$lang" in
            js)     echo "http://localhost:8081" ;;
            python) echo "http://localhost:8082" ;;
            c)      echo "http://localhost:8083" ;;
            cpp)    echo "http://localhost:8084" ;;
            *)      echo "FATAL: no URL mapping for lang=$lang" >&2; exit 1 ;;
        esac
        return
    fi
    if [ -n "${BASE_URL:-}" ]; then
        echo "$BASE_URL"
        return
    fi
    echo "FATAL: BASE_URL or LOCAL=1 required (no URL for $lang)" >&2
    exit 1
}

if command -v k6 >/dev/null 2>&1; then
    DRIVER="k6"
elif command -v wrk >/dev/null 2>&1; then
    DRIVER="wrk"
else
    echo "FATAL: neither k6 nor wrk is installed." >&2
    exit 1
fi
echo "==> driver=$DRIVER local=$LOCAL languages=\"$LANGUAGES\" duration=$DURATION vus=$VUS"

TIMESTAMP="$(date -u +%Y%m%dT%H%M%SZ)"
SUMMARY_CSV="$OUTPUT_DIR/summary-${TIMESTAMP}.csv"
echo "lang,url,duration,vus,reqs,reqs_per_sec,p95_ms,p99_ms,error_rate" > "$SUMMARY_CSV"

if [ "$DRIVER" = "k6" ]; then
    K6_SCRIPT="$(mktemp --suffix=.js)"
    trap 'rm -f "$K6_SCRIPT"' EXIT
    cat > "$K6_SCRIPT" <<'JS'
import http from 'k6/http';
import { check } from 'k6';

const base    = __ENV.BASE_URL;
const lang    = __ENV.LANG_NAME;
const apiKey  = __ENV.API_KEY;
const email   = __ENV.EMAIL;
const pass    = __ENV.PASSWORD;

export const options = {
    duration: __ENV.DURATION || '30s',
    vus: parseInt(__ENV.VUS || '50', 10),
    thresholds: {
        http_req_failed: ['rate<0.01'],
        http_req_duration: ['p(95)<500', 'p(99)<1000'],
    },
};

// Login once before the run to acquire a session cookie.
export function setup() {
    const r = http.post(
        `${base}/api/${lang}/auth/login`,
        JSON.stringify({ email, password: pass }),
        { headers: { 'content-type': 'application/json', 'X-API-Key': apiKey }},
    );
    if (r.status !== 200) {
        throw new Error(`login failed: ${r.status} ${r.body}`);
    }
    const cookie = r.headers['Set-Cookie'] || '';
    const m = cookie.match(/session=([^;]+)/);
    if (!m) throw new Error(`no session cookie in Set-Cookie: ${cookie}`);
    return { session: m[1] };
}

export default function (data) {
    const headers = {
        'X-API-Key': apiKey,
        Cookie: `session=${data.session}`,
    };
    const r = Math.random();
    if (r < 0.8) {
        const res = http.get(`${base}/api/${lang}/data`, { headers });
        check(res, { 'GET 200': (r) => r.status === 200 });
    } else {
        const payload = JSON.stringify({ content: `bench-${Date.now()}` });
        const res = http.post(`${base}/api/${lang}/data`, payload, {
            headers: { ...headers, 'content-type': 'application/json' },
        });
        check(res, { 'POST 201': (r) => r.status === 201 });
    }
}
JS
fi

for lang in $LANGUAGES; do
    url="$(resolve_url "$lang")"
    raw="$OUTPUT_DIR/${lang}-${TIMESTAMP}.json"
    echo
    echo "================================================================"
    printf "  Language: %-7s  URL: %s\n" "$lang" "$url"
    echo "  duration=$DURATION  vus=$VUS"
    echo "================================================================"

    if [ "$DRIVER" = "k6" ]; then
        BASE_URL="$url" LANG_NAME="$lang" DURATION="$DURATION" VUS="$VUS" \
            API_KEY="$API_KEY" EMAIL="$EMAIL" PASSWORD="$PASSWORD" \
            k6 run --summary-export "$raw" "$K6_SCRIPT" || true

        reqs=$(jq -r '.metrics.http_reqs.count // 0' "$raw")
        rps=$(jq -r '.metrics.http_reqs.rate // 0' "$raw")
        p95=$(jq -r '.metrics.http_req_duration["p(95)"] // 0' "$raw")
        p99=$(jq -r '.metrics.http_req_duration["p(99)"] // 0' "$raw")
        err=$(jq -r '.metrics.http_req_failed.value // 0' "$raw")
        printf "%s,%s,%s,%s,%s,%.2f,%.2f,%.2f,%.4f\n" \
            "$lang" "$url" "$DURATION" "$VUS" "$reqs" "$rps" "$p95" "$p99" "$err" \
            >> "$SUMMARY_CSV"
    else
        # wrk fallback: less rich metrics. Pre-login with curl to get a cookie.
        cookies="$(mktemp)"
        trap 'rm -f "$cookies"' RETURN
        curl -s -c "$cookies" -o /dev/null \
            -X POST -H "X-API-Key: $API_KEY" \
            -H 'content-type: application/json' \
            -d "{\"email\":\"$EMAIL\",\"password\":\"$PASSWORD\"}" \
            "$url/api/$lang/auth/login"
        session="$(awk '$6=="session" {print $7}' "$cookies" | head -1)"
        wrk -t8 -c"$VUS" -d"$DURATION" --latency \
            -H "X-API-Key: $API_KEY" \
            -H "Cookie: session=$session" \
            "$url/api/$lang/data" | tee "$raw"
        printf "%s,%s,%s,%s,,,,,\n" "$lang" "$url" "$DURATION" "$VUS" >> "$SUMMARY_CSV"
        rm -f "$cookies"
    fi
done

echo
echo "==> summary written to $SUMMARY_CSV"
column -s, -t < "$SUMMARY_CSV"
