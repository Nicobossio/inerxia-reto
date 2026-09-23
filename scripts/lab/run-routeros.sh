#!/usr/bin/env bash
# Lab RouterOS (MikroTik CHR) on a rootless QEMU — start/stop/status/bootstrap.
#
# Why no Docker: this environment has neither Docker nor passwordless sudo, so we
# drive a real RouterOS CHR image under QEMU with the user-mode (slirp) network.
# The router is REAL (actual RouterOS API), which is what the challenge requires.
#
# Usage:
#   scripts/lab/run-routeros.sh bootstrap   # one-time: fetch QEMU toolchain + CHR image
#   scripts/lab/run-routeros.sh start       # boot CHR and wait for the console login
#   scripts/lab/run-routeros.sh stop
#   scripts/lab/run-routeros.sh status
#   scripts/lab/run-routeros.sh wait        # poll the REST API until it answers (200)
#
# Env overrides:
#   MIKROTIK_USER / MIKROTIK_PASSWORD  credentials provisioned on the router
#   CACHE_DIR                          where toolchain + image live (default ~/.cache/inerxia-lab)

set -euo pipefail

CACHE_DIR="${CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/inerxia-lab}"
ROOT_DIR="$CACHE_DIR/qemu-root"
FW_DIR="$CACHE_DIR/qemu-fw"
CHR_DIR="$CACHE_DIR/chr"
CHR_IMG="$CHR_DIR/chr-7.16.1.img"
PID_FILE="$CHR_DIR/qemu.pid"
SOCK_FILE="$CHR_DIR/cons.sock"
CHR_VERSION="7.16.1"
CHR_URL="https://cdn.mikrotik.com/routeros/${CHR_VERSION}/chr-${CHR_VERSION}.img.zip"

QEMU_BIN="$ROOT_DIR/usr/bin/qemu-system-x86_64"
LD_PATH="$ROOT_DIR/usr/lib/x86_64-linux-gnu"

# Host forwards: 8080->REST/www, 8022->SSH, 8443->www-ssl, 8291->winbox.
HOSTFWD_ARGS="hostfwd=tcp::8080-:80,hostfwd=tcp::8022-:22,hostfwd=tcp::8443-:443,hostfwd=tcp::8291-:8291"

QEMU_PKGS="qemu-system-x86 qemu-system-data qemu-system-data-hwe qemu-system-common \
ubuntu-virt ubuntu-virt-hwe ubuntu-helper-virt-hwe qemu-system-data-hwe \
ipxe-qemu seabios seabios-hwe libslirp0 libfdt1 libaio1t64 libasound2-data libasound2t64 \
libbrlapi0.8 libcacard0 libdaxctl1 libdebian-installer4 libibverbs1 libjpeg-turbo8 \
libjpeg8 libmpathcmd0 libmpathpersist0 libmultipath0 libndctl6 libnl-3-200 libnl-route-3-200 \
libnspr4 libnss3 libnuma1 libpcsclite1 libpmem1 librdmacm1t64 libtextwrap1 liburcu8t64 \
liburing2 libusb-1.0-0 libusbredirparser1t64 cdebconf"

log() { printf '[lab-routeros] %s\n' "$*"; }

stop() {
    if [ -f "$PID_FILE" ]; then
        kill "$(cat "$PID_FILE")" 2>/dev/null || true
        rm -f "$PID_FILE"
    fi
    pkill -f "$ROOT_DIR/usr/bin/qemu-system" 2>/dev/null || true
    log "stopped (if it was running)"
}

bootstrap() {
    mkdir -p "$ROOT_DIR" "$FW_DIR" "$CHR_DIR"
    if [ -x "$QEMU_BIN" ] && [ -s "$FW_DIR/bios-256k.bin" ] && [ -s "$CHR_IMG" ]; then
        log "toolchain + image already present in $CACHE_DIR"
        return
    fi
    if [ ! -x "$QEMU_BIN" ]; then
        log "downloading QEMU packages (rootless, into $ROOT_DIR)..."
        mkdir -p "$CACHE_DIR/debs"
        for pkg in $QEMU_PKGS; do
            apt-get download "$pkg" -o Dir::Cache::archives="$CACHE_DIR/debs" \
                >/dev/null 2>&1 || log "note: package '$pkg' unavailable (not fatal)"
        done
        for deb in "$CACHE_DIR"/debs/*.deb; do
            [ -e "$deb" ] && dpkg-deb -x "$deb" "$ROOT_DIR"
        done
    fi
    if [ ! -s "$FW_DIR/bios-256k.bin" ]; then
        log "assembling firmware dir (seabios + qemu roms)..."
        cp -a "$ROOT_DIR/usr/share/seabios/." "$FW_DIR/" 2>/dev/null || true
        cp -a "$ROOT_DIR/usr/share/qemu/." "$FW_DIR/" 2>/dev/null || true
        cp -a "$ROOT_DIR/usr/share/ipxe/." "$FW_DIR/" 2>/dev/null || true
    fi
    if [ ! -s "$CHR_IMG" ]; then
        log "downloading CHR $CHR_VERSION image..."
        curl -fL --retry 3 -o "$CHR_DIR/chr.zip" "$CHR_URL"
        python3 - "$CHR_DIR/chr.zip" "$CHR_IMG" <<'EOF'
import sys, zipfile
with zipfile.ZipFile(sys.argv[1]) as z:
    for name in z.namelist():
        if name.endswith(".img"):
            with open(sys.argv[2], "wb") as out:
                out.write(z.read(name))
EOF
        rm -f "$CHR_DIR/chr.zip"
    fi
    log "bootstrap done"
}

start() {
    stop
    if [ ! -x "$QEMU_BIN" ] || [ ! -s "$FW_DIR/bios-256k.bin" ] || [ ! -s "$CHR_IMG" ]; then
        log "missing toolchain — running bootstrap first"
        bootstrap
    fi
    rm -f "$SOCK_FILE"
    log "booting CHR (TCG emulation, may take a minute)..."
    LD_LIBRARY_PATH="$LD_PATH" "$QEMU_BIN" -daemonize -pidfile "$PID_FILE" \
        -machine pc,accel=tcg -cpu qemu64 -m 512 -smp 1 \
        -drive "file=$CHR_IMG,format=raw,if=ide" \
        -netdev "user,id=lan0,$HOSTFWD_ARGS" \
        -device e1000,netdev=lan0,mac=08:00:27:11:22:33 \
        -display none -serial "unix:$SOCK_FILE,server=on,wait=off" -monitor none \
        -L "$FW_DIR" >"$CHR_DIR/qemu.out" 2>&1
    log "qemu running (pid $(cat "$PID_FILE"))"
}

# -- wait for the console "MikroTik Login:" (rootless console via unix socket)
await_login() {
    python3 - "$SOCK_FILE" <<'EOF'
import socket, re, sys, time
path = sys.argv[1]
deadline = time.time() + 360
s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(3)
while time.time() < deadline:
    try:
        s.connect(path)
        break
    except OSError:
        time.sleep(2)
else:
    print("console socket never appeared"); sys.exit(1)
buf = b""
while time.time() < deadline:
    try:
        c = s.recv(4096)
        if not c:
            time.sleep(1); continue
        buf += c
        if b"Login:" in buf:
            print("console up: MikroTik Login reached"); sys.exit(0)
    except socket.timeout:
        pass
print("timed out waiting for console login"); sys.exit(1)
EOF
}

wait_rest() {
    local user="${MIKROTIK_USER:-lab-admin}" pass="${MIKROTIK_PASSWORD:-lab-router-password-2026}"
    local url="${MIKROTIK_BASE_URL:-http://127.0.0.1:8080/rest}"
    for i in $(seq 1 120); do
        if curl -s --max-time 3 -u "$user:$pass" -o /dev/null "$url/system/identity"; then
            log "REST API up ($url) after ${i}0s-ish"
            return 0
        fi
        sleep 5
    done
    log "REST API did not answer in time"
    return 1
}

case "${1:-}" in
    bootstrap) bootstrap ;;
    start) start; await_login || log "console not up yet; check $CHR_DIR/qemu.out"; wait_rest || true ;;
    stop) stop ;;
    status)
        if [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; then
            log "qemu running (pid $(cat "$PID_FILE"))"
        else
            log "qemu not running"
        fi
        ;;
    wait) wait_rest ;;
    *) echo "usage: $0 {bootstrap|start|stop|status|wait}"; exit 1 ;;
esac