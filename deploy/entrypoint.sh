#!/bin/bash
# RAN server container entrypoint: Xvfb + wine + the 4 servers, ODBC->MariaDB.
set -u
export HOME=/root
SRVIP="${RAN_SERVER_IP:-167.233.23.97}"   # servers bind this specific IP (server_ip in cfg)
mkdir -p /opt/ran/logs
echo "[entrypoint] $(date) starting (bind IP $SRVIP)"

# clean stale locks from a prior run (docker restart keeps the container fs)
rm -f /tmp/.X99-lock /tmp/.X11-unix/X99 2>/dev/null
wineserver -k 2>/dev/null || true
rm -f "$WINEPREFIX"/*.lock 2>/dev/null

Xvfb :99 -screen 0 1024x768x16 >/opt/ran/logs/xvfb.log 2>&1 &
sleep 2

if [ ! -f "$WINEPREFIX/system.reg" ]; then
  echo "[entrypoint] initializing wine prefix (first run)…"
  wineboot --init >/opt/ran/logs/wineboot.log 2>&1
  wineserver -w
fi

export WINEDLLOVERRIDES="odbc32=b"
cd /opt/ran/game-client || { echo "[entrypoint] FATAL: no game-client"; exit 1; }

launch() {
  local exe="$1" port="$2"
  echo "[entrypoint] launching $exe …"
  wine "$exe" start >"/opt/ran/logs/${exe%.exe}.log" 2>&1 &
  for i in $(seq 1 75); do
    if (exec 3<>"/dev/tcp/$SRVIP/$port") 2>/dev/null; then
      exec 3>&- 3<&- 2>/dev/null
      echo "[entrypoint] $exe bound $SRVIP:$port ✓ (${i}s)"
      return 0
    fi
    sleep 1
  done
  echo "[entrypoint] !! $exe did NOT bind :$port"
  return 1
}

launch SessionServer.exe 5113
launch LoginServer.exe   5001
launch FieldServer.exe   5003
launch AgentServer.exe   5502
echo "[entrypoint] startup sequence complete"
exec tail -F /opt/ran/logs/SessionServer.log /opt/ran/logs/LoginServer.log \
              /opt/ran/logs/FieldServer.log /opt/ran/logs/AgentServer.log
