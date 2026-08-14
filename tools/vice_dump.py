#!/usr/bin/env python3
"""
vice_dump.py - drive VICE's binary monitor to snapshot a running program.

VICE is launched separately with:
    x64sc -warp -binarymonitor -binarymonitoraddress ip4://127.0.0.1:6502 \
          -autostart <disk.d64>

This client connects, waits a fixed warp-time delay for the program to
load, then captures everything the hybrid runtime needs to resume the
machine:

    <out>          64K CPU-view memory (I/O registers visible at $DXXX)
    <out>.ram      64K raw RAM (the bytes *under* the I/O window)
    <out>.regs     text file: PC/A/X/Y/SP/FLAGS lines "NAME VALUE"

Binary monitor protocol: VICE "Binary monitor" (STX 0x02 framing); async
responses (e.g. register events, type 0x31 with request id 0xffffffff)
are interleaved, so replies are matched on our request id.
"""

import socket
import struct
import sys
import time

STX = 0x02
API = 0x02

CMD_MEM_GET = 0x01
CMD_KEYBOARD_FEED = 0x72
CMD_REGISTERS_GET = 0x31
CMD_BANKS_AVAILABLE = 0x82
CMD_REGISTERS_AVAILABLE = 0x83
CMD_EXIT = 0xaa
CMD_QUIT = 0xbb

REQ_ID = 0x11223344


def send(sock, cmd, body=b"", req=REQ_ID):
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


def wait_reply(sock, cmd):
    for _ in range(256):
        rtype, err, req, body = recv_response(sock)
        if rtype == cmd and req == REQ_ID:
            if err:
                raise RuntimeError("cmd 0x%02x error %d" % (cmd, err))
            return body
    raise RuntimeError("no reply for cmd 0x%02x" % cmd)


def mem_get(sock, start, end, bank=0):
    # sidefx(1) start(2) end(2) memspace(1) bankid(2)
    body = struct.pack("<BHHBH", 0, start, end, 0, bank)
    send(sock, CMD_MEM_GET, body)
    rbody = wait_reply(sock, CMD_MEM_GET)
    n = struct.unpack("<H", rbody[:2])[0]
    return rbody[2:2 + n]


def mem_get_64k(sock, bank=0):
    # A single 0000-ffff request overflows the 16-bit length field.
    return mem_get(sock, 0x0000, 0x7fff, bank) + \
        mem_get(sock, 0x8000, 0xffff, bank)


def banks_available(sock):
    send(sock, CMD_BANKS_AVAILABLE)
    body = wait_reply(sock, CMD_BANKS_AVAILABLE)
    count = struct.unpack("<H", body[:2])[0]
    off = 2
    banks = {}
    for _ in range(count):
        isize = body[off]
        bankid = struct.unpack("<H", body[off + 1:off + 3])[0]
        namelen = body[off + 3]
        name = body[off + 4:off + 4 + namelen].decode("ascii", "replace")
        banks[name] = bankid
        off += 1 + isize
    return banks


def registers(sock):
    # names
    send(sock, CMD_REGISTERS_AVAILABLE, struct.pack("<B", 0))
    body = wait_reply(sock, CMD_REGISTERS_AVAILABLE)
    count = struct.unpack("<H", body[:2])[0]
    off = 2
    names = {}
    for _ in range(count):
        isize = body[off]
        regid = body[off + 1]
        # bits(1), namelen(1), name
        namelen = body[off + 3]
        name = body[off + 4:off + 4 + namelen].decode("ascii", "replace")
        names[regid] = name
        off += 1 + isize
    # values
    send(sock, CMD_REGISTERS_GET, struct.pack("<B", 0))
    body = wait_reply(sock, CMD_REGISTERS_GET)
    count = struct.unpack("<H", body[:2])[0]
    off = 2
    vals = {}
    for _ in range(count):
        isize = body[off]
        regid = body[off + 1]
        val = struct.unpack("<H", body[off + 2:off + 4])[0]
        vals[names.get(regid, "R%d" % regid)] = val
        off += 1 + isize
    return vals


def keyboard_feed(sock, text):
    body = bytes([len(text)]) + text.encode("ascii")
    send(sock, CMD_KEYBOARD_FEED, body)
    wait_reply(sock, CMD_KEYBOARD_FEED)


def main():
    if len(sys.argv) < 2:
        print("usage: vice_dump.py out.bin [host] [port] [wait_s] "
              "[script]")
        print("  script: alternating waits/keys, e.g. '12:a:6:b:30'")
        print("  (waits are seconds; keys are fed as PETSCII text)")
        return 1
    out = sys.argv[1]
    host = sys.argv[2] if len(sys.argv) > 2 else "127.0.0.1"
    port = int(sys.argv[3]) if len(sys.argv) > 3 else 6502
    wait_s = float(sys.argv[4]) if len(sys.argv) > 4 else 20.0
    script = sys.argv[5] if len(sys.argv) > 5 else None

    sock = None
    for _ in range(60):
        try:
            sock = socket.create_connection((host, port), timeout=5)
            break
        except OSError:
            time.sleep(0.5)
    if not sock:
        print("could not connect to VICE binary monitor")
        return 1
    print("connected; warping for %.0fs to let the game load..." % wait_s)
    time.sleep(wait_s)

    if script:
        parts = script.split(":")
        for i, p in enumerate(parts):
            if i % 2 == 0:
                print("  wait %ss" % p)
                time.sleep(float(p))
            else:
                print("  feed %r" % p)
                keyboard_feed(sock, p)

    banks = banks_available(sock)
    print("banks: %s" % banks)

    cpu_view = mem_get_64k(sock, banks.get("cpu", 0))
    with open(out, "wb") as f:
        f.write(cpu_view)
    print("wrote %s (%d bytes, cpu view)" % (out, len(cpu_view)))

    if "ram" in banks:
        raw = mem_get_64k(sock, banks["ram"])
        with open(out + ".ram", "wb") as f:
            f.write(raw)
        print("wrote %s.ram (%d bytes, raw ram)" % (out, len(raw)))

    regs = registers(sock)
    with open(out + ".regs", "w") as f:
        for k in sorted(regs):
            f.write("%s %04X\n" % (k, regs[k]))
    print("registers: %s" %
          " ".join("%s=%04X" % (k, v) for k, v in sorted(regs.items())))

    send(sock, CMD_EXIT)
    time.sleep(0.2)
    send(sock, CMD_QUIT)
    sock.close()
    return 0


if __name__ == "__main__":
    sys.exit(main())
