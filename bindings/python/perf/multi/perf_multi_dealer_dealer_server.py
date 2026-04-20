import os
import sys
import threading
import time

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    new_payload,
    parse_server_args,
    safe_poll,
    send_nonblocking,
    stamp_payload,
    tcp_endpoint,
)


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])

    endpoints = [tcp_endpoint() for _ in range(args.clients)]
    start_event = threading.Event()
    stop_event = threading.Event()
    payload = new_payload(args.msg_size)

    def read_commands():
        for line in sys.stdin:
            text = line.strip().upper()
            if text.startswith("START,"):
                start_event.set()
            elif text in {"STOP", "QUIT"}:
                stop_event.set()
                return

    threading.Thread(target=read_commands, daemon=True).start()

    with zlink.Context() as ctx:
        sockets = [zlink.DealerSocket(ctx) for _ in range(args.clients)]
        try:
            for sock, endpoint in zip(sockets, endpoints):
                apply_multi_socket_options(sock)
                sock.bind(endpoint)

            print(f"READY,{';'.join(endpoints)}", flush=True)
            while not start_event.is_set() and not stop_event.is_set():
                time.sleep(0.01)
            if stop_event.is_set():
                sys.stdout.flush()
                os._exit(0)

            while not stop_event.is_set():
                made_progress = False
                for current_sock in sockets:
                    while not stop_event.is_set():
                        if not send_nonblocking(
                            current_sock,
                            stamp_payload(payload, phase=1),
                        ):
                            break
                        made_progress = True
                if not made_progress:
                    time.sleep(0.001)
        finally:
            for sock in sockets:
                try:
                    sock.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
