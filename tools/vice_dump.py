#!/usr/bin/env python3
"""
vice_dump.py - drive VICE's binary monitor to snapshot a running program.

VICE is launched separately with:
    x64sc -warp -binarymonitor -binarymonitoraddress ip4://127.0.0.1:6502 \
          -autostart <disk.d64>

This client connects, waits for the program to load (a fixed warp-time
delay, or until a watchpoint at a known game address fires), then dumps
the full 64K CPU memory to a file and exits VICE.

Binary monitor protocol: VICE "Binary monitor" (STX 0x02 framing).
See docs/vice/Monitor.txt in the VICE distribution.
"""

import socket
import struct
import sys
import time

STX = 0x02
API = 0x02

CMD_MEM_GET = 0x01
CMD_PING = 0x81
CMD_EXIT = 0xaa
CMD_QUIT = 0xbb
CMD_RESET = 0xcc

def send(sock, cmd, body=b"", req=1):
    hdr = struct.pack("<BBII", STX, API, len(body), req) + bytes([cmd])
    sock.sendall(hdr + body)

def recv_exact(sock, n):
    buf = b""
    while len(buf) < n:
        chunk = sock.recv(n - len(buf))
        if not chunk:
            raise EOFError("monitor closed")
        buf += chunk
    return buf

def recv_response(sock):
    # response header: STX, api, len(4), resp_type(1), err(1), req(4)
    hdr = recv_exact(sock, 12)
    stx, api, blen, rtype, err = struct.unpack("<BBIBB", hdr[:8])
    req = struct.unpack("<I", hdr[8:12])[0]
    body = recv_exact(sock, blen)
    return rtype, err, req, body

REQ_ID = 0x11223344

def mem_get(sock, start, end, bank=0):
    # sidefx(1) start(2) end(2) memspace(1) bankid(2)
    body = struct.pack("<BHHBH", 0, start, end, 0, bank)
    send(sock, CMD_MEM_GET, body, REQ_ID)
    # VICE interleaves async events (0x31, 0x62, ...) before the reply;
    # match on response type AND our request id.
    for _ in range(64):
        rtype, err, req, rbody = recv_response(sock)
        if rtype == CMD_MEM_GET and req == REQ_ID:
            n = struct.unpack("<H", rbody[:2])[0]
            return rbody[2:2 + n]
    raise RuntimeError("no MEM_GET reply")

def main():
    if len(sys.argv) < 2:
        print("usage: vice_dump.py out.bin [host] [port] [wait_s]")
        return 1
    out = sys.argv[1]
    host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 6502
    wait_s = float(sys.argv[4]) if len(sys.argv) > 4 else 20.0

    # connect (retry while VICE starts)
    sock = None
    for _ in range(60):
        try:
            sock = socket.create_connection((host, port), timeout=2)
            break
        except OSError:
            time.sleep(0.5)
    if not sock:
        print("could not connect to VICE binary monitor")
        return 1
    print("connected; warping for %.0fs to let the game load..." % wait_s)
    time.sleep(wait_s)

    # A single MEM_GET of 0000-ffff is 65536 bytes and overflows VICE's
    # 16-bit length field; fetch in two halves.
    data = mem_get(sock, 0x0000, 0x7fff) + mem_get(sock, 0x8000, 0xffff)
    with open(out, "wb") as f:
        f.write(data)
    print("dumped %d bytes to %s" % (len(data), out))

    send(sock, CMD_EXIT)
    time.sleep(0.2)
    send(sock, CMD_QUIT)
    sock.close()
    return 0

if __name__ == "__main__":
    sys.exit(main())
