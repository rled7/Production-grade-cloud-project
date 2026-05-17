output "db_address" {
  description = "Hostname of the RDS instance (address only, no port)."
  value       = aws_db_instance.this.address
}

output "db_endpoint" {
  description = "Connection endpoint of the RDS instance (host:port)."
  value       = aws_db_instance.this.endpoint
}

output "db_port" {
  description = "Port on which the database accepts connections."
  value       = aws_db_instance.this.port
}

output "db_name" {
  description = "Initial database name."
  value       = aws_db_instance.this.db_name
}

output "db_username" {
  description = "Master database username."
  value       = aws_db_instance.this.username
  sensitive   = true
}
