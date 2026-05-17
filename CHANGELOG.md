# Changelog

All notable changes to this project are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/).

## [1.9.0] - 2026-05-17

### Added — Admin seeding, JWT rotation, bootstrap helper, DEPLOYMENT.md

Pre-deploy hardening across three independent threads:

- **Admin user seeding (item #1 of the pre-deploy audit):** new
  `admin_email` / `admin_password` Terraform variables stored in the
  existing app-secrets Secrets Manager entry and injected into the
  migrator task. New `db/entrypoint.sh` runs `migrate.sh up` then UPSERTs
  the admin user when both env vars are set. Migrator Dockerfile points
  at the new entrypoint. Idempotent — re-deploys rotate the admin
  password to whatever Secrets Manager currently holds.
- **JWT secret rotation (item #5):** new `jwt_secret_next` variable
  propagated through all four apps. Each language's `verify_session`
  function (JS `verifySession`, Python `verify_session`, C
  `jwt_verify_hs256_dual`, C++ `jwt_verify_hs256_dual`) tries the
  primary first, falls back to the next. Auth is "active" when EITHER
  secret is non-empty. Same four-step rotation pattern as the API key.
  17 new tests (4 per language + 1 in C++).
- **ECR bootstrap (item #2):** new
  `terraform/scripts/bootstrap-deploy.sh` resolves the
  IMMUTABLE-ECR chicken-and-egg: targeted apply of only the ECR repos →
  docker build/push initial `:bootstrap` and `:<sha>` images for every
  service + migrator → full apply with `image_tag=bootstrap` so services
  start with a real image. CI takes over on the next push to main.
- **DEPLOYMENT.md (items #4 + #6):** dedicated pre-deploy guide. Covers
  the AWS account / repo / GitHub Actions checklist, the cost
  break-down per environment (~$170/mo prod, ~$135/mo staging — NAT is
  the biggest line), commands for running Trivy and `terraform plan`
  locally to catch surprises CI hasn't seen yet, a smoke-test recipe
  for after the first deploy, and the list of deferred items
  (CloudFront, app metrics, more alarms, DR runbook, container hardening).

Test totals (before -> after):
- JS:     73 -> 77
- Python: 70 -> 74
- C:      44 -> 48
- C++:    47 -> 52
- Total: 234 -> 251

## [1.8.0] - 2026-05-17

### Added — Language linters in CI (eslint, ruff, clang-format)
- `apps/js/.eslintrc.json` (eslint:recommended + no-unused-vars with `_`
  ignore prefix) and an `npm run lint` script. `eslint` added to devDeps.
- `apps/python/pyproject.toml` with a conservative `[tool.ruff]` config
  (E/F/W/I/B/UP rule sets, `line-length=100`); `ruff` added to
  `requirements-dev.txt`.
- Repo-root `.clang-format` matching the existing hand-written style
  (4-space indent, 100-col, attached braces). The whole tree was
  reformatted in-place so the CI check has a clean baseline; vendored
  sources (mongoose, crow_all, unity) are excluded by the glob.
- New `lint` matrix job in `.github/workflows/deploy.yml` running
  eslint / ruff / clang-format per language. Runs in parallel with the
  test jobs; `security-fs-scan` now `needs:` lint too, so a lint failure
  blocks image build/push/deploy.
- All 234 tests still pass after the reformat + ruff auto-fixes.

## [1.7.0] - 2026-05-17

### Changed — ECR IMMUTABLE + SHA-only deploys
- All ECR repos (`<project>-{js,python,c,cpp,migrator}`) set to
  `image_tag_mutability = "IMMUTABLE"` — a deployed tag can no longer be
  silently overwritten.
- CI no longer pushes `:latest`; only `:<github.sha>` per build.
- Deploy job now describes each ECS task definition, patches
  `containerDefinitions[0].image` to the SHA via `jq`, registers a new
  revision, and updates the service to that revision. `--force-new-deployment`
  alone would not work because IMMUTABLE ECR makes "redeploy the same tag"
  meaningless.
- Migrate job uses the same describe → patch → register → `run-task`
  pattern against `<project>-migrator`.
- ECS service `lifecycle.ignore_changes` now includes `task_definition`
  so CI's revision bumps don't trigger Terraform drift.

## [1.6.0] - 2026-05-17

### Added — Staging environment + C/C++ access log status code
- `terraform/environments/staging/` — a copy of the prod root with smaller /
  cheaper defaults (single-AZ RDS, `desired_count=1`, `waf_rate_limit=1000`,
  separate VPC CIDR `10.1.0.0/16`). Shares the same state-backend S3 bucket
  + lock table as prod; isolated via `staging/terraform.tfstate` key.
- C and C++ services now emit the full combined access-log format
  (status + bytes + elapsed_ms + user_id), matching JS (morgan) and Python
  (uvicorn-style). C threads scratch fields through `app_ctx_t` since
  mongoose is single-threaded; C++ uses a Crow `App<AccessLogMiddleware>`
  with `before_handle` / `after_handle` and a thread_local for the JWT sub.

## [1.5.0] - 2026-05-16

### Added — Graceful API-key rotation + repo housekeeping
- New `api_key_next` Terraform variable, stored in the existing app-secrets
  Secrets Manager entry, injected into every task as `API_KEY_NEXT`. Empty
  means no rotation in flight.
- All four apps accept either `API_KEY` or `API_KEY_NEXT` on the `X-API-Key`
  header when both are configured:
  - JS: `createApp({ apiKey, apiKeyNext })` with OR-check in the middleware.
  - Python: `Config.api_key_next` + dual-check inside the `_api_key_auth`
    middleware.
  - C: new `check_api_key_dual` pure helper + `app_ctx_t.api_key_next` +
    dispatch uses dual variant.
  - C++: new `check_api_key_dual` + `AppDeps.api_key_next` + dual variant
    in the Crow `api_key_check` lambda.
- 16 new unit/integration tests (4 per language) covering primary-match,
  secondary-match, neither-matches, only-secondary-set, and disabled.
  Totals: **73 / 70 / 44 / 47 = 234** (was 218; +16).
- README gained a "Rotating the API key" section with the four-step
  procedure.

### Added — Repo housekeeping
- `.github/CODEOWNERS` with default + per-area ownership placeholders.
- `.github/pull_request_template.md` (what/how-tested/risk/checklist).
- `.editorconfig` (2-space default; 4-space for python/c/c++; tab for Makefiles).
- `SECURITY.md` — disclosure process, in-scope/out-of-scope, hardening notes.
- `.github/dependabot.yml` — weekly bumps for Docker base images, GitHub
  Actions, npm + pip lockfiles, and the AWS Terraform provider pin.

## [1.4.0] - 2026-05-16

### Added — Migrations in CI, OIDC docs, admin credentials parameterized
- New `apps/migrator/` image (Python + postgresql-client + bcrypt + `db/`).
  CI builds and pushes it to a dedicated `${project}-migrator` ECR repo on
  every push to main.
- ECS migrator task definition in the `ecs` module (Fargate, awsvpc, runs in
  the private subnets, pulls DB password from the existing app secret).
- SSM parameters `/${project}/migrator/{subnets,security-group}` so CI can
  resolve the network config without hardcoding.
- New `migrate` job in `.github/workflows/deploy.yml`. Runs `aws ecs run-task`
  on the migrator task definition, waits for it to stop, and **fails the
  whole workflow if the migration exited non-zero**. The `deploy` job now
  depends on `migrate`, so a schema-changing release cannot roll services
  until the schema is applied.
- README gained an "GitHub Actions setup" section listing every required
  secret/var and a copy-paste OIDC trust policy for the deploy role, plus
  a bootstrap recipe for an initial manual `aws ecs run-task` invocation.
- `docker-compose.yml` `migrate` service now reads `ADMIN_EMAIL` and
  `ADMIN_PASSWORD` from env (defaults match the previous hardcoded values,
  but it warns on stderr when the default password is in use).

## [1.3.0] - 2026-05-16

### Added — Redis TLS, edge-case suites, per-language benchmarks
- All four apps now honor `REDIS_TLS=true` and connect to ElastiCache via TLS
  (matches the Terraform default of `transit_encryption_enabled = true`):
  - JS: `socket.tls=true` on `redis.createClient`.
  - Python: `redis.Redis(ssl=True, ssl_cert_reqs="required")`.
  - C: `hiredis_ssl` — `redisInitOpenSSL` (pthread_once) +
    `redisCreateSSLContext` + `redisInitiateSSLWithContext`. Makefile links
    `-lhiredis_ssl -lssl`.
  - C++: same as C, exposed through `Cache(..., bool tls)` and
    initialized via `std::call_once`. CMake links `hiredis_ssl OpenSSL::SSL`.
- Comprehensive edge-case test sweep added per language:
  - Unicode + multi-byte content roundtrips (POST → GET).
  - Embedded `"`, `\`, control chars, and embedded NUL bytes (length-based
    serialization in C/C++).
  - Body exactly at `MAX_BODY_BYTES` boundary (accepted) and +1 (413).
  - `parse_positive_long` at the `LLONG_MAX` boundary; overflow rejection.
  - Method-not-allowed responses (`DELETE /api/<lang>/data`).
  - Unknown language prefix → 404.
  - JWT verifier rejects 1-dot / 4-dot / empty / garbage tokens.
  - bcrypt / cookie parsing edge cases (case-insensitive name, surrounding
    whitespace, truncated buffer).
  - b64url decoder rejects `+`, `/`, whitespace, and other non-url-safe chars.
  - `roles_contains_any` returns false for malformed JSON.
  - `check_api_key` returns MISSING (not INVALID) when presented is empty.
- Test totals (per language): **70 / 67 / 39 / 42 = 218** (was 180; +38).

### Added — Per-language and cross-language benchmark runners
- `benchmark/run_tests.sh` rewritten with two deployment shapes:
  - Single ALB: `BASE_URL=...`
  - Local docker-compose: `LOCAL=1` (auto-maps to 8081/8082/8083/8084), or
    per-language `BASE_URL_JS=...`.
  - Authenticates once via `/api/<lang>/auth/login` and forwards the cookie.
- New wrappers `run_js.sh`, `run_python.sh`, `run_c.sh`, `run_cpp.sh`,
  plus `run_compare.sh` (ranks all four by req/sec).
- `chaos_test.sh` adopts the same `LOCAL=1` / per-language URL pattern.

## [1.2.0] - 2026-05-16

### Added — JWT cookie auth + protected routes + access logging
- DB migrations: `db/migrations/V001__init.sql`, `V002__users.sql`, plus a
  canonical `db/migrate.sh` runner with `up`, `status`, and `seed-admin`
  subcommands. Tracking via `schema_migrations` table. Each migration runs
  in its own transaction together with its tracker insert.
- docker-compose: new one-shot `migrate` service installs psql + bcrypt,
  runs `migrate.sh up`, seeds an admin user, then exits. Every app
  `depends_on: migrate` so the schema is always current at boot.
- Users table with bcrypt password_hash + roles[]. Login flow in every
  language: `POST /api/<lang>/auth/login` returns a HttpOnly + SameSite=Strict
  + Secure session cookie holding an HS256 JWT (sub, email, roles, iat, exp).
- `POST /api/<lang>/auth/logout` clears the cookie. `GET /api/<lang>/auth/me`
  returns the current user (requires valid JWT).
- All `/api/<lang>/data*` routes now require a valid JWT (401 otherwise).
  `POST /api/<lang>/data` additionally requires role `writer` or `admin`
  (403 otherwise).
- Per-language pure auth helpers — `check_api_key`, `jwt_sign_hs256`,
  `jwt_verify_hs256`, `cookie_get_session`, `roles_contains_any`, bcrypt
  verify — implemented with constant-time comparison and unit-tested.
- Request access logging with rotation in every app: JS uses
  morgan+rotating-file-stream, Python uses RotatingFileHandler +
  middleware, C/C++ implement a small in-process rotator. All also mirror
  to stdout for CloudWatch.

### Security infrastructure (Terraform)
- ElastiCache: replaced `aws_elasticache_cluster` with
  `aws_elasticache_replication_group`; `at_rest_encryption_enabled` and
  `transit_encryption_enabled` default `true`; new `redis_tls` flag flows
  through to ECS tasks.
- ALB: `drop_invalid_header_fields = true`; access logs written to a new
  encrypted, public-access-blocked, lifecycle-90-day S3 bucket with the
  correct ELB-service-account bucket policy.
- WAF: CloudWatch log group named `aws-waf-logs-<project>` plus a
  `wafv2_web_acl_logging_configuration` resource.
- VPC: optional flow logs to a CloudWatch log group with a dedicated IAM
  role for the `vpc-flow-logs` service. Enabled by default.
- ECS task secrets: Secrets Manager JSON now also contains `JWT_SECRET`,
  injected into every task. New `COOKIE_SECURE=true`, `ACCESS_LOG_PATH`
  env vars on every task.
- New CI job `terraform plan (prod)`: runs `terraform plan` against the
  prod env on PRs when AWS deploy role + tfvars secrets are configured.

### Test totals (before → after this release)
- JS:     47 → 60
- Python: 44 → 57
- C:      21 → 31
- C++:    22 → 32
- Total:  134 → **180**

## [1.1.0] - 2026-05-15

### Added — API-key authentication
- New `check_api_key` pure helper in all four languages with constant-time
  comparison; AUTH_OK / AUTH_MISSING / AUTH_INVALID / AUTH_DISABLED states.
- Every `/api/<lang>/*` route now requires the `X-API-Key` header. Missing →
  401 `{"error":"missing api key"}`; wrong → 401 `{"error":"invalid api key"}`.
- `/health` stays public (ALB target-group health check).
- ECS Terraform module: the existing Secrets Manager secret now stores both
  `DB_PASSWORD` and `API_KEY`; both are injected into every task definition's
  `secrets` block. Renamed the secret to `${project_name}-app-secrets`.
- Root `terraform/environments/prod` and `terraform.tfvars.example` gained an
  `api_key` variable.
- `docker-compose.yml` sets `API_KEY=local-dev-key` on every service.
- `benchmark/run_tests.sh` and `benchmark/chaos_test.sh` now require
  `API_KEY` and forward `X-API-Key` on every probe; chaos suite gained two
  new checks: no-key → 401, wrong-key → 401.
- 23 new unit/integration tests across the four languages (134 total,
  up from 111).

## [1.0.0] - Production Ready - 2026-05-15

### Phase 0 — Project Setup, State & Tracking
- Created folder structure: `apps/{js,python,c,cpp}`, `terraform`, `tests`, `benchmark`, `security`.
- Added `README.md`, `CHANGELOG.md`, and `TRACKER.md` with the full task checklist.
- Added `terraform/environments/prod/providers.tf` with the S3 remote-state backend.
- Added `terraform/scripts/setup-backend.sh` to bootstrap the S3 bucket and DynamoDB lock table.
- Defined the shared database schema in `db/init.sql`.

### Phase 1 — Application Logic, Caching Suite & Edge Cases
- Defined the REST API contract shared by all four implementations.
- Implemented identical APIs in JavaScript (Express), Python (FastAPI), C (mongoose), and C++ (Crow).
- Added a Redis cache-aside layer with graceful fallback to RDS on cache outage/timeout.
- Added input validation, malformed-JSON handling, and payload-size limits.
- Added an optimized multi-stage Dockerfile per language.
- Added `docker-compose.yml` running all four apps plus local Postgres and Redis.

### Phase 1.5 — Testing Suite
- Added unit tests for all four languages (Jest, PyTest, GoogleTest, Unity).
- Added integration tests that mock DB/Redis and assert the Redis-timeout fallback returns 200 via the DB path.

### Phase 2 — Networking Foundation
- Added `terraform/modules/vpc`: 2-AZ VPC, 2 public + 2 private subnets, NAT gateways, route tables.

### Phase 3 — Database & Persistent Storage
- Added `terraform/modules/security-groups` with tightly scoped ALB/ECS/RDS/Redis rules.
- Added `terraform/modules/rds` (PostgreSQL) and `terraform/modules/elasticache` (Redis).

### Phase 4 — Load Balancing & SSL
- Added `terraform/modules/waf` with managed SQLi rules and a rate-limiting rule.
- Added `terraform/modules/alb`: ALB, 4 target groups, path-based routing, HTTP→HTTPS redirect, ACM certificate.

### Phase 5 — Compute & Orchestration
- Added `terraform/modules/ecs`: 4 ECR repos, 1 cluster, 4 Fargate services, task roles.
- Wired DB/Redis credentials through AWS Secrets Manager.
- Added CPU-based auto-scaling and per-container CloudWatch log groups.

### Phase 6 — CI/CD Pipeline
- Added `terraform/environments/prod/main.tf` wiring every module together.
- Added `.github/workflows/deploy.yml`: runs the full test suite and Trivy scans, gating image build/push to ECR/ECS.

### Phase 7 — Benchmark & Chaos Suite
- Added `benchmark/run_tests.sh` (k6 load test, per-language latency/throughput output).
- Added `benchmark/chaos_test.sh` (malformed payloads, oversized blobs, WAF rate-limit probes).

### Phase 8 — Monitoring, Logging & Final Analysis
- Added `terraform/modules/monitoring`: CloudWatch dashboard tracking CPU, memory, and errors for all four services.
- Added `BENCHMARK_RESULTS.md` (stub — to be populated after a live benchmark run).
- Finalized `README.md` with the architecture diagram and sealed this changelog as 1.0.0.
