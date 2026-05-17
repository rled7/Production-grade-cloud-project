# Security policy

## Reporting a vulnerability

Email the maintainer privately rather than opening a public issue. Include:

- The version (`git rev-parse HEAD`) you reproduced on.
- Repro steps, ideally a minimal curl or k6 script.
- Impact assessment as you see it.

A reply (acknowledgement) is expected within **3 business days**; a fix
or mitigation plan within **14 days** of acknowledgement, depending on
severity.

## Supported versions

This is a reference / benchmark project, not a long-lived library.
The `main` branch is the only supported version; older tags are
historical.

## In-scope

- Authentication/authorization bypass in any of the four services
  (`apps/js`, `apps/python`, `apps/c`, `apps/cpp`).
- JWT validation bugs (signature, expiry, malformed token handling).
- API-key gate bypass.
- SQL injection via the data endpoints.
- Container escape via the published Docker images.
- IAM privilege escalation or wildcards in the Terraform modules.
- Secrets leakage (Secrets Manager handling, environment variables,
  log lines, error responses).

## Out of scope

- Findings against the demo seed user (`admin@local` / `supersecret` from
  `docker-compose.yml`). That account is intentionally weak — production
  deploys override `ADMIN_EMAIL` / `ADMIN_PASSWORD`.
- DoS findings against the rate-limit threshold (`waf_rate_limit`, default
  2000 reqs/5min/IP). Tune in your own deployment.
- Cosmetic CSP / clickjacking issues on `/health` (a JSON endpoint, not a
  page).
- Findings that require the operator to set `JWT_SECRET=""` or
  `API_KEY=""` — both env vars are explicitly documented as "auth
  disabled" toggles for the test suites.

## Hardening notes for operators

- `terraform/environments/prod/terraform.tfvars` MUST stay gitignored;
  it holds the DB password, API key, and JWT secret.
- Generate `jwt_secret` with `openssl rand -hex 64`; rotate by setting
  the new value, applying, and invalidating outstanding sessions out of
  band (no graceful in-flight rotation for JWT yet).
- API-key rotation: `api_key_next` is supported — see README for the
  graceful overlap procedure.
- The chaos suite (`benchmark/chaos_test.sh`) is destructive — never
  point `BASE_URL` at a production endpoint without dropping
  `RATE_LIMIT_FLOOD` to a small number first.
