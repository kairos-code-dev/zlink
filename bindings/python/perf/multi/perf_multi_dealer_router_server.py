import sys
import threading

import zlink

from perf_multi_common import parse_server_args, safe_poll, tcp_endpoint


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
            with zlink.Poller() as poller:
                poller.add_socket(router, zlink.PollEvent.POLLIN)
                while not stop.is_set():
                    events = safe_poll(poller, 100)
                    if not events:
                        continue
                    while True:
                        received = router.try_recv()
                        if received is None:
                            break
                        with received:
                            router.send(
                                received.to_bytes_list()[0],
                                routing_id=received.routing_id,
                            )


if __name__ == "__main__":
    main()
