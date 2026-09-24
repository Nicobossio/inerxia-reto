# Inerxia — Documentación de la API

Referencia completa del panel HTTP del sistema de facturación y gestión de
servicio para ISP con integración MikroTik.

## 1. Acceso

| Campo | Valor |
|---|---|
| URL base (DNS local) | `http://inerxia.local:8484` |
| URL alternativa (loopback) | `http://127.0.0.1:8484` |
| Interfaz | `GET /ui` — panel del operador (raíz `GET /` redirige a él) |
| Documentación interactiva | `GET /swagger` (Swagger UI) |
| Especificación OpenAPI 3.0 | `GET /api/openapi.json` |
| Formato | JSON, `Content-Type: application/json` |
| Fechas | ISO-8601 `YYYY-MM-DD` |
| Importes | céntimos de euro (enteros positivos) |

El nombre `inerxia.local` se resuelve por la zona `hosts` de Windows
(`127.0.0.1`): el servidor sigue escuchando en la dirección de `API_HOST`
(por defecto `127.0.0.1`).

## 2. Autenticación

Sesión de operador con `Authorization: Bearer <token>`.

- **Públicas** (sin token): `GET /api/health`, `GET /api/openapi.json`,
  `POST /api/auth/register`, `POST /api/auth/login`, `POST /api/auth/logout`,
  y todo lo que no empieza por `/api/` (dashboard, swagger, raíz).
- **Privadas**: el resto de rutas `/api/*`. Sin token válido responden
  `401 {"error":"unauthorized","message":"Missing or invalid operator session"}`.
- **Sesión**: el token caduca por defecto a las 12 h (`AUTH_SESSION_TTL_MS`).
  Ante un `401`, el dashboard limpia el token y pide volver a entrar.
- **Registro**: `POST /api/auth/register` está disponible (los operadores se
  crean ahí), aunque el dashboard de producción ya no expone el formulario de
  alta: solo login.

### POST /api/auth/register — alta de operador
```json
{"username": "operador_1", "password": "S3cret!"}
```
- `201` → `{"username": "operador_1"}`
- Validación: normalización (trim + minúsculas), 3–32 caracteres `[a-z0-9._-]`.
- `409 username_taken` (duplicado) · `422 registration_rule` (input inválido).

### POST /api/auth/login — inicio de sesión
```json
{"username": "operador_1", "password": "S3cret!"}
```
- `200` → `{"token": "<64 hex>", "username": "operador_1"}`
- `401 unauthorized` → `{"error":"unauthorized","message":"Invalid username or password"}`

### POST /api/auth/logout — cierre de sesión
- Sin cuerpo; el token viaja en el header. `204` (revoca la sesión en memoria).

### GET /api/auth/me — estado de sesión
- `200` → `{"authenticated": true, "username": "operador_1"}`
- `401` si el token no es válido.

## 3. Formato de errores

Toda respuesta de error usa el mismo envelope:

```json
{"error": "<codigo>", "message": "<descripcion humana>"}
```

| HTTP | Código | Cuándo |
|---|---|---|
| 400 | `bad_request` | JSON malformado, tipo erróneo, fecha no `YYYY-MM-DD` o inexistente, `limit` inválido |
| 401 | `unauthorized` | Token ausente/vencido/incorrecto; credenciales de login incorrectas |
| 404 | `not_found` | Id de entidad inexistente |
| 409 | `username_taken` | Nombre de operador ya registrado |
| 422 | `domain_rule` | Regla de negocio violada (p. ej. contrato no suspendible) |
| 422 | `registration_rule` | Registro de operador inválido |
| 503 | `infrastructure` | RouterOS/postgres inalcanzable. En operaciones que mutan el dominio, la mutación **ya se persistió** antes de notificar |
| 500 | `internal` | Fallo no clasificado |

## 4. Clientes (subscribers)

### POST /api/subscribers — alta de cliente
```json
{"name": "Ana García", "static_ip": "10.99.20.10"}
```
- `201` → `{"id":"<uuid>","name":"Ana García","static_ip":"10.99.20.10"}`
- `name` no vacío y `static_ip` única (colisión → `422 domain_rule`).

### GET /api/subscribers — inventario (lista)
- `200` → array de `{"id","name","static_ip"}`.

### GET /api/subscribers/{id} — detalle de cliente
- `200` → mismo objeto; `404 not_found` si no existe.

## 5. Planes de internet

### POST /api/plans — alta de plan
```json
{"name": "Fibra 300", "download_mbps": 300, "upload_mbps": 150, "monthly_price_cents": 15000}
```
- `201` → `{"id":"<uuid>","name":"Fibra 300","download_mbps":300,"upload_mbps":150,"monthly_price_cents":15000}`
- Velocidades y precio > 0 (si no, `422 domain_rule`).

### GET /api/plans/{id} — detalle de plan
- `200` → mismo objeto; `404 not_found`.

## 6. Contratos

El contrato copia la velocidad y el precio del plan en el momento de crearse
(snapshot, no referencias vivas). ID de contrato, `subscriber_id` y `plan_id`
son UUID.

### POST /api/contracts — alta de contrato
```json
{
  "subscriber_id": "<uuid>",
  "plan_id": "<uuid>",
  "billing_start": "2026-08-01",
  "due_date": "2026-11-30"
}
```
- `201` → `ContractView` completo (ver §6.4). Regla: `due_date > billing_start`
  (si no, `422 domain_rule`). `404` si el suscriptor o el plan no existen.

### GET /api/contracts — lista de contratos con estado computado
- `200` → array de `ContractView`. El campo `status` se computa con la fecha de
  hoy del servidor: `active` (al corriente o pagado), `overdue` (vencido sin
  saldar, aún no suspendido), `suspended` (suspendido manual o por vencimiento).
- Lo usa el módulo «Ciclo de vida» del dashboard para saber quién se renueva
  (`active`) y quién se cancela (`suspended`/`overdue`).

### GET /api/contracts/{id} — detalle de contrato
- `200` → `ContractView`; `404 not_found`.

### PUT /api/contracts/{id} — reprogramar vencimiento
```json
{"due_date": "2026-12-31"}
```
- `200` → `ContractView` actualizado. Edición de `due_date` (renegociación).
- `422 domain_rule` si la nueva fecha no respeta `due_date > billing_start`.

### POST /api/contracts/{id}/suspend — suspensión manual
- `200` → `ContractView` con `status: suspended`, `suspended: true`.
- Llama al router (deshabilita al usuario). Router inalcanzable → `503`, pero la
  suspensión queda persistida (seguro de reintentar: idempotente).

### POST /api/contracts/{id}/reactivate — reactivación
- `200` → `ContractView` con `status: active`, `suspended: false`.
- Requiere que el contrato no esté saldado-partial: la reactivación manual no es
  posible si el contrato está vencido sin pagar (→ `422 domain_rule`); el pago
  total es la vía automática.

### POST /api/contracts/{id}/payments — registro de pago
```json
{"amount_cents": 15000, "paid_on": "2026-11-15"}
```
- `201` → `{"id":"<uuid>","contract_id":"<uuid>","amount_cents":15000,"registered_on":"2026-11-15"}`
- **Idempotente por contenido**: un pago idéntico `(contract_id, amount_cents,
  paid_on)` no se cobra dos veces; devuelve el pago ya existente y re-emite el
  `enableUser()` si el contrato quedaba saldado (convergencia router/datos tras
  un `503`).
- Un pago parcial sobre contrato suspendido **no** lo reactiva (deuda no
  saldada) y nunca toca el router. Si la deuda queda saldada, se reactiva y se
  llama a `enableUser()`.

### PATCH /api/contracts/{id}/speed-profile — cambio de perfil
```json
{"download_mbps": 600, "upload_mbps": 300}
```
- `200` → `ContractView` con el nuevo perfil. Cambia colas en el router
  (idempotente). Router inalcanzable → `503` (mutación persistida).

### ContractView (respuesta JSON completa)
```json
{
  "id": "<uuid>",
  "subscriber_id": "<uuid>",
  "plan_id": "<uuid>",
  "download_mbps": 300,
  "upload_mbps": 150,
  "billing_start": "2026-08-01",
  "due_date": "2026-11-30",
  "price_per_period_cents": 15000,
  "suspended": false,
  "status": "active",
  "payments": []
}
```
`status ∈ {active, overdue, suspended}`; `payments` es un array de
`{"id","contract_id","amount_cents","registered_on"}` ordenado por fecha.

## 7. Suspensión automática (sweep)

### POST /api/sweep/expired
- Sin cuerpo. Suspende todo contrato vencido sin pagar (llama
  `disableUser()` en el router) y reactiva los contratos suspendidos que se
  hayan saldado entre tanto.
- `200` → `{"suspended_contract_ids": ["<uuid>", ...]}` (vacio si no hay
  cambios).
- Un scheduler interno (`SWEEP_INTERVAL_MS`, por defecto 60000 ms) lo ejecuta
  solo; `0` lo desactiva y queda el disparo manual.

## 8. Auditoría de cambios

### GET /api/audit?limit=N
- `N` entre 1 y 500 (por defecto 50; a partir de 500 se recorta). Más recientes
  primero.
- `200` → array de entradas:
```json
{
  "id": 1,
  "occurred_at": "2026-10-02T14:03:00Z",
  "actor": "operador_1",
  "method": "POST",
  "path": "/api/contracts/.../suspend",
  "status": 200,
  "detail": "descripción corta del body"
}
```
- Se registra **todo** `POST/PUT/PATCH/DELETE` en `/api/*` (en la frontera HTTP,
  no en el dominio). Las rutas de auth solo registran "user registration /
  login / logout" — nunca el password.

## 9. Salud

### GET /api/health
- `200` → `{"status":"ok"}` cuando PostgreSQL responde; `503` si no.

## 10. Notas de diseño relevantes para el cliente HTTP

1. **Fallos de router ≠ pérdida de datos**: ante `503 infrastructure`, la
   mutación ya está persistida; el cliente puede reintentar con seguridad
   (operaciones de router idempotentes, pagos idempotentes por contenido).
2. **Errores `4xx` siempre dentro del envelope** `{"error","message"}`; el
   dashboard muestra `message`.
3. Las cabeceras internas (`X-Inerxia-Actor`) nunca salen del servidor.