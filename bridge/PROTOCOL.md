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

Later versions below add authoritative block actions, mobs and UE health.
Other future types should add versioned payloads/handlers behind this transport. Unknown types are
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


## Authoritative editing, first-person view and video v2 (0.6.0)

Status advertises `build:"0.6.0"`, `blockActionsV1:true`, `videoV2:true` and the
last attempted action's result string `lastAction`. Actions require a sealed
import, matching importId, fresh input and active UE control. Retransmissions are
ACKed without a second edit; older action sequences cannot replay against a newer
aim or import. Attempts are limited to one per 80ms; MC repeats held buttons every
200ms. GUI/pause disables buttons. This remains dedicated creative mode.

Input adds optional `sprint:boolean`, `heldItem` and `heldBlock` (empty or valid
registry IDs <=128 chars), `heldColor:0..0xffffff`. MC supplies heldBlock only for
a default full-cube collision shape. The receiver updates its arm/item visuals
without generating materials on every input. Item models other than cubes are
placeholders; skin and inventory authority are not synchronized.

`block_action`: reliable event, common envelope, `importId` UUID,
`action:"break"|"place"`, finite yaw/pitch with input bounds, and the same held
selection fields. The click carries its aim and selection; UE uses its own current
camera position, casts up to 500cm and only accepts imported collidable instances.
Break removes all shapes owned by that voxel. Place selects the adjacent voxel
using the hit normal, validates scope, occupancy, body/geometry overlap and shape/
material budgets, then adds a collidable cube. Only the edited cell is rebuilt,
without a Minecraft rescan. No vanilla world mutation or item-count update occurs.

For an actions-capable receiver, `world_cell_physics` rows append absolute source
voxel `blockX,blockY,blockZ` integers, producing 12 fields instead of 9. Bounds are
+/-30 million and their floor-divided cell must match the envelope cell. This
identifies all partial shapes belonging to one block. Four rows still fit the
2048-byte datagram bound. The receiver also accepts older 9-field physics rows;
the MOD only sends owners when blockActionsV1 is advertised. Owners join the cell
fingerprint. Source refresh cannot overwrite edited, sealed cells.

UE video now sends version 2. The first six network-order uint32 words remain
magic/version/width/height/frameSeq/JPEG byte length. The header then appends one
network-order uint64 input sequence and two uint32 microsecond timings: capture
request to GPU-readback completion observation, and JPEG encoding time. Header is
40 bytes, payload cap remains 2MiB. MC accepts both v1 and v2 and validates metadata
before allocation. An old MOD cannot decode v2; update both components.

Capture runs after movement and first-person visual updates. GPU texture copies
and fence polls run on the render thread; there is no per-frame ReadPixels or
GPU wait. At most two readbacks, one encode and one partial output are retained;
finished readbacks prefer the newest frame. Readbacks older than 250ms are skipped
before encode. Encoding owns CPU pixels only. RHI references survive queued copies;
shutdown drains rendering before teardown. TCP buffers are bounded to limit stale
frames. JPEG decode/channel conversion run on the MC worker; native RGBA pixels
are bulk-copied before render-thread upload. Fullscreen fresh UE control can skip
native Minecraft world drawing while retaining camera/input/HUD and world data.
The optimize command disables this independently.

Video config fps bound is now 1..60 when videoV2 is advertised; old receivers get
at most30. Presets: low480x270/30/q75, balanced960x540/60/q85,
high1280x720/60/q90, ultra1920x1080/60/q90. These are caps, not guaranteed rates.
Input-to-upload latency uses MC's retained input send timestamp echoed by the
captured frame, without cross-process clock subtraction. It ends at GPU upload
submission, excluding upload completion, VSync and scanout. GPU-readback timing
includes fence-poll scheduling; it is not a pure hardware copy measurement.

## Player appearance, perspective and vanilla feedback (0.7.0)

Status adds `playerVisualsV1:true`, `vanillaFeedbackV1:true`, `skinReady` and
`particlesReady` configuration flags. MOD gates appearance fields on the first
capability. Inputs optionally contain `perspective:0|1|2` (first/rear/front),
`skinLayers:0..127` (vanilla PlayerModelPart mask), `slimArms:boolean`,
`leftHanded:boolean`, `swingProgress:0..1`, `equipProgress:0..1` (1 raised),
`usingItem:boolean`, `useAction` and `useProgress:0..1`.
`cameraFov:30..110` is Minecraft's vertical base FOV; UE converts to horizontal
FOV for the video stream's 16:9 aspect. Defaults are first person, all layers,
classic right arm, swing 0, equip 1, unused, and FOV 70.
UseAction accepts none/eat/drink/block/bow/spear/trident/crossbow/spyglass/
toot_horn/brush/bundle. This visual state does not grant item gameplay effects.
Additional currently unused client metadata is ignored (offItem/equipOff/useTicks).

Perspective samples `options.getPerspective()` after Minecraft handles its own
key binding; the bridge never consumes a second toggle or hardcodes F5. Third
person offsets the display camera 400cm and sweeps against colliders. Aiming and
block edits use the authoritative eye position and facing, independently of that
display camera. Native swing/equip interpolation is sampled; native swing ticks
are explicitly advanced while vanilla player movement is frozen.

`/uebridge player export` writes the already loaded skin to a new local export
directory. Manifest `kind:"player",version:1` identifies a checksummed normalized
64x64 PNG, classic/slim model, player UUID and name. UE's local Python importer
validates all metadata/path/size/hash/PNG CRCs before writes, creates local
skin material/appearance assets, and assigns the saved level's sole receiver.
It adds no binary asset traffic to the high-frequency input socket.
Block texture manifests now optionally include `particle:{texture,tint,color}`;
palette particle maps are distinct from the world face material maps. Old
manifests remain readable. Tints sample the export location; grass dust is untinted.

UE emits outcome feedback after successful break/place and measured grounded
walking/landing, without replaying a Minecraft world action:

```json
{"v":1,"kind":"feedback","session":"00000000-0000-4000-8000-000000000001","receiverId":"00000000-0000-4000-8000-000000000002","effectId":"00000000-0000-4000-8000-000000000003","seq":20,"type":"break","block":"minecraft:stone","x":0.5,"y":0.5,"z":3.5,"listenerX":0,"listenerY":1.62,"listenerZ":0,"listenerYaw":0,"listenerPitch":0,"fallDistance":0}
```

Positions are relative MC-space source/camera in blocks, yaw/pitch in MC degrees.
Finite source/listener coordinates are bounded +/-100000, pitch +/-90,
fallDistance 0..1000, block is a registry ID, and IDs are UUIDs. `seq` echoes a
sent input <=1s old; retransmissions refresh the echoed seq but retain effectId
and outcome coordinates. MC requires the active session/receiver/capability,
validates the entire payload before ACK, and bounds queued feedback to64.
Duplicate effect IDs are ACKed again without delivery (dedup256). Overflow is
not ACKed, allowing retry. Sound playback independently deduplicates256 IDs.

```json
{"v":1,"kind":"feedback_ack","session":"00000000-0000-4000-8000-000000000001","seq":21,"effectId":"00000000-0000-4000-8000-000000000003"}
```

ACK has no position/eventId. It only removes an existing pending effect for the
current session/source endpoint; it cannot acquire/renew the input lease or edit
terrain. UE retains at most64 effects, retries every100ms for1s, and clears them
on disconnect, controller stop or session takeover. This is bounded retry, not
persistent guaranteed delivery. MC uses its active registry SoundGroup and
SoundManager, rebasing source relative to the UE view onto its actual audio
listener. Block sounds use vanilla volume/pitch formulas. No audio files are
exported. Ordinary jumps and creative landing have no invented fall sound.

UE renders block dust so it remains visible while vanilla world rendering is
skipped. A full-cube break emits64 fragments; sprint dust samples20Hz, normal
walking emits none. Quarter-sprite UVs, lifetime, gravity, drag and brightness
follow the Minecraft particle values; lighting and collision use UE. At most384
particles and64 material groups. Movement events use imported block metadata;
unsupported world objects are silent. Special block sound branches, arbitrary
shape subdivision and per-voxel biome tints remain future extensions.

## Movement, camera and dust diagnostics (0.8.0)

Packet version remains 1; `build` is `0.8.0`. Existing 0.7 appearance and feedback
fields remain compatible. `sprint` in UE-control mode now represents a local
intent latch driven by the configured sprint key or a second forward-key press
within 350ms. It clears on forward release, backward input, sneaking, menus,
paused input, stale/missing authority, disconnect and control/import transitions.
The bridge does not set the frozen Minecraft player's sprint attributes.

`cameraFov` remains the user's **base vertical FOV**, not a pre-multiplied sprint
value. Missing fields now default to80; an explicit Minecraft setting is retained.
Set Minecraft FOV to80 for the requested base view. UE smooths its sprint
multiplier from1 to1.15, then converts to the stream's horizontal FOV. Camera,
eye aim and reported feet share a grounded floor-gap correction; the physical
collision capsule is not lowered into the floor. Avatar body yaw is presentation
only and cannot change authoritative aim/movement.

`land` feedback now plays the supporting block's existing fall sound even for
creative mode and ordinary jumps (UE emits for a measured fall>=0.25 blocks).
No player fall-damage sound or HP/inventory effect is added. The landing frame
does not also generate a walking-step event; existing effectId deduplication and
ACK/retry rules remain unchanged.

Status optionally contains `particles` alongside `particlesReady`:

```json
{"particlesReady":true,"particles":{"reason":"ready","materialReady":true,"textureCount":10,"requested":64,"spawned":64,"rejected":0,"active":40,"instances":40,"peakInstances":64,"groups":1,"lastType":"break","lastBlock":"minecraft:stone","lastRequested":64,"lastSpawned":64,"lastReason":"ready"}}
```

Readiness checks the material's required texture/color parameters, instancing
usage, plane, palette, usable textures and camera. It does not prove shader
compilation or visible GPU output. `instances` and `peakInstances` count current
and maximum registered CPU instances; these are not rendered-pixel counts.
Requests, spawned and rejected totals saturate at2^53-1 and persist for the
effects actor's lifetime. Last-request fields retain a short-lived break result
after particles expire. Material/texture/camera failures, group/particle limits
and instance-submission failures have bounded reason tokens and rate-limited
UE log messages. No walking particles are emitted.

The MOD validates this optional extension independently of the main status.
Missing extensions identify an older receiver; invalid diagnostic metadata does
not invalidate otherwise valid receiver readiness. `/uebridge status` displays
the extension without asserting GPU visibility. The nested extension and bounded
strings remain within the2048-byte UDP datagram limit.

## Models, ground mobs and native sky (0.9.0)

Status capabilities: `blockModelsV2`, `blockPaletteReady`, `mobsV1`,
`mobPaletteReady`, `videoV3`, `skySupported`. A missing model palette prevents
initial import. `blockModelError` identifies a rejected state or missing face
material; a cell is not acknowledged as complete with missing visuals.

Physics rows may append a canonical sorted state key and role to the previous
12-field owner row:

```text
[x,y,z,rgb,sx,sy,sz,blockId,collision,ownerX,ownerY,ownerZ,stateKey,role]
```

`role=1`: baked render model, no collision. `role=2`: native collision boxes,
hidden, Pawn blocking. It also blocks Visibility when that owner has no separate
outline boxes. `role=3`: hidden native outline boxes, Visibility only. Identical
collision/outline lists share role2 instead of duplicating proxies. Owners stay
in their declared cell. State keys are at most1024 characters; roles1/3 require
collision=false, role2 requires true. Old7/8/9/12-field rows remain parseable.
New physics batches use at most4 rows and a1500-byte row budget, up to8192 batches
and8192 rows per cell. Cells stage atomically for60seconds; total world shapes
are bounded at524288. Event envelopes still fit2048bytes. Normal imports bake
variants/multipart and export native collision/outline metadata; special blocks
are explicitly excluded locally. See `docs/BLOCK_SUPPORT_0.9.0.md`.

Block actions optionally include typed `sneak`. Unsneaked right clicks use wooden
doors/trapdoors/gates/levers/buttons before placing. Same-slab clicks merge only
on the open half's appropriate vertical face. Other placement states derive from
UE hit position, face and Minecraft yaw; source block edits remain UE-local.
All-item models and complete block-specific gameplay mechanics are outside this version.

`video_config` optionally adds `lighting:boolean` (defaulttrue),
`vanillaSky:boolean` (defaultfalse), `particleScale:0.25..2` (default.75),
`particleDensity:0.125..1` (default1), `particleLifetime:0.25..2` (default.9).
Lighting=true with vanillaSky=true is rejected. Lighting flags affect stream
scene captures, preserving saved assets/editor view. Paused input sets controller=false.
First-person projected meshes use fixed verticalFOV70 while the world retains
its smooth cameraFov/sprint projection. They share world depth.

A v3 client sends `UEB3` +36-byte session UUID instead of legacy `UEBH`.
Legacy clients continue receiving v2 JPEG frames. V3 headers are84bytes:

```text
uint32 magic='UEBV', version=3, width, height, frameSequence, jpegLength
uint64 inputSequence
uint32 readbackMicroseconds, encodeMicroseconds
uint32 flags(bit0=skyMask), maskLength
float64 absoluteMinecraftCameraX,Y,Z
float32 minecraftYaw,minecraftPitch,verticalFov
byte[jpegLength] JPEG
byte[maskLength] lossless mask
```

All numbers are big endian IEEE/integer. The mask is a stream of
`uint16 runLength` + `uint8 opacity`; every run is positive and runs must cover
exactly width*height. Limits1920*1080, JPEG2MiB, mask3*1920*1080bytes.
SkyMode pairs the same-view LDR color and SceneColorHDR inverse-opacity capture,
removes UE sky/atmosphere/fog, and latches RGB/mask/camera/session/mode revision
together. It does not color-key pixels. Matching captures disable temporalAA.
Decoder converts captured premultiplied RGB back to straight alpha in gamma2.2
before the native GUI blend; fully transparent pixels are zeroed.

Minecraft renders only its native sky/celestial/cloud framegraph behind that
latched frame, using native time/weather and captured camera position/rotation.
Image letterboxing adjusts sky projection to the same focal length. No native
terrain/entities/particles/hand or frozen-player fire/water overlays draw in
that mode. Rain/snow geometry and GPU shared textures are not implemented.
`maskPixels/maskForeground/maskTranslucent` are CPU mask pixel counts, not FPS.

`mob_spawn`: ordinary reliable event envelope plus importId, mobId UUID,
mobType identifier, appearance64-lowercase-hex key, relative feet x/y/z,
yaw, width/height0.05..16, health/maxHealth0.01..10000 (health<=maxHealth),
speed0..2, damage0..100, hostile/baby typed booleans. UE requires the current
sealed import and active controller. Missing palette/appearance or failed spawn
gets noACK; retries expire and MC does not suspend originals. Accepted UUIDs
are tombstoned for that world import, including dead copies. `mob_clear` requires
the current importId. Modifications never write source position/NBT/health.
In dedicated integrated singleplayer (not LAN), only successfully acknowledged
imported source mobs receive a2second in-memory lease cancelling the outer
ServerWorld.tickEntity call. Off/pause/stale control/disconnect releases it.
Ground copies use generic wander/chase/melee; species-specificAI, flying/aquatic
mobs and feature render layers are not complete.

`mob_feedback` uses reliable effectId/session/receiverId/seq and feedback_ack,
`type:"mob"`, a native `sound` identifier and numeric `lx/ly/lz` in blocks
relative to the UE camera's right/up/forward axes. MC remaps these to its audio
listener and uses active-resource-pack sounds. UUIDs accept upper/lowercase hex
whileACK preserves the original wire ID. No MC saved-player damage is applied.
UE playerHP is reported in `uePlayerHealth` and displayed in the native HUD.
AtHP0 UE movement/editing stops. `player_respawn` requires current importId,
active control and HP0; UE finds an unblocked original spawn position and resetsHP.

`mobCount/mobMissing/mobReason` and `/uebridge mobs` expose import outcomes.
CloudJava/Python/portableC++ tests validate protocols/assets/math. They do not
validate UE module compilation, shader output or full game integration.

## Native held items, spawn eggs, creative flight and color correction (0.10.0)

Input optionally appends typed `creative:boolean` and `flying:boolean`, both
default false. UE enables MOVE_Flying only while the current controller lease
is active and both flags are true; losing permission restores gravity. The MC
client derives creative from the real player game mode and toggles flying with
configured jump-key rising edges within350ms. Jump/sneak are ascent/descent.
Takeoff ignores the still-grounded first frame; landing clears flight after an
airborne frame. This does not implement complete survival inventory/hunger rules.

Input and block actions may include `heldModelKey` (item identifier + `@` +64
lowercase hex, maximum256 characters), and `spawnType` (entity identifier).
Held keys hash canonical, recursively key-sorted ItemStack codec JSON with count1;
components affect the key, stack count does not. Empty hand uses an empty key.
Block-action spawn eggs use UE hit position, the imported entity-type template
and event UUID; retries do not duplicate entities. MC terrain/entities are not
modified. Grounded templates only;128 UUIDs per world import. Missing templates
or occupied spawn positions produce an action diagnostic.

Local items export `kind:"items",version:1` contains `items` keyed by heldModelKey,
each with firstperson_righthand/lefthand and thirdperson_righthand/lefthand arrays.
Native display transforms are already applied to centered item-space vertices.
Each face has4 vertices (3 finite numbers),4 UVs (2 finite numbers), texture SHA256
and RGB tint integer. Textures are checksummed local PNGs. The MOD captures native
default registry stacks and current hotbar/offhand components; UE displays only
the main hand. Unsupported native draw commands are in `excluded`; no stick
substitution is used. Static snapshots do not reproduce live conditional/use
models, glint or animated texture playback. Re-export changed components.

Mob export additionally includes `templates:{entityType:appearanceHash}`. Each
referenced appearance includes validated width/height/maxHealth/speed/damage,
hostile/baby template stats. Templates are captured from detached native entities,
never added to the Minecraft world. Old exports may import individuals but have
no egg templates. Imported visuals and AI retain the0.9 ground-mob limitations.

Vanilla feedback types extend to `open`, `close`, `activate`, `deactivate`.
Minecraft selects the native BlockSetType/WoodType sounds (including button
release), uses active-resource-pack audio and the existing relative listener
position. Reliable effect IDs/ACK deduplication are unchanged. Doors emit one
sound for the two-half state change; timed button release emits a separate event.

A paired0.10 client still sends `UEB3`; its UE receives version4 frames, with the
same84-byte header/mask RLE as v3. RGB is now **straight sRGB**, rather than v3's
premultiplied display RGB. The client attaches mask alpha without re-unpremultiplying
or applying a gamma correction. Legacy v3 decoding remains for old UE streams.
Do not mix old UEB3 clients with the0.10 sender. Legacy UEBH remains version2.

Lighting ON uses FinalToneCurveHDR in linear sRGB; OFF uses SceneColorHDR with
inverse opacity. FloatRGBA readback is converted to sRGB once on the worker.
OFF's RGB/mask come from one capture and RGB is unpremultiplied before encoding.
Temporal AA and motion blur are disabled on streamed captures. Generated diffuse
materials use Specular0; selection edges use a black unlit material and the union
boundary of native outline boxes. JPEG/TCP remains; GPU sharing is not implemented.

Status adds `itemsV1`, `creativeFlightV1`, `itemModelCount`, `heldModel`, `flying`,
`mobTemplateCount`, `videoColor`. Pose optionally adds `flying`; old grounded pose
fields remain unchanged. Status strings are bounded; UDP responses use condensed
JSON and respect the2048-byte budget. Tests do not establish UE5.8 compilation
or rendered color correctness on Windows.

## Compressed item exports (MOD/importer0.10.1, UE0.10.0)

Completed `manifest.json` now contains `kind:"items",version:2,payload:"items.json.gz"`,
SHA256 of the gzip file and integer `uncompressedBytes`. The gzip payload is the
previous version1 items JSON. Export streams UTF-8 JSON into gzip instead of
creating the complete uncompressed JSON string and byte array. Limits are256MiB
uncompressed and64MiB compressed; existing texture/geometry budgets still apply.
Only a completed payload publishes the manifest, so interrupted directories are
ignored by latest-export selection. Importer checks containment, checksum, gzip
CRC, exact bounded decompressed length and all previous geometry/PNG validations.
Legacy uncompressed version1 exports remain supported. UE palette data and the
network/rendering protocol are unchanged; no UE module rebuild is required.

## Terrain, lighting, item transactions and GPU video (0.11.0)

This section supersedes the earlier release-specific limits and behavior. Both
MOD and UE sources must be updated. UDP remains loopback JSON v1, at most2048
bytes. Source Minecraft blocks are read; UE maintains gameplay edits. Local
Minecraft player inventory is changed only by the new item escrow described below.

### Per-event receipts and newly summoned mobs

UE event ACKs add `receiverId`, the exact event's `seq`, `accepted:boolean`, and
`reason` (at most160 characters). An accepted transport receipt establishes that
this event was handled successfully, not that a shader/model was drawn. Rejection
receipts terminate retries and expose the actual failure stage. The client matches
receiver, session, event UUID and sequence before granting source-mob ownership.
Legacy ACKs remain accepted for older non-transactional features. UE retains up to
32768 event IDs/results for10seconds; this larger window covers compact terrain
traffic without exhausting the former2048-ID budget. Client per-event receipts
are retained in a1024-entry bounded map until receiver change.

After sealing, native mobs are scanned every250ms near both the frozen MC source
position and the current UE position. Read-only integrated-server snapshots use
the spatial entity index for up to128 new target UUIDs, including entities outside
the frozen client player's tracking range; primitive results cross to the client
thread. Old/different-dimension/LAN results are discarded on source generation
changes. No source entity is created, moved or rewritten by this scan. Newly summoned adult UUIDs can resolve their
species through `templates`, with exact-type adult appearance fallback for older
nearby-only palettes. An exported individual UUID retains its captured appearance.
Baby geometry without its individual capture is deferred rather than substituted
with an adult. `/summon` keeps its actual Minecraft coordinates; relative vanilla
commands therefore still reference the frozen native player. New command
`/uebridge mobs summon minecraft:zombie` (or villager) sends:

```json
{"event":"mob_template_spawn","importId":"00000000-0000-4000-8000-000000000003","mobType":"minecraft:zombie","x":0,"y":0,"z":2}
```

This reliable event uses UE-relative feet and the normal event envelope. The
assigned palette, exact species, loaded-terrain bounds, supported floor, world
capsule overlap, player capsule and actor/model initialization are checked before
acceptance. A bounded adjacent-position search may move an obstructed ground spawn;
it never skips the selected wall or closed door. Individual `mob_spawn` still
supplies its width/height/health/appearance. Successfully delivered UUIDs are
retained as tombstones so retransmission cannot resurrect a killed UE copy.
Limits are128 live mobs and4096 source/spawn IDs per import. Each source UUID is
leased only after its own accepted receipt; rejected/expired spawns leave the source
running. Up to3 transport attempts keep the same UUID and bounded backoff. Stop,
stale control, disconnect or a changed receiver releases all in-memory leases.
Mob copies outside loaded terrain stop simulation until their area is loaded again.

Species defaults are exported before nearby variants consume the128-appearance
budget, with zombie/villager first. Empty cuboid exports are rejected. Empty root
hierarchy nodes remain valid; complete geometry, UV, parent and pose validation
precedes actor creation. `mobReason`, import status and `Bridge mob ...` UE logs
include the palette/type/appearance and failed stage. These remain ground body
models with generic AI; flying/swimming, feature layers and full species AI are
not implied by successful spawning.

### Compact cells and independent source chunk loading

`terrainV2:true` enables horizontal `world_scope.radius:1..12` **8-block cells**
and `halfHeight:1..6`. Four and six Minecraft chunks correspond to radii8 and12,
with81 and169 native horizontal source chunks respectively, including the center.
The largest inclusive scope is25×25×13=8125 cells; `world_commit.cells` now allows
27..8125. After seal, a newer scope follows UE position. New cells are streamed
without replacing already edited source voxels; UE edits remain authoritative.

`world_cell_compact` has the reliable snapshot/cell envelope, `ox/oy/oz` absolute
MC origin, and dictionary-coded native blocks. A palette entry is
`[blockId,stateKey,RGBInteger,opacity,emission]`; a voxel row is
`[localIndex,paletteIndex,skyLight,blockLight]`. Light, opacity and emission are
integers0..15. Local index=`localX + (localZ<<3) + (localY<<6)`, with each coordinate
0..7. The exact absolute owner comes from `cell*8 + local`; its center is relative
to the provided origin. Visual/selection/collision geometry comes from the local
validated native state palette, so repeated physics boxes do not cross the wire.

```json
{"event":"world_cell_compact","cellX":0,"cellY":0,"cellZ":0,"ox":0,"oy":64,"oz":0,"snapshotId":"00000000-0000-4000-8000-000000000004","snapshotSeq":10,"batchIndex":0,"totalBatches":1,"palette":[["minecraft:stone","",16777215,15,0]],"blocks":[[0,0,15,0]]}
```

At most32 dictionary entries and128 rows per packet,512 batches and512 unique
logical voxels per completed cell. Duplicated owners, dangling dictionary indices,
fractional or coerced values and invalid state characters are rejected. The client
splits smaller packets to preserve the2048-byte envelope limit. `skyTop` optionally
adds64 integer light levels to batch0, ordered localX+(localZ<<3), sampled at the
cell's top boundary. Missing source chunks are loading, never an empty snapshot.
An actually empty ready cell still commits one empty batch and its sky boundary.

`SourceTerrainAccess` requests FULL chunks from the dedicated integrated server
at the UE center, independently of native client render distance/player position.
It uses loading-only, non-serialized,43-tick expiring tickets, refreshes active
ones, keeps at most8 generation requests and requests2 new chunks per service job.
No blocking `getChunk`/future join or player teleport is used. An actual FULL,
light-ready WorldChunk is required before copying state/color/light. Cell snapshots
are immutable across threads; the request queue is32 and cache256 cells. Server
copying caps8 cells/4096 reads per job with a4ms work budget; empty sections skip
voxel reads. Biome color sampling uses only already loaded chunks, so a color
lookup cannot synchronously generate an adjacent chunk. Temporary tickets are
removed on stop/world exit/dimension or LAN
change. New terrain can generate in this dedicated local world as normal exploration
would; source blocks, entities and forced-chunk state are not edited.

UE drops faces occluded by native adjacent full faces, reuses a local texture
atlas/material groups, and rebuilds affected cells under a bounded work queue.
Near-player/mob/drop collision regions are maintained separately from distant
visual meshes. This changes drawing/transfer cost; it does not establish actual
Windows FPS without measurement.

### Vanilla lighting environment

Input may add a fully typed `vanillaLight` object:
`skyFactor,blockFactor:0..4`; `ambient,gamma,nightVision,darkness,darkenWorld:0..1`;
`skyColor,ambientColor:0..0xffffff` RGB integers; `hasSky:boolean`. Partial objects
and nonfinite/coerced/out-of-range values are rejected. Native1.21.11 environment
attributes, resource colors, gamma and lightmap/effect controls are sampled read-only.

Lighting OFF now uses separate unlit diffuse materials driven by per-face
brightness, corner AO, block/sky light and native lightmap-like environment values.
UE terrain edits update local light propagation and the changed mesh; native source
light is the starting condition, not a continuous overwrite of UE edits. Dynamic
arms/items/mobs/drops receive surrounding light. The MC sky remains aligned to the
same camera/day/environment. Lighting ON retains the UE lit path and its own
material settings; toggling does not permanently replace the user's lights or level.
Native lightmap timing/color and UE shader output still require real-game comparison.

Dust instances preserve custom-data channels0/1 for UV offsets and add channels2
for sky light,3 for block light and4 for face shade: five floats per instance.
The material explicitly interpolates custom data into the pixel shader. Runtime
lighting refreshes existing dust at20Hz; particle UVs and lighting do not share
channels. Reimporting the0.11 dust material is required for these new channels.

### Item escrow and world drops

`itemDropsV1:true` enables configured vanilla drop-key input, including vanilla
whole-stack modifier behavior. Input adds an inventory session `itemEpoch:UUID`
and `itemSession:boolean`. Chat/GUI pauses keep escrow; full control exit renews
the epoch. An old pending request cannot recreate an item in a closed epoch.
Drops use an auxiliary packet, not the generic event retry queue:

```json
{"v":1,"kind":"item_drop","session":"00000000-0000-4000-8000-000000000001","seq":11,"itemTx":"00000000-0000-4000-8000-000000000005","itemEpoch":"00000000-0000-4000-8000-000000000006","importId":"00000000-0000-4000-8000-000000000003","itemId":"minecraft:diamond_sword","itemModelKey":"minecraft:diamond_sword@aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","itemCount":1,"itemMaxCount":1,"position":{"x":0,"y":1.3,"z":0.5},"velocity":{"x":0,"y":2,"z":6}}
```

Position is relative MC blocks and velocity is MC blocks/sec, per component±30.
Count/maxCount are integers1..99 with count≤native maximum. Model keys match the
item identity. The dedicated integrated-server thread first validates the selected
stack/components/slot and reserves exactly that count in an escrow, then sends the
transaction. The receiver verifies current peer/session/epoch/import/controller,
spawn reach and native GROUND model. No native MC dropped-item entity is created.
Repeated itemTx does not create another drop. UE owns launch/gravity/collision,
spin/bob, nearby pickup, stack merging and lifetime under bounded live-item limits.

Results use `kind:"item_feedback"`, current session/receiver/seq, itemTx,
`itemRevision`, `itemAction` (`spawned`, `pickup`, `refund`, `rejected`), `itemCount`
and bounded `itemReason`. MC applies each transaction revision once on its server
thread and replies with auxiliary `kind:"item_resolve"`, itemTx, itemRevision and
`itemAccepted:0..99`. Resolution can finish after control exit and does not renew
or acquire the gameplay input lease. UE retains/retries unsettled balances until
accepted inventory counts settle them. Full exit refunds remaining balances; a
full inventory retains remainder in the mod's player-NBT escrow for later recovery.
The escrow is stored beside the same inventory save, preventing process restart
from abandoning transient UE drops. This exclusively-owned NBT field is the only
new source save mutation beyond the corresponding inventory count changes.

Item exports add native `ground` display context alongside the four held contexts.
Old hand-only captures remain usable as held items; ground spawn rejects/refunds
those items and asks for0.11 export/import. Component hashes/tints and native
model textures continue to apply. Offhand, feature models and live item animation
limitations from0.10 remain unless their actual exporter context is supported.

### GPU frames, fallback and performance accounting

The optional Windows x64/NVIDIA backend requires MC OpenGL4.3 plus
`WGL_NV_DX_interop2`, UE's D3D11 RHI (`-d3d11`), and the same GPU adapter. The MOD
contains a JNI consumer DLL; UE provides three shared BGRA8 D3D11 textures and
GPU readiness queries. RGB is straight sRGB and native-sky alpha is in the same
shared image. Copies/conversion stay on the GPU; no CPU pixel readback/JPEG is
required for a successful GPU frame. DirectX12 remains on the JPEG fallback.
D3D11 lighting features differ from D3D12/Lumen; use a compatible saved level.

A40-byte handshake `UEB5` plus the current36-character UDP session requests GPU
sharing; `UEB3` and `UEBH` remain JPEG paths. Version5 frames preserve the84-byte
v3/v4 metadata header, require JPEG/mask byte lengths0, and set flag2 (`GPU_FRAME`),
optionally flag1 (`SKY_MASK`). The header appends uint64 shared handle, uint64
DXGI adapter LUID, uint32 slot0..2 and nonzero uint32 generation:108 bytes total.
All wire integer words/float bit patterns remain network-byte-order. No image
payload follows v5; the handle is only for local validated native interop.

After rendering or superseding a frame, MC sends16 TCP bytes: magic `UEBA`,
uint32 frame sequence, slot, generation. UE releases only the exact slot lease.
A producer does not reuse an unacknowledged texture. Latest-frame replacement
releases discarded frames; frames do not accumulate. `UEBF` plus three zero words
requests JPEG fallback when native interop fails without changing UDP session or
inventory epoch. Backend/RHI/adapter failures are surfaced in diagnostics.
`maskCountsAvailable` distinguishes actual CPU mask counts from unavailable GPU
counts. The GPU path does not read back its alpha mask to count foreground or
translucent pixels; zero counts with this flagfalse mean unknown, not an empty
frame.

`video_config.fps` permits1..60 while dimensions remain≤1920×1080; this is a
capture target, not an achieved FPS. `perf_status` separately reports current
receiver/session/input-sequence: `fps`, `frameMs` (averaged UE tick intervals),
`faces`, `renderSections`, `cells`, `streamPending`, `lightPending`, `itemCount`,
`videoTransport`, `gpuReason`, `videoReadyMs`, `videoDropped`. These intervals are
not GPU profiler timings. MC video diagnostics report received/displayed frame
rates and bounded input-to-display latency samples separately from MC HUD FPS.

Acceptance target: RTX5060,1920×1080,4–6 native chunks, displayed UE-video≥30fps
in the documented repeatable scene after streaming/light queues settle. Compare
JPEG and GPU, lighting ON/OFF, and four versus six chunks. Cloud tests can verify
Java/native cross-compilation, schemas and portable math; they cannot certify
UE5.8 compilation, WGL/driver interoperability, final rendering or target FPS.
