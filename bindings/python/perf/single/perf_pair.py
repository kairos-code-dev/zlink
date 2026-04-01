import sys
import time

import zlink

from perf_common import (
    latency_us_from_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    result_metrics,
    safe_poll,
    stamp_payload,
    tcp_endpoint,
    wait_connected_pair,
)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="pair")
    payload = new_payload(args.msg_size)
    latencies = []
    count = 0

    with zlink.Context() as ctx:
        with zlink.PairSocket(ctx) as server:
            with zlink.PairSocket(ctx) as client:
                endpoint = tcp_endpoint("pair")
                server.bind(endpoint)
                client.connect(endpoint)
                wait_connected_pair(server, client)

                with zlink.Poller() as poller:
                    poller.add_socket(server, zlink.PollEvent.POLLIN)

                    warmup_deadline = time.perf_counter() + args.warmup
                    while time.perf_counter() < warmup_deadline:
                        client.send(stamp_payload(payload))
                        for event in safe_poll(poller, 0):
                            while True:
                                received = event["socket"].try_recv()
                                if received is None:
                                    break
                                with received:
                                    pass

                    started = time.perf_counter()
                    deadline = started + args.duration
                    while time.perf_counter() < deadline:
                        client.send(stamp_payload(payload))
                        for event in safe_poll(poller, 0):
                            while True:
                                received = event["socket"].try_recv()
                                if received is None:
                                    break
                                with received:
                                    latencies.append(
                                        latency_us_from_message(received.to_bytes_list()[0])
                                    )
                                    count += 1

                if count == 0:
                    raise RuntimeError("pair benchmark did not receive any message")
                elapsed = time.perf_counter() - started
                metrics = result_metrics(
                    count=count,
                    msg_size=args.msg_size,
                    elapsed_s=elapsed,
                    latencies_us=latencies,
                )
                print_result_lines("PAIR", "tcp", args.msg_size, metrics)


if __name__ == "__main__":
    main()
