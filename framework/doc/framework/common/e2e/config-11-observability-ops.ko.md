<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Spot actor transfer](config-10-spot-actor-transfer.ko.md) |
[다음: Channel egress routing](config-12-channel-egress-routing.ko.md)
<!-- framework-adapter-nav:end -->

# Config 11 — 관측·운영 배포 (metrics · flow correlation · drain)

운영 중인 배포에서 새 관측·운영 표면 세 가지를 실제 배포 조건(공유 store·다중 노드·프로세스
경계)에서 검증한다. 세 표면의 계약은 각각 [메시지 흐름 상관관계](../../spec/server/53-flow-correlation.ko.md),
[런타임 메트릭](../../spec/server/51-runtime-metrics.ko.md), [Graceful Drain & Handoff](../../spec/server/54-graceful-drain-handoff.ko.md)이
소유하고, 이 config는 그 계약이 배포 현장에서 의도대로 동작하는지를 확인한다.

기존 [Config 7 — Runtime Monitoring](config-7-monitoring.ko.md)이 MeshNode의 snapshot과 typed runtime
event를 다룬다면, 이 config는 (1) 한 흐름을 노드 경계 너머로 잇는 **flow correlation 로그**,
(2) 밖에서 못 재는 신호의 **집계 메트릭**, (3) stateful 노드의 **우아한 종료·핸드오프**를 다룬다.

## 1. 목적과 범위

- 다룬다: `flow=` 로그가 STREAM→actor→spot 경계를 관통하는지, 메트릭 계기가 실제 사건과 일치하는지,
  drain이 draining 마커·핸드오프·고정된 Spot 종료 순서·강제 종료를 계약대로 수행하는지.
- 여기서 다루지 않는 것: 기능 자체의 messaging 정확성(다른 config), MeshNode runtime snapshot·event
  관찰(Config 7), 대시보드·exporter 구성(앱 몫,
  [Runtime Metrics §7](../../spec/server/51-runtime-metrics.ko.md#7-reader와-성능)).

## 2. 서버 구성 (한 번 구동)

관측 대상 사건을 모두 만들려면 세션 게이트웨이 + 룸 + owner spot이 함께 있는 배포가 필요하다.
Bingo형 3역할에 owner-spot 서비스를 더한 구성을 쓴다.

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension. 실행마다 전용 key prefix. |
| `Session` | 1 | client STREAM endpoint, 인증, actor binding, packet relay. STREAM 세션이 CCU/재접속 계기의 소스. |
| `Play` | 2 (`play-a`, `play-b`) | room MeshNode + player actor + transfer adapter. actor 이동·룸 타이머·bound push의 소스. 두 노드로 핸드오프·drain을 본다. |
| `OrderWorkflow` | 2 | event-sourcing owner Spot(`OrderWorkflowSpot`)과 projection fan-out의 소스. |
| trigger client | 시나리오별 | STREAM 접속·게임 진행·주문 흐름·연결 해제를 유발한다. |

각 host는 기존 message-flow 설정, 언어별 표준 meter/registry와 MeshNode runtime drain을 사용한다.
drain 중에는 새 ChannelName·Logical Multicast 선택에서 해당 membership을 제외한다. flow id는
message-flow가 켜진 발원점에서 자동 생성되며 별도 설정을
추가하지 않는다. client 시나리오는
역할 server app의 HTTP/STREAM endpoint만 호출한다(README §코드 작성 규칙).

## 3. 실행 모델

`run_e2e.sh`가 Redis(전용 key prefix)를 준비하고 네 역할을 시작한 뒤, trigger client가 흐름을
유발한다. 검증은 세 경로로 한다.

- **flow 로그**: 모든 프로세스가 [README](README.ko.md) §6대로 `log/`에 파일 로그를 남기고, message
  flow를 `key_transitions` 이상으로 켠다. `flow=`로 grep해 노드 간 흐름을 잇는다.
  파싱 부록 확정 전까지는 `flow=`/`corr=`/`label=`/`origin=` 토큰의 **바이트 동일**이 조인의 유일한
  계약이다(공통 flow-correlation §9).
- **메트릭**: 각 host가 언어 표준 in-process reader(.NET `MeterListener`, Java/Kotlin Micrometer
  test registry, Node.js OpenTelemetry test reader)를 연결한다. C++는 기존 `metric_event_payload_t`
  이벤트를 test collector가 집계한다. 계기 스냅샷은 `/evidence`에 노출하며 외부 exporter는 쓰지 않는다.
- **drain evidence**: drain lifecycle 이벤트와 authority projection 상태를 `/evidence`에 기록한다.

`/evidence` JSON은 언어와 무관하게 다음 최소 배열을 제공한다. metric snapshot은 시나리오 시작 직전
기준값과 사건 완료 뒤 값을 함께 저장해 counter delta와 current gauge를 구분한다.

```json
{
  "metrics": [{"name":"...","kind":"counter","value":1,"unit":"{event}","tags":{}}],
  "drainEvents": [{"sequence":1,"state":"draining","source":"drain"}],
  "peerRows": [{"nodeRid":"...","draining":true,"generation":1}]
}
```

`kind`는 `counter|updown|observable|histogram`, drain state label은
`serving|draining|drained|force_stopping` 소문자 값으로 고정한다. histogram은 raw sample 또는 provider
snapshot 중 어느 형식인지 runner가 함께 기록하고 같은 언어 실행 안에서 기준값과 비교한다.

## 4. 시나리오

### Track A — flow correlation 로그

#### OBS-A1 flow가 STREAM→actor→room-spot을 관통

우선순위: `P0`

**검증 질문:** client가 STREAM으로 보낸 한 요청이 actor relay와 room-spot 내부 dispatch까지 하나의 `flow=`로 이어지는가.

- 절차: trigger client가 `Session` STREAM으로 게임 액션(예: 카드 제출)을 보낸다. 메시지는
  `Session` → `Play` actor relay → room Spot handler 순서로 전달된다.
- 검증: trigger connector가 별도 설정 없이 `origin=application` flow를 생성한다. 세 노드 로그를 모아
  `grep flow=<id>` 하면 connector outbound→Session STREAM inbound(보존)→actor relay→room-spot 내부
  dispatch가 시간순으로 이어진다. corr이 없는 Spot 경계에서도 `flow=`가 유지된다
  ([Flow Correlation §2](../../spec/server/53-flow-correlation.ko.md#2-두-식별자)).
- 세부 동작: Spot·Actor 경계 관통
  ([Flow Correlation §5](../../spec/server/53-flow-correlation.ko.md#5-propagation)).

#### OBS-A2 error 라인에도 flow

우선순위: `P0`

**검증 질문:** dispatch가 실패(핸들러 없음/디코드 실패)해도 그 error 라인에 `flow=`가 찍혀 성공·실패가 한 grep에 잡히는가.

- 절차: 알 수 없는 packet이나 잘못된 payload로 dispatch 실패를 유발한다.
- 검증: dispatch error 라인에 `flow=`가 있고, `grep flow=<id>`로 성공 라인과 실패 라인이 함께
  확인된다([Flow Correlation §7](../../spec/server/53-flow-correlation.ko.md#7-reply와-failure)).
- 세부 동작: error reporter flow 기록.

#### OBS-A3 create-if-absent · off 노드 전파

우선순위: `P1`

**검증 질문:** flow가 이미 있으면 재생성하지 않고, 트레이싱 off 노드를 지나도 흐름이 끊기지 않는가.

- 절차: (a) 진입점에서 생성된 flow가 하류 노드에서 그대로 유지되는지, (b) 중간 노드의 트레이싱을 `off`로 두고 흐름을 통과시킨다.
- 검증: (a) 하류 노드는 flow를 재생성하지 않는다(같은 id 유지). (b) off 노드는 새 flow를 시작하지
  않지만 전파는 유지해, off 노드 이후 노드에서 같은 flow가 다시 나타난다
  ([Flow Correlation §4](../../spec/server/53-flow-correlation.ko.md#4-flow-생성)).
- 세부 동작: create-if-absent + 전파 무조건.

#### OBS-A4 publish fan-out 트리 · timer 발원

우선순위: `P1`

**검증 질문:** 한 흐름이 publish로 N 구독자에 갈라져도 같은 flow를 갖고, timer 발원 콜백은 새 flow를 시작하는가.

- 절차: (a) `OrderWorkflow`가 projection 갱신을 fanout publish하고 다수 구독자가 받는다. (b) room timer tick이 발생한다.
- 검증: (a) 구독자 N개 라인이 같은 flow ID를 갖는다
  ([Flow Correlation §5](../../spec/server/53-flow-correlation.ko.md#5-propagation)). (b) timer 발원
  callback은 `origin=timer`로 새 flow를 시작한다
  ([Flow Correlation §4](../../spec/server/53-flow-correlation.ko.md#4-flow-생성)).
- 세부 동작: fan-out 트리 + timer origin.

### Track B — 런타임 메트릭

#### OBS-B1 CCU·재접속 계기

우선순위: `P0`

**검증 질문:** STREAM 세션이 연결되고 해제될 때 `stream.connections.active`가 정확히 증감하고 재접속이 계수되는가.

- 절차: trigger client 여럿이 `Session`에 STREAM으로 접속했다 끊고, 일부는 재접속한다.
- 검증: 서버 reader의 `zlink.stream.connections.active`가 접속/종료에 정확히 증감한다. 재접속을
  수행한 trigger connector의 reader에서 `zlink.stream.reconnects`가 자동 재접속 시도마다 증가한다.
  서버는 새 연결과 재접속을 추측하지 않는다. `close_reason` 라벨은 닫힌 enum에 속한다.
- 세부 동작: 서버 연결 계기는 [Runtime metrics §4](../../spec/server/51-runtime-metrics.ko.md#4-object와-stream-계기),
  connector 재접속 계기는 [Stream Connector §6.2](../../spec/stream-connector/32-stream-connector.ko.md#62-connector-reconnect-계기)를 따른다.

#### OBS-B2 SPOT 큐·actor 이동 계기

우선순위: `P0`

**검증 질문:** 룸 부하와 actor 노드 간 이동이 `spot.queue.depth`·`spot.queue.wait.duration`·`actor.transfers`에 반영되는가.

- 절차: 룸에 부하를 주고(다수 액션), player actor를 `play-a`→`play-b`로 이동시킨다.
- 검증: `zlink.spot.queue.depth`/`queue.wait.duration`이 `spot_kind=user` 라벨로 계수되고,
  `zlink.actor.transfers`가 target activation 1회당 1회, `transfer.duration`이 transfer 시작부터 target
  activation 또는 실패 terminal까지의 구간을 담는다. commit ack는 중간 상태이며, 성공 reply 전달
  완료까지 구간을 늘리지 않는다.
  이동 전 actor request가 pending이면 `zlink.mesh_node.requests.inflight`의 `surface=actor` 값에
  반영되고, 각 request가 terminal completion에 도달하면 기준값으로 돌아온다. spot 계기는
  `spot_kind`(`entry|user`)로 분리된다
  ([Runtime Metrics §3.1](../../spec/server/51-runtime-metrics.ko.md#31-peer와-channel),
  [§4](../../spec/server/51-runtime-metrics.ko.md#4-object와-stream-계기)).
- 세부 동작: SPOT/actor 계기.

#### OBS-B3 fanout·lease 계기와 카디널리티

우선순위: `P1`

**검증 질문:** fanout 발행/수신 차분과 owner lease 갱신 지연이 계기에 기록되고 고카디널리티 라벨이 제외되는가.

- 절차: `OrderWorkflow`가 이벤트를 fanout publish하고 다수 subscriber가 받는다. lease 갱신 지연은 내부 훅이 아니라 Redis 측 지연 주입(외부 인프라 조작, house rule 준수)으로 만든다.
- 검증: `zlink.fanout.published`/`received`가 1:N로 계수된다. backend가 drop을 관찰할 수 없으면
  `fanout.dropped` instrument가 없고 0을 방출하지 않는다. 관찰 가능한 backend에서는
  queue 제한으로 drop을 유발해 실제 수와 일치하는지 확인한다. `zlink.location.owner_lease.renew.lateness`가
  갱신 지연을 기록한다. **어떤 계기에도 `correlation_id`/`flow_id`/`actor_id`/`spot_rid` 라벨을
  포함하지 않는다
  ([Runtime Metrics §5~6](../../spec/server/51-runtime-metrics.ko.md#5-location과-classic-fanout-계기)).
- 세부 동작: fanout/lease 계기 + 카디널리티 규약.

#### OBS-B4 비활성 계측의 최소 비용

우선순위: `P1`

**검증 질문:** meter/reader가 없으면 event별 값 저장·동기화·시각 측정 없이 비활성 경로로 끝나는가.

- 절차: reader를 등록하지 않은 노드에서 트래픽을 흘린다.
- 검증: reader 미등록에서도 messaging 정확성이 불변이고, 장시간 트래픽에 계기 저장 공간이 상한 내로
  유지된다(무한 적재 없음,
  [Runtime Metrics §7](../../spec/server/51-runtime-metrics.ko.md#7-reader와-성능)). Hot path의 clock
  read 생략은 프로세스 밖 E2E로 관찰할 수 없는 구현 내부 속성이므로 언어별 benchmark·unit test
  `RMETRIC-009`가 소유하며 이 config에서 단언하지 않는다.
- 세부 동작: 비활성 계측의 최소 비용.

### Track C — Graceful Drain & Handoff

#### OBS-C1 draining 마커 — 연결 유지 + 배치 제외

우선순위: `P0`

**검증 질문:** `play-a`를 drain하면 신규 배정에서만 빠지고 기존 연결·in-flight는 유지되는가.

- 절차: 룸과 bound actor가 유지 중인 `play-a`에 drain을 요청한다(자동 drain 또는 명시 `DrainAsync`).
- 검증: `play-a` MeshNode descriptor의 `Draining=true`와 runtime snapshot의
  `State=Draining`이 관측되어 신규 room/actor 배정에서 제외된다. `zlink.drain.state` gauge가
  `state=serving`→`state=draining`으로 전이한다
  ([Graceful Drain §9](../../spec/server/54-graceful-drain-handoff.ko.md#9-observability-identifiers)).
  descriptor는 drain 중 유지되므로 기존
  연결이 유지되고, 전파 지연 창에 기존 연결로 온 request가 정상 처리된다
  ([Graceful Drain §3~5](../../spec/server/54-graceful-drain-handoff.ko.md#3-drain-순서)). owner lease는
  draining 동안 계속 갱신된다([§8](../../spec/server/54-graceful-drain-handoff.ko.md#8-location과-owner-cleanup)).
- 세부 동작: 마커 기반 배치 제외 + 연결 유지.

#### OBS-C2 actor 핸드오프 + bound session 연속성

우선순위: `P0`

**검증 질문:** drain이 유지 중인 actor를 `play-b`로 이동시키고, 이동 중에도 bound session이 이어지는가.

- 절차: `play-a` drain 중 bound actor가 `play-b`로 transfer된다.
- 검증: transfer가 [Spot Actor §5](../../spec/server/23-spot-actor.ko.md#5-다른-meshnode로-transfer) 완료 조건까지
  진행되고 committed Actor authority가 `play-b`를 target owner로 가리킨다. Bound session push가 이동 후
  `play-b` Actor로 이어진다.
  `zlink.drain.actors.handed_off`가 계수된다. 이동 전 pending actor request는
  `zlink.mesh_node.requests.inflight{surface=actor}`에 반영되고, 이동 중 각 request는 원래 reply
  또는 timeout 결과를 유지한 뒤 계기 값에서 제거된다
  ([Runtime Metrics §3.1](../../spec/server/51-runtime-metrics.ko.md#31-peer와-channel),
  [Graceful Drain §6](../../spec/server/54-graceful-drain-handoff.ko.md#6-actor와-spot-handoff)).
- 세부 동작: 핸드오프 + FIFO 연속성.

#### OBS-C3 고정 Spot drain 순서와 명시적 재생성

우선순위: `P0`

**검증 질문:** 정상 동작 중인 Spot이 request 완료만으로 닫히지 않고, drain을 시작한 뒤에는 이미 받은
turn과 Actor·STREAM 경계를 정리한 다음 local Spot과 authority row를 한 번만 닫는가. 닫힌 Spot의 이전
`SpotHandle`이 다른 노드에서 Spot을 몰래 다시 만들지 않는가.

- 절차:
  1. `play-a`의 room Spot을 만들고 `SpotHandle`, Spot generation과 authority projection을 기록한다. 정상 request를
     완료한 뒤 같은 handle로 후속 request를 보내 같은 Spot generation과 row가 유지되는지 확인한다.
  2. 다음 Spot request가 handler에 들어온 evidence를 남긴 뒤 bounded gate에서 turn 완료를 막고
     `play-a` drain을 시작한다. Spot-local admission이 닫힌 evidence를 확인한 다음 같은 Spot에 새 request를
     보내 handler에 들어가지 않고 공개 실패로 끝나는지 확인한다. 이미 accepted된 turn의 gate는 이후
     해제해 정상 완료시킨다.
  3. OBS-C2가 소유하는 actor handoff 완료와 bound STREAM 연속성 barrier를 기다린다. 두 barrier가 모두
     끝나기 전에는 local room Spot close와 row removal evidence가 없어야 한다. barrier 이후 local close와
     owner Spot row 제거를 순서대로 확인한다.
  4. row 제거 전 얻은 `SpotHandle`로 다시 request한다. 요청은 stale target 또는 route failure로 끝나며,
     `play-b`에 remote `GetOrCreate`가 실행되었다는 evidence와 새 authority row가 없어야 한다.
  5. application이 `play-b`의 local Spot manager에서 같은 논리 ID를 명시적으로 `GetOrCreate`한다. 새
     generation의 handle과 row가 생긴 뒤에만 request가 성공해야 한다.
- 검증: drain 전 정상 request completion은 Spot을 유지한다. Drain은 새 Spot turn admission을 닫지만 이미
  accepted된 turn은 완료시킨다. Actor handoff와 STREAM barrier가 local Spot close보다 먼저이고, close 뒤
  authority row가 제거된다. Stale handle은 숨은 remote 생성으로 이어지지 않으며 명시적 local
  `GetOrCreate`만 새 Spot을 만든다. 한 drain operation의 terminal result와 terminal lifecycle event는 각각
  정확히 한 번 기록된다. OBS-C1의 membership 배치 제외, OBS-C2의 handoff·session 내용, OBS-C4의 강제
  종료와 OBS-C5의 zero-target 판단은 여기서 다시 판정하지 않고 각 시나리오 evidence를 barrier로만 사용한다.
- 세부 동작: 고정 admission seal → accepted turn 완료 → handoff·STREAM barrier → local close → row 제거.

#### OBS-C4 강제 종료 + 세션 종료 통지

우선순위: `P1`

**검증 질문:** deadline을 넘기면 강제 종료로 넘어가고 활성 세션에 `server_drain` 종료 통지가 가는가.

- 절차: actor handler 또는 transfer가 시작됐다는 evidence가 나온 뒤 application의 bounded gate에서
  완료를 막는다. gate 대기 상한보다 짧지만 0보다 큰 drain deadline으로 `Drain(deadline)`을 호출하고,
  deadline이 지나 terminal result가 나온 뒤 gate를 해제한다. 벽시계 sleep만으로 handoff 지연을
  유도하지 않는다.
- 검증: 상태가 `ForceStopping`으로 전이하고 drain 결과가 `ForceStopped`이다. 활성 STREAM 세션에는
  versioned `session-closing` 제어 프레임의 `reason=server_drain`이 통지 상한 내에 전달된다. connector가
  제어 프레임을 저장한 뒤 disconnect event를 내보내며, client는 공개 `closeReason`으로 확인한다.
  `zlink.drain.forced{kind=session}`이 계수되고 통지가 프로세스 종료를 무한 지연시키지 않는다.
- 세부 동작: 강제 종료 경로.

#### OBS-C5 무중단 롤아웃과 zero-target 종료

우선순위: `P1`

**검증 질문:** serving target이 있는 순차 롤아웃과 target이 없는 강제 종료가 각각 계약대로 동작하는가.

- 절차: (a) fresh topology에서 accepted turn이 남지 않았음을 확인하고 `play-a`만 drain해 actor를
  serving `play-b`로 이동시킨다. (b) topology를 다시 시작하고 actor는 `play-a`에 둔다. `play-b`에서
  application handler를 bounded gate로 막은 뒤 `play-a`보다 긴 deadline으로 `play-b`의 drain을 먼저
  시작하고, location 성공 조회에서 `play-b`의 `Draining=true`를 확인한다. 그 상태에서 `play-a`를
  drain하면 handoff 대상 선택은 이미 draining인 `play-b`를 제외하므로 eligible target이 0이 된다.
  `play-a`의 terminal result를 확인한 뒤 `play-b`의 gate를 해제한다.
- 검증: (a)는 `ForceStopping` 없이 `Drained`로 끝난다. (b)는 draining peer를 target에서 제외하고,
  actor는 source에서 유지된다. application이 actor 작업을 정상 종료하면 고정 drain 순서가 계속되고,
  종료되지 않으면 전역 deadline에 `ForceStopped(deadline_exceeded)`가 된다.
- 세부 동작: 동시 drain 폴백.

## 5. 완료 기준

- OBS-A1~A4, OBS-B1~B4, OBS-C1~C5를 모두 통과한다. 우선순위는 실행 순서만 정하며 완료 범위를
  줄이지 않는다.
- flow 로그는 노드 경계를 관통하고 error 라인에도 `flow=`가 있다.
- 메트릭 계기는 실제 사건과 일치하고 고카디널리티 라벨이 없다.
- drain은 마커로 연결을 유지하며 배치만 제외하고, accepted turn과 actor·STREAM barrier 뒤에 Spot과 row를
  정리하며, owner lease를 drain 동안 계속 갱신한다.
- 공개 표면만 직접 사용하고 `ensure`로 단언한다.
