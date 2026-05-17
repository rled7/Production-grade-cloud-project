# Staging variables. Same shape as prod, with smaller / cheaper defaults.
# Override anything in terraform.tfvars; keep secrets out of version control.

variable "project_name" {
  description = "Name of the project; used to prefix every named resource."
  type        = string
  default     = "ml-ecs-benchmark-staging"
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
  default     = "10.1.0.0/16" # different from prod (10.0.0.0/16) so VPC peering is possible later
}

variable "public_subnet_cidrs" {
  description = "CIDRs for the two public subnets (one per AZ)."
  type        = list(string)
  default     = ["10.1.0.0/24", "10.1.1.0/24"]
}

variable "private_subnet_cidrs" {
  description = "CIDRs for the two private subnets (one per AZ)."
  type        = list(string)
  default     = ["10.1.10.0/24", "10.1.11.0/24"]
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
  description = "Desired Fargate task count per service. Lower than prod."
  type        = number
  default     = 1
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
  description = "RDS instance class (smaller default in staging)."
  type        = string
  default     = "db.t3.micro"
}

variable "db_multi_az" {
  description = "Provision RDS as Multi-AZ. Off by default in staging to save cost."
  type        = bool
  default     = false
}

variable "redis_node_type" {
  description = "ElastiCache Redis node type."
  type        = string
  default     = "cache.t3.micro"
}

# ---------- Edge / TLS ----------

variable "domain_name" {
  description = "Fully qualified domain name to serve traffic on (e.g. staging-api.example.com). An ACM certificate is created and validated via Route53."
  type        = string
}

variable "route53_zone_id" {
  description = "Route53 hosted zone ID that owns var.domain_name's base domain."
  type        = string
}

variable "waf_rate_limit" {
  description = "WAF rate-based rule limit. Tighter in staging to catch loops cheaply."
  type        = number
  default     = 1000
}

variable "api_key" {
  description = "API key required on the X-API-Key header. Keep in terraform.tfvars (gitignored)."
  type        = string
  sensitive   = true
}

variable "api_key_next" {
  description = "Optional second API key accepted in parallel during rotation."
  type        = string
  sensitive   = true
  default     = ""
}

variable "jwt_secret" {
  description = "HMAC-SHA256 signing secret for the session JWT cookie. Keep in terraform.tfvars."
  type        = string
  sensitive   = true
}
