# Inerxia — Manual de integración (cómo se integró todo)

Este manual explica **cómo se conectan todas las piezas**: de la petición HTTP
al router MikroTik pasando por el dominio, y cómo encajan repositorios, auth,
auditoría, scheduler y tests. Es el mapa de "quién llama a quién".

Índice:
- [`INSTALL.md`](INSTALL.md): instalación y arranque por plataforma.
- [`INFRASTRUCTURE.md`](INFRASTRUCTURE.md): componentes, BD y entorno.
- [`API.md`](API.md): referencia HTTP.
- **Este archivo**: integración interna y wiring.

## 1. Arquitectura en una imagen

```
┌────────────── Capa API (src/api) ──────────────┐
│  ApiServer ─ controllers ─ DTOs                 │
│  pre_routing: auth ─ post_routing: auditoría    │
└───────────────┬─────────────────────────────────┘
                │  solo casos de uso
┌───────────────▼──────── Capa aplicación ────────┐
│  Use cases (CreateContract, SuspendContract,    │
│  RegisterPayment, …)                            │
│  AuthService, ListContracts, ListSubscribers    │
│  │              │              │                │
│  │ ports        │ putos       puerto            │
│  └ Repositorios (interfaces)  RouterGateway     │
└───────────────┬──────────────┬┴─────────────────┘
                │              │
┌───────────────▼───────────┐  ┌──────────────────▼─────────────┐
│ Infra PostgreSQL          │  │ Infra MikroTik                 │
│  PgPool + repos concretos │  │  REST adapter + RetryingRouter │
│  (migraciones v1–v4)      │  └────────────────────────┬───────┘
└───────────────┬───────────┘                           │
                │ libpq                                 │ libcurl / REST
        ┌───────▼────────┐                      ┌───────▼───────────┐
        │  PostgreSQL    │                      │ RouterOS (real)   │
        └────────────────┘                      └───────────────────┘

El dominio (src/domain) no depende de nada externo: define entidades,
value objects y el agregado Contract. Las flechas apuntan siempre hacia dentro
(depende → interface declarada en application/ports).
```

## 2. De la petición HTTP al router (secuencia tipo: suspensión)

```
POST /api/contracts/{id}/suspend
  Browser ──► ApiServer.pre_routing
              │  1) cabe "X-Inerxia-Actor: anonymous" por defecto
              │  2) ¿ruta pública? no  →  3) AuthService.authenticate(token)
              │     si no hay sesión válida → 401 (short-circuit)
              │     si valida → header interno "X-Inerxia-Actor: <operador>"
              ▼
  ContractController.handle_suspend  (sin lógica de negocio)
              │  parsea el id, delega
              ▼
  application::SuspendContract use case
              │  1) ContractRepository.findById → si falta → 404
              │  2) dominio: contract.suspend() → estado Suspended
              │     (agrega ContractSuspendedEvent al agregado, ver §4)
              │  3) ContractRepository.save
              │  4) RouterGateway.disableUser(contract_id, static_ip)
              ▼
  RetryingRouterGateway (decorador)
              │  reintenta fallos transitorios con backoff (MIKROTIK_RETRY_*)
              ▼
  RouterOS REST: /ip/firewall/address-list …  (bloquea al suscriptor)
              │
              ▼
  responde a la respuesta del controller → ContractView JSON
  ApiServer.post_routing: auditoría ("Suspensión de contrato") + borra
  el header interno → 200 {} al cliente
```

Reglas transversales:
- **Los controllers no llaman a repositorios**: solo convierten HTTP↔caso de
  uso y DTOs.
- **El orden dominio→router es fijo**: la mutación se persiste **antes** de
  tocar el router. Si el router falla, la API responde `503 infrastructure`
  pero el estado ya está en PostgreSQL y reintentar es seguro (operaciones de
  router idempotentes).
- Los **eventos de dominio** se registran en el agregado `Contract`
  (`ContractSuspendedEvent`, `PaymentRegisteredEvent`,
  `ContractReactivatedEvent`, `SpeedProfileChangedEvent`) como punto de
  extensión DDD; hoy la reacción (llamar al router, el sweep) la disparan los
  propios casos de uso, no un bus de eventos.

## 3. Puertos y adaptadores

Regla: la aplicación declara interfaces (ports) en `src/application/ports/`;
la infraestructura las implementa; el wiring se inyecta manualmente en el
composition root (§5).

| Puerto (interfaz) | Implementación | Uso |
|---|---|---|
| `ContractRepository` | `PostgresContractRepository` | CRUD del agregado (incluye pagos) + `all()` para el read-model |
| `SubscriberRepository` | `PostgresSubscriberRepository` | clientes + `find_all()` (inventario) |
| `InternetPlanRepository` | `PostgresInternetPlanRepository` | planes |
| `PaymentRepository` | `PostgresPaymentRepository` | movimientos (saldo) |
| `UserRepository` | `PostgresUserRepository` | operadores (auth) |
| `AuditRepository` | `PostgresAuditRepository` | traza de cambios |
| `PasswordHasher` | `OpenSslPbkdf2Hasher` | PBKDF2-HMAC-SHA256, 210k iter. |
| `SessionStore` | `InMemorySessionStore` | sesiones de operador (TTL) |
| `TimeProvider` | `SystemTimeProvider` | "hoy" para status/sweep (falso en tests) |
| `RouterGateway` | `MikrotikRestRouterGateway` (envuelto por `RetryingRouterGateway`) | enable/disable/perfil en RouterOS |

## 4. El dominio y el agregado `Contract`

- `Contract` copia velocidad y precio del plan en el alta (snapshot): el plan
  puede cambiar sin romper facturación. Es el **agregado**: es la única entidad
  que se carga con sus pagos.
- `ContractStatus {Active, Overdue, Suspended}` se computa con
  `status_as_of(hoy)` — regla de negocio que la API expone como `status`.
- `suspended_reason {manual, overdue}` se persiste en la tabla para saber *por
  qué* se cortó el servicio.
- Reglas clave: `due_date > billing_start`; suspención manual de un contrato
  ya suspendido → error de dominio (422); reactivación manual de un contrato
  vencido sin saldar → 422 (la vía correcta es saldar la deuda); pago parcial
  sobre suspendido **no** reactiva.
- **Idempotencia de pagos**: `RegisterPayment` devuelve el pago ya existente
  si `(contract_id, amount_cents, paid_on)` coincide; así un reintento tras 503
  nunca cobra dos veces y, si el contrato quedaba saldado, re-llama
  `enableUser()` para converger PostgreSQL (activo) con RouterOS (habilitado).

## 5. Composition root (inyección manual)

No hay framework de DI. `src/api/main.cpp` y `AppServices` construyen el grafo:

```
main.cpp
  ├─ PostgresConfig::from_env() → PostgresPool (fail-fast si faltan PG*)
  ├─ apply_migrations(pool)                 ← DDL se crea solo
  ├─ make_mikrotik_router_gateway_from_env() (fail-fast si faltan MIKROTIK_*)
  ├─ SystemTimeProvider
  ├─ AppServices{pool, *router, clock}      ← construye TODOS los casos de uso
  │     ├─ CreateContract, GetContract, ListContracts, UpdateContract,
  │     │  SuspendContract, ReactivateContract, ChangeSpeedProfile,
  │     │  RegisterPayment, EvaluateExpiredContracts
  │     └─ CreateSubscriber/GetSubscriber/ListSubscribers,
  │        CreatePlan/GetPlan
  └─ ApiServer{services, pool, canonical_host}
        ├─ auth (UserRepository + PasswordHasher + SessionStore + AuthService)
        ├─ audit (AuditRepository)
        └─ controllers con los casos de uso como referencias
```

Los controllers reciben **referencias** a los casos de uso (baratos, con
estado solo en los repositorios reales), por eso `list_contracts` y `get` viven
en `AppServices`.

## 6. Autenticación (cómo se integra)

- `pre_routing` decide rutas públicas: `health`, `openapi.json`,
  `auth/register`, `auth/login`, `auth/logout` y todo lo que no empieza por
  `/api/`. El resto exige `Authorization: Bearer <token>`.
- `AuthService` valida/crea usuarios (username normalizado, PBKDF2) y emite
  tokens aleatorios de 64 hex guardados en `SessionStore`.
- El **actor viaja en el header interno** `X-Inerxia-Actor` que `post_routing`
  borra antes de escribir la respuesta: el cliente nunca lo ve.
- El alta de operadores sigue siendo posible por API
  (`POST /api/auth/register`); el dashboard solo ofrece login.

## 7. Auditoría (cómo se integra)

`post_routing` inserta en `audit_logs` cada `POST/PUT/PATCH/DELETE` de
`/api/*` con actor (del header interno), método, ruta, status e instante ISO.
El `detail` se deriva del body (autorizaciones jamás guardan el password). Si
`append` falla, se loguea a stderr y **no** se rompe la respuesta ya generada.
El dashboard la muestra traducida a acciones legibles (Alta de cliente,
Suspensión de contrato, …) vía `auditAction()`.

## 8. Router MikroTik (cómo se integra)

- El caso de uso solo conoce `RouterGateway {enableUser, disableUser,
  changeSpeedProfile}`; nada de RouterOS en el dominio.
- El adaptador REST (`infrastructure/mikrotik`) habla el REST de RouterOS
  (`…/rest`) y materializa cada operación así:
  - `disableUser` → IP del suscriptor a la lista de direcciones `suspended`
    (regla actual de bloqueo).
  - `enableUser` → quitar la IP de esa lista.
  - `changeSpeedProfile` → ajustar la cola `inerxia-<contract_id>`.
  - comentarios `inerxia:<contract_id>` para trazabilidad.
- Todas idempotentes → repetibles sin efectos duplicados.
- `RetryingRouterGateway` decora cualquier gateway: reintenta solo fallos
  transitorios (transporte y 408/429/5xx) con backoff exponencial configurable;
  agotado, relanza → la API mapea `503 infrastructure`.
- Mismo decorador para el **HTTP** y para el **sweep** → política única.

## 9. Sweep (suspensión/reactivación automática)

Dos disparadores del mismo caso de uso `EvaluateExpiredContracts`:
1. **Scheduler** `PeriodicScheduler` (hilo + condition_variable) cada
   `SWEEP_INTERVAL_MS` (defecto 60 s; `0` lo desactiva). Corre la primera vez
   inmediatamente, nunca en paralelo, y `stop()` hace join (apagado por señal).
2. **Manual** `POST /api/sweep/expired`.

Qué hace: consulta contratos activos vencidos (índice parcial
`idx_contracts_due_date_active`), los suspende por `overdue` y llama
`disableUser`; y reactiva los suspendidos que queden saldados
(`enableUser`) convergiendo BD↔router.

## 10. Redirección canónica (acceso por dominio)

`ApiServer::redirect_to_canonical` (env `CANONICAL_DOMAIN`): si una petición a
`/`, `/ui` o `/swagger` llega con un `Host` distinto del canónico, responde
`301 Location: http://<canonical><ruta>`. `/api/*` está exenta → curl, scripts
y tests no se ven afectados. El dashboard usa rutas relativas, así que tras la
primera redirección todo el tráfico ya viaja al dominio.

## 11. Read-models del dashboard (integridad de la integración)

- **Inventario** (`GET /api/subscribers`): `ListSubscribers` →
  `SubscriberRepository::find_all` → `SubscriberView`.
- **Ciclo de vida** (`GET /api/contracts`): `ListContracts` →
  `ContractRepository::all()` + `status_as_of(hoy)` → `ContractView` con
  `status` (active/overdue/suspended). La UI hace join con los clientes por
  `subscriber_id` y muestra el contrato más reciente de cada uno.
- **Detalle de contrato** (`GET /api/contracts/{id}`): mismo `ContractView` con
  `payments` resuelto del agregado; el dashboard añade nombre/IP del cliente y
  nombre del plan consultando sus endpoints.

## 12. Cómo se integran los tests

| Etiqueta (`ctest -L`) | Qué cubre | Cómo |
|---|---|---|
| `unit` | reglas de dominio y casos de uso | fakes en memoria (`FakeContractRepository`, `FakeRouterGateway`, `FakeTimeProvider`); sin servicios |
| `integration` | repositorios PostgreSQL + adaptador MikroTik live | base `inerxia_test` (nunca la real) y RouterOS real (CHR/QEMU) vía libcurl; env-gated |
| `api` | contrato HTTP end-to-end | `ApiServer` real en puerto efímero + hilo worker; router real si `MIKROTIK_*`, si no, `FailingRouterGateway` (503) |

Los tests `api` construyen el mismo composition root que producción
(`AppServices` + `ApiServer`), con `FakeTimeProvider` para fechas
deterministas y router real/falso según entorno.

## 13. Mapa de archivos

```
src/domain/           entidades, value objects, eventos, errores
src/application/use_cases/  18 casos de uso (1 por operación)
src/application/ports/      interfaces (10 puertos)
src/application/services/   AuthService
src/infrastructure/postgres/ pool + 7 repositorios + migraciones
src/infrastructure/mikrotik/ REST adapter, RetryingRouterGateway, config
src/infrastructure/auth/     PBKDF2 hasher + session store
src/infrastructure/scheduler/ PeriodicScheduler
src/api/               ApiServer, controllers, DTOs, json_utils, openapi.json
tests/unit|integration|api   GoogleTest por capa
scripts/lab/          router lab (QEMU CHR) + demo de QA
deploy/postgres/init/  01-create-test-database.sql
```