#!/usr/bin/env python3
"""Read-only MQTT 3.1.1 subscriber, no dependencies: never publishes.

Prints every message received on the filters for SECONDS, as
"<epoch> <topic> <payload>[ [R]]" (same format as the AcquaThermoNet
minibroker.py log). The password is read from the environment
(MQTT_PASSWORD), never from the command line.

Usage: mqttsub.py HOST PORT USER SECONDS FILTER [FILTER...]
"""
import os, socket, struct, sys, time

def mstr(s):
    s = s.encode()
    return struct.pack('!H', len(s)) + s

def enc_len(n):
    out = b''
    while True:
        b = n % 128; n //= 128
        out += bytes([b | (0x80 if n else 0)])
        if not n: return out

def pkt(t, body):
    return bytes([t]) + enc_len(len(body)) + body

def read_pkt(s):
    h = s.recv(1)
    if not h: return None, None
    m, n = 1, 0
    while True:
        b = s.recv(1)[0]
        n += (b & 127) * m; m *= 128
        if not b & 128: break
    body = b''
    while len(body) < n:
        chunk = s.recv(n - len(body))
        if not chunk: return None, None
        body += chunk
    return h[0], body

def main():
    if len(sys.argv) < 6:
        sys.exit(__doc__)
    host, port, user, secs = sys.argv[1], int(sys.argv[2]), sys.argv[3], float(sys.argv[4])
    filters = sys.argv[5:]
    password = os.environ.get('MQTT_PASSWORD', '')

    s = socket.create_connection((host, port), timeout=5)
    flags = 0x02 | (0x80 if user else 0) | (0x40 if user and password else 0)
    body = mstr('MQTT') + bytes([4, flags]) + struct.pack('!H', 60) + mstr('mqttsub-%d' % os.getpid())
    if user: body += mstr(user)
    if user and password: body += mstr(password)
    s.sendall(pkt(0x10, body))
    t, b = read_pkt(s)
    if t is None or t >> 4 != 2 or b[1] != 0:
        sys.exit('connection refused (CONNACK %s)' % (b[1] if b else 'none'))
    s.sendall(pkt(0x82, struct.pack('!H', 1) + b''.join(mstr(f) + b'\x00' for f in filters)))

    end = time.time() + secs
    s.settimeout(0.5)
    while time.time() < end:
        try:
            t, b = read_pkt(s)
        except socket.timeout:
            continue
        if t is None:
            break
        if t >> 4 == 3:
            n = struct.unpack('!H', b[:2])[0]
            topic = b[2:2 + n].decode(errors='replace')
            qos = (t >> 1) & 3
            payload = b[2 + n + (2 if qos else 0):].decode(errors='replace')
            print('%.3f %s %s%s' % (time.time(), topic, payload, ' [R]' if t & 1 else ''), flush=True)
    s.sendall(b'\xe0\x00')
    s.close()

if __name__ == '__main__':
    main()
