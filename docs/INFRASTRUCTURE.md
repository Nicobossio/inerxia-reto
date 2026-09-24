# Inerxia — Infraestructura y despliegue

Documento de referencia de la arquitectura, la persistencia, las piezas de
infraestructura y los pasos para desplegar el sistema en el entorno local.

## 1. Stack

| Capa | Tecnología |
|---|---|
| Lenguaje | C++20 (`CMAKE_CXX_STANDARD 20`) |
| Build | CMake (multi-plataforma, `build/` para Debug) |
| Tests | GoogleTest, con `ctest` por etiquetas `unit` / `integration` / `api` |
| Persistencia | PostgreSQL (controlador `libpq`) |
| HTTP | cpp-httplib (header-only, yhirose) vía `FetchContent` — sin transporte adicional |
| Criptografía | OpenSSL `libcrypto` (PBKDF2-HMAC-SHA256) |
| Router | REST API de RouterOS (`libcurl`), con reintentos exponenciales |
| Infra local | Docker Compose (PostgreSQL); en este entorno corre PostgreSQL nativo |

## 2. Arquitectura (hexagonal)

```
src/
├── domain/          Entidades, value objects, eventos y reglas de negocio.
│                    Sin dependencias de frameworks.
├── application/     Casos de uso + puertos (interfaces de repositorios,
│                    RouterGateway, TimeProvider). Sin dependencias externas.
├── infrastructure/  Adaptadores: PostgreSQL, MikroTik, auth, scheduler.
├── api/             Controllers HTTP + DTOs + composición raíz (main.cpp).
│                    Sin lógica de negocio; solo traducción HTTP↔caso de uso.
tests/
├── unit/            Reglas de dominio y casos de uso (fakes en memoria).
├── integration/     Repositorios PostgreSQL + adaptador MikroTik live.
└── api/             Dto HTTP end-to-end (servidor real en puerto efímero).
```

Dependencias dirigidas hacia dentro: `api → application → domain` y
`infrastructure → (adapter de application)`. El dominio nunca conoce
PostgreSQL, HTTP ni MikroTik.

## 3. Persistencia (PostgreSQL)

### 3.1 Migraciones (`src/infrastructure/postgres/migrations.cpp`)

Aplicadas automáticamente al arrancar el binario (idempotente, registradas en
`schema_migrations`). Aisladas para tests: el arranque crea la base `inerxia_test`
(solo tests) aparte de la base de producción `inerxia`.

| v | nombre | contenido |
|---|---|---|
| 1 | `create_tables` | `internet_plans`, `subscribers`, `contracts`, `payments` |
| 2 | `referential_integrity_and_query_indexes` | FK RESTRICT sobre contratos; índices parciales/composites |
| 3 | `audit_log` | tabla `audit_logs` |
| 4 | `users` | tabla `users` (operadores) |

### 3.2 Esquema

**internet_plans** — `id` (PK), `name`, `download_mbps`, `upload_mbps`,
`monthly_price_cents` (checks: `>0`, nombre no vacío).

**subscribers** — `id` (PK), `name` (no vacío), `static_ip` (UNIQUE).

**contracts** — `id` (PK), `subscriber_id`→`subscribers` (ON DELETE RESTRICT),
`plan_id`→`internet_plans` (ON DELETE RESTRICT), `download_mbps`,
`upload_mbps`, `billing_start`, `due_date` (`due_date > billing_start`),
`price_per_period_cents`, `suspended_reason ∈ {manual, overdue} | NULL`.
Índices: `idx_contracts_subscriber_id`, `idx_contracts_due_date_active`
(parcial, solo no suspendidos — objetivo del sweep).

**payments** — `id` (PK), `contract_id`→`contracts` (ON DELETE CASCADE),
`amount_cents` (>0), `registered_on`. Índice composite
`(contract_id, registered_on)` para saldar balances.

**audit_logs** — `id BIGSERIAL` (PK), `occurred_at TIMESTAMPTZ`,
`actor`, `method`, `path`, `status`, `detail`. Índice por `occurred_at DESC`.

**users** — `id` (PK), `username` (UNIQUE, 3–32), `password_hash`,
`created_at`.

**schema_migrations** — `version` (PK), `name`, `applied_at`.

### 3.3 Conexión

Pool propio (`PostgresPool`) con config por entorno:

| Var | Requerida | Defecto |
|---|---|---|
| `PGHOST` | no | `127.0.0.1` |
| `PGPORT` | no | `5432` |
| `PGDATABASE` | sí (binario) | — |
| `PGUSER` | sí (binario) | — |
| `PGPASSWORD` | sí (binario) | — |
| `PGSSLMODE` | no | `disable` |

## 4. Integración MikroTik (RouterOS)

- **Puerto de dominio** (`application::RouterGateway`):
  `enableUser(id, ip) · disableUser(id, ip) · changeSpeedProfile(id, ip, speed)`.
- **Adaptador HTTP REST** (`infrastructure/mikrotik`): usa el endpoint REST de
  RouterOS (`…/rest`). Todas las operaciones son **idempotentes en el router**:
  re-emitirlas nunca duplica efectos, lo que hace seguro el reintento tras un
  503 y el re-sync post-pago.
- **Reintentos**: decorador `RetryingRouterGateway` envuelve cualquier gateway
  (HTTP y sweep usan la misma política). Solo reintenta fallos transitorios
  (transporte, errores `408/429/5xx`); `4xx` falla al instante. Política
  configurable (`RetryPolicy::from_env()`), backoff exponencial con tope,
  `max_attempts=1` desactiva los reintentos; tras agotarlos se relanza la
  excepción (→ `503`).
- **Logging** a stderr mediante `RouterLogSink` inyectable.

### Variables MikroTik (`src/infrastructure/mikrotik/MikrotikEnvConfig.cpp`)

| Var | Requerida | Defecto |
|---|---|---|
| `MIKROTIK_BASE_URL` | sí | — (`…/rest`) |
| `MIKROTIK_USER` | sí | — |
| `MIKROTIK_PASSWORD` | sí | — |
| `MIKROTIK_CONNECT_TIMEOUT_SECONDS` | no | `10` |
| `MIKROTIK_TIMEOUT_SECONDS` | no | `30` |
| `MIKROTIK_VERIFY_TLS` | no | `true` |
| `MIKROTIK_SUSPENDED_LIST` | no | `suspended` |
| `MIKROTIK_QUEUE_PREFIX` | no | `inerxia-` |
| `MIKROTIK_CONTRACT_COMMENT_PREFIX` | no | `inerxia:` |
| `MIKROTIK_BLOCKING_RULE_COMMENT` | no | `inerxia:baja-automatica` |
| `MIKROTIK_RETRY_MAX` | no | `3` |
| `MIKROTIK_RETRY_BACKOFF_MS` | no | `250` |
| `MIKROTIK_RETRY_MAX_BACKOFF_MS` | no | `2000` |
| `MIKROTIK_RETRY_MULTIPLIER` | no | `2` |

Modelo del router usado por el adaptador: lista de direcciones `suspended`
(bloqueo/desbloqueo por IP estática del suscriptor), colas por suscriptor
(`inerxia-<id>`) para el perfil de velocidad, y comentarios sobre objetos
(`inerxia:<contract_id>`) para trazabilidad.

## 5. Autenticación y sesiones

- **Operadores**: tabla `users`. Alta vía `POST /api/auth/register`;
  `AuthService` (application) valida, normaliza y delega el hashing.
- **Hashing**: `OpenSslPbkdf2Hasher` — PBKDF2-HMAC-SHA256, 210 000 iteraciones,
  sal de 16 bytes, clave de 32 bytes, comparación constante
  (`CRYPTO_memcmp`). Formato almacenado autodescriptivo:
  `pbkdf2-sha256$<iter>$<salt-hex>$<key-hex>`.
- **Sesiones**: `InMemorySessionStore` (thread-safe), token aleatorio de 64 hex
  (`random_device`), TTL por `AUTH_SESSION_TTL_MS` (defecto `43200000` ms = 12 h).
- **Gate HTTP**: `ApiServer::pre_routing` exige `Authorization: Bearer` en toda
  ruta privada; el actor viaja internamente en `X-Inerxia-Actor` que
  `post_routing` borra antes de responder.

## 6. Auditoría de cambios

`ApiServer::post_routing` inserta una fila en `audit_logs` por cada
`POST/PUT/PATCH/DELETE` en `/api/*`: actor, método, ruta, status, instante
ISO-8601 y un detalle corto derivado del body (auth nunca persiste el password).
Fallo de append → log a stderr, nunca rompe la respuesta. `GET /api/audit?limit=N`
(cota 500, más recientes primero) la expone al operador.

## 7. Scheduler de suspensión automática

`PeriodicScheduler` (`infrastructure/scheduler`): hilo `std::thread` +
`condition_variable`, sin cola externa. Primera ejecución inmediata, después un
intervalo tras cada vuelta completada (no encola overruns; nunca corre en
paralelo). Excepciones → `SchedulerLogSink` y continúa. `stop()` hace join
(apagado por señal SIGINT/SIGTERM vía hilo watchdog que llama a `server.stop()`
y `sweep.stop()`). Intervalo: `SWEEP_INTERVAL_MS` (defecto `60000`; `0`
desactiva el automático, queda el manual `POST /api/sweep/expired`).

## 8. Variables del servidor API

| Var | Requerida | Defecto |
|---|---|---|
| `PGDATABASE` / `PGUSER` / `PGPASSWORD` | sí (fail-fast) | — |
| `MIKROTIK_BASE_URL` / `MIKROTIK_USER` / `MIKROTIK_PASSWORD` | sí (fail-fast) | — |
| `API_HOST` | no | `127.0.0.1` |
| `API_PORT` | no | `8484` |
| `CANONICAL_DOMAIN` | no | (sin redirección) |
| `SWEEP_INTERVAL_MS` | no | `60000` (0 desactiva) |
| `AUTH_SESSION_TTL_MS` | no | `43200000` |

Plantilla: `.env.example` (nunca subir credenciales reales; `.env` está
ignorado por git).

## 9. DNS local

`inerxia.local` → `127.0.0.1` en el archivo `hosts` de Windows (entrada
`# inerxia.local`, añadida con PowerShell elevado/UAC:
`scripts`/Agentes añaden `Add-Content`). Es **resolución de cliente**: el
servidor escucha en `API_HOST` (loopback), así que `http://inerxia.local:8484`
abre el panel desde la misma máquina sin exponer puertos.

### Redirección canónica (CG dominio)

Con `CANONICAL_DOMAIN=inerxia.local:8484` el servidor contesta `301` a toda
petición de las páginas de navegación (`/`, `/ui`, `/swagger`, docs) cuyo
header `Host` no coincida con ese dominio. Así, si entras por
`http://127.0.0.1:8484` o `http://localhost:8484`, el navegador salta a
`http://inerxia.local:8484`: siempre operas con el nombre de dominio. Las rutas
`/api/*` están exentas, de modo que scripts, `curl` y tests conservan su host
habitual y no se ven afectados por la redirección.

## 10. Despliegue paso a paso (Linux/WSL2)

```bash
# 1. Dependencias del binario: libpq-dev, libcurl dev, OpenSSL dev,
#    PostgreSQL en ejecución (puerto 5432) con rol/db provisionados.

# 2. Configurar y compilar
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j

# 3. Pruebas
ctest --test-dir build -L unit        # dominio + casos de uso (sin servicio)
export PGUSER=inerxia PGPASSWORD=...  # y MIKROTIK_* si hay router live
ctest --test-dir build -L integration # repositorios (inerxia_test) + MikroTik live
ctest --test-dir build -L api         # HTTP end-to-end (PostgreSQL requerido)

# 4. Arrancar el servidor (fail-fast si faltan credenciales)
export PGUSER=inerxia PGPASSWORD=... PGDATABASE=inerxia
export MIKROTIK_BASE_URL=... MIKROTIK_USER=... MIKROTIK_PASSWORD=...
./build/src/api/inerxia_server     # http://127.0.0.1:8484 (inerxia.local:8484)

# 5. Alta del primer operador (solo API; el dashboard usa login)
curl -X POST http://inerxia.local:8484/api/auth/register \
     -H 'Content-Type: application/json' \
     -d '{"username":"operador_1","password":"S3cret!"}'
```

Alternativa contenedorizada solo para PostgreSQL:

```bash
docker compose up -d   # postgres:18, crea inerxia_test en el init
```

## 11. Verificación post-despliegue

```bash
curl http://inerxia.local:8484/api/health        # {"status":"ok"}
curl -o /dev/null -w '%{http_code}\n' http://inerxia.local:8484/ui   # 200
TOKEN=$(curl -s -X POST http://inerxia.local:8484/api/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"demo","password":"demo2026"}' | python3 -c \
  "import sys,json;print(json.load(sys.stdin)['token'])")
curl -s http://inerxia.local:8484/api/subscribers -H "Authorization: Bearer $TOKEN"
```

Credenciales de laboratorio: `demo` / `demo2026` (solo entorno local).