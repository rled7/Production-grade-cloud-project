terraform {
  required_version = ">= 1.5.0"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

resource "aws_elasticache_subnet_group" "this" {
  name        = "${var.project_name}-redis-subnet-group"
  description = "Private subnet group for ${var.project_name} Redis"
  subnet_ids  = var.private_subnet_ids

  tags = {
    Name    = "${var.project_name}-redis-subnet-group"
    Project = var.project_name
  }
}

# Use a replication group so we can enable transit encryption (Redis AUTH
# isn't on, but in-transit TLS + at-rest encryption are). For a single-node
# deployment this is just one primary, no replicas.
resource "aws_elasticache_replication_group" "this" {
  replication_group_id = "${var.project_name}-redis"
  description          = "Redis cache for ${var.project_name}"

  engine               = "redis"
  engine_version       = var.engine_version
  node_type            = var.node_type
  num_cache_clusters   = var.num_cache_clusters
  port                 = 6379
  parameter_group_name = "default.redis7"

  subnet_group_name  = aws_elasticache_subnet_group.this.name
  security_group_ids = [var.redis_sg_id]

  at_rest_encryption_enabled = var.at_rest_encryption_enabled
  transit_encryption_enabled = var.transit_encryption_enabled

  automatic_failover_enabled = var.num_cache_clusters > 1
  multi_az_enabled           = var.num_cache_clusters > 1

  apply_immediately = true

  tags = {
    Name    = "${var.project_name}-redis"
    Project = var.project_name
  }
}
