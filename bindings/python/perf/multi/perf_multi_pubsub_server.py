import os
import sys
import threading
import time

import zlink

from perf_multi_common import (
    TOPIC,
    apply_multi_socket_options,
    benchmark_endpoint,
    benchmark_run_id,
    new_payload,
    parse_server_args,
    publish_nonblocking,
    stamp_payload,
)


def _stdin_stop(stop_event):
    for line in sys.stdin:
        if line.strip().upper() in {"STOP", "QUIT"}:
            stop_event.set()
            return


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    run_id = benchmark_run_id()
    stop_event = threading.Event()
    start_event = threading.Event()
    endpoint = benchmark_endpoint(args.transport, "multi-pubsub")
    payload = new_payload(args.msg_size)
    active_duration_s = max(
        1.0,
        float(os.environ.get("PERF_MULTI_DURATION_SECONDS", "5")),
    )

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
        with zlink.PubSocket(ctx) as publisher:
            apply_multi_socket_options(publisher)
            publisher.bind(endpoint)
            print(f"READY,{endpoint}", flush=True)
            active_deadline = None
            while not stop_event.is_set():
                if not start_event.is_set():
                    stop_event.wait(0.01)
                    continue
                if active_deadline is None:
                    active_deadline = time.perf_counter() + active_duration_s
                if time.perf_counter() >= active_deadline:
                    stop_event.wait(0.01)
                    continue
                sent_any = False
                while time.perf_counter() < active_deadline and not stop_event.is_set():
                    sent = publish_nonblocking(
                        publisher,
                        TOPIC,
                        stamp_payload(payload, phase=1, run_id=run_id),
                    )
                    if not sent:
                        break
                    sent_any = True
                if not sent_any:
                    stop_event.wait(0.001)


if __name__ == "__main__":
    main()
