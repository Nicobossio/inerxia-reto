# Inerxia — Manual de instalación y ejecución (múltiples dispositivos)

Guía para descargar, compilar y arrancar el proyecto desde cero en Linux,
Windows (WSL2 o nativo) y macOS, incluyendo PostgreSQL, el router MikroTik
(lab) y el acceso por dominio.

Índice de manuales:
- **Este archivo**: instalación y arranque por plataforma.
- [`docs/INFRASTRUCTURE.md`](INFRASTRUCTURE.md): arquitectura, esquema de BD,
  variables de entorno y despliegue en detalle.
- [`docs/API.md`](API.md): referencia completa de la API HTTP.
- [`docs/INTEGRACION.md`](INTEGRACION.md): cómo se integraron todas las piezas.

## 1. Qué trae el proyecto

| Pieza | Cómo se ejecuta |
|---|---|
| Servidor HTTP (`inerxia_server`) | Binario C++ que escucha en `API_HOST:API_PORT` (defecto `127.0.0.1:8484`) |
| Base de datos | PostgreSQL (nativo o vía `docker compose`) |
| Router MikroTik | RouterOS real usando la REST API; el lab local corre un CHR bajo QEMU |
| Tests | GoogleTest vía `ctest` (etiquetas `unit`, `integration`, `api`) |

Dependencias de compilación:
- Compilador con soporte **C++20** (GCC ≥ 10, Clang ≥ 14, MSVC 2022).
- **CMake ≥ 3.20** y un generador (Make/Ninja).
- **OpenSSL** (headers + `libcrypto`) para hashing de contraseñas.
- **libpq** (headers) para el cliente PostgreSQL.
- **libcurl** (headers) para el adaptador REST de MikroTik.
- Red para el primer `cmake --build`: cpp-httplib, nlohmann/json y GoogleTest
  se descargan con `FetchContent` (pinned).
- Python 3 (opcional: scripts de laboratorio y demo).

## 2. Por plataforma

### 2.1 Ubuntu / Debian (y WSL2)

```bash
sudo apt update
sudo apt install -y build-essential cmake pkg-config \
    libssl-dev libcurl4-openssl-dev libpq-dev postgresql
git clone <repositorio> inerxia-reto && cd inerxia-reto
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
```

### 2.2 Fedora

```bash
sudo dnf install -y gcc-c++ cmake pkgconfig openssl-devel libcurl-devel \
    libpq-devel postgresql-server
# igual que arriba: cmake -S . -B build && cmake --build build -j
```

### 2.3 macOS (Intel y Apple Silicon)

```bash
brew install cmake pkg-config openssl@3 libpq curl
brew install postgresql@16   # o usa docker compose (ver §4-B)
git clone <repositorio> inerxia-reto && cd inerxia-reto
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug \
    -DOPENSSL_ROOT_DIR="$(brew --prefix openssl@3)" \
    -DCMAKE_PREFIX_PATH="$(brew --prefix libpq)"
cmake --build build -j
```

### 2.4 Windows

- **Recomendado: WSL2** (Ubuntu). Seguir §2.1 dentro de WSL; el binario y el
  servicio se gestionan desde la terminal de WSL. Windows accede vía
  `http://localhost:8484` o `http://inerxia.local:8484`.
  - Si Windows no alcanza el puerto del servidor en WSL, comprueba
    `wsl --version` → `networkingMode`:
    - `mirrored`: `localhost` es bidireccional (recomendado, ya activo en este
      proyecto).
    - `nat`: arranca el servidor con `API_HOST=0.0.0.0` para que Windows llegue
      por `localhost:8484`.
- **Nativo (avanzado)**: con MSYS2/Mingw-w64 (packages `mingw-w64-ucrt-x86_64`
  de gcc, cmake, openssl, libpq, libcurl) o MSVC 2022 + CMake. La compilación
  nativa de libpq/libcurl/OpenSSL es engorrosa; solo se recomienda si no se
  puede usar WSL.

## 3. Compilación genérica (todas las plataformas)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug   # configura
cmake --build build -j                         # compila binario y tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release # opcional: perfil de producción
```

Resultado: `build/src/api/inerxia_server`.

## 4. Base de datos (PostgreSQL)

### Opción A — PostgreSQL nativo

```bash
sudo -u postgres psql <<'SQL'
CREATE ROLE inerxia LOGIN PASSWORD 'cambia-esto';
CREATE DATABASE inerxia OWNER inerxia;
SQL
# Opcional: la base de tests (solo la usan las pruebas de integración)
sudo -u postgres psql -d inerxia -f deploy/postgres/init/01-create-test-database.sql
```

Las tablas se crean solas al arrancar el servidor (migraciones v1–v4,
idempotentes, registradas en `schema_migrations`).

### Opción B — PostgreSQL en Docker

```bash
PGUSER=inerxia PGPASSWORD=cambia-esto docker compose up -d
```

Crea el contenedor con los usuarios/tablas iniciales y la base `inerxia_test`.

## 5. Router MikroTik (RouterOS)

### 5.1 Sin router hardware (lab local, recomendado)

Scripts sin Docker ni sudo que arrancan un RouterOS CHR real bajo QEMU
(user-mode network) y lo provisionan:
```bash
scripts/lab/run-routeros.sh bootstrap   # una vez: descarga QEMU + imagen CHR 7.16.1
scripts/lab/run-routeros.sh start       # arranca y espera la consola
scripts/lab/provision-routeros.sh       # usuario API, www/REST, red, ruta
scripts/lab/verify-routeros.sh          # comprueba la REST API de extremo a extremo
scripts/lab/run-routeros.sh status      # estado
scripts/lab/run-routeros.sh stop
```
Servicio REST local: `http://127.0.0.1:8080/rest`, usuario `lab-admin`,
contraseña `lab-router-password-2026` (sobrescribibles con entorno).

### 5.2 Router hardware / máquina real

Necesitas un usuario RouterOS con derechos suficientes (grupo `full`) y el
servicio `www`/REST habilitado (`/ip service set www disabled=no`). Además el
adaptador usa por convención:
- lista de direcciones `suspended` (bloqueo/desbloqueo por IP estática),
- colas por suscriptor con prefijo `inerxia-` (perfiles de velocidad),
- comentarios `inerxia:<contract_id>` como trazabilidad.

Todo es configurable (ver variables `MIKROTIK_*` en `docs/INFRASTRUCTURE.md` §4).

> Sin router no se puede **arrancar el servidor**: falla rápido si faltan
> `MIKROTIK_BASE_URL`, `MIKROTIK_USER` y `MIKROTIK_PASSWORD`. Para una demo solo
> de API/UI puedes apuntarlos a un servicio REST falso que responda; en los
> tests ya existe ese stand-in (devuelve 503).

## 6. Variables de entorno

Resumen (todas las cuáles, en `docs/INFRASTRUCTURE.md` §4 y §8; plantilla en
`.env.example`):

| Variable | Requerida | Defecto |
|---|---|---|
| `PGHOST` / `PGPORT` | no | `127.0.0.1:5432` |
| `PGDATABASE` | sí (arranque) | — |
| `PGUSER` / `PGPASSWORD` | sí (arranque) | — |
| `PGSSLMODE` | no | `disable` |
| `MIKROTIK_BASE_URL` / `MIKROTIK_USER` / `MIKROTIK_PASSWORD` | sí (arranque) | — |
| `API_HOST` / `API_PORT` | no | `127.0.0.1:8484` |
| `CANONICAL_DOMAIN` | no | (sin redirección) |
| `SWEEP_INTERVAL_MS` | no | `60000` (0 desactiva) |
| `AUTH_SESSION_TTL_MS` | no | `43200000` |
| `MIKROTIK_RETRY_*` | no | reintentos con backoff exponencial |

## 7. Ejecutar el servidor

```bash
export PGUSER=inerxia PGPASSWORD=cambia-esto PGDATABASE=inerxia
export MIKROTIK_BASE_URL=http://127.0.0.1:8080/rest
export MIKROTIK_USER=lab-admin MIKROTIK_PASSWORD=lab-router-password-2026
# Opcional: forzar el acceso por dominio
export CANONICAL_DOMAIN=inerxia.local:8484

./build/src/api/inerxia_server
# -> Inerxia API listening on http://127.0.0.1:8484
```

Primer arranque: aplica migraciones, arranca el scheduler de suspensión y
empieza a servir. Ctrl+C apaga servidor y scheduler limpiamente.

## 8. Acceso por dominio (`inerxia.local`)

La resolución es **de cliente** (no DNS público): cada dispositivo mapea el
nombre a `127.0.0.1` en su zona local y abre `http://inerxia.local:8484`.

- **Windows (elevado, PowerShell)**:
  ```powershell
  Add-Content "$env:windir\System32\drivers\etc\hosts" "127.0.0.1 inerxia.local"
  ```
- **Linux / WSL / macOS**:
  ```bash
  sudo sh -c 'echo "127.0.0.1 inerxia.local" >> /etc/hosts'
  ```

Con `CANONICAL_DOMAIN=inerxia.local:8484` el servidor responde `301` a las
páginas de navegación (`/`, `/ui`, `/swagger`) cuando el header `Host` no es el
dominio: si entras por `127.0.0.1` o `localhost`, el navegador termina siempre
en `inerxia.local`. La API (`/api/*`) nunca se redirige.

## 9. Pruebas

```bash
ctest --test-dir build -L unit        # dominio + casos de uso (sin servicios)
export PGUSER=inerxia PGPASSWORD=cambia-esto
ctest --test-dir build -L integration # repositorios Postgres (inerxia_test) + router live
ctest --test-dir build -L api         # API end-to-end (PostgreSQL requerido)
```

## 10. Verificación final

```bash
curl http://127.0.0.1:8484/api/health                    # {"status":"ok"}
curl -o /dev/null -w '%{http_code}\n' http://127.0.0.1:8484/ui
# registra un operador de prueba (la primera vez) y loguea
curl -s -X POST http://127.0.0.1:8484/api/auth/register \
  -H 'Content-Type: application/json' \
  -d '{"username":"demo","password":"demo2026"}'
TOKEN=$(curl -s -X POST http://127.0.0.1:8484/api/auth/login \
  -H 'Content-Type: application/json' \
  -d '{"username":"demo","password":"demo2026"}' | python3 -c \
  "import sys,json;print(json.load(sys.stdin)['token'])")
curl -s http://127.0.0.1:8484/api/subscribers -H "Authorization: Bearer $TOKEN"
```

Demo end-to-end (QA de cada requisito contra API + router real):
```bash
MIKROTIK_BASE_URL=http://127.0.0.1:8080/rest \
  scripts/lab/demo-challenge.sh
```

## 11. Solución de problemas típicos

| Síntoma | Causa / fix |
|---|---|
| `Missing required environment variable(s)` al arrancar | Faltan `PGDATABASE/PGUSER/PGPASSWORD` o `MIKROTIK_*`: exportarlos (§6). |
| `could not translate host name` / `connection refused` | PostgreSQL no responde en `PGHOST:PGPORT`; arrancarlo (servicio o `docker compose up -d`). |
| `Failed to start: Port already in use` | Cambia `API_PORT` o cierra lo que ocupa 8484. |
| `find_package` no encuentra OpenSSL/libpq/curl | Instala los `-dev`/headers de tu distro (§2) y reconfigure: `cmake -S . -B build`. |
| Windows no alcanza `localhost:8484` desde WSL | `networkingMode` en `nat`: arrancar con `API_HOST=0.0.0.0` (o activar `mirrored`). |
| `inerxia.local` no resuelve | Falta la línea en el `hosts` de ese dispositivo (§8) o el navegador cacheó DNS: recargar/flush. |
| Router devuelve 503 en operaciones | RouterOS inalcanzable; la mutación ya está en BD — reintentar es seguro (idempotente). |
| `libpq` de RedHat/Fedora headless | Probar vía Docker (§4-B) si la instalación de headers falla. |