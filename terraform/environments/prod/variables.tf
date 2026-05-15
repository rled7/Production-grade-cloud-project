variable "project_name" {
  description = "Name of the project; used to prefix every named resource."
  type        = string
  default     = "ml-ecs-benchmark"
}

variable "aws_region" {
  description = "AWS region."
  type        = string
  default     = "us-east-1"
}

# ---------- Networking ----------

variable "vpc_cidr" {
  description = "CIDR block for the VPC."
  type        = string
  default     = "10.0.0.0/16"
}

variable "public_subnet_cidrs" {
  description = "CIDRs for the two public subnets (one per AZ)."
  type        = list(string)
  default     = ["10.0.0.0/24", "10.0.1.0/24"]
}

variable "private_subnet_cidrs" {
  description = "CIDRs for the two private subnets (one per AZ)."
  type        = list(string)
  default     = ["10.0.10.0/24", "10.0.11.0/24"]
}

# ---------- Application ----------

variable "languages" {
  description = "Language identifiers; one Fargate service + ECR repo + target group per language."
  type        = list(string)
  default     = ["js", "python", "c", "cpp"]
}

variable "image_tag" {
  description = "Container image tag to deploy across all services."
  type        = string
  default     = "latest"
}

variable "desired_count" {
  description = "Desired Fargate task count per service."
  type        = number
  default     = 2
}

variable "task_cpu" {
  description = "Fargate task CPU units."
  type        = string
  default     = "256"
}

variable "task_memory" {
  description = "Fargate task memory (MiB)."
  type        = string
  default     = "512"
}

# ---------- Data tier ----------

variable "db_name" {
  description = "Initial database name."
  type        = string
  default     = "appdb"
}

variable "db_username" {
  description = "Master database username."
  type        = string
  default     = "appuser"
}

variable "db_password" {
  description = "Master database password — keep this in terraform.tfvars (gitignored)."
  type        = string
  sensitive   = true
}

variable "db_instance_class" {
  description = "RDS instance class."
  type        = string
  default     = "db.t3.micro"
}

variable "db_multi_az" {
  description = "Provision RDS as Multi-AZ."
  type        = bool
  default     = true
}

variable "redis_node_type" {
  description = "ElastiCache Redis node type."
  type        = string
  default     = "cache.t3.micro"
}

# ---------- Edge / TLS ----------

variable "domain_name" {
  description = "Fully qualified domain name to serve traffic on (e.g. api.example.com). An ACM certificate is created and validated via Route53."
  type        = string
}

variable "route53_zone_id" {
  description = "Route53 hosted zone ID that owns var.domain_name's base domain. Used for ACM DNS validation and the ALB alias record."
  type        = string
}

variable "waf_rate_limit" {
  description = "WAF rate-based rule limit (requests per 5-minute window, per IP)."
  type        = number
  default     = 2000
}
