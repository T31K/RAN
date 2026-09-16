# Overnight autonomous deploy — decisions log (2026-09-17)

Goal: RanOdyssey.app (dock) -> plays on VPS servers running on MariaDB.

## Networking
- Server container runs with `--network host` (server_ip binds the host's real
  public IP 167.233.23.97 and advertises it; s_CServer.cpp:658 binds
  inet_addr(server_ip)). Inter-server refs = 127.0.0.1.
- MariaDB published to 127.0.0.1:3306 (host loopback only, not public).
- ODBC DSN SERVER=127.0.0.1 PORT=3306.
- Client dials LoginAddress:5001 (ConnectLoginServer default port 5001).
- Firewall: only 5113/5001/5003/5502 (+22) public.

## ODBC bridge (game <-> MariaDB)
- Wine BUILTIN odbc32 (WINEDLLOVERRIDES=odbc32=b) -> unixODBC -> odbc-mariadb.
  (Drops our FreeTDS shim; the native-bridge is clean on Linux.)
- DSNs in /etc/odbc.ini; driver in /etc/odbcinst.ini.
- OPTION=2 (FLAG_FOUND_ROWS) so UPDATE reports matched rows == MSSQL @@ROWCOUNT.

## Client
- LoginAddress -> 167.233.23.97 in the app's param.cfg, shipped as OTA build 2.

## Stretch (if time): dual server — local Mac "T31K Server" as a 2nd select option.
