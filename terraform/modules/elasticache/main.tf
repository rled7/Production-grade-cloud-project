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

resource "aws_elasticache_cluster" "this" {
  cluster_id           = "${var.project_name}-redis"
  engine               = "redis"
  engine_version       = var.engine_version
  node_type            = var.node_type
  num_cache_nodes      = 1
  port                 = 6379
  subnet_group_name    = aws_elasticache_subnet_group.this.name
  security_group_ids   = [var.redis_sg_id]
  parameter_group_name = "default.redis7"

  tags = {
    Name    = "${var.project_name}-redis"
    Project = var.project_name
  }
}
