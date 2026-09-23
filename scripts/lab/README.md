# Lab RouterOS (MikroTik CHR 7.16.1) — rootless local router

This folder brings up a **real RouterOS** instance on QEMU (MikroTik CHR), so the
MikroTik integration is exercised against genuine RouterOS API surface instead of
mocks. No Docker or root required: it uses QEMU with the user-mode (slirp) network
and a serial-console socket for initial provisioning.

## Quick start

```bash
scripts/lab/run-routeros.sh bootstrap       # one-time: QEMU toolchain + CHR image (into ~/.cache/inerxia-lab)
scripts/lab/run-routeros.sh status          # check the router is up
scripts/lab/run-routeros.sh start           # boot CHR and wait for REST to answer
scripts/lab/provision-routeros.sh            # idempotent: user, IP, www service (REST)
scripts/lab/verify-routeros.sh              # REST round-trips for the 3 operations + blocking rule
scripts/lab/run-routeros.sh stop
```

`start` automatically waits for the REST API to answer once the image already has
config; the first boot additionally needs `provision-routeros.sh` because a fresh
CHR has no IP and an admin password-change wizard.

## Ports (host -> guest)

| Host      | Guest | Usage                     |
|-----------|-------|---------------------------|
| `8080`    | 80    | REST API / webfig (`www`) |
| `8022`    | 22    | SSH                       |
| `8443`    | 443   | `www-ssl` (REST over TLS) |
| `8291`    | 8291  | Winbox                    |

The guest runs at `10.0.2.15/24` on its ether interface (slirp subnet, gateway
`10.0.2.2`).

## Credentials (local lab only, never used in production)

| Account | Username | Password (default)    |
|---------|----------|-----------------------|
| admin   | `admin`  | `lab-admin-2026`      |
| app     | `lab-admin` | `lab-router-password-2026` |

Set `MIKROTIK_USER` / `MIKROTIK_PASSWORD` / `LAB_ADMIN_PASSWORD` to override.
Create/refresh a local `.env` (gitignored) with:

```
MIKROTIK_BASE_URL=http://127.0.0.1:8080/rest
MIKROTIK_USER=lab-admin
MIKROTIK_PASSWORD=lab-router-password-2026
```

## What “verified” means here

`verify-routeros.sh` calls the exact REST operations the production adapter uses and
checks the router state:

- `disableUser`    -> `PUT /ip/firewall/address-list` (adds `suspended` list entry)
- `enableUser`     -> `DELETE /ip/firewall/address-list/<.id>`
- `changeSpeedProfile` -> `PUT /queue/simple` with `target=<ip>/32` + `max-limit`
- blocking rule    -> `PUT /ip/firewall/filter` (`chain=forward action=drop src-address-list=suspended`)

The env-gated integration tests in `tests/integration` drive the same operations
through the production libcurl transport:

```bash
export MIKROTIK_BASE_URL=http://127.0.0.1:8080/rest
export MIKROTIK_USER=lab-admin
export MIKROTIK_PASSWORD=lab-router-password-2026
cmake --build build
ctest --test-dir build -L integration --output-on-failure
```

Without those three variables the integration tests skip (CI stays green without a
router).

## Notes / operational details

- CHR download: `https://cdn.mikrotik.com/routeros/7.16.1/chr-7.16.1.img.zip`.
- QEMU is used from packages extracted into `~/.cache/inerxia-lab/qemu-root`
  (Ubuntu `apt-get download` + `dpkg-deb -x`) and run with a custom `LD_LIBRARY_PATH`,
  so no system install is needed.
- Firmware (seabios `bios-256k.bin`, ROMs) is assembled in `~/.cache/inerxia-lab/qemu-fw`
  and passed with `-L`.
- The console talks RouterOS through a Unix socket (`cons.sock`); provisioning is a
  short Python driver that answers the first-boot license/password wizards and runs
  idempotent RouterOS commands (`:if` guards) so it is safe to re-run.
- TCG emulation is slow: boot takes ~1–2 minutes. Give it time.