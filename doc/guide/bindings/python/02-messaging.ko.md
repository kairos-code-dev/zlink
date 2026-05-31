[← 시작하기](./01-getting-started.ko.md) · [Python 가이드](./index.ko.md) · [다음: 서비스 →](./03-services.ko.md)

# 메시징

소켓 패턴별 Python API 사용법을 설명합니다.

---

## PAIR

```python
import zlink

with zlink.create_context() as ctx:
    with zlink.create_pair_socket(ctx) as server, \
         zlink.create_pair_socket(ctx) as client:
        server.bind("tcp://127.0.0.1:5560")
        client.connect("tcp://127.0.0.1:5560")

        client.send().message(b"hello").submit()

        received = zlink.create_received()
        server.recv_into(received)
        with received:
            print(received.to_bytes_list()[0])  # b"hello"
```

---

## DEALER / ROUTER

### 단순 송수신

```python
with zlink.create_context() as ctx:
    with zlink.create_router_socket(ctx) as router, \
         zlink.create_dealer_socket(ctx) as dealer:

        dealer.set_routing_id(b"client-01")
        router.bind("tcp://127.0.0.1:5561")
        dealer.connect("tcp://127.0.0.1:5561")

        dealer.send().message(b"get-price").submit()

        request = zlink.create_received()
        router.recv_into(request)
        with request:
            print(request.to_bytes_list())  # [b"get-price"]
            request.send().message(b"101.25").submit()

        reply = zlink.create_received()
        dealer.recv_into(reply)
        with reply:
            print(reply.to_bytes_list()[0])  # b"101.25"
```

### 비동기 요청

```python
import asyncio, zlink

async def main():
    with zlink.create_context() as ctx:
        with zlink.create_dealer_socket(ctx) as dealer:
            dealer.connect("tcp://127.0.0.1:5561")
            reply_parts = await dealer.request() \
                .message(b"get-price") \
                .timeout(2.0) \
                .submit_async()
            try:
                print([p.to_bytes() for p in reply_parts])
            finally:
                for p in reply_parts:
                    p.close()

asyncio.run(main())
```

서버 쪽 회신:

```python
request = zlink.create_received()
router.recv_into(request)
with request:
    if request.request_seq is not None:
        router.reply(request.routing_id, request.request_seq) \
              .message(b"101.25").submit()
```

---

## PUB / SUB

```python
with zlink.create_context() as ctx:
    with zlink.create_pub_socket(ctx) as pub, \
         zlink.create_sub_socket(ctx) as sub:

        pub.bind("tcp://127.0.0.1:5562")
        sub.set_subscription(b"prices")
        sub.connect("tcp://127.0.0.1:5562")

        pub.publish(b"prices").message(b"101.25").submit()

        topic = zlink.create_topic_message()
        if sub.subscribe_into(topic):
            with topic:
                print(topic.topic, topic.to_bytes_list())
                # "prices", [b"101.25"]
```

---

## XPUB / XSUB

```python
with zlink.create_xpub_socket(ctx) as xpub, \
     zlink.create_sub_socket(ctx) as sub:

    xpub.bind("tcp://127.0.0.1:5563")
    sub.connect("tcp://127.0.0.1:5563")
    sub.set_subscription(b"events")

    event = zlink.create_subscription_event()
    if xpub.receive_subscription_event_into(event):
        print(event.subscribed, event.topic)  # True, "events"
```

---

## STREAM

```python
import socket as tcp_socket

with zlink.create_context() as ctx:
    with zlink.create_stream_socket(ctx) as server:
        server.bind("tcp://127.0.0.1:5564")

        sock = tcp_socket.create_connection(("127.0.0.1", 5564))
        sock.sendall(b"hello")

        received = zlink.create_received()
        server.recv_into(received)
        with received:
            print(received.to_bytes_list()[0])  # b"hello"
            received.send().message(b"world").submit()

        sock.close()
```

---

## 논블로킹 수신

```python
received = zlink.create_received()
ok = socket.recv_into(received, zlink.RecvFlags.DONT_WAIT)
if not ok:
    pass  # 메시지 없음
else:
    with received:
        # 처리
        pass
```

---

## 멀티파트 전송

```python
dealer.send().message(b"header").message(b"body").submit()

# 수신
received = zlink.create_received()
router.recv_into(received)
with received:
    for part in received.to_bytes_list():
        print(part)
```
