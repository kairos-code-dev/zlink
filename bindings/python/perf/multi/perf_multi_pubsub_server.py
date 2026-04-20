import sys
import threading

import zlink

from perf_multi_common import (
    TOPIC,
    apply_multi_socket_options,
    benchmark_endpoint,
    benchmark_run_id,
    new_payload,
    parse_server_args,
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
            while not start_event.is_set() and not stop_event.is_set():
                stop_event.wait(0.01)
            while not stop_event.is_set():
                publisher.publish(
                    TOPIC,
                    stamp_payload(payload, phase=1, run_id=run_id),
                )


if __name__ == "__main__":
    main()
