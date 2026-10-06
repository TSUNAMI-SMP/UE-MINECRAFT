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
