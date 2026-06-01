[English](./reliability.md) | [한국어](./reliability.ko.md)

[가이드 목록](./README.ko.md)

# 신뢰성과 전달 보장

메시징 라이브러리를 도입할 때 가장 먼저 묻게 되는 질문은 **"이 메시지가
보장되는가"**다. 이 문서는 zlink가 무엇을 보장하고 무엇을 보장하지 않는지를
정직하게 정리한다. 각 항목은 소켓 옵션·패턴 챕터가 소유하는 동작을 신뢰성 관점에서
한데 모은 것이다.

> 한 줄 요약: **zlink는 transport·dispatch 계층이지 durable queue가 아니다.**
> 메시지 보존(영속), 재시도, 정확히 한 번 전달은 응용/인프라의 몫이다. zlink가
> 주는 것은 빠른 전달과 명확한 흐름 제어 신호다.

---

## 1. 흐름 제어 — HWM에 도달하면

소켓 송신 큐에는 상한(HWM, High Water Mark)이 있다. 도달했을 때 동작은 **송신
플래그**로 갈린다.

| 송신 방식 | HWM 도달 시 |
|-----------|------------|
| 블로킹 (`flags=0`) | 큐에 자리가 날 때까지 대기, `SNDTIMEO` 만료 시 `BACKPRESSURED` 반환 |
| 논블로킹 (`DONTWAIT`) | 즉시 `BACKPRESSURED` 반환 (대기 없음) |

`SNDTIMEO` 기본값은 `1000ms`이며, `-1`은 무한 대기, `0`은 `DONTWAIT`과 같다.

**`BACKPRESSURED`는 오류가 아니다.** "지금 큐가 가득 찼다"는 신호이며, 응용은
나중에 재시도하거나 send-ready 신호를 기다린다. 큐가 `(HWM+1)/2`까지 빠지면 다시
쓰기 가능 상태가 되고 send-ready 핸들러가 발화한다.

> HWM은 기본적으로 **Auto HWM**(연결 수에 맞춰 자동 조정)이다. 프로필은
> `BALANCED`(기본)·`COMPACT`·`LOW_LATENCY`·`THROUGHPUT`. 컨텍스트 auto-HWM을 끄면
> 고정 기본값 `1000`을 쓴다. 자세한 내용은 [10 성능](./10-performance.ko.md),
> [12 소켓 옵션](./12-socket-options.ko.md).

---

## 2. 소켓 패턴별 전달 보장

소켓 패턴마다 보장의 성격이 다르다. **공통적으로 zlink는 application-level 전달
ACK를 제공하지 않는다** — 전달 확인이 필요하면 요청/응답을 쓴다.

| 패턴 | 전달 성격 | HWM 도달 시 기본 동작 |
|------|----------|----------------------|
| **PAIR** | best-effort, 방향별 순서 보존 | 블로킹 또는 `BACKPRESSURED` |
| **PUB / SUB** | best-effort | **`NODROP=1`이 기본** → drop 없이 `BACKPRESSURED` 반환 |
| **DEALER** | best-effort, 라운드로빈 | 블로킹 또는 `BACKPRESSURED` |
| **ROUTER** | best-effort, 피어별 큐 | `MANDATORY=1`(기본) → 도달 불가/큐참 시 `NOT_CONNECTED` |
| **STREAM** | best-effort, 연결별 | 연결(source_rid)별 큐잉 |

> **PUB 기본값 주의**: zlink의 `ZLINK_PUB_OPT_NODROP` **기본값은 `1`**이다. 즉
> 느린 구독자 때문에 HWM이 차면 조용히 버리지 않고 `zlink_publish()`가
> `BACKPRESSURED`를 반환한다. 전통적 ZeroMQ처럼 "느리면 버린다"를 원하면
> `NODROP=0`을 **명시적으로** 설정한다([03-2 PUB/SUB](./03-2-pubsub.ko.md)).

**PUB/SUB의 구조적 손실 지점** (보장되지 않음, 정직하게):
- **slow-joiner**: 구독이 발행자에 전파되기 전에 발행된 메시지는 받지 못한다.
  구독은 비동기로 전파된다.
- 이는 손실이 아니라 PUB/SUB의 설계 특성이다. 첫 메시지 보장이 필요하면 구독 후
  발행을 반복하거나, 요청/응답·SPOT 같은 다른 패턴을 쓴다.

---

## 3. 순서 보장

- **한 연결/피어 안에서는 순서가 보존된다** — PAIR(방향별), ROUTER(피어별 큐),
  STREAM(연결별), PUB/SUB(한 토픽·한 구독자 내).
- **멀티파트 메시지는 원자적이다** — 전체가 함께 전달되거나 전체가 실패한다. 파트가
  중간에 섞이지 않는다([internals/multipart-atomicity](../internals/multipart-atomicity.ko.md)).
- **보장되지 않는 것**:
  - 여러 스레드가 같은 소켓에 보낼 때 스레드 간 순서(각 메시지는 원자적이지만
    인입 순서는 정해지지 않음).
  - 재연결을 가로지르는 순서(아래 4절).
  - DEALER의 fair-queue 수신에서 서로 다른 송신자 간의 엄격한 순서.

---

## 4. 재연결과 in-flight 메시지

연결이 끊기면 zlink는 자동으로 재연결을 시도한다.

- `RECONNECT_IVL` 기본 `100ms`. `RECONNECT_IVL_MAX > 0`이면 지수 백오프(실패마다
  2배, MAX에서 상한).
- 재연결 시점은 모니터 이벤트로 관측한다: `DISCONNECTED` → `CONNECT_DELAYED` /
  `CONNECT_RETRIED` → `CONNECTION_READY`([06 모니터링](./06-monitoring.ko.md)).

**in-flight 메시지는 재연결을 가로질러 보존이 보장되지 않는다.** 끊기기 전에 큐에
있던 메시지는 (a) 빠르게 재연결되면 그대로 흘러가고, (b) 그렇지 않으면 유실될 수
있다. zlink는 durable queue가 아니므로 디스크에 적재하거나 재전송을 보장하지
않는다.

**LINGER** — 소켓을 닫을 때 미전송 메시지를 얼마나 기다릴지 정한다. 기본은 컨텍스트
상속(`BLOCKY=1`이면 `-1` = 무한 대기). `0`이면 즉시 닫고 미전송 폐기. SUB/XSUB는
생성 시 `0`으로 강제된다([12 소켓 옵션](./12-socket-options.ko.md)).

> 끊김에 강건해야 한다면: `DISCONNECTED`를 모니터링해 복구 로직을 돌리고, 중요한
> 데이터는 요청/응답으로 ACK를 받거나 응용 레벨 재전송 버퍼를 둔다.

---

## 5. 요청/응답 타임아웃 의미

`dealer.request()`·`spot.request*()`는 타임아웃을 받는다(기본 `5000ms`).

타임아웃이 발생하면:
- 결과가 `TIMED_OUT`으로 콜백/Future에 전달된다.
- **요청은 취소된다** — 그 이후로 더 전달을 시도하지 않는다.
- 그러나 **늦은 응답이 도착할 수 있다.** 요청이 이미 나갔다면 서버가 늦게 응답을
  보낼 수 있으므로, 응답 콜백만이 유일한 수신 경로라고 가정하지 않는다.
- **자동 재시도는 없다.** 타임아웃은 실패를 뜻하며, 재시도는 응용이 결정한다.
- **멱등성은 응용 책임이다.** 요청/응답은 중복 처리를 막지 않는다. 같은 요청이 두 번
  처리되면 안 되는 경우 응용이 dedup 키로 중복을 제거한다.

가능한 결과값: `OK` · `TIMED_OUT` · `NOT_FOUND`(피어 도달 불가) ·
`PROTOCOL_ERROR` · `TERMINATED` 등([03-3 DEALER](./03-3-dealer.ko.md)).

---

## 6. 서비스 계층(SPOT/Actor)의 전달

SPOT routed 평면과 Actor 메시징은 raw 소켓 위에 라우팅을 얹은 것이며, **raw 소켓
이상의 전달 보장을 추가하지 않는다.**

- SPOT routed 요청/응답은 raw 요청/응답과 같은 타임아웃 의미를 따른다(5절).
- Actor가 이동(다른 Spot으로 join/leave) 중인 메시지는 유실되거나 다른 Spot으로
  갈 수 있다. 활성 경로는 user Spot **join 성공 시점**에 게시된다([07-4
  Actor](./07-4-actor.ko.md)).
- SPOT 토픽 pub/sub은 raw PUB/SUB와 같은 특성(slow-joiner, 느린 구독자 흐름 제어)을
  가진다.

SPOT이 더해 주는 것은 전달 보장이 아니라 **실행 직렬성**(한 Spot의 메시지를 한 줄로
처리)과 **위치 투명성**이다([07-3 SPOT](./07-3-spot.ko.md)).

---

## 7. 정리 — 보장 / 비보장

**보장:**
- 멀티파트 메시지의 원자성(전체 또는 없음).
- 한 연결/피어 안에서의 순서.
- wire 무결성(ZMP 프레이밍, [ZMP 레퍼런스](./zmp-protocol.ko.md)).
- 흐름 제어 신호(`BACKPRESSURED`)의 명확성.

**비보장 (응용/인프라 책임):**
- HWM·재연결을 가로지르는 전달 — 흐름 제어와 복구는 응용이 한다.
- 영속(durability)·재전송 — durable queue가 필요하면 Kafka/NATS 등을 쓴다.
- 정확히 한 번(exactly-once)·멱등성 — 응용이 dedup한다.
- PUB/SUB의 slow-joiner·느린 구독자(기본은 drop 대신 backpressure지만, 여전히
  전달이 막힐 수 있음).

> 더 보기: [10 성능](./10-performance.ko.md)(HWM·튜닝) ·
> [12 소켓 옵션](./12-socket-options.ko.md)(LINGER·RECONNECT·HWM) ·
> [06 모니터링](./06-monitoring.ko.md)(연결 이벤트) ·
> [설계 근거](./design-rationale.ko.md)(왜 이런 모델인가).
