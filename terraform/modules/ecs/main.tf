terraform {
  required_version = ">= 1.5.0"
  required_providers {
    aws = {
      source  = "hashicorp/aws"
      version = "~> 5.0"
    }
  }
}

locals {
  languages_set = toset(var.languages)
}

############################
# ECR Repositories
############################
# image_tag_mutability = IMMUTABLE prevents a deployed tag from being silently
# overwritten — CI pushes only SHA-suffixed tags, so re-pushing the same SHA
# is the only way to "overwrite" (and it should be content-identical).
resource "aws_ecr_repository" "this" {
  for_each = local.languages_set

  name                 = "${var.project_name}-${each.key}"
  force_delete         = true
  image_tag_mutability = "IMMUTABLE"

  image_scanning_configuration {
    scan_on_push = true
  }

  tags = {
    Project  = var.project_name
    Language = each.key
  }
}

############################
# ECS Cluster
############################
resource "aws_ecs_cluster" "this" {
  name = "${var.project_name}-cluster"

  setting {
    name  = "containerInsights"
    value = "enabled"
  }

  tags = {
    Project = var.project_name
  }
}

############################
# CloudWatch Log Groups
############################
resource "aws_cloudwatch_log_group" "this" {
  for_each = local.languages_set

  name              = "/ecs/${var.project_name}/${each.key}"
  retention_in_days = 14

  tags = {
    Project  = var.project_name
    Language = each.key
  }
}

############################
# Secrets Manager
############################
resource "aws_secretsmanager_secret" "db" {
  name        = "${var.project_name}-app-secrets"
  description = "App credentials (DB password, API key) for ${var.project_name} ECS services."
}

resource "aws_secretsmanager_secret_version" "db" {
  secret_id = aws_secretsmanager_secret.db.id
  secret_string = jsonencode({
    DB_PASSWORD  = var.db_password
    API_KEY      = var.api_key
    API_KEY_NEXT = var.api_key_next
    JWT_SECRET   = var.jwt_secret
  })
}

############################
# IAM Roles
############################
data "aws_iam_policy_document" "ecs_tasks_assume" {
  statement {
    effect  = "Allow"
    actions = ["sts:AssumeRole"]
    principals {
      type        = "Service"
      identifiers = ["ecs-tasks.amazonaws.com"]
    }
  }
}

# Task execution role
resource "aws_iam_role" "task_execution" {
  name               = "${var.project_name}-ecs-task-execution"
  assume_role_policy = data.aws_iam_policy_document.ecs_tasks_assume.json
}

resource "aws_iam_role_policy_attachment" "task_execution_managed" {
  role       = aws_iam_role.task_execution.name
  policy_arn = "arn:aws:iam::aws:policy/service-role/AmazonECSTaskExecutionRolePolicy"
}

data "aws_iam_policy_document" "secrets_read" {
  statement {
    effect    = "Allow"
    actions   = ["secretsmanager:GetSecretValue"]
    resources = [aws_secretsmanager_secret.db.arn]
  }
}

resource "aws_iam_role_policy" "task_execution_secrets" {
  name   = "${var.project_name}-ecs-task-execution-secrets"
  role   = aws_iam_role.task_execution.id
  policy = data.aws_iam_policy_document.secrets_read.json
}

# Task role (placeholder)
resource "aws_iam_role" "task" {
  name               = "${var.project_name}-ecs-task"
  assume_role_policy = data.aws_iam_policy_document.ecs_tasks_assume.json
}

############################
# Task Definitions
############################
resource "aws_ecs_task_definition" "this" {
  for_each = local.languages_set

  family                   = "${var.project_name}-${each.key}"
  requires_compatibilities = ["FARGATE"]
  network_mode             = "awsvpc"
  cpu                      = var.cpu
  memory                   = var.memory
  execution_role_arn       = aws_iam_role.task_execution.arn
  task_role_arn            = aws_iam_role.task.arn

  container_definitions = jsonencode([
    {
      name      = each.key
      image     = "${aws_ecr_repository.this[each.key].repository_url}:${var.image_tag}"
      essential = true

      portMappings = [
        {
          containerPort = var.app_port
          hostPort      = var.app_port
          protocol      = "tcp"
        }
      ]

      environment = [
        { name = "PORT", value = tostring(var.app_port) },
        { name = "APP_LANG", value = each.key },
        { name = "DB_HOST", value = var.db_host },
        { name = "DB_PORT", value = tostring(var.db_port) },
        { name = "DB_NAME", value = var.db_name },
        { name = "DB_USER", value = var.db_username },
        { name = "REDIS_HOST", value = var.redis_host },
        { name = "REDIS_PORT", value = tostring(var.redis_port) },
        { name = "REDIS_TLS", value = var.redis_tls ? "true" : "false" },
        { name = "CACHE_TTL_SECONDS", value = tostring(var.cache_ttl_seconds) },
        { name = "REDIS_TIMEOUT_MS", value = tostring(var.redis_timeout_ms) },
        { name = "MAX_BODY_BYTES", value = tostring(var.max_body_bytes) },
        { name = "COOKIE_SECURE", value = "true" },
        { name = "ACCESS_LOG_PATH", value = "/var/log/app/access.log" },
      ]

      secrets = [
        {
          name      = "DB_PASSWORD"
          valueFrom = "${aws_secretsmanager_secret.db.arn}:DB_PASSWORD::"
        },
        {
          name      = "API_KEY"
          valueFrom = "${aws_secretsmanager_secret.db.arn}:API_KEY::"
        },
        {
          name      = "API_KEY_NEXT"
          valueFrom = "${aws_secretsmanager_secret.db.arn}:API_KEY_NEXT::"
        },
        {
          name      = "JWT_SECRET"
          valueFrom = "${aws_secretsmanager_secret.db.arn}:JWT_SECRET::"
        }
      ]

      logConfiguration = {
        logDriver = "awslogs"
        options = {
          "awslogs-group"         = aws_cloudwatch_log_group.this[each.key].name
          "awslogs-region"        = var.aws_region
          "awslogs-stream-prefix" = each.key
        }
      }

      healthCheck = {
        command     = ["CMD-SHELL", "curl -f http://localhost:${var.app_port}/health || exit 1"]
        interval    = 30
        timeout     = 5
        retries     = 3
        startPeriod = 30
      }
    }
  ])

  tags = {
    Project  = var.project_name
    Language = each.key
  }
}

############################
# ECS Services
############################
resource "aws_ecs_service" "this" {
  for_each = local.languages_set

  name            = "${var.project_name}-${each.key}"
  cluster         = aws_ecs_cluster.this.id
  task_definition = aws_ecs_task_definition.this[each.key].arn
  desired_count   = var.desired_count
  launch_type     = "FARGATE"

  deployment_circuit_breaker {
    enable   = true
    rollback = true
  }

  network_configuration {
    subnets          = var.private_subnet_ids
    security_groups  = [var.ecs_sg_id]
    assign_public_ip = false
  }

  load_balancer {
    target_group_arn = var.target_group_arns[each.key]
    container_name   = each.key
    container_port   = var.app_port
  }

  # CI registers new task-definition revisions (SHA-tagged image) and points
  # the service at them; Terraform manages only the initial revision.
  lifecycle {
    ignore_changes = [desired_count, task_definition]
  }

  tags = {
    Project  = var.project_name
    Language = each.key
  }
}

############################
# Auto Scaling
############################
resource "aws_appautoscaling_target" "this" {
  for_each = local.languages_set

  max_capacity       = var.max_capacity
  min_capacity       = var.min_capacity
  resource_id        = "service/${aws_ecs_cluster.this.name}/${aws_ecs_service.this[each.key].name}"
  scalable_dimension = "ecs:service:DesiredCount"
  service_namespace  = "ecs"
}

resource "aws_appautoscaling_policy" "cpu" {
  for_each = local.languages_set

  name               = "${var.project_name}-${each.key}-cpu-tt"
  policy_type        = "TargetTrackingScaling"
  resource_id        = aws_appautoscaling_target.this[each.key].resource_id
  scalable_dimension = aws_appautoscaling_target.this[each.key].scalable_dimension
  service_namespace  = aws_appautoscaling_target.this[each.key].service_namespace

  target_tracking_scaling_policy_configuration {
    target_value = 60

    predefined_metric_specification {
      predefined_metric_type = "ECSServiceAverageCPUUtilization"
    }
  }
}

############################
# Migrator: ECR repo + task definition + log group + SSM params
#
# Idea: the CI workflow builds and pushes apps/migrator → this ECR repo,
# then `aws ecs run-task` invokes this task definition inside the VPC's
# private subnets so it can reach RDS. The task exits 0 on success and
# non-zero on any migration failure; CI gates the deploy job on that.
############################
resource "aws_ecr_repository" "migrator" {
  name                 = "${var.project_name}-migrator"
  force_delete         = true
  image_tag_mutability = "IMMUTABLE"

  image_scanning_configuration {
    scan_on_push = true
  }

  tags = {
    Project = var.project_name
    Role    = "migrator"
  }
}

resource "aws_cloudwatch_log_group" "migrator" {
  name              = "/ecs/${var.project_name}/migrator"
  retention_in_days = 14

  tags = {
    Project = var.project_name
    Role    = "migrator"
  }
}

resource "aws_ecs_task_definition" "migrator" {
  family                   = "${var.project_name}-migrator"
  requires_compatibilities = ["FARGATE"]
  network_mode             = "awsvpc"
  cpu                      = "256"
  memory                   = "512"
  execution_role_arn       = aws_iam_role.task_execution.arn
  task_role_arn            = aws_iam_role.task.arn

  container_definitions = jsonencode([
    {
      name      = "migrator"
      image     = "${aws_ecr_repository.migrator.repository_url}:${var.image_tag}"
      essential = true

      environment = [
        { name = "PGHOST", value = var.db_host },
        { name = "PGPORT", value = tostring(var.db_port) },
        { name = "PGDATABASE", value = var.db_name },
        { name = "PGUSER", value = var.db_username },
      ]

      secrets = [
        {
          name      = "PGPASSWORD"
          valueFrom = "${aws_secretsmanager_secret.db.arn}:DB_PASSWORD::"
        }
      ]

      logConfiguration = {
        logDriver = "awslogs"
        options = {
          "awslogs-group"         = aws_cloudwatch_log_group.migrator.name
          "awslogs-region"        = var.aws_region
          "awslogs-stream-prefix" = "migrator"
        }
      }
    }
  ])

  tags = {
    Project = var.project_name
    Role    = "migrator"
  }
}

# Expose the network config the CI workflow needs to run the migrator task.
# Both are read by the `migrate` job in .github/workflows/deploy.yml via
# `aws ssm get-parameter`, so you don't need to remember them.
resource "aws_ssm_parameter" "migrator_subnets" {
  name      = "/${var.project_name}/migrator/subnets"
  type      = "StringList"
  value     = join(",", var.private_subnet_ids)
  overwrite = true

  tags = {
    Project = var.project_name
    Role    = "migrator"
  }
}

resource "aws_ssm_parameter" "migrator_security_group" {
  name      = "/${var.project_name}/migrator/security-group"
  type      = "String"
  value     = var.ecs_sg_id
  overwrite = true

  tags = {
    Project = var.project_name
    Role    = "migrator"
  }
}
