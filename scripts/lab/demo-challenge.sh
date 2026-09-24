#!/usr/bin/env bash
#
# demo-challenge.sh — end-to-end verification of every functional requirement of
# the Inerxia challenge against the LIVE stack (PostgreSQL + MikroTik RouterOS +
# HTTP API). It is a QA walkthrough, not business logic: it drives the public
# HTTP API with curl and cross-checks the real router state over RouterOS REST.
#
# Returns 0 when every requirement passes, non-zero otherwise. Prints a
# requirement-by-requirement checklist.
#
# Required (env): MIKROTIK_BASE_URL, MIKROTIK_USER, MIKROTIK_PASSWORD.
# Defaults point at the local lab (scripts/lab/run-routeros.sh).
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"

BASE="${API_BASE_URL:-http://127.0.0.1:8484}"
RT="${MIKROTIK_BASE_URL:-http://127.0.0.1:8080/rest}"
RT_USER="${MIKROTIK_USER:-lab-admin}"
RT_PASS="${MIKROTIK_PASSWORD:-lab-router-password-2026}"

FAILURES=0
BODY=/tmp/inerxia-demo-body.json
RUN_TAG=$((RANDOM % 200))
IPA="10.99.$RUN_TAG.10"
IPB="10.99.$RUN_TAG.11"
DEMO_PASS="demo-operator-pass"

log() { printf '\n\033[1m%s\033[0m\n' "$*"; }
pass() { printf '  \033[32mPASS\033[0m %s\n' "$*"; }
fail() { printf '  \033[31mFAIL\033[0m %s\n' "$*"; FAILURES=$((FAILURES + 1)); }

req() { # method path data -> echoes HTTP code, leaves body in $BODY
    curl -s -o "$BODY" -w '%{http_code}' -X "$1" -H 'Content-Type: application/json' \
        ${TOKEN:+-H "Authorization: Bearer $TOKEN"} "$BASE$2" ${3:+-d "$3"}
}

json() { python3 -c 'import json,sys
d=json.load(open("/tmp/inerxia-demo-body.json"))
for k in sys.argv[1:]:
    d=d[k]
print(d if not isinstance(d,list) else json.dumps(d))' "$@"; }

router_has_ip() { # ip -> 0 if an address-list entry exists for that ip
    curl -s -u "$RT_USER:$RT_PASS" "$RT/ip/firewall/address-list" | python3 -c "
import json,sys
for e in json.load(sys.stdin):
    if e.get('address') == '$1': print('.id=' + e['.id']); break
" | grep -q .""
}

router_queue_for() { # ip -> exact max-limit if a queue exists
    curl -s -u "$RT_USER:$RT_PASS" "$RT/queue/simple" | python3 -c "
import json,sys
for e in json.load(sys.stdin):
    if e.get('target') == '$1/32': print(e.get('max-limit','')); break
"
}

start_server() {
    export PGUSER=${PGUSER:-inerxia} PGPASSWORD=${PGPASSWORD:-inerxia_secret}
    export PGDATABASE=${PGDATABASE:-inerxia} SWEEP_INTERVAL_MS=0
    export MIKROTIK_BASE_URL="$RT" MIKROTIK_USER="$RT_USER" MIKROTIK_PASSWORD="$RT_PASS"
    "$ROOT/build/src/api/inerxia_server" >/tmp/inerxia-demo-server.log 2>&1 &
    SRV=$!
    for _ in $(seq 1 50); do
        curl -sf "$BASE/api/health" >/dev/null && return 0
        sleep 0.2
    done
    return 1
}

# Auth is always on: register a throwaway operator and log in so req() is
# authorized. The dashboard follows the same flow (register is the landing page).
register_operator() {
    DEMO_USER="demo_${RUN_TAG}"
    curl -s -X POST -H 'Content-Type: application/json' \
        -d "{\"username\":\"$DEMO_USER\",\"password\":\"$DEMO_PASS\"}" \
        "$BASE/api/auth/register" >/dev/null
    TOKEN=$(
        curl -s -X POST -H 'Content-Type: application/json' \
            -d "{\"username\":\"$DEMO_USER\",\"password\":\"$DEMO_PASS\"}" \
            "$BASE/api/auth/login" \
            | python3 -c 'import json,sys; print(json.load(sys.stdin).get("token",""))'
    )
    [ -n "$TOKEN" ] && log "Operador registrado: $DEMO_USER" || log "Aviso: no se pudo autenticar"
}
stop_server() { [ -n "${SRV:-}" ] && kill -TERM "$SRV" 2>/dev/null; return 0; }
trap stop_server EXIT

if ! curl -sf "$BASE/api/health" >/dev/null; then
    if ! start_server; then
        echo "Could not reach the API at $BASE (server failed to boot; see /tmp/inerxia-demo-server.log)"
        exit 1
    fi
    echo "API server started (pid $SRV)"
fi
register_operator

# ---------------------------------------------------------------------------
log "R1. Plan de Internet asignado a un usuario ISP (subscriber + plan + contrato)"
CODE=$(req POST /api/subscribers "{\"name\":\"María Demo\",\"static_ip\":\"$IPA\"}")
[ "$CODE" = "201" ] && SUB=$(json id) || fail "crear subscriber (estado $CODE)"
CODE=$(curl -s -o "$BODY" -w '%{http_code}' -X POST -H 'Content-Type: application/json' \
    ${TOKEN:+-H "Authorization: Bearer $TOKEN"} "$BASE/api/plans" \
    -d '{"name":"Fibra 100","download_mbps":100,"upload_mbps":50,"monthly_price_cents":12000}')
[ "$CODE" = "201" ] && PLAN=$(json id) || fail "crear plan (estado $CODE)"
CODE=$(req POST /api/contracts "{\"subscriber_id\":\"$SUB\",\"plan_id\":\"$PLAN\",\"billing_start\":\"2026-07-01\",\"due_date\":\"2026-10-31\"}")
[ "$CODE" = "201" ] && CTRA=$(json id) || fail "crear contrato (estado $CODE)"
[ -n "$SUB" ] && [ -n "$PLAN" ] && [ -n "$CTRA" ] && pass "subscriber=$SUB plan=$PLAN contract=$CTRA" || fail "ids vacíos"

log "R2. Contrato con megas contratados + R3 billing_start + R4 due_date"
[ "$(req GET /api/contracts/$CTRA)" = "200" ] || fail "GET contrato no 200"
[ "$(json download_mbps)" = "100" ] && [ "$(json upload_mbps)" = "50" ] && pass "megas contratados 100/50" || fail "megas: got $(json download_mbps)/$(json upload_mbps)"
[ "$(json billing_start)" = "2026-07-01" ] && pass "billing_start=2026-07-01" || fail "billing_start=$(json billing_start)"
[ "$(json due_date)" = "2026-10-31" ] && pass "due_date=2026-10-31" || fail "due_date=$(json due_date)"
[ "$(json status)" = "active" ] && pass "contrato activo al crearse" || fail "status=$(json status)"

log "R6. Editar contrato (PUT reschedule due_date)"
[ "$(req PUT /api/contracts/$CTRA '{"due_date":"2026-11-30"}')" = "200" ] && [ "$(json due_date)" = "2026-11-30" ] && pass "due_date→2026-11-30" || fail "reschedule"

log "R6. Suspensión manual"
[ "$(req POST /api/contracts/$CTRA/suspend '{}')" = "200" ] || fail "suspend no 200"
[ "$(json status)" = "suspended" ] && sleep 1 && router_has_ip "$IPA" && pass "MikroTik: $IPA en lista 'suspended'" || fail "router no bloquea tras suspender"

log "R6. Reactivación manual"
[ "$(req POST /api/contracts/$CTRA/reactivate '{}')" = "200" ] || fail "reactivate no 200"
[ "$(json status)" = "active" ] && sleep 1 && ! router_has_ip "$IPA" && pass "MikroTik: $IPA fuera de la lista" || fail "router no desbloquea tras reactivar"

log "R10. Modificar perfil de velocidad"
[ "$(req PATCH /api/contracts/$CTRA/speed-profile '{"download_mbps":300,"upload_mbps":150}')" = "200" ] || fail "speed-profile no 200"
LIMIT=$(router_queue_for "$IPA")
[ -n "$LIMIT" ] && pass "MikroTik queue simple max-limit=$LIMIT (target $IPA/32)" || fail "sin queue en el router"

log "R5. Registro manual de pago"
CODE=$(req POST /api/contracts/$CTRA/payments '{"amount_cents":12000,"paid_on":"2026-09-20"}')
[ "$CODE" = "201" ] || fail "payment no 201"
req GET /api/contracts/$CTRA >/dev/null
NPAY=$(json payments | python3 -c 'import sys,json;print(len(json.load(sys.stdin)))')
[ "$NPAY" = "1" ] && pass "1 pago registrado (amount=12000)" || fail "payments=$NPAY"

log "R5. Idempotencia del pago (no doble crédito)"
CODE2=$(req POST /api/contracts/$CTRA/payments '{"amount_cents":12000,"paid_on":"2026-09-20"}')
[ "$CODE2" = "201" ] || fail "pago duplicado no 201"
req GET /api/contracts/$CTRA >/dev/null
NPAY2=$(json payments | python3 -c 'import sys,json;print(len(json.load(sys.stdin)))')
[ "$NPAY2" = "1" ] && pass "pago duplicado ignorado (sigue 1 pago)" || fail "payments=$NPAY2"

# ---------------------------------------------------------------------------
log "R7. Suspensión automática al vencer sin pago"
CODE=$(req POST /api/subscribers "{\"name\":\"Carlos Demo\",\"static_ip\":\"$IPB\"}")
[ "$CODE" = "201" ] && SUB2=$(json id) || fail "crear subscriber B (estado $CODE)"
CODE=$(req POST /api/contracts "{\"subscriber_id\":\"$SUB2\",\"plan_id\":\"$PLAN\",\"billing_start\":\"2026-06-01\",\"due_date\":\"2026-09-01\"}")
[ "$CODE" = "201" ] && CTRB=$(json id) || fail "crear contrato B (estado $CODE)"
[ "$(req GET /api/contracts/$CTRB)" = "200" ] && [ "$(json status)" = "overdue" ] && pass "contrato B vencido sin pago → status overdue" || fail "status=$(req GET /api/contracts/$CTRB; json status)"
[ "$(req POST /api/sweep/expired '{}')" = "200" ] || fail "sweep no 200"
[ "$(req GET /api/contracts/$CTRB)" = "200" ] && [ "$(json status)" = "suspended" ] && sleep 1 && router_has_ip "$IPB" && pass "sweep → suspended y MikroTik bloquea $IPB" || fail "suspensión automática falló"

log "R8. Reactivación automática tras pago"
[ "$(req POST /api/contracts/$CTRB/payments '{"amount_cents":12000,"paid_on":"2026-09-22"}')" = "201" ] || fail "pago B no 201"
[ "$(req GET /api/contracts/$CTRB)" = "200" ] && [ "$(json status)" = "active" ] && sleep 1 && ! router_has_ip "$IPB" && pass "pago → active y MikroTik desbloquea $IPB" || fail "reactivación automática falló"

# ---------------------------------------------------------------------------
# Operator extras: inventory + audit (auth is always on; TOKEN comes from register+login).
log "R11. Extras: registro en BD, inventario de usuarios y auditoría de cambios"
if [ -n "${TOKEN:-}" ]; then
    [ "$(req GET /api/subscribers)" = "200" ] && [ "$(json )" != "[]" ] && pass "inventario devuelve usuarios" || fail "inventario vacío o no disponible"
    CODE=$(req GET "/api/audit?limit=10")
    [ "$CODE" = "200" ] && [ "$(json )" != "[]" ] && pass "auditoría registra los cambios del operador" || fail "auditoría no devuelve entradas ($CODE)"
    [ "$(req POST /api/auth/logout '{}')" = "204" ] && [ "$(req GET /api/audit)" != "200" ] && pass "logout revoca la sesión (401 tras salir)" || fail "logout no revoca la sesión"
    register_operator
else
    echo "  (sin token — no se pudo registrar el operador de demo; se omiten)"
fi

# ---------------------------------------------------------------------------
log "Resumen"
if [ "$FAILURES" = "0" ]; then
    printf '\n\033[32mTODOS LOS REQUISITOS VERIFICADOS.\033[0m\n'
else
    printf '\n\033[31m%d chequeos fallaron.\033[0m\n' "$FAILURES"
fi
exit "$FAILURES"