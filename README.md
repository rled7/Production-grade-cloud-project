# Advanced Multi-Language AWS ECS Benchmark & Testing Suite

A production-grade reference architecture that runs the **same REST API in four
languages** — JavaScript, Python, pure C, and pure C++ — side-by-side on AWS
Fargate behind a single ALB, then load- and chaos-tests them so you can compare
runtime characteristics on identical workloads.

The four implementations share an identical contract: same endpoints, same
Postgres schema, same Redis cache-aside semantics, same edge-case behaviour.
Differences in measured throughput and tail latency therefore measure the
runtime / framework, not the design.

## Architecture

```
              ┌─────────────────────────────────────────────────────┐
              │                       Internet                       │
              └────────────────────────┬─────────────────────────────┘
                                       │
                                  ┌────▼────┐
                                  │  WAF v2 │  (managed SQLi + Common
                                  │  Web ACL│   + rate-based rule)
                                  └────┬────┘
                                       │
                  Route53 ALIAS  ┌─────▼─────┐
                  api.example.com│    ALB    │   80 → 301 → 443 (ACM/TLS 1.3)
                                 │ (public)  │
                                 └─────┬─────┘
                                       │ path-routing
            ┌────────────┬─────────────┼─────────────┬────────────┐
       /api/js/*    /api/python/*  /api/c/*     /api/cpp/*
            │            │              │             │
       ┌────▼───┐   ┌────▼───┐    ┌────▼───┐    ┌────▼───┐
       │TG js   │   │TG python│    │TG c    │    │TG cpp  │
       └────┬───┘   └────┬───┘    └────┬───┘    └────┬───┘
            │            │              │             │
       ┌────▼─────────────────────────────────────────▼────┐
       │       ECS Fargate cluster (private subnets)        │
       │  ┌────────┐ ┌────────┐ ┌────────┐ ┌────────┐      │
       │  │ js x N │ │ py x N │ │ c x N  │ │ cpp xN │      │ ← CPU autoscale
       │  └────┬───┘ └────┬───┘ └────┬───┘ └────┬───┘      │
       └───────┼──────────┼──────────┼──────────┼──────────┘
               │          │          │          │
               └──────┬───┴──────────┴──────┬───┘
                      ▼                      ▼
              ┌───────────────┐      ┌──────────────────┐
              │  Postgres RDS │      │ ElastiCache Redis│
              │  (Multi-AZ,   │      │  (private)       │
              │   private)    │      │                  │
              └───────────────┘      └──────────────────┘

       Secrets Manager  → DB credentials  → ECS task definitions
       CloudWatch Logs  → /ecs/<project>/<lang>
       CloudWatch Dashboard → CPU, memory, request rate, 5XX, latency
```

## Repository layout

```
apps/
  js/        Node.js + Express    (Jest tests)
  python/    FastAPI + Uvicorn    (pytest tests)
  c/         mongoose + libpq + hiredis  (Unity tests)
  cpp/       Crow + libpqxx + hiredis    (GoogleTest tests)
db/init.sql                 shared schema (also runs CREATE TABLE IF NOT EXISTS at app startup)
docker-compose.yml          local stack: 4 apps + Postgres + Redis
terraform/
  modules/
    vpc/                    2-AZ VPC, 2 public + 2 private subnets, NATs
    security-groups/        tight ALB/ECS/RDS/Redis SGs
    rds/                    Postgres (Multi-AZ, encrypted)
    elasticache/            Redis
    waf/                    WAFv2 Web ACL (SQLi + Common + rate-limit)
    alb/                    ALB, 4 target groups, HTTP→HTTPS + ACM, listener rules
    ecs/                    cluster, ECR repos, task defs, services, autoscale, Secrets Manager
    monitoring/             CloudWatch dashboard + alarms
  environments/prod/        root composition wiring every module
  scripts/setup-backend.sh  bootstraps S3 + DynamoDB for remote state
benchmark/
  run_tests.sh              k6 load test (per-language)
  chaos_test.sh             malformed/oversized/SQLi/rate-limit probes
security/.trivyignore       accepted-finding allowlist
.github/workflows/deploy.yml  test → Trivy → build → push → roll
CHANGELOG.md, TRACKER.md, BENCHMARK_RESULTS.md
```

## REST contract (identical for all four services)

| Method | Path                       | Behaviour                                                                 |
|--------|----------------------------|---------------------------------------------------------------------------|
| GET    | `/health`                  | Liveness — no DB/cache calls. Used by the ALB target group health check. |
| GET    | `/api/<lang>/data`         | List all rows (cache-aside on `data:all`).                                |
| GET    | `/api/<lang>/data/<id>`    | Get one row (cache-aside on `data:<id>`).                                 |
| POST   | `/api/<lang>/data`         | Insert `{"content":"..."}`; invalidate `data:all`.                        |

Edge cases (each app must satisfy all of these — covered by unit + integration tests):

- Malformed JSON body → **400**.
- Missing/empty `content` → **400**.
- Body > `MAX_BODY_BYTES` → **413**.
- Redis down / timed out → **200**, `"source":"db"` (never 5xx because of Redis).
- Postgres down → **503** with JSON error; process stays alive and reconnects.
- Invalid id (non-positive integer) → **400**.
- Missing `X-API-Key` header on any `/api/<lang>/*` route → **401** `{"error":"missing api key"}`.
- Wrong `X-API-Key` → **401** `{"error":"invalid api key"}`.
- `/health` is intentionally **unauthenticated** so the ALB target-group health check stays public.

### Authentication

Two layers, both required on `/api/<lang>/*`:

1. **API key** (`X-API-Key` header) — service-to-service gate. Value comes
   from `API_KEY` env (Secrets Manager in prod). Empty disables (test only).
2. **JWT session cookie** — end-user gate. Issued by `POST /api/<lang>/auth/login`
   after bcrypt password check, stored as an HttpOnly + SameSite=Strict +
   Secure cookie named `session`. Signed HS256 with `JWT_SECRET` (Secrets
   Manager in prod). 1-hour TTL.

`/health` is exempt from both layers — the ALB target-group health check
stays public.

**Auth endpoints (per language):**
- `POST /api/<lang>/auth/login` — body `{"email","password"}` → 200 + Set-Cookie,
  or 401 `{"error":"invalid credentials"}`.
- `POST /api/<lang>/auth/logout` → 204 + clear cookie.
- `GET  /api/<lang>/auth/me` → 200 with the current user, or 401.

**Role gate on POST `/api/<lang>/data`:** user must have `writer` or `admin`
in their roles array, otherwise 403 `{"error":"forbidden"}`.

The `users` table is populated by the migration tool's `seed-admin` command;
docker-compose seeds a default admin (`admin@local` / `supersecret`).

#### Rotating the API key

`api_key` and `api_key_next` are both stored in Secrets Manager and injected
as `API_KEY` and `API_KEY_NEXT`. The auth middleware (in every language)
accepts an `X-API-Key` that matches **either** value, so rotation can be
zero-downtime:

```bash
# 1. Mint a new key and stage it alongside the current one.
openssl rand -hex 32   # → e.g. 9f3a...
# In terraform.tfvars:
#   api_key      = "<current>"
#   api_key_next = "9f3a..."
cd terraform/environments/prod && terraform apply

# 2. ECS rolls all four services (~1–2 min). Both keys are now valid.
#    Update every caller to send the new key.

# 3. Once no clients use the old key, swap and clear:
#   api_key      = "9f3a..."
#   api_key_next = ""
cd terraform/environments/prod && terraform apply

# 4. Wait for the roll. Only the new key is accepted now.
```

The `check_api_key_dual` pure helper in each language is unit-tested for
this contract: primary-match, secondary-match, neither-matches, only-secondary,
both-empty-disabled.

## Getting started

There are two supported ways to bring the stack up locally. The Docker path
is recommended — it spins up Postgres, Redis, and all four language services
behind a single command and mirrors the production environment variables.

### Prerequisites

- **Docker path** (recommended): Docker 20.10+ with the Compose v2 plugin.
- **Native path**: PostgreSQL 14+ and Redis 6+ running locally, plus the
  toolchain for whichever language you want to run (Node 20, Python 3.12,
  `build-essential` + `libpq-dev` + `libhiredis-dev` for C, the same plus
  `cmake` + `libpqxx-dev` for C++).
- **Network**: the default ports 5432 (Postgres), 6379 (Redis), and 8081-8084
  (per-language apps) must be free.

### Quick start — `docker compose` (all four services + Postgres + Redis)

```bash
# from the repository root
docker compose up --build           # add -d to detach

# in another terminal — verify each service is healthy (no key needed for /health)
curl http://localhost:8081/health   # JS      → {"status":"ok","lang":"js"}
curl http://localhost:8082/health   # Python  → {"status":"ok","lang":"python"}
curl http://localhost:8083/health   # C       → {"status":"ok","lang":"c"}
curl http://localhost:8084/health   # C++     → {"status":"ok","lang":"cpp"}

# /api/<lang>/* requires X-API-Key (service gate) + a session cookie (user gate).
# docker-compose sets API_KEY=local-dev-key. The migrate service seeds admin@local.
KEY=local-dev-key

# 1. Log in to obtain a session cookie.
curl -c /tmp/cookies -X POST http://localhost:8081/api/js/auth/login \
     -H "X-API-Key: $KEY" \
     -H 'content-type: application/json' \
     -d '{"email":"admin@local","password":"supersecret"}'
# → {"user":{"id":1,"email":"admin@local","roles":["admin","writer","reader"]}}

# 2. Use the cookie + the API key on data routes.
curl -b /tmp/cookies -H "X-API-Key: $KEY" \
     -X POST http://localhost:8081/api/js/data \
     -H 'content-type: application/json' \
     -d '{"content":"hello world"}'
# → 201 {"item":{...}}

# 3. List rows (first call → "source":"db", second call → "source":"cache")
curl -b /tmp/cookies -H "X-API-Key: $KEY" http://localhost:8081/api/js/data
curl -b /tmp/cookies -H "X-API-Key: $KEY" http://localhost:8081/api/js/data

# 4. Logout.
curl -b /tmp/cookies -H "X-API-Key: $KEY" \
     -X POST http://localhost:8081/api/js/auth/logout

# Without the API key:   → 401 {"error":"missing api key"}
# With key but no cookie → 401 {"error":"authentication required"}
# With reader-only role on POST → 403 {"error":"forbidden"}

# tear everything down
docker compose down                 # add -v to drop the Postgres volume too
```

Port map: 8081 = JS, 8082 = Python, 8083 = C, 8084 = C++. Postgres on 5432, Redis on 6379. All four apps share the same Postgres database and the same Redis instance, so rows you create through one language are immediately visible to the others. Each app runs `CREATE TABLE IF NOT EXISTS` on startup, so no manual migration step is needed.

### Native path — running a single service without Docker

Useful when iterating on one language. Start Postgres and Redis manually,
create the database/user, then export the env vars and run the app.

```bash
# 1. Start Postgres + Redis (commands depend on your distro/OS package).
sudo service postgresql start
sudo service redis-server start

# 2. Create the database + user the apps expect.
sudo -u postgres psql <<'SQL'
CREATE USER appuser WITH PASSWORD 'apppass';
CREATE DATABASE appdb OWNER appuser;
GRANT ALL PRIVILEGES ON DATABASE appdb TO appuser;
SQL

# 3. Apply the schema (apps also create the table on startup; this is just optional).
PGPASSWORD=apppass psql -h 127.0.0.1 -U appuser -d appdb -f db/init.sql

# 3.5. Apply migrations + seed an admin user (canonical: db/migrate.sh).
PGHOST=127.0.0.1 PGPORT=5432 PGUSER=appuser PGPASSWORD=apppass PGDATABASE=appdb \
    pip install --quiet bcrypt && \
    ./db/migrate.sh up && \
    ./db/migrate.sh seed-admin --email admin@local --password supersecret

# 4. Set the env vars used by every language.
export DB_HOST=127.0.0.1 DB_PORT=5432 DB_NAME=appdb DB_USER=appuser DB_PASSWORD=apppass
export REDIS_HOST=127.0.0.1 REDIS_PORT=6379
export API_KEY=local-dev-key                                # service-to-service gate
export JWT_SECRET=local-dev-jwt-secret-change-in-prod       # signs session cookie
export COOKIE_SECURE=false                                   # set true only over HTTPS
export ACCESS_LOG_PATH=./access.log                          # rotating per-request log

# 5. Run whichever app you want. Each listens on $PORT and answers /api/$APP_LANG/...
# --- JavaScript ---
cd apps/js && npm install
PORT=8081 APP_LANG=js node src/server.js

# --- Python ---
cd apps/python && python3 -m venv .venv && . .venv/bin/activate
pip install -r requirements.txt
PORT=8082 APP_LANG=python python -m uvicorn app.main:app --host 0.0.0.0 --port 8082

# --- C ---
cd apps/c && make server
PORT=8083 APP_LANG=c ./server

# --- C++ ---
cd apps/cpp && cmake -S . -B build && cmake --build build --target server -j
PORT=8084 APP_LANG=cpp ./build/server
```

### Verifying the Redis-down fallback

The contract says reads must keep returning **200** even if Redis is down.
You can prove this against a running stack:

```bash
# with the apps up, stop redis and confirm reads still succeed
docker compose stop redis             # or: sudo service redis-server stop
curl -i http://localhost:8081/api/js/data   # → HTTP/1.1 200 OK, "source":"db"
docker compose start redis            # or: sudo service redis-server start
```

## Running the test suite locally

```bash
# JavaScript    (Jest + supertest)        73 tests
cd apps/js     && npm install && npm test
# Python        (PyTest + httpx)          70 tests
cd apps/python && pip install -r requirements.txt -r requirements-dev.txt && pytest -q
# C             (Unity)                   44 tests
cd apps/c      && make test
# C++           (GoogleTest)              47 tests
cd apps/cpp    && cmake -S . -B build && cmake --build build --target unit_tests -j && ./build/unit_tests
```

The CI pipeline (`.github/workflows/deploy.yml`) runs all four suites plus
`terraform validate`/`fmt -check` plus Trivy filesystem and IaC scans, and
**blocks image build/push if any of them fails**.

## Deploying

```bash
# 1. One-time: bootstrap the remote state backend.
./terraform/scripts/setup-backend.sh ml-ecs-benchmark us-east-1

# 2. Update the backend block in terraform/environments/prod/providers.tf with
#    the bucket and table names printed by the script.

# 3. Fill terraform.tfvars (gitignored) from the example.
cp terraform/environments/prod/terraform.tfvars.example \
   terraform/environments/prod/terraform.tfvars
$EDITOR terraform/environments/prod/terraform.tfvars
#    - domain_name + route53_zone_id MUST be set (ACM cert is DNS-validated).
#    - db_password MUST be set.

# 4. Apply.
cd terraform/environments/prod
terraform init
terraform apply

# 5. Push container images. On main, CI does this automatically. For an
#    initial bootstrap, `docker build && docker push` against the ECR repo
#    URLs in `terraform output ecr_repository_urls`, then roll the services:
aws ecs update-service --cluster ml-ecs-benchmark --service ml-ecs-benchmark-js  --force-new-deployment
# ...repeat for python, c, cpp.

# 6. Apply DB migrations. On main this runs as the `migrate` CI job (built
#    from apps/migrator and invoked via `aws ecs run-task`). For an initial
#    bootstrap, push the migrator image too and run it manually:
docker build -t "$(terraform output -raw migrator_ecr_repository_url):latest" \
             -f apps/migrator/Dockerfile .
docker push "$(terraform output -raw migrator_ecr_repository_url):latest"
SUBNETS=$(aws ssm get-parameter --name "/ml-ecs-benchmark/migrator/subnets"        --query Parameter.Value --output text)
SG=$(aws ssm get-parameter      --name "/ml-ecs-benchmark/migrator/security-group" --query Parameter.Value --output text)
aws ecs run-task --cluster ml-ecs-benchmark --launch-type FARGATE \
  --task-definition "$(terraform output -raw migrator_task_definition_family)" \
  --network-configuration "awsvpcConfiguration={subnets=[\"${SUBNETS//,/\",\"}\"],securityGroups=[\"$SG\"],assignPublicIp=DISABLED}"
```

### GitHub Actions setup (one-time)

The CI workflow assumes AWS access via OIDC — no long-lived AWS keys are
stored in GitHub. Create an OIDC provider + role in your AWS account, then
populate the following GitHub secrets / vars on the repo:

| Kind   | Name                       | Value                                                                                   |
|--------|----------------------------|-----------------------------------------------------------------------------------------|
| Secret | `AWS_DEPLOY_ROLE_ARN`      | ARN of the IAM role CI assumes (trust policy below).                                    |
| Secret | `TF_DB_PASSWORD`           | Same value as `db_password` in terraform.tfvars (used by the `terraform plan` job).     |
| Secret | `TF_API_KEY`               | Same as `api_key`.                                                                      |
| Secret | `TF_JWT_SECRET`            | Same as `jwt_secret`.                                                                   |
| Var    | `TF_DOMAIN_NAME`           | Same as `domain_name`.                                                                  |
| Var    | `TF_ROUTE53_ZONE_ID`       | Same as `route53_zone_id`.                                                              |
| Var    | `RUN_TERRAFORM_PLAN`       | Set to `"true"` to enable the `terraform plan` job on pull requests.                    |

Trust policy for the deploy role (replace `<ACCOUNT_ID>`, `<OWNER>`, `<REPO>`):

```json
{
  "Version": "2012-10-17",
  "Statement": [{
    "Effect": "Allow",
    "Principal": { "Federated": "arn:aws:iam::<ACCOUNT_ID>:oidc-provider/token.actions.githubusercontent.com" },
    "Action": "sts:AssumeRoleWithWebIdentity",
    "Condition": {
      "StringEquals": {
        "token.actions.githubusercontent.com:aud": "sts.amazonaws.com"
      },
      "StringLike": {
        "token.actions.githubusercontent.com:sub": "repo:<OWNER>/<REPO>:ref:refs/heads/main"
      }
    }
  }]
}
```

Minimum permissions the role needs:

- `AmazonEC2ContainerRegistryFullAccess` (push to ECR + run image scans)
- `AmazonECS_FullAccess` (run-task for migrations, update-service for rolls,
  wait services-stable, describe-tasks)
- `ssm:GetParameter` on `/<project>/migrator/*`
- For the `terraform plan` job only: read access on the resources the modules
  manage (VPC, RDS, ElastiCache, ALB, WAFv2, IAM, Secrets Manager, S3, Route53).
  In practice attach `ReadOnlyAccess` plus write on the project's TF state
  bucket.

## Running the benchmark and chaos suite

**Two modes**: production (single ALB endpoint) or local (per-language docker-compose ports).

```bash
# --- Cross-language comparison ---
# Production (one ALB):
BASE_URL=https://api.example.com API_KEY=<key> ./benchmark/run_compare.sh
# Local (docker-compose up first):
LOCAL=1 API_KEY=local-dev-key ./benchmark/run_compare.sh

# --- One language at a time ---
LOCAL=1 API_KEY=local-dev-key ./benchmark/run_js.sh
LOCAL=1 API_KEY=local-dev-key ./benchmark/run_python.sh
LOCAL=1 API_KEY=local-dev-key ./benchmark/run_c.sh
LOCAL=1 API_KEY=local-dev-key ./benchmark/run_cpp.sh

# --- Chaos / abuse probes ---
LOCAL=1 API_KEY=local-dev-key ./benchmark/chaos_test.sh
```

Every runner authenticates once via `/api/<lang>/auth/login` (with `EMAIL` /
`PASSWORD`, defaults `admin@local` / `supersecret`), captures the session
cookie, and uses it on every subsequent request. The cross-language script
emits `./results/summary-<timestamp>.csv` with throughput and p95/p99
latency per language; `run_compare.sh` additionally prints a ranked table.

`run_tests.sh` produces `results/summary-<timestamp>.csv` with per-language
throughput and p95/p99 latency. `chaos_test.sh` verifies edge-case handling
and that the WAF rate limit actually fires.

Paste the numbers into `BENCHMARK_RESULTS.md`.

## Security posture

- **Network:** RDS, ElastiCache, and every ECS task live in private subnets.
  Only the ALB is internet-facing.
- **Security groups:** ALB → ECS only; ECS → RDS:5432 only; ECS → Redis:6379 only.
- **TLS:** ACM-issued certificate; ALB serves only TLS 1.3 (`ELBSecurityPolicy-TLS13-1-2-2021-06`);
  HTTP 80 redirects to HTTPS 443.
- **WAF:** AWS managed Common rules + SQLi rules + a rate-based rule.
- **Secrets:** DB password lives in AWS Secrets Manager and is injected into
  task definitions via the `secrets` block; never baked into the image.
- **CI gate:** Trivy filesystem, IaC, and image scans must pass before push.
  S3 state bucket is versioned, encrypted, and public-access-blocked.

## Tracking

- [`TRACKER.md`](TRACKER.md) — phase-by-phase task checklist with UTC timestamps.
- [`CHANGELOG.md`](CHANGELOG.md) — semantic-versioned change log; currently 1.0.0.
- [`BENCHMARK_RESULTS.md`](BENCHMARK_RESULTS.md) — to be filled after a live run.
