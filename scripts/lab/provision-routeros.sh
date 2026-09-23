#!/usr/bin/env bash
# Idempotently provision the lab RouterOS instance over its serial console.
# Works whether the CHR was just booted or has been provisioned before.
#
# Does, against the REAL router:
#   1. log in as admin (empty initial password), skipping license + password wizards
#   2. gives etherN the static address 10.0.2.15/24 (slirp subnet) if missing
#   3. adds the default route via 10.0.2.2 if missing
#   4. enables the www / www-ssl services (REST/webfig)
#   5. creates the application user $MIKROTIK_USER (group full) if missing
#
# Env overrides:
#   MIKROTIK_USER   (default lab-admin)
#   MIKROTIK_PASSWORD (default lab-router-password-2026)
#   LAB_ADMIN_PASSWORD (default lab-admin-2026) — password assigned to 'admin'
#   CACHE_DIR       runtime dir; uses the same one as run-routeros.sh

set -euo pipefail
CACHE_DIR="${CACHE_DIR:-${XDG_CACHE_HOME:-$HOME/.cache}/inerxia-lab}"
SOCK_FILE="$CACHE_DIR/chr/cons.sock"
MIKROTIK_USER="${MIKROTIK_USER:-lab-admin}"
MIKROTIK_PASSWORD="${MIKROTIK_PASSWORD:-lab-router-password-2026}"
LAB_ADMIN_PASSWORD="${LAB_ADMIN_PASSWORD:-lab-admin-2026}"

[ -S "$SOCK_FILE" ] || { echo "no console socket at $SOCK_FILE — run scripts/lab/run-routeros.sh start" >&2; exit 1; }

export MIKROTIK_USER MIKROTIK_PASSWORD LAB_ADMIN_PASSWORD
python3 - "$SOCK_FILE" <<'EOF'
import socket, re, sys, time

USER = __import__("os").environ["MIKROTIK_USER"]
PASS = __import__("os").environ["MIKROTIK_PASSWORD"]
ADMIN_PASS = __import__("os").environ["LAB_ADMIN_PASSWORD"]

s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
s.settimeout(4)
s.connect(sys.argv[1])

def clean(b):
    return re.sub(rb"\x1b\[[0-9;?]*[A-Za-z]", b"", b).decode("utf-8", "replace")

def read_until(tok, timeout=10):
    buf = b""; t0 = time.time()
    while time.time() - t0 < timeout:
        try:
            c = s.recv(4096)
            if not c: break
            buf += c
            if tok.encode() in buf: break
        except socket.timeout:
            break
    return clean(buf).replace("\r", " ")

def send(x):
    s.sendall(x.encode() + b"\r"); time.sleep(1.3)

# Unified console driver: handles a fresh CHR (license + password-change wizards,
# empty admin password), an already-provisioned router, and a router where admin
# is already logged in when we connect. Loop:
#   Login:  -> send "admin"
#   Password: -> send candidate password (empty first, then ADMIN_PASS)
#   [Y/n] (wizards) -> n ; "new password>" -> ADMIN_PASS (twice)
#   "] >" -> logged in, proceed
s.sendall(b"\r")
password_index = 0
LOGIN_PASSWORDS = ["", ADMIN_PASS]

def resend_login():
    global password_index
    pwd = LOGIN_PASSWORDS[password_index]
    password_index = min(password_index + 1, len(LOGIN_PASSWORDS) - 1)
    send("admin")
    send(pwd)

for _ in range(40):
    out = read_until("]>", 12)
    low = out.lower()
    if "] >" in out:
        break
    if "mikrotik login" in low:
        resend_login()
    elif "password:" in low and "new password>" not in low:
        resend_login()
    elif "do you want to see the software license?" in low or "[y/n]" in low:
        send("n")
    elif "do you want to change your password?" in low:
        send("n")
    elif "new password>" in low:
        send(ADMIN_PASS)
    elif "repeat new password>" in low:
        send(ADMIN_PASS)
    elif "invalid username or password" in low:
        pass
else:
    print("FATAL: could not reach the RouterOS shell prompt"); sys.exit(1)
print("shell prompt reached")

def to_prompt(timeout=12, attempts=30):
    for _ in range(attempts):
        out = read_until("]>", timeout)
        if "] >" in out:
            return out
    print("FATAL: lost the RouterOS shell prompt"); sys.exit(1)

ether = None
send(':foreach i in=[/interface find type=ether running=yes] do={:put [/interface get $i name]}')
out = to_prompt()
for token in out.split():
    if token.startswith("ether"):
        ether = token
        break
if ether is None:
    print("FATAL: could not detect the active ether interface"); sys.exit(1)
print(f"active ether interface: {ether}")

cmds = [
    f"/ip service set www disabled=no",
    f"/ip service set www-ssl disabled=no",
    f"/system identity set name=inerxia-lab",
    f':if ([:len [/ip address find address=10.0.2.15/24]] = 0) do={{/ip address add address=10.0.2.15/24 interface={ether}}}',
    f':if ([:len [/ip route find dst-address=0.0.0.0/0]] = 0) do={{/ip route add gateway=10.0.2.2}}',
    f':if ([:len [/user find name={USER}]] = 0) do={{/user add name={USER} password={PASS} group=full}}',
    f'/user set {USER} group=full',
]
for cmd in cmds:
    send(cmd)
    to_prompt()

send(':put (":ok user=" . [:len [/user find name=' + USER + ']])')
print(to_prompt()[-120:].strip())
send("/quit")
print("provisioning applied cleanly")
EOF