## What this changes

<!-- One paragraph. The "why", not just the "what". -->

## How it's tested

<!-- Which test suite(s) exercise this? Did you run the local docker-compose
     stack? Did terraform validate pass? -->
- [ ] `apps/js`     — `npm test`
- [ ] `apps/python` — `pytest -q`
- [ ] `apps/c`      — `make test`
- [ ] `apps/cpp`    — `cmake --build build --target unit_tests && ./build/unit_tests`
- [ ] `terraform`   — `terraform fmt -check -recursive && terraform validate`
- [ ] `docker compose up` — round-tripped a POST/GET against the affected service

## Risk / rollout

<!-- What could go wrong on deploy? Is a DB migration involved? Does the
     change require a coordinated client update (API key rotation, JWT secret
     change, etc.)? Anything that needs to happen before merge? -->

## Checklist

- [ ] Tests added or updated for the new behavior.
- [ ] `CHANGELOG.md` updated (and version bumped if user-visible).
- [ ] No secrets committed (terraform.tfvars, .env, AWS keys, JWT secret).
