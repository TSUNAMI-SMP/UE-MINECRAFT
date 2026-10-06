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

UE mapping: `(X,Y,Z) = spawnFeet + 100 * (mcZ,mcX,mcY)`, yaw `-mcYaw`, pitch
`-mcPitch`. Character capsule center adds its half height above feet. First-person
camera is 162 cm above feet. Movement authority stays in Minecraft: position is
mirrored directly; UE does not simulate a second player movement controller or
call Jump twice. UE wall debris is independently simulated in Chaos.

Event, only on ignition of a TNT block with flint and steel or fire charge:

```json
{"v":1,"kind":"event","session":"00000000-0000-4000-8000-000000000001","seq":2,"eventId":"00000000-0000-4000-8000-000000000002","event":"tnt_ignite","x":0.5,"y":0.5,"z":3.5}
```

MVP explodes immediately in UE at TNT's center, not after Minecraft's fuse.
Client confirms the clicked TNT disappears within 10 ticks. Intended for a new
local singleplayer test world; not authoritative server confirmation. Redstone,
chain explosions, placing TNT, bow ignition, and protected multiplayer worlds
are outside this MVP.

UE ACK:

```json
{"v":1,"kind":"ack","session":"00000000-0000-4000-8000-000000000001","eventId":"00000000-0000-4000-8000-000000000002"}
```

MC retries same eventId every 100 ms for 2 seconds, up to 64 pending events. UE
deduplicates IDs for 10 seconds, at most 256 IDs, and ACKs duplicates. This bounds
memory and retry traffic; it is not persistent delivery across UE restarts. Restart
UE Play and reconnect the MC world together when testing. ACK indicates receiver
handled the event, not proof that an assigned Niagara asset rendered or wall broke.

Future types (`bow_fire`, `block_place`, `block_break`, `mob_state`, `hp_state`)
should add versioned payloads/handlers behind this transport. Unknown types are
ignored. No Minecraft or Fabric types in `BridgeTransport`; no rendering code in
the sender.
