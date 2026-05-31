[← 시작하기](./01-getting-started.ko.md) · [C++ 가이드](./index.ko.md) · [다음: 서비스 →](./03-services.ko.md)

# 메시징

소켓 패턴별 C++ API 사용법을 설명합니다.

---

## PAIR

```cpp
zlink::context_t ctx;
zlink::pair_socket_t server (ctx);
zlink::pair_socket_t client (ctx);

server.bind ("tcp://127.0.0.1:5560");
client.connect ("tcp://127.0.0.1:5560");

zlink::message_t msg = zlink::message_t::from_string ("hello");
client.send ().message (msg).submit ();

zlink::received_t inbound;
server.recv (inbound);
std::printf ("%s\n", inbound.parts ()[0].to_string ().c_str ());
inbound.close ();
```

---

## DEALER / ROUTER

### 단순 송수신

```cpp
zlink::context_t ctx;
zlink::router_socket_t router (ctx);
zlink::dealer_socket_t dealer (ctx);

auto rid = zlink::routing_id_t::from_bytes (
    reinterpret_cast<const uint8_t*> ("client-01"), 9);
dealer.set_routing_id (rid);

router.bind ("tcp://127.0.0.1:5561");
dealer.connect ("tcp://127.0.0.1:5561");

// 요청
zlink::message_t req = zlink::message_t::from_string ("get-price");
dealer.send ().message (req).submit ();

// 서버: 수신 후 회신
zlink::received_t request;
router.recv (request);
zlink::message_t reply = zlink::message_t::from_string ("101.25");
request.send ().message (reply).submit ();
request.close ();

// 클라이언트: 응답 수신
zlink::received_t response;
dealer.recv (response);
std::printf ("%s\n", response.parts ()[0].to_string ().c_str ()); // 101.25
response.close ();
```

### 비동기 요청

`submit_async()`는 `std::future`를 반환합니다.

```cpp
zlink::message_t req = zlink::message_t::from_string ("ping");
std::vector<zlink::message_t> reply =
    dealer.request ()
        .message (req)
        .timeout (std::chrono::milliseconds (2000))
        .submit_async ()
        .get ();   // 블로킹 대기

std::printf ("%s\n", reply[0].to_string ().c_str ()); // pong
// reply 벡터 소멸 시 각 message_t 자동 해제
```

코루틴(`co_await`)으로도 사용할 수 있습니다:

```cpp
auto reply = co_await dealer.request ().message (req).submit_async ();
```

서버 회신:

```cpp
zlink::received_t request;
router.recv (request);
if (request.request_seq ().has_value ()) {
    zlink::message_t reply = zlink::message_t::from_string ("pong");
    router.reply (request.routing_id ().value (),
                  request.request_seq ().value ())
          .message (reply).submit ();
}
```

---

## PUB / SUB

```cpp
zlink::context_t ctx;
zlink::pub_socket_t pub (ctx);
zlink::sub_socket_t sub (ctx);

pub.bind ("tcp://127.0.0.1:5562");
sub.set_subscription ("prices");
sub.connect ("tcp://127.0.0.1:5562");

zlink::message_t msg = zlink::message_t::from_string ("101.25");
pub.publish ("prices").message (msg).submit ();

zlink::topic_message_t inbound;
if (sub.subscribe (inbound) == static_cast<int> (zlink::recv_result_t::ok)) {
    std::printf ("%s: %s\n",
        inbound.topic ().c_str (),
        inbound.parts ()[0].to_string ().c_str ());
}
inbound.close ();
```

---

## XPUB / XSUB

```cpp
zlink::xpub_socket_t xpub (ctx);
zlink::sub_socket_t sub (ctx);

xpub.bind ("tcp://127.0.0.1:5563");
sub.connect ("tcp://127.0.0.1:5563");
sub.set_subscription ("events");

zlink::subscription_event_t event;
if (xpub.receive_subscription_event (event)
    == static_cast<int> (zlink::recv_result_t::ok)) {
    std::printf ("subscribed=%d topic=%s\n",
        event.subscribed, event.topic.c_str ());
}
```

---

## STREAM

```cpp
zlink::context_t ctx;
zlink::stream_socket_t server (ctx);
server.bind ("tcp://127.0.0.1:5564");

// 일반 TCP 클라이언트가 연결 후 "hello" 전송했다고 가정

zlink::received_t inbound;
server.recv (inbound);
std::printf ("%s\n", inbound.parts ()[0].to_string ().c_str ()); // hello

zlink::message_t reply = zlink::message_t::from_string ("world");
inbound.send ().message (reply).submit ();
inbound.close ();
```

패킷 콜백:

```cpp
server.set_packet_handler (
    [] (const zlink::routing_id_t &rid,
        zlink::message_t &&header,
        zlink::message_t &&body) {
        std::printf ("from %s: %s\n",
            rid.to_string ().c_str (),
            body.to_string ().c_str ());
    });
```

---

## 논블로킹 수신

```cpp
zlink::received_t inbound;
int rc = socket.recv (inbound, zlink::recv_flags_t::dontwait);
if (rc == static_cast<int> (zlink::recv_result_t::no_data)) {
    // 메시지 없음
} else if (rc == 0) {
    // 처리
    inbound.close ();
}
```

---

## 멀티파트 전송

```cpp
zlink::message_t header = zlink::message_t::from_string ("cmd:buy");
zlink::message_t body = zlink::message_t::from_string ("{\"qty\":10}");
dealer.send ().message (header).message (body).submit ();
```
