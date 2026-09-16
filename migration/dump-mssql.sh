#!/bin/zsh
# migration/dump-mssql.sh — snapshot schema + proc bodies from live MSSQL
set -e
OUT="$(cd "$(dirname "$0")" && pwd)/mssql"
SQL() { docker exec ran-mssql /opt/mssql-tools18/bin/sqlcmd -S localhost -U sa -P 'RanDev!2026' -C -No "$@"; }
for DB in RanUser RanGame1 RanLog RanShop; do
  mkdir -p "$OUT/procs/$DB"
  # 1) column catalog (TSV: table, col, type, len, prec, scale, nullable, identity)
  SQL -d "$DB" -h -1 -s $'\t' -W -Q "SET NOCOUNT ON;
    SELECT t.name, c.name, ty.name, c.max_length, c.precision, c.scale, c.is_nullable, c.is_identity
    FROM sys.tables t JOIN sys.columns c ON c.object_id=t.object_id
    JOIN sys.types ty ON ty.user_type_id=c.system_type_id AND ty.user_type_id=ty.system_type_id
    ORDER BY t.name, c.column_id;" > "$OUT/schema-$DB.tsv"
  # 2) primary keys
  SQL -d "$DB" -h -1 -s $'\t' -W -Q "SET NOCOUNT ON;
    SELECT t.name, c.name, ic.key_ordinal FROM sys.indexes i
    JOIN sys.tables t ON t.object_id=i.object_id
    JOIN sys.index_columns ic ON ic.object_id=i.object_id AND ic.index_id=i.index_id
    JOIN sys.columns c ON c.object_id=ic.object_id AND c.column_id=ic.column_id
    WHERE i.is_primary_key=1 ORDER BY t.name, ic.key_ordinal;" > "$OUT/pk-$DB.tsv"
  # 3) each proc body to its own file
  for P in $(SQL -d "$DB" -h -1 -W -Q "SET NOCOUNT ON; SELECT name FROM sys.procedures ORDER BY name;"); do
    SQL -d "$DB" -y 0 -Q "SET NOCOUNT ON; SELECT OBJECT_DEFINITION(OBJECT_ID('dbo.$P'));" \
      | sed '/^-\{3,\}/d' > "$OUT/procs/$DB/$P.sql"
  done
done
echo "dumped: $(ls $OUT/procs/*/ | wc -l) files"
