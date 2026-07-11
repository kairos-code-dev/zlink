<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Spot actor transfer](config-10-spot-actor-transfer.ko.md)
<!-- framework-adapter-nav:end -->

# Config 11 — 관측·운영 배포 (metrics · flow correlation · drain)

운영 중인 배포에서 새 관측·운영 표면 세 가지를 실제 배포 조건(공유 store·다중 노드·프로세스
경계)에서 검증한다. 세 표면의 계약은 각각 [메시지 흐름 상관관계](../spec/flow-correlation.ko.md),
[런타임 메트릭](../spec/runtime-metrics.ko.md), [Graceful Drain & Handoff](../spec/graceful-drain-handoff.ko.md)이
소유하고, 이 config는 그 계약이 배포 현장에서 의도대로 도는지를 본다.

기존 [Config 7 — Runtime Monitoring](config-7-monitoring.ko.md)이 socket/location/spot **이벤트
관찰**을 다룬다면, 이 config는 (1) 한 흐름을 노드 경계 너머로 잇는 **flow correlation 로그**,
(2) 밖에서 못 재는 신호의 **집계 메트릭**, (3) stateful 노드의 **우아한 종료·핸드오프**를 다룬다.

## 1. 목적과 범위

- 다룬다: `flow=` 로그가 STREAM→actor→spot 경계를 관통하는지, 메트릭 계기가 실제 사건과 일치하는지,
  drain이 draining 마커·핸드오프·SPOT 정책·강제 종료를 계약대로 수행하는지.
- 여기서 다루지 않는 것: 기능 자체의 messaging 정확성(다른 config), socket/location/spot 이벤트
  관찰(Config 7), 대시보드·exporter 구성(앱 몫, 공통 스펙 §6).

## 2. 서버 구성 (한 번 구동)

관측 대상 사건을 모두 만들려면 세션 게이트웨이 + 룸 + owner spot이 함께 있는 배포가 필요하다.
Bingo형 3역할에 owner-spot 서비스를 더한 구성을 쓴다.

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension. 실행마다 전용 key prefix. |
| `Session` | 1 | client STREAM endpoint, 인증, actor binding, packet relay. STREAM 세션이 CCU/재접속 계기의 소스. |
| `Play` | 2 (`play-a`, `play-b`) | room spot-mesh + player actor + transfer adapter. actor 이동·룸 타이머·bound push의 소스. 두 노드로 핸드오프·drain을 본다. |
| `OrderWorkflow` | 2 | event-sourcing owner spot(`OrderWorkflowSpot`). `release-and-recreate` drain 정책 검증용. |
| trigger client | 시나리오별 | STREAM 접속·게임 진행·주문 흐름·연결 해제를 유발한다. |

각 host는 공개 표면으로 세 기능을 켠다: `configureDispatch().flowId(...)`(flow), 앰비언트
meter/registry(metrics), spot mesh `useDrainPolicy(...)` + 자동 drain(drain). client 시나리오는
역할 server app의 HTTP/STREAM endpoint만 호출한다(README §코드 작성 규칙).

## 3. 실행 모델

`run_e2e.sh`가 Redis(전용 key prefix)를 준비하고 네 역할을 띄운 뒤, trigger client가 흐름을
유발한다. 검증은 세 경로로 한다.

- **flow 로그**: 모든 프로세스가 [README](README.ko.md) §6대로 `log/`에 파일 로그를 남기고, message
  flow를 `key_transitions` 이상 + `flowId(GlobalUnique)`로 켠다. `flow=`로 grep해 노드 간 흐름을 잇는다.
  파싱 부록 확정 전까지는 `flow=`/`corr=`/`label=`/`origin=` 토큰의 **바이트 동일**이 조인의 유일한
  계약이다(공통 flow-correlation §9).
- **메트릭**: 각 host가 테스트용 in-process reader(.NET `MeterListener` / C++ `zlink_metrics_reader_t`
  등)로 계기 스냅샷을 `/evidence`에 노출한다. 외부 exporter는 쓰지 않는다.
- **drain evidence**: drain lifecycle 이벤트와 location row 상태를 `/evidence`에 기록한다.

## 4. 시나리오

### Track A — flow correlation 로그

#### OBS-A1 flow가 STREAM→actor→room-spot을 관통

우선순위: `P0`

**한마디로:** client가 STREAM으로 보낸 한 요청이 actor relay와 room-spot 내부 dispatch까지 하나의 `flow=`로 이어지는가.

- 절차: trigger client가 `Session` STREAM으로 게임 액션(예: 카드 제출)을 보낸다. `Session`→`Play` actor relay→room-spot handler로 흐른다.
- 검증: 세 노드 로그를 모아 `grep flow=<id>` 하면 STREAM inbound(생성)→actor relay→room-spot 내부 dispatch가 시간순 한 줄로 이어진다. corr이 끊기는 spot 경계에서도 `flow=`가 유지된다(공통 §2). room-spot 내부 라인에는 corr이 없어도 flow가 있다.
- 세부 동작: spot/actor 경계 관통(공통 §4 홉 커버리지).

#### OBS-A2 error 라인에도 flow

우선순위: `P0`

**한마디로:** dispatch가 실패(핸들러 없음/디코드 실패)해도 그 error 라인에 `flow=`가 찍혀 성공·실패가 한 grep에 잡히는가.

- 절차: 알 수 없는 packet이나 잘못된 payload로 dispatch 실패를 유발한다.
- 검증: dispatch error 라인에 `flow=`가 있고, `grep flow=<id>`로 성공 라인과 실패 라인이 함께 잡힌다(공통 §4.3).
- 세부 동작: error reporter flow 기록.

#### OBS-A3 create-if-absent · off 노드 전파

우선순위: `P1`

**한마디로:** flow가 이미 있으면 재생성하지 않고, 트레이싱 off 노드를 지나도 흐름이 끊기지 않는가.

- 절차: (a) 진입점에서 생성된 flow가 하류 노드에서 그대로 유지되는지, (b) 중간 노드의 트레이싱을 `off`로 두고 흐름을 통과시킨다.
- 검증: (a) 하류 노드는 flow를 재생성하지 않는다(같은 id 유지). (b) off 노드는 새 flow를 시작하지 않지만 전파는 유지해, off 노드 이후 노드에서 같은 flow가 다시 나타난다(공통 §2.2).
- 세부 동작: create-if-absent + 전파 무조건.

#### OBS-A4 publish fan-out 트리 · timer 발원

우선순위: `P1`

**한마디로:** 한 흐름이 publish로 N 구독자에 갈라져도 같은 flow를 갖고, timer 발원 콜백은 새 flow를 시작하는가.

- 절차: (a) `OrderWorkflow`가 projection 갱신을 fanout publish하고 다수 구독자가 받는다. (b) room timer tick이 발생한다.
- 검증: (a) 구독자 N개 라인이 같은 flow_id를 갖는다(owner가 아니어서 skip한 라인 포함, 공통 §4.1). (b) timer 발원 콜백은 `origin=timer`로 새 flow를 시작한다(공통 §4.2).
- 세부 동작: fan-out 트리 + timer origin.

### Track B — 런타임 메트릭

#### OBS-B1 CCU·재접속 계기

우선순위: `P0`

**한마디로:** STREAM 세션이 붙고 끊길 때 `stream.connections.active`가 정확히 증감하고 재접속이 계수되는가.

- 절차: trigger client 여럿이 `Session`에 STREAM으로 접속했다 끊고, 일부는 재접속한다.
- 검증: `zlink.stream.connections.active`(updown)가 접속/종료에 정확히 증감하고, `zlink.stream.reconnects`가 재접속을 계수한다. `close_reason` 라벨이 닫힌 enum(`client_close`/`idle_timeout` 등)에 속한다.
- 세부 동작: STREAM 계기 정합(공통 §4.1).

#### OBS-B2 SPOT 큐·actor 이동 계기

우선순위: `P0`

**한마디로:** 룸 부하와 actor 노드 간 이동이 `spot.queue.depth`·`spot.callback.latency`·`actor.transfers`에 반영되는가.

- 절차: 룸에 부하를 주고(다수 액션), player actor를 `play-a`→`play-b`로 이동시킨다.
- 검증: `zlink.spot.queue.depth`/`callback.latency`가 `kind=user` 라벨로 계수되고, `zlink.actor.transfers`가 이동 완료 1회당 1회, `transfer.duration`이 out→commit ack 구간을 담는다. spot 계기는 `kind`(entry/user)로 분리된다(공통 §4.2/§4.3).
- 세부 동작: SPOT/actor 계기.

#### OBS-B3 fanout·lease 계기와 카디널리티

우선순위: `P1`

**한마디로:** fanout 발행/수신 차분, owner lease 갱신 지연이 계기로 잡히고, 고카디널리티 라벨이 붙지 않는가.

- 절차: `OrderWorkflow`가 이벤트를 fanout publish하고 다수 subscriber가 받는다. lease 갱신 지연은 내부 훅이 아니라 Redis 측 지연 주입(외부 인프라 조작, house rule 준수)으로 만든다.
- 검증: `zlink.fanout.published`/`received`가 1:N로 계수된다. `zlink.location.owner_lease.renew.lateness`가 갱신 지연을 기록한다. **어떤 계기에도 `correlation_id`/`flow_id`/`actor_id`/`spot_rid` 라벨이 붙지 않는다**(공통 §5).
- 세부 동작: fanout/lease 계기 + 카디널리티 규약.

#### OBS-B4 no-op 제로코스트

우선순위: `P1`

**한마디로:** meter/reader가 없으면 계기 갱신이 no-op으로 접히고 latency 타임스탬프 채취도 생략되는가.

- 절차: reader를 등록하지 않은 노드에서 트래픽을 흘린다.
- 검증: reader 미등록에서도 messaging 정확성이 불변이고, 장시간 트래픽에 계기 저장 공간이 상한 내로 유지된다(무한 적재 없음, 공통 §7.3). (핫패스 clock read 생략은 프로세스 밖 e2e로 관찰 불가한 구현 내부 속성이라 언어별 벤치/단위 테스트 RMETRIC-009가 소유한다 — 이 config에서 단언하지 않는다.)
- 세부 동작: off 제로코스트.

### Track C — Graceful Drain & Handoff

#### OBS-C1 draining 마커 — 연결 유지 + 배치 제외

우선순위: `P0`

**한마디로:** `play-a`를 drain하면 신규 배정에서만 빠지고 기존 연결·in-flight는 유지되는가.

- 절차: 룸과 bound actor가 살아 있는 `play-a`에 drain을 건다(자동 drain 또는 명시 `DrainAsync`).
- 검증: `play-a`에 draining 마커가 서고 `IsReady()`=false가 되어 신규 room/actor 배정에서 빠진다. `zlink.drain.state` gauge가 `Serving`→`Draining`으로 전이한다(공통 §9). 그러나 peer row는 삭제되지 않아 기존 연결이 유지되고, 전파 지연 창에 기존 연결로 온 request가 정상 처리된다(오류율 0, 공통 §3.1/§3.3). owner lease는 draining 동안 계속 갱신된다(공통 §3.2).
- 세부 동작: 마커 기반 배치 제외 + 연결 유지.

#### OBS-C2 actor 핸드오프 + bound session 연속성

우선순위: `P0`

**한마디로:** drain이 살아 있는 actor를 `play-b`로 이동시키고, 이동을 가로질러도 bound session이 이어지는가.

- 절차: `play-a` drain 중 bound actor가 `play-b`로 transfer된다.
- 검증: transfer가 [spot-actor §5.1](../spec/spot-actor.ko.md) 완료 조건까지 완주하고, location row가 `Takeover`로 `play-b`를 가리킨다. bound session push가 이동 후 `play-b` actor로 이어진다. `zlink.drain.actors.handed_off`가 계수되고, 이동 중 request의 orphan은 [spot-actor §10.5](../spec/spot-actor.ko.md) 계약이 닫힌 구현에서 0이다(공통 §5.2).
- 세부 동작: 핸드오프 + FIFO 연속성.

#### OBS-C3 SPOT 정책 — drain-natural vs release-and-recreate

우선순위: `P0`

**한마디로:** 룸 spot은 자연 종료로, event-sourcing owner spot은 release-and-recreate로 각각 다르게 비워지는가.

- 절차: (a) `play`의 room spot에 `drain-natural` 정책을 두고 drain한다. (b) `OrderWorkflow`의 owner spot에 `release-and-recreate` 정책을 두고 drain한다.
- 검증: (a) 신규 join만 막히고 진행 중 룸은 자연 종료될 때까지 유지된다. (b) owner spot row가 해제되고, 다음 요청이 타 노드에서 `GetOrCreate`로 event replay를 통해 상태를 재구성한다(공통 §5.1, ShoppingMall projection rebuild). `zlink.drain.rooms.drained`가 `policy` 라벨로 계수된다.
- 세부 동작: SPOT 정책별 종료.

#### OBS-C4 강제 종료 + 세션 종료 통지

우선순위: `P1`

**한마디로:** deadline을 넘기면 강제 종료로 넘어가고 활성 세션에 `server_drain` 종료 통지가 가는가.

- 절차: 핸드오프가 deadline 안에 못 끝나도록 짧은 deadline으로 drain한다.
- 검증: 상태가 `ForceStopping`으로 전이하고, 활성 STREAM 세션에 `close_reason=server_drain` 종료 통지가 통지 상한 내에 전달된다. 클라이언트 측은 connector의 `closeReason`(공통 drain §7.1)으로 이 사유를 읽어 확인한다. `zlink.drain.forced`가 계수되고, 통지가 프로세스 종료를 무한 지연시키지 않는다(공통 §7).
- 세부 동작: 강제 종료 경로.

#### OBS-C5 무중단 롤아웃 — 동시 drain 폴백

우선순위: `P1`

**한마디로:** 여러 노드가 동시에 drain이라 갈 곳이 없으면 핸드오프가 SPOT 정책으로 강등되는가.

- 절차: `play-a`와 `play-b`를 거의 동시에 drain해 eligible target을 0으로 만든다.
- 검증: 핸드오프 대상 선택이 draining peer를 제외하고, 대상이 없으면 `drain-natural`/`deadline` 폴백으로 강등된다(공통 §5.3). 정상 롤아웃(한 노드씩 drain)에서는 `ForceStopping` 없이 완주한다.
- 세부 동작: 동시 drain 폴백.

## 5. 완료 기준

- Track A `P0`(OBS-A1·A2), Track B `P0`(OBS-B1·B2), Track C `P0`(OBS-C1·C2·C3)는 모두 통과한다.
- flow 로그는 노드 경계를 관통하고 error 라인에도 `flow=`가 있다.
- 메트릭 계기는 실제 사건과 일치하고 고카디널리티 라벨이 없다.
- drain은 마커로 연결을 유지하며 배치만 제외하고, SPOT 정책별로 다르게 비워지며, owner lease를
  drain 동안 계속 갱신한다.
- 공개 표면만 직접 사용하고 `ensure`로 단언한다.
