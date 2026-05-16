output "alb_dns_name" {
  description = "DNS name of the ALB."
  value       = module.alb.alb_dns_name
}

output "domain_name" {
  description = "Public domain that resolves to the ALB."
  value       = aws_route53_record.alb_alias.fqdn
}

output "ecr_repository_urls" {
  description = "Map of language => ECR repository URL. Push your built images here."
  value       = module.ecs.ecr_repository_urls
}

output "ecs_cluster_name" {
  description = "Name of the ECS cluster."
  value       = module.ecs.cluster_name
}

output "ecs_service_names" {
  description = "Names of the ECS Fargate services (one per language)."
  value       = module.ecs.service_names
}

output "cloudwatch_dashboard" {
  description = "Name of the CloudWatch dashboard."
  value       = module.monitoring.dashboard_name
}

output "db_address" {
  description = "RDS hostname (private)."
  value       = module.rds.db_address
}

output "redis_endpoint" {
  description = "ElastiCache Redis endpoint (private)."
  value       = module.elasticache.redis_endpoint
}

output "migrator_ecr_repository_url" {
  description = "ECR repository for the DB migrator image (apps/migrator). CI pushes here before invoking aws ecs run-task."
  value       = module.ecs.migrator_ecr_repository_url
}

output "migrator_task_definition_family" {
  description = "Task definition family CI invokes for migrations."
  value       = module.ecs.migrator_task_definition_family
}
