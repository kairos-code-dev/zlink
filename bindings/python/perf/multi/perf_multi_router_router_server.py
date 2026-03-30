import argparse
import os
import sys
import threading
import time

import zlink

from perf_multi_common import tcp_endpoint


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--clients", type=int, default=4)
    parser.add_argument("--recv", default="recv")
    parser.add_argument("--msg-size", type=int, default=256)
    parser.parse_args(argv or sys.argv[1:])

    endpoint = tcp_endpoint()
    stop = threading.Event()

    def wait_stop():
        sys.stdin.readline()
        stop.set()

    threading.Thread(target=wait_stop, daemon=True).start()

    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            router.set_routing_id(b"SERVER")
            router.bind(endpoint)
            print(f"READY,{endpoint}", flush=True)
            while not stop.is_set():
                received = router.try_recv()
                if received is None:
                    time.sleep(0.0005)
                    continue
                with received:
                    router.send_to(received.routing_id, received.to_bytes_list()[0])
            sys.stdout.flush()
            os._exit(0)


if __name__ == "__main__":
    main()
