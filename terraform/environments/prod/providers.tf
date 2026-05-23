terraform {
  required_version = ">= 1.5.0"

  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 6.46"
    }
  }

  # Remote state backend. Bootstrap the S3 bucket and DynamoDB lock table
  # with terraform/scripts/setup-backend.sh before running `terraform init`.
  backend "s3" {
    bucket         = "REPLACE_WITH_STATE_BUCKET"
    key            = "prod/terraform.tfstate"
    region         = "us-east-1"
    dynamodb_table = "REPLACE_WITH_LOCK_TABLE"
    encrypt        = true
  }
}

provider "aws" {
  region = var.aws_region

  default_tags {
    tags = {
      Project     = var.project_name
      Environment = "prod"
      ManagedBy   = "terraform"
    }
  }
}
