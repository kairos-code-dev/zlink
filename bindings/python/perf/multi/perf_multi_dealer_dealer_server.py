import sys
import threading
import time

import zlink

from perf_multi_common import (
    apply_multi_socket_options,
    benchmark_endpoint,
    benchmark_run_id,
    new_payload,
    parse_server_args,
    safe_poll,
    send_nonblocking,
    stamp_payload,
)


def main(argv=None):
    args = parse_server_args(argv or sys.argv[1:])
    run_id = benchmark_run_id()
    endpoint = benchmark_endpoint(args.transport, "multi-dealer-dealer")
    active_duration_s = float(
        max(1, int(float(sys.modules["os"].environ.get("PERF_MULTI_DURATION_SECONDS", "5"))))
    )
    start_event = threading.Event()
    stop_event = threading.Event()
    payload = new_payload(args.msg_size)

    def read_commands():
        for line in sys.stdin:
            text = line.strip().upper()
            if text == f"START,{args.msg_size}":
                start_event.set()
            elif text in {"STOP", "QUIT"}:
                stop_event.set()
                return

    threading.Thread(target=read_commands, daemon=True).start()

    with zlink.Context() as ctx:
        with zlink.DealerSocket(ctx) as dealer:
            apply_multi_socket_options(dealer)
            dealer.bind(endpoint)
            print(f"READY,{endpoint}", flush=True)
            while not start_event.is_set() and not stop_event.is_set():
                stop_event.wait(0.01)
            if stop_event.is_set():
                return

            active_deadline = time.perf_counter() + active_duration_s
            cooldown_sent = False
            send_pending = True
            with zlink.Poller() as poller:
                poller.add_socket(dealer, zlink.PollEvent.POLLOUT)
                while not stop_event.is_set():
                    if time.perf_counter() >= active_deadline and cooldown_sent:
                        break
                    events = safe_poll(poller, 100)
                    if not events:
                        continue
                    for event in events:
                        if not (event["events"] & int(zlink.PollEvent.POLLOUT)):
                            continue
                        while send_pending and not stop_event.is_set():
                            phase = 1 if time.perf_counter() < active_deadline else 2
                            sent = send_nonblocking(
                                dealer,
                                stamp_payload(payload, phase=phase, run_id=run_id),
                            )
                            if not sent:
                                break
                            if phase == 2:
                                cooldown_sent = True
                                send_pending = False


if __name__ == "__main__":
    main()
