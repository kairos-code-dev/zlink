<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Store 장애·복구](config-6-store-failure-recovery.ko.md) | [다음: 실행 turn과 terminator](config-8-execution-turn.ko.md)
<!-- framework-adapter-nav:end -->

# Config 7 — Runtime Monitoring 배포

운영 중인 MeshNode의 peer·channel readiness와 runtime health를 공개 status와 status stream으로
관찰한다. Logical Multicast는 publish 전용 monitoring을 만들지 않는지도 함께 확인한다. 의미의 정본은
[Runtime Monitoring](../spec/24-runtime-monitoring.ko.md)이며, 이 문서는 새로운 monitoring
source나 event kind를 정의하지 않는다.

언어별 E2E는 아래 exact interface가 정한 public runtime 표면만 사용한다.

| 언어 | 정식 interface |
|---|---|
| C++ | [`route_mesh_runtime_t`](../spec/server/languages/cpp/interfaces/08-monitoring.ko.md) |
| .NET | [`IZLinkRouteMeshRuntime`](../spec/server/languages/dotnet/interfaces/10-topology-monitoring.ko.md) |
| Java | [Java monitoring](../spec/server/languages/java/interfaces/monitoring.ko.md) |
| Kotlin | [Kotlin monitoring](../spec/server/languages/kotlin/interfaces/monitoring.ko.md) |
| Node.js | [`ZLinkRouteMeshRuntime`](../spec/server/languages/node/interfaces/03-location-observability.ko.md) |

## 1. 목적과 범위

- 다룬다: RouteMesh와 host status, peer·channel readiness 전이, Logical Multicast monitoring 부재,
  Location Store 장애가 topology status에 미치는 영향, status sequence와 observer 격리.
- 여기서 다루지 않는다: socket 내부 monitor event, registry·Spot별 monitoring source, 집계 metric catalog와
  exporter(Config 11), message flow trace(Config 11), host `Relocate`·`Shutdown`의 전체 state machine(Config 11).
- `GetStatus`가 반환한 값은 조회 시점의 완전한 status다. `Observe` stream도 일부 field만 담은 event가
  아니라 완전한 status를 전달한다. Structured log는 변화 이유를 확인하는 진단 자료이며 현재 상태의
  기준으로 사용하지 않는다.

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|---|---|---|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix를 사용한다. |
| service MeshNode | 2 (`svc-a`, `svc-b`) | 같은 MeshName과 ChannelName에 참여한다. Spot subscription, 지연 가능한 handler, runtime status·structured log evidence endpoint를 제공한다. |
| trigger | 시나리오별 | public ChannelName·Logical Multicast operation과 process lifecycle 조작으로 상태 전이를 만든다. |

각 service host는 자기 DI container의 RouteMesh runtime과 host framework runtime을 사용한다. 다른 process의
runtime 객체를 직접 열거나 내부 socket monitor를 조립하지 않는다. RouteMesh status evidence에는
sequence와 source MeshName을 함께 기록한다. Host status에는 별도 sequence를 기록하며 RouteMesh sequence와
서로 비교하지 않는다.

## 3. 실행 모델

`run_e2e.sh`가 Redis와 `svc-a`를 시작한 뒤 status observer를 열고 `svc-b`를 추가한다. 각 시나리오는
public operation의 terminal result, status stream과 후속 `GetStatus` 결과를 함께 확인한다. Backpressure는
언어별 exact interface가 공개한 queue·timeout 설정과 bounded application gate로 만들며, private hook이나
raw frame을 사용하지 않는다.

로그는 [README](README.ko.md) §6대로 모든 process가 `log/`에 남긴다. 로그 문자열은 진단 자료이며
status field와 operation result를 대신하지 않는다.

## 4. 시나리오

### Track A — status와 readiness

#### MON-A1 일관된 RouteMesh와 host status

우선순위: `P0`

**검증 질문:** RouteMesh status가 peer·channel·placement 상태를 한 값으로 제공하고, host lifecycle
상태는 MeshName과 무관한 framework runtime status 한 곳에서 제공되는가.

- 절차: `svc-a`만 실행한 baseline과 `svc-b`가 ready가 된 뒤 `svc-a`의 RouteMesh status를 각각 읽고, 같은
  host의 framework runtime status도 별도로 읽는다.
- 검증: RouteMesh status는 MeshName, topology state, ready 여부, ready peer 수, Channel별 ready target 수,
  peer의 Node RID·state와 이 process의 active Actor·Spot 수를 포함한다. Endpoint, descriptor revision,
  lifecycle generation, claim·reservation 단계와 Location Store record는 포함하지 않는다. Host status는
  MeshName 없이 runtime state, ready 여부, 새 작업 수락 여부, deadline, relocation 결과와 shutdown 결과를
  제공한다. 두 번째 RouteMesh sequence는 같은 MeshName source의 첫 값보다 크고, host sequence는 같은 host
  source 안에서만 비교한다. 반환된 status는 후속 호출 뒤에도 바뀌지 않는
  immutable value다.
- 세부 동작: [Runtime Monitoring §2](../spec/24-runtime-monitoring.ko.md#2-application이-한-번에-읽는-상태)의
  source별 status와 sequence 계약.

#### MON-A2 peer admission과 ready 전이

우선순위: `P0`

**검증 질문:** Peer의 Node RID와 현재 작업 가능 상태가 공개 status에서 정확히 관찰되는가.

- 절차: observer를 연 뒤 `svc-b`를 시작해 admission과 ready를 기다리고, 정상 종료 후 같은 역할 prefix와
  새 automatic RID·lifecycle generation으로 다시 시작한다.
- 검증: Status stream은 `svc-b`의 peer state가 `Connecting`에서 `Ready`로 바뀐 완전한 status를 전달하고,
  structured log에는 `zlink.runtime.mesh_node.peer_changed`가 기록된다. Public peer status는 Node RID,
  `Connecting|Ready|Draining|Unavailable` state와 unavailable reason만 제공한다. 재시작 뒤 새 Node RID가
  Ready가 되고 이전 Node RID는 ready peer로 남지 않는다. Endpoint, descriptor revision과 lifecycle
  generation은 public status에 노출하지 않는다.
- 세부 동작: public peer identity와 readiness, private discovery fence의 분리.

#### MON-A3 ChannelName readiness와 선택 가능 상태

우선순위: `P0`

**검증 질문:** ready member 수와 ChannelName 선택 가능 여부가 실제 request 결과와 일치하는가.

- 절차: `svc-b`가 ready가 된 뒤 `svc-a`와 `svc-b`의 RouteMesh status를 읽고, `svc-b`의 weight를 0으로
  변경했다가 복원한다. 각 전이 뒤 public ChannelName request를 제출한다.
- 검증: Structured log의 `zlink.runtime.mesh_node.channel_changed`, `svc-a` status의 Channel
  `IsReady`·`ReadyTargetCount`와 실제 request 결과가 일치한다. Weight 0 전파 뒤에는 해당 membership이 새
  select-one 대상에서 제외되고, 복원 뒤 다시 선택될 수 있다. RouteMesh status에 개별 membership weight나
  별도 `selectable` field를 추가하지 않는다.
- 세부 동작: channel readiness와 실제 selection 결과 대조.

#### MON-A4 replacement·failover 중 readiness 복원

우선순위: `P1`

**검증 질문:** 정상 replacement와 비정상 종료 뒤 peer·Channel status가 최신 상태로
수렴하는가.

- 절차: (a) `svc-b`를 정상 종료한 뒤 같은 역할 prefix의 새 automatic RID·generation으로 다시 시작한다.
  (b) 별도 fresh topology에서 `svc-b`를 `SIGKILL`하고 owner lease 만료 뒤 새 RID로 다시 시작한다.
- 검증: 두 경우 모두 status stream 뒤 최신 `GetStatus`가 ready peer와 ready target 수를 정확히
  반영한다. 정상 종료와 비정상 종료의 원인은 process lifecycle evidence와 structured log에서 구분하며
  public status에 별도 종료 원인 field를 추가하지 않는다. 비정상 종료에서는 old descriptor를 성공적인
  ready route로 사용하지 않고,
  계속 실행 중인 `svc-a`의 follow-up request는 bounded terminal result를 얻는다.
- 세부 동작: status stream과 현재 status의 수렴.

#### MON-A5 location runtime health

우선순위: `P1`

**검증 질문:** Store 장애와 복구가 topology status와 고정 structured log identifier로 관찰되는가.

- 절차: 정상 status를 읽은 뒤 harness가 Redis를 정지하고 Store failure grace 전후의 상태를 관찰한
  다음 Redis를 재시작한다.
- 검증: Structured log의 `zlink.runtime.location.store_changed`와 RouteMesh status의
  `Degraded/LocationUnavailable` 전이가 실제 장애와 일치하고, 복구 뒤 topology가 다시 `Ready`로 수렴한다.
  Public status에 Store의 last success·last failure, owner token이나 raw provider record를 추가하지 않는다.
  Store 장애만으로 이미 admitted된 peer와 local queue의 메시지를 즉시 중단하지 않으며, 복구 뒤 Framework가
  descriptor를 현재 owner fence로 재검증한다.
- 세부 동작: [Location Runtime §8](../spec/21-location-runtime.ko.md#8-store-outage와-cancellation)의
  health projection.

#### MON-A6 public placement 집계와 capacity 경계

우선순위: `P0`

**검증 질문:** Public status가 active Actor·Spot 수와 새 placement 가능 여부만 제공하고 내부 reservation
상태를 노출하지 않는가.

- 절차: 작은 Actor total, Spot total과 Spot stable type limit을 설정하고 creation을 factory gate에서
  대기시킨다. Entry Spot에 Actor를 추가하고, gate 전·후와 public create가 capacity에서 거부된 뒤
  RouteMesh status를 읽는다.
- 검증: `Placement.ActiveActorCount`는 Entry Spot member Actor를 포함하고,
  `Placement.ActiveSpotCount`는 Entry Spot을 제외한다. `Placement.IsAvailable`과
  `UnavailableReason=CapacityExceeded`는 실제 public create 결과와 일치한다. Status에는 stable type별
  active·reserved·limit, activation concurrency와 reservation row를 노출하지 않는다.
- 세부 동작: public placement 집계와 Framework 내부 capacity transaction의 경계.

### Track B — Publish 전용 monitoring 부재

#### MON-B1 remote target 실패와 monitoring 부재

우선순위: `P0`

**검증 질문:** Remote ROUTER target이 송신을 수락할 수 없어도 publish 전용 status field·metric·structured
log를 만들지 않는가.

- 절차: `svc-b` 방향 ROUTER 송신 HWM에 도달하도록 수신을 막고 Logical Multicast를 짧은 send
  timeout으로 제출한다. 다른 remote target은 수락 가능한 상태로 둔다.
- 검증: Publish operation은 source-local capacity를 확보해 작업을 시작하면 반환 데이터 없이 정상
  완료한다. RouteMesh status에 Logical Multicast 통계 객체가 없고 publish target count field도 없다.
  `zlink.runtime.mesh_node.multicast_backpressured`, `zlink.runtime.mesh_node.multicast_dropped`와
  `zlink.mesh_node.multicast.*` metric은 발생하지 않는다. 앞에서 수락된 target의 payload는 취소되지 않고
  전체 publish를 rollback하거나 자동 재시도하지 않는다.
- 세부 동작: [Spot Messaging §4.1](../spec/12-spot-messaging.ko.md#41-publish-시작과-완료)과
  [Runtime Monitoring §5](../spec/24-runtime-monitoring.ko.md#5-structured-log)의
  publish 전용 monitoring 제거 계약.

#### MON-B2 local target drop과 monitoring 부재

우선순위: `P0`

**검증 질문:** 용량이 없는 local target이 있어도 target별 결과를 public monitoring에 집계하지 않는가.

- 절차: 하나의 matching target은 수락 가능하게 두고 다른 target은 bounded queue가 가득 찬 상태로 만든
  뒤 publish한다.
- 검증: 수락 가능한 target은 payload를 한 번 처리하고 막힌 target은 처리하지 않는다.
  Public publish terminal은 결과값 없이 정상 완료하며 RouteMesh status, structured log, metric과
  message-flow trace에 local target count나 drop count가 없다. 전체 publish를 rollback하거나 자동
  재시도하지 않는다.
- 세부 동작: 부분 전달과 public monitoring 부재의 분리.

### Track C — status observer 격리

#### MON-C1 느린 status observer 격리

우선순위: `P1`

**검증 질문:** 느린 status observer가 있어도 request completion, topology 변화와 다른 observer가
진행되는가.

- 절차: `svc-a`의 application handler를 bounded gate에서 대기시킨 상태로 peer lifecycle 변화와 별도
  request completion을 유발한다. 같은 MeshName에 느린 observer와 정상 observer를 함께 열고 peer·channel
  상태를 반복 변경한다. 느린 consumer 한 곳은 받은 status 처리 중 예외를 발생시켜 자체 enumeration을
  종료한다.
- 검증: Request terminal result와 정상 observer, messaging은 느린 consumer와 독립적으로 진행된다.
  각 stream item은 완전한 immutable status다. Coalescing으로 sequence gap이 생겨도 정상 observer가
  `GetStatus`를 다시 읽으면 최신 peer·channel 상태와 일치한다. Public status에 observer queue
  backpressure·drop counter, claim progress나 Logical Multicast 통계를 추가하지 않는다.
- 세부 동작: [Runtime Monitoring §3](../spec/24-runtime-monitoring.ko.md#3-현재-상태-조회와-변화-관찰)의
  완전한 status stream, coalescing과 observer 격리.

### Track D — validation과 반복 장애

#### MON-D1 public validation과 장애 반복 중 연속성

우선순위: `P1`

**검증 질문:** 잘못된 public 호출은 명확히 거부되고, 반복 장애 뒤에도 status stream과 현재 status가
복구되는가.

- 절차: (a) 등록하지 않은 MeshName의 `GetStatus`와 `Observe`를 요청한다. (b) 정상 observer에서는
  `svc-b` 비정상 종료→lease 만료→재시작 cycle을 세 번 반복하며 각 단계의 status를
  읽는다.
- 검증: (a)는 구성 오류 또는 호출 인자 오류로 실패한다. (b)는 cycle마다 peer·channel event가 이어지고
  같은 source의 sequence가 단조 증가하며 최신 status가 실제 ready 상태와 일치한다. Status에는 payload와
  application metadata가 복사되지 않는다. Node RID는 peer 진단 값으로만 제공하고 endpoint는 public
  status에 포함하지 않는다. 둘 다 metric label 검증 근거로 사용하지 않는다.
- 세부 동작: startup validation, immutable status와 반복 장애 후 resync.

## 5. 완료 기준

- `P0`인 MON-A1·A2·A3·A6·B1·B2를 모두 통과한다.
- 각 판정은 public operation result, status stream과 후속 `GetStatus`를 함께 사용한다.
- Structured log identifier와 닫힌 state 값은 Runtime Monitoring 정식 계약과 byte 단위로 일치한다.
- publish operation의 backpressure와 target별 drop을 같은 결과·event로 합치지 않는다.
- 한 observer의 지연·예외가 다른 observer, message dispatch와 reply를 바꾸지 않는다.
- 언어별 exact interface에 없는 monitoring source, server-side proxy API, raw frame 또는 private helper를
  추가하지 않는다.
