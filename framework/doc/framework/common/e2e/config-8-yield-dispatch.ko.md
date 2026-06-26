<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Monitoring](config-7-monitoring.ko.md)
<!-- framework-adapter-nav:end -->

# Config 8 — Spot yield dispatch 배포

Spot과 Entry Spot handler가 `yield` 계열 terminator를 실제 배포 구성에서 사용했을 때,
의도한 직렬 실행 의미가 유지되는지 본다. 핵심은 빠른 처리량이 아니라 **현재 handler turn을
반납해도 원래 mailbox 규칙이 깨지지 않는가**이다.

이 config는 모든 framework 언어가 같은 의미로 동작해야 하는 공통 검증이다. 언어마다 메서드
이름은 다를 수 있지만, 어떤 작업에서 turn을 반납하고 어떤 mailbox에서 continuation을 재개하는지는
같아야 한다. 특정 언어가 같은 공개 API나 같은 동작을 아직 제공하지 못하면 그 언어는 이 config를
완료한 것으로 보지 않고, feature-map에 공통 contract gap으로 남긴다.

기본 terminator는 handler가 기다리는 동안 같은 Spot 또는 Entry Spot의 다음 작업을 시작하지
않는다. 반대로 `yield` terminator는 현재 turn을 반납하고, 기다리던 작업이 끝나면 원래
mailbox에서 handler continuation을 재개한다. 이때 같은 actor와 같은 timer는 재진입하지 않아야
하고, 다른 actor나 다른 timer 작업은 막히지 않아야 한다.

## 1. 목적과 범위

- 다룬다: 기본 terminator와 `yield` terminator의 차이, Spot outbound request yield, actor join
  yield, worker offload yield, actor mailbox 격리, timer mailbox 격리, local/remote Spot topology,
  route bridge를 통한 handler 처리, timeout·cancellation·shutdown 때의 cleanup.
- 여기서 다루지 않는다: 처리량 수치 비교(공통 perf 문서), codec 변주(Config 4), registry
  failover(Config 6), 장기 장애 복구(Config 5).
- public contract 근거는 공통 비동기 실행 정책의 `yield` 의미다. 언어별 이름은 각 언어 public API를
  따른다(`Yield(...)`, `yield(...)`, `yield(call, ...)`, `yield()` 등). 이름만 다르고 의미는 같아야
  한다. 특정 언어에 아직 같은 공개 API가 없으면 내부 helper로 메우지 않고 feature-map에 공통
  contract gap으로 남긴다.
- `yield`는 framework가 Spot/Entry Spot handler turn과 연결해서 만든 call object에만 사용한다.
  이 config에서 허용하는 흐름은 Spot outbound request(`RequestToChannel`, `RequestToSpot` 등),
  actor join request(`JoinSpot`, `JoinEntrySpot` 등), worker offload request(`RunWorker` 등)이다.
  각 언어의 메서드 이름은 달라도 의미는 같아야 한다. 일반 send/publish, route mesh request, 외부
  HTTP 호출, 사용자가 만든 임의 async 작업에 `yield` wrapper를 붙여 검증하지 않는다.

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| registry | 1 | discovery server. |
| play 노드 | 2 (`play-a`, `play-b`) | Entry Spot + `YieldProbeSpot` + actor mailbox + timer handler + worker offload. SpotNode는 router와 pub/sub를 켜고 registry에 광고한다. |
| delay service | 2 (`delay-a`, `delay-b`) | client-server channel request를 받아 지정한 시간 뒤 reply한다. handler가 `yield`로 기다릴 외부 I/O 역할을 한다. route mesh request로는 `yield`를 검증하지 않는다. |
| session gateway | 2 (`session-a`, `session-b`) | stream session을 받고 play 노드에 시나리오 packet을 relay한다. actor bind/relay는 YD-D4에서 검증한다. |
| consumer | 시나리오별 | 언어별 stream connector로 session gateway에 연결한다. HTTP endpoint는 health/evidence 조회에만 쓰고, yield 시나리오 시작은 TicTacToe/Bingo 샘플처럼 실제 connector request로 보낸다. framework client를 직접 들고 Spot을 호출하지 않는다. |

`YieldProbeSpot`은 상태를 가진 작은 probe spot이다. handler는 evidence에 `started`,
`yield-released`, `resumed`, `completed` 같은 marker를 남긴다. actor와 timer handler도 자기
mailbox id, 실행 순서, 처리 노드, request id를 evidence로 남긴다.

delay service와 session gateway는 실제 역할 server다. client가 이 서버들을 테스트 driver로 쓰지는
않는다. consumer는 stream connector로 session gateway에 연결하고, connector request packet으로
시나리오를 시작한다. session gateway는 play 노드로 요청을 relay하고, play 노드의 Spot handler가 공개
framework API로 delay service에 request를 보낸 뒤 기본 terminator 또는 `yield` terminator로 기다린다.
이 구조는 Bingo/TicTacToe 샘플처럼 실제 client connection에서 들어온 요청이 yield handler까지 도달하는
경로를 검증하기 위한 것이다.

worker offload 시나리오는 delay service를 쓰지 않는다. handler가 framework worker pool에 작업을
맡기고 `yield`로 기다리는 동안 같은 Spot의 다른 작업이 진행되는지 확인한다. 이 흐름도 임의 `Task`나
외부 thread wrapper가 아니라 framework가 제공하는 worker call object로만 검증한다.

## 3. 실행 모델

`run_e2e.sh`가 registry → delay service → play 노드 → session gateway 순으로 띄우고 client 시나리오를 순차 실행한다.
각 시나리오는 session gateway의 stream endpoint에 connector로 접속한 뒤 request packet을 보내
Spot/actor/timer 작업을 만든다.

언어별 구현은 TicTacToe/Bingo 샘플처럼 실제 client connector 연결을 통해 시나리오를 시작해야 한다.
테스트 driver가 play 노드의 route client, Spot manager, actor manager를 직접 들고 시작 packet을 만들면
session gateway, stream framing, actor bind/relay 경로를 건너뛰므로 이 config를 통과한 것으로 보지 않는다.
HTTP endpoint는 process readiness와 evidence 조회에만 둔다. yield 동작을 시작하는 HTTP endpoint나
HTTP client 호출을 추가하지 않는다.

로그는 [README](README.ko.md) §6(로깅과 메시지 흐름 추적, 필수 공통)대로 모든 프로세스가 `log/`
폴더에 파일로 남긴다. message flow 추적을 지원하는 언어는 최소 `key_transitions`로 켜고, 아직
지원하지 않는 언어도 server evidence에 turn id와 mailbox id를 남겨 원인 레이어를 나눌 수 있게 한다.

## 4. 시나리오

### Track A — 기본 terminator와 yield terminator 비교

#### YD-A1 기본 terminator는 같은 Spot turn을 붙잡는다

우선순위: `P0`

**한마디로:** 기본 terminator로 외부 request를 기다리는 동안 같은 Spot의 다음 작업이 먼저 실행되지 않는가.

- 절차: `YieldProbeSpot`의 `HoldReq` handler가 delay service request를 기본 terminator로 기다린다.
  같은 spot rid로 `ProbeReq`를 바로 보낸다.
- 검증: `ProbeReq`는 `HoldReq` continuation이 끝난 뒤 실행된다. evidence 순서는
  `hold-started` → `hold-resumed` → `hold-completed` → `probe-started` 이다.
- 세부 동작: 기본 terminator가 Spot 직렬 실행 줄을 유지한다.

#### YD-A2 yield terminator는 현재 turn을 반납하고 원래 mailbox에서 재개한다

우선순위: `P0`

**한마디로:** `yield`로 기다리면 같은 Spot의 독립 작업이 먼저 실행되고, 기다리던 handler는 완료 뒤 이어서 재개되는가.

- 절차: `YieldReq` handler가 delay service request를 `yield` terminator로 기다린다. 기다리는 동안
  같은 spot rid로 독립 `ProbeReq`를 보낸다.
- 검증: evidence 순서는 `yield-started` → `yield-released` → `probe-started` →
  `probe-completed` → `yield-resumed` → `yield-completed` 이다. `yield-resumed` 이후 상태 변경은
  같은 spot mailbox에서 직렬로 반영된다.
- 세부 동작: Spot turn 반납과 continuation 재개.

#### YD-A3 yield 후 continuation은 원래 handler context를 유지한다

우선순위: `P0`

**한마디로:** `yield` 전후의 spot rid, cancellation token, message metadata가 같은 handler 흐름으로 이어지는가.

- 절차: `YieldReq`에 correlation id와 사용자 metadata를 넣고 delay service reply를 기다린다.
- 검증: `yield` 전 marker와 `yield` 후 marker가 같은 request id를 공유한다. continuation에서 읽은
  spot rid, metadata, cancellation 상태가 request 시작 시점의 context와 일치한다.
- 세부 동작: yield continuation context 보존.

#### YD-A4 worker offload yield는 Spot turn을 붙잡지 않는다

우선순위: `P0`

**한마디로:** handler가 framework worker pool 작업을 `yield`로 기다리는 동안 같은 Spot의 독립 작업이 실행되는가.

- 절차: `WorkerYieldReq` handler가 framework worker call object로 blocking 작업을 시작하고 `yield`
  terminator로 기다린다. worker가 끝나기 전에 같은 spot rid로 `ProbeReq`를 보낸다.
- 검증: evidence 순서는 `worker-yield-started` → `worker-yield-released` → `probe-started` →
  `probe-completed` → `worker-yield-resumed` → `worker-yield-completed` 이다. worker 작업은 별도
  worker thread에서 실행되지만, continuation의 상태 변경은 원래 Spot mailbox에서 직렬로 반영된다.
- 세부 동작: worker offload 대기 중 Spot turn 반납과 원래 dispatcher continuation 재개.

### Track B — actor mailbox 격리

#### YD-B1 한 actor가 yield 중이어도 다른 actor는 처리된다

우선순위: `P0`

**한마디로:** actor A의 handler가 `yield`로 대기하는 동안 actor B의 handler가 같은 Spot 안에서 실행되는가.

- 절차: 같은 `YieldProbeSpot`에 actor A와 actor B를 join한다. actor A에 `YieldActorReq`를 보내
  delay service를 `yield`로 기다리게 하고, 그 사이 actor B에 `FastActorReq`를 보낸다.
- 검증: actor B의 `FastActorReq`가 actor A의 continuation보다 먼저 완료된다. evidence에는 actor A와
  actor B의 mailbox id가 분리되어 남는다.
- 세부 동작: actor별 mailbox 격리와 yield 중 다른 actor 진행.

#### YD-B2 같은 actor는 yield 중 재진입하지 않는다

우선순위: `P0`

**한마디로:** actor A의 handler가 `yield` 중일 때 actor A의 다음 packet은 continuation 뒤에 처리되는가.

- 절차: actor A에 `YieldActorReq`를 보낸 직후 같은 actor A에 `FastActorReq`를 보낸다.
- 검증: 같은 actor A의 `FastActorReq`는 `YieldActorReq` continuation과 completion 뒤에 실행된다.
  같은 actor evidence에 overlapping marker가 없어야 한다.
- 세부 동작: 같은 actor mailbox 재진입 금지.

#### YD-B3 actor join yield는 Entry Spot을 막지 않는다

우선순위: `P0`

**한마디로:** Entry Spot admission handler가 actor `JoinSpot` 또는 `JoinEntrySpot`을 `yield`로 기다릴 때 다른 입장 요청이 막히지 않는가.

- 절차: Entry Spot handler가 actor join call object의 `yield` terminator로 user spot join을 기다린다.
  첫 번째 actor의 join 대기 중 두 번째 actor의 독립 admission request를 보낸다.
- 검증: 두 번째 admission request가 첫 번째 actor join continuation 전에 Entry Spot에서 처리될 수 있다.
  다만 같은 actor에 대한 후속 packet은 join continuation 뒤에 처리된다.
- 세부 동작: actor join yield와 Entry Spot turn 반납.

### Track C — timer mailbox 격리

#### YD-C1 timer가 yield 중이어도 다른 timer는 실행된다

우선순위: `P0`

**한마디로:** timer A handler가 `yield`로 대기하는 동안 timer B handler가 같은 Spot에서 실행되는가.

- 절차: 같은 spot에 timer A와 timer B를 등록한다. timer A는 delay service request를 `yield`로
  기다리고, timer B는 짧은 marker만 남긴다.
- 검증: timer B marker가 timer A continuation보다 먼저 남는다. 두 timer의 mailbox id와 tick id가
  evidence에 분리되어 기록된다.
- 세부 동작: timer별 mailbox 격리와 yield 중 다른 timer 진행.

#### YD-C2 같은 timer는 yield 중 다음 tick으로 재진입하지 않는다

우선순위: `P0`

**한마디로:** timer A handler가 `yield` 중일 때 같은 timer A의 다음 tick은 continuation 뒤에 처리되는가.

- 절차: timer A의 주기를 delay service 대기 시간보다 짧게 설정한다. timer A handler는 `yield`로
  delay service reply를 기다린다.
- 검증: 같은 timer A의 다음 tick은 이전 tick continuation과 completion 뒤에 실행된다. tick id가
  겹치거나 같은 timer handler가 동시에 실행된 marker가 없어야 한다.
- 세부 동작: 같은 timer mailbox 재진입 금지와 overrun 정책의 기본 격리.

#### YD-C3 actor와 timer는 서로 다른 mailbox로 진행된다

우선순위: `P1`

**한마디로:** actor handler가 `yield` 중일 때 timer handler가 실행되고, timer가 `yield` 중일 때 다른 actor handler가 실행되는가.

- 절차: actor A의 `YieldActorReq`와 timer A의 yield tick을 각각 만든 뒤 반대쪽에 빠른 작업을 보낸다.
- 검증: actor와 timer는 같은 spot 상태를 최종 반영할 때만 직렬 순서를 지키고, 서로의 mailbox가
  yield 대기 때문에 전체적으로 막히지 않는다.
- 세부 동작: actor mailbox와 timer mailbox의 독립 진행.

### Track D — topology별 yield 처리

#### YD-D1 local Spot topology에서 yield가 동작한다

우선순위: `P0`

**한마디로:** delay service와 target Spot이 같은 play 노드에 있을 때 yield 흐름이 정상인가.

- 절차: `play-a`의 `YieldProbeSpot`이 같은 프로세스 또는 같은 노드의 delay service에 request를 보내고
  `yield`로 기다린다.
- 검증: Track A~C의 핵심 marker가 local topology에서 모두 성립한다.
- 세부 동작: local handler dispatch와 yield 재개.

#### YD-D2 remote Spot topology에서 yield가 동작한다

우선순위: `P0`

**한마디로:** handler가 원격 노드의 service reply를 `yield`로 기다려도 원래 Spot mailbox에서 재개되는가.

- 절차: `play-a`의 handler가 `play-b` 쪽 delay service 또는 원격 Spot request를 보내고 `yield`로
  기다린다.
- 검증: reply는 원격 노드에서 돌아오고, continuation은 `play-a`의 원래 spot/actor/timer mailbox에서
  재개된다. `play-b`에는 continuation marker가 남지 않는다.
- 세부 동작: cross-node request reply와 yield continuation 소유권.

#### YD-D3 route bridge 경유 handler에서도 yield가 동작한다

우선순위: `P1`

**한마디로:** 외부 route mesh channel이 target Spot으로 보낸 request handler 안에서도 yield 의미가 같은가.

- 절차: consumer가 session gateway의 stream connector request를 보낸다. session gateway가 route mesh
  channel로 `play-b`의 target Spot에 request를 relay하고, target Spot handler는 delay service
  request를 `yield`로 기다린다.
- 검증: route bridge를 경유해 들어온 handler도 turn 반납, 다른 mailbox 진행, 원래 target Spot
  mailbox continuation 재개가 모두 성립한다. 일반 route packet과 spot route packet은 서로 오염되지
  않는다.
- 세부 동작: route bridge ingress와 yield dispatch 결합.

#### YD-D4 session relay로 들어온 actor handler에서도 yield가 동작한다

우선순위: `P1`

**한마디로:** stream session에서 actor로 relay된 packet handler가 yield를 써도 session reply와 actor push가 올바른 경로로 돌아오는가.

- 절차: stream client가 session gateway에 붙고 actor에 bind한다. client packet은 session handler를
  통해 actor로 relay되고, actor handler는 delay service request를 `yield`로 기다린다.
- 검증: actor handler continuation은 actor가 사는 play 노드에서 재개된다. reply와 push는 bound
  session으로만 돌아오고, bind하지 않은 session에는 오지 않는다.
- 세부 동작: session relay + actor yield + bound session reply/push 경로.

### Track E — 실패와 cleanup

#### YD-E1 yield 중 timeout은 turn을 영구 점유하지 않는다

우선순위: `P0`

**한마디로:** `yield`로 기다리던 request가 timeout 되어도 mailbox가 다시 진행되는가.

- 절차: delay service가 request timeout보다 늦게 reply하도록 설정하고 handler가 `yield`로 기다린다.
  timeout 뒤 같은 spot/actor/timer에 빠른 request를 보낸다.
- 검증: timeout은 public error로 관찰되고, 해당 mailbox는 다음 작업을 처리한다. pending continuation이
  뒤늦게 상태를 다시 바꾸지 않는다.
- 세부 동작: yield timeout cleanup.

#### YD-E2 yield 중 cancellation은 continuation을 중단하고 다음 작업을 진행시킨다

우선순위: `P1`

**한마디로:** cancellation token이 취소되면 yield 대기와 continuation이 정리되고 mailbox가 풀리는가.

- 절차: handler가 `yield`로 기다리는 동안 client request cancellation 또는 server-side cancellation을
  발생시킨다.
- 검증: handler는 취소 상태를 public error 또는 정해진 cancellation result로 끝낸다. 같은 mailbox의
  다음 작업은 정상 처리되고, 취소된 continuation이 reply나 push를 중복 전송하지 않는다.
- 세부 동작: yield cancellation cleanup.

#### YD-E3 yield 중 runtime shutdown은 무한 대기하지 않는다

우선순위: `P1`

**한마디로:** yield 대기 중인 handler가 있을 때 프로세스를 정상 종료해도 drain이 끝나거나 정해진 오류로 닫히는가.

- 절차: `YieldReq`가 delay service를 기다리는 동안 play 노드를 정상 종료한다.
- 검증: shutdown은 무한 대기하지 않는다. pending yield 작업은 정해진 closed/cancelled 오류로 정리되고,
  다음 실행에서 같은 routing id를 다시 사용할 수 있다.
- 세부 동작: runtime shutdown과 pending yield cleanup.

#### YD-E4 yield가 허용되지 않는 표면은 E2E 구현에서 쓰지 않는다

우선순위: `P0`

**한마디로:** route mesh send/request, 일반 send/publish, 외부 HTTP 호출, 사용자가 만든 임의 async 작업을 yield로 우회하지 않는가.

- 절차: 각 언어 구현의 Config 8 server code와 client code를 정적 검증한다.
- 검증: `yield`는 framework가 Spot/Entry Spot handler turn과 연결해서 만든 call object에만 사용한다.
  허용 대상은 Spot outbound request, actor join request, worker offload request이다. 일반 send/publish,
  route mesh request, 임의 `Task`/`Promise`/`CompletionStage`, 외부 HTTP client async 호출에는
  yield wrapper를 만들지 않는다. 필요한 공개 API가 없으면 feature-map gap으로 남긴다.
- 세부 동작: yield 공개 API 허용 범위 검증.

#### YD-E5 언어별 yield 의미 동등성

우선순위: `P0`

**한마디로:** 같은 시나리오 id는 모든 framework 언어에서 같은 mailbox·timeout·cancellation 의미를 갖는가.

- 절차: 각 언어 구현의 Config 8 report를 같은 시나리오 id(`YD-A1` 등)로 모은다. 메서드 이름은
  언어별 public API를 따르되, marker 이름과 성공 조건은 공통 문서의 정의를 따른다.
- 검증: 같은 id의 evidence가 같은 의미를 증명한다. 예를 들어 `YD-B2`는 모든 언어에서 같은 actor
  재진입 금지를 보여야 하고, `YD-E1`은 모든 언어에서 timeout 뒤 mailbox가 풀리는 것을 보여야 한다.
  특정 언어만 다른 동작을 보이면 그 언어 구현은 완료가 아니라 common contract gap이다.
- 세부 동작: 언어별 API 이름 차이와 공통 runtime 의미 분리.

## 5. 완료 기준

- Track A~E의 `P0` 시나리오가 모든 framework 언어에서 같은 의미로 통과한다.
- 시나리오 시작 packet은 client stream connector에서 session gateway로 들어가야 한다. HTTP endpoint는
  health와 evidence 조회에만 쓰며, yield 시나리오를 시작하는 HTTP endpoint를 두지 않는다.
- evidence에는 request id, mailbox id, handler kind(spot/actor/timer), node id, `yield-released`,
  `resumed`, `completed` marker가 남아야 한다.
- 기본 terminator와 `yield` terminator를 같은 topology에서 비교해, `yield`만 현재 turn을 반납한다는
  차이를 증명한다. Spot outbound request, actor join request, worker offload request는 각각 별도
  evidence로 증명한다.
- 같은 actor와 같은 timer의 재진입 금지, 다른 actor와 다른 timer의 진행 가능성을 둘 다 검증한다.
- 실패 경로(timeout/cancellation/shutdown)는 client가 받은 public error와 server evidence를 함께
  확인한다.
- 지원하지 않는 언어는 test skip으로 끝내지 않고 feature-map에 공통 contract gap과 구현 계획을
  남긴다. gap은 완료 판정이 아니라 후속 parity 작업의 입력이다.
