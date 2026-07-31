[한국어](scenarios.ko.md)

[가이드 목록](../index.ko.md)

# 공유 시나리오 매트릭스

이 문서는 zlink 가이드 전반에서 쓰는 **정규 예제 시나리오**를 한 곳에 정의한다.
코어 가이드(C)와 7개 언어 바인딩 가이드는 모두 이 시나리오를 재사용한다 — 한
언어를 익힌 사용자가 다른 언어 가이드를 같은 시나리오로 바로 읽을 수 있게 하기
위해서다.

> 각 시나리오는 모든 언어의 `bindings/<lang>/samples/`에 같은 의미의 실행 가능한
> 샘플이 있다(파일명은 언어 idiom을 따른다). 가이드 코드는 이 샘플과 1:1로
> 대응한다([예제 규약](EXAMPLES.ko.md)).

## 정규 시나리오

| # | 시나리오 | 패턴 | 무엇을 보이나 | 샘플 |
|---|----------|------|--------------|------|
| 1 | **PING/ACK** | PAIR | 첫 메시지 — 1:1 송수신 | `PairRecv` / `pair_recv_sample` |
| 2 | **부하 분산 요청/응답** | DEALER ↔ ROUTER | 라우팅된 요청, fan-out 워커 | `DealerRouterRecv` / `dealer_router_recv_sample` |
| 3 | **비동기 요청/응답** | DEALER | correlation·timeout이 있는 RPC | `RequestReplyAsync` / `request_reply_async_sample` |
| 4 | **토픽 Pub/Sub** | PUB / SUB | 토픽 기반 이벤트 fan-out | `PubSubRecv` / `pubsub_recv_sample` |
| 6 | **SPOT 메시징** | SPOT | 동적 상태 단위 + 토픽/라우팅 | `SpotRecv`, `SpotRequestAsync` |
| 7 | **Actor 룸 서버** | SPOT + Actor + STREAM | 세션↔엔티티 binding, 룸 dispatch | `ActorRoomServer`, `ActorSinglePlayerQueue`, `ActorGatewayRelay` |
| 8 | **STREAM 패킷** | STREAM | 외부 raw TCP 클라이언트 | `StreamPacketCallback` / `stream_packet_callback_sample` |

## 시나리오별 고정 값 (전 언어 공통)

같은 시나리오는 모든 언어 가이드에서 **같은 값**을 쓴다 — 코드만 언어 관용구가
다르다.

| 시나리오 | 고정 값 |
|----------|--------|
| PING/ACK | 페이로드 `"PING"` → `"ACK"`, `tcp://127.0.0.1:5555` |
| 비동기 요청/응답 | routing id `"order-client"`, 요청 `"ping"` → 응답 `"pong"`, timeout 2초 |
| 토픽 Pub/Sub | 토픽 `"prices"`, 페이로드 `"101.25"` |
| SPOT | 토픽 `"room:lobby"`, 채널 `"orders"` |
| Actor | actor id `"room-player-1"`, join `"join:lobby"` → `"accepted:lobby"` |

## 어떻게 쓰나 (작성자용)

- 새 언어 바인딩 가이드를 쓸 때 이 매트릭스의 시나리오·고정 값을 그대로 따른다.
  코드만 그 언어 관용구로 바꾼다.
- 코어 가이드의 패턴 챕터(03-*)도 같은 시나리오를 예시로 든다.
- 시나리오를 추가·변경하면 이 문서를 먼저 고친 뒤 각 언어 가이드를 맞춘다.

---

> 더 보기: [소켓 패턴 선택](03-0-socket-patterns.ko.md) ·
> [바인딩 가이드](../../../../bindings/doc/guide/README.ko.md) · [예제 규약](EXAMPLES.ko.md).
