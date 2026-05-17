variable "project_name" {
  description = "Project name used to namespace ECS resources."
  type        = string
}

variable "aws_region" {
  description = "AWS region for log configuration and other regional resources."
  type        = string
  default     = "us-east-1"
}

variable "private_subnet_ids" {
  description = "Private subnet IDs for ECS service ENIs."
  type        = list(string)
}

variable "ecs_sg_id" {
  description = "Security group ID to attach to ECS tasks."
  type        = string
}

variable "languages" {
  description = "List of language identifiers; one Fargate service is created per language."
  type        = list(string)
  default     = ["js", "python", "c", "cpp"]
}

variable "target_group_arns" {
  description = "Map of language => ALB target group ARN."
  type        = map(string)
}

variable "app_port" {
  description = "Container port the application listens on."
  type        = number
  default     = 8080
}

variable "db_host" {
  description = "Database hostname for the application."
  type        = string
}

variable "db_port" {
  description = "Database port."
  type        = number
  default     = 5432
}

variable "db_name" {
  description = "Database name."
  type        = string
}

variable "db_username" {
  description = "Database username."
  type        = string
}

variable "db_password" {
  description = "Database password (stored in Secrets Manager)."
  type        = string
  sensitive   = true
}

variable "api_key" {
  description = "API key required on the X-API-Key header for every /api/<lang>/* route. Stored in Secrets Manager and injected into each task. Empty disables auth."
  type        = string
  sensitive   = true
}

variable "api_key_next" {
  description = "Optional second API key accepted in parallel with api_key. Used for graceful rotation: set the new key here, redeploy, migrate clients, then swap api_key=api_key_next and clear this var. Empty means no rotation in flight."
  type        = string
  sensitive   = true
  default     = ""
}

variable "jwt_secret" {
  description = "HMAC-SHA256 signing secret for the session JWT cookie. Stored in Secrets Manager and injected into each task. Empty disables JWT verification."
  type        = string
  sensitive   = true
}

variable "redis_tls" {
  description = "Whether the ElastiCache cluster has in-transit encryption enabled. Apps connect via TLS when true."
  type        = bool
  default     = true
}

variable "redis_host" {
  description = "Redis hostname."
  type        = string
}

variable "redis_port" {
  description = "Redis port."
  type        = number
  default     = 6379
}

variable "cache_ttl_seconds" {
  description = "Cache TTL in seconds."
  type        = number
  default     = 30
}

variable "redis_timeout_ms" {
  description = "Redis client timeout in milliseconds."
  type        = number
  default     = 200
}

variable "max_body_bytes" {
  description = "Maximum HTTP body size in bytes."
  type        = number
  default     = 1048576
}

variable "desired_count" {
  description = "Desired number of tasks per service."
  type        = number
  default     = 2
}

variable "cpu" {
  description = "Fargate task CPU units."
  type        = string
  default     = "256"
}

variable "memory" {
  description = "Fargate task memory (MiB)."
  type        = string
  default     = "512"
}

variable "image_tag" {
  description = "Container image tag to deploy."
  type        = string
  default     = "latest"
}

variable "min_capacity" {
  description = "Minimum capacity for app autoscaling target."
  type        = number
  default     = 2
}

variable "max_capacity" {
  description = "Maximum capacity for app autoscaling target."
  type        = number
  default     = 6
}
