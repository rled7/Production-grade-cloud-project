#!/usr/bin/env bash
#
# Bootstraps the Terraform remote state backend: an S3 bucket for state
# storage (versioned + encrypted) and a DynamoDB table for state locking.
#
# Run this ONCE before `terraform init`. After it completes, update the
# backend "s3" block in terraform/environments/prod/providers.tf with the
# bucket and table names printed at the end.
#
# Usage:
#   ./setup-backend.sh [project-name] [aws-region]
#
set -euo pipefail

PROJECT_NAME="${1:-ml-ecs-benchmark}"
AWS_REGION="${2:-us-east-1}"

ACCOUNT_ID="$(aws sts get-caller-identity --query Account --output text)"
STATE_BUCKET="${PROJECT_NAME}-tfstate-${ACCOUNT_ID}"
LOCK_TABLE="${PROJECT_NAME}-tflock"

echo "==> Creating S3 state bucket: ${STATE_BUCKET}"
if aws s3api head-bucket --bucket "${STATE_BUCKET}" 2>/dev/null; then
  echo "    Bucket already exists, skipping creation."
else
  if [ "${AWS_REGION}" = "us-east-1" ]; then
    aws s3api create-bucket \
      --bucket "${STATE_BUCKET}" \
      --region "${AWS_REGION}"
  else
    aws s3api create-bucket \
      --bucket "${STATE_BUCKET}" \
      --region "${AWS_REGION}" \
      --create-bucket-configuration LocationConstraint="${AWS_REGION}"
  fi
fi

echo "==> Enabling versioning on ${STATE_BUCKET}"
aws s3api put-bucket-versioning \
  --bucket "${STATE_BUCKET}" \
  --versioning-configuration Status=Enabled

echo "==> Enabling default encryption on ${STATE_BUCKET}"
aws s3api put-bucket-encryption \
  --bucket "${STATE_BUCKET}" \
  --server-side-encryption-configuration \
  '{"Rules":[{"ApplyServerSideEncryptionByDefault":{"SSEAlgorithm":"AES256"}}]}'

echo "==> Blocking public access on ${STATE_BUCKET}"
aws s3api put-public-access-block \
  --bucket "${STATE_BUCKET}" \
  --public-access-block-configuration \
  BlockPublicAcls=true,IgnorePublicAcls=true,BlockPublicPolicy=true,RestrictPublicBuckets=true

echo "==> Creating DynamoDB lock table: ${LOCK_TABLE}"
if aws dynamodb describe-table --table-name "${LOCK_TABLE}" --region "${AWS_REGION}" 2>/dev/null; then
  echo "    Table already exists, skipping creation."
else
  aws dynamodb create-table \
    --table-name "${LOCK_TABLE}" \
    --attribute-definitions AttributeName=LockID,AttributeType=S \
    --key-schema AttributeName=LockID,KeyType=HASH \
    --billing-mode PAY_PER_REQUEST \
    --region "${AWS_REGION}"
fi

cat <<EOF

==> Backend bootstrap complete.

Update terraform/environments/prod/providers.tf backend "s3" block with:

  bucket         = "${STATE_BUCKET}"
  key            = "prod/terraform.tfstate"
  region         = "${AWS_REGION}"
  dynamodb_table = "${LOCK_TABLE}"
  encrypt        = true

Then run: terraform -chdir=terraform/environments/prod init
EOF
