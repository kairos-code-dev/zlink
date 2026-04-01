import sys
import threading

import zlink

from perf_multi_common import TOPIC, new_payload, parse_server_args, stamp_payload, tcp_endpoint


def _stdin_stop(stop_event):
    for line in sys.stdin:
        if line.strip().upper() in {"STOP", "QUIT"}:
            stop_event.set()
            return


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    stop_event = threading.Event()
    payload = new_payload(args.msg_size)
    endpoint = tcp_endpoint()
    threading.Thread(target=_stdin_stop, args=(stop_event,), daemon=True).start()

    with zlink.Context() as ctx:
        with zlink.PubSocket(ctx) as publisher:
            publisher.options.linger_ms = 0
            publisher.bind(endpoint)
            print(f"READY,{endpoint}", flush=True)
            while not stop_event.is_set():
                publisher.publish(TOPIC, stamp_payload(payload))


if __name__ == "__main__":
    main()
