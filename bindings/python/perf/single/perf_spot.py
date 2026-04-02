import sys
import time
import uuid

import zlink

from perf_common import (
    latency_us_from_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    result_metrics,
    safe_poll,
    stamp_payload,
)


def _wait_spot_ready(monitor, timeout_s):
    deadline = time.perf_counter() + timeout_s
    filter_ready = False
    pub_ready = False
    sub_ready = False
    while time.perf_counter() < deadline:
        event = monitor.recv()
        if event.event_type == zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED:
            filter_ready = True
        elif (
            event.event_type == zlink.ServiceMonitorMask.SPOT_FIRST_DELIVERY_READY_CHANGED
            and event.value > 0
        ):
            pub_ready = True
        elif (
            event.event_type == zlink.ServiceMonitorMask.SPOT_SUB_DELIVERY_READY_CHANGED
            and event.value > 0
        ):
            sub_ready = True
        if filter_ready and pub_ready and sub_ready:
            return
    raise RuntimeError("timed out waiting for spot delivery-ready events")


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="spot")
    payload = new_payload(args.msg_size)
    topic = f"bench.{uuid.uuid4().hex}".encode("ascii")
    latencies = []
    count = 0

    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as node:
            with zlink.Spot(node) as spot:
                with spot.open_monitor(
                    zlink.ServiceMonitorMask.SPOT_FILTER_APPLIED
                    | zlink.ServiceMonitorMask.SPOT_FIRST_DELIVERY_READY_CHANGED
                    | zlink.ServiceMonitorMask.SPOT_SUB_DELIVERY_READY_CHANGED
                ) as monitor:
                    spot.on_send_ready(lambda _: None)
                    spot.set_subscription(topic)
                    _wait_spot_ready(monitor, 5.0)

                    with zlink.Poller() as poller:
                        poller.add_socket(spot, zlink.PollEvent.POLLIN)

                        warmup_deadline = time.perf_counter() + args.warmup
                        while time.perf_counter() < warmup_deadline:
                            spot.publish(topic, [stamp_payload(payload)])
                            for event in safe_poll(poller, 0):
                                while True:
                                    received = event["socket"].try_subscribe()
                                    if received is None:
                                        break
                                    with received:
                                        pass

                        started = time.perf_counter()
                        deadline = started + args.duration
                        while time.perf_counter() < deadline:
                            spot.publish(topic, [stamp_payload(payload)])
                            for event in safe_poll(poller, 0):
                                while True:
                                    received = event["socket"].try_subscribe()
                                    if received is None:
                                        break
                                    with received:
                                        latencies.append(
                                            latency_us_from_message(received.to_bytes_list()[0])
                                        )
                                        count += 1

                    if count == 0:
                        raise RuntimeError("spot benchmark did not receive any message")
                    elapsed = time.perf_counter() - started
                    metrics = result_metrics(
                        count=count,
                        msg_size=args.msg_size,
                        elapsed_s=elapsed,
                        latencies_us=latencies,
                    )
                    print_result_lines("SPOT", "inproc", args.msg_size, metrics)


if __name__ == "__main__":
    main()
