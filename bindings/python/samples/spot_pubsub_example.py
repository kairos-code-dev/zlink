"""자립형 가이드 예제: SPOT 토픽 pub/sub.

한 노드가 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
    PYTHONPATH=src python samples/spot_pubsub_example.py
"""

import errno

import zlink
from sample_support import tcp_endpoint, wait_spot_peer_connected, wait_until


def main():
# --8<-- [start:doc]
    with zlink.create_context() as ctx, \
         zlink.create_spot_node(ctx) as publisher_node, \
         zlink.create_spot_node(ctx) as subscriber_node:
        topic = "room:lobby"
        _, pub_endpoint = tcp_endpoint()
        _, sub_endpoint = tcp_endpoint()
        publisher_node.set_pub_bind(pub_endpoint)
        subscriber_node.set_pub_bind(sub_endpoint)
        publisher_node.connect_peer(sub_endpoint)
        subscriber_node.connect_peer(pub_endpoint)

        with publisher_node.create_spot() as publisher, \
             subscriber_node.create_spot() as subscriber:
            # 구독자는 받을 토픽을 등록한다.
            subscriber.set_subscription(topic)
            wait_spot_peer_connected(publisher_node, timeout_ms=15000)
            wait_spot_peer_connected(subscriber_node, timeout_ms=15000)

            # 연결 직후 첫 publish가 닿기 전일 수 있어, 도착할 때까지 반복 발행한다.
            received = zlink.create_topic_message()
            def publish_until_delivered():
                try:
                    with zlink.Message.from_(b"hello-everyone") as message:
                        publisher.publish(topic).message(message).submit()
                except zlink.SubmitError as exc:
                    if exc.native_errno != errno.ESHUTDOWN:
                        raise
                    return False
                try:
                    if subscriber.subscribe_into(received, flags=zlink.RecvFlags.DONT_WAIT):
                        return True
                except zlink.RecvError:
                    pass
                return False

            wait_until(
                publish_until_delivered,
                timeout_ms=5000,
                description="spot pubsub delivery",
            )

            received_topic = received.topic
            payload = received.to_bytes_list()[0].decode()
            received.close()
            print(f'[spot/pubsub] topic "{received_topic}" -> recv: "{payload}"')
# --8<-- [end:doc]


if __name__ == "__main__":
    main()
