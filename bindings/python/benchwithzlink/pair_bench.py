#!/usr/bin/env python3
import os
import socket
import sys
import time

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), "../../.."))
sys.path.insert(0, os.path.join(ROOT, "bindings/python/src"))
import zlink  # noqa: E402

ZLINK_PAIR = 0
ZLINK_DONTWAIT = 1


def get_port():
    s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    s.bind(("127.0.0.1", 0))
    port = s.getsockname()[1]
    s.close()
    return port


def endpoint_for(transport: str):
    if transport == "inproc":
        return f"inproc://bench-pair-{int(time.time() * 1000)}"
    return f"{transport}://127.0.0.1:{get_port()}"


def resolve_msg_count(size: int) -> int:
    env = os.environ.get("BENCH_MSG_COUNT")
    if env and env.isdigit() and int(env) > 0:
        return int(env)
    return 200000 if size <= 1024 else 20000


def main() -> int:
    if len(sys.argv) < 4:
        return 1
    pattern = sys.argv[1].upper()
    transport = sys.argv[2]
    size = int(sys.argv[3])
    if pattern != "PAIR":
        return 0

    warmup = int(os.environ.get("BENCH_WARMUP_COUNT", "1000"))
    lat_count = int(os.environ.get("BENCH_LAT_COUNT", "500"))
    msg_count = resolve_msg_count(size)

    ctx = zlink.Context()
    a = zlink.Socket(ctx, ZLINK_PAIR)
    b = zlink.Socket(ctx, ZLINK_PAIR)
    ep = endpoint_for(transport)

    try:
        a.bind(ep)
        b.connect(ep)
        time.sleep(0.05)
        buf = b"a" * size

        for _ in range(warmup):
            b.send(buf, 0)
            a.recv(size, 0)

        t0 = time.perf_counter()
        for _ in range(lat_count):
            b.send(buf, 0)
            x = a.recv(size, 0)
            a.send(x, 0)
            b.recv(size, 0)
        lat_us = ((time.perf_counter() - t0) * 1_000_000.0) / (lat_count * 2)

        t1 = time.perf_counter()
        for _ in range(msg_count):
            b.send(buf, 0)
        for _ in range(msg_count):
            a.recv(size, 0)
        thr = msg_count / (time.perf_counter() - t1)

        print(f"RESULT,current,PAIR,{transport},{size},throughput,{thr}")
        print(f"RESULT,current,PAIR,{transport},{size},latency,{lat_us}")
        return 0
    except Exception:
        return 2
    finally:
        try:
            a.close()
            b.close()
            ctx.close()
        except Exception:
            pass


if __name__ == "__main__":
    raise SystemExit(main())
