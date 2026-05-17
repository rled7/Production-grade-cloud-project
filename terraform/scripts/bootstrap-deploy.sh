#!/usr/bin/env bash
#
# First-time deploy bootstrap. The chicken-and-egg problem:
#
#   - ECR repos have image_tag_mutability=IMMUTABLE.
#   - ECS task definitions reference ${ecr}:${image_tag} (default "latest").
#   - First `terraform apply` creates services pointing at a tag that does
#     not exist yet, so tasks fail to pull and stay PENDING.
#
# This script breaks the cycle:
#
#   1. terraform apply -target=... — creates ONLY the ECR repos.
#   2. docker build + docker push the initial image for every service,
#      tagged with both `bootstrap` and the current git SHA.
#   3. terraform apply — creates the rest. ECS task definitions now
#      reference an image that actually exists; the services come up clean.
#   4. The CI deploy job takes over on the next push to main and
#      register-task-definition's the SHA-tagged image as the live revision.
#
# Run from the repo root:
#   ENV=prod    AWS_REGION=us-east-1 ./terraform/scripts/bootstrap-deploy.sh
#   ENV=staging AWS_REGION=us-east-1 ./terraform/scripts/bootstrap-deploy.sh

set -euo pipefail

ENV="${ENV:-prod}"
AWS_REGION="${AWS_REGION:-us-east-1}"
TF_DIR="terraform/environments/${ENV}"
LANGUAGES=(js python c cpp migrator)
BOOTSTRAP_TAG="${BOOTSTRAP_TAG:-bootstrap}"

if [ ! -d "$TF_DIR" ]; then
    echo "FATAL: $TF_DIR does not exist" >&2
    exit 1
fi
if [ ! -f "$TF_DIR/terraform.tfvars" ]; then
    echo "FATAL: $TF_DIR/terraform.tfvars not found." >&2
    echo "       Copy terraform.tfvars.example and fill it in first." >&2
    exit 1
fi

echo "==> [1/4] terraform init ($TF_DIR)"
terraform -chdir="$TF_DIR" init

echo "==> [2/4] targeted apply: ECR repos only"
terraform -chdir="$TF_DIR" apply -auto-approve \
    -target='module.ecs.aws_ecr_repository.this' \
    -target='module.ecs.aws_ecr_repository.migrator'

ACCOUNT_ID="$(aws sts get-caller-identity --query Account --output text)"
REGISTRY="${ACCOUNT_ID}.dkr.ecr.${AWS_REGION}.amazonaws.com"
PROJECT_NAME="$(terraform -chdir="$TF_DIR" output -raw project_name 2>/dev/null \
    || grep -E '^project_name' "$TF_DIR/terraform.tfvars" | sed 's/.*= *"\(.*\)"/\1/')"
SHA="$(git rev-parse --short HEAD 2>/dev/null || echo $$)"

echo "==> [3/4] docker login + build + push initial images for ${LANGUAGES[*]}"
aws ecr get-login-password --region "$AWS_REGION" \
  | docker login --username AWS --password-stdin "$REGISTRY"

for lang in "${LANGUAGES[@]}"; do
    repo="${REGISTRY}/${PROJECT_NAME}-${lang}"
    case "$lang" in
        migrator) ctx="."; dockerfile="apps/migrator/Dockerfile" ;;
        *)        ctx="apps/${lang}"; dockerfile="apps/${lang}/Dockerfile" ;;
    esac
    echo "    building ${lang} -> ${repo}:${BOOTSTRAP_TAG} (+${SHA})"
    docker build -t "${repo}:${BOOTSTRAP_TAG}" -t "${repo}:${SHA}" -f "$dockerfile" "$ctx"
    docker push "${repo}:${BOOTSTRAP_TAG}"
    docker push "${repo}:${SHA}"
done

echo "==> [4/4] full terraform apply"
terraform -chdir="$TF_DIR" apply -auto-approve \
    -var "image_tag=${BOOTSTRAP_TAG}"

cat <<EOF

==> Bootstrap complete.

Next steps:
  - Push to main; the CI deploy job will replace the bootstrap tag with
    SHA-tagged images via register-task-definition + update-service.
  - Hit https://<your-domain>/health to verify the ALB + services are healthy.
  - Run benchmark/run_compare.sh against the new endpoint.
EOF
