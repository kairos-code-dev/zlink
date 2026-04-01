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
    unique_endpoint,
    wait_connected_pair,
)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="dealer_router")
    payload = new_payload(args.msg_size)
    latencies = []
    count = 0

    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            with zlink.DealerSocket(ctx) as dealer:
                endpoint = unique_endpoint("dealer-router")
                router.bind(endpoint)
                dealer.connect(endpoint)
                wait_connected_pair(router, dealer)

                with zlink.Poller() as poller:
                    poller.add_socket(router, zlink.PollEvent.POLLIN)

                    warmup_deadline = time.perf_counter() + args.warmup
                    while time.perf_counter() < warmup_deadline:
                        dealer.send(stamp_payload(payload))
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
                        dealer.send(stamp_payload(payload))
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
                    raise RuntimeError("dealer-router benchmark did not receive any message")
                elapsed = time.perf_counter() - started
                metrics = result_metrics(
                    count=count,
                    msg_size=args.msg_size,
                    elapsed_s=elapsed,
                    latencies_us=latencies,
                )
                print_result_lines("DEALER_ROUTER", "inproc", args.msg_size, metrics)


if __name__ == "__main__":
    main()
