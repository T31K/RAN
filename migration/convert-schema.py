#!/usr/bin/env python3
# Reads schema-<db>.tsv + pk-<db>.tsv (from dump-mssql.sh) -> MariaDB DDL.
import sys, os, collections
BASE = os.path.dirname(os.path.abspath(__file__))
TYPEMAP = {  # simple 1:1 (others handled in code)
 'int':'INT','smallint':'SMALLINT','bigint':'BIGINT','bit':'TINYINT(1)',
 'float':'DOUBLE','real':'FLOAT','money':'DECIMAL(19,4)','smallmoney':'DECIMAL(10,4)',
 'datetime':'DATETIME','smalldatetime':'DATETIME','uniqueidentifier':'CHAR(36)',
 'image':'LONGBLOB','text':'LONGTEXT','ntext':'LONGTEXT','tinyint':'TINYINT UNSIGNED',
}
def conv(ty, maxlen, prec, scale):
    ty = ty.lower()
    if ty in TYPEMAP: return TYPEMAP[ty]
    if ty in ('decimal','numeric'): return f'DECIMAL({prec},{scale})'
    if ty in ('char','varchar'):
        if int(maxlen) == -1: return 'LONGTEXT'
        k = 'CHAR' if ty=='char' else 'VARCHAR'
        return f'{k}({maxlen}) CHARACTER SET euckr'
    if ty in ('nchar','nvarchar'):
        if int(maxlen) == -1: return 'LONGTEXT'
        k = 'CHAR' if ty=='nchar' else 'VARCHAR'
        return f'{k}({int(maxlen)//2}) CHARACTER SET utf8mb4'
    if ty in ('binary','varbinary'):
        if int(maxlen) == -1: return 'LONGBLOB'
        return f'{ty.upper()}({maxlen})'
    raise SystemExit(f'UNMAPPED TYPE: {ty}')
for db in ('RanUser','RanGame1','RanLog','RanShop'):
    cols = collections.OrderedDict()   # table -> [(col, ddl)]
    for line in open(f'{BASE}/mssql/schema-{db}.tsv', encoding='utf-8', errors='replace'):
        p = line.rstrip('\n').split('\t')
        if len(p) < 8: continue
        t,c,ty,ml,pr,sc,nul,ident = [x.strip() for x in p[:8]]
        ddl = conv(ty,ml,pr,sc) + ('' if nul=='1' else ' NOT NULL') + (' AUTO_INCREMENT' if ident=='1' else '')
        cols.setdefault(t,[]).append((c,ddl))
    pks = collections.OrderedDict()
    for line in open(f'{BASE}/mssql/pk-{db}.tsv', encoding='utf-8', errors='replace'):
        p = line.rstrip('\n').split('\t')
        if len(p) >= 3 and p[0].strip(): pks.setdefault(p[0].strip(),[]).append(p[1].strip())
    with open(f'{BASE}/mariadb/schema-{db}.sql','w') as f:
        f.write(f'USE {db};\nSET FOREIGN_KEY_CHECKS=0;\n')
        for t, cl in cols.items():
            body = ',\n  '.join(f'`{c}` {d}' for c,d in cl)
            pkcols = pks.get(t, [])
            pk = f',\n  PRIMARY KEY ({",".join("`"+c+"`" for c in pkcols)})' if pkcols else ''
            # MariaDB: every AUTO_INCREMENT column must be a key. If the identity
            # column isn't the first PK column, give it its own index.
            extra = ''
            for c, d in cl:
                if 'AUTO_INCREMENT' in d and (not pkcols or pkcols[0] != c):
                    extra += f',\n  KEY `idx_ai_{c}` (`{c}`)'
            f.write(f'CREATE TABLE `{t}` (\n  {body}{pk}{extra}\n) ENGINE=InnoDB DEFAULT CHARSET=euckr;\n\n')
    print(db, 'tables:', len(cols))
