"""Stand-in backend: real auto_mapping in a native process. Each message is a
pen sample plus the current stroke; the reply is the mapped stroke, the shape of
a display-list update. Length-prefixed JSON over loopback TCP."""
import json
import math
import socket
import struct
import sys
import types

sys.dont_write_bytecode = True
sys.path.insert(0, r"C:\Users\admin\Documents\AnimeAn\pyfile")
sys.modules.setdefault("animean_python", types.ModuleType("animean_python"))
import io, contextlib
with contextlib.redirect_stdout(io.StringIO()):
    import auto_mapping as am


def guide(amp, axis, n=101, half=200.0, period=300.0):
    out = []
    for k in range(n):
        s = -half + 2.0 * half * k / (n - 1)
        bow = amp * math.sin(2.0 * math.pi * s / period)
        out.append((s, bow) if axis == "h" else (bow, s))
    return out


with contextlib.redirect_stdout(io.StringIO()):
    MP, _ = am.build_mapper(guide(30.0, "h"), guide(30.0, "v"), guide(60.0, "h"), guide(45.0, "v"), {})


def recv_exact(conn, n):
    data = bytearray()
    while len(data) < n:
        chunk = conn.recv(n - len(data))
        if not chunk:
            return None
        data.extend(chunk)
    return bytes(data)


listener = socket.create_server(("127.0.0.1", int(sys.argv[1])))
print("ready", flush=True)
while True:
    conn, _ = listener.accept()
    conn.setsockopt(socket.IPPROTO_TCP, socket.TCP_NODELAY, 1)
    while True:
        head = recv_exact(conn, 4)
        if head is None:
            break
        body = recv_exact(conn, struct.unpack(">I", head)[0])
        message = json.loads(body)
        mapped = [MP(tuple(p)) for p in message["points"]] if message.get("map") else message["points"]
        reply = json.dumps({"seq": message["seq"], "points": mapped}).encode("utf8")
        conn.sendall(struct.pack(">I", len(reply)) + reply)
    conn.close()
