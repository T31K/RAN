# Odyssey Rift — co-op wave survival mode (design)

Theme: arcane energy tears a rift into a mythic other world (the *how*); each run is a
voyage and each Convergence round a trial from Homer's *Odyssey* (the *what*). Rounds are
announced as "legs of the Voyage".

Date: 2026-10-09 · Status: draft for review

## Intent

A COD-Zombies-style co-op mode for T31K and friends on the RanOdyssey server: a squad
fights escalating waves on one map, earns currency from kills, spends it between rounds,
and starts each round together. Success = a run that is fun for 30-40 minutes with 1-6
players, and a first version playable tonight (4-hour build budget).

What the user said: own character goes in; skills and weapons are wall-buys; generous
economy; mystery box = powers and weapon upgrades; buildable structures; one-floor map with
many doors and outdoor areas; no break timer - the next round starts when everyone charges a
beacon; very immersive; arcane-energy theme; random weather conditions; a "power trip" someone
must fix; mid-fight quests; interesting mobs; a squad-only boss every 10 rounds; entry via an
NPC in the Mystic Peak square.

Assumptions (correct me): squads of 1-6, scaling with player count; enemies are
"arcane-risen" monsters using existing RAN mob models for now.

## Full vision (later seasons, for reference only - NOT built in this pass)

- **Season 2 - chaos:** mystery box (powers + weapon upgrades), perk buffs, random arcane
  storms per round (rain / night / fog / frost, each with its own mob set), ley-line
  rupture ("power trip": one player re-seals the Rune Anchor), side events (escort, hold the
  zone, kill the carrier), per-player contracts, down-not-dead + revive, special mobs with
  mechanics (Mirror Wraith, Leech Totem, Gravemind Caster, Splitter Slime, Rune Hounds).
- **Season 3 - epic:** doors that open areas, structures (arcane wards, rune towers, sigil
  traps), skill tiers on walls (≤47 free, 57/67 bought, off-class from the box), level/stat
  normalization with snapshot/restore of the real character, Convergence raid bosses
  (Polyphemus the Cyclops r10 - chained, immune until every player holds an anchor;
  Circe r20 - one copy per player, copies must die together; Scylla & Charybdis r30 - one
  shared HP bar; the Sirens as a 3-singer mini-boss), the Underworld Easter-egg descent,
  rewards that carry out to the real character, new models, possibly a custom map.

## MVP (this build)

### Player flow
1. In **MP_Campus (map 5,0)**, in the water at the centre of **Mystic Peak Square**,
   stands a giant **Trojan Horse** (Sketchfab "Low Poly Trojan Horse", 4,015 faces,
   CC-BY, id `6452304ccd8e4a3786e8f8d644e4e093`, static prop scaled to building size).
   Clicking it offers "Climb into the Horse", which moves the player to the arena.
2. The arena is a **dedicated map id** (new `mapslist.ini` entry reusing the
   **Another W South** level file `w_ep3_another_1.Lev` - floating crystal islands in a void,
   3215 units across, a glowing orb at the centre), so the real Another World is untouched.
   The level's own mob schedules must not spawn on the arena id (only rift-spawned mobs
   live there). Its outward gates are disabled on the arena id. One run at a time.
   Another W centre (the Director's room) is kept for the Season 3 finale.
3. Arriving on the arena map joins the player to the run's lobby. The **Nexus beacon** is
   the glowing orb at the centre (a stationary marker mob placed on it).
4. A round starts when **every player in the arena stands within the beacon radius** for 3
   seconds. Chat counts it up: `[NEXUS] T31K charges the Nexus. 2 / 3`.
5. During a round, waves spawn at the arena edges and run at the players. When the last mob
   of the round dies the round ends, the beacon "re-rises" (chat), and players spend Essence.
6. The run ends when every player in the arena is dead, or the arena is empty. A chat
   scoreboard prints (round reached, kills, Essence earned per player). Dead players are
   returned to Mystic Peak.

### Systems
- **Round manager** (field server, ticked from the server frame loop): states
  `LOBBY → CHARGING → WAVE → BREAK → (CHARGING …) → OVER`. Owns the player list, round
  number, live mob list.
- **Wave scaling:** mob count = `6 + 3·round + 2·(players-1)`, spawned in trickles of up to
  8 alive at once per player; mob HP/damage multiplier `1 + 0.15·round`. Every 5th round
  is a **special round** (a single mob type, faster); round 10 and every 10th is a
  **Convergence round**: one boss-class RAN mob with HP scaled by player count, no other
  mobs. Mob ids come from a small table in code chosen from existing crows.
- **Essence:** per player, in memory for the run (no per-hit income in MVP).
  **+50 per kill, +250 per round survived, ×2 on Convergence kills** (generous). Shown in
  chat after each round and on `essence`.
- **Wall-buys (chat-command form):** fixed spots on the map, each with a radius. Typing
  `buy` while standing in a spot buys its item; `shop` lists spots and prices. Items are
  granted with the existing `getitem` path; skills with the existing skill-grant path.
  MVP catalogue: 3 weapons (cheap/mid/top), 2 skill scrolls, a full heal (consumable).
- **Narration:** every state change posts a themed system chat line (Nexus voice), e.g.
  wave incoming, last 3 mobs, Convergence warning, run over.
- **Character handling (MVP):** players keep their real gear and skills. No stripping in
  this pass - it needs DB snapshot/restore and is too risky to rush on the live database.
  Mob scaling uses the squad's average level as an extra multiplier so high levels do not
  trivialise it.

### New monster model (stretch, after the core loop is playable)
One Sketchfab model goes through a full import pipeline and replaces the common mob:
candidate "Zombie" (4,824 faces, 10 animations, CC-BY, sketchfab id
`73ef58af341e46afba1da53366ed79cf`). Pipeline: Sketchfab Data API download (token in
`~/.config/sketchfab/token`, never committed) → Blender (free, headless) → RAN skinned
`.x` mesh + skeleton + idle/walk/run/attack/death animations + the char/anim config files
RAN mobs use → a new crow entry pointing at it. Credit the author (CC-BY) in the repo.
If the pipeline is not working by the end of the budget, the mode ships with stand-in
RAN mobs and the pipeline work continues next session.

### Out of scope for MVP
Everything under "Full vision"; new UI widgets (all feedback is chat); new effects.

## Technical approach

- New server-side module (`GLOdysseyRift.{h,cpp}` under `G-Logic/Server`), owned by
  `GLGaeaServer`, ticked in its frame move; hooks: player enters/leaves land, crow death
  (credit the killer), chat (`buy`, `shop`, `essence`, GM `rift reset`). Built with the
  existing `GLLandMan::DropCrow` for spawns.
- The Trojan Horse: an NPC crow (one-bone skin around the static horse mesh) whose talk
  routes to the arena (exact mechanism - bus-station destination vs a new talk action -
  picked during planning after reading `NpcTalk` and the bus-station flow). If the horse
  model is not in game yet, a stock RAN NPC stands in at the same spot.
- Data: `mapslist.ini` arena entry on both client and server; NPC placement in Mystic Peak
  Square (MP_Campus 5,0) at coordinates read from the DB after the user stands there.
  Client data changes ship via the native app's update path; server via the VPS payload.
- Sources are CP949: edit through the iconv round-trip; verify `git diff --numstat`.

## Error handling
- Player disconnects / leaves map: removed from the run; if none left, run resets and its
  mobs are despawned.
- Server restart mid-run: run is lost (in-memory only); players respawn normally.
- Buying with too little Essence or outside a spot: chat explains, nothing changes.

## Testing
- Build on Windows CI; deploy the field server to the VPS.
- Live test with the native client (game window must stay in front): enter via the NPC,
  charge the beacon solo, clear rounds 1-3, buy one item, force a Convergence round with a
  GM `rift round 10`, die and see the scoreboard.
- Field server errlog must show no new errors.
