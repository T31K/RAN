FROM debian:bookworm-slim
ENV DEBIAN_FRONTEND=noninteractive
# Wine (32-bit game exes via WoW64), Xvfb virtual display, unixODBC + MariaDB ODBC driver
RUN dpkg --add-architecture i386 && apt-get update && apt-get install -y --no-install-recommends \
      wine wine64 wine32:i386 xvfb xauth \
      unixodbc libodbc2:i386 odbc-mariadb:i386 \
      procps net-tools ca-certificates \
    && rm -rf /var/lib/apt/lists/*
ENV WINEPREFIX=/opt/wineprefix WINEDEBUG=-all DISPLAY=:99
COPY odbcinst.ini /etc/odbcinst.ini
COPY odbc.ini     /etc/odbc.ini
COPY entrypoint.sh /entrypoint.sh
RUN chmod +x /entrypoint.sh
WORKDIR /opt/ran/game-client
ENTRYPOINT ["/entrypoint.sh"]
