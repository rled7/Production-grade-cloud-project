variable "project_name" {
  description = "Name prefix used for tagging and naming security groups."
  type        = string
}

variable "vpc_id" {
  description = "ID of the VPC in which to create the security groups."
  type        = string
}

variable "app_port" {
  description = "Application port exposed by ECS tasks and reachable from the ALB."
  type        = number
  default     = 8080
}
