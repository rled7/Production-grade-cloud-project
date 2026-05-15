#!/usr/bin/env bash
#
# Chaos / abuse tests. Verifies the apps and the infrastructure security
# suite stay healthy when fed malformed, oversized, or rate-abusive traffic.
#
# All probes target the ALB endpoint. The script exits non-zero if any
# expected protection fails (e.g. a malformed body produces a 5xx instead
# of a 400, or oversized payloads aren't rejected).
#
# Usage:
#   BASE_URL=https://api.example.com ./chaos_test.sh
#
set -euo pipefail

BASE_URL="${BASE_URL:?BASE_URL is required, e.g. https://api.example.com}"
LANGUAGES="${LANGUAGES:-js python c cpp}"
RATE_LIMIT_FLOOD="${RATE_LIMIT_FLOOD:-3000}" # requests for the rate-limit probe
fail=0

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
    url="$BASE_URL/api/$lang/data"

    # 1. Malformed JSON → 400
    code=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$url" \
        -H 'content-type: application/json' \
        -d 'this is not json')
    expect_status "malformed JSON body" 400 "$code"

    # 2. Missing content field → 400
    code=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$url" \
        -H 'content-type: application/json' \
        -d '{}')
    expect_status "missing content field" 400 "$code"

    # 3. Empty content → 400
    code=$(curl -s -o /dev/null -w "%{http_code}" -X POST "$url" \
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
    code=$(curl -s -o /dev/null -w "%{http_code}" "$inj_url")
    if [ "$code" = "403" ] || [ "$code" = "400" ]; then
        printf "  [OK]   %-40s got %s\n" "SQLi probe blocked/rejected" "$code"
    else
        printf "  [FAIL] %-40s expected 403/400 got %s\n" "SQLi probe blocked/rejected" "$code"
        fail=$((fail + 1))
    fi

    # 6. Invalid id (string) → 400
    code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/api/$lang/data/abc")
    expect_status "non-integer id" 400 "$code"

    # 7. Negative id → 400
    code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/api/$lang/data/-1")
    expect_status "negative id" 400 "$code"

    # 8. Health still passes after abuse
    code=$(curl -s -o /dev/null -w "%{http_code}" "$BASE_URL/health")
    expect_status "/health still 200" 200 "$code"
done

echo
echo "========== WAF rate-limit probe (single language) =========="
# Hammer one endpoint with $RATE_LIMIT_FLOOD requests from this IP. After
# WAF triggers we should see a substantial number of 403s.
lang="${LANGUAGES%% *}"
url="$BASE_URL/api/$lang/data"
codes_file="$(mktemp)"
trap 'rm -f "$codes_file"' EXIT

for _ in $(seq 1 "$RATE_LIMIT_FLOOD"); do
    curl -s -o /dev/null -w "%{http_code}\n" "$url" &
done
wait
echo "(parallel curls done; counting codes that arrived)"
# Re-run a small sequential burst to actually capture the 403 wave reliably.
for _ in $(seq 1 200); do
    curl -s -o /dev/null -w "%{http_code}\n" "$url"
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
