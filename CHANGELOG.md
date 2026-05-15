# Changelog

All notable changes to this project are documented here.
Format follows [Keep a Changelog](https://keepachangelog.com/).

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
