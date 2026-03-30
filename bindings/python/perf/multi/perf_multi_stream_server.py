import argparse
import os
import sys

import zlink

from perf_multi_common import tcp_endpoint


def main(argv=None):
    parser = argparse.ArgumentParser()
    parser.add_argument("--clients", type=int, default=4)
    parser.add_argument("--recv", default="recv")
    parser.add_argument("--msg-size", type=int, default=256)
    parser.parse_args(argv or sys.argv[1:])

    endpoint = tcp_endpoint()
    with zlink.Context() as ctx:
        with zlink.StreamSocket(ctx) as server:
            def on_message(received):
                server.send_to(received.routing_id, received.to_bytes_list()[0])

            server.on_receive(on_message)
            server.bind(endpoint)
            print(f"READY,{endpoint}", flush=True)
            sys.stdin.readline()
            sys.stdout.flush()
            os._exit(0)


if __name__ == "__main__":
    main()
