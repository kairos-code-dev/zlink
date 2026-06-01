"""자립형 가이드 예제: SPOT 토픽 pub/sub.

한 노드가 토픽에 publish하면, 그 토픽을 구독한 다른 노드가 받는다.
    PYTHONPATH=src python samples/spot_pubsub_example.py
"""

import socket
import time

import zlink


def unique_tcp():
    sock = socket.socket()
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
    sock.close()
    return f"tcp://127.0.0.1:{port}"


def wait_peer(node):
    deadline = time.monotonic() + 15
    while time.monotonic() < deadline:
        status = node.status()
        if status is not None and status.connected_peer_count > 0:
            return
        time.sleep(0.01)
    raise RuntimeError("spot peer not connected")


def main():
    with zlink.create_context() as ctx, \
         zlink.create_spot_node(ctx) as publisher_node, \
         zlink.create_spot_node(ctx) as subscriber_node:
        topic = "room:lobby"
        pub_endpoint = unique_tcp()
        sub_endpoint = unique_tcp()
        publisher_node.set_pub_bind(pub_endpoint)
        subscriber_node.set_pub_bind(sub_endpoint)
        publisher_node.connect_peer(sub_endpoint)
        subscriber_node.connect_peer(pub_endpoint)

        with publisher_node.create_spot() as publisher, \
             subscriber_node.create_spot() as subscriber:
            # 구독자는 받을 토픽을 등록한다.
            subscriber.set_subscription(topic)
            wait_peer(publisher_node)
            wait_peer(subscriber_node)

            # 연결 직후 첫 publish가 닿기 전일 수 있어, 도착할 때까지 반복 발행한다.
            received = zlink.create_topic_message()
            delivered = False
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                publisher.publish(topic).message(b"hello-everyone").submit()
                try:
                    if subscriber.subscribe_into(received, flags=zlink.RecvFlags.DONT_WAIT):
                        delivered = True
                        break
                except zlink.RecvError:
                    pass
                time.sleep(0.01)
            if not delivered:
                raise RuntimeError("spot delivery did not arrive")

            received_topic = received.topic
            payload = received.to_bytes_list()[0].decode()
            received.close()
            print(f'[spot/pubsub] topic "{received_topic}" -> recv: "{payload}"')


if __name__ == "__main__":
    main()
