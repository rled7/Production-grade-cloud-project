output "redis_endpoint" {
  description = "Address of the primary Redis cache node."
  value       = aws_elasticache_cluster.this.cache_nodes[0].address
}

output "redis_port" {
  description = "Port on which the Redis cluster accepts connections."
  value       = 6379
}
