#!/usr/bin/env bash
# run-inerxia.sh — levanta (o apaga/consulta) todo el stack local sin complicaciones:
# PostgreSQL + router lab MikroTik + servidor API + usuario demo listo para entrar.
#
# Uso:
#   scripts/run-inerxia.sh start     # arranca todo (idempotente)
#   scripts/run-inerxia.sh stop      # apaga servidor, router y PostgreSQL
#   scripts/run-inerxia.sh status    # estado de las 3 piezas
#   scripts/run-inerxia.sh logs      # sigue el log del servidor
#
# Credenciales de TURNO para entrar al dashboard:
#   usuario: demo    contraseña: demo2026
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
export PATH="$HOME/cppenv/bin:$PATH"

PGDATA="${PGDATA:-$HOME/.cache/inerxia-pg/data}"
PG_LOG="$PGDATA/logfile"
PGUSER="${PGUSER:-inerxia}"
PGPASSWORD="${PGPASSWORD:-inerxia_secret}"
PGDATABASE="${PGDATABASE:-inerxia}"

MIKROTIK_BASE_URL="${MIKROTIK_BASE_URL:-http://127.0.0.1:8080/rest}"
MIKROTIK_USER="${MIKROTIK_USER:-lab-admin}"
MIKROTIK_PASSWORD="${MIKROTIK_PASSWORD:-lab-router-password-2026}"
CANONICAL_DOMAIN="${CANONICAL_DOMAIN:-inerxia.local:8484}"

API_URL="${API_URL:-http://127.0.0.1:8484}"
SERVER_BIN="$ROOT/build/src/api/inerxia_server"
PID_FILE="${PID_FILE:-/tmp/inerxia-api.pid}"
LOG_FILE="${LOG_FILE:-/tmp/inerxia-api.log}"

say()  { printf '\033[1m%s\033[0m\n' "$*"; }

db_up() { pg_ctl -D "$PGDATA" status >/dev/null 2>&1; }
router_up() { "$ROOT/scripts/lab/run-routeros.sh" status >/dev/null 2>&1; }
server_up() { [ -f "$PID_FILE" ] && kill -0 "$(cat "$PID_FILE")" 2>/dev/null; }

start_pg() {
    say "[postgres] comprobando..."
    if db_up; then say "[postgres] ya esta en marcha (port 5432)."
    else
        pg_ctl -D "$PGDATA" -l "$PG_LOG" start >/dev/null 2>&1
        say "[postgres] arrancado."
    fi
}

start_router() {
    say "[router] comprobando lab MikroTik..."
    if router_up; then say "[router] ya en marcha."
    else
        "$ROOT/scripts/lab/run-routeros.sh" start
        "$ROOT/scripts/lab/provision-routeros.sh"
        say "[router] listo (REST en $MIKROTIK_BASE_URL)."
    fi
}

start_server() {
    say "[api] comprobando servidor..."
    if server_up; then say "[api] ya en marcha (pid $(cat "$PID_FILE"))."
    else
        PGUSER=$PGUSER PGPASSWORD=$PGPASSWORD PGDATABASE=$PGDATABASE \
        MIKROTIK_BASE_URL=$MIKROTIK_BASE_URL MIKROTIK_USER=$MIKROTIK_USER \
        MIKROTIK_PASSWORD=$MIKROTIK_PASSWORD CANONICAL_DOMAIN=$CANONICAL_DOMAIN \
        nohup "$SERVER_BIN" > "$LOG_FILE" 2>&1 &
        echo $! > "$PID_FILE"
        disown
        for _ in $(seq 1 20); do
            curl -sf "$API_URL/api/health" >/dev/null 2>&1 && break
            sleep 1
        done
        say "[api] arrancado (pid $(cat "$PID_FILE"))."
    fi
}

ensure_demo_user() {
    say "[auth] comprobando usuario demo..."
    code=$(curl -s -o /dev/null -w '%{http_code}' -X POST "$API_URL/api/auth/register" \
        -H 'Content-Type: application/json' \
        -d '{"username":"demo","password":"demo2026"}' || true)
    case "$code" in
        201) say "[auth] usuario demo creado. -> demo / demo2026" ;;
        409) say "[auth] usuario demo ya existe (ok)." ;;
        *)   say "[auth] aviso: no pude comprobar el alta (HTTP $code)" ;;
    esac
}

cmd_start() {
    start_pg
    start_router
    start_server
    ensure_demo_user
    echo
    say "== TODO LEVANTADO =="
    say "  Dashboard: http://inerxia.local:8484/ui"
    say "  Alternativo: http://127.0.0.1:8484/ui"
    say "  Usuario: demo    Contrasena: demo2026"
    say "  Log del servidor: $LOG_FILE"
}

cmd_stop() {
    say "[api] apagando..."
    if server_up; then kill -TERM "$(cat "$PID_FILE")"; rm -f "$PID_FILE"; fi
    say "[router] apagando lab..."
    "$ROOT/scripts/lab/run-routeros.sh" stop 2>/dev/null || true
    say "[postgres] apagando..."
    pg_ctl -D "$PGDATA" stop -m fast >/dev/null 2>&1 || true
    say "Todo apagado."
}

cmd_status() {
    db_up      && say "[postgres] en marcha"      || say "[postgres] PARADO"
    router_up  && say "[router]   en marcha"      || say "[router]   PARADO"
    server_up  && say "[api]      en marcha (pid $(cat "$PID_FILE"))" || say "[api]      PARADO"
    curl -s "$API_URL/api/health" >/dev/null 2>&1 && say "[api]      /api/health -> ok"
}

cmd_logs() { tail -f "$LOG_FILE"; }

case "${1:-start}" in
    start)  cmd_start  ;;
    stop)   cmd_stop   ;;
    status) cmd_status ;;
    logs)   cmd_logs   ;;
    *) echo "uso: $0 {start|stop|status|logs}" ; exit 2 ;;
esac