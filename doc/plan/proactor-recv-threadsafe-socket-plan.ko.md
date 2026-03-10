# Proactor Recv 전환 및 Thread-Safe 소켓 방향성

## 1. 배경

zlink은 내부적으로 asio(proactor)를 사용하지만, 외부 API는 ZeroMQ 관례를
따라 reactor 스타일(동기 `recv()` + `poll()`)로 노출하고 있다.
이 구조는 asio 완료 → 내부 큐 → 사용자 `recv()`로 이중 큐가 발생한다.

## 2. 핵심 아이디어

### 2.1 모든 소켓/서비스를 callback 기반 수신으로 전환

- 현재: 사용자가 `recv()`를 호출해서 메시지를 꺼내감 (pull 모델)
- 전환: 메시지 도착 시 등록된 callback을 호출 (push 모델)
- io thread에서 수신 완료 → 바로 사용자 callback 호출
- `msg_t` 소유권을 callback으로 move 전달하여 zero-copy
- raw 소켓(PUB/SUB, PUSH/PULL, DEALER/ROUTER 등), 서비스(SPOT, Receiver 등) 모두 동일

### 2.2 이중 큐 제거

```
현재:  network → asio completion → 내부 큐(pipe/inproc) → recv()
전환:  network → asio completion → callback(msg_t&&)
```

중간 큐가 사라지고 레이어가 한 단계 줄어든다.

### 2.3 Thread-safe 소켓

- 소켓을 thread-safe하게 작성하여 임의 스레드에서 send/subscribe/set_option 등을
  안전하게 호출할 수 있게 한다.
- 필요한 경우 내부적으로 lock을 사용하여 동시 접근을 보호한다.
- 현재 SpotPub의 `scoped_lock(_sync)` 패턴과 동일한 방식을 소켓 전체로 확장.

### 2.4 Poller 변화

- **POLLIN**: 콜백이 즉시 호출되므로 의미 없어짐
- **POLLOUT**: send 측 flow control 용도로 유지 가능

### 2.5 Backpressure

- 사용자 책임으로 문서화
- 콜백이 블로킹하면 해당 소켓/서비스의 수신이 정지
- 권장: 콜백 내에서 lock-free 큐에 push, 별도 스레드에서 소비

### 2.6 바인딩 단순화

현재 바인딩은 `recv()` 루프 / poller / handler 세 패턴을 지원해야 하는데,
callback 하나로 통일되면 각 언어에서 static callback → 언어별 async
primitive(Promise, Future, asyncio 등)로 변환하는 단일 패턴으로 충분.

## 3. 적용 범위

SPOT만이 아니라 **zlink 전체 소켓 및 서비스 계층**에 적용하는 방향:

| 계층 | 대상 | 변경 |
|------|------|------|
| raw 소켓 | PUB/SUB, PUSH/PULL, DEALER/ROUTER, PAIR 등 | callback recv + thread-safe |
| 서비스 | SPOT, Receiver, Gateway, Discovery 등 | 동일 모델 적용 |
| socket_base_t | 공통 베이스 | thread-safe 보장, callback dispatch 기본 제공 |
