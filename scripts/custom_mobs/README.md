# Custom mobs and NPCs from glTF models

Turns a `.glb` (static or skinned + animated) into a brand-new RAN crow: the crow record, its name,
and the full skin chain (`.chf` → `.cps` → skin `.x` + skeleton `.x` → `.cfg` + `.bin` animations →
`.dds` textures). Model credits: [CREDITS.md](CREDITS.md).

## Crows

| Crow | Name | Type | Model / clips | Size |
|---|---|---|---|---|
| 66 0 | Trojan Horse | NPC (clone of 81 3, the `gate_holl.chf` NPC; no talk file) | static, one 2-key idle | 50 tall × chf scale 3 = 150 |
| 66 1 | Rift Zombie | mob (clone of 44 11 Mono Eye) | Idle, Walk_InPlace, Run_InPlace, Attack, FallingBack (die) | 20 |
| 66 2 | Rift Ghoul | mob (Mono Eye) | Idle, Walk (run = walk), Attack Combo (3 hits), Death; walk made in place | 16 |
| 66 3 | Skeleton Warrior | mob (Mono Eye) | rig\|Idel, Walck, Run, Attack_1, Dead; sword + shield ride the hands | 21 |
| 66 4 | Bone Dragon | mob (Mono Eye) | travelidle, travelmove, travelrun, attack_2, hurtsmall (shock), dead | 30 × 1.5 = 45 |
| 66 5 | Colossal Titan | mob (Mono Eye) | its one clip (scream) as idle and attack | 20 × chf scale 8 = 160 |

Main id 66 was free (stock crows use 0..65 and 72, 80, 81). Stats, AI and drops come from the
template crow; the attack is a plain melee hit (Mono Eye's mob skills are switched off) with the
model's attack clip and hit times. Tune per mob in `mobs.json` (`level`, `hp`, `body_radius`,
`attack_range`, `strikes`, `chf_scale`, `height`, `yaw`, clip names).

## Run

Needs numpy (`/opt/homebrew/bin/python3` has it; the pyenv `python3` does not) and ImageMagick
`magick`. Installing needs only python3 + openssl.

```bash
cd ~/Projects/RAN/RanOnline
PY=/opt/homebrew/bin/python3
$PY scripts/custom_mobs/make_mob.py                 # every mob in mobs.json (or name some: rift_zombie ...)
$PY scripts/custom_mobs/build_mobs.py               # -> port/build/custom_mobs/out/ (from COPIES of the client archives)
$PY scripts/custom_mobs/verify.py [--stock]         # re-parse + cross-check everything, play every anim
scripts/custom_mobs/xload_check.sh                  # load every skin through the native port's own D3DX loader
```

Per mob, `port/build/custom_mobs/mobs/<tag>/` also gets `<tag>_preview.png` (idle pose, front +
side), `<tag>_textured.png` (the written mesh + textures) and `<tag>_anims.png` (every anim,
played from the written files the way the engine does).

New model: drop the `.glb` in `assets/custom-mobs/`, add an entry to `mobs.json` (next free id,
unique `tag`, glTF clip names: `make_mob.py` prints the available ones when a name is wrong), run
the four commands, look at the PNGs. If the preview shows its back, set `"yaw": 180`.

## Install

The install tree `port/build/custom_mobs/out/` is laid out like a client folder (`data/…`,
`textures/mob/…`). `build_mobs.py --install` applies the same changes in place to any game folder
using that folder's own archives (safe if they differ from the build machine's), idempotently,
backing the three archives up once under `port/build/custom_mobs/backups/`.

**Client** (the played client is `~/Projects/RAN/client`):

```bash
python3 scripts/custom_mobs/build_mobs.py --install ~/Projects/RAN/client
```

Players get it through the normal native release (`release_native.sh`, Sparkle OTA): the changed
files are `data/glogic/glogic.rcc`, `data/skinobject/skinobject.rcc`,
`data/animation/animation.rcc` and the new loose files under `data/skeleton`, `data/skin`,
`data/animation` and `textures/mob`.

**Server** (`/opt/ran/game-client` on the VPS). The server needs the three archives: crow.mnsf for
the crows, and the `.chf` + `.cfg` because the field server reads every crow's animation set at
start (a crow whose `.chf` is missing pops a MessageBox on the server). Install all three
together:

```bash
VPS=root@167.233.23.97; D=/opt/ran/game-client/data
S=~/Projects/RAN/RanOnline/port/build/custom_mobs/server
for r in glogic skinobject animation; do mkdir -p $S/data/$r; scp $VPS:$D/$r/$r.rcc $S/data/$r/; done
python3 scripts/custom_mobs/build_mobs.py --install $S --server
for r in glogic skinobject animation; do scp $S/data/$r/$r.rcc $VPS:$D/$r/; done
ssh $VPS 'docker restart $(docker ps -q --filter name=server-giwbhnjmamk06qomgpvz02rj | head -1)'
```

Update the server and the clients together: a client without the data cannot show (or `/mob_gen`)
these crows.

## Test in game

- GM chat `rift spawn 66 1` spawns crow 66 1 next to you (any id, any map; GM3+).
- Or the console command `/mob_gen 66 1 <x> <y>`.
- Look for: the model stands on the ground at the right size and faces where it walks; idle loops;
  walk/run play while it moves; the attack anim plays and the hit lands mid-swing; it falls over
  and stays down on death; the name shows above it. For the horse: `rift spawn 66 0` in an open
  area, then click it (talk range = its radius 60 + 30).
- To use the horse as the Rift entrance: `entry_npc = 66 0` in `odysseyrift.ini`, then
  `rift reload` (only once every client has the model).
- If something is off, the client errlog says which file failed to load.

## What was verified offline

- `crow.mnsf` and `CrowStrTable.txt` round-trip byte for byte (also re-encoding every SACTION /
  SATTACK chunk from its fields); in the built `glogic.rcc` every stock crow is byte-identical and
  the new ones read back with the right id, type, skin, radius, attack anim and name.
- The readers mirror the C++ loaders and parse the stock data to the last byte: all 444 `.chf`,
  every `.cps` and `.cfg` of versions 0x105-0x107 (up to the effect list where one is present),
  all 2,613 `.bin` (`verify.py --stock`). Older file versions are not needed and are skipped.
- Every written file re-parses with those readers and cross-checks (chf → cps → skin/skeleton →
  cfg/bin names, piece slot, mesh container name, material count, bone names, key times vs cfg
  times, crow SANIATTACK vs attack cfg, key matrices proper with scale only along bone axes).
- Playing every anim from the written files (skeleton bind + `.bin` keys + offsets, row vectors,
  like `DxSkeleton`/`DxSkinMesh9`) matches the glTF sampled at the same time to < 0.002 units
  (in-place clips: no travel left).
- The native port's `xfile_sweep` parses all written `.x` (both file APIs, blob sizes) and its
  `D3DXLoadMeshHierarchyFromXA` loads every skin (container name, materials, skin info, indexed
  blended mesh with the engine's 26-matrix palette), same as it does for the stock `s_mob_me_01.x`.

## Known risks (need an in-game look)

- Nothing was run in the real game yet. The text `.x` route is the one the custom items use.
- The Windows/Wine build uses Microsoft's d3dx9 for the skin `.x` (the files declare the two skin
  templates themselves, like the stock binary files).
- Animation playback goes through the engine's quaternion compression and its own key
  interpolation: tiny jitter is possible on long bone chains (dragon tail, wings).
- Backface culling: some of these models were authored double-sided (thin parts may vanish from
  one side).
- Sizes and facing are guesses until seen next to players and buildings: `height`, `chf_scale`,
  `yaw` in `mobs.json`.
- The Colossal Titan skin has 42k vertices / 66k triangles (a 9 MB text `.x`): it may hitch when
  it first loads. The Skeleton Warrior keeps 123 of its 384 Rigify bones.
- Stats are the template's (Mono Eye: level 260, 63k HP, its drop table). Set `level` / `hp` per
  mob or use the Rift's own scaling.
- `rift spawn` of an id the server does not know fails quietly; a crow the server knows but whose
  `.chf` is missing on the server blocks it with a MessageBox at start. Install the three server
  archives together.
