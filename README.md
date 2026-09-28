# Inerxia — Billing & service management para ISP con MikroTik

Implementación del reto Inerxia: facturación y gestión de servicios para ISP
integrada con RouterOS. C++20, hexagonal/DDD, PostgreSQL, cpp-httplib.

## Documentación

| Manual | Contenido |
|---|---|
| [`docs/PROYECTO.md`](docs/PROYECTO.md) | Documentación integral del proyecto: requisitos, modelo de dominio (DDD), casos de uso, decisiones de diseño, tests |
| [`docs/INSTALL.md`](docs/INSTALL.md) | Manual de ejecución: instalar y arrancar en Windows (WSL2/nativo), Linux y macOS; PostgreSQL, router lab, variables, tests |
| [`docs/INTEGRACION.md`](docs/INTEGRACION.md) | Manual de implementación: cómo se integró todo — capas, puertos/adaptadores, flujos, auth, auditoría, router, sweep |
| [`docs/API.md`](docs/API.md) | Referencia completa de la API HTTP |
| [`docs/INFRASTRUCTURE.md`](docs/INFRASTRUCTURE.md) | Arquitectura, esquema de BD, migraciones y despliegue |
| [`AGENTS.md`](AGENTS.md) | Convenciones de la base de código y decisiones de diseño |
| [`scripts/lab/README.md`](scripts/lab/README.md) | Laboratorio MikroTik (CHR en QEMU) y demo de QA |

## Arranque rápido

```bash
cmake -S . -B build && cmake --build build -j        # solo primera vez / tras cambios
./scripts/run-inerxia.sh start                       # levanta todo + usuario demo
```

**Windows nativo (cmd, sin WSL):**

```bat
scripts\build-windows.cmd     REM vcpkg + MSVC -> build-vs\...\inerxia_server.exe
scripts\run-inerxia.cmd start REM levanta el servidor + usuario demo
```

Sigue las instrucciones de `scripts/` y abre el dashboard en
`http://inerxia.local:8484/ui` con **`demo` / `demo2026`**. Sin ese script, la
secuencia manual es:

```bash
scripts/lab/run-routeros.sh bootstrap && scripts/lab/run-routeros.sh start
scripts/lab/provision-routeros.sh
export PGUSER=inerxia PGPASSWORD=CHANGE_ME PGDATABASE=inerxia
export MIKROTIK_BASE_URL=http://127.0.0.1:8080/rest \
       MIKROTIK_USER=lab-admin MIKROTIK_PASSWORD=lab-router-password-2026
./build/src/api/inerxia_server
```

Dashboard (operator): `http://127.0.0.1:8484/ui` — o `http://inerxia.local:8484`
con `CANONICAL_DOMAIN=inerxia.local:8484` y el host mapeado en `hosts`.