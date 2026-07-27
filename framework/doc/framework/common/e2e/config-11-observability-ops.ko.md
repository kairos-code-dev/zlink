<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Spot actor relocation](config-10-spot-actor-relocation.ko.md) |
[다음: Channel egress routing](config-12-channel-egress-routing.ko.md)
<!-- framework-adapter-nav:end -->

# Config 11 — 관측·운영 배포 (metrics · flow correlation · host maintenance)

운영 중인 배포에서 새 관측·운영 표면 세 가지를 실제 배포 조건(공유 store·다중 노드·프로세스
경계)에서 검증한다. 세 표면의 계약은 각각 [메시지 흐름 상관관계](../spec/27-flow-correlation.ko.md),
[런타임 메트릭](../spec/25-runtime-metrics.ko.md), [Host Relocate, Shutdown & Handoff](../spec/28-graceful-drain-handoff.ko.md)이
소유하고, 이 config는 그 계약이 배포 현장에서 의도대로 동작하는지를 확인한다.

기존 [Config 7 — Runtime Monitoring](config-7-monitoring.ko.md)이 MeshNode의 snapshot과 typed runtime
event를 다룬다면, 이 config는 (1) 한 흐름을 노드 경계 너머로 잇는 **flow correlation 로그**,
(2) 밖에서 못 재는 신호의 **집계 메트릭**, (3) stateful 노드의 **우아한 종료·핸드오프**를 다룬다.

## 1. 목적과 범위

- 다룬다: `flow=` 로그가 STREAM→actor→spot 경계를 관통하는지, 메트릭 계기가 실제 사건과 일치하는지,
  `Relocate`가 배치 제외·핸드오프 뒤 `Relocated`에서 host를 유지하는지, `Shutdown`이 relocation과
  독립적으로 bounded cleanup을 수행하는지.
- 여기서 다루지 않는 것: 기능 자체의 messaging 정확성(다른 config), MeshNode runtime snapshot·event
  관찰(Config 7), 대시보드·exporter 구성(앱 몫,
  [Runtime Metrics §8](../spec/25-runtime-metrics.ko.md#8-reader와-성능)).

## 2. 서버 구성 (한 번 구동)

관측 대상 사건을 모두 만들려면 세션 게이트웨이 + 룸 + owner spot이 함께 있는 배포가 필요하다.
Bingo형 3역할에 owner-spot 서비스를 더한 구성을 쓴다.

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension. 실행마다 전용 key prefix. |
| relocation store | 1 | 공식 Redis relocation store extension. location store와 같은 Redis deployment를 쓰되 별도 key prefix를 사용한다. |
| `Session` | 1 | Location Store를 등록한 Object Client. client STREAM endpoint, global Actor binding과 packet relay를 제공하며 factory와 placement target은 제공하지 않는다. STREAM 세션이 CCU·재접속 계기의 소스다. |
| `Play` | 2 (`play-a`, `play-b`) | Location Store와 Relocation Store를 등록한 Object Server. Entry Spot, stable User Spot type `play.room`, Actor type `play.player`, Instance Spot type `play.instance` factory를 모두 명시적 `Snapshot` policy로 등록한다. Actor factory에는 Actor relocation adapter를, 두 Spot factory에는 각 concrete Spot type의 Spot relocation adapter를 지정한다. 두 노드는 같은 capability set, placement weight `100`, Actor total·Spot total limit `128`, 두 Spot stable type별 limit `128`과 activation concurrency `32`를 제공한다. actor 이동·룸 타이머·bound push와 `Relocate` handoff의 source·target이다. |
| `OrderWorkflow` | 2 | Location Store를 등록한 Object Server. Stable User Spot type `order.workflow` factory를 명시적 `Disabled` policy, placement weight `100`, Actor total·Spot total·`order.workflow` stable type limit `64`, activation concurrency `16`으로 등록하고 event-sourcing owner Spot과 projection fan-out을 제공한다. 이 역할은 Play의 maintenance relocation target이 아니다. |
| trigger client | 시나리오별 | STREAM 접속·게임 진행·주문 흐름·연결 해제를 유발한다. |

Store instance는 공통 Redis deployment를 사용할 수 있지만 각 Framework root가 필요한 capability를
명시적으로 등록한다. `Play`의 `Snapshot` factory 때문에 두 Play root에는 Relocation Store가 필수다.
`Session`과 `OrderWorkflow`에는 Snapshot 또는 Recreate factory가 없으므로 Relocation Store를 등록하지 않는다.
OBS-C6의 `ApplicationVersion=N+1` target과 OBS-C7의 동일 version target은 source와 같은 세 stable type,
factory·adapter kind를 게시하고 source inventory보다 큰 Actor total, Spot total과 Spot stable type의
`Active+Reserved+Requested` headroom을 유지한다.

각 host는 기존 message-flow 설정, 언어별 표준 meter/registry와 host framework runtime을 사용한다.
`Relocating`, `Relocated` 또는 `Draining` 중에는 새 ChannelName·Logical Multicast 선택에서 해당
membership을 제외한다. flow id는
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
- **termination evidence**: host termination lifecycle 이벤트, terminal result, authority projection과 Spot
  closing callback을 `/evidence`에 기록한다.

`/evidence` JSON은 언어와 무관하게 다음 최소 배열을 제공한다. metric snapshot은 시나리오 시작 직전
기준값과 사건 완료 뒤 값을 함께 저장해 counter delta와 current gauge를 구분한다.

```json
{
  "metrics": [{"name":"...","kind":"counter","value":1,"unit":"{event}","tags":{}}],
  "lifecycleEvents": [{"sequence":1,"state":"relocating","operation":"relocate"}],
  "peerRows": [{"nodeRid":"...","draining":true,"generation":1}]
}
```

`kind`는 `counter|updown|observable|histogram`, runtime state label은
`serving|relocating|relocated|draining|stopped|error` 소문자 값으로 고정한다. histogram은 raw sample 또는 provider
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
  ([Flow Correlation §2](../spec/27-flow-correlation.ko.md#2-두-식별자)).
- 세부 동작: Spot·Actor 경계 관통
  ([Flow Correlation §5](../spec/27-flow-correlation.ko.md#5-propagation)).

#### OBS-A2 error 라인에도 flow

우선순위: `P0`

**검증 질문:** dispatch가 실패(핸들러 없음/디코드 실패)해도 그 error 라인에 `flow=`가 찍혀 성공·실패가 한 grep에 잡히는가.

- 절차: 알 수 없는 packet이나 잘못된 payload로 dispatch 실패를 유발한다.
- 검증: dispatch error 라인에 `flow=`가 있고, `grep flow=<id>`로 성공 라인과 실패 라인이 함께
  확인된다([Flow Correlation §7](../spec/27-flow-correlation.ko.md#7-reply와-failure)).
- 세부 동작: error reporter flow 기록.

#### OBS-A3 create-if-absent · off 노드 전파

우선순위: `P1`

**검증 질문:** flow가 이미 있으면 재생성하지 않고, 트레이싱 off 노드를 지나도 흐름이 끊기지 않는가.

- 절차: (a) 진입점에서 생성된 flow가 하류 노드에서 그대로 유지되는지, (b) 중간 노드의 트레이싱을 `off`로 두고 흐름을 통과시킨다.
- 검증: (a) 하류 노드는 flow를 재생성하지 않는다(같은 id 유지). (b) off 노드는 새 flow를 시작하지
  않지만 전파는 유지해, off 노드 이후 노드에서 같은 flow가 다시 나타난다
  ([Flow Correlation §4](../spec/27-flow-correlation.ko.md#4-flow-생성)).
- 세부 동작: create-if-absent + 전파 무조건.

#### OBS-A4 publish fan-out 트리 · timer 발원

우선순위: `P1`

**검증 질문:** 한 흐름이 publish로 N 구독자에 갈라져도 같은 flow를 갖고, timer 발원 콜백은 새 flow를 시작하는가.

- 절차: (a) `OrderWorkflow`가 projection 갱신을 fanout publish하고 다수 구독자가 받는다. (b) room timer tick이 발생한다.
- 검증: (a) 구독자 N개 라인이 같은 flow ID를 갖는다
  ([Flow Correlation §5](../spec/27-flow-correlation.ko.md#5-propagation)). (b) timer 발원
  callback은 `origin=timer`로 새 flow를 시작한다
  ([Flow Correlation §4](../spec/27-flow-correlation.ko.md#4-flow-생성)).
- 세부 동작: fan-out 트리 + timer origin.

### Track B — 런타임 메트릭

#### OBS-B1 CCU·재접속 계기

우선순위: `P0`

**검증 질문:** STREAM 세션이 연결되고 해제될 때 `stream.connections.active`가 정확히 증감하고 재접속이 계수되는가.

- 절차: trigger client 여럿이 `Session`에 STREAM으로 접속했다 끊고, 일부는 재접속한다.
- 검증: 서버 reader의 `zlink.stream.connections.active`가 접속/종료에 정확히 증감한다. 재접속을
  수행한 trigger connector의 reader에서 `zlink.stream.reconnects`가 자동 재접속 시도마다 증가한다.
  서버는 새 연결과 재접속을 추측하지 않는다. `close_reason` 라벨은 닫힌 enum에 속한다.
- 세부 동작: 서버 연결 계기는 [Runtime metrics §4](../spec/25-runtime-metrics.ko.md#4-object와-stream-계기),
  connector 재접속 계기는 [Stream Connector §6.2](../spec/stream-connector/32-stream-connector.ko.md#62-connector-reconnect-계기)를 따른다.

#### OBS-B2 Actor 이동 계기

우선순위: `P0`

**검증 질문:** Actor의 node 간 이동 결과와 service 중단 시간이 relocation metric에
반영되는가.

- 절차: Player Actor를 `play-a`→`play-b`로 이동시킨다.
- 검증: `zlink.relocation.completed`가 `object_kind=actor`의 terminal relocation
  1회당 1회 증가한다.
  `zlink.relocation.duration`은 prepare부터 `completed|aborted|recovered|failed|shutdown` terminal까지의
  구간을 담는다. Location commit은 중간 상태이며 terminal 결과로 기록하지 않는다.
  Entry Spot 또는 `PerActor` User Spot의 Actor는 queue seal부터 target admission까지의 시간을
  `zlink.actor.relocation.interruption`에 기록한다. 1초를 넘겨도 relocation은 계속 진행하며,
  structured warning에는 `interruption_target_exceeded=true`와 실제 시간이 남는다. 이 초과를
  `zlink.relocation.completed`의 실패 결과로 바꾸면 안 된다.
  이동 전 actor request가 pending이면 `zlink.mesh_node.requests.inflight`의 `surface=actor` 값에
  반영되고, 각 request가 terminal completion에 도달하면 기준값으로 돌아온다
  ([Runtime Metrics §3.1](../spec/25-runtime-metrics.ko.md#31-peer와-channel),
  [§4](../spec/25-runtime-metrics.ko.md#4-object와-stream)).
- 세부 동작: Actor relocation 계기.

#### OBS-B3 fanout·lease 계기와 카디널리티

우선순위: `P1`

**검증 질문:** fanout 발행/수신 차분과 owner lease 갱신 지연이 계기에 기록되고 고카디널리티 라벨이 제외되는가.

- 절차: `OrderWorkflow`가 이벤트를 fanout publish하고 다수 subscriber가 받는다. lease 갱신 지연은 내부 훅이 아니라 Redis 측 지연 주입(외부 인프라 조작, house rule 준수)으로 만든다.
- 검증: `zlink.fanout.published`/`received`가 1:N로 계수된다. backend가 drop을 관찰할 수 없으면
  `fanout.dropped` instrument가 없고 0을 방출하지 않는다. 관찰 가능한 backend에서는
  queue 제한으로 drop을 유발해 실제 수와 일치하는지 확인한다. `zlink.location.owner_lease.renew.lateness`가
  갱신 지연을 기록한다. **어떤 계기에도 `correlation_id`/`flow_id`/`actor_id`/`spot_id` 라벨을
  포함하지 않는다
  ([Runtime Metrics §6~7](../spec/25-runtime-metrics.ko.md#6-location과-classic-fanout-계기)).
- 세부 동작: fanout/lease 계기 + 카디널리티 규약.

#### OBS-B4 비활성 계측의 최소 비용

우선순위: `P1`

**검증 질문:** meter/reader가 없으면 event별 값 저장·동기화·시각 측정 없이 비활성 경로로 끝나는가.

- 절차: reader를 등록하지 않은 노드에서 트래픽을 흘린다.
- 검증: reader 미등록에서도 messaging 정확성이 불변이고, 장시간 트래픽에 계기 저장 공간이 상한 내로
  유지된다(무한 적재 없음,
  [Runtime Metrics §8](../spec/25-runtime-metrics.ko.md#8-reader와-성능)). Hot path의 clock
  read 생략은 프로세스 밖 E2E로 관찰할 수 없는 구현 내부 속성이므로 언어별 benchmark·unit test
  `RMETRIC-009`가 소유하며 이 config에서 단언하지 않는다.
- 세부 동작: 비활성 계측의 최소 비용.

### Track C — Host Relocate, Shutdown & Handoff

#### OBS-C1 relocating·relocated 마커 — 연결 유지 + 배치 제외

우선순위: `P0`

**검증 질문:** `play-a`에 `Relocate`를 시작하면 신규 배정에서 제외되고 이미 수락한
작업은 terminal 결과까지 유지되는가.

- 절차: 룸과 bound actor가 유지 중인 `play-a`에 `PlannedMaintenance` mode로 host `Relocate`를 요청한다.
- 검증: `play-a` MeshNode descriptor의 relocation 제외 상태와 runtime snapshot의
  `State=Relocating`이 관측되어 신규 room/actor 배정에서 제외된다. 완료 뒤 `State=Relocated`가
  되고 host connection과 infrastructure는 유지된다. `zlink.host.state` gauge가
  `state=serving`→`state=relocating`→`state=relocated`로 전이한다
  ([Host maintenance §13](../spec/28-graceful-drain-handoff.ko.md#13-관측-정보)).
  descriptor는 `Relocating`과 `Relocated` 중 유지되므로 기존
  연결이 유지되고, 전파 지연 창에 기존 연결로 온 request가 정상 처리된다
  ([Host maintenance §7](../spec/28-graceful-drain-handoff.ko.md#7-relocation-unit과-실행량-제한)). owner lease는
  draining 동안 계속 갱신된다([§11](../spec/28-graceful-drain-handoff.ko.md#11-shutdown과-relocate의-경쟁)).
- 세부 동작: 마커 기반 배치 제외 + 연결 유지.

#### OBS-C2 actor 핸드오프 + bound session 연속성

우선순위: `P0`

**검증 질문:** `Relocate` 중인 actor를 `play-b`로 이동시키고, 이동 중에도 bound session이 이어지는가.

- 절차: `play-a`에 `PlannedMaintenance` mode의 host `Relocate`를 요청하고, bound actor가
  `play-b`로 relocation되게 한다.
- 검증: relocation이 [Spot Actor §4](../spec/15-spot-actor.ko.md#4-join-의미와-commit-순서) 완료 조건까지
  진행되고 committed Actor authority가 `play-b`를 target owner로 가리킨다. Bound session push가 이동 후
  `play-b` Actor로 이어진다.
  `zlink.relocation.completed{object_kind=actor}`가 target activation당 한 번 계수된다. 이동 전 pending actor request는
  `zlink.mesh_node.requests.inflight{surface=actor}`에 반영되고, 이동 중 각 request는 원래 reply
  또는 timeout 결과를 유지한 뒤 계기 값에서 제거된다
  ([Runtime Metrics §3.1](../spec/25-runtime-metrics.ko.md#31-peer와-channel),
  [Host maintenance §7](../spec/28-graceful-drain-handoff.ko.md#7-relocation-unit과-실행량-제한)).
- 세부 동작: 핸드오프 + FIFO 연속성.

#### OBS-C3 User Spot aggregate Relocate handoff

우선순위: `P0`

**검증 질문:** User Spot과 member Actor가 `Relocate`에서 하나의 aggregate로 target에 이전되고 logical identity와
ObjectGeneration을 유지하는가.

- 절차:
  1. `play-a`에 Snapshot policy의 room User Spot과 member Actor를 만들고 Spot·Actor의 global ID,
     ObjectGeneration, AuthorityOwnerGeneration, participant count, inventory root와 digest를 기록한다.
  2. Spot turn과 Actor request 하나를 각각 수락한 뒤 bounded gate에서 완료를 막고 `play-a`에
     `PlannedMaintenance` mode의 `Relocate`를 시작한다.
     Source admission seal 뒤 같은 object의 신규 request가 handler에 들어가지 않는지 확인하고 accepted gate는
     deadline 안에 해제한다.
  3. `play-b`의 target factory·adapter `Restore`, participant별 staging과 모든 `Prepared`가 끝난 뒤에만
     `Relocated`가 게시되는지 확인한다. Location Store의 aggregate commit 전에는 target handler를 열지 않는다.
  4. Aggregate commit 뒤 같은 global Spot ID와 Actor ID로 request를 보내 `play-b` handler에서 처리되는지
     확인하고 bound STREAM route ACK와 steady normalization까지 기다린다.
- 검증: User Spot과 모든 member Actor는 ObjectGeneration을 유지하고 AuthorityOwnerGeneration만 증가한다.
  Location Store의 aggregate owner·generation·inventory root는 한 CAS에서 target으로
  전환되고 Relocation manifest의 participant count와 inventory digest가 일치한다. Source Spot에는 `OnClosing(RelocationOut)`이 한 번
  전달되지만 logical authority row를 삭제하거나 같은 RID를 새 generation으로 재생성하지 않는다. Target
  factory와 `Restore`는 retry-safe하게 at-least-once 실행될 수 있지만 stale attempt는 commit·admission을
  수행하지 못한다. Current relocation fence의 accepted message·journal은 application handler에 중복 적용되지
  않는다. 한 `Relocate` operation의 terminal result와 terminal lifecycle event는 각각 정확히 한 번 기록된다.
  OBS-C1의 배치 제외,
  OBS-C2의 bound-session 연속성, OBS-C4의 `Shutdown` closing과 OBS-C5의 target blocker 판단은 각 scenario
  evidence를 barrier로 사용한다.
- 세부 동작: aggregate preflight·seal → immutable payload 준비 → target staging·Prepared → Location aggregate
  commit → callback·journal replay → route ACK·steady normalization.

#### OBS-C4 Shutdown closing callback + 세션 종료 통지

우선순위: `P1`

**검증 질문:** `Shutdown`이 새 relocation을 시작하지 않고 Spot에 closing callback을 알린 뒤
활성 세션을 종료하는가.

- 절차: Entry·User·Instance Spot이 하나씩 존재하고 Actor membership이 유효한 상태에서 host
  `Shutdown`을 호출한다. 각 Spot은 callback 순서, reason, deadline, callback 시점의 membership을
  evidence로 남긴다.
- 검증: 새 relocation·target reservation이 0건이고 각 Spot의 `OnClosing` reason이
  `HostShutdown`으로 정확히 한 번 기록된다. Callback 시점에 local Actor membership과 Spot instance가
  유효하고 callback 완료 뒤에만 scope·authority·listener를 정리한다. Actor별 closing callback은 없다.
  활성 STREAM 세션은 versioned `session-closing` 제어 프레임의 `ServerDrain`을 저장한 뒤 disconnect
  event를 내보내고 client는 공개 `closeReason`으로 확인한다. Terminal result는
  `Outcome=Stopped`, `Reason=None`이다.
- 세부 동작: relocation 없는 host cleanup과 Spot closing lifecycle.

#### OBS-C5 Relocate eligible target 부재

우선순위: `P1`

**검증 질문:** continuity를 받을 eligible target이 없으면 `Relocate`가 source를 유지한 채
종료 전에 차단되는가.

- 절차: Actor와 User·Instance Spot을 `play-a`에 둔다. 첫 반복은 같은 version node를 시작하지 않고
  `PlannedMaintenance`를 호출한다. 두 번째 반복은 source보다 큰 requested version을 지정해
  `RollingUpdate`를 호출하되 그 version node를 시작하지 않는다. 별도 반복에서는 candidate를
  `Draining`, type capability 부족, maintenance wave 일치 실패 또는 capacity 소진 중 하나로 만든다.
- 검증: 두 mode 모두 요청한 exact version의 eligible target이 나타날 때까지 deadline 안에서 기다린다.
  Deadline 뒤 terminal result는 admission seal 전 `Blocked/TargetUnavailable` 또는 해당 compatibility
  blocker의 `Blocked/StateIncompatible`이다. Host state·descriptor·readiness·object authority·membership과
  handler admission이 모두 유지되고 relocation root, reservation, closing callback은 0건이다. `Shutdown`을
  숨은 fallback으로 시작하지 않는다.
- 세부 동작: all-or-none preflight blocker와 source continuity 유지.

#### OBS-C6 무중단 patch

우선순위: `P0`

**검증 질문:** 새 application version node를 먼저 Serving으로 준비한 뒤 기존 version host를
`Relocate`하면 stateful object와 session continuity를 유지하면서 새 version으로 전환되는가.

- 절차: `ApplicationVersion=N` source에 Actor·User Spot aggregate·Instance Spot과 bound STREAM session을
  만든다. 호환되는 adapter·type capability와 충분한 capacity를 갖춘
  `ApplicationVersion=N+1` target만 새로 Serving 상태로 준비한다. 지속 request와 push를 보내는
  중 `Mode=RollingUpdate`, `TargetApplicationVersion=N+1`로 source `Relocate`를 호출한다.
- 검증: source가 `Relocated`를 publish하기 전 모든 target restore와 `Prepared`가 끝난다. Actor·Spot
  generation, accepted journal, participant inventory digest와 bound session ordering이 유지되고 committed owner는
  `N+1` target을 가리킨다. Relocation result는 `Relocated/None`,
  `Mode=RollingUpdate`, effective `TargetApplicationVersion=N+1`이고 source infrastructure와 connection은
  유지된다. 이어서 `Shutdown`을 호출하면 source는 `Stopped/None`으로 끝난다. 전환 구간의 request는 원래 reply,
  명시적 moving 결과 또는 caller timeout 중 하나로 유한 완료되고 hidden retry는 없다. 완료 뒤
  신규 request·push evidence는 `N+1` target에서만 남는다.
- 세부 동작: 새 version target 선행 배치→source `Relocate`→stateful continuity 전환.

#### OBS-C7 동일 version planned maintenance

우선순위: `P0`

**검증 질문:** application patch가 아닌 node 점검은 동일 version의 다른 Serving node로
continuity를 이전한 뒤 source를 종료할 수 있는가.

- 절차: source와 target을 모두 `ApplicationVersion=N`으로 시작하고 target의 type capability, wave와
  capacity를 호환 상태로 둔다. Source에 stateful object와 accepted request가 있는 상태에서
  `Mode=PlannedMaintenance`, `TargetApplicationVersion=null`로 `Relocate`를 호출한다.
- 검증: 새 application version node가 없어도 동일 version target이 eligible하면 preflight가 통과한다.
  Accepted work가 완료되고 Actor·Spot·session continuity가 target으로 이전된 뒤 source는
  `Relocated/None`, `Mode=PlannedMaintenance`, effective `TargetApplicationVersion=N`을 반환하고 host를
  유지한다. 이어서 `Shutdown`을 호출하면 `Stopped/None`으로
  종료된다. Target은 종료 전·후 같은 `ApplicationVersion=N`을 관측한다.
- 세부 동작: application version 변경 없는 node 점검 handoff.

#### OBS-C8 Shutdown deadline과 bounded teardown

우선순위: `P1`

**검증 질문:** Spot closing callback이 deadline 내에 완료되지 않아도 `Shutdown`이 무한히
대기하지 않고 정해진 terminal result로 끝나는가.

- 절차: Spot `OnClosing` 진입 evidence를 남긴 뒤 application bounded gate에서 완료를 막는다.
  Gate 대기 상한보다 짧고 0보다 큰 deadline으로 `Shutdown`을 호출하고 terminal result 후 gate를
  해제한다. 벽시계 sleep만으로 지연을 만들지 않는다.
- 검증: callback에 전달하는 absolute deadline이 host deadline과 같다. 해당 언어가 표준 cleanup
  cancellation을 지원하면 deadline에 signal이 전환되고, 지원하지 않으면 Framework가 callback
  completion 대기를 끝낸다. Host state는 `Stopped`, terminal result는
  `Outcome=ForceStopped`, `Reason=DeadlineExceeded`이며
  `zlink.host.shutdown.forced{reason=deadline_exceeded}`가 한 번 계수된다. Callback을
  기다리는 동안 relocation을 시작하지 않고 기존 handler cancellation signal을 cleanup에 재사용하지 않는다.
- 세부 동작: deadline 기반 cooperative cleanup과 bounded forced teardown.

#### OBS-C9 automatic convergence와 manual topology blocker

우선순위: `P0`

**검증 질문:** `Relocate`가 automatic RouteMesh의 실제 peer readiness를 확인한 뒤에만 시작되고, Framework가
replacement 연결을 증명할 수 없는 manual topology는 source를 변경하지 않은 채 차단되는가.

- 절차:
  1. Automatic source와 green target을 시작하되 green descriptor publication 뒤 RouteMesh handshake admission을
     bounded gate에서 막는다. Source에 `PlannedMaintenance` mode의 `Relocate`를 호출하고 descriptor,
     connect intent와 source Core peer table을
     함께 기록한다.
  2. Gate를 해제해 exact green RID·lifecycle generation이 source Core peer table에서 admitted·ready가 되게 한다.
     Stateful object 하나를 이전하고 `Relocated` result까지 관찰한 뒤 `Shutdown`을 호출해 source의
     descriptor release와 peer disconnect까지 관찰한다.
  3. 별도 process 반복에서 local manual RouteMesh peer, ClientServer client endpoint, fanout subscriber endpoint,
     Location Store에 descriptor를 게시하지 않는 manual fanout publisher를 각각 하나씩 구성한다. 각 host에
     `PlannedMaintenance` mode의 `Relocate`를 호출한 뒤 같은 host에 `Shutdown`을 호출한다.
- 검증: descriptor와 connect intent만 존재하는 동안 source state는 `Serving`, readiness와 application admission은
  그대로이고 relocation root와 reservation은 0건이다. Exact peer가 `Ready`가 된 뒤에만 old `Relocating`,
  relocation, old `Relocated`, 명시적 `Shutdown`, old `Draining`과 accepted barrier,
  descriptor·owner lease release, disconnect 순서로 진행한다. Relocation은 `Relocated/None`,
  Shutdown은 `Stopped/None`으로 끝난다. 네 manual 반복의 `Relocate`는 모두
  `Blocked/ManualTopologyUnsupported`이며 state, readiness, manual connection과 handler admission을 바꾸지 않는다.
  이어서 호출한 `Shutdown`은 manual topology를 blocker로 사용하지 않고 `Stopped` 또는 `ForceStopped`로 유한
  완료된다.
- 세부 동작: descriptor와 physical readiness의 구분, automatic replacement ordering과 manual topology의
  precommit 차단.

#### OBS-C10 Relocation mode의 exact version 선택

우선순위: `P0`

**검증 질문:** 같은 topology에 여러 application version이 있어도 mode가 정한 exact version만
target으로 선택되는가.

- 절차: `ApplicationVersion=N` source와 충분한 capacity를 가진 `N`, `N+1`, `N+2` target을 모두
  Serving으로 준비한다. 모든 target은 같은 stable type, policy와 adapter capability를 제공하고 서로 다른
  maintenance wave를 사용한다. 첫 반복은 `PlannedMaintenance`를 호출하고, 두 번째 반복은 새 source workload에
  `Mode=RollingUpdate`, `TargetApplicationVersion=N+1`을 호출한다. Candidate weight는 의도한 version이 가장
  낮고 제외되어야 하는 version이 가장 높도록 구성한다.
- 검증: planned maintenance의 모든 committed owner는 `N` target만 가리키고, rolling update의 모든 committed
  owner는 `N+1` target만 가리킨다. `N+2`는 requested version보다 더 높아도 rolling update 후보가 아니며,
  `N`은 rolling update 후보가 아니다. Version filter 뒤 남은 후보에만 capacity와 weight를 적용한다.
  각 result의 mode와 effective target application version이 요청과 일치한다.
- 세부 동작: mode별 exact version filter가 weight·capacity selection보다 먼저 적용되는지 검증한다.

#### OBS-C11 Concurrent Relocate option 충돌

우선순위: `P0`

**검증 질문:** concurrent caller가 같은 relocation intent에는 합류하고 다른 intent로 진행 중인 operation을
변경하지 못하는가.

- 절차: `RollingUpdate`, `TargetApplicationVersion=N+1`의 첫 `Relocate`를 target readiness gate에서
  대기시킨다. 같은 mode와 target version의 두 번째 호출, `PlannedMaintenance` 호출, target version `N+2`를
  지정한 `RollingUpdate` 호출을 차례로 시작한다. 그 뒤 readiness gate를 해제한다.
- 검증: 같은 mode와 effective target version의 두 번째 호출은 최초 deadline을 사용하는 shared operation에
  합류하고 두 waiter가 동일한 terminal result를 받는다. 다른 두 호출은 즉시
  `Blocked/OperationInProgress`를 받고 host state, target selection과 최초 deadline을 변경하지 않는다.
  `OperationInProgress`의 reason 값은 `10`이고 `ShutdownRequested=9`는 유지된다. 첫 operation이 끝난 뒤에는
  새 option으로 별도의 `Relocate`를 시작할 수 있다.
- 세부 동작: 같은 intent의 single-flight와 다른 intent의 deterministic rejection.

## 5. 완료 기준

- OBS-A1~A4, OBS-B1~B4, OBS-C1~C11을 모두 통과한다. 우선순위는 실행 순서만 정하며 완료 범위를
  줄이지 않는다.
- flow 로그는 노드 경계를 관통하고 error 라인에도 `flow=`가 있다.
- 메트릭 계기는 실제 사건과 일치하고 고카디널리티 라벨이 없다.
- `Relocate`는 mode가 정한 exact application version과 automatic peer의 exact readiness를 만족하는 target을
  준비한 뒤 배치를 제외하고 accepted turn·Actor·Spot·STREAM continuity를 옮겨 source를 `Relocated`로 유지한다.
  Manual topology는 precommit
  blocker이며 `Shutdown`은 `Serving` 또는 `Relocated`에서 새 relocation 없이 Spot closing callback과 bounded
  cleanup을 수행한다.
- 공개 표면만 직접 사용하고 `ensure`로 단언한다.
