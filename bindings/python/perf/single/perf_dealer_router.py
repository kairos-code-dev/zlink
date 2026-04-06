import sys
import time

import zlink

from perf_common import (
    CallbackMetrics,
    new_payload,
    parse_single_args,
    payload_phase,
    print_result_lines,
    result_metrics,
    stamp_payload,
    unique_endpoint,
)


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="dealer_router")
    payload = new_payload(args.msg_size)
    metrics_sink = CallbackMetrics(phase_filter=1)

    with zlink.Context() as ctx:
        with zlink.RouterSocket(ctx) as router:
            with zlink.DealerSocket(ctx) as dealer:
                endpoint = unique_endpoint("dealer-router")
                router.bind(endpoint)
                dealer.connect(endpoint)

                def on_message(received):
                    data = received.to_bytes_list()[0]
                    metrics_sink.on_payload(data, phase=payload_phase(data))

                router.on_receive(on_message)

                warmup_ready = False
                warmup_deadline = time.perf_counter() + args.warmup
                while time.perf_counter() < warmup_deadline:
                    dealer.send(stamp_payload(payload, phase=0))
                    warmup_ready = metrics_sink.wait_ready(0) or warmup_ready

                if not warmup_ready:
                    raise RuntimeError(
                        "dealer-router benchmark did not receive any warmup message"
                    )

                metrics_sink.activate()
                started = time.perf_counter()
                deadline = started + args.duration
                while time.perf_counter() < deadline:
                    dealer.send(stamp_payload(payload, phase=1))

                metrics_sink.deactivate()
                count, latencies = metrics_sink.finish()
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
