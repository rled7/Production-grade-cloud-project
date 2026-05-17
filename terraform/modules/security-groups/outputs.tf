output "alb_sg_id" {
  description = "ID of the ALB security group."
  value       = aws_security_group.alb.id
}

output "ecs_sg_id" {
  description = "ID of the ECS tasks security group."
  value       = aws_security_group.ecs.id
}

output "rds_sg_id" {
  description = "ID of the RDS security group."
  value       = aws_security_group.rds.id
}

output "redis_sg_id" {
  description = "ID of the Redis security group."
  value       = aws_security_group.redis.id
}
