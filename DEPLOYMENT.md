# Deployment guide

Pre-flight checklist and known gaps for an actual AWS deployment. Pair this
with the **Deploying** and **GitHub Actions setup** sections of the main
`README.md`.

## Pre-deploy checklist (item #6 from the audit)

Before the first `terraform apply`, you need every box below ticked. Most of
them only need to happen once per AWS account.

### AWS account

- [ ] **An AWS account** with admin (or equivalent) IAM access for the
      bootstrap. CI assumes a role via OIDC after this.
- [ ] **A Route53 hosted zone** for the public domain you intend to use
      (`prod` and `staging` both need a fully-qualified domain name and that
      domain's parent zone ID). The ACM cert is DNS-validated against this
      zone. Without it, `terraform apply` blocks on cert validation.
- [ ] **Service quota headroom** in the target region:
        - At least **2 NAT gateways** per environment (default limit is 5).
        - At least **8 Fargate tasks** running concurrently per env
          (4 services × `desired_count=2` in prod).
        - **2 RDS instances** (one per env if you deploy both).
        - **2 ElastiCache replication groups**.
        - **5 ECR repositories** per env (js, python, c, cpp, migrator).
- [ ] **AWS CLI configured** locally for the bootstrap user
      (`aws sts get-caller-identity` must work).

### Repository / Git

- [ ] **`terraform/scripts/setup-backend.sh` run once** — creates the S3
      state bucket and DynamoDB lock table.
- [ ] **`terraform/environments/prod/providers.tf`** backend block updated
      with the bucket + table names printed by the script.
      (Same for `staging/providers.tf` if you deploy staging.)
- [ ] **`terraform.tfvars`** created from the example in each environment.
      Filled-in fields: `domain_name`, `route53_zone_id`, `db_password`,
      `api_key`, `jwt_secret`, `admin_email`, `admin_password`.
      (gitignored — never commit.)

### GitHub Actions

These mirror the table in the README's *GitHub Actions setup* section.
Without them, CI's `migrate`, `build-and-push`, `deploy`, and (optionally)
`terraform plan` jobs will fail on first push.

- [ ] **OIDC provider** for GitHub Actions created in your AWS account
      (`token.actions.githubusercontent.com`).
- [ ] **IAM role** with the trust policy from the README, granting at least:
      `AmazonEC2ContainerRegistryFullAccess`, `AmazonECS_FullAccess`,
      `ssm:GetParameter` on `/<project>/migrator/*`. (For `terraform plan`:
      add `ReadOnlyAccess` + write on the TF state bucket.)
- [ ] **Repo secrets** set: `AWS_DEPLOY_ROLE_ARN`, `TF_DB_PASSWORD`,
      `TF_API_KEY`, `TF_JWT_SECRET`.
- [ ] **Repo vars** set: `TF_DOMAIN_NAME`, `TF_ROUTE53_ZONE_ID`,
      `RUN_TERRAFORM_PLAN` (= `"true"` to enable the PR plan job).

### First deploy: solve the ECR chicken-and-egg

ECR repos are `image_tag_mutability = "IMMUTABLE"` and the ECS task
definitions reference `${ecr}:${image_tag}` — but with no images pushed,
services would stay PENDING forever on first apply.

**Use the bootstrap helper:**

```bash
ENV=prod    AWS_REGION=us-east-1 ./terraform/scripts/bootstrap-deploy.sh
ENV=staging AWS_REGION=us-east-1 ./terraform/scripts/bootstrap-deploy.sh
```

The script:
1. `terraform apply -target` to create the ECR repos only.
2. `docker build` + `docker push` an initial image (tagged `:bootstrap`
   and the current git SHA) for js / python / c / cpp / migrator.
3. Full `terraform apply -var image_tag=bootstrap` — services come up
   pulling the `:bootstrap` image.
4. CI takes over on the next push to `main`: it builds the SHA-tagged
   image, registers a new task-definition revision pointing at the SHA,
   and updates the service.

After that, every push to `main` deploys via the CI workflow without
manual intervention.

## Known unknowns before going live (item #4 from the audit)

These have been wired up but **never actually run against AWS** from this
sandbox. Verify them locally or with a throwaway AWS account first.

### Trivy scans

CI runs Trivy on the filesystem, on the Terraform IaC, and on each built
image, failing on `CRITICAL` or `HIGH`. The base images we depend on
(`node:20-slim`, `python:3.12-slim`, `debian:bookworm-slim`,
`python:3.12-slim` for the migrator) refresh often — fresh CVEs can show
up and break CI without warning.

**Recommended local verification before pushing to main the first time:**

```bash
# Per service:
for lang in js python c cpp; do
    docker build -t scan:$lang apps/$lang
    trivy image --severity CRITICAL,HIGH --ignore-unfixed scan:$lang
done
docker build -t scan:migrator -f apps/migrator/Dockerfile .
trivy image --severity CRITICAL,HIGH --ignore-unfixed scan:migrator

# Filesystem + IaC:
trivy fs     --severity CRITICAL,HIGH --ignore-unfixed .
trivy config --severity CRITICAL,HIGH terraform/
```

If something flags, either pin the offending package to a fixed version,
bump the base image, or add a *time-bounded* entry to `security/.trivyignore`
with the rationale.

### `terraform plan` against the real account

`terraform validate` (which CI gates on) does NOT call AWS. It catches
syntax + cross-module wiring but misses:

- IAM trust-policy syntax issues that AWS rejects at apply time.
- ACM cert region constraints (CloudFront certs must be in `us-east-1`).
- Reserved characters in resource names / SSM parameter paths.
- Stale Terraform state lock from a half-aborted previous run.
- ElastiCache parameter-group version drift.

The `terraform-plan` CI job runs against the real account only when
`vars.RUN_TERRAFORM_PLAN == "true"`. **Strongly recommend running
`terraform -chdir=terraform/environments/staging plan` once locally with
real AWS creds before the first apply** — it costs nothing and reveals
provider-side surprises cheaply.

### Cost surprises

Rough monthly burn (us-east-1, on-demand pricing, no traffic):

| Component | Prod | Staging |
|---|---|---|
| ALB (base) | ~$16 | ~$16 |
| 2× NAT gateway | ~$65 | ~$65 |
| RDS db.t3.micro Multi-AZ (prod) / Single-AZ (staging) | ~$30 | ~$15 |
| ElastiCache cache.t3.micro replication group | ~$12 | ~$12 |
| 4× Fargate services @ desired_count (256 CPU / 512 MiB) | ~$30 (×2) | ~$15 (×1) |
| WAF Web ACL | ~$5 + per-request | ~$5 |
| CloudWatch logs + dashboards | ~$5–15 | ~$5 |
| **Subtotal** | **~$170** | **~$135** |

NAT is the biggest line. If cost matters, collapse to one NAT gateway or
add VPC endpoints for `ssm`, `secretsmanager`, `ecr.api`, `ecr.dkr`,
`s3`, `logs` so the tasks don't egress through NAT for those calls.

## Item-specific deferred work

These are tracked here so they don't get lost. None block a first deploy.

- **CloudFront in front of ALB** — global cache + edge SSL. Not wired.
- **Custom application metrics** (request count per endpoint, cache
  hit-rate, failed auth attempts) — apps don't emit any custom metrics
  beyond what CloudWatch Container Insights provides.
- **More CloudWatch alarms** — currently only CPU per service and ALB
  5XX. Worth adding: RDS connection count, RDS free storage, cache hit
  rate, WAF blocked-request rate, ACM cert expiry.
- **WAF rate-limit tuning** — default 2000 reqs/5min/IP is generous.
  Tighten after observing real traffic patterns.
- **DR runbook** — RDS has 7-day automated backups but no documented
  restore procedure.
- **Container hardening** — Dockerfiles run as non-root but task
  definitions could add `readonlyRootFilesystem`, dropped capabilities,
  and seccomp profiles.

## Smoke test after deploy

Once `terraform apply` and the first CI deploy both complete:

```bash
DOMAIN="https://api.example.com"   # whatever you set as var.domain_name
KEY="<api_key from terraform.tfvars>"

# 1. Health check (unauthenticated).
curl -i $DOMAIN/health

# 2. Login as the seeded admin.
curl -i -c /tmp/c -X POST $DOMAIN/api/js/auth/login \
     -H "X-API-Key: $KEY" -H 'content-type: application/json' \
     -d '{"email":"admin@example.com","password":"<admin_password>"}'

# 3. Hit a protected route.
curl -i -b /tmp/c -H "X-API-Key: $KEY" $DOMAIN/api/js/auth/me

# 4. Round-trip data.
curl -i -b /tmp/c -X POST $DOMAIN/api/js/data \
     -H "X-API-Key: $KEY" -H 'content-type: application/json' \
     -d '{"content":"first prod write"}'

# 5. Run chaos against a single language with a tiny rate-limit flood.
BASE_URL=$DOMAIN API_KEY=$KEY \
  LANGUAGES=js RATE_LIMIT_FLOOD=50 \
  ./benchmark/chaos_test.sh

# 6. Speed compare once everything looks healthy.
BASE_URL=$DOMAIN API_KEY=$KEY EMAIL=admin@example.com \
  PASSWORD=<admin_password> ./benchmark/run_compare.sh
```

If any of those steps fail, check in this order:
- ALB target group health (in the AWS console: EC2 → Target Groups).
- CloudWatch log groups `/ecs/<project>/<lang>` for app startup errors.
- CloudWatch log group `/ecs/<project>/migrator` for migration failures.
- `aws ecs describe-services` for the four language services.
