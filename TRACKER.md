# Project Tracker

Strict checklist for the Advanced Multi-Language AWS ECS Benchmark & Testing Suite.
Timestamps are UTC, recorded at phase completion.

## Phase 0: Project Setup, State & Tracking
- [x] Create folder structure (apps, terraform, tests, benchmark, security) - 2026-05-15 00:10 UTC
- [x] Write README.md outlining the project - 2026-05-15 00:10 UTC
- [x] Initialize CHANGELOG.md - 2026-05-15 00:10 UTC
- [x] Initialize TRACKER.md with full checklist - 2026-05-15 00:10 UTC
- [x] Write terraform/environments/prod/providers.tf - 2026-05-15 00:10 UTC
- [x] Write S3/DynamoDB state backend bootstrap script - 2026-05-15 00:10 UTC
- [x] Define shared DB schema (db/init.sql) - 2026-05-15 00:10 UTC

## Phase 1: Application Logic, Caching Suite, & Edge Cases
- [x] Define REST API contract - 2026-05-15 00:10 UTC
- [x] Define application edge cases - 2026-05-15 00:10 UTC
- [x] JavaScript app (Node.js/Express) + Dockerfile - 2026-05-15 01:00 UTC
- [x] Python app (FastAPI/Uvicorn) + Dockerfile - 2026-05-15 01:00 UTC
- [x] C app (mongoose + libpq + hiredis) + Dockerfile - 2026-05-15 01:00 UTC
- [x] C++ app (Crow + libpqxx + redis-plus-plus) + Dockerfile - 2026-05-15 01:00 UTC
- [x] Redis-down graceful fallback to RDS in all 4 apps - 2026-05-15 01:00 UTC
- [x] Input validation / malformed JSON / payload size limits in all 4 apps - 2026-05-15 01:00 UTC
- [x] Unified docker-compose.yml (4 apps + Postgres + Redis) - 2026-05-15 01:00 UTC

## Phase 1.5: The Testing Suite (Unit & Integration)
- [x] JS unit + integration tests (Jest) - 2026-05-15 01:00 UTC
- [x] Python unit + integration tests (PyTest) - 2026-05-15 01:00 UTC
- [x] C unit tests (Unity) - 2026-05-15 01:00 UTC
- [x] C++ unit + integration tests (GoogleTest) - 2026-05-15 01:00 UTC
- [x] Integration tests assert Redis-timeout falls back to DB with 200 OK - 2026-05-15 01:00 UTC

## Phase 2: Networking Foundation (VPC)
- [x] terraform/modules/vpc (2-AZ, 2 public + 2 private subnets, NAT) - 2026-05-15 01:30 UTC
- [x] VPC module outputs (vpc_id, subnet IDs) - 2026-05-15 01:30 UTC

## Phase 3: Database & Persistent Storage (RDS & ElastiCache)
- [x] terraform/modules/security-groups (tightly scoped) - 2026-05-15 01:30 UTC
- [x] terraform/modules/rds (Postgres) - 2026-05-15 01:30 UTC
- [x] terraform/modules/elasticache (Redis) - 2026-05-15 01:30 UTC

## Phase 4: Load Balancing & SSL
- [x] terraform/modules/waf (SQLi + rate limiting) - 2026-05-15 01:30 UTC
- [x] terraform/modules/alb (ALB + 4 target groups + path routing) - 2026-05-15 01:30 UTC
- [x] HTTP -> HTTPS redirect + ACM certificate attached - 2026-05-15 01:30 UTC

## Phase 5: Compute & Orchestration (ECS Fargate)
- [x] terraform/modules/ecs - 2026-05-15 01:30 UTC
- [x] 4 ECR repositories, 1 ECS cluster, 4 Fargate services - 2026-05-15 01:30 UTC
- [x] Secrets Manager for DB/Redis credentials - 2026-05-15 01:30 UTC
- [x] CPU-based auto-scaling per service - 2026-05-15 01:30 UTC
- [x] CloudWatch log groups per container - 2026-05-15 01:30 UTC

## Phase 6: CI/CD Pipeline
- [x] terraform/environments/prod/main.tf wiring all modules - 2026-05-15 02:00 UTC
- [x] .github/workflows/deploy.yml with test + Trivy gating - 2026-05-15 02:00 UTC

## Phase 7: The Benchmark & Chaos Suite
- [x] benchmark/run_tests.sh (k6 load test, per-language) - 2026-05-15 02:00 UTC
- [x] benchmark/chaos_test.sh (malformed payloads, huge blobs, WAF rate limits) - 2026-05-15 02:00 UTC

## Phase 8: Monitoring, Logging, & Final Analysis
- [x] terraform/modules/monitoring (CloudWatch dashboard) - 2026-05-15 02:00 UTC
- [x] BENCHMARK_RESULTS.md (stub — to be filled after a live run) - 2026-05-15 02:00 UTC
- [x] Polish README.md with architecture diagram - 2026-05-15 02:00 UTC
- [x] Seal CHANGELOG.md as [1.0.0] - Production Ready - 2026-05-15 02:00 UTC

## Post-1.0: API-Key Authentication
- [x] JS middleware in createApp + 6 new tests - 2026-05-15 18:00 UTC
- [x] Python middleware in create_app + 7 new tests - 2026-05-15 18:00 UTC
- [x] C check_api_key + dispatch guard + 5 new Unity tests - 2026-05-15 18:00 UTC
- [x] C++ check_api_key + Crow route guard + 5 new GoogleTest tests - 2026-05-15 18:00 UTC
- [x] ECS Terraform: API_KEY injected via Secrets Manager - 2026-05-15 18:00 UTC
- [x] Root tfvars.example: api_key variable - 2026-05-15 18:00 UTC
- [x] docker-compose.yml: API_KEY=local-dev-key on all services - 2026-05-15 18:00 UTC
- [x] Benchmark + chaos scripts: X-API-Key + no-key/wrong-key probes - 2026-05-15 18:00 UTC
- [x] README + CHANGELOG (1.1.0) updates - 2026-05-15 18:00 UTC

## Post-1.0: Migrations + JWT cookie auth + Access logging + Security infra
- [x] db/migrations/{V001__init,V002__users}.sql - 2026-05-16 00:00 UTC
- [x] db/migrate.sh with up/status/seed-admin - 2026-05-16 00:00 UTC
- [x] docker-compose migrate one-shot service + JWT/COOKIE env vars - 2026-05-16 00:00 UTC
- [x] JS: auth.js + access_log.js + login/logout/me + role gate + tests (60 total) - 2026-05-16 00:00 UTC
- [x] Python: auth.py + access_log.py + login/logout/me + role gate + tests (57 total) - 2026-05-16 00:00 UTC
- [x] C: HS256 via OpenSSL HMAC + crypt_r bcrypt + cookie parsing + rotating log + tests (31 total) - 2026-05-16 00:00 UTC
- [x] C++: HS256 via OpenSSL + crypt_r bcrypt + Crow routes + rotating log + tests (32 total) - 2026-05-16 00:00 UTC
- [x] Live smoke test of C and C++ auth end-to-end against Postgres + Redis - 2026-05-16 00:00 UTC
- [x] ElastiCache: at-rest + in-transit encryption (replication_group) - 2026-05-16 00:00 UTC
- [x] ALB access logs to encrypted S3 bucket - 2026-05-16 00:00 UTC
- [x] WAF logging to CloudWatch log group - 2026-05-16 00:00 UTC
- [x] VPC flow logs to CloudWatch log group + IAM role - 2026-05-16 00:00 UTC
- [x] JWT_SECRET injected through Secrets Manager into every ECS task - 2026-05-16 00:00 UTC
- [x] CI: terraform plan (prod) job for PR rehearsal - 2026-05-16 00:00 UTC
- [x] README + CHANGELOG 1.2.0 + TRACKER updated - 2026-05-16 00:00 UTC

## Post-1.0: Redis TLS, Edge cases, Per-language benchmarks (1.3.0)
- [x] JS cache: socket.tls flag + env wiring - 2026-05-16 01:00 UTC
- [x] Python cache: ssl=True + ssl_cert_reqs - 2026-05-16 01:00 UTC
- [x] C cache: hiredis_ssl init + SSL context + handshake - 2026-05-16 01:00 UTC
- [x] C++ cache: same via std::call_once + redisInitiateSSLWithContext - 2026-05-16 01:00 UTC
- [x] Edge cases per language (unicode, boundary body, malformed JWT, b64url invalid chars, case-insensitive headers) - 2026-05-16 01:00 UTC
- [x] benchmark/run_tests.sh supports LOCAL=1 + per-language URLs + auth login - 2026-05-16 01:00 UTC
- [x] benchmark/run_{js,python,c,cpp}.sh per-language wrappers - 2026-05-16 01:00 UTC
- [x] benchmark/run_compare.sh ranks all four by req/sec - 2026-05-16 01:00 UTC
- [x] benchmark/chaos_test.sh updated for LOCAL=1 - 2026-05-16 01:00 UTC
- [x] Test totals: 70 JS / 67 Py / 39 C / 42 C++ = 218 - 2026-05-16 01:00 UTC

## Post-1.0: CI migrations + OIDC docs + admin parameterization (1.4.0)
- [x] apps/migrator/Dockerfile - 2026-05-16 02:00 UTC
- [x] ECS migrator task definition + ECR repo + SSM params - 2026-05-16 02:00 UTC
- [x] CI `migrate` job between build-and-push and deploy - 2026-05-16 02:00 UTC
- [x] README: GitHub Actions setup + OIDC trust policy + bootstrap recipe - 2026-05-16 02:00 UTC
- [x] docker-compose admin user parameterized (ADMIN_EMAIL/ADMIN_PASSWORD) - 2026-05-16 02:00 UTC
