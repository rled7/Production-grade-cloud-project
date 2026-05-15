variable "project_name" {
  description = "Name of the project; used to prefix resource names."
  type        = string
}

variable "vpc_id" {
  description = "ID of the VPC where the target groups and ALB live."
  type        = string
}

variable "public_subnet_ids" {
  description = "List of public subnet IDs the internet-facing ALB is attached to."
  type        = list(string)
}

variable "alb_sg_id" {
  description = "Security group ID to attach to the ALB."
  type        = string
}

variable "web_acl_arn" {
  description = "ARN of the WAFv2 Web ACL to associate with the ALB."
  type        = string
}

variable "certificate_arn" {
  description = "ARN of the ACM certificate used by the HTTPS listener."
  type        = string
}

variable "languages" {
  description = "List of language identifiers; one target group and listener rule is created per language."
  type        = list(string)
  default     = ["js", "python", "c", "cpp"]
}

variable "app_port" {
  description = "Port the application containers listen on; used by each target group."
  type        = number
  default     = 8080
}

variable "health_check_path" {
  description = "HTTP path used for target group health checks."
  type        = string
  default     = "/health"
}
