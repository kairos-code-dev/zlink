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
                actor = node.actor("room-1")
                actor_ref = actor.ref()
                replies = []
                joins = []

                def on_dispatch(current_spot, info):
                    if info.event != zlink.SpotDispatchEvent.ACTOR_JOIN_READABLE:
                        return
                    item = current_spot.recv_actor_join(flags=zlink.RecvFlags.DONT_WAIT)
                    if item is None:
                        return
                    joins.append(item.message.to_bytes())
                    item.message.close()
                    current_spot.reply_actor_join(item, 0).message(
                        b"joined"
                    ).submit()

                spot.on_dispatch_event(on_dispatch)

                def on_reply(result, messages):
                    replies.append((result, [message.to_bytes() for message in messages]))
                    for message in messages:
                        message.close()

                with zlink.create_stream_socket(ctx) as stream:
                    stream.attach_actor_gateway(node)
                    with stream.monitor_open(zlink.MonitorEventMask.ACCEPTED) as monitor:
                        stream.bind(endpoint)
                        with socket.create_connection(("127.0.0.1", port), timeout=3) as client:
                            wait_socket_monitor_event(
                                monitor, zlink.MonitorEventMask.ACCEPTED
                            )
                            client.sendall(b"seed")
                            stream_msg = zlink.create_received()
                            if not stream.recv_into(stream_msg):
                                raise AssertionError("expected stream payload")
                            with stream_msg:
                                session_rid = stream_msg.routing_id
                            submit_request_op(
                                stream.bind_actor(session_rid, actor_ref),
                                description="stream actor bind",
                            )

                            actor.join(spot).message(b"join-room").timeout(2).submit(
                                on_reply
                            )
                            wait_until(lambda: replies, timeout_ms=5000, description="actor join")

                            if joins != [b"join-room"]:
                                raise AssertionError("unexpected join request")
                            if (
                                replies[0][0].result != zlink.RequestResult.OK
                                or replies[0][1] != [b"joined"]
                            ):
                                raise AssertionError("unexpected join reply")
                            if spot.actors()[0].actor_id != "room-1":
                                raise AssertionError("joined actor snapshot missing")
                            submit_request_op(
                                actor.leave(spot), description="actor leave"
                            )
                actor.close()
                print("[actor/room] join accepted")


if __name__ == "__main__":
    main()
