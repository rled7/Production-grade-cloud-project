output "cluster_name" {
  description = "Name of the ECS cluster."
  value       = aws_ecs_cluster.this.name
}

output "cluster_arn" {
  description = "ARN of the ECS cluster."
  value       = aws_ecs_cluster.this.arn
}

output "ecr_repository_urls" {
  description = "Map of language => ECR repository URL."
  value       = { for k, r in aws_ecr_repository.this : k => r.repository_url }
}

output "service_names" {
  description = "List of ECS service names."
  value       = [for s in aws_ecs_service.this : s.name]
}

output "task_execution_role_arn" {
  description = "ARN of the ECS task execution IAM role."
  value       = aws_iam_role.task_execution.arn
}

output "secret_arn" {
  description = "ARN of the Secrets Manager secret holding DB credentials."
  value       = aws_secretsmanager_secret.db.arn
}

output "migrator_ecr_repository_url" {
  description = "ECR repository URL for the one-shot DB migrator image. The CI workflow builds apps/migrator and pushes here."
  value       = aws_ecr_repository.migrator.repository_url
}

output "migrator_task_definition_family" {
  description = "Task definition family name for the migrator. CI invokes this via `aws ecs run-task --task-definition`."
  value       = aws_ecs_task_definition.migrator.family
}

output "migrator_subnets_ssm_param" {
  description = "SSM parameter name holding the private subnet IDs the migrator task runs in (comma-separated)."
  value       = aws_ssm_parameter.migrator_subnets.name
}

output "migrator_security_group_ssm_param" {
  description = "SSM parameter name holding the security group the migrator task attaches to."
  value       = aws_ssm_parameter.migrator_security_group.name
}
