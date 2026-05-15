output "alb_arn" {
  description = "ARN of the Application Load Balancer."
  value       = aws_lb.this.arn
}

output "alb_dns_name" {
  description = "DNS name of the Application Load Balancer."
  value       = aws_lb.this.dns_name
}

output "alb_zone_id" {
  description = "Hosted zone ID of the Application Load Balancer (for Route53 alias records)."
  value       = aws_lb.this.zone_id
}

output "alb_arn_suffix" {
  description = "ARN suffix of the ALB, used in CloudWatch metric dimensions."
  value       = aws_lb.this.arn_suffix
}

output "target_group_arns" {
  description = "Map of language => target group ARN."
  value       = { for lang, tg in aws_lb_target_group.this : lang => tg.arn }
}

output "https_listener_arn" {
  description = "ARN of the HTTPS (443) listener."
  value       = aws_lb_listener.https.arn
}
