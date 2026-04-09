import argparse
import os
import sys
import threading

import zlink

from perf_multi_common import tcp_endpoint


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--clients", type=int, default=4)
    parser.add_argument("--msg-size", type=int, default=256)
    args = parser.parse_args(argv or sys.argv[1:])

    endpoints = [tcp_endpoint() for _ in range(args.clients)]
    with zlink.Context() as ctx:
        sockets = [zlink.DealerSocket(ctx) for _ in range(args.clients)]
        try:
            for sock, endpoint in zip(sockets, endpoints):
                sock.bind(endpoint)

            def make_worker(sock):
                def worker():
                    while True:
                        with sock.recv() as received:
                            sock.send(received.to_bytes_list()[0])

                return worker

            for sock in sockets:
                threading.Thread(target=make_worker(sock), daemon=True).start()

            print(f"READY,{';'.join(endpoints)}", flush=True)
            sys.stdin.readline()
            sys.stdout.flush()
            os._exit(0)
        finally:
            for sock in sockets:
                try:
                    sock.close()
                except Exception:
                    pass


if __name__ == "__main__":
    main()
