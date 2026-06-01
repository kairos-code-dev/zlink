import socket

import zlink
from sample_support import (
    submit_request_op,
    tcp_endpoint,
    wait_socket_monitor_event,
    wait_until,
)


def main():
    port, endpoint = tcp_endpoint()
    with zlink.create_context() as ctx:
        with zlink.create_spot_node(ctx) as node:
            with node.create_spot() as spot:
                actor = node.actor("play-session-actor")
                actor_ref = actor.ref()
                received = []
                replies = []

                def on_dispatch(current_spot, info):
                    if info.event == zlink.SpotDispatchEvent.ACTOR_JOIN_READABLE:
                        item = current_spot.recv_actor_join(
                            flags=zlink.RecvFlags.DONT_WAIT
                        )
                        if item is None:
                            return
                        item.message.close()
                        current_spot.reply_actor_join(item, 0).message(
                            b"accepted"
                        ).submit()
                    elif info.event == zlink.SpotDispatchEvent.ACTOR_READABLE:
                        part = info.recv_actor_part(flags=zlink.RecvFlags.DONT_WAIT)
                        received.append(part.message.to_bytes())
                        part.message.close()

                spot.on_dispatch_event(on_dispatch)

                with zlink.create_stream_socket(ctx) as stream:
                    stream.attach_actor_gateway(node)
                    with stream.monitor_open(zlink.MonitorEventMask.ACCEPTED) as monitor:
                        stream.bind(endpoint)
                        with socket.create_connection(
                            ("127.0.0.1", port), timeout=3
                        ) as client:
                            wait_socket_monitor_event(
                                monitor, zlink.MonitorEventMask.ACCEPTED
                            )
                            client.sendall(b"hello")
                            stream_msg = zlink.create_received()
                            if not stream.recv_into(stream_msg):
                                raise AssertionError("expected stream payload")
                            with stream_msg:
                                session_rid = stream_msg.routing_id
                            submit_request_op(
                                stream.bind_actor(session_rid, actor_ref),
                                description="stream actor bind",
                            )
                            actor.join(spot).message(b"join-play").timeout(2).submit(
                                lambda result, messages: (
                                    replies.append(result),
                                    [message.close() for message in messages],
                                ),
                            )
                            wait_until(lambda: replies, timeout_ms=5000, description="actor join")
                            stream.send_bound_actor(session_rid, "play-session-actor").message(
                                b"client-input"
                            ).submit()
                            wait_until(
                                lambda: received,
                                timeout_ms=5000,
                                description="actor payload",
                            )

                            if received != [b"client-input"]:
                                raise AssertionError("unexpected actor payload")
                            submit_request_op(
                                actor.leave(spot), description="actor leave"
                            )
                actor.close()
                print('[actor/gateway] stream payload: "client-input" -> actor: "client-input"')


if __name__ == "__main__":
    main()
