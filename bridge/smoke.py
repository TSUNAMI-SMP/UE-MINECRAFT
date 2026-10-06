#!/usr/bin/env python3
"""Receive real MOD traffic, or exercise a running UE receiver over loopback."""
import argparse
import json
import socket
import time
import uuid
import math

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["listen", "send"])
    parser.add_argument("--port", type=int, default=7779)
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--tnt", action="store_true", help="Send a test explosion 3 blocks ahead of spawn")
    parser.add_argument("--bow", action="store_true", help="Send an optional UE bow prototype event")
    parser.add_argument("--preview", action="store_true", help="Send a small visual-only floor snapshot")
    args = parser.parse_args()
    if not 1024 <= args.port <= 65535 or not math.isfinite(args.seconds) or not 0 < args.seconds <= 3600:
        parser.error("port must be 1024..65535 and seconds must be 0..3600 (exclusive of 0)")
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        if args.mode == "listen":
            sock.bind(("127.0.0.1", args.port)); sock.settimeout(0.25)
            count = events = 0; last = None; seen = set(); last_status = 0
            until = time.monotonic() + args.seconds
            while time.monotonic() < until:
                try: data, source = sock.recvfrom(2049)
                except socket.timeout: continue
                try:
                    p = json.loads(data)
                    if len(data) > 2048 or p.get("v") != 1: continue
                    if p.get("kind") == "input":
                        count += 1; last = p
                        status = {"v":1,"kind":"status","receiver":"diagnostic","session":p["session"],
                                  "seq":p["seq"],"cameraReady":False,"vfxReady":False,"walls":0,"previewBlocks":0}
                        if time.monotonic() - last_status >= 0.25:
                            sock.sendto(json.dumps(status).encode(), source); last_status = time.monotonic()
                    elif p.get("kind") == "event":
                        key = (p["session"], p["eventId"])
                        if key not in seen:
                            seen.add(key); events += 1; print("event:", p, flush=True)
                        ack = {"v":1,"kind":"ack","session":p["session"],"eventId":p["eventId"]}
                        sock.sendto(json.dumps(ack).encode(), source)
                except (ValueError, KeyError): continue
            print(f"input packets={count}, unique events={events}, last input={last}")
            if not count: raise SystemExit("No input received; this is not a successful MOD test")
        else:
            sock.connect(("127.0.0.1", args.port)); sock.settimeout(0.15)
            session = str(uuid.uuid4()); seq = 0; start = time.monotonic()
            while time.monotonic() - start < args.seconds:
                seq += 1
                p = {"v":1,"kind":"input","session":session,"seq":seq,"x":0,"y":0,"z":0,
                     "yaw":(time.monotonic()-start)*30,"pitch":0,"forward":0,"right":0,"jump":False}
                sock.send(json.dumps(p).encode()); time.sleep(1/120)
            events = []
            if args.tnt: events.append({"event":"tnt_ignite","x":0.5,"y":0.5,"z":3.5})
            if args.bow: events.append({"event":"bow_fire","x":0,"y":1.62,"z":0,"dx":0,"dy":0,"dz":1,"pull":1})
            if args.preview:
                events.append({"event":"block_snapshot","x":0,"y":0,"z":0,"snapshotId":str(uuid.uuid4()),
                               "snapshotSeq":seq+len(events)+1,"batchIndex":0,"totalBatches":1,
                               "blocks":[[x+0.5,-0.5,z+0.5,0x659642] for x in range(3) for z in range(3)]})
            for event in events:
                seq += 1; event.update({"v":1,"kind":"event","session":session,"seq":seq,"eventId":str(uuid.uuid4())})
                acknowledged = False
                for _ in range(15):
                    sock.send(json.dumps(event).encode())
                    until = time.monotonic() + 0.15
                    while time.monotonic() < until:
                        sock.settimeout(max(0.001, until-time.monotonic()))
                        try:
                            ack = json.loads(sock.recv(2048))
                            if ack.get("v")==1 and ack.get("session")==session and ack.get("eventId")==event["eventId"] and ack.get("kind")=="ack":
                                print("Receiver event ACK received:", event["event"], ". Verify UE behavior visually."); acknowledged = True; break
                        except (socket.timeout, ValueError): pass
                    if acknowledged: break
                if not acknowledged: raise SystemExit("No matching receiver ACK for "+event["event"])
            if events: return
            print("Input sent. Verify camera visually; no input ACK exists.")

if __name__ == "__main__": main()
