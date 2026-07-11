<!-- draft-status: DRAFT · 제안 단계 · 공개 계약 아님 -->

[스펙 목차](../README.ko.md)

# 런타임 메트릭 계기 (Runtime Metrics Instruments) — DRAFT

> **상태: DRAFT.** 이 문서는 아직 채택된 공개 계약이 아니라 제안 초안이다. 계기 이름·라벨·
> 단위는 리뷰 과정에서 바뀔 수 있다. 확정 전에는 어떤 이름도 하위호환 대상이 아니다.

이 문서는 framework가 표준 기능으로 노출하는 **런타임 메트릭 계기**의 언어 중립 공통 스펙
초안이다. 공통 의미(계기 카탈로그, 계기 종류, 라벨 규약, 수집 백엔드 경계, 성능 계약)는 이
문서가 소유하고, 언어별 문서는 여기서 정한 의미를 자기 언어의 계기 API 표면으로만 구체화한다.

이 문서는 [메시지 흐름 추적](../message-flow-tracing.ko.md)과 **짝**을 이룬다. 추적이 "메시지
한 건이 dispatch 됐는가"(per-message 로그)를 담당한다면, 이 문서는 "지금 이 노드 상태가
정상인가"(집계 시계열)를 담당한다. 둘은 같은 관측 철학(백엔드 무의존 + observer/reader 훅)을
공유한다.

## 1. 목적과 성격

운영 중 가장 잦은 질문은 "이 노드가 지금 정상인가 / 어디가 밀리나 / 언제 스케일해야 하나"다.
그 답은 **집계된 숫자 시계열(counter/gauge/histogram)**로만 나온다. 그리고 이 숫자 중 상당수는
**프로세스 밖에서 측정이 불가능**하다.

핵심 원칙: **밖에서 못 재는 신호만 framework가 계기로 노출한다.**

- SPOT 단일 큐의 대기 깊이·콜백 지연은 그 SPOT을 소유한 프로세스만 안다.
- 살아있는 actor 수, STREAM 활성 연결 수(=CCU)는 런타임 내부 상태다.
- actor 이동(transfer) 횟수·소요, 이동 중 유실 request 수는 framework만 관측한다.

이 기능은 dispatch **제어**가 아니라 **관측**이다. 계기 갱신이 실패하거나 reader가 없어도
framework 기본 동작과 성능은 변하지 않는다.

## 2. 추적과의 경계 — 왜 observer 훅으로 대체할 수 없나

[메시지 흐름 추적](../message-flow-tracing.ko.md)의 `message_flow_observer`는 **이벤트 스트림**을
준다. 여기서 일부 counter(예: `sent` 발생 수)는 파생할 수 있다. 그러나 다음은 이벤트 스트림에서
파생할 수 없어 **별도 계기 표면이 필요**하다.

- **gauge(현재값)** — `stream.connections.active`, `actor.count`, `spot.count` 같은 "지금 몇 개"는
  상태를 계속 유지해야 얻는다. 이벤트 스트림만으로는 재구성 불가.
- **histogram(분포)** — `spot.callback.latency` p99 같은 분위수는 버킷 집계가 필요하다. 앱이
  observer에서 직접 버킷팅하면 framework 내부 시각(큐 진입 시각 등)에 접근하지 못한다.

그래서 메트릭은 추적과 다른 1급 표면으로 둔다.

## 3. 계기 종류

| 종류 | 의미 | 예 |
|------|------|-----|
| `counter` | 단조 증가값(누계) | 요청 수, 타임아웃 수, 재접속 수 |
| `gauge` (up/down) | 현재 스냅샷값 | 활성 연결 수, 살아있는 actor 수, 큐 깊이 |
| `histogram` | 분포(분위수·버킷) | 요청 지연, 콜백 대기시간, transfer 소요 |

## 4. 계기 카탈로그 (초안)

> 이름은 `zlink.<surface>.<metric>` 규약을 따른다. 단위는 OTel 관례(`{connection}`, `ms`,
> `By`)를 쓴다. 아래는 제안 목록이며 surface별 최소 셋이다.

### 4.1 STREAM (외부 client 엣지)

| 계기 | 종류 | 단위 | 의미 |
|------|------|------|------|
| `zlink.stream.connections.active` | gauge | `{connection}` | 현재 활성 세션 수 = **CCU** |
| `zlink.stream.connections.opened` | counter | `{connection}` | 누적 접속 수 |
| `zlink.stream.connections.closed` | counter | `{connection}` | 누적 종료 수(사유 라벨) |
| `zlink.stream.reconnect` | counter | `{event}` | 재접속 수(스파이크=사고 신호) |
| `zlink.stream.session.bind.duration` | histogram | `ms` | 세션↔actor 바인딩 소요 |
| `zlink.stream.inbound.bytes` | counter | `By` | 수신 바이트(transport 라벨) |

### 4.2 SPOT (room/stage/zone)

| 계기 | 종류 | 단위 | 의미 |
|------|------|------|------|
| `zlink.spot.count` | gauge | `{spot}` | 노드당 살아있는 SPOT 수(=룸 수) |
| `zlink.spot.queue.depth` | gauge | `{item}` | SPOT 단일 큐 대기 콜백 수 |
| `zlink.spot.callback.latency` | histogram | `ms` | 큐 진입→실행 완료 대기시간 |
| `zlink.spot.timer.tick.lateness` | histogram | `ms` | timer tick 예정 대비 실제 지연 |
| `zlink.spot.created` / `zlink.spot.closed` | counter | `{spot}` | 생성·소멸 누계 |

> `spot.callback.latency` p99가 게임 루프 tick 주기를 넘으면 **그 룸이 밀리는 중**이다. SPOT은
> 직렬 실행이라 이 지표가 곧 룸 체감 렉의 선행 신호다.

### 4.3 actor

| 계기 | 종류 | 단위 | 의미 |
|------|------|------|------|
| `zlink.actor.count` | gauge | `{actor}` | 살아있는 actor 수 |
| `zlink.actor.mailbox.depth` | gauge | `{item}` | mailbox 대기 메시지 수 |
| `zlink.actor.transfer.count` | counter | `{transfer}` | 노드 간 이동 횟수(폭증=리밸런싱 폭풍) |
| `zlink.actor.transfer.duration` | histogram | `ms` | 이동 소요(out→in 완료) |
| `zlink.actor.transfer.inflight_request.dropped` | counter | `{request}` | **이동 중 유실된 request 수** |

> 마지막 계기는 [feature-map §5](../languages/dotnet/../../../dotnet/guide/11-feature-map.ko.md)의
> 알려진 제약(이동 중 request가 caller에 재연결되지 않고 timeout)의 **실측 창**이다. 그 갭이
> 닫히기 전까지 이 계기로 영향도를 관측한다.

### 4.4 channel / route mesh (서버 간)

| 계기 | 종류 | 단위 | 의미 |
|------|------|------|------|
| `zlink.channel.request.duration` | histogram | `ms` | request→reply 왕복 지연 |
| `zlink.channel.request.inflight` | gauge | `{request}` | 응답 대기 중 request 수 |
| `zlink.channel.request.timeout` | counter | `{request}` | 타임아웃 누계 |
| `zlink.channel.message.dropped` | counter | `{message}` | 폐기 누계(surface/kind 라벨) |

### 4.5 location

| 계기 | 종류 | 단위 | 의미 |
|------|------|------|------|
| `zlink.location.peers` | gauge | `{peer}` | 발견된 peer 수(급감=클러스터 분열) |
| `zlink.location.store.errors` | counter | `{error}` | store 조회/등록 실패 누계 |
| `zlink.location.refresh.duration` | histogram | `ms` | peer row 갱신 소요 |

## 5. 라벨(attribute) 규약 — 카디널리티 규율

라벨은 **저(低)카디널리티만** 붙인다. 고카디널리티 라벨은 시계열 폭발을 일으켜 수집 백엔드를
망가뜨린다.

| 허용(저카디널리티) | 금지(고카디널리티) |
|--------------------|---------------------|
| `channel`, `topic`, `surface`, `kind`, `transport`(tcp/tls/ws/wss), `close_reason`, `node_label` | `correlation_id`, `actor_id`, `spot_rid`, `packet_name`(무제한일 때), 사용자 ID |

> 개별 흐름 식별은 메트릭이 아니라 **추적(correlation_id)**의 몫이다. 메트릭에서 `actor_id`로
> per-actor 시계열을 만들면 안 된다. 그건 추적 로그로 조인한다.

## 6. 수집 백엔드 경계 (중요 원칙)

[메시지 흐름 추적 §6](../message-flow-tracing.ko.md)과 동일한 경계를 지킨다. **framework는 특정
메트릭 백엔드/SDK에 하드 의존하지 않는다.**

**framework가 제공 (백엔드 무관, 의존성 0):**

1. **계기 카탈로그** — §4의 계기를 표준 이름·종류·단위로 갱신한다.
2. **metrics reader 훅** — `set_metrics_reader(reader)` 류. reader가 주기적으로(또는 scrape 시)
   계기 스냅샷을 pull 한다. 로거 sink 주입과 같은 패턴이다.
3. **instrument provider 어댑터 접점** — 앱이 자기 meter provider를 주입하면 framework가 그
   provider로 계기를 만든다(없으면 no-op provider).

**애플리케이션이 선택 (원할 때만, framework 밖):**

- **OTel / Micrometer / Prometheus 브리지** — 앱이 자기 SDK의 meter provider를 주입하거나 reader
  콜백에서 스냅샷을 exporter로 밀어 넣는다. **framework는 내장 OTLP/Prometheus scrape 서버를
  제공하지 않는다.**
- **대시보드·알람** — 전적으로 앱/운영의 몫. framework는 신호만 낸다.

> 요약: framework = 계기 카탈로그 + reader/provider 훅까지. 그 위 OTel/Prometheus/대시보드는 앱이
> 끼운다. 이 경계를 깨고 framework에 백엔드를 하드 의존시키지 않는다.

## 7. 성능 계약

- **counter/histogram** — 핫패스 갱신은 relaxed atomic 수준. reader가 없으면 갱신 비용만 남고
  export 비용은 0.
- **gauge** — pull 시점(scrape/reader tick)에만 스냅샷을 읽는다. 상시 계산하지 않는다.
- **provider 미주입** — no-op provider로 계기 호출이 분기 하나로 접힌다(사실상 제로코스트).
- 계기 갱신 실패·reader 예외는 dispatch를 깨지 않는다.

## 8. 회귀 테스트 매트릭스 (RMETRIC)

| ID | 검증 |
|----|------|
| RMETRIC-001 | provider 미주입 시 계기 호출이 no-op이고 dispatch 결과·성능 불변 |
| RMETRIC-002 | `stream.connections.active`가 접속/종료에 정확히 증감(gauge 정합) |
| RMETRIC-003 | `spot.callback.latency`가 큐 진입→완료 구간을 담고 분위수가 산출됨 |
| RMETRIC-004 | `actor.transfer.count`/`duration`이 out→in 완료 1회당 1회 기록 |
| RMETRIC-005 | `actor.transfer.inflight_request.dropped`가 이동 중 유실 request를 계수 |
| RMETRIC-006 | `channel.request.duration`/`timeout`이 왕복·타임아웃과 일치 |
| RMETRIC-007 | 고카디널리티 라벨(correlation_id/actor_id) 미부착 규약 준수 |
| RMETRIC-008 | reader가 주기 스냅샷을 pull하고 reader 예외가 dispatch를 깨지 않음 |

## 9. 언어별 투영

각 언어는 이 카탈로그를 자기 계기 API로 내려 적는다. 의미·이름·단위는 바꾸지 않고 케이싱·표면만
변환한다.

| 언어 | 표면 |
|------|------|
| `.NET` | `System.Diagnostics.Metrics.Meter` + `MeterListener`; `AddZLinkFramework`에서 provider 주입, OTel `MeterProvider`로 브리지 |
| Java/Kotlin | Micrometer `MeterRegistry`; Spring Boot starter가 `MeterRegistry` bean을 주입 |
| Node | OpenTelemetry Metrics API `Meter`; NestJS `ZLinkModule.forRoot`에서 provider 주입 |
| C++ (레퍼런스) | 자체 instrument 추상 + `metrics_reader_t` pull 인터페이스 |

---
<!-- draft-status: DRAFT · 제안 단계 · 공개 계약 아님 -->
