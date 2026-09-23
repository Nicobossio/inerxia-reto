#!/usr/bin/env bash
# Verify the lab RouterOS instance through the real REST API.
# Exits non-zero if any check fails.
#
# Env overrides:
#   MIKROTIK_BASE_URL (default http://127.0.0.1:8080/rest)
#   MIKROTIK_USER / MIKROTIK_PASSWORD

set -uo pipefail
BASE="${MIKROTIK_BASE_URL:-http://127.0.0.1:8080/rest}"
USER="${MIKROTIK_USER:-lab-admin}"
PASS="${MIKROTIK_PASSWORD:-lab-router-password-2026}"
AUTH=(-u "$USER:$PASS")
IP="10.99.0.1"          # fake subscriber IP used only for the round-trips below
CONTRACT="ct-verify"

pass=0; fail=0
check() { # check <label> <result-ok?>
    if [ "$2" = "ok" ]; then echo "PASS  $1"; pass=$((pass+1));
    else echo "FAIL  $1"; fail=$((fail+1)); fi
}

q() { curl -s --max-time 12 "${AUTH[@]}" -H 'content-type: application/json' "$@"; }

# --- static checks ---
identity=$(q "$BASE/system/identity" | tr -d '{}"')
check "identity" "$([ "$identity" = "name:inerxia-lab" ] && echo ok || echo no)"
user_present=$(q "$BASE/user?name=$USER" | grep -q "$USER" && echo ok || echo no)
check "user $USER present" "$user_present"
www_ok=$(q "$BASE/ip/service?name=www" | grep -q '"disabled":"false"' && echo ok || echo no)
check "www service enabled" "$www_ok"
net_ok=$(q "$BASE/ip/address" | grep -q '10.0.2.15/24' && echo ok || echo no)
check "address 10.0.2.15/24 present" "$net_ok"

# --- operation round-trips (create -> read -> delete) ---
code=$(curl -s -o /dev/null -w '%{http_code}' -X PUT "${AUTH[@]}" -H 'content-type: application/json' \
    --data "{\"list\":\"suspended\",\"address\":\"$IP\",\"comment\":\"inerxia:$CONTRACT\"}" "$BASE/ip/firewall/address-list")
check "disableUser creates address-list entry ($code)" "$([ "$code" = 201 ] && echo ok || echo no)"
found=$(q "$BASE/ip/firewall/address-list?list=suspended&address=$IP" | grep -q "$IP" && echo ok || echo no)
check "suspended entry visible via REST" "$found"
did=$(q "$BASE/ip/firewall/address-list?list=suspended&address=$IP" | sed -E 's/.*"\.id":"([^"]+)".*/\1/')
curl -s -o /dev/null "${AUTH[@]}" -X DELETE "$BASE/ip/firewall/address-list/$did"
check "enableUser removes entry" "$(q "$BASE/ip/firewall/address-list?list=suspended&address=$IP" | grep -q "$IP" && echo no || echo ok)"

code=$(curl -s -o /dev/null -w '%{http_code}' -X PUT "${AUTH[@]}" -H 'content-type: application/json' \
    --data "{\"name\":\"inerxia-$CONTRACT\",\"target\":\"$IP/32\",\"max-limit\":\"300M/150M\",\"limit-at\":\"300M/150M\",\"comment\":\"inerxia-$CONTRACT\"}" "$BASE/queue/simple")
check "changeSpeedProfile creates queue ($code)" "$([ "$code" = 201 ] && echo ok || echo no)"
ml=$(q "$BASE/queue/simple?target=${IP}%2F32" | grep -o '"max-limit":"[^"]*"' | head -1)
check "queue max-limit applied ($ml)" "$([ "$ml" = '"max-limit":"300000000/150000000"' ] && echo ok || echo no)"
qid=$(q "$BASE/queue/simple?target=${IP}%2F32" | sed -E 's/.*"\.id":"([^"]+)".*/\1/')
curl -s -o /dev/null "${AUTH[@]}" -X DELETE "$BASE/queue/simple/$qid"

code=$(curl -s -o /dev/null -w '%{http_code}' -X PUT "${AUTH[@]}" -H 'content-type: application/json' \
    --data '{"chain":"forward","action":"drop","src-address-list":"suspended","comment":"inerxia:baja-automatica"}' "$BASE/ip/firewall/filter")
check "blocking filter rule created ($code)" "$([ "$code" = 201 ] && echo ok || echo no)"
fid=$(q "$BASE/ip/firewall/filter?comment=inerxia%3Abaja-automatica" | sed -E 's/.*"\.id":"([^"]+)".*/\1/')
[ -n "$fid" ] && curl -s -o /dev/null "${AUTH[@]}" -X DELETE "$BASE/ip/firewall/filter/$fid"

echo "----"
echo "summary: $pass passed, $fail failed"
[ "$fail" = 0 ]