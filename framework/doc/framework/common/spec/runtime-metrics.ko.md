[스펙 목차](README.ko.md)

# 런타임 메트릭 계기 (Runtime Metrics Instruments)

> **상태: 제안(Proposed).** 이 문서는 [공개 계약 관리](public-contract-governance.ko.md)의 승격
> 절차를 아직 거치지 않은 제안 스펙이다. 계기 이름·라벨·단위는 승격 리뷰에서 확정되며, 확정
> 전까지 하위호환 대상이 아니다.

이 문서는 framework가 표준 기능으로 노출하는 **런타임 메트릭 계기**의 언어 중립 공통 스펙이다.
공통 의미(계기 카탈로그, 계기 종류, 라벨 규약, 이름 문법, 성능 계약, 수집 백엔드 경계)는 이
문서가 소유하고, 언어별 문서는 여기서 정한 의미를 자기 언어의 계기 API 표면으로만 구체화한다.
네이밍은 [framework API](framework-api.ko.md)와
[공개 계약 관리 §4](public-contract-governance.ko.md#4-언어별-표현-원칙)의 언어별 표현 원칙을 따른다.

이 문서는 [메시지 흐름 추적](message-flow-tracing.ko.md)(이하 **MFT**)과 **짝**을 이룬다. MFT가
"메시지 한 건이 dispatch 됐는가"(per-message 로그·이벤트)를 담당한다면, 이 문서는 "지금 이 노드
상태가 정상인가"(집계 시계열)를 담당한다. 둘은 같은 관측 철학 — **framework는 신호를 주고,
백엔드·대시보드는 앱이 끼운다** — 을 공유한다.

## 1. 목적과 성격

운영 중 가장 잦은 질문은 "이 노드가 지금 정상인가 / 어디가 밀리나 / 언제 스케일해야 하나"다. 그
답은 집계된 숫자 시계열(counter/gauge/histogram)로만 나오며, 그 숫자 중 상당수는 **프로세스 밖에서
측정이 불가능**하다.

핵심 원칙: **밖에서 못 재는 신호만 framework가 계기로 노출한다.**

- SPOT 단일 큐의 대기 깊이·콜백 지연은 그 SPOT을 소유한 프로세스만 안다.
- 살아있는 actor 수, STREAM 활성 연결 수(=CCU)는 런타임 내부 상태다.
- actor 이동(transfer) 횟수·소요, 이동 commit 시점 pending request 수는 framework만 관측한다.

이 기능은 dispatch **제어**가 아니라 **관측**이다. 계기 갱신 실패나 reader 예외가 dispatch
결과·성능을 바꾸면 안 된다(관측 callback 의미는 [비동기 실행 정책 §2](async-execution-policy.ko.md)를
따른다).

## 2. 추적·monitoring 이벤트와의 경계

framework는 이미 두 관측 표면을 가진다. 메트릭은 이 둘과 **다른 목적**이며, 셋의 경계를 먼저
잡는다.

| 표면 | 무엇 | 언제 |
|------|------|------|
| **메시지 흐름 추적**(MFT) | per-message 로그 라인(`corr=…`, phase) | "이 메시지가 dispatch 됐나" 디버깅 |
| **location/runtime 이벤트**([location-runtime §9](location-runtime.ko.md)) | 상태 변화 통지(`TopologyChanged`, `StoreUnavailable` 등) | "무엇이 바뀌었나" 알림 |
| **런타임 메트릭**(이 문서) | 집계 시계열(counter/gauge/histogram) | "지금 상태가 정상인가" 그래프·알람 |

**왜 이벤트/observer 훅으로 메트릭을 대체할 수 없나.** MFT의 `message_flow_observer`는 이벤트
스트림이라 일부 counter(예: `sent` 발생 수)는 파생할 수 있다. 그러나 다음은 이벤트 스트림에서
파생 불가라 별도 계기 표면이 필요하다.

- **gauge(현재값)** — `stream.connections.active`, `actor.count`처럼 "지금 몇 개"는 상태를 계속
  유지해야 얻는다. 이벤트 스트림만으로 재구성 불가.
- **histogram(분포)** — `spot.callback.latency` p99는 framework 내부 시각(큐 진입 시각)에 접근해야
  하며, 앱이 observer에서 직접 버킷팅해도 그 내부 시각을 알 수 없다.

단, location 이벤트의 polling diff는 일부 gauge(예: `location.peers`)의 갱신 소스로 **재사용**할
수 있다(§7.2). 이벤트와 메트릭이 같은 관찰 tick을 공유하는 것은 허용한다.

## 3. 계기 종류

| 종류 | 의미 | 예 |
|------|------|-----|
| `counter` | 단조 증가 누계 | 요청 수, 타임아웃 수, 재접속 수 |
| `updown` (up/down counter형 gauge) | 이벤트 시점 atomic inc/dec로 유지하는 현재값 | 활성 연결 수, 살아있는 actor/spot 수, 큐 깊이 |
| `observable` (관찰형 gauge) | reader tick/scrape 시점에만 스냅샷을 읽는 현재값 | 발견된 peer 수, lease 잔여 |
| `histogram` | 분포(분위수·버킷) | 요청 지연, 콜백 대기시간, transfer 소요 |

> gauge를 `updown`과 `observable`로 나누는 이유는 성능·정합 계약이 다르기 때문이다(§7). `updown`은
> 이벤트 경로에서 정확히 증감하고, `observable`은 polling 지연을 허용한다.

## 4. 계기 카탈로그

### 4.0 이름 문법 (계약)

- 계기 이름은 `zlink.<surface>.<name>` 규약을 쓴다.
- **계기 이름·라벨 키는 언어 간 바이트 동일**하다(§9, [X-1]). 이는 와이어/백엔드 식별자이므로
  API 타입·메서드에 적용하는 케이싱 변환([01-overview §6](../dotnet/guide/01-overview.ko.md)) 대상이
  아니다. Java에서 `zlinkActorCount`처럼 케이싱을 바꾸지 않는다.
- 접미사 규칙(대시보드 사고 방지): **counter=복수 명사 또는 과거분사**(`transfers`, `reconnects`,
  `timeouts`), **gauge=단수 명사 또는 상태값**(`active`, `count`, `depth`), **histogram=`.duration`
  또는 `.latency`**.
- 단위는 이 스펙이 **ms로 고정**한다(OTel semconv duration 권장 단위는 `s`이나, 이 스펙은 ms를
  택하고 초 변환은 브리지 몫으로 둔다 — "OTel 관례"를 근거로 삼지 않는다).

### 4.1 STREAM (외부 client 엣지)

| 계기 | 종류 | 단위 | 의미 |
|------|------|------|------|
| `zlink.stream.connections.active` | updown | `{connection}` | 현재 활성 세션 수 = **CCU** |
| `zlink.stream.connections.opened` | counter | `{connection}` | 누적 접속 수 |
| `zlink.stream.connections.closed` | counter | `{connection}` | 누적 종료 수(`close_reason` 라벨) |
| `zlink.stream.reconnects` | counter | `{event}` | 재접속 수(스파이크=사고 신호) |
| `zlink.stream.handshake.duration` | histogram | `ms` | TLS/WS handshake 소요(`transport` 라벨) |
| `zlink.stream.handshake.failures` | counter | `{failure}` | handshake 실패(`transport`, `reason` 라벨) |
| `zlink.stream.session.bind.duration` | histogram | `ms` | 세션↔actor 바인딩 소요(§4.7 구간 정의) |
| `zlink.stream.inbound.bytes` / `outbound.bytes` | counter | `By` | 수신·송신 바이트(`transport` 라벨) |

`close_reason`은 **닫힌 enum**이다: `client_close`, `idle_timeout`, `heartbeat_timeout`,
`server_drain`, `protocol_error`, `transport_error`. (heartbeat/idle 종료는 STREAM 실제 표면이다.)

### 4.2 SPOT (room/stage/zone)

| 계기 | 종류 | 단위 | 의미 |
|------|------|------|------|
| `zlink.spot.count` | updown | `{spot}` | 노드당 살아있는 SPOT 수(`kind` 라벨) |
| `zlink.spot.queue.depth` | updown | `{item}` | SPOT 단일 큐 대기 콜백 수(`kind` 라벨) |
| `zlink.spot.callback.latency` | histogram | `ms` | 큐 진입→실행 완료 대기시간 |
| `zlink.spot.timer.tick.lateness` | histogram | `ms` | timer tick 예정 대비 실제 지연 |
| `zlink.spot.created` / `zlink.spot.closed` | counter | `{spot}` | 생성·소멸 누계 |

`kind` 라벨은 [location-runtime §2.0](location-runtime.ko.md)의 `SpotKind`(`entry`/`user`)를 쓴다.
Entry Spot은 노드당 상주라 "룸 수"가 아니다 — **user spot 수 ≈ 룸 수**이고, Entry Spot 큐 깊이는
매치메이킹/배정 병목의 신호다.

> `spot.callback.latency` p99가 게임 루프 tick 주기를 넘으면 **그 룸이 밀리는 중**이다. SPOT은
> 직렬 실행이라 이 지표가 룸 체감 렉의 선행 신호다.

### 4.3 actor

| 계기 | 종류 | 단위 | 의미 |
|------|------|------|------|
| `zlink.actor.count` | updown | `{actor}` | 살아있는 actor 수 |
| `zlink.actor.mailbox.depth` | updown | `{item}` | mailbox 대기 메시지 수 |
| `zlink.actor.transfers` | counter | `{transfer}` | 노드 간 이동 완료 수(폭증=리밸런싱 폭풍) |
| `zlink.actor.transfer.duration` | histogram | `ms` | 이동 소요(out→commit ack, [spot-actor §5.1](spot-actor.ko.md)) |
| `zlink.actor.transfer.request.orphaned` | counter | `{request}` | 이동 commit 시점 pending이던 request 수 |

`transfer.request.orphaned`의 의미는 **"유실 확정 수"가 아니라 "이동 commit 시점 pending request
수(유실 가능성의 상한)"**다. 계수 지점은 source node가 actor를 moving으로 표시하는 시점
([spot-actor §4/§5](spot-actor.ko.md) `mark actor dispatch as moving`)이다.

> **버전 캐비앗.** [spot-actor §10.5](spot-actor.ko.md)는 이동을 가로지른 request의 reply
> correlation·timeout **보존을 계약으로 요구**한다. 즉 이 계약을 만족하는 구현·바인딩에서는 이
> 계기가 상시 0에 수렴해야 한다(그 자체가 회귀 검증, RMETRIC-014). 특정 바인딩 버전에서 이 갭이
> 열려 있으면([.NET feature-map §5](../dotnet/guide/11-feature-map.ko.md)) 이 계기가 그 영향도의
> 실측 창이 된다. 언어 중립 스펙은 이 계기의 근거를 특정 .NET 버전 제약에 묶지 않는다.

### 4.4 channel / route mesh (서버 간)

| 계기 | 종류 | 단위 | 의미 |
|------|------|------|------|
| `zlink.channel.request.duration` | histogram | `ms` | request→reply 왕복 지연 |
| `zlink.channel.request.inflight` | updown | `{request}` | 응답 대기 중 request 수 |
| `zlink.channel.request.timeouts` | counter | `{request}` | 타임아웃 누계 |
| `zlink.channel.messages.dropped` | counter | `{message}` | 폐기 누계(`surface`, `kind`, `reason` 라벨) |

`reason` 라벨은 error reporter의 dropped 사유와 정합한다(닫힌 집합): `no_handler`, `decode_error`,
`backpressure`, `stale_route`.

### 4.4b fanout (pub/sub)

| 계기 | 종류 | 단위 | 의미 |
|------|------|------|------|
| `zlink.fanout.published` | counter | `{message}` | publish 발행 수(`topic` 라벨, §5 닫힌 집합 규칙) |
| `zlink.fanout.received` | counter | `{message}` | subscriber 수신 수(`topic` 라벨) |
| `zlink.fanout.dropped` | counter | `{message}` | SUB HWM overflow 등 유실(core 관측 가능 시) |

> `published`와 `received`의 노드 간 차분이 [GameQuest §4](../dotnet/guide/samples/gamequest-sample.ko.md)
> 의 "fanout event 누락 보정" 시나리오의 유일한 실측 창이다.

### 4.5 location

| 계기 | 종류 | 단위 | 의미 |
|------|------|------|------|
| `zlink.location.peers` | observable | `{peer}` | 발견된 peer 수(급감=클러스터 분열) |
| `zlink.location.store.errors` | counter | `{error}` | store 조회/등록 실패 누계 |
| `zlink.location.owner_lease.renew.failures` | counter | `{failure}` | owner lease 갱신 실패 |
| `zlink.location.owner_lease.renew.lateness` | histogram | `ms` | 갱신 예정 대비 지연 |
| `zlink.location.write.conflicts` | counter | `{write}` | `RejectedConflict`/`IgnoredStale` 발생(스플릿 브레인/이중 owner 신호) |

> lease 갱신 지연이 TTL(기본 15s)의 2/3를 넘으면 **그 owner의 전 row가 stale 임박**이다 — 가장
> 치명적인 선행 신호이므로 `renew.lateness`를 1급으로 둔다([location-runtime §2.5](location-runtime.ko.md)).

### 4.6 관측 인프라 자체

| 계기 | 종류 | 단위 | 의미 |
|------|------|------|------|
| `zlink.observability.observer.overflow` | counter | `{event}` | observer offload bounded queue overflow drop 수(MFT §3 계약) |

### 4.7 다른 스펙이 소유하는 계기 (참조)

계기 카탈로그의 **소유 모델**: 이 문서는 **이름 문법·종류·라벨·성능 계약을 소유**하고, surface별
동작 스펙이 자기 계기를 정의한다. 아래는 타 스펙 소유 계기의 참조 등재다([X-3]).

| 계기 | 소유 스펙 |
|------|-----------|
| `zlink.drain.*`(`duration`/`actors.handed_off`/`rooms.drained`/`forced`) | [graceful drain & handoff](graceful-drain-handoff.ko.md) §9 |

**`session.bind.duration` 구간 정의**: 시작=STREAM 세션이 actor bind를 요청한 시점, 끝=
[session-actor-dispatch](session-actor-dispatch.ko.md)의 bind 완료(actor location row 확정 +
bound session route 활성화).

## 5. 라벨(attribute) 규약 — 닫힌 집합 규칙

라벨은 **등록 시점에 닫힌 집합**만 붙인다. 런타임에 생성되는 값은 시계열 폭발을 일으켜 수집
백엔드를 망가뜨린다.

| 허용(등록 시점 닫힌 집합) | 금지(런타임 생성 값) |
|---------------------------|----------------------|
| `channel`(등록된 channel명), `packet_name`(핸들러 등록으로 닫힘), `topic`(선언된 topic), `surface`, `kind`, `transport`, `close_reason`, `reason`, `node_label` | `correlation_id`, `flow_id`, `actor_id`, `spot_rid`, 동적 생성 topic, 사용자 ID |

> **판정 기준은 "닫혔는가"이지 "무엇인가"가 아니다.** `topic`도 앱이 room id를 인코딩해 동적으로
> 만들면 금지 대상이다 — 선언된 topic 집합만 허용한다. `packet_name`은 핸들러 등록으로 닫힌
> 집합이므로 허용한다.
>
> 개별 흐름 식별은 메트릭이 아니라 **추적**의 몫이다. 메트릭에서 `actor_id`로 per-actor 시계열을
> 만들지 않는다 — 그 조인은 흐름 로그의 `flow_id`(있으면)/`corr`로 한다
> ([flow correlation](flow-correlation.ko.md) 채택 시 조인 상위 키는 `flow_id`).

## 6. 수집 백엔드 경계

경계 원칙의 정본은 [MFT §6](message-flow-tracing.ko.md)이다. 이 문서는 그 원칙을 반복하지 않고
**메트릭 특화 차분**만 정의한다. **framework는 특정 메트릭 백엔드/SDK에 하드 의존하지 않는다.**

**framework가 제공 (백엔드 무관, 의존성 0):**

1. **계기 카탈로그** — §4의 계기를 표준 이름·종류·단위로 갱신.
2. **meter provider 주입 접점** — 앱이 자기 meter provider를 주입하면 framework가 그 provider로
   계기를 만든다(미주입 시 no-op provider).
3. **metrics reader 훅**(선택) — `observable` 계기 pull과 내장 스냅샷 회수용.

**애플리케이션이 선택 (framework 밖):**

- OTel `MeterProvider` / Micrometer `MeterRegistry` / Prometheus scrape 브리지. **framework는 내장
  OTLP/Prometheus scrape 서버를 제공하지 않는다.**
- 대시보드·알람은 전적으로 앱/운영의 몫.

## 7. 성능 계약

### 7.1 gauge 2부류

- **`updown`** — 이벤트 시점(연결 open/close, actor create/destroy, 큐 enqueue/dequeue)에 atomic
  inc/dec. 이벤트 경로에 이미 있는 지점이라 추가 비용이 원자 연산 하나다.
- **`observable`** — reader tick/scrape 시점에만 스냅샷 계산. 상시 계산하지 않으며 polling 지연을
  허용한다(§2 location 이벤트 tick 재사용 가능).

### 7.2 off/no-op 제로코스트

- provider 미주입 시 계기 호출은 no-op provider로 분기 하나로 접힌다.
- **histogram의 타임스탬프 채취도 게이트 뒤로 미룬다.** 계기가 비활성이면 `spot.callback.latency`용
  큐-진입 시각 clock read를 **생략**한다. 이 요구가 없으면 "no-op=제로코스트" 주장이 계측 시각
  채취 비용을 숨기게 된다(RMETRIC-009). MFT §4의 lazy 이벤트 생성과 동형이다.
- counter/histogram 갱신 실패·reader 예외는 dispatch를 깨지 않는다.

### 7.3 히스토그램 버킷 소유권

framework는 **기록만** 하고 aggregation·버킷 경계는 provider 책임이다. 내장 no-op/테스트 reader의
기본 버킷 경계는 계기별 부록 표로 고정한다(승격 시 확정). 저장 공간은 상한 내로 유지한다(reader
미등록 상태에서도 무한 적재 금지, RMETRIC-010).

## 8. 길목 (hook point)

계기별 갱신 지점을 논리 지점 + C++ 레퍼런스 파일로 고정한다(MFT §7 형식). 특히
`spot.callback.latency`는 "큐 진입 시각 기록"을 요구하므로 채취 지점을 명시해야 성능 계약이
검증된다.

| 계기 | 논리 지점 | C++ 레퍼런스(예상) |
|------|-----------|--------------------|
| `stream.connections.active`/`opened`/`closed` | session accept/close | `streams/stream_runtime.cpp` |
| `spot.queue.depth`, `spot.callback.latency` | SPOT 큐 enqueue(시각 stamp)·dequeue 완료 | `spots/spot_runtime.cpp` |
| `actor.transfers`, `transfer.duration`, `transfer.request.orphaned` | moving 표시·commit ack | `actors/actor_gateway_runtime.cpp` |
| `channel.request.*` | client request 제출·reply 수신 | `channels/channel_runtime.cpp` |
| `fanout.published`/`received` | publish 제출·subscription dispatch | `spots/spot_runtime.cpp` |
| `location.*` | reconcile tick·lease renew | (location runtime) |

## 9. 구현 상태

이 문서는 언어별 구현 진행률을 기록하지 않는다. 언어별 계기 표면과 현재 차이는
[언어별 구현 차이](implementation-gap.ko.md)에 기록한다. observer/reader를 외부 수집기나 OTel
adapter에 연결하는 일은 application 또는 별도 extension이 담당한다.

## 10. 회귀 테스트 매트릭스 (RMETRIC)

| ID | 검증 |
|----|------|
| RMETRIC-001 | provider 미주입 시 계기 호출이 no-op이고 dispatch 결과·성능 불변 |
| RMETRIC-002 | `stream.connections.active`가 접속/종료에 정확히 증감(updown 정합) |
| RMETRIC-003 | `spot.callback.latency`가 큐 진입→완료 구간을 담고 분위수 산출 |
| RMETRIC-004 | `actor.transfers`/`transfer.duration`이 out→commit ack 완료 1회당 1회 기록 |
| RMETRIC-005 | `actor.transfer.request.orphaned`가 moving 표시 시점 pending request와 일치 |
| RMETRIC-006 | `channel.request.duration`/`timeouts`가 왕복·타임아웃과 일치 |
| RMETRIC-007 | 고카디널리티 라벨(correlation_id/actor_id/spot_rid) 미부착 규약 준수 |
| RMETRIC-008 | reader가 주기 스냅샷을 pull하고 reader 예외가 dispatch를 깨지 않음 |
| RMETRIC-009 | 계기 비활성(off/no-op) 시 latency 히스토그램용 타임스탬프 채취가 생략됨(핫패스 clock read 없음) |
| RMETRIC-010 | reader 미등록 장시간 트래픽에도 계기 저장 공간이 상한 내(무한 적재 없음) |
| RMETRIC-011 | `fanout.published`/`received`가 publish 1회·구독자 N 수신에서 1:N 계수 |
| RMETRIC-012 | `spot.count`/`queue.depth`가 `kind`(entry/user) 라벨로 분리 계수 |
| RMETRIC-013 | owner lease 갱신 실패/`write.conflicts`가 해당 counter에 계수되고 dispatch에 영향 없음 |
| RMETRIC-014 | [spot-actor §10.5](spot-actor.ko.md) 계약을 만족하는 구현에서 `transfer.request.orphaned`가 상시 0에 수렴 |
| RMETRIC-015 | observer offload bounded queue overflow 시 drop + `observer.overflow` 증가(MFT §3 정합) |

## 11. 언어별 투영

각 언어는 이 카탈로그를 자기 계기 API로 내려 적는다. 계기 **이름·라벨 키**는 바꾸지 않고 provider
연결 표면만 언어화한다.

| 언어 | 표면 |
|------|------|
| `.NET` | `System.Diagnostics.Metrics.Meter` + `MeterListener`; `AddZLinkFramework`에서 provider 주입, OTel `MeterProvider`로 브리지 |
| Java/Kotlin | Micrometer `MeterRegistry`; Spring Boot starter가 registry bean 주입 |
| Node | OpenTelemetry Metrics API `Meter`; NestJS `ZLinkModule.forRoot`에서 provider 주입 |
| C++ (레퍼런스) | 자체 instrument 추상 + `metrics_reader_t` pull 인터페이스 |

---

> 관련: [메시지 흐름 추적](message-flow-tracing.ko.md) · [메시지 흐름 상관관계](flow-correlation.ko.md) ·
> [graceful drain & handoff](graceful-drain-handoff.ko.md) · [location runtime](location-runtime.ko.md)
