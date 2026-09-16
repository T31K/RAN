# Data migration decision (2026-09-16)

RESET (no data copy). The only existing character is a dev/test char (T31K,
lvl 100) created via the getitem/maxskills cheat commands. Blob-typed
inventory columns don't survive a TSV copy, and the char rebuilds in minutes.
Login auto-creates the account (RanUser.user_verify inserts UserInfo on first
verify). Fresh empty DBs = clean slate for friends.

If exact char state must be preserved later: write a pyodbc->mysql-connector
row-copier that parameter-binds the varbinary/image columns (see plan Task 1.3).
