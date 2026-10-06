#!/usr/bin/env python3
"""Receive real MOD traffic, or exercise a running UE receiver over loopback."""
import argparse
import json
import socket
import time
import uuid

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("mode", choices=["listen", "send"])
    parser.add_argument("--port", type=int, default=7779)
    parser.add_argument("--seconds", type=float, default=10)
    parser.add_argument("--tnt", action="store_true", help="Send a test explosion 3 blocks ahead of spawn")
    args = parser.parse_args()
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        if args.mode == "listen":
            sock.bind(("127.0.0.1", args.port)); sock.settimeout(0.25)
            count = events = 0; last = None; seen = set()
            until = time.monotonic() + args.seconds
            while time.monotonic() < until:
                try: data, source = sock.recvfrom(2049)
                except socket.timeout: continue
                try:
                    p = json.loads(data)
                    if len(data) > 2048 or p.get("v") != 1: continue
                    if p.get("kind") == "input": count += 1; last = p
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
            if args.tnt:
                event = {"v":1,"kind":"event","session":session,"seq":seq+1,"eventId":str(uuid.uuid4()),
                         "event":"tnt_ignite","x":0.5,"y":0.5,"z":3.5}
                for _ in range(15):
                    sock.send(json.dumps(event).encode())
                    try:
                        ack = json.loads(sock.recv(2048))
                        if ack.get("session")==session and ack.get("eventId")==event["eventId"] and ack.get("kind")=="ack":
                            print("UE event ACK received. Verify Niagara and wall visually."); return
                    except (socket.timeout, ValueError): pass
                raise SystemExit("No matching UE ACK")
            print("Input sent. Verify camera visually; no input ACK exists.")

if __name__ == "__main__": main()
