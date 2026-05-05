import socket

import zlink
from sample_support import tcp_endpoint, wait_socket_monitor_event, wait_until


def main():
    port, endpoint = tcp_endpoint()
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as node:
            with node.create_spot() as spot:
                actor = node.actor("player-1")
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
                        join_info, message = item
                        message.close()
                        current_spot.reply_actor_join(join_info, True, b"ok")
                    elif info.event == zlink.SpotDispatchEvent.ACTOR_READABLE:
                        part = info.recv_actor_part(flags=zlink.RecvFlags.DONT_WAIT)
                        received.append(part.message.to_bytes())
                        part.message.close()

                spot.on_dispatch_event(on_dispatch)
                actor.join(
                    spot,
                    b"join",
                    lambda result, messages: (
                        replies.append(result),
                        [message.close() for message in messages],
                    ),
                    timeout=2,
                )
                wait_until(lambda: replies, timeout_ms=5000, description="actor join")

                with zlink.StreamSocket(ctx) as stream:
                    with stream.monitor_open(zlink.MonitorEventMask.ACCEPTED) as monitor:
                        stream.bind(endpoint)
                        with socket.create_connection(
                            ("127.0.0.1", port), timeout=3
                        ) as client:
                            wait_socket_monitor_event(
                                monitor, zlink.MonitorEventMask.ACCEPTED
                            )
                            client.sendall(b"hello")
                            with stream.recv() as stream_msg:
                                session_rid = stream_msg.routing_id
                            stream.bind_actor(node, session_rid, actor_ref, timeout=2)
                            stream.send_bound_actor(
                                node, session_rid, "player-1", b"client-payload"
                            )
                            wait_until(
                                lambda: received,
                                timeout_ms=5000,
                                description="actor payload",
                            )

                if received != [b"client-payload"]:
                    raise AssertionError("unexpected actor payload")
                actor.leave(spot)
                actor.close()
                print("[actor/gateway] stream relayed to actor")


if __name__ == "__main__":
    main()
