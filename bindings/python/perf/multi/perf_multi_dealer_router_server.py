import sys
import threading

import zlink

from perf_multi_common import parse_server_args, tcp_endpoint


def main(argv=None):
    parse_server_args(argv or sys.argv[1:])
    endpoint = tcp_endpoint()
    stop = threading.Event()

    def wait_stop():
        sys.stdin.readline()
        stop.set()

    threading.Thread(target=wait_stop, daemon=True).start()

    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            router.bind(endpoint)
            print(f"READY,{endpoint}", flush=True)

            def worker():
                while not stop.is_set():
                    try:
                        with router.recv() as received:
                            router.send(received.routing_id, received.to_bytes_list()[0])
                    except zlink.RecvError as exc:
                        if stop.is_set() or exc.result == zlink.RecvResult.TERMINATED or exc.internal_errno == 4:
                            return
                        raise

            threading.Thread(target=worker, daemon=True).start()
            while not stop.is_set():
                stop.wait(0.1)


if __name__ == "__main__":
    main()
