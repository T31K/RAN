# Odyssey Rift MVP Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** A playable, server-only co-op wave mode: enter at Mystic Peak Square, fight waves on
Another W South, earn Essence, open Rift Seals, buy skill tiers / heals / weapon overclocks,
start rounds together at the Nexus, Convergence boss every 10 rounds.

**Architecture:** One new singleton `GLOdysseyRift` (field server) holds all run state and is
ticked from `DxFieldInstance::FrameMove`. Thin hooks call into it from existing server code
(chat, crow death, skill use). All positions, mob ids, prices and zones come from a plain-text
config `odysseyrift.ini` next to the server exe, reloadable with a GM chat command, so
coordinates are calibrated live without rebuilding. No client changes: the arena is the real
Another W South (43,0) map, whose natural spawns are suppressed while the mode is enabled.

**Tech Stack:** C++ (MSVC 2022 Win32 via GitHub Actions; clang for the macOS client build,
which also compiles server files), CP949 sources, Wine servers on the VPS.

**Spec:** `docs/superpowers/specs/2026-10-09-odyssey-rift-mode-design.md`

## Global Constraints

- Sources are CP949: new files are pure ASCII; edits to existing files go through
  `iconv -f CP949 -t UTF-8` → Edit → `iconv -f UTF-8 -t CP949`, then `git diff --numstat` must
  show only the intended lines.
- New files start with `#include "pch.h"` and are listed in
  `[Lib]__RanClient/[Lib]__RanClient.vcxproj` (+ `.filters`); they must also compile with
  `port/scripts/compile_one.sh` (the macOS client build compiles them).
- Work on branch `odyssey-rift`; never push master (Coolify auto-rebuilds from master).
- CI: `gh workflow run build.yml --ref odyssey-rift`, artifact `ran-binaries`.
- Deploy: back up `/opt/ran/game-client/*Server.exe` on the VPS first, then
  `deploy/redeploy.sh <run-id>`; check nobody is online before restarting.
- No client data / Game.exe changes and no OTA release in this pass.
- Chat lines ≤ `CHAT_MSG_SIZE`; no Korean text in new code.
- Spec deviations taken for the 4-hour budget (record in the morning report): arena uses the
  real map id 43 instead of a dedicated id; entry is a stand-in stock NPC + `sail` chat command
  (the Trojan Horse model needs a client release); no HP multipliers (difficulty scales by
  count, spawn rate and mob tier); "stripping" = skills above the bought tier are refused while
  in the arena (no DB snapshot).

## Review Focus

1. A player disconnects or walks out through a gate mid-round → removed from the run; when no
   one is left the run resets and every rift mob is despawned (no orphan mobs, no stuck state).
2. Two players kill the same mob / summons kill a mob → exactly one credit, never a crash
   on a null char (`m_sAssault.dwID` may be stale).
3. Natural Another W South spawns must not appear while the mode is enabled, and mobs must
   tick even though the land's PC list is briefly empty between rounds.
4. Seal push-back must never trap a player (last valid position always inside an unlocked
   zone; fallback = Nexus position).
5. Bad or missing `odysseyrift.ini` → mode disabled with an errlog line, server unaffected.

---

### Task 1: Config + module skeleton + build wiring

**Files:**
- Create: `[Lib]__RanClient/Sources/G-Logic/Server/GLOdysseyRift.h`, `GLOdysseyRift.cpp`
- Modify: `[Lib]__RanClient/[Lib]__RanClient.vcxproj`, `.vcxproj.filters`
- Create: `deploy/odysseyrift.ini` (versioned copy of the live config)

**Interfaces (produces):**
```cpp
class GLOdysseyRift {
public:
  static GLOdysseyRift& GetInstance();
  bool  LoadConfig(const char* szPath);          // false -> disabled
  void  FrameMove(float fElapsed);               // from DxFieldInstance::FrameMove
  BOOL  OnChat(GLChar* pChar, const char* szMsg); // TRUE = swallowed
  void  OnCrowKilled(GLCrow* pCrow);              // before GenerateReward
  bool  CanUseSkill(GLChar* pChar, const GLSKILL* pSkill); // false = refuse
  bool  IsArenaLand(const GLLandMan* pLand) const;
  bool  SuppressNaturalSpawns(const GLLandMan* pLand) const;
};
```
Config format (`key = value`, `#` comments, repeated keys for lists):
```
enabled = 1
arena_map = 43 0
entry_map = 5 0
entry_pos = 20 -2230          # world x z, calibrated live
entry_npc = <mid> <sid>
nexus_pos = 12 505
nexus_radius = 150
mob = <mid> <sid> <tier>      # tier 1 common, 2 special, 3 boss
zone = <id> <x> <z> <radius> <price> <seal_x> <seal_z>   # zone 0 price 0 = start
seal_npc = <mid> <sid>
shop = <zone> <x> <z> <kind> <price> <param>   # kind heal|skills|overclock
```
- [ ] Write header + cpp with config parser (fopen/fgets, tolerant), a `CONSOLEMSG_WRITE`/
  `CDebugSet::ToLogFile` line on load, and empty hooks.
- [ ] Add both files to the vcxproj/filters next to `GLAutoLevel.cpp`.
- [ ] `port/scripts/compile_one.sh` on the new cpp → compiles clean.
- [ ] Commit.

### Task 2: Hooks into existing code

**Files (CP949 edits):**
- `[Lib]__RanClient/Sources/DxServerInstance.cpp` (~261): load config at init, call
  `GLOdysseyRift::GetInstance().FrameMove(fElapsedAppTime)`.
- `G-Logic/Server/GLGaeaServerMsg.cpp` `ChatMsgProc` CHAT_TYPE_NORMAL, before `getitem`:
  `if (GLOdysseyRift::GetInstance().OnChat(pChar, pNetMsg->szChatMsg)) return TRUE;`
- `G-Logic/Server/GLCrow.cpp:~1260` before `GenerateReward()`:
  `GLOdysseyRift::GetInstance().OnCrowKilled(this);`
- `G-Logic/Server/GLCharSkillMsg.cpp` `MsgReqSkill` after `pSkill` lookup:
  `if (!GLOdysseyRift::GetInstance().CanUseSkill(this, pSkill)) return E_FAIL;`
- `G-Logic/Server/GLLandMan.cpp:2369` guard `m_MobSchMan.FrameMove` (and 2380 Ex) with
  `!GLOdysseyRift::GetInstance().SuppressNaturalSpawns(this)`; same guard on the crow-tick
  condition at 2017 so rift mobs keep moving (`|| IsArenaLand(this)`).
- [ ] Apply each edit through the iconv round-trip; `git diff --numstat` additions only.
- [ ] compile_one.sh on each touched file.
- [ ] Commit.

### Task 3: Run state machine, waves, Essence, narration

In `GLOdysseyRift.cpp`. States `IDLE → LOBBY → CHARGING → WAVE → BREAK → OVER`.
- Players = chars whose `m_sMapID` is the arena (rebuilt every tick from the land's
  `m_GlobPCList`); new arrivals get a welcome line and 500 starting Essence; leavers are
  dropped (Review Focus 1). Empty arena → despawn all rift mobs, state IDLE.
- CHARGING: every alive player within `nexus_radius` of `nexus_pos` for 3 s → WAVE. Chat
  `[NEXUS] <name> charges the Nexus. n / N` on each new charger.
- WAVE: total = `6 + 3*round + 2*(N-1)`; alive cap `min(8*N, 30)`; spawn every 1.5 s at a
  random spawn point of an unlocked zone (spawn points = the arena level's own mob-schedule
  positions, captured at first tick, grouped by nearest zone); mob = tier-1 list, plus tier 2
  one in four from round 3; every 5th round tier 2 only; every 10th = one tier-3 boss with
  escorts. Track spawned crows by (GlobID, NativeID).
- Kill credit via `OnCrowKilled`: only rift mobs; killer = `m_sAssault` CROW_PC → char on the
  arena; +50 Essence (×4 boss). Round end when spawned==total and none alive → +250 each,
  BREAK, chat summary, beacon line.
- All dead → OVER: scoreboard (round, kills, Essence per player), dead players are revived
  to Mystic Peak via the normal rebirth flow (they press revive), state IDLE after 10 s.
- Narration lines as in spec (wave incoming, last 3 alive, Convergence warning, run over).
- [ ] Implement, compile_one, commit.

### Task 4: Rift Seals, shop, skill tiers, entry

- Zones from config; `unlocked` set starts {0}. Each tick, a player outside every unlocked
  zone circle is moved back to their last valid position (same-map
  `RequestInvenRecallThisSvr` + RECALL_FB, as in `GMCtrolMove2MapPos`) with a warning
  (Review Focus 4). Seal marker crows spawned at seal positions while locked.
- Chat (only on the arena unless noted): `buy` (nearest shop/seal within 80 units),
  `shop`, `essence`, `ready` (alias for status of the charge); on the entry map within 300 of
  `entry_pos`: `sail` → recall to `nexus_pos`. GM (`m_dwUserLvl >= USER_GM3`): `rift reload`,
  `rift pos`, `rift round <n>`, `rift reset`, `rift essence <n>`.
- Shop kinds: `heal` (full HP/MP/SP + client update), `skills <lvl>` (raises the player's
  allowed skill tier), `overclock <n>` (current right-hand weapon `cDAMAGE += n`, cap 15,
  `SNETPC_PUTON_UPDATE` to client).
- `CanUseSkill`: in the arena, refuse skills whose `m_sLEARN.sLVL_STEP[0].dwLEVEL` exceeds the
  player's tier (default 47) with a rate-limited chat line.
- Entry NPC: a stock NPC crow dropped on the entry map at `entry_pos` when the mode loads.
- [ ] Implement, compile_one, commit.

### Task 5: Build, deploy, calibrate, play-test

- [ ] Push branch, run CI, wait, `deploy/redeploy.sh <run>` after VPS backup; copy
  `deploy/odysseyrift.ini` to the server exe dir.
- [ ] Native client (window in front): log in `[GM]T31K`, `/m2p 5 0 …` to the square,
  `rift pos` → fix `entry_pos`; `sail`; `rift pos` at the orb → fix `nexus_pos`, zones;
  `rift reload`.
- [ ] Charge solo, clear rounds 1-3, buy heal + a seal + skills, `rift round 10` for the boss,
  die → scoreboard. Field errlog clean.
- [ ] Morning report `docs/odyssey-rift/README.md` (how to play, commands, config, rollback).
