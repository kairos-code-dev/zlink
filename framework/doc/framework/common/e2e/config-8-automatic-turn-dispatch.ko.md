<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Monitoring](config-7-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

# Config 8 — 자동 turn dispatch 배포

Spot과 Entry Spot handler가 request, actor join, worker 작업의 단일 완료 terminator를 실제
배포 구성에서 기다릴 때 framework가 실행 turn을 자동으로 관리하는지 검증한다. 호출자는
turn을 유지하거나 반납하는 방식을 선택하지 않는다. framework가 교착을 피하는 대기 방식과
continuation의 복귀 위치를 결정한다.

이 config는 모든 framework 언어가 같은 사용성과 실행 의미를 제공하는지 확인한다. 언어별
terminator 이름은 달라도 하나의 완료 경로만 제공해야 한다. 기다리는 동안 진행 가능한 다른
mailbox 작업은 계속 처리하고, 같은 actor와 같은 timer에는 재진입하지 않으며, 완료 뒤
continuation은 원래 실행 문맥에서 재개해야 한다.

## 1. 목적과 범위

- 다룬다: 단일 완료 terminator, Spot outbound request 대기, actor join 대기, worker offload 대기,
  actor mailbox 격리, timer mailbox 격리, local/remote Spot topology,
  route bridge를 통한 handler 처리, timeout·cancellation·shutdown 때의 cleanup.
- 여기서 다루지 않는다: 처리량 수치 비교(공통 perf 문서), codec 변주(Config 4), location store
  장애/복구(Config 6), 장기 장애 복구(Config 5).
- public contract 근거는 공통 비동기 실행 정책의 단일 terminator와 자동 turn 관리 규칙이다.
  `.NET`의 `Async(...)`처럼 언어별 정식 interface에 고정된 이름을 사용한다. 별도 `Yield`,
  blocking wait 또는 callback completion 경로를 만들어 같은 작업의 실행 방식을 선택하게 하지 않는다.
- 이 config에서 turn 관리 대상은 Spot outbound request, actor join request, worker offload request다.
  일반 send/publish는 one-way submit 계약을 검증하고, 외부 HTTP 호출이나 사용자가 만든 임의 async
  작업을 framework turn 관리 대상으로 가장하지 않는다.

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 사용하는 공유 Redis instance. 실행마다 전용 key prefix. 각 노드는 `AddLocationStore(new ZLinkRedisLocationStore(...))`로 등록하고, peer/spot location row는 framework lifecycle이 자동 갱신한다. |
| play 노드 | 2 (`play-a`, `play-b`) | Entry Spot + `AwaitProbeSpot` + actor mailbox + timer handler + worker offload. SpotNode는 router와 pub/sub를 켜고 peer/spot location row를 자동 등록한다. |
| delay service | 2 (`delay-a`, `delay-b`) | client-server channel request를 받아 지정한 시간 뒤 reply한다. handler가 `await`로 기다릴 외부 I/O 역할을 한다. route mesh request로는 `await`를 검증하지 않는다. |
| session gateway | 2 (`session-a`, `session-b`) | stream session을 받고 play 노드에 시나리오 packet을 relay한다. actor bind/relay는 ATD-D4에서 검증한다. |
| consumer | 시나리오별 | 언어별 stream connector로 session gateway에 연결한다. HTTP endpoint는 health/evidence 조회에만 쓰고, await 시나리오 시작은 TicTacToe/Bingo 샘플처럼 실제 connector request로 보낸다. framework client를 직접 들고 Spot을 호출하지 않는다. |

`AwaitProbeSpot`은 상태를 가진 작은 probe spot이다. handler는 evidence에 `started`,
`await-released`, `resumed`, `completed` 같은 marker를 남긴다. actor와 timer handler도 자기
mailbox id, 실행 순서, 처리 노드, request id를 evidence로 남긴다.

delay service와 session gateway는 실제 역할 server다. client가 이 서버들을 테스트 driver로 쓰지는
않는다. consumer는 stream connector로 session gateway에 연결하고, connector request packet으로
시나리오를 시작한다. session gateway는 play 노드로 요청을 relay하고, play 노드의 Spot handler가 공개
framework API로 delay service에 request를 보낸 뒤 언어별 단일 terminator로 기다린다.
이 구조는 Bingo/TicTacToe 샘플처럼 실제 client connection에서 들어온 요청이 await handler까지 도달하는
경로를 검증하기 위한 것이다.

worker offload 시나리오는 delay service를 쓰지 않는다. handler가 framework worker pool에 작업을
맡기고 `await`로 기다리는 동안 같은 Spot의 다른 작업이 진행되는지 확인한다. 이 흐름도 임의 `Task`나
외부 thread wrapper가 아니라 framework가 제공하는 worker call object로만 검증한다.

## 3. 실행 모델

`run_e2e.sh`가 Redis(전용 key prefix) 준비 → delay service → play 노드 → session gateway 순으로
띄우고 client 시나리오를 순차 실행한다. 각 시나리오는 session gateway의 stream endpoint에
connector로 접속한 뒤 request packet을 보내 Spot/actor/timer 작업을 만든다. 실행이 끝나면 전용
prefix의 key를 정리하거나 disposable Redis instance를 버린다.

언어별 구현은 TicTacToe/Bingo 샘플처럼 실제 client connector 연결을 통해 시나리오를 시작해야 한다.
테스트 driver가 play 노드의 route client, Spot manager, actor manager를 직접 들고 시작 packet을 만들면
session gateway, stream framing, actor bind/relay 경로를 건너뛰므로 이 config를 통과한 것으로 보지 않는다.
HTTP endpoint는 process readiness와 evidence 조회에만 둔다. await 동작을 시작하는 HTTP endpoint나
HTTP client 호출을 추가하지 않는다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남긴다. message flow 추적을 지원하는 언어는 최소 `key_transitions`로 켜고, 아직
지원하지 않는 언어도 server evidence에 turn id와 mailbox id를 남겨 원인 레이어를 나눌 수 있게 한다.

## 4. 시나리오

### Track A — 단일 terminator와 자동 turn 관리

#### ATD-A1 완료 terminator는 하나만 노출한다

우선순위: `P0`

**한마디로:** request call이 완료 방식 선택 없이 정식 terminator 하나로 실행되는가.

- 절차: contract test와 배포 fixture에서 request, actor join, worker call의 public terminator를
  inventory한다. `AwaitProbeSpot` handler는 정식 terminator로 delay service 응답을 기다린다.
- 검증: 구형 `Yield` 계열과 blocking/callback 대안이 package에 없고, request가 정상 완료된다.
- 세부 동작: 단일 완료 경로와 package surface.

#### ATD-A2 framework가 기다리는 turn을 자동으로 관리한다

우선순위: `P0`

**한마디로:** 호출자가 실행 방식을 지정하지 않아도 진행 가능한 작업과 continuation 순서가 유지되는가.

- 절차: `AwaitReq` handler가 delay service request를 정식 terminator로 기다린다. 기다리는 동안
  같은 spot rid로 독립 `ProbeReq`를 보낸다.
- 검증: evidence 순서는 `await-started` → `await-released` → `probe-started` →
  `probe-completed` → `await-resumed` → `await-completed` 이다. `await-resumed` 이후 상태 변경은
  같은 spot mailbox에서 직렬로 반영된다.
- 세부 동작: framework가 소유하는 turn 대기와 continuation 재개.

#### ATD-A3 await 후 continuation은 원래 handler context를 유지한다

우선순위: `P0`

**한마디로:** `await` 전후의 request id와 target spot이 같은 handler 흐름으로 이어지는가.

- 절차: `AwaitReq`에 correlation id를 넣고 target spot rid로 delay service reply를 기다린다.
- 검증: `await` 전 marker와 `await` 후 marker가 같은 request id를 공유한다. continuation에서 읽은
  spot rid가 request 시작 시점의 target spot과 일치한다.
- stream metadata나 cancellation token 상태를 Spot request handler public context에서 직접 읽는 검증은
  이 시나리오에 넣지 않는다. 현재 public Spot request handler 표면은 stream metadata를 직접 노출하지
  않으므로, 이 값을 요구하면 각 언어가 내부 helper나 raw metadata 경로를 만들게 된다. metadata 보존이
  필요하면 actor/session metadata context처럼 별도 public contract로 먼저 정한다.
- 세부 동작: await continuation context 보존.

#### ATD-A4 worker offload await는 Spot turn을 붙잡지 않는다

우선순위: `P0`

**한마디로:** handler가 framework worker pool 작업을 `await`로 기다리는 동안 같은 Spot의 독립 작업이 실행되는가.

- 절차: `WorkerAwaitReq` handler가 framework worker call object로 blocking 작업을 시작하고 `await`
  terminator로 기다린다. worker가 끝나기 전에 같은 spot rid로 `ProbeReq`를 보낸다.
- 검증: evidence 순서는 `worker-await-started` → `worker-await-released` → `probe-started` →
  `probe-completed` → `worker-await-resumed` → `worker-await-completed` 이다. worker 작업은 별도
  worker thread에서 실행되지만, continuation의 상태 변경은 원래 Spot mailbox에서 직렬로 반영된다.
- 세부 동작: worker offload 대기 중 Spot turn 반납과 원래 dispatcher continuation 재개.

### Track B — actor mailbox 격리

#### ATD-B1 한 actor가 await 중이어도 다른 actor는 처리된다

우선순위: `P0`

**한마디로:** actor A의 handler가 `await`로 대기하는 동안 actor B의 handler가 같은 Spot 안에서 실행되는가.

- 절차: 같은 `AwaitProbeSpot`에 actor A와 actor B를 join한다. actor A에 `AwaitActorReq`를 보내
  delay service를 `await`로 기다리게 하고, 그 사이 actor B에 `FastActorReq`를 보낸다.
- 검증: actor B의 `FastActorReq`가 actor A의 continuation보다 먼저 완료된다. evidence에는 actor A와
  actor B의 mailbox id가 분리되어 남는다.
- 세부 동작: actor별 mailbox 격리와 await 중 다른 actor 진행.

#### ATD-B2 같은 actor는 await 중 재진입하지 않는다

우선순위: `P0`

**한마디로:** actor A의 handler가 `await` 중일 때 actor A의 다음 packet은 continuation 뒤에 처리되는가.

- 절차: actor A에 `AwaitActorReq`를 보낸 직후 같은 actor A에 `FastActorReq`를 보낸다.
- 검증: 같은 actor A의 `FastActorReq`는 `AwaitActorReq` continuation과 completion 뒤에 실행된다.
  같은 actor evidence에 overlapping marker가 없어야 한다.
- 세부 동작: 같은 actor mailbox 재진입 금지.

#### ATD-B3 actor join 대기는 Entry Spot을 막지 않는다

우선순위: `P0`

**한마디로:** Entry Spot admission handler가 actor `JoinSpot` 또는 `JoinEntrySpot` 완료를 기다릴 때 다른 입장 요청이 막히지 않는가.

- 절차: Entry Spot handler가 actor join call object의 정식 완료 terminator로 user spot join을 기다린다.
  첫 번째 actor의 join 대기 중 두 번째 actor의 독립 admission request를 보낸다.
- 검증: 두 번째 admission request가 첫 번째 actor join continuation 전에 Entry Spot에서 처리될 수 있다.
  다만 같은 actor에 대한 후속 packet은 join continuation 뒤에 처리된다.
- 세부 동작: actor join 대기와 Entry Spot turn 관리.

### Track C — timer mailbox 격리

#### ATD-C1 timer가 await 중이어도 다른 timer는 실행된다

우선순위: `P0`

**한마디로:** timer A handler가 `await`로 대기하는 동안 timer B handler가 같은 Spot에서 실행되는가.

- 절차: 같은 spot에 timer A와 timer B를 등록한다. timer A는 delay service request를 `await`로
  기다리고, timer B는 짧은 marker만 남긴다.
- 검증: timer B marker가 timer A continuation보다 먼저 남는다. 두 timer의 mailbox id와 tick id가
  evidence에 분리되어 기록된다.
- 세부 동작: timer별 mailbox 격리와 await 중 다른 timer 진행.

#### ATD-C2 같은 timer는 await 중 다음 tick으로 재진입하지 않는다

우선순위: `P0`

**한마디로:** timer A handler가 `await` 중일 때 같은 timer A의 다음 tick은 continuation 뒤에 처리되는가.

- 절차: timer A의 주기를 delay service 대기 시간보다 짧게 설정한다. timer A handler는 `await`로
  delay service reply를 기다린다.
- 검증: 같은 timer A의 다음 tick은 이전 tick continuation과 completion 뒤에 실행된다. tick id가
  겹치거나 같은 timer handler가 동시에 실행된 marker가 없어야 한다.
- 세부 동작: 같은 timer mailbox 재진입 금지와 overrun 정책의 기본 격리.

#### ATD-C3 actor와 timer는 서로 다른 mailbox로 진행된다

우선순위: `P1`

**한마디로:** actor handler가 `await` 중일 때 timer handler가 실행되고, timer가 `await` 중일 때 다른 actor handler가 실행되는가.

- 절차: actor A의 `AwaitActorReq`와 timer A의 await tick을 각각 만든 뒤 반대쪽에 빠른 작업을 보낸다.
- 검증: actor와 timer는 같은 spot 상태를 최종 반영할 때만 직렬 순서를 지키고, 서로의 mailbox가
  await 대기 때문에 전체적으로 막히지 않는다.
- 세부 동작: actor mailbox와 timer mailbox의 독립 진행.

### Track D — topology별 await 처리

#### ATD-D1 local Spot topology에서 await가 동작한다

우선순위: `P0`

**한마디로:** delay service와 target Spot이 같은 play 노드에 있을 때 await 흐름이 정상인가.

- 절차: `play-a`의 `AwaitProbeSpot`이 같은 프로세스 또는 같은 노드의 delay service에 request를 보내고
  `await`로 기다린다.
- 검증: Track A~C의 핵심 marker가 local topology에서 모두 성립한다.
- 세부 동작: local handler dispatch와 await 재개.

#### ATD-D2 remote Spot topology에서 await가 동작한다

우선순위: `P0`

**한마디로:** handler가 원격 노드의 service reply를 `await`로 기다려도 원래 Spot mailbox에서 재개되는가.

- 절차: `play-a`의 handler가 `play-b` 쪽 delay service 또는 원격 Spot request를 보내고 `await`로
  기다린다.
- 검증: reply는 원격 노드에서 돌아오고, continuation은 `play-a`의 원래 spot/actor/timer mailbox에서
  재개된다. `play-b`에는 continuation marker가 남지 않는다.
- 세부 동작: cross-node request reply와 await continuation 소유권.

#### ATD-D3 route bridge 경유 handler에서도 await가 동작한다

우선순위: `P1`

**한마디로:** 외부 route mesh channel이 target Spot으로 보낸 request handler 안에서도 await 의미가 같은가.

- 절차: consumer가 session gateway의 stream connector request를 보낸다. session gateway가 route mesh
  channel로 `play-b`의 target Spot에 request를 relay하고, target Spot handler는 delay service
  request를 `await`로 기다린다.
- 검증: route bridge를 경유해 들어온 handler도 turn 반납, 다른 mailbox 진행, 원래 target Spot
  mailbox continuation 재개가 모두 성립한다. 일반 route packet과 spot route packet은 서로 오염되지
  않는다.
- 세부 동작: route bridge ingress와 await dispatch 결합.

#### ATD-D4 session relay로 들어온 actor handler에서도 await가 동작한다

우선순위: `P1`

**한마디로:** stream session에서 actor로 relay된 packet handler가 await를 써도 session reply와 actor push가 올바른 경로로 돌아오는가.

- 절차: stream client가 session gateway에 붙고 actor에 bind한다. client packet은 session handler를
  통해 actor로 relay되고, actor handler는 delay service request를 `await`로 기다린다.
- 검증: actor handler continuation은 actor가 사는 play 노드에서 재개된다. reply와 push는 bound
  session으로만 돌아오고, bind하지 않은 session에는 오지 않는다.
- 세부 동작: session relay + actor await + bound session reply/push 경로.

### Track E — 실패와 cleanup

#### ATD-E1 await 중 timeout은 turn을 영구 점유하지 않는다

우선순위: `P0`

**한마디로:** `await`로 기다리던 request가 timeout 되어도 mailbox가 다시 진행되는가.

- 절차: delay service가 request timeout보다 늦게 reply하도록 설정하고 handler가 `await`로 기다린다.
  timeout 뒤 같은 spot/actor/timer에 빠른 request를 보낸다.
- 검증: timeout은 public error로 관찰되고, 해당 mailbox는 다음 작업을 처리한다. pending continuation이
  뒤늦게 상태를 다시 바꾸지 않는다.
- 세부 동작: await timeout cleanup.

#### ATD-E2 await 중 cancellation은 continuation을 중단하고 다음 작업을 진행시킨다

우선순위: `P1`

**한마디로:** cancellation token이 취소되면 await 대기와 continuation이 정리되고 mailbox가 풀리는가.

- 절차: handler가 `await`로 기다리는 동안 client request cancellation 또는 server-side cancellation을
  발생시킨다.
- 검증: handler는 취소 상태를 public error 또는 정해진 cancellation result로 끝낸다. 같은 mailbox의
  다음 작업은 정상 처리되고, 취소된 continuation이 reply나 push를 중복 전송하지 않는다.
- 세부 동작: await cancellation cleanup.

#### ATD-E3 await 중 runtime shutdown은 무한 대기하지 않는다

우선순위: `P1`

**한마디로:** await 대기 중인 handler가 있을 때 프로세스를 정상 종료해도 drain이 끝나거나 정해진 오류로 닫히는가.

- 절차: client stream connector가 session gateway로 shutdown scenario request를 보내고, play 노드의
  `AwaitReq`가 delay service를 기다리는 동안 play 노드를 정상 종료한다.
- 검증: shutdown은 무한 대기하지 않는다. pending await 작업은 정해진 closed/cancelled 오류로 정리되고,
  다음 실행에서 같은 routing id를 다시 사용할 수 있다.
- 세부 동작: runtime shutdown과 pending await cleanup.

#### ATD-E4 await가 허용되지 않는 표면은 E2E 구현에서 쓰지 않는다

우선순위: `P0`

**한마디로:** route mesh send/request, 일반 send/publish, 외부 HTTP 호출, 사용자가 만든 임의 async 작업을 await로 우회하지 않는가.

- 절차: 각 언어 구현의 Config 8 server code와 client code를 정적 검증한다.
- 검증: `await`는 framework가 Spot/Entry Spot handler turn과 연결해서 만든 call object에만 사용한다.
  허용 대상은 Spot outbound request, actor join request, worker offload request이다. 일반 send/publish,
  route mesh request, 임의 `Task`/`Promise`/`CompletionStage`, 외부 HTTP client async 호출에는
  await wrapper를 만들지 않는다. 필요한 공개 API가 없으면 feature-map gap으로 남긴다.
- 세부 동작: await 공개 API 허용 범위 검증.

#### ATD-E5 언어별 await 의미 동등성

우선순위: `P0`

**한마디로:** 같은 시나리오 id는 모든 framework 언어에서 같은 mailbox·timeout·cancellation 의미를 갖는가.

- 절차: 각 언어 구현은 Config 8 report를 같은 시나리오 id(`ATD-A1` 등)와 공통 marker 이름으로 남긴다.
  메서드 이름은 언어별 public API를 따르되, marker 이름과 성공 조건은 공통 문서의 정의를 따른다.
- 검증: 한 언어의 report는 그 언어가 구현한 scenario id와 marker 이름이 공통 정의에 맞는지 증명해야 한다.
  여러 언어의 report를 한 번에 모아 비교하는 aggregation은 이 config의 입력이지만, 개별 언어 runner 안에서
  직접 수행하지 않는다. 특정 언어만 다른 동작을 보이면 그 언어 구현은 완료가 아니라 common contract gap이다.
- 세부 동작: 언어별 API 이름 차이와 공통 runtime 의미 분리. cross-language aggregation은 모든 언어 report가
  준비된 뒤 별도 parity gate에서 수행한다.

## 5. 완료 기준

- Track A~E의 `P0` 시나리오가 모든 framework 언어에서 같은 의미로 통과한다.
- 시나리오 시작 packet은 client stream connector에서 session gateway로 들어가야 한다. HTTP endpoint는
  health와 evidence 조회에만 쓰며, await 시나리오를 시작하는 HTTP endpoint를 두지 않는다.
- evidence에는 request id, mailbox id, handler kind(spot/actor/timer), node id, `await-released`,
  `resumed`, `completed` marker가 남아야 한다.
- request, actor join, worker call이 하나의 완료 terminator만 노출하고 framework가 turn 대기와
  continuation 복귀를 자동으로 처리한다는 것을 각각 별도 evidence로 증명한다.
- 같은 actor와 같은 timer의 재진입 금지, 다른 actor와 다른 timer의 진행 가능성을 둘 다 검증한다.
- 실패 경로(timeout/cancellation/shutdown)는 client가 받은 public error와 server evidence를 함께
  확인한다.
- 지원하지 않는 언어는 test skip으로 끝내지 않고 feature-map에 공통 contract gap과 구현 계획을
  남긴다. gap은 완료 판정이 아니라 후속 parity 작업의 입력이다.
