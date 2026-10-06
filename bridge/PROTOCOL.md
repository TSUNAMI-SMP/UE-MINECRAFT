# Bridge protocol v1

UTF-8 JSON, one datagram per packet, at most 2048 bytes. MC sends from an ephemeral
loopback port to `127.0.0.1:7779`. UE replies to that source port. Do not expose it
on a network interface. No external relay or Minecraft server changes required.

Every input/event: `v:1`, `session` (UUID per world connection), `seq` (positive
integer, increasing, within JSON's exact integer range). UE accepts one live source;
a new source can take over after 500 ms without packets.

Input, at most 120 Hz and limited by MC's actual render FPS:

```json
{"v":1,"kind":"input","session":"00000000-0000-4000-8000-000000000001","seq":1,"x":0,"y":0,"z":0,"yaw":0,"pitch":0,"forward":1,"right":0,"jump":false}
```

`x/y/z`: Minecraft blocks relative to the player's feet at first frame in this
world. `yaw/pitch`: vanilla degrees. `forward/right`: key axes -1..1. `jump`: held
state (UE exposes rising-edge notification). Input gets no ACK; old/reordered
input is discarded. After 250 ms without valid input, key states reset and pose
holds. GUI/pause sends neutral keys. World disconnect stops transmission.

0.3.0 adds optional typed fields: `sneak:boolean`, `eyeHeight:0.1..2.5`,
`bodyHeight:0.2..3` in blocks. Defaults for old senders are false/1.62/1.8.
UE applies body height to the capsule and places the camera eyeHeight above feet.

UE mapping: `(X,Y,Z) = spawnFeet + 100 * (mcZ,-mcX,mcY)`, yaw `mcYaw`, pitch
`-mcPitch`. Character capsule center adds its half height above feet. First-person
camera uses the supplied eyeHeight (default 162 cm). Movement authority stays in Minecraft: position is
mirrored directly; UE does not simulate a second player movement controller or
call Jump twice. UE wall debris is independently simulated in Chaos.

Event, only on ignition of a TNT block with flint and steel or fire charge:

```json
{"v":1,"kind":"event","session":"00000000-0000-4000-8000-000000000001","seq":2,"eventId":"00000000-0000-4000-8000-000000000002","event":"tnt_ignite","x":0.5,"y":0.5,"z":3.5}
```

MVP explodes immediately in UE at TNT's center, not after Minecraft's fuse.
Since MOD 0.2.0, the client matches a subsequently loaded primed TNT entity with
the locally clicked TNT position, within 2 seconds. Block disappearance alone
does not trigger it. This confirms observed priming, not a cryptographic proof of
which player caused it. Intended for a new local singleplayer test world. Redstone,
chain explosions, placing TNT, bow ignition, and protected multiplayer worlds
are outside this MVP.

UE ACK:

```json
{"v":1,"kind":"ack","session":"00000000-0000-4000-8000-000000000001","eventId":"00000000-0000-4000-8000-000000000002"}
```

MC retries same eventId every 100 ms for 2 seconds, up to 64 pending events. UE
deduplicates IDs for 10 seconds, at most 2048 IDs (256 in 0.1.0), and ACKs duplicates. This bounds
memory and retry traffic; it is not persistent delivery across UE restarts. Restart
UE Play and reconnect the MC world together when testing. ACK indicates receiver
handled the event, not proof that an assigned Niagara asset rendered or wall broke.

Future types (`block_place`, `block_break`, `mob_state`, `hp_state`)
should add versioned payloads/handlers behind this transport. Unknown types are
ignored. No Minecraft or Fabric types in `BridgeTransport`; no rendering code in
the sender.

## Additive 0.2.0 messages (v1)

After a fully validated input, UE sends status at most 4 Hz, echoing that input's
sequence. MOD only accepts replies referencing a recently sent input from its own
session. A duplicate/older status does not extend readiness. MC times out after
1 second. RTT includes UE scheduling and MC receive polling, not photon latency.

```json
{"v":1,"kind":"status","receiver":"ue","session":"00000000-0000-4000-8000-000000000001","seq":1,"cameraReady":true,"vfxReady":true,"walls":1,"previewBlocks":0}
```

`cameraReady`: active Camera component and Controller on the target Character.
`vfxReady`: Niagara asset assigned. `walls`: tagged collections with rest assets.
These indicate configuration, not a successful frame/physics outcome. The Python
listener uses `receiver:"diagnostic"` and reports all rendering capabilities false.
Old UE still accepts new MOD inputs/TNT events but cannot report status or new event types.

Optional bow event (`/uebridge bow on`, default off): common event envelope plus
`event:"bow_fire"`, `x/y/z` at MC eye relative to origin, normalized Minecraft
`dx/dy/dz`, `pull` in 0.1..1. UE swaps direction axes `(dz,-dx,dy)` without scaling
and may launch a local prototype arrow at `6000*pull` cm/s. No MC entity tracking,
damage, Mob or HP synchronization. The vanilla BowItem return is client-side
success; server permission/arrow-hit authority is not replicated.

Manual nearby cube snapshot (`/uebridge preview`): common event envelope plus:

```json
{"event":"block_snapshot","snapshotId":"00000000-0000-4000-8000-000000000003","snapshotSeq":10,"batchIndex":0,"totalBatches":1,"blocks":[[0.5,-0.5,0.5,6657602]]}
```

The row is `[relative mc x, relative mc y, relative mc z, mapColor RGB integer]`.
Centers, not corners; all rendered cubes have side 100cm. `snapshotSeq` is the
sequence of the first emitted batch and stays identical across the snapshot.
It must be no larger than each envelope's `seq`. Each batch has its own eventId
and normal retries/ACK. Max 12 rows/batch, 217 batches, 2601 blocks, 64 colors.
An empty snapshot is a single empty batch, replacing the display with no cubes.

UE stages out-of-order batches, deduplicates them, and replaces the display only
once all numbered batches arrive. Newer snapshotSeq supersedes older staging.
Incomplete staging expires after 15 seconds; previous complete display stays.
`event:"block_preview_clear"` clears staged and displayed data and uses its own
envelope sequence as a generation barrier; late older batches are ACKed/ignored.
Session changes clear preview data because the MC origin has changed.

The sender emits at most 4 snapshot batches/client tick and reserves 16 pending
slots for gameplay events. No automatic periodic snapshots or differential block
updates; the user explicitly requests a snapshot. Cubes have no collision, no
Chaos and no textures. Non-full-cube blocks/unloaded chunks are skipped.

UE fully validates every message (types/ranges/UUIDs) before renewing session
leases. Input frames are drained up to 256 datagrams/tick and only the latest pose
is applied once. No network callback thread mutates UE objects.

## Automatic world stream (0.3.0, additive v1)

Status adds `build:"0.3.0"`, `receiverId` (fresh UUID on each BeginPlay),
`worldV1:true`, `videoV1:true`, `worldCells`, `worldShapes`, `videoReady`.
The MOD gates new traffic on supported capabilities and clears its cell cache
when receiverId changes or status reconnects, including very short UE restarts.

`world_scope`: event envelope, `cellX/cellY/cellZ` integer absolute Minecraft
cell coordinates (`floor(blockCoordinate/8)`), `radius:1..3`, `halfHeight:1..2`.
No MC origin is needed to interpret cell keys; geometry positions remain relative
to the session origin. A new scope prunes cells outside the inclusive axis bounds.
Scope is sent after the previous transfer completes and is ACKed before new cells.

`world_cell`: snapshot envelope with `cellX/cellY/cellZ`, `snapshotId`,
`snapshotSeq`, `batchIndex`, `totalBatches` and rows:

```json
{"event":"world_cell","cellX":-1,"cellY":0,"cellZ":0,"snapshotId":"00000000-0000-4000-8000-000000000003","snapshotSeq":10,"batchIndex":0,"totalBatches":1,"blocks":[[0.5,-0.25,0.5,6657602,1,0.5,1]]}
```

Rows: `[relative centerX,centerY,centerZ,mapColorRGB,sizeX,sizeY,sizeZ]`.
MC box scales map to UE `(sizeZ,sizeX,sizeY)`. Max 8 rows/batch, 1024 batches,
8192 shapes/cell, 64 colors/cell, 131072 displayed shapes. Dimensions must be
finite in (0,4]. Empty cells use one empty batch to remove previous geometry.
The UE stages at most 4 incomplete cells, with 60-second deadlines, then commits
only complete newer snapshots atomically per cell. Revisions/clear barriers and
scope bounds reject late stale data. Unfinished updates retain the previous cell.

MC scans 128 blocks/client tick and prioritizes notified block changes and their
neighbors. Continuous bounded rescans cover chunk loads and missed notifications.
Buried opaque cubes are omitted. Outline boxes and approximate fluid boxes are
sent; no textures, block entities, mobs, collision or persistent world export.
At most 6 batches/tick, with 16 gameplay queue slots reserved. SHA256 fingerprints
are cached only after all events ACK; expired transfers force a new full cell
snapshot. There is no byte delta within a changed cell. Radius defaults to 2 and
halfHeight to 1 (75 cells, 40x24x40 blocks). Limits are independent of camera rate.

`world_clear`: clears cell/staging/revision data, installs a sequence barrier and
requires a newer world_scope before accepting cell snapshots. Session takeover
also clears all streamed world data. Manual block_preview remains separate.

## Video return path (0.3.0, TCP v1)

UE listens only on 127.0.0.1:7780, separate from UDP. One client sends exactly
40 ASCII bytes: `UEBH` plus the 36-character current UDP session UUID. The UE
accepts loopback clients only, checks the session, and drops a mismatched/stale
handshake after 2 seconds. This correlates the streams; it is not a security
boundary against other processes on the same PC.

Each UE frame has six unsigned 32-bit words in **network byte order**:
magic 0x55454256 (`UEBV`), version 1, width, height, frame sequence, JPEG byte length;
then that many JPEG bytes. Width/height <=1920x1080, payload <=2 MiB. Sequence wraps
as uint32; the ordered TCP stream supplies frame order. MC verifies JPEG dimensions
before pixel allocation. No audio. Default 480x270, maximum15fps, quality75.

UE captures the target Camera's current view with SceneCapture2D, performs a
synchronous GPU pixel readback, compresses JPEG on a thread-pool worker, and sends
nonblocking partial writes. One compressed frame/in-flight encode, no accumulated
frame queue; a stalled send disconnects after 2 seconds. Capture cost can affect
UE FPS even though it uses another socket. Target/worker cleanup occurs on EndPlay.
MC receives/decodes on a dedicated daemon, retains only the latest decoded frame,
uploads textures on the render thread, and draws a HUD layer before the crosshair.
Timeout/disconnect retries do not block input. Aspect ratio is preserved. GPU
capture/JPEG means this is a bounded prototype, not a zero-copy streaming pipeline.

## Block textures and video settings (0.4.0, additive v1)

Status adds `build:"0.4.0"`, `blockTexturesV1:true`, `videoControlsV1:true`,
`textureMaterials:0..4096`. The MOD gates these features on the capability flags;
older receivers still get legacy world_cell rows without block IDs.

`world_cell_textured` has the same world_cell envelope, plus an eighth row field
containing the registry block ID, for example
`[0.5,0.5,0.5,8355711,1,1,1,"minecraft:stone"]`.
IDs contain one namespace colon and valid lowercase registry characters, <=128
characters. Max 4 rows/batch, 2048 batches, 8192 shapes/cell. Complete cells are
limited to 256 ID/color groups, 64 colors; the global 131072-shape cap is unchanged.
The cell fingerprint includes block identity. Textures themselves do not cross UDP.
UE resolves IDs against its local BridgeBlockPalette; missing IDs use the existing
color material. The manual preview remains unchanged and uses no textures.

`video_config` is a reliable event with the common x/y/z envelope and integer
`width:160..1920`, `height:90..1080`, `fps:1..30`, `quality:30..95`, and finite
`exposure:-6..6`. UE applies it on its game thread and ACKs normally. Settings are
resent on receiver restart/reconnect or event expiry. The video wire protocol and
2 MiB frame limit remain unchanged. The 0.4.0 default is 960x540 / 20fps / quality85.
SceneCapture retains its exposure history, uses an explicit 2.2 output gamma and
allows an additive exposure bias. Capture/JPEG overhead still limits actual FPS.

Local export format: JSON `format:"uebridge-block-textures"`, `version:1`,
`blocks` maps block IDs to top/side/bottom `{texture,tint}`; `textures` maps resource
IDs to relative PNG file, width, height and SHA256. The UE import script validates
paths, dimensions, file size, PNG CRC and hash before creating assets. Export uses
active client resources and default block state, not the live variant of each
placed block. Multipart/complex models fall back; animations use a static tile.
Generated assets and exports are local and are not included in source bundles.

## UE movement authority and initial import (0.5.0, additive v1)

Input adds optional strictly boolean `controller`, default false. When the world
is sealed and the target is BridgeCharacter, true enables UE CharacterMovement:
normalized WASD relative to yaw, gravity, collision, rising-edge jump and native
crouch/uncrouch ceiling checks. MC position/body pose is ignored after seal. Input
age >250ms freezes movement/gravity; GUI input is neutral. The MC local player is
held at its source position and vanilla attack/use/break are suppressed during
import/controller mode. Start is limited to a grounded creative singleplayer.
No HP/inventory authority synchronization or UE item actions are added yet.

Status adds `authorityV1`, `worldSealed`, `ueControl`, `importId` and
`importedCells` (0..245 including empty cells). Capability is true only with a
BridgeCharacter. Mode is session-local, not automatically enabled on startup.

`world_begin`: reliable event with importId UUID, `ox/oy/oz` absolute Minecraft
origin (each within +/-30 million); x/y/z envelope remains relative zero. Explicit
new import clears UE imported terrain, resets its barrier and prepares physics.
Same importId is idempotent, including after sealing. Older begin sequences cannot
replace a newer import within the source. A new source resets transport barriers
but retains sealed geometry until an explicit new import.

`world_cell_physics`: existing textured envelope, four rows/batch, <=2048 batches;
row `[x,y,z,color,sx,sy,sz,blockId,collision:boolean]`. MC uses collision shape
boxes for solids, outline/fluid boxes with collision=false otherwise. Buried
blocks are included. Unloaded horizontal chunks are deferred, never committed
as empty during initial import. Scope is fixed for this import and includes six
invisible perimeter collision boxes in UE. Legacy live streams still omit buried
cubes and have no collision.

`world_commit`: importId and integer `cells` 27..245. UE seals only if importId
matches, scope exists, count equals the entire expected scope, all cells including
empty ones have committed and no stages are incomplete. ACK confirms handling;
MC waits for matching sealed status before READY. Begin/commit are retried with
same importId if lost; restart of receiverId changes the client to LOST and never
silently reimports. Per-cell ACK/retry rules are retained.

Sealed UE worlds ACK/ignore source cell/scope/clear messages. MC stops scanning;
legacy refresh/on/off does not overwrite it. Source reconnect preserves sealed
world within the same UE Play. Play end destroys session-local terrain; disk
persistence is not provided. Blueprint RemoveImportedBlocks exposes removal of
shapes whose centers fall within a UE-space sphere, updating render/collision and
stored geometry without changing Minecraft.

UE returns `kind:"pose"` at most60Hz, with session, receiverId, echoed input seq,
increasing poseSeq, relative MC-space feet x/y/z and grounded:boolean. Inverse
mapping is `(-UErelativeY,UErelativeZ,UErelativeX)/100`. MC accepts only the current
receiver, correct session, a sent input <=1s old, valid finite coordinates and a
new poseSeq; cached pose expires after1s. Used for diagnostics in this stage; MC
world position is deliberately not teleported to avoid reintroducing vanilla
collision and chunk movement as gameplay authority.
