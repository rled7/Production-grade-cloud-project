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
