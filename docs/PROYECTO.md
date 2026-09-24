# Inerxia — Documentación del proyecto

Descripción completa del sistema: qué es, cómo se modeló el dominio, qué casos
de uso soporta, qué decisiones de diseño se tomaron, cómo se prueba y dónde
vive cada pieza. Es la vista integral; los manuales operativos están en el
índice de abajo.

Índice de documentación:
- [`README.md`](../README.md): índice general y arranque rápido.
- [`INSTALL.md`](INSTALL.md): manual de **ejecución** por dispositivo/plataforma.
- [`INTEGRACION.md`](INTEGRACION.md): manual de **implementación/integración**
  interna (wiring, flujos, puertos y adaptadores).
- [`API.md`](API.md): referencia HTTP del panel.
- [`INFRASTRUCTURE.md`](INFRASTRUCTURE.md): infraestructura, esquema de BD y
  despliegue.

## 1. Qué es

Sistema de facturación y gestión de servicio para un ISP basado en **C++20**
con arquitectura **hexagonal** y **DDD**. Asigna planes de internet a clientes,
gestiona el ciclo de vida del contrato (alta, consulta, edición, suspensión y
reactivación), registra pagos manuales, **suspende automáticamente** los
contratos vencidos sin pagar, **reactiva automáticamente** al saldar la deuda y
aplica los cambios en un **router MikroTik real** (habilitar/deshabilitar el
servicio y cambiar el perfil de velocidad). Persistencia en PostgreSQL. Exposición
HTTP con panel de operador y auditoría de cambios.

Requisitos funcionales del reto y dónde se resuelven:

| Requisito | Implementación |
|---|---|
| Plan de internet asignado | `InternetPlan` + `Contract.plan_id` (copia snapshot de velocidad/precio) |
| Contrato con megas contratados | `SpeedProfile` dentro de `Contract` |
| Fecha de facturación | `billing_start` (regla: `due_date > billing_start`) |
| Fecha de vencimiento | `due_date`, reprogramable vía `PUT /api/contracts/{id}` |
| Registro de pago manual | `POST /api/contracts/{id}/payments` (idempotente por contenido) |
| Ciclo de vida (crear/editar/consultar/suspender/reactivar) | `POST|GET|PUT /api/contracts…`, `…/suspend`, `…/reactivate` |
| Suspensión automática al vencer | sweep `EvaluateExpiredContracts` (scheduler o `POST /api/sweep/expired`) |
| Reactivación automática tras pago | pago que salda la deuda → `activateUser` + `enableUser()` |
| Integración MikroTik | puerto `RouterGateway` + adaptador REST RouterOS real |
| Cambio de perfil de velocidad | `PATCH /api/contracts/{id}/speed-profile` → colas en el router |

## 2. Modelo de dominio (lenguaje ubicuo)

### Entidades

- **`Subscriber`** — cliente del ISP (nombre + IP estática única). La IP estática
  es la identidad operativa del suscriptor en el router.
- **`InternetPlan`** — catálogo de velocidades y precio mensual.
- **`Contract`** — el **agregado**. Asocia un suscriptor a un plan, fija
  `billing_start`, `due_date`, perfil de velocidad y precio (snapshot del plan en
  el alta), y es el único que se carga con sus **pagos**. Estados:
  `Active`, `Overdue` (vencido no saldado, aún con servicio), `Suspended`
  (motivo `Manual` o `Overdue`).
- **`Payment`** — pago manual registrado contra un contrato (`amount`, fecha).

### Value objects

- **`SpeedProfile`** — par `(download_mbps, upload_mbps)`, ambas > 0.
- **`Money`** — importes en **céntimos** (entero de 64 bits); aritmética
  `constexpr`, operadores de suma/resta, `is_positive()`.
- **`IPAddress`** — IPv4 estática del suscriptor.
- **`Id<Tag>`** — identificadores tipados por entidad (`ContractId`,
  `SubscriberId`, `PlanId`, `PaymentId`), nunca `std::string` pelado.
- **`ContractStatus` / `SuspensionReason`** — estados y causas del ciclo de vida.

### Invariantes del dominio (todas con tests unitarios)

- Un contrato nuevo: `due_date > billing_start`.
- Pagos con importe positivo; el balance `balance_as_of(hoy)` = precio − pagos
  hasta la fecha; deuda saldada ⇔ `balance_as_of(hoy) ≤ 0`.
- `status_as_of(hoy)`: vencido sin saldar → `Overdue`; suspendido → `Suspended`;
  resto activo; las suspensiones `Overdue` nunca "se caen" solas.
- Reglas de transición: no se puede suspender manualmente lo ya suspendido;
  no se puede reactivar manualmente un contrato **vencido sin saldar** (la vía es
  pagar); un **pago parcial** sobre suspendido no reactiva (deuda no saldada) y
  jamás toca el router; cambiar de perfil está permitido en cualquier estado;
  reprogramar `due_date` debe respetar `due_date > billing_start`.
- La suspensión se registra en el agregado (`suspended_reason`) y los eventos de
  dominio (`ContractSuspendedEvent`, `ContractReactivatedEvent`,
  `PaymentRegisteredEvent`, `SpeedProfileChangedEvent`) quedan disponibles vía
  `take_events()` como punto de extensión DDD; la reacción actual la ejecutan los
  casos de uso que tocan el router (no hay bus de eventos).

## 3. Casos de uso (capa aplicación)

| Caso de uso | Operación |
|---|---|
| `CreateSubscriber` / `GetSubscriber` / `ListSubscribers` | alta, detalle, inventario |
| `CreatePlan` / `GetPlan` | catálogo |
| `CreateContract` / `GetContract` / `ListContracts` / `UpdateContract` | ciclo de vida |
| `SuspendContract` · `ReactivateContract` | suspensión/reactivación manual + router |
| `RegisterPayment` | pago manual (idempotente) + reactivación al saldar |
| `ChangeSpeedProfile` | cambio de perfil + router |
| `EvaluateExpiredContracts` | sweep automático (suspende vencidos, reactiva saldados) |
| `AuthService` | registro/login/logout/me de operadores |

Contratos de puertos que la aplicación espera (`src/application/ports/`):
`ContractRepository`, `SubscriberRepository`, `InternetPlanRepository`,
`PaymentRepository`, `UserRepository`, `AuditRepository`, `RouterGateway`,
`TimeProvider`, `PasswordHasher`, `SessionStore`.

## 4. Arquitectura y bounded responsibilities

```
api → application → domain        (dependencias hacia dentro)
infrastructure = adaptadores de los puertos (postgres, mikrotik, auth, scheduler)
```

- **`domain/`**: reglas y lenguaje de negocio. Sin frameworks, ni PostgreSQL, ni
  RouterOS.
- **`application/`**: casos de uso y puertos. Sin detalles de HTTP/BD/router.
- **`infrastructure/`**: implementa los puertos (repositorios PostgreSQL,
  adaptador REST MikroTik + `RetryingRouterGateway`, hasher PBKDF2, sesiones en
  memoria, `PeriodicScheduler`).
- **`api/`**: HTTP. Controllers **sin lógica de negocio** (traducen JSON↔caso de
  uso), DTOs, gates de auth (pre_routing) y auditoría (post_routing), composición
  raíz (DI manual).

Boundaries y porqué: el dominio habla «enable/disable service y speed profile»,
nunca «ip firewall address-list»; el agregado carga pagos para decidir saldo sin
consultas idiosincráticas; los read-models (`ListSubscribers`, `ListContracts`)
son lecturas puras que alimentan el dashboard sin contaminar el agregado.

### Decisiones de diseño (resumen; detalle en `AGENTS.md`)

1. **cpp-httplib** header-only (ligero, sin transporte extra).
2. **DI manual** en el composition root (`AppServices` + `main.cpp`); fail-fast
   si faltan `PG*` / `MIKROTIK_*`.
3. **Router tras persistir**: la mutación se guarda antes de llamar al router;
   ante fallo → `503` con reintento seguro (operaciones idempotentes en RouterOS).
4. **Pagos idempotentes por contenido** — reintentos nunca cobran dos veces.
5. **`RetryingRouterGateway`** decorador con política exponencial (HTTP y sweep
   comparten la misma).
6. **Scheduler std-only** (`std::thread` + `condition_variable`), sin colas.
7. **Auth siempre on** (aditiva al reto) con PBKDF2-SHA256 y sesiones con TTL.
8. **Auditoría en la frontera HTTP** (post_routing), no en el dominio.
9. **Redirección canónica** `CANONICAL_DOMAIN` para operar siempre por dominio.
10. **Eventos de dominio** modelados en el agregado como extensión, sin bus aún.

## 5. Estrategia de tests

| Etiqueta `ctest -L` | Alcance | Dependencias |
|---|---|---|
| `unit` | invariantes del dominio + casos de uso | fakes en memoria (`Fake…Repository`, `FakeRouterGateway`, `FakeTimeProvider`) |
| `integration` | repositorios PostgreSQL + adaptador MikroTik live | `inerxia_test` + RouterOS real (CHR/QEMU) |
| `api` | contrato HTTP end-to-end | servidor real en puerto efímero; router real o `FailingRouterGateway` (503) |

API-C = 100 % verde en las tres suites (verificado). Las de integración/API están
env-gated: se saltan si faltan credenciales.

## 6. Esquema de datos (resumen)

`internet_plans`, `subscribers` (IP única), `contracts` (snapshot de velocidad y
precio, `suspended_reason`), `payments`, `audit_logs`, `users`,
`schema_migrations` (migraciones v1–v4 aplicadas automáticamente al arrancar).
Detalle y DDL en `docs/INFRASTRUCTURE.md` §3.

## 7. Ubicación de archivos clave

```
src/domain/            contract, subscriber, internet_plan, payment, value objects, eventos
src/application/use_cases/  18 casos de uso
src/application/ports/      10 interfaces
src/infrastructure/postgres/ pool, 7 repositorios, migraciones
src/infrastructure/mikrotik/ REST adapter + retry + config de entorno
src/infrastructure/auth/     PBKDF2 hasher, sesiones
src/infrastructure/scheduler/ PeriodicScheduler
src/api/               ApiServer, controllers, DTOs, dashboard, openapi.json
tests/unit|integration|api   GoogleTest
scripts/lab/           router lab (QEMU CHR), provision/verificación, demo QA
deploy/postgres/init/  bootstrap de la base de tests
```

## 8. Cómo seguir (la hoja de ruta de implementación)

El proyecto se construyó incrementalmente; este es el orden lógico que podrías
reproducir/ampliar:
1. Dominio puro (entidades, value objects, invariantes) + tests unitarios.
2. Puertos y casos de uso con fakes (cada regla de negocio cubierta).
3. Adapter PostgreSQL (migraciones + repositorios) + tests de integración.
4. Adapter MikroTik REST + reintentos + tests de integración con el lab.
5. Capa HTTP (controllers, DTOs, gate de auth, auditoría, scheduler) + tests API.
6. Read-models y dashboard del operador.
7. Documentación (estos manuales) y demo de QA (`scripts/lab/demo-challenge.sh`).

Extensiones naturales ya dejadas preparadas: consumir `take_events()` con
listeners (proyecciones, notificaciones) cuando el sistema añada contexto de
facturación/facturas; repositorio de audit SQL; sesiones en Postgres si el
servicio se escala a varios nodos.