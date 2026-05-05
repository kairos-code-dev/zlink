import zlink

from sample_support import wait_until


def main():
    with zlink.Context() as ctx:
        with zlink.SpotNode(ctx) as node:
            with node.create_spot() as spot:
                actor = node.actor("room-1")
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
                    current_spot.reply_actor_join(join_info, True, b"joined")

                spot.on_dispatch_event(on_dispatch)

                def on_reply(result, messages):
                    replies.append((result, [message.to_bytes() for message in messages]))
                    for message in messages:
                        message.close()

                actor.join(spot, b"join-room", on_reply, timeout=2)
                wait_until(lambda: replies, timeout_ms=5000, description="actor join")

                if joins != [b"join-room"]:
                    raise AssertionError("unexpected join request")
                if replies[0] != (zlink.RequestResult.OK, [b"joined"]):
                    raise AssertionError("unexpected join reply")
                if spot.actors_snapshot()[0].actor_id != "room-1":
                    raise AssertionError("joined actor snapshot missing")
                actor.leave(spot)
                actor.close()
                print("[actor/room] join accepted")


if __name__ == "__main__":
    main()
