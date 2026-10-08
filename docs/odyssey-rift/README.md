# Odyssey Rift — how to play, run and change it

Co-op wave survival on **Another W South** (floating crystal islands). Server-only: no client
update needed to play. Design: `docs/superpowers/specs/2026-10-09-odyssey-rift-mode-design.md`.

## Playing

1. Go to **Mystic Peak Square** on MP Campus (the fountain). Stand near the terminal there and
   type `sail` in normal chat. You land on the island next to the **Nexus** (the glowing orb).
2. Everyone in the arena stands on the Nexus → after 3 seconds the first leg starts.
   Mobs pour out of the rift and hunt you.
3. Kill them for **Essence** (+50 a kill, more for specials and the boss, +250 per leg).
4. Between legs: spend Essence, then charge the Nexus together again.
5. Every 5th leg sends hunters (tougher mobs), every 10th leg is a **Convergence**: the
   Geomencer (Chairman) and escorts.
6. Everyone down = the Voyage ends and the scoreboard prints. Revive and charge to sail again.

Chat commands (inside the arena): `shop` (list wares + prices), `buy` (buy the ware or Rift
Seal you stand next to), `essence`, `ready`.

Skills above **Lv47** are sealed in the arena until you buy them back at a shrine
(Lv57 shrine, Lv67 shrine). The Forge overclocks your held weapon +3 (max +15 — this is a
permanent change to the real item).

## GM commands (account level GM3+)

| Command | What |
|---|---|
| `rift pos` | prints your map + world x z (use it to place things in the config) |
| `rift reload` | re-reads `odysseyrift.ini` and resets the run |
| `rift round N` | jump to leg N (e.g. `rift round 10` for the boss) |
| `rift essence N` | give yourself N Essence |
| `rift reset` | end the run, despawn the mobs |
| `rift mobs` | mob table, spawn points, live count, state |
| `rift spawn M S` | spawn crow M S next to you (test any mob id) |
| `sail` | GMs can sail from anywhere |

## Config

`/opt/ran/game-client/odysseyrift.ini` on the VPS (versioned copy: `deploy/odysseyrift.ini`).
Edit it, then type `rift reload` in game — no rebuild or restart. No file = mode off.

- `zone = id x z radius price seal_x seal_z` — circles of walkable ground; price 0 = open.
  Locked zones push players back; the seal stands at seal_x/seal_z and `buy` next to it opens it.
- `shop = zone x z kind price param name` — kinds: `heal`, `skills <level>`, `overclock <grades>`.
- `mob = mid sid tier` — 1 common, 2 special (every 5th leg + mixed in from leg 3), 3 boss.

## Rollback

The pre-rift server exes are on the VPS in `/opt/ran/backup-pre-rift/`. To roll back:
copy them over `/opt/ran/game-client/*.exe` and restart the server container (as in
`deploy/redeploy.sh`). Or just delete `odysseyrift.ini` and restart: the mode is off and
Another W South spawns its normal mobs again.
