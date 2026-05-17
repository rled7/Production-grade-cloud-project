variable "project_name" {
  description = "Name prefix used for tagging and naming RDS resources."
  type        = string
}

variable "private_subnet_ids" {
  description = "List of private subnet IDs for the DB subnet group."
  type        = list(string)
}

variable "rds_sg_id" {
  description = "Security group ID to associate with the RDS instance."
  type        = string
}

variable "db_name" {
  description = "Initial database name to create."
  type        = string
  default     = "appdb"
}

variable "db_username" {
  description = "Master database username."
  type        = string
  default     = "appuser"
}

variable "db_password" {
  description = "Master database password."
  type        = string
  sensitive   = true
}

variable "instance_class" {
  description = "RDS instance class."
  type        = string
  default     = "db.t3.micro"
}

variable "allocated_storage" {
  description = "Allocated storage size in GiB."
  type        = number
  default     = 20
}

variable "engine_version" {
  description = "PostgreSQL engine version."
  type        = string
  default     = "16"
}

variable "multi_az" {
  description = "Whether to deploy the database in multiple AZs."
  type        = bool
  default     = true
}
