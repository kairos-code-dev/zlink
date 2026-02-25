#!/usr/bin/env python3
import sys

import perf_dealer_dealer
import perf_dealer_router
import perf_gateway
import perf_pair
import perf_pubsub
import perf_router_router
import perf_router_router_poll
import perf_spot
import perf_stream
import perf_stream_callback


RUNNERS = {
    "PAIR": perf_pair.run,
    "PUBSUB": perf_pubsub.run,
    "DEALER_DEALER": perf_dealer_dealer.run,
    "DEALER_ROUTER": perf_dealer_router.run,
    "ROUTER_ROUTER": perf_router_router.run,
    "ROUTER_ROUTER_POLL": perf_router_router_poll.run,
    "STREAM": perf_stream.run,
    "STREAM_CALLBACK": perf_stream_callback.run,
    "STREAM_LEN32BE": perf_stream.run_len32be,
    "GATEWAY": perf_gateway.run,
    "SPOT": perf_spot.run,
}

def run_pattern(pattern: str, transport: str, size: int) -> int:
    runner = RUNNERS.get(pattern.upper())
    if runner is None:
        return 2
    return runner(transport, size)


def main_from_args(args) -> int:
    if len(args) < 3:
        return 1
    pattern = args[0].upper()
    transport = args[1].lower()
    try:
        size = int(args[2])
    except ValueError:
        return 1
    return run_pattern(pattern, transport, size)


def main() -> int:
    return main_from_args(sys.argv[1:])


if __name__ == "__main__":
    raise SystemExit(main())
