variable "project_name" {
  description = "Name of the project; used to prefix resource names and metrics."
  type        = string
}

variable "rate_limit" {
  description = "Maximum requests allowed from a single IP in a 5-minute window before the rate-based rule blocks traffic."
  type        = number
  default     = 2000
}

variable "enable_logging" {
  description = "Create the CloudWatch log group and wire WAFv2 logging to it."
  type        = bool
  default     = true
}

variable "log_retention_days" {
  description = "CloudWatch log group retention in days for WAF logs."
  type        = number
  default     = 30
}
