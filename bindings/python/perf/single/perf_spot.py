import sys
import threading
import time

import zlink

from perf_common import (
    HEADER_MAGIC,
    benchmark_run_id,
    apply_single_spot_node_admission,
    configure_single_tls_client,
    configure_single_tls_server,
    decode_header,
    latency_ns_from_message,
    is_active_message,
    new_payload,
    parse_single_args,
    print_result_lines,
    recv_nonblocking,
    resolve_single_connect_ready_timeout_ms,
    resolve_single_endpoint,
    resolve_single_recv_timeout_ms,
    resolve_single_spot_ready_settle_s,
    result_metrics,
    stamp_payload,
)


CHANNEL_NAME = "spot-svc"
TOPIC = "bench.topic"


def main(argv=None):
    args = parse_single_args(argv or sys.argv[1:], pattern="spot")
    run_id = benchmark_run_id()
    latencies = []
    probe_ready = threading.Event()
    recv_lock = threading.Lock()
    active_deadline = [0.0]
    probe_payload = new_payload(args.msg_size)
    active_payload = new_payload(args.msg_size)
    cooldown_payload = new_payload(args.msg_size)

    with zlink.Context() as ctx:
        with zlink.Registry(ctx) as registry:
            with zlink.Discovery(
                ctx, zlink.AutoConnectType.SPOT_MESH, CHANNEL_NAME
            ) as publisher_discovery:
                with zlink.Discovery(
                    ctx, zlink.AutoConnectType.SPOT_MESH, CHANNEL_NAME
                ) as subscriber_discovery:
                    with zlink.SpotNode(ctx) as publisher_node:
                        with zlink.SpotNode(ctx) as subscriber_node:
                            apply_single_spot_node_admission(
                                publisher_node, subscriber_node
                            )
                            with publisher_node.create_spot() as publisher:
                                with subscriber_node.create_spot() as subscriber:
                                    publisher_node.set_routing_id(
                                        b"z-python-perf-spot-publisher"
                                    )
                                    subscriber_node.set_routing_id(
                                        b"a-python-perf-spot-subscriber"
                                    )
                                    publisher.set_routing_id(
                                        b"z-python-perf-spot-publisher-spot"
                                    )
                                    subscriber.set_routing_id(
                                        b"a-python-perf-spot-subscriber-spot"
                                    )
                                    registry_pub_endpoint = resolve_single_endpoint(
                                        args.transport, "spot-registry-pub"
                                    )
                                    registry_router_endpoint = resolve_single_endpoint(
                                        args.transport, "spot-registry-router"
                                    )
                                    publisher_endpoint = resolve_single_endpoint(
                                        args.transport, "spot-publisher"
                                    )
                                    subscriber_endpoint = resolve_single_endpoint(
                                        args.transport, "spot-subscriber"
                                    )
                                    configure_single_tls_server(
                                        publisher_node, args.transport
                                    )
                                    configure_single_tls_client(
                                        subscriber_node, args.transport
                                    )
                                    registry.bind(
                                        registry_pub_endpoint,
                                        registry_router_endpoint,
                                    )
                                    registry.set_broadcast_interval(50)
                                    publisher_discovery.connect_registry(
                                        registry_router_endpoint
                                    )
                                    subscriber_discovery.connect_registry(
                                        registry_router_endpoint
                                    )
                                    publisher_node.bind(publisher_endpoint)
                                    subscriber_node.bind(subscriber_endpoint)
                                    publisher_node.attach_discovery(
                                        publisher_discovery
                                    )
                                    subscriber_node.attach_discovery(
                                        subscriber_discovery
                                    )
                                    subscriber.set_subscription(TOPIC)

                                    def on_dispatch(current_spot, info):
                                        if (
                                            info.event
                                            != zlink.SpotDispatchEvent.SUBSCRIBE_READABLE
                                        ):
                                            return
                                        while True:
                                            received = recv_nonblocking(
                                                current_spot, method="subscribe"
                                            )
                                            if received is None:
                                                return
                                            with received:
                                                parts = received.to_bytes_list()
                                            if not parts:
                                                continue
                                            data = parts[0]
                                            header = decode_header(data)
                                            if (
                                                not probe_ready.is_set()
                                                and header is not None
                                                and header["magic"] == HEADER_MAGIC
                                                and header["phase"] == 0
                                                and header["msg_size"] == args.msg_size
                                                and header["run_id"] == run_id
                                            ):
                                                probe_ready.set()
                                                continue
                                            if not is_active_message(
                                                data,
                                                expected_msg_size=args.msg_size,
                                                run_id=run_id,
                                            ):
                                                continue
                                            if time.perf_counter() > active_deadline[0]:
                                                continue
                                            with recv_lock:
                                                latencies.append(
                                                    latency_ns_from_message(data)
                                                )

                                    subscriber.on_dispatch_event(on_dispatch)

                                    ready_deadline = time.monotonic() + (
                                        resolve_single_connect_ready_timeout_ms()
                                        / 1000.0
                                    )
                                    while (
                                        not probe_ready.is_set()
                                        and time.monotonic() < ready_deadline
                                    ):
                                        publisher.publish(
                                            CHANNEL_NAME,
                                            TOPIC,
                                            [
                                                stamp_payload(
                                                    probe_payload,
                                                    phase=0,
                                                    run_id=run_id,
                                                )
                                            ],
                                        )
                                        probe_ready.wait(0.05)
                                    if not probe_ready.is_set():
                                        raise RuntimeError(
                                            "spot benchmark probe-ready timeout"
                                        )

                                    time.sleep(resolve_single_spot_ready_settle_s())

                                    active_deadline[0] = time.perf_counter() + args.duration
                                    while time.perf_counter() < active_deadline[0]:
                                        publisher.publish(
                                            CHANNEL_NAME,
                                            TOPIC,
                                            [
                                                stamp_payload(
                                                    active_payload,
                                                    phase=1,
                                                    run_id=run_id,
                                                )
                                            ],
                                        )
                                    publisher.publish(
                                        CHANNEL_NAME,
                                        TOPIC,
                                        [
                                            stamp_payload(
                                                cooldown_payload,
                                                phase=2,
                                                run_id=run_id,
                                            )
                                        ],
                                    )
                                    threading.Event().wait(
                                        resolve_single_recv_timeout_ms() / 1000.0
                                    )

                                    with recv_lock:
                                        collected = list(latencies)
                                    if not collected:
                                        raise RuntimeError(
                                            "spot benchmark did not receive any active message"
                                        )
                                    metrics = result_metrics(
                                        count=len(collected),
                                        msg_size=args.msg_size,
                                        elapsed_s=args.duration,
                                        latencies_ns=collected,
                                    )
                                    print_result_lines(
                                        "SPOT", args.transport, args.msg_size, metrics
                                )


if __name__ == "__main__":
    main()
