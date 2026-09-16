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
        return f'{k}({maxlen}) CHARACTER SET utf8mb4'
    if ty in ('nchar','nvarchar'):
        if int(maxlen) == -1: return 'LONGTEXT'
        k = 'CHAR' if ty=='nchar' else 'VARCHAR'
        return f'{k}({int(maxlen)//2}) CHARACTER SET utf8mb4'
    if ty in ('binary','varbinary'):
        if int(maxlen) == -1: return 'LONGBLOB'
        return f'{ty.upper()}({maxlen})'
    raise SystemExit(f'UNMAPPED TYPE: {ty}')
def conv_default(d):
    """MSSQL default definition -> MariaDB DEFAULT value (or None to skip)."""
    d = d.strip(); dl = d.lower()
    if 'datepart' in dl: return '0'            # int date-part cols; app always writes them
    if 'getdate' in dl or 'getutcdate' in dl: return 'CURRENT_TIMESTAMP'
    while d.startswith('(') and d.endswith(')'):  # strip ((0))->0, ('')->'' , ('x')->'x'
        d = d[1:-1].strip()
    return d if d != '' else "''"

for db in ('RanUser','RanGame1','RanLog','RanShop'):
    # defaults: table -> {col -> mariadb default}
    defs = collections.defaultdict(dict)
    dpath = f'{BASE}/mssql/default-{db}.tsv'
    if os.path.exists(dpath):
        for line in open(dpath, encoding='utf-8', errors='replace'):
            p = line.rstrip('\n').split('\t')
            if len(p) >= 3 and p[0].strip():
                defs[p[0].strip()][p[1].strip()] = conv_default(p[2])
    cols = collections.OrderedDict()   # table -> [(col, ddl)]
    for line in open(f'{BASE}/mssql/schema-{db}.tsv', encoding='utf-8', errors='replace'):
        p = line.rstrip('\n').split('\t')
        if len(p) < 8: continue
        t,c,ty,ml,pr,sc,nul,ident = [x.strip() for x in p[:8]]
        base = conv(ty,ml,pr,sc)
        ddl = base + ('' if nul=='1' else ' NOT NULL')
        # DEFAULT (skip on AUTO_INCREMENT). TEXT/BLOB need the value parenthesized (MariaDB 10.2+).
        dv = defs.get(t, {}).get(c)
        if ident != '1' and dv is not None:
            if 'LONGTEXT' in base or 'LONGBLOB' in base:
                ddl += f' DEFAULT ({dv})'
            else:
                ddl += f' DEFAULT {dv}'
        ddl += (' AUTO_INCREMENT' if ident=='1' else '')
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
            f.write(f'CREATE TABLE `{t}` (\n  {body}{pk}{extra}\n) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;\n\n')
    print(db, 'tables:', len(cols))
