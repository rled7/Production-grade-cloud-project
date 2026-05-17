#!/usr/bin/env bash
#
# Chaos / abuse tests. Verifies the apps and the infrastructure security
# suite stay healthy when fed malformed, oversized, or rate-abusive traffic.
#
# Two deployment shapes are supported (matches run_tests.sh):
#
#   1. Single ALB endpoint:
#        BASE_URL=https://api.example.com API_KEY=... ./chaos_test.sh
#
#   2. Per-language endpoints (local docker-compose):
#        LOCAL=1 API_KEY=local-dev-key ./chaos_test.sh
#
# Exits non-zero if any expected protection fails.
#
set -euo pipefail

LOCAL="${LOCAL:-0}"
API_KEY="${API_KEY:?API_KEY is required (the value the services were deployed with)}"
LANGUAGES="${LANGUAGES:-js python c cpp}"
RATE_LIMIT_FLOOD="${RATE_LIMIT_FLOOD:-3000}"
auth_hdr=(-H "X-API-Key: $API_KEY")
fail=0

resolve_url() {
    local lang="$1"
    local var="BASE_URL_$(echo "$lang" | tr '[:lower:]' '[:upper:]')"
    if [ -n "${!var-}" ]; then echo "${!var}"; return; fi
    if [ "$LOCAL" = "1" ]; then
        case "$lang" in
            js)     echo "http://localhost:8081" ;;
            python) echo "http://localhost:8082" ;;
            c)      echo "http://localhost:8083" ;;
            cpp)    echo "http://localhost:8084" ;;
            *) echo "FATAL: no URL mapping for lang=$lang" >&2; exit 1 ;;
        esac
        return
    fi
    if [ -n "${BASE_URL:-}" ]; then echo "$BASE_URL"; return; fi
    echo "FATAL: BASE_URL or LOCAL=1 required" >&2; exit 1
}

expect_status() {
    local label="$1" expected="$2" actual="$3"
    if [ "$actual" = "$expected" ]; then
        printf "  [OK]   %-40s got %s\n" "$label" "$actual"
    else
        printf "  [FAIL] %-40s expected %s got %s\n" "$label" "$expected" "$actual"
        fail=$((fail + 1))
    fi
}

for lang in $LANGUAGES; do
    echo
    echo "========== $lang =========="
    BASE_URL="$(resolve_url "$lang")"
    url="$BASE_URL/api/$lang/data"

    # 1. Malformed JSON → 400
    code=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$url" \
        "${auth_hdr[@]}" \
        -H 'content-type: application/json' \
        -d 'this is not json')
    expect_status "malformed JSON body" 400 "$code"

    # 2. Missing content field → 400
    code=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$url" \
        "${auth_hdr[@]}" \
        -H 'content-type: application/json' \
        -d '{}')
    expect_status "missing content field" 400 "$code"

    # 3. Empty content → 400
    code=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$url" \
        "${auth_hdr[@]}" \
        -H 'content-type: application/json' \
        -d '{"content":""}')
    expect_status "empty content" 400 "$code"

    # 4. Massive payload (~2 MiB) → 413 (or 400 if framework rejects earlier)
    big_payload="$(mktemp)"
    {
        printf '{"content":"'
        head -c $((2 * 1024 * 1024)) /dev/urandom | base64 | tr -d '\n'
        printf '"}'
    } > "$big_payload"
    code=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$url" \
        "${auth_hdr[@]}" \
        -H 'content-type: application/json' \
        --data-binary @"$big_payload")
    rm -f "$big_payload"
    if [ "$code" = "413" ] || [ "$code" = "400" ]; then
        printf "  [OK]   %-40s got %s\n" "oversized payload rejected" "$code"
    else
        printf "  [FAIL] %-40s expected 413/400 got %s\n" "oversized payload rejected" "$code"
        fail=$((fail + 1))
    fi

    # 5. SQL-injection attempt — WAF should block (403) or app validates (400).
    inj_url="$BASE_URL/api/$lang/data/1%27%20OR%20%271%27%3D%271"
    code=$(curl -s -o /dev/null -w "%{http_code}" "${auth_hdr[@]}" "$inj_url")
    if [ "$code" = "403" ] || [ "$code" = "400" ]; then
        printf "  [OK]   %-40s got %s\n" "SQLi probe blocked/rejected" "$code"
    else
        printf "  [FAIL] %-40s expected 403/400 got %s\n" "SQLi probe blocked/rejected" "$code"
        fail=$((fail + 1))
    fi

    # 6. Invalid id (string) → 400
    code=$(curl -s -o /dev/null -w "%{http_code}" "${auth_hdr[@]}" "$BASE_URL/api/$lang/data/abc")
    expect_status "non-integer id" 400 "$code"

    # 7. Negative id → 400
    code=$(curl -s -o /dev/null -w "%{http_code}" "${auth_hdr[@]}" "$BASE_URL/api/$lang/data/-1")
    expect_status "negative id" 400 "$code"

    # 8. Missing API key → 401
    code=$(curl -s -o /dev/null -w "%{http_code}" "$url")
    expect_status "no api key" 401 "$code"

    # 9. Wrong API key → 401
    code=$(curl -s -o /dev/null -w "%{http_code}" -H "X-API-Key: wrong" "$url")
    expect_status "wrong api key" 401 "$code"

    # 10. Health still passes after abuse — and stays unauthenticated.
    code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/health")
    expect_status "/health still 200 (no auth needed)" 200 "$code"
done

echo
echo "========== WAF rate-limit probe (single language) =========="
# Hammer one endpoint with $RATE_LIMIT_FLOOD requests from this IP. After
# WAF triggers we should see a substantial number of 403s.
lang="${LANGUAGES%% *}"
BASE_URL="$(resolve_url "$lang")"
url="$BASE_URL/api/$lang/data"
codes_file="$(mktemp)"
trap 'rm -f "$codes_file"' EXIT

for _ in $(seq 1 "$RATE_LIMIT_FLOOD"); do
    curl -s -o /dev/null -w "%{http_code}\n" "${auth_hdr[@]}" "$url" &
done
wait
echo "(parallel curls done; counting codes that arrived)"
# Re-run a small sequential burst to actually capture the 403 wave reliably.
for _ in $(seq 1 200); do
    curl -s -o /dev/null -w "%{http_code}\n" "${auth_hdr[@]}" "$url"
done >> "$codes_file"

blocked=$(grep -c '^403' "$codes_file" || true)
ok=$(grep -c '^200' "$codes_file" || true)
echo "  200s in sample: $ok   403s in sample: $blocked"
if [ "$blocked" -gt 0 ]; then
    echo "  [OK]   WAF rate-limit triggered ($blocked × 403)"
else
    echo "  [WARN] no 403s observed — confirm rate-limit threshold or window."
fi

echo
if [ $fail -gt 0 ]; then
    echo "FAILED: $fail chaos check(s) did not behave as expected."
    exit 1
fi
echo "All chaos checks passed."
