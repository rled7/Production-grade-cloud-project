variable "project_name" {
  description = "Project name used to namespace monitoring resources."
  type        = string
}

variable "aws_region" {
  description = "AWS region for CloudWatch widgets."
  type        = string
  default     = "us-east-1"
}

variable "cluster_name" {
  description = "ECS cluster name to monitor."
  type        = string
}

variable "service_names" {
  description = "List of ECS service names to include in dashboard and alarms."
  type        = list(string)
}

variable "alb_arn_suffix" {
  description = "ARN suffix of the Application Load Balancer for ALB CloudWatch metrics."
  type        = string
}

variable "alarm_sns_topic_arns" {
  description = "SNS topic ARNs for alarm actions."
  type        = list(string)
  default     = []
}

variable "cpu_alarm_threshold" {
  description = "Per-service CPU utilization alarm threshold (percent)."
  type        = number
  default     = 80
}

variable "alb_5xx_alarm_threshold" {
  description = "ALB 5XX count alarm threshold over the evaluation period."
  type        = number
  default     = 10
}
