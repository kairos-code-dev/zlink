import argparse
import os
import sys
import threading
import time

import zlink

from perf_multi_common import TOPIC, new_payload, stamp_payload, tcp_endpoint


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--clients", type=int, default=4)
    parser.add_argument("--msg-size", type=int, default=256)
    args = parser.parse_args(argv or sys.argv[1:])

    endpoint = tcp_endpoint()
    stop = threading.Event()
    payload = new_payload(args.msg_size)

    def wait_stop():
        sys.stdin.readline()
        stop.set()

    threading.Thread(target=wait_stop, daemon=True).start()

    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as node:
            node.set_routing_id(b"SPOT-SERVER")
            node.bind(endpoint)
            with node.wrap_handle() as spot:
                print(f"READY,{endpoint}", flush=True)
                while not stop.is_set():
                    spot.publish(TOPIC, stamp_payload(payload, phase=1))
                    time.sleep(0.001)
                sys.stdout.flush()
                os._exit(0)


if __name__ == "__main__":
    main()
