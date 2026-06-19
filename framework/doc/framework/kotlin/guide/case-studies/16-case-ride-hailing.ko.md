<!-- framework-adapter-nav:start -->
[문서 목록](../../README.ko.md) | [이전: 케이스 — 실시간 멀티플레이 게임](15-case-realtime-game.ko.md) | [다음: 케이스 — 채팅·메시징 플랫폼](17-case-chat-messaging.ko.md)
<!-- framework-adapter-nav:end -->

# 케이스 — 라이드헤일링 실시간 dispatch

> [12-grpc-alternative](../12-grpc-alternative.ko.md)의 케이스 스터디 중 하나다.
> 대량 위치 fan-out + 지역(zone) 단위 매칭을 다루며, **geo-index·영속 이력은
> 그대로 남고** 라이브 전파·연결·매칭 직렬화가 ZLink 로 들어오는 사례다. 이 문서는
> 도입 판단과 책임 경계 설명을 맡고, 같은 "요청→배정→실시간 상태 push" 구조를
> 빌드·실행해 보는 샘플은 [DeliveryDispatch](#7-실행-가능한-샘플--deliverydispatch)다(§7).

> **이 케이스에서 ZLink 이 좋은 지점**
> - STREAM 이 위치 연결을 받고, pub/sub 가 다운스트림 fan-out 을 한다.
> - zone SPOT 이 지역 단위 배정을 직렬 처리해 배정 분산 락을 없앤다.
> - **그대로 남는 것**: 근접 질의(Redis GEO)와 위치 이력 영속(Kafka)은 그대로다.

## 1. 도메인 — 실시간 dispatch의 진짜 난제

- **고처리량 위치 ingestion.** 운전자 앱이 4–5초마다 위치를 보낸다. 500만 운전자면
  분당 ~100만 업데이트가 영속 연결로 쏟아진다.
- **지리 근접 질의.** "반경 2km 가용 운전자" 를 밀리초에 찾아야 한다 — **Redis
  geospatial 인덱스/H3 셀** 로 푼다.
- **다운스트림 fan-out.** 위치 스트림을 ETA·surge·analytics 가 동시에 구독한다.
- **배정의 일관성.** 한 운전자가 두 호출에 동시에 배정되면 안 된다 — 지역 단위
  **직렬 결정**이 필요하다.
  ([Uber-scale dispatch](https://dev.to/madhur_banger/architecting-an-uber-scale-real-time-tracking-dispatch-system-3a72))

남는 난제: **geo-index(Redis/H3)** 와 **위치 이력의 영속/replay(Kafka)** 는 그대로
남는다. ZLink 가 줄이는 건 연결 수용·라이브 fan-out transport·discovery 다.

## 2. 기존 스택 — WS ingestion + Kafka + Redis GEO

### 2.1 컴포넌트와 그 이유

| 컴포넌트 | 왜 필요한가 |
|----------|-------------|
| WS ingestion gateway | 운전자 앱의 영속 연결 수용 + 초당 대량 위치 수신 |
| Kafka | 위치 스트림을 **다운스트림 fan-out**(ETA·surge·analytics) + 이력 영속 |
| Redis GEO / H3 | 반경 근접 질의("2km 내 가용 운전자")를 밀리초에 |
| 분산 락(Redis 등) | 같은 운전자가 두 호출에 **이중 배정**되는 것을 막음 |
| dispatch service | ride 요청을 소비해 후보 운전자와 매칭 |
| service mesh / discovery | dispatch↔다른 서비스 위치 해결·분배 |

### 2.2 위치 ingestion + 다운스트림

```java
for (DriverLocation loc : locationReader.read(ws)) {
    kafka.send("driver.locations", loc);
    geoIndex.update(loc);
}
```

```java
kafkaConsumer.consume("driver.locations", loc -> etaIndex.update(loc));
```

### 2.3 매칭(dispatch)

```java
List<DriverCandidate> nearby = geoIndex.nearby(req.lat(), req.lng(), 2_000);
DriverCandidate chosen = dispatcher.choose(nearby);
try (DistributedLock ignored = locker.acquire("driver:" + chosen.driverId())) {
    rideStore.assign(req.riderId(), chosen.driverId());
}
```

서 있어야 하는 것: WS ingestion gateway, Kafka, Redis GEO, 분산 락, dispatch
service, 다운스트림 consumer(서비스마다), service discovery/mesh.

## 3. ZLink 스택 — STREAM + pub/sub + zone SPOT

```kotlin
@Component
class DriverSession(
    private val context: ZLinkSessionContext,
    private val feed: ZLinkFanoutClient,
    private val geo: GeoIndex,
) : ZLinkSuspendingSession() {
    override fun context(): ZLinkSessionContext = context

    override suspend fun onDispatchSuspending(header: ZLinkStreamHeader, payload: Message) {
        val loc = StreamPayloads.decode(header, payload, DriverLocation::class.java)
        geo.update(loc)
        feed.publish("loc.events", "driver.location", loc).submit().await()
    }
}
```

```kotlin
@Component
class AssignRideHandler(private val geo: GeoIndex) :
    ZLinkSuspendingSpotRequestHandler<ZoneSpot, AssignRide, RideAssigned> {
    override suspend fun handle(spot: ZoneSpot, req: AssignRide): RideAssigned {
        val candidates = geo.nearby(req.lat, req.lng, 2_000)
        return spot.assign(candidates, req.riderId)
    }
}
```

```kotlin
@Component
@ZLinkHandlerGroup("loc.events")
class EtaProjector(private val etaIndex: EtaIndex) :
    ZLinkSuspendingPublishHandler<DriverLocation> {
    override suspend fun handle(loc: DriverLocation, context: ZLinkPublishContext) {
        etaIndex.update(loc)
    }
}
```

```kotlin
@Component
class DriverSession(
    private val context: ZLinkSessionContext,
    private val feed: ZLinkFanoutClient,
    private val geo: GeoIndex,
) : ZLinkSuspendingSession() {
    override fun context(): ZLinkSessionContext = context

    override suspend fun onDispatchSuspending(header: ZLinkStreamHeader, payload: Message) {
        val loc = StreamPayloads.decode(header, payload, DriverLocation::class.java)
        geo.update(loc)
        feed.publish("loc.events", "driver.location", loc).submit().await()
    }
}
```

> **분산 락이 빠지는 이유.** 한 zone 의 배정 결정이 그 **zone SPOT 의 단일 실행
> 큐**에서 직렬로 돌기 때문에, 같은 운전자를 두 요청이 동시에 잡는 race 가 구조적으로
> 없다. 기존의 `driver:{id}` 분산 락이 사라진다(단, 운전자가 zone 경계를 넘나드는
> 설계는 spot 분할 정책으로 푼다).

## 4. 양쪽 코드 비교 — "위치 수신 + 배정"

| 축 | 기존(WS + Kafka + Redis) | ZLink |
|----|---------------------------|-------|
| 연결 수용 | WS ingestion gateway 직접 | STREAM `ZLinkSession` |
| 라이브 fan-out | Kafka produce/consume | `publish("loc.events", ...)` |
| 근접 질의 | Redis GEO | Redis GEO(유지) |
| 이중 배정 방지 | 분산 락 | zone SPOT 단일 실행 큐 |
| 위치 이력 영속 | Kafka 유지 | Kafka 유지 |

## 5. 아키텍처 비교 — 컴포넌트와 메시지 흐름

```text
[classic]  WS ingestion + Kafka + Redis GEO + dispatch

  +--------------+   +--------------+   +--------------+
  | WS ingestion |   | Kafka        |   | Redis GEO    |
  | gateway      |-->| (locations)  |   | + dist. lock |
  +--------------+   +------+-------+   +------+-------+
                            |                  |
              +-------------+------+    +-------v------+
              v             v      v    | dispatch     |
            ETA          surge  analytics+--------------+
  + service discovery / mesh
```

```text
[ZLink]  STREAM + fanout channel + zone SPOT

  +--------------+                     +--------------+
  | ingest server|   publish           | Redis GEO    |  (kept: geo query)
  | (STREAM)     |--> loc.events --+    +--------------+
  +--------------+                 |    +--------------+
                          +--------+-->| ETA/surge/.. |
                          v        v   +--------------+
                  +--------------+     +--------------+
                  | dispatch     |---->| zone SPOT    |  serial assign
                  +--------------+     +--------------+
   + Registry (location resolve)
  + Kafka (kept if location history needs persist)
```

- **빠지는 박스:** WS ingestion gateway, 라이브 fan-out용 Kafka 한 겹, 배정 분산 락,
  discovery/mesh.
- **그대로인 박스:** Redis GEO(근접 질의), Kafka(영속/replay 이력).

### 메시지 흐름 — 시퀀스 비교

위치 수신과 배정 흐름이다.

```mermaid
sequenceDiagram
%%{init: {'theme': 'base', 'themeVariables': {'signalTextColor': '#000000', 'actorTextColor': '#000000', 'noteTextColor': '#000000', 'actorBkg': '#ffffff', 'actorBorder': '#555555', 'activationBorderColor': '#555555'}}}%%
  autonumber
  participant D as driver
  participant I as ingest gw
  participant K as Kafka
  participant G as Redis GEO
  participant DS as dispatch
  D->>I: WS location
  I->>K: produce driver.locations
  I->>G: GEO add
  Note over DS: rider 요청 도착
  DS->>G: GEO nearby
  DS->>DS: 분산 락 획득 후 배정
```

```mermaid
sequenceDiagram
%%{init: {'theme': 'base', 'themeVariables': {'signalTextColor': '#000000', 'actorTextColor': '#000000', 'noteTextColor': '#000000', 'actorBkg': '#ffffff', 'actorBorder': '#555555', 'activationBorderColor': '#555555'}}}%%
  autonumber
  participant D as driver
  participant I as ingest STREAM
  participant G as Redis GEO
  participant Z as zone SPOT
  D->>I: STREAM location
  I->>G: GEO add
  I->>I: publish loc.events
  Note over Z: rider 요청 도착
  Z->>G: GEO nearby
  Z->>Z: zone SPOT 단일 큐 직렬 배정
```

라이브 fan-out용 Kafka 한 겹과 배정 분산 락이 빠진다. 근접 질의 Redis GEO 는 양쪽
모두 그대로다.

## 6. 줄어드는 것 / 그대로 남는 것

- **줄어드는 것:** 라이브 fan-out transport, 연결 수용 gateway, 배정 분산 락,
  discovery/mesh.
- **그대로 남는 것:** **geo-index(Redis/H3)** 와 **위치 이력 영속/replay(Kafka)**.
  ZLink 는 geo 질의나 영속 큐를 대신하지 않는다. 공통 경계는
  [12-grpc-alternative](../12-grpc-alternative.ko.md)의 §4 경계 절 참고.

## 7. 실행 가능한 샘플 — DeliveryDispatch

라이드헤일링과 같은 "요청을 만들고 수행자를 배정한 뒤 상태를 실시간으로 보여 주는"
구조를 작게 실행해 보는 샘플이 DeliveryDispatch(배송 배차)다. 외부 경계(고객 HTTP·
stream)는 그대로 두고, **내부 배차 메시징·상태 fanout·delivery 별 상태 참여**를 ZLink
역할 메시지로 구성한다.

- 구현 학습(deep-dive): [DeliveryDispatch Sample 문서](../samples/deliverydispatch-sample.ko.md)
- 실행 코드: [Kotlin DeliveryDispatch 샘플](../../../../../languages/java/samples/kotlin/DeliveryDispatch)
- 공통 시나리오(언어 중립): [spec/sample/deliverydispatch](../../../common/sample/deliverydispatch/README.ko.md)

### 서버 구성과 ZLink 요소

| 프로세스 | 책임 | ZLink 요소 |
|----------|------|------------|
| `DispatchApi` | 고객 HTTP `POST /deliveries` 수신 → `AssignDelivery` 전달 | client-server channel client |
| `DispatchCenter` | 배차 접수, 배송원 선택, timeout, 재배정 | channel server/client + dispatch worker |
| `Courier` | 제안 수락 또는 의도적 무응답(timeout) | channel server (`OfferDelivery`) |
| `Tracking` | 상태 event 기록, `DeliveryTrackingSpot`, status fanout publish | fanout publisher + Spot mesh + actor factory |
| `Session` | 고객 stream session, delivery subscription, 상태 push | stream node + fanout subscriber |

### 케이스 본문 너머로 이 샘플이 더 보여 주는 것

- **timeout 재배정을 1급 흐름으로**: 샘플은 `delivery-success` 와 `delivery-reassign`
  두 흐름을 **반드시** 포함한다. Courier A 를 `timeout-reassign` mode(성공 건은 수락,
  reassign 건만 무응답), Courier B 를 `accept` mode 로 띄워 무응답 → 재제안 → 수락이
  실제로 일어난다.
- **fanout 으로 고객까지 도달**: Tracking 이 `DeliveryStatusNotify` 를 fanout 으로
  publish 하고 Session 이 구독해 stream 으로 push 한다(케이스 §3 의 pub/sub fan-out).
- **`DeliveryTrackingSpot`**: 고객 actor 가 관심 delivery 에 join 하는 구조 — zone SPOT
  대신 delivery 단위 참여를 보여 준다.
- **codec**: 읽기 쉬운 JSON payload.

### client self-check 가 검증하는 의미

`delivery-success` 는 `Assigned → Accepted → PickedUp → Delivered` 가 순서대로,
`delivery-reassign` 은 `Assigned → Reassigned → Accepted → PickedUp → Delivered` 가
순서대로 도착하는지 확인하고, 재배정 건의 `Accepted`/`PickedUp`/`Delivered` 가 `courier-b`
처리임을 검증한다. 서버 evidence check 가 두 delivery 의 상태 순서를 누락 없이 기록한다.

## 8. 더 보기

- 케이스 허브: [12-grpc-alternative](../12-grpc-alternative.ko.md)
- 사용법: [04-channel-messaging](../04-channel-messaging.ko.md), [05-spot](../05-spot.ko.md), [07-stream](../07-stream.ko.md)
- 다음 케이스: [17-case-chat-messaging](17-case-chat-messaging.ko.md)

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](../../README.ko.md) | [이전: 케이스 — 실시간 멀티플레이 게임](15-case-realtime-game.ko.md) | [다음: 케이스 — 채팅·메시징 플랫폼](17-case-chat-messaging.ko.md)
<!-- framework-adapter-nav:bottom:end -->
