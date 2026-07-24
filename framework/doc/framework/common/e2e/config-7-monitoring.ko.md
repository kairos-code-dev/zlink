<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Store 장애·복구](config-6-store-failure-recovery.ko.md) | [다음: 실행 turn과 terminator](config-8-execution-turn.ko.md)
<!-- framework-adapter-nav:end -->

# Config 7 — Runtime Monitoring 배포

운영 중인 MeshNode의 peer·channel readiness, Logical Multicast backpressure·drop과 runtime health를
공개 snapshot과 typed event로 관찰한다. 의미의 정본은
[Runtime Monitoring](../../spec/server/50-runtime-monitoring.ko.md)이며, 이 문서는 새로운 monitoring
source나 event kind를 정의하지 않는다.

언어별 E2E는 아래 exact interface가 정한 public runtime 표면만 사용한다.

| 언어 | 정식 interface |
|---|---|
| C++ | [`route_mesh_runtime_t`](../../spec/server/languages/cpp/interfaces/08-monitoring.ko.md) |
| .NET | [`IZLinkRouteMeshRuntime`](../../spec/server/languages/dotnet/interfaces/10-topology-monitoring.ko.md) |
| Java | [Java monitoring](../../spec/server/languages/java/interfaces/monitoring.ko.md) |
| Kotlin | [Kotlin monitoring](../../spec/server/languages/kotlin/interfaces/monitoring.ko.md) |
| Node.js | [`ZLinkRouteMeshRuntime`](../../spec/server/languages/node/interfaces/03-location-observability.ko.md) |

## 1. 목적과 범위

- 다룬다: 하나의 MeshNode snapshot, peer·channel readiness 전이, Logical Multicast backpressure·drop,
  application·infrastructure claim, location health, event sequence와 observer 격리.
- 여기서 다루지 않는다: socket 내부 monitor event, registry·Spot별 monitoring source, 집계 metric catalog와
  exporter(Config 11), message flow trace(Config 11), host `Retire`·`Shutdown`의 전체 state machine(Config 11).
- snapshot은 현재 상태의 authority이고 event는 변화 알림이다. E2E는 event만 보고 최종 상태를
  확정하지 않으며, 항상 같은 MeshNode의 최신 snapshot과 대조한다.

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|---|---|---|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix를 사용한다. |
| service MeshNode | 2 (`svc-a`, `svc-b`) | 같은 MeshName과 ChannelName에 참여한다. Spot subscription, 지연 가능한 handler, runtime snapshot·event evidence endpoint를 제공한다. |
| trigger | 시나리오별 | public ChannelName·Logical Multicast operation과 process lifecycle 조작으로 상태 전이를 만든다. |

각 service host는 자기 DI container의 RouteMesh runtime과 host framework runtime을 사용한다. 다른 process의
runtime 객체를 직접 열거나 내부 socket monitor를 조립하지 않는다. MeshNode snapshot·event evidence에는
sequence와 source MeshName을 함께 기록한다. Host termination snapshot·event에는 host ID와 별도 sequence를
기록하며 MeshName source sequence와 서로 비교하지 않는다.

## 3. 실행 모델

`run_e2e.sh`가 Redis와 `svc-a`를 시작한 뒤 runtime observer를 열고 `svc-b`를 추가한다. 각 시나리오는
public operation의 terminal result, typed runtime event와 후속 snapshot을 함께 확인한다. Backpressure는
언어별 exact interface가 공개한 queue·timeout 설정과 bounded application gate로 만들며, private hook이나
raw frame을 사용하지 않는다.

로그는 [README](README.ko.md) §6대로 모든 process가 `log/`에 남긴다. 로그 문자열은 진단 자료이며
snapshot field, event identifier와 operation result를 대신하지 않는다.

## 4. 시나리오

### Track A — snapshot과 readiness

#### MON-A1 일관된 MeshNode snapshot

우선순위: `P0`

**검증 질문:** MeshNode snapshot이 peer, channel, multicast, claim과 location을 일관되게 제공하고, host 종료
상태는 MeshName과 무관한 framework runtime snapshot 한 곳에서 제공되는가.

- 절차: `svc-a`만 실행한 baseline과 `svc-b`가 ready가 된 뒤 `svc-a`의 MeshNode snapshot을 각각 읽고, 같은
  host의 framework runtime snapshot도 별도로 읽는다.
- 검증: MeshNode snapshot은 MeshName, RID, lifecycle generation, descriptor revision, endpoint, component
  lifecycle state, descriptor source, peer·channel·multicast·claim·location 값을 포함한다. Host framework
  snapshot은 MeshName 없이 runtime state, effective termination intent, deadline, sealed work, pending count와
  terminal result를 한 번 제공한다. 두 번째 MeshNode snapshot sequence는 같은 MeshName source의 첫 값보다
  크고, host sequence는 같은 host source 안에서만 비교한다. 반환된 snapshot은 후속 호출 뒤에도 바뀌지 않는
  immutable value다.
- 세부 동작: [Runtime Monitoring §2](../../spec/server/50-runtime-monitoring.ko.md#2-snapshot)의 source별
  snapshot과 sequence 계약.

#### MON-A2 peer admission과 ready 전이

우선순위: `P0`

**검증 질문:** peer의 generation·descriptor revision과 실제 ready 상태가 분리되어 관찰되는가.

- 절차: observer를 연 뒤 `svc-b`를 시작해 admission과 ready를 기다리고, 정상 종료 후 같은 역할 prefix와
  새 automatic RID·lifecycle generation으로 다시 시작한다.
- 검증: `zlink.runtime.mesh_node.peer_changed` event가 sequence 순서로 기록된다. Snapshot은 peer RID,
  lifecycle generation, descriptor revision, endpoint, admission state, ready와 last failure를 별도 field로
  제공한다. 재시작 뒤 RID와 generation이 모두 바뀌며 old RID·endpoint·generation이 ready peer로 남지 않는다.
- 세부 동작: peer identity와 readiness를 하나의 boolean으로 축약하지 않는다.

#### MON-A3 ChannelName readiness와 선택 가능 상태

우선순위: `P0`

**검증 질문:** ready member 수와 ChannelName 선택 가능 여부가 실제 request 결과와 일치하는가.

- 절차: `svc-b`가 ready가 된 뒤 `svc-a`와 `svc-b`의 channel snapshot을 읽고, `svc-b`의 weight를 0으로
  변경했다가 복원한다. 각 전이 뒤 public ChannelName request를 제출한다.
- 검증: `zlink.runtime.mesh_node.channel_changed` event, `svc-b` snapshot의 local weight와 `svc-a`
  snapshot의 ready member count·selectable 값이 일치한다. Weight 0 전파 뒤에는 해당 membership이 새
  select-one 대상에서 제외되고, 복원 뒤 다시 선택될 수 있다.
- 세부 동작: channel readiness와 실제 selection 결과 대조.

#### MON-A4 replacement·failover 중 readiness 복원

우선순위: `P1`

**검증 질문:** 정상 replacement와 비정상 종료가 peer·channel snapshot에서 구분되고 최신 상태로
수렴하는가.

- 절차: (a) `svc-b`를 정상 종료한 뒤 같은 역할 prefix의 새 automatic RID·generation으로 다시 시작한다.
  (b) 별도 fresh topology에서 `svc-b`를 `SIGKILL`하고 owner lease 만료 뒤 새 RID로 다시 시작한다.
- 검증: 두 경우 모두 peer·channel event 뒤 최신 snapshot이 ready peer와 ready member 수를 정확히
  반영한다. 비정상 종료에서는 lease 만료 전 old descriptor를 성공적인 ready route로 사용하지 않고,
  계속 실행 중인 `svc-a`의 follow-up request는 bounded terminal result를 얻는다.
- 세부 동작: event는 변화 알림이고 snapshot이 최종 상태의 authority다.

#### MON-A5 location runtime health

우선순위: `P1`

**검증 질문:** store 장애와 복구가 MeshNode snapshot과 고정 event identifier로 관찰되는가.

- 절차: 정상 snapshot을 읽은 뒤 harness가 Redis를 정지하고 store failure grace 전후의 상태를 관찰한
  다음 Redis를 재시작한다.
- 검증: `zlink.runtime.location.store_changed` event와 snapshot의 location state, last success, last
  failure가 실제 장애·복구와 일치한다. Store 장애만으로 이미 admitted된 peer와 local queue의 메시지를
  즉시 중단하지 않으며, 복구 뒤 descriptor를 현재 owner token으로 재검증한다.
- 세부 동작: [Location Runtime §8](../../spec/server/40-location-runtime.ko.md#8-store-outage와-cancellation)의
  health projection.

#### MON-A6 typed capacity snapshot과 projection lag

우선순위: `P0`

**검증 질문:** Actor·Spot population과 activation concurrency를 서로 다른 값으로 관찰할 수 있는가.

- 절차: 작은 Actor total, Spot total과 Spot stable type limit을 설정하고 creation을 factory gate에서
  대기시킨다. Entry Spot에 Actor를 추가하고 descriptor publication을 의도적으로 한 polling interval
  지연시킨 상태에서 authoritative Location Store reservation과 runtime snapshot을 읽는다.
- 검증: Snapshot은 Actor total, Spot total과 stable type별 `active`, `reserved`, `limit`을 구분한다.
  Entry Spot은 Spot count에 없고 member Actor는 Actor count에 있다. `limit=0`은 unlimited로 표현하며
  숫자 0개의 여유로 해석하지 않는다. Activation concurrency의 active·limit은 population
  capacity와 다른 field에 있다. Descriptor projection이 stale해도 authoritative reservation 결과와
  혼합하지 않고 다음 snapshot에서 수렴한다.
- 세부 동작: typed capacity와 stale descriptor projection의 관측 경계.

### Track B — Logical Multicast backpressure와 drop

#### MON-B1 remote ROUTER backpressure

우선순위: `P0`

**검증 질문:** remote ROUTER target이 송신을 수락할 수 없을 때 backpressure 결과와 target 집계가
함께 관찰되는가.

- 절차: `svc-b` 방향 ROUTER 송신 HWM에 도달하도록 수신을 막고 Logical Multicast를 짧은 send
  timeout으로 제출한다. 다른 remote target은 수락 가능한 상태로 둔다.
- 검증: operation은 성공으로 가장하지 않고 backpressure 또는 timeout terminal result를 반환한다.
  `zlink.runtime.mesh_node.multicast_backpressured` event가 발생하며 후속 snapshot에서 submitted,
  backpressured와 remote·local snapshot/admitted/dropped 수가 실제 target 결과와 일치한다. 앞에서
  수락된 target의 payload는 취소되지 않는다.
- 세부 동작: [Spot Messaging §4.1](../../spec/server/20-spot-messaging.ko.md#41-target별-수락)과
  [Runtime Monitoring §3](../../spec/server/50-runtime-monitoring.ko.md#3-event-identifiers)의
  target별 제출 계약.

#### MON-B2 local target drop

우선순위: `P0`

**검증 질문:** 용량이 없는 local target과 수락된 target이 분리되어 관찰되는가.

- 절차: 하나의 matching target은 수락 가능하게 두고 다른 target은 bounded queue가 가득 찬 상태로 만든
  뒤 publish한다.
- 검증: 수락 가능한 target은 payload를 한 번 처리하고 막힌 target은 처리하지 않는다.
  `zlink.runtime.mesh_node.multicast_dropped` event와 후속 snapshot의 remote·local snapshot, admitted,
  dropped 수가 실제 target evidence와 일치한다. Backpressure event로 바꾸어 기록하지 않는다.
- 세부 동작: 부분 수락과 target drop의 분리.

### Track C — claim progress와 observer 격리

#### MON-C1 claim progress와 observer 격리

우선순위: `P1`

**검증 질문:** application callback과 느린 observer가 대기 중이어도 infrastructure claim, request
completion과 다른 observer가 진행되는가.

- 절차: `svc-a`의 application handler를 bounded gate에서 대기시킨 상태로 peer lifecycle 변화와 별도
  request completion을 유발한다. 같은 MeshName에는 작은 양수 capacity의 느린 observer와 정상 observer를
  함께 열고, peer·channel 상태를 반복 변경해 bounded queue에 압력을 준다. 느린 consumer 한 곳에서
  application 예외도 발생시킨다.
- 검증: snapshot은 application claim과 infrastructure claim의 active·pending 값을 구분한다.
  `zlink.runtime.mesh_node.claim_changed` event와 request terminal result가 application gate 해제 전에도
  진행되고 정상 observer와 messaging도 계속 동작한다. Coalescing이 발생해도 최신 snapshot sequence와
  backpressure·drop 누계 증가분을 잃지 않는다. Sequence gap을
  발견한 observer가 snapshot을 다시 읽으면 최신 peer·channel·multicast 상태와 일치한다.
- 세부 동작: application·infrastructure claim 분리와
  [Runtime Monitoring §4~5](../../spec/server/50-runtime-monitoring.ko.md#4-event-ordering과-coalescing)의
  coalescing·observer 격리.

### Track D — validation과 반복 장애

#### MON-D1 public validation과 장애 반복 중 연속성

우선순위: `P1`

**검증 질문:** 잘못된 public 호출은 명확히 거부되고, 반복 장애 뒤에도 event와 snapshot이 최신 상태로
복구되는가.

- 절차: (a) 등록하지 않은 MeshName의 snapshot·event stream과 0 이하 capacity를 요청한다. (b) 정상
  observer에서는 `svc-b` 비정상 종료→lease 만료→재시작 cycle을 세 번 반복하며 각 단계의 snapshot을
  읽는다.
- 검증: (a)는 구성 오류 또는 호출 인자 오류로 실패한다. (b)는 cycle마다 peer·channel event가 이어지고
  같은 source의 sequence가 단조 증가하며 최신 snapshot이 실제 ready 상태와 일치한다. Event에는 payload와
  application metadata가 복사되지 않는다. RID와 endpoint 같은 진단 값은 metric label 검증 근거로
  사용하지 않는다.
- 세부 동작: startup validation, bounded event field와 반복 장애 후 resync.

## 5. 완료 기준

- `P0`인 MON-A1·A2·A3·A6·B1·B2를 모두 통과한다.
- 각 판정은 public operation result, typed event와 후속 snapshot을 함께 사용한다.
- Event identifier와 닫힌 state 값은 Runtime Monitoring 정식 계약과 byte 단위로 일치한다.
- publish operation의 backpressure와 target별 drop을 같은 결과·event로 합치지 않는다.
- 한 observer의 지연·예외가 다른 observer, message dispatch와 reply를 바꾸지 않는다.
- 언어별 exact interface에 없는 monitoring source, server-side proxy API, raw frame 또는 private helper를
  추가하지 않는다.
