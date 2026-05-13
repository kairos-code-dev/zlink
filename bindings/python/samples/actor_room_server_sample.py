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
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as node:
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
                    join_info, message = item
                    joins.append(message.to_bytes())
                    message.close()
                    current_spot.reply_actor_join(join_info, True).message(
                        b"joined"
                    ).submit()

                spot.on_dispatch_event(on_dispatch)

                def on_reply(result, messages):
                    replies.append((result, [message.to_bytes() for message in messages]))
                    for message in messages:
                        message.close()

                with zlink.StreamSocket(ctx) as stream:
                    with stream.monitor_open(zlink.MonitorEventMask.ACCEPTED) as monitor:
                        stream.bind(endpoint)
                        with socket.create_connection(("127.0.0.1", port), timeout=3) as client:
                            wait_socket_monitor_event(
                                monitor, zlink.MonitorEventMask.ACCEPTED
                            )
                            client.sendall(b"seed")
                            with stream.recv() as stream_msg:
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
                            if replies[0] != (zlink.RequestResult.OK, [b"joined"]):
                                raise AssertionError("unexpected join reply")
                            if spot.actors_snapshot()[0].actor_id != "room-1":
                                raise AssertionError("joined actor snapshot missing")
                            submit_request_op(
                                actor.leave(spot), description="actor leave"
                            )
                actor.close()
                print("[actor/room] join accepted")


if __name__ == "__main__":
    main()
