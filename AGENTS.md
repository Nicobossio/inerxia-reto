# AGENTS.md — Inerxia Founder Engineer Challenge

## Project overview

Implementation of the Inerxia Founder Engineer technical challenge: a billing and
service-management system for an ISP with MikroTik router integration.

## Language and toolchain (mandatory)

- Modern C++ only: **C++20 or C++23** (pick one standard and keep it consistent).
- Build system: **CMake**.
- Unit tests: **GoogleTest**.
- Persistence: **PostgreSQL** (when persistence is required).
- HTTP: a lightweight C++ HTTP framework (evaluate options; record the choice as a design decision).
- Local infrastructure: **Docker Compose** (PostgreSQL, and any other required services).
- All code, tests, and build files must be C++. No business logic in scripts.

## Architecture (mandatory)

- **Hexagonal Architecture** (ports & adapters).
- Strong **DDD** principles: ubiquitous language, entities, value objects, aggregates,
  domain events, repositories as abstractions.
- **Domain isolated from infrastructure**: domain/application code must not depend on
  PostgreSQL, HTTP framework, MikroTik APIs, or other frameworks.
- **Dependency inversion**: domain defines interfaces (ports); infrastructure implements them.
- **Repository interfaces** live at the domain/application boundary; concrete
  implementations live outside the domain (infrastructure layer).
- **Event-driven design** where it provides clear value (e.g., payment registered,
  contract suspended/reactivated). Do not add events without a reason.
- **No business logic inside controllers/handlers**: controllers only translate
  HTTP input to application use cases and map results to responses.
- **No MikroTik-specific code inside domain entities**: router behavior is expressed
  as a domain-level port (e.g., service enable/disable, speed profile change);
  MikroTik specifics belong to an infrastructure adapter.

## Functional requirements (from the challenge only)

- Internet plan assigned to an ISP user.
- Contract with contracted megabytes.
- Billing start date.
- Due date.
- Manual payment registration.
- Contract lifecycle: create, query, edit, suspend, reactivate.
- Automatic suspension when due date passes without payment.
- Automatic reactivation after payment.
- MikroTik router integration to enable/disable service.
- Ability to modify the subscriber's speed profile.

## Network assumptions (fixed by the challenge)

- ISP uses MikroTik.
- Each subscriber has a static IP.
- Router integration must be **real** (actual RouterOS API usage), not merely mocked
  in the production architecture. Mocks/fakes are allowed only in tests.

## Development process

- **Test-driven or test-first whenever practical.**
- Every domain rule must have **unit tests**.
- **Integration tests** for persistence (PostgreSQL).
- **Integration tests** for the MikroTik adapter (against a real or containerized
  RouterOS when feasible; otherwise clearly marked environment-gated tests).
- **API tests** for HTTP endpoints.
- Clear separation between **unit**, **integration**, and **end-to-end** tests
  (e.g., separate CTest labels/directories).
- Run relevant tests after each implementation step and fix failures before moving forward.

### Mandatory workflow before implementing

Before writing implementation code:

1. Analyze the challenge.
2. Propose the domain model.
3. Propose the architecture.
4. Identify bounded responsibilities.
5. Identify domain events.
6. Design the persistence model.
7. Design the MikroTik integration.
8. Design the test strategy.
9. **Wait for approval before implementing.**

### Incremental delivery

- Do **not** implement the whole project in one step.
- Work incrementally: small, reviewable steps, each with passing tests.

### Requirements discipline

- Never invent requirements not stated in the challenge.
- Any extra capability (auth, multi-tenancy, retries, caching, etc.) must be
  **explicitly marked as a design decision** and justified, or avoided.

## Code quality requirements

- Modern C++:RAII, `const` correctness, smart pointers where ownership requires them.
- Avoid global state (no singletons holding business state; inject dependencies).
- Meaningful error handling (exceptions or error types used consistently and deliberately).
- Clear naming; small cohesive classes; no premature abstractions.
- Prefer value semantics for domain value objects where practical.

## Quick commands (update as the build evolves)

```bash
# Configure and build
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build

# Run tests by label (adjust once targets exist)
ctest --test-dir build -L unit
ctest --test-dir build -L integration
ctest --test-dir build -L api

# Local infrastructure
docker compose up -d

# PostgreSQL integration tests (repositories) — env-gated, skip without these.
# Tests connect to the `inerxia_test` database (separate from production) unless
# PGDATABASE is explicitly set.
export PGUSER=inerxia PGPASSWORD=CHANGE_ME
ctest --test-dir build -L integration      # postgres_repository_tests runs against inerxia_test

# MikroTik integration tests (live RouterOS) — env-gated too.
export MIKROTIK_BASE_URL=... MIKROTIK_USER=... MIKROTIK_PASSWORD=...
ctest --test-dir build -L integration      # integration_tests runs against the router

# HTTP API tests (PostgreSQL required; router real if MIKROTIK_* set, otherwise
# router endpoints assert 503 via a test-only failing gateway).
export PGUSER=inerxia PGPASSWORD=CHANGE_ME
ctest --test-dir build -L api

# Run the HTTP API server (fail-fast: requires PostgreSQL + MikroTik credentials).
export PGUSER=inerxia PGPASSWORD=CHANGE_ME PGDATABASE=inerxia
export MIKROTIK_BASE_URL=... MIKROTIK_USER=... MIKROTIK_PASSWORD=...
# API_HOST/API_PORT optional (default 127.0.0.1:8484)
./build/src/api/inerxia_server
```

## Design decisions (recorded as required)

- **HTTP framework: cpp-httplib (header-only, yhirose)**, pinned via FetchContent. Picked
  over Drogon/Pistache/Beast because the challenge asks for a *lightweight* framework: it
  adds no transport/library dependencies beyond the standard sockets+pthreads and is a
  single header tree. See `src/api/CMakeLists.txt`.
- **Controllers never call repositories.** Contacts between HTTP and use cases only.
  Additional thin application services (`CreateSubscriber`, `GetSubscriber`, `CreatePlan`,
  `GetPlan`) were added solely so the life-cycle endpoints could be exercised end-to-end;
  `SubscriberRepository::next_id()` / `InternetPlanRepository::next_id()` follow the same
  identity pattern as the Contract/Payment repositories.
- **Composition root is manual DI** (`api::AppServices` + `src/api/main.cpp`), no DI
  framework. The API server binary fails fast when PostgreSQL or MikroTik credentials are
  missing.
- **Router failure semantics**: if RouterOS is unreachable, the affected endpoint answers
  503 (the domain mutation is persisted first; a retry is safe/idempotent). A failing
  stand-in gateway (throwing `RouterOSApiError`) exists **only in the API tests**; it never
  appears in production wiring.

## Repository conventions

- Keep the hexagonal layout obvious in the tree, e.g.:
  - `src/domain` — entities, value objects, domain events, repository interfaces, ports
  - `src/application` — use cases / application services
  - `src/infrastructure` — PostgreSQL, HTTP, MikroTik adapters
  - `tests/unit`, `tests/integration`, `tests/api` (or e2e)
- Never commit secrets (database credentials, RouterOS passwords); use environment
  variables and `.env.example` placeholders.
