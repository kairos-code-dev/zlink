#!/usr/bin/env python3
import sys

import pattern_dealer_dealer
import pattern_dealer_router
import pattern_gateway
import pattern_pair
import pattern_pubsub
import pattern_router_router
import pattern_router_router_poll
import pattern_spot
import pattern_stream


RUNNERS = {
    "PAIR": pattern_pair.run,
    "PUBSUB": pattern_pubsub.run,
    "DEALER_DEALER": pattern_dealer_dealer.run,
    "DEALER_ROUTER": pattern_dealer_router.run,
    "ROUTER_ROUTER": pattern_router_router.run,
    "ROUTER_ROUTER_POLL": pattern_router_router_poll.run,
    "STREAM": pattern_stream.run,
    "GATEWAY": pattern_gateway.run,
    "SPOT": pattern_spot.run,
}


def run_pattern(pattern: str, transport: str, size: int) -> int:
    runner = RUNNERS.get(pattern.upper())
    if runner is None:
        return 2
    return runner(transport, size)


def main_from_args(args) -> int:
    if len(args) < 3:
        return 1
    try:
        size = int(args[2])
    except ValueError:
        return 1
    return run_pattern(args[0], args[1], size)


def main() -> int:
    return main_from_args(sys.argv[1:])


if __name__ == "__main__":
    raise SystemExit(main())
