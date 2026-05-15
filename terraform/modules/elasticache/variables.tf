variable "project_name" {
  description = "Name prefix used for tagging and naming ElastiCache resources."
  type        = string
}

variable "private_subnet_ids" {
  description = "List of private subnet IDs for the cache subnet group."
  type        = list(string)
}

variable "redis_sg_id" {
  description = "Security group ID to associate with the Redis cluster."
  type        = string
}

variable "node_type" {
  description = "ElastiCache node type."
  type        = string
  default     = "cache.t3.micro"
}

variable "engine_version" {
  description = "Redis engine version."
  type        = string
  default     = "7.1"
}
