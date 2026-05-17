output "redis_endpoint" {
  description = "Primary endpoint address for the Redis replication group."
  value       = aws_elasticache_replication_group.this.primary_endpoint_address
}

output "redis_port" {
  description = "Port the Redis replication group accepts connections on."
  value       = aws_elasticache_replication_group.this.port
}

output "transit_encryption_enabled" {
  description = "Whether in-transit encryption (TLS) is enabled. Apps must connect via rediss:// / TLS when true."
  value       = aws_elasticache_replication_group.this.transit_encryption_enabled
}
