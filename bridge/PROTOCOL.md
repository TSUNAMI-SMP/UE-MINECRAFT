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

UE mapping: `(X,Y,Z) = spawnFeet + 100 * (mcZ,-mcX,mcY)`, yaw `mcYaw`, pitch
`-mcPitch`. Character capsule center adds its half height above feet. First-person
camera is 162 cm above feet. Movement authority stays in Minecraft: position is
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
