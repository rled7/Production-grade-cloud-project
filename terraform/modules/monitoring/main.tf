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
  service_set = toset(var.service_names)

  # CPU widgets for each ECS service
  cpu_widgets = [
    for idx, svc in var.service_names : {
      type   = "metric"
      x      = (idx % 2) * 12
      y      = floor(idx / 2) * 6
      width  = 12
      height = 6
      properties = {
        title  = "${svc} - CPUUtilization"
        view   = "timeSeries"
        region = var.aws_region
        stat   = "Average"
        period = 60
        metrics = [
          ["AWS/ECS", "CPUUtilization", "ClusterName", var.cluster_name, "ServiceName", svc]
        ]
      }
    }
  ]

  # Memory widgets for each ECS service
  memory_y_base = ceil(length(var.service_names) / 2.0) * 6
  memory_widgets = [
    for idx, svc in var.service_names : {
      type   = "metric"
      x      = (idx % 2) * 12
      y      = local.memory_y_base + floor(idx / 2) * 6
      width  = 12
      height = 6
      properties = {
        title  = "${svc} - MemoryUtilization"
        view   = "timeSeries"
        region = var.aws_region
        stat   = "Average"
        period = 60
        metrics = [
          ["AWS/ECS", "MemoryUtilization", "ClusterName", var.cluster_name, "ServiceName", svc]
        ]
      }
    }
  ]

  alb_y_base = local.memory_y_base + ceil(length(var.service_names) / 2.0) * 6

  alb_widgets = [
    {
      type   = "metric"
      x      = 0
      y      = local.alb_y_base
      width  = 8
      height = 6
      properties = {
        title  = "ALB - RequestCount"
        view   = "timeSeries"
        region = var.aws_region
        stat   = "Sum"
        period = 60
        metrics = [
          ["AWS/ApplicationELB", "RequestCount", "LoadBalancer", var.alb_arn_suffix]
        ]
      }
    },
    {
      type   = "metric"
      x      = 8
      y      = local.alb_y_base
      width  = 8
      height = 6
      properties = {
        title  = "ALB - HTTPCode_Target_5XX_Count"
        view   = "timeSeries"
        region = var.aws_region
        stat   = "Sum"
        period = 60
        metrics = [
          ["AWS/ApplicationELB", "HTTPCode_Target_5XX_Count", "LoadBalancer", var.alb_arn_suffix]
        ]
      }
    },
    {
      type   = "metric"
      x      = 16
      y      = local.alb_y_base
      width  = 8
      height = 6
      properties = {
        title  = "ALB - TargetResponseTime"
        view   = "timeSeries"
        region = var.aws_region
        stat   = "Average"
        period = 60
        metrics = [
          ["AWS/ApplicationELB", "TargetResponseTime", "LoadBalancer", var.alb_arn_suffix]
        ]
      }
    }
  ]

  widgets = concat(local.cpu_widgets, local.memory_widgets, local.alb_widgets)
}

resource "aws_cloudwatch_dashboard" "this" {
  dashboard_name = "${var.project_name}-dashboard"

  dashboard_body = jsonencode({
    widgets = local.widgets
  })
}

############################
# Alarms
############################
resource "aws_cloudwatch_metric_alarm" "service_cpu_high" {
  for_each = local.service_set

  alarm_name          = "${var.project_name}-${each.key}-cpu-high"
  alarm_description   = "High CPU utilization on ECS service ${each.key}"
  comparison_operator = "GreaterThanThreshold"
  evaluation_periods  = 2
  metric_name         = "CPUUtilization"
  namespace           = "AWS/ECS"
  period              = 60
  statistic           = "Average"
  threshold           = var.cpu_alarm_threshold
  treat_missing_data  = "notBreaching"

  dimensions = {
    ClusterName = var.cluster_name
    ServiceName = each.key
  }

  alarm_actions = var.alarm_sns_topic_arns
  ok_actions    = var.alarm_sns_topic_arns
}

resource "aws_cloudwatch_metric_alarm" "alb_5xx_high" {
  alarm_name          = "${var.project_name}-alb-5xx-high"
  alarm_description   = "ALB target 5XX count exceeded threshold"
  comparison_operator = "GreaterThanThreshold"
  evaluation_periods  = 1
  metric_name         = "HTTPCode_Target_5XX_Count"
  namespace           = "AWS/ApplicationELB"
  period              = 60
  statistic           = "Sum"
  threshold           = var.alb_5xx_alarm_threshold
  treat_missing_data  = "notBreaching"

  dimensions = {
    LoadBalancer = var.alb_arn_suffix
  }

  alarm_actions = var.alarm_sns_topic_arns
  ok_actions    = var.alarm_sns_topic_arns
}
