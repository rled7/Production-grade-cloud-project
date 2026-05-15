# Root composition for the production environment.
# Wires VPC → security groups → data tier (RDS, ElastiCache) → WAF/ALB → ECS → monitoring.

module "vpc" {
  source = "../../modules/vpc"

  project_name         = var.project_name
  vpc_cidr             = var.vpc_cidr
  public_subnet_cidrs  = var.public_subnet_cidrs
  private_subnet_cidrs = var.private_subnet_cidrs
}

module "security_groups" {
  source = "../../modules/security-groups"

  project_name = var.project_name
  vpc_id       = module.vpc.vpc_id
}

module "rds" {
  source = "../../modules/rds"

  project_name       = var.project_name
  private_subnet_ids = module.vpc.private_subnet_ids
  rds_sg_id          = module.security_groups.rds_sg_id
  db_name            = var.db_name
  db_username        = var.db_username
  db_password        = var.db_password
  instance_class     = var.db_instance_class
  multi_az           = var.db_multi_az
}

module "elasticache" {
  source = "../../modules/elasticache"

  project_name       = var.project_name
  private_subnet_ids = module.vpc.private_subnet_ids
  redis_sg_id        = module.security_groups.redis_sg_id
  node_type          = var.redis_node_type
}

module "waf" {
  source = "../../modules/waf"

  project_name = var.project_name
  rate_limit   = var.waf_rate_limit
}

# ----- ACM certificate (DNS-validated via the user-supplied Route53 zone) -----

resource "aws_acm_certificate" "this" {
  domain_name       = var.domain_name
  validation_method = "DNS"

  lifecycle {
    create_before_destroy = true
  }
}

resource "aws_route53_record" "cert_validation" {
  for_each = {
    for dvo in aws_acm_certificate.this.domain_validation_options : dvo.domain_name => {
      name   = dvo.resource_record_name
      record = dvo.resource_record_value
      type   = dvo.resource_record_type
    }
  }

  zone_id = var.route53_zone_id
  name    = each.value.name
  type    = each.value.type
  records = [each.value.record]
  ttl     = 60
}

resource "aws_acm_certificate_validation" "this" {
  certificate_arn         = aws_acm_certificate.this.arn
  validation_record_fqdns = [for r in aws_route53_record.cert_validation : r.fqdn]
}

# ----- ALB + WAF association -----

module "alb" {
  source = "../../modules/alb"

  project_name      = var.project_name
  vpc_id            = module.vpc.vpc_id
  public_subnet_ids = module.vpc.public_subnet_ids
  alb_sg_id         = module.security_groups.alb_sg_id
  web_acl_arn       = module.waf.web_acl_arn
  certificate_arn   = aws_acm_certificate_validation.this.certificate_arn
  languages         = var.languages
}

# Friendly DNS alias for the ALB.
resource "aws_route53_record" "alb_alias" {
  zone_id = var.route53_zone_id
  name    = var.domain_name
  type    = "A"

  alias {
    name                   = module.alb.alb_dns_name
    zone_id                = module.alb.alb_zone_id
    evaluate_target_health = true
  }
}

# ----- Compute -----

module "ecs" {
  source = "../../modules/ecs"

  project_name       = var.project_name
  aws_region         = var.aws_region
  private_subnet_ids = module.vpc.private_subnet_ids
  ecs_sg_id          = module.security_groups.ecs_sg_id
  languages          = var.languages
  target_group_arns  = module.alb.target_group_arns

  db_host     = module.rds.db_address
  db_port     = module.rds.db_port
  db_name     = var.db_name
  db_username = var.db_username
  db_password = var.db_password
  api_key     = var.api_key

  redis_host = module.elasticache.redis_endpoint
  redis_port = module.elasticache.redis_port

  desired_count = var.desired_count
  cpu           = var.task_cpu
  memory        = var.task_memory
  image_tag     = var.image_tag
}

# ----- Monitoring -----

module "monitoring" {
  source = "../../modules/monitoring"

  project_name   = var.project_name
  aws_region     = var.aws_region
  cluster_name   = module.ecs.cluster_name
  service_names  = module.ecs.service_names
  alb_arn_suffix = module.alb.alb_arn_suffix
}
