<!-- framework-adapter-nav:start -->
[E2E 목차](README.ko.md) | [이전: Monitoring](config-7-monitoring.ko.md) | [다음: actor 메시징](config-9-to-actor-messaging.ko.md)
<!-- framework-adapter-nav:end -->

# Config 8 — 실행 turn과 terminator 배포

User Spot, Instance Spot과 Actor handler가 request, actor join과 worker 완료를 기다릴 때
**실행 lane의 권한을 어떻게 다루는지**를 실제 배포 구성에서 검증한다.

**핵심 계약은 하나다** — [04 §1.1](../spec/04-async-execution-policy.ko.md).

| terminator | 실행 줄 |
|---|---|
| **submit 의미** | one-way. 완료를 기다리지 않는다 |
| **async 의미** | **turn을 유지한다.** 대기 중 같은 owner의 다른 callback이 시작하지 않는다. **handler = 하나의 turn** |
| **yield 의미** | `SpotWide` User Spot 또는 Instance Spot의 **공유 execution gate만 반납한다.** 완료된 continuation은 같은 gate의 queue에 재삽입되어 새 turn으로 재개된다. Member Actor의 FIFO claim은 반납하지 않는다 |

`Yield`는 Channel·Spot·Actor request와 I/O·CPU worker call에만 제공한다. `SpotWide` User Spot과
Instance Spot 밖에서 공통 call type의 `Yield`를 호출하면 operation을 제출하기 전에
`InvalidConfiguration`으로 끝난다. Actor join, create, send, publish, timer 등록, close와 destroy에는
`Yield`가 없다. Mutable state를 가로질러 기다려야 하는 호출자는 `Async`를 사용하고, 공유 gate를 반납한
뒤에는 대기 전에 읽은 상태를 다시 확인한다.

## 1. 목적과 범위

- 다룬다: 세 terminator의 실행 줄 의미, `SpotWide` 이중 claim, `PerActor` actor·timer lane,
  CPU/IO worker 분리, local/remote Spot topology, MeshNode routed path 경유 handler,
  timeout·cancellation·shutdown 때의 cleanup.
- 다루지 않는다: 처리량 수치(공통 perf 문서), codec 변주(Config 4), store 장애·복구(Config 6).
- **turn 관리 대상**: Channel·Spot·Actor outbound request, actor join과 worker offload.
  일반 send/publish는 one-way submit 계약을 검증한다. 사용자가 만든 임의 async 작업을 framework
  terminator로 가장하지 않는다.

## 2. 서버 구성 (한 번 구동, 공유)

| 역할 | 수 | 구성 |
|------|----|------|
| location store | 1 | 공식 Redis location store extension이 쓰는 공유 Redis. 실행마다 전용 key prefix. 각 노드는 `AddLocationStore(new ZLinkRedisLocationStore(...))`로 등록한다. |
| play 노드 | 2 (`play-a`, `play-b`) | Location Store를 등록한 Object Server. Entry Spot, `SpotWide`와 `PerActor` stable User Spot type, stable Actor type의 factory를 제공한다. Factory는 명시적 `Disabled` policy를 사용하고 placement weight `100`, Actor limit `128`, Spot limit `32`, activation concurrency `32`를 고정한다. `TurnProbeSpot` + actor mailbox + timer + worker를 실행하고 descriptor와 Spot·Actor location row를 자동 게시한다. |
| delay service | 2 (`delay-a`, `delay-b`) | ChannelName request를 받아 지정한 시간 뒤 reply한다. handler가 기다릴 **framework request** 역할이다. |
| **external API** | 1 (`ext-api`) | **framework 밖의 순수 HTTP 서버.** 지정한 시간 뒤 JSON을 돌려준다. framework HTTP client가 호출할 **외부·레거시 API** 역할이다. zlink channel이 아니다. |
| session gateway | 2 (`session-a`, `session-b`) | Location Store를 등록한 Object Client. stream session을 받고 global Actor·Spot address로 play 노드에 시나리오 packet을 relay하며 factory와 placement target은 제공하지 않는다. |
| consumer | 시나리오별 | 언어별 stream connector로 session gateway에 접속한다. |

`TurnProbeSpot`은 상태를 가진 작은 probe spot이다. handler는 evidence에 `started`, `held`,
`released`, `resumed`, `completed` 같은 marker와 **turn id**를 남긴다. actor와 timer handler도
mailbox id, 실행 순서, 처리 노드를 evidence로 남긴다.

Track E의 Entry Spot, User Spot A·B와 Actor는 모두 `play-a`에 배치해 same-node join을 검증한다. 따라서
`Disabled` policy가 join을 막지 않으며 Relocation Store와 relocation adapter가 필요하지 않다. `play-b`는
TD-F1의 remote Spot request target으로만 사용하고 이 config에서 cross-node object relocation을 시작하지 않는다.

**`TurnProbeSpot`은 공유 counter 하나를 갖는다.** `async`가 turn을 유지하는지 검증할 때
read-modify-write 불변식이 await를 가로질러 유지되는지 확인하는 데 쓴다.

## 3. 실행 모델

`run_e2e.sh`가 Redis → delay service → external API → play 노드 → session gateway 순으로 시작하고
client 시나리오를 순차 실행한다. 각 시나리오는 session gateway의 stream endpoint에 connector로
접속해 request packet을 보내 Spot/actor/timer 작업을 만든다.

테스트 driver가 play 노드의 route client·Spot manager·actor manager를 직접 들고 시작 packet을 만들면
session gateway·stream framing·actor bind/relay 경로를 건너뛰므로 통과로 보지 않는다.

로그는 [README](README.ko.md) §6대로 `log/`에 남기고, message flow 추적을 `key_transitions` 이상으로
켠다. server evidence에는 **turn id와 mailbox id**를 남겨 원인 레이어를 나눌 수 있게 한다.

## 4. 시나리오

### Track A — async는 turn을 유지한다

#### TD-A1 operation별 terminator 의미를 노출한다

우선순위: `P0`

**검증 질문:** 각 call object가 operation 종류에 맞는 terminator를 제공하고 세 완료 의미를 섞지 않는가.

- 절차: 각 표면의 public 타입을 확인한다.
- 검증: one-way operation은 submit 의미를 제공하고, 완료 결과를 기다릴 수 있는 operation은 계약에 맞는
  async·yield 의미를 제공한다. 언어별 표면에 `submit`, `async`, `yield`, `await`와 다른 자연스러운 이름이
  있어도 된다. **완료 값을 동기로 언래핑하는 blocking terminator는 없다.**
- 세부 동작: operation별 terminator 의미 고정.

#### TD-A2 async 대기 중 같은 Spot의 다른 callback이 시작하지 않는다

우선순위: `P0`

**검증 질문:** `async`로 기다리는 동안 같은 spot의 다음 dispatch가 정말 멈추는가(= handler가 하나의 turn인가).

- 절차: `TurnProbeSpot` handler가 delay service에 request를 보내고 `.Async(...)`로 기다린다. 대기
  중에 같은 spot으로 probe packet을 보낸다.
- 검증: evidence 순서가 `await-held → await-resumed → completed → probe-started → probe-completed`다.
  **probe가 대기 구간에 끼어들지 않는다.** turn id가 대기 전후로 같다.
- 세부 동작: async = turn 유지.

#### TD-A3 async를 가로지르는 spot 상태 불변식이 유지된다

우선순위: `P0`

**검증 질문:** await 앞에서 검사한 결과가 await 뒤에도 유효한가(= lock 없이 room 로직을 쓸 수 있는가).

- 절차: handler가 `TurnProbeSpot`의 counter를 읽고 → delay request를 `.Async(...)`로 기다린 뒤 →
  읽은 값 기준으로 counter를 증가시킨다. 같은 흐름을 동시에 N번 유발한다.
- 검증: **counter가 정확히 N만큼 증가한다**(lost update 없음). 어떤 handler도 다른 handler가
  대기하는 사이에 counter를 바꾸지 못했다.
- 세부 동작: async 구간의 read-modify-write 원자성.

#### TD-A4 async 대기가 완료를 막지 않는다

우선순위: `P0`

**검증 질문:** turn을 잡고 기다려도 응답이 정상 도착하는가(dispatch·completion이 실행 줄과 별개 축인가).

- 절차: TD-A2와 같되 delay를 충분히 길게(예: 1s) 준다.
- 검증: 응답이 timeout 없이 도착하고 handler가 재개된다. **turn을 유지한 대기가 completion 경로를
  막지 않는다.**
- 세부 동작: completion 축의 독립성.

#### TD-A5 async 대기 중 timer는 지연된다 (의도된 결과)

우선순위: `P1`

**검증 질문:** `async`로 오래 기다리면 같은 spot의 timer tick이 밀리는가 — 그게 계약대로인가.

- 절차: `TurnProbeSpot`에 짧은 주기 timer를 등록하고, handler가 `.Async(...)`로 1s 기다린다.
- 검증: 대기 구간 동안 timer tick이 실행되지 않고, 대기가 끝난 뒤 overrun 정책대로 처리된다. **이는
  결함이 아니라 `async`의 정의다.** 지연을 피해야 하면 `yield`를 쓴다(Track B).
- 세부 동작: turn 유지의 대가를 명시적으로 고정.

### Track B — yield는 turn을 반납한다

#### TD-B1 yield 대기 중 같은 Spot의 다른 callback이 실행된다

우선순위: `P0`

**검증 질문:** `yield`로 기다리는 동안 다른 메시지가 처리되는가.

- 절차: `TurnProbeSpot` handler가 delay request를 `.Yield(...)`로 기다린다. 대기 중에 같은 spot으로
  probe packet을 보낸다.
- 검증: evidence 순서가 `yield-released → probe-started → probe-completed → yield-resumed → completed`다.
  probe가 대기 구간에 **끼어든다.**
- 세부 동작: yield = turn 반납.

#### TD-B2 yield의 continuation은 큐에 재삽입되어 순서대로 재개된다

우선순위: `P0`

**검증 질문:** 응답이 와도 즉시 끼어들지 않고, 큐에 들어가 순차로 처리되는가.

- 절차: handler A가 `.Yield(...)`로 대기하는 동안 probe packet 세 개를 연속으로 보낸다. A의 응답은
  두 번째 probe가 queue에 추가된 뒤 도착하도록 delay를 맞춘다.
- 검증: A의 continuation이 **응답 도착 즉시가 아니라 큐 순서대로** 재개된다. 재개 시점에는 줄을 다시
  배타적으로 점유한다(재개 중 다른 callback이 겹치지 않는다).
- 세부 동작: continuation 재삽입과 순차 재개.

#### TD-B3 yield를 가로지르는 spot 상태는 보장되지 않는다

우선순위: `P1`

**검증 질문:** `yield` 앞뒤로 상태가 바뀔 수 있음을 샘플과 문서가 정확히 반영하는가.

- 절차: TD-A3과 같은 read-modify-write를 **`yield`로** 수행하고 동시에 N번 유발한다.
- 검증: lost update가 **발생할 수 있다.** 이것이 `yield`의 정의이며, 그래서 `yield`는 spot 공유
  흐름과 무관한 대기에만 쓴다. E2E는 이 성질을 관측 사실로 기록하고, 애플리케이션 코드가
  yield 이후 상태를 다시 확인하도록 작성됐는지 확인한다.
- 세부 동작: yield의 대가를 명시적으로 고정.

#### TD-B4 yield 대기 중 timer가 정상 실행된다

우선순위: `P0`

**검증 질문:** `yield`로 기다리면 room의 timer가 안 멈추는가(= yield가 존재하는 이유).

- 절차: TD-A5와 같되 `.Yield(...)`로 기다린다.
- 검증: 대기 구간 동안 timer tick이 정상 실행된다. TD-A5와 정확히 대비된다.
- 세부 동작: yield가 head-of-line 지연을 해소한다.

### Track C — 외부 I/O와 worker

#### TD-C1 외부 API는 I/O worker의 yield로 호출한다

우선순위: `P0`

**검증 질문:** spot handler가 외부 HTTP API를 부를 때 User Spot 전체가 멈추지 않는가.

- 절차: `TurnProbeSpot` handler가 `RunIoWorker` 안에서 DI로 주입받은 HTTP client로 `ext-api`를
  호출하고 worker call의 `.Yield(...)`로 기다린다. 대기 중에 같은 Spot으로 probe packet을 보내고
  timer도 실행한다.
- 검증: 대기 중 probe와 timer가 정상 실행되고, 응답 뒤 continuation이 큐 순서대로 재개된다.
  evidence에 `io-worker-yield-released` / `io-worker-yield-resumed` marker가 남는다.
- 검증: HTTP client call 자체에는 `Yield` terminal이 없다. `Yield` 권한은 worker call이 소유한다.
- 세부 동작: 외부 I/O와 execution gate의 책임 분리.

#### TD-C2 I/O worker의 async는 turn을 유지한다

우선순위: `P1`

**검증 질문:** 같은 HTTP 호출을 `.Async(...)`로 하면 turn이 유지되는가.

- 절차: TD-C1의 worker call을 `.Async(...)`로 기다린다.
- 검증: 대기 중 probe가 끼어들지 않고 timer도 지연된다. TD-C1과 대비된다.
- 세부 동작: HTTP client `Async`는 worker 안에서 실행되고 turn 유지·반납은 worker terminator가 결정한다.

#### TD-C3 I/O worker는 스레드를 점유하지 않는다

우선순위: `P0`

**검증 질문:** 외부 비동기 I/O를 `RunIoWorker`로 감싸면 worker pool 스레드가 잠기지 않는가.

- 절차: handler가 `RunIoWorker(...)`로 `ext-api` 호출을 감싸고 `.Yield(...)`로 기다린다. 이 흐름을
  **worker pool 크기보다 훨씬 많이**(예: pool 크기 × 4) 동시에 유발한다.
- 검증: **`WorkerQueueFull`이 나지 않고** 전부 정상 완료한다. 대기 중 probe와 timer가 진행된다.
  I/O worker가 스레드를 점유하지 않았음을 뜻한다.
- 세부 동작: I/O worker = turn 반납 경계이지 스레드 점유 장치가 아니다.

#### TD-C4 CPU worker는 pool 스레드를 점유하고, terminator가 turn을 결정한다

우선순위: `P1`

**검증 질문:** CPU 작업을 `RunCpuWorker`로 넘기면 **spot의 직렬 스레드에서는 실행되지 않되**, turn 유지/반납은
terminator가 정하는가.

- 절차: handler가 `RunCpuWorker(...)`로 짧은 CPU 작업을 넘긴다. (a) `.Async(...)`, (b) `.Yield(...)`
  두 경로를 각각 실행한다. 동시에 pool 크기를 넘는 수를 유발한다.
- 검증:
  - **두 경로 모두** CPU 작업은 worker pool 스레드에서 실행되고 **spot의 직렬 스레드를 점유하지 않는다.**
    다른 spot의 작업과 다른 spot의 timer는 계속 진행된다.
  - (a) `.Async(...)`는 **turn을 유지한다** — 대기 중 **같은 spot의** 다른 callback과 timer는 시작하지
    않는다(TD-A3와 같은 불변식).
  - (b) `.Yield(...)`는 **turn을 반납한다** — 대기 중 같은 spot의 다음 callback이 실행되고,
    continuation이 큐에 재삽입되어 순서대로 재개된다.
  - pool 한도를 넘기면 `WorkerQueueFull`로 거부되어 bounded pool의 제한을 호출자에게 알린다.
- 세부 동작: **worker 축(어느 스레드에서 실행되는가)과 turn 축(같은 spot이 진행하는가)은 별개다.**
  `RunCpuWorker`는 앞의 축만 옮긴다. 뒤의 축은 terminator가 정한다.

#### TD-C5 CPU worker에서 blocking I/O를 하지 않는다

우선순위: `P1`

**검증 질문:** 샘플과 E2E 코드가 CPU worker 안에서 외부 I/O를 blocking으로 기다리지 않는가.

- 절차: 구현 코드를 검사한다.
- 검증: CPU worker 델리게이트 안에 blocking I/O 대기(`GetAwaiter().GetResult()`, `.join()`,
  `runBlocking` 등)가 없다. 외부 I/O는 `RunIoWorker` 안에서 HTTP client `Async`를 사용한다.
- 세부 동작: pool 고갈 방지 규칙.

### Track D — mailbox 격리

#### TD-D1 SpotWide Actor가 yield하면 다른 Actor와 Spot callback은 처리된다

우선순위: `P0`

- 절차: 같은 `SpotWide` User Spot의 Actor A handler가 `.Yield(...)`로 기다리는 동안 Actor B, Spot direct
  handler와 timer record를 제출한다.
- 검증: B, Spot handler와 timer가 A의 대기 중에 실행된다. A의 continuation은 같은 User Spot gate를
  다시 얻은 뒤 다른 callback과 겹치지 않고 실행된다.
- 세부 동작: Actor claim 유지와 User Spot gate 반납.

#### TD-D2 같은 Actor의 다음 record는 yield 대기 중 실행되지 않는다

우선순위: `P0`

- 절차: `SpotWide` User Spot의 Actor A handler가 `.Yield(...)`로 기다리는 동안 같은 Actor A에 packet을
  하나 더 보낸다.
- 검증: 두 번째 packet은 첫 handler의 continuation과 terminal completion 뒤에만 시작한다. Evidence는
  `job1-started → job1-yielded → job1-resumed → job1-completed → job2-started` 순서이며 overlap은 0이다.
- 세부 동작: `Yield`가 반납하지 않는 Actor FIFO claim.

#### TD-D3 같은 Spot의 다음 timer record가 yield 대기 중 실행된다

우선순위: `P0`

- 절차: timer handler가 `.Yield(...)`로 기다리는 동안 다음 tick 시각이 지나게 한다.
- 검증: 현재 callback이 turn을 반납한 뒤 다음 timer record가 같은 Spot의 새 turn에서 실행될 수 있다.
  Yield continuation과 timer turn은 동시에 실행되지 않으며, 같은 timer key의 중복 만료는 공통 timer
  계약에 따라 한 pending record로 합칠 수 있다.
- 세부 동작: Spot timer의 yield와 순차 turn 실행.

#### TD-D4 PerActor의 Async는 같은 Actor만 막는다

우선순위: `P0`

- 절차: `PerActor` User Spot에서 Actor A가 `.Async(...)`로 장시간 기다리는 동안 Actor A의 다음 packet,
  Actor B packet, 같은 timer의 연속 두 callback, 서로 다른 두 timer, Spot direct handler와 lifecycle
  callback을 제출한다. Test barrier로 각 callback의 실행 구간을 겹치거나 유지할 수 있게 한다.
- 검증: Actor B와 다른 timer lane은 진행하지만 Actor A의 다음 packet은 첫 job 완료 뒤에만 시작한다.
  같은 timer의 두 callback은 제출 순서를 유지하며 겹치지 않고 서로 다른 timer는 겹칠 수 있다.
  Spot direct handler와 lifecycle callback은 같은 Spot lane에서 순서대로 실행되며 서로 겹치지 않는다.
- 세부 동작: Actor별 FIFO lane, timer별 FIFO lane과 Spot direct·lifecycle 공용 lane.

#### TD-D5 지원하지 않는 문맥의 Yield는 제출 전에 실패한다

우선순위: `P0`

- 절차: Entry Spot, Entry Actor, `PerActor` User Spot, Node·Channel handler와 owner turn 밖의 client에서
  request·worker call의 `Yield`를 호출한다.
- 검증: 모두 `InvalidConfiguration`으로 한 번 완료되고 operation ID 할당, outbound admission, worker
  scheduling, queue mutation과 gate release가 0건이다. 같은 call을 `Async`로 실행한 결과와 구분한다.
- 세부 동작: runtime execution-context allowlist와 submit 전 validation.

#### TD-D6 self awaited request와 same-gate Async를 선검증한다

우선순위: `P0`

- 절차: Actor가 자신에게 awaited request를 `Async`와 `Yield`로 각각 제출하고, `SpotWide` handler가
  같은 User Spot gate가 필요한 target을 `Async`로 기다린다.
- 검증: operation submission 전에 닫힌 오류로 끝나며 handler inline·reentrant 실행과 timeout 의존
  deadlock이 없다. One-way send는 FIFO queue에 정상 제출된다.
- 세부 동작: same-claim deadlock 제거와 one-way 구분.

### Track E — actor join

#### TD-E1 Entry Spot에서 user Spot으로 Join을 defer한다

우선순위: `P0`

**검증 질문:** handler가 Join을 등록한 뒤 정상 종료해야만 membership transition이 시작되는가.

- 절차: Entry Spot membership을 가진 Actor의 handler가 `JoinSpot(...).Defer()`를 호출하고, 같은 handler에서
  marker를 하나 더 기록한 뒤 정상 종료한다. 같은 Actor에 후속 packet도 제출한다.
- 검증: handler terminal 전에는 target admission·Location CAS·source/target lifecycle evidence가 없다.
  이후 evidence 순서는 `target OnActorJoin → location CAS commit → target OnJoinedActor →
  source OnLeaveActor → Actor OnJoinCompleted(Accepted)`다
  ([23 §4](../spec/23-spot-actor.ko.md#4-join-의미와-commit-순서)).
  Entry Spot도 이 join의 source이므로 source leave를 생략하지 않는다. 다른 actor의 join·packet이 그 사이
  막히지 않는다. 같은 Entry Actor에 뒤이어 제출한 packet은 join job의 terminal 완료 전에는 시작하지
  않으며, 두 job의 handler 실행 구간은 겹치지 않는다.
- 세부 동작: deferred barrier, Entry Actor별 FIFO·non-overlap과 서로 다른 Actor의 독립 진행.

#### TD-E2 PerActor와 SpotWide에서 같은 deferred Join 의미를 제공한다

우선순위: `P0`

**검증 질문:** execution mode와 관계없이 Defer가 gate나 Actor claim을 반납하지 않는가.

- 절차: `PerActor`와 `SpotWide` User Spot A의 Actor handler가 각각 `JoinSpot(B).Defer()`를 호출한다.
  `SpotWide` 반복에서는 먼저 request `Yield` continuation을 발생시키고, 다시 gate를 획득한 마지막
  continuation에서 Join을 defer한다.
- 검증: 두 mode 모두 handler의 최종 terminal 뒤에만 Join이 활성화된다. `Yield`는 기존 규칙대로
  SpotWide gate를 반납하지만 `Defer()`는 gate와 Actor FIFO claim을 반납하지 않는다. evidence 순서는
  `target OnActorJoin → location CAS commit → target OnJoinedActor → source OnLeaveActor →
  Actor OnJoinCompleted(Accepted)`다.
  CAS 실패 주입에서는 source leave와 target membership evidence가 없어야 한다. 세 lifecycle callback은
  각각 해당 Spot의 control claim에서 실행되고 caller의 Actor turn id와 달라야 한다. 같은 호출을
  awaited Join이나 coroutine wrapper로 시작할 수 있는 public terminal이 없어야 한다.
- 세부 동작: Yield와 Defer의 gate 의미 분리, Actor caller turn과 Spot lifecycle control claim의 독립 진행.

#### TD-E2A handler 실패는 비활성 Join barrier를 폐기한다

우선순위: `P0`

- 절차: User Spot handler가 서로 다른 member Actor A·B의 Join을 차례로 `Defer()`한다. 같은 설정에서
  정상 terminal, exception terminal, cancellation terminal의 세 반복을 각각 실행한다.
- 검증: 정상 반복에서는 handler terminal 전 두 Join 모두 target admission, Location Store mutation과
  source seal이 0건이고 terminal 뒤 두 barrier가 함께 활성화된다. Exception 반복과 cancellation 반복은
  각각 두 Actor 모두 target admission, Location Store mutation, source seal과 completion callback이
  0건임을 검증한다. 어느 반복에서도 한 Actor의 barrier만 활성화하거나 남기는 partial activation이 없다.
  Exception과 cancellation 반복에서만 두 Actor의 후속 작업이 기존 membership에서 정상 처리됨을 확인한다.
  정상 terminal로 활성화된 뒤 발생한 Actor별 admission 결과는 서로 독립이며 한 Join의 거절이 다른 Join을
  rollback하지 않는다.
- 세부 동작: 여러 Actor intent의 process-local all-or-none handler terminal과 deferred barrier cleanup.

#### TD-E3 반대 방향 join 두 개가 동시에 진행된다

우선순위: `P0`

**검증 질문:** spot A→B와 B→A join이 동시에 일어나도 서로 막지 않는가(노드 전역 직렬화 없이).

- 절차: actor X가 A→B로, actor Y가 B→A로 **동시에** join한다.
- 검증: 둘 다 timeout 없이 완료된다. 두 Spot의 timer도 계속 실행된다. **local join을 노드 전역에서
  직렬화하지 않는다** — 서로 다른 Spot 쌍의 join은 병행 진행된다. 두 join은 `PerActor` source 또는
  owner 밖의 controller에서 시작해 `SpotWide` same-gate awaited cycle을 만들지 않는다.
- 세부 동작: join 사이클 부재. 노드 전역 join 세마포어가 필요 없음을 고정한다.

### Track F — topology와 실패 경로

#### TD-F1 remote Spot topology에서 세 terminator가 같은 의미를 갖는다

우선순위: `P0`

- 절차: `play-a`의 handler가 `play-b`의 spot으로 request를 보내고 `.Async(...)` / `.Yield(...)`로
  각각 기다린다.
- 검증: local topology(Track A·B)와 **같은 실행 줄 의미**가 관측된다.
- 세부 동작: topology가 terminator 의미를 바꾸지 않는다.

#### TD-F2 MeshNode routed path 경유 handler에서도 같다

우선순위: `P1`

- 절차: MeshNode routed path를 거쳐 도달한 handler에서 Track A·B를 반복한다.
- 검증: 같은 결과.

#### TD-F3 session relay로 들어온 actor handler에서도 같다

우선순위: `P1`

- 절차: session gateway가 relay한 actor packet handler에서 Track A·B를 반복한다.
- 검증: 같은 결과.

#### TD-F4 대기 중 timeout은 turn을 영구 점유하지 않는다

우선순위: `P0`

- 절차: delay service가 응답하지 않게 하고 `.Async(...)` / `.Yield(...)`로 각각 기다린다.
- 검증: 둘 다 request timeout으로 실패하고, 실패 뒤 그 spot의 다음 작업이 정상 진행된다. **turn이
  영구 점유되지 않는다.**
- 세부 동작: 실패 경로의 turn 회수.

#### TD-F5 대기 waiter cancellation

우선순위: `P1`

- 절차: 지연된 외부 request를 `.Async(...)` 또는 `.Yield(...)`로 기다리는 waiter만 취소한다.
- 검증: 해당 waiter continuation은 언어별 cancellation 결과로 끝나고 shared runtime이나 이미 수락한 remote
  operation을 취소한 것으로 간주하지 않는다. Spot·Actor가 `Serving`인 동안 후속 작업은 정상 진행된다.

#### TD-F5A 대기 중 host Shutdown

우선순위: `P1`

- 절차: 지연된 외부 request가 이미 수락된 상태에서 host `Shutdown`을 시작하고 admission seal과 deadline을
  관찰한다. Seal 뒤 같은 Spot·Actor에 새 작업도 제출한다.
- 검증: Seal 뒤 신규 작업은 `Shutdown`으로 거부된다. 이미 수락한 continuation은 deadline 안에 정상 terminal로
  끝나거나 deadline 경계에서 owner별 shutdown terminal을 정확히 한 번 받는다. Seal 뒤 “다음 작업이
  진행된다”고 단언하지 않는다. Host는 bounded `Stopped/None` 또는 `ForceStopped/DeadlineExceeded`로 끝나며
  무한 대기하지 않는다.

#### TD-F6 wait-for 사이클은 제출 전에 거부한다

우선순위: `P1`

**검증 질문:** 현재 claim 없이는 처리할 수 없는 target을 기다리는 요청을 timeout에 의존하지 않고 거부하는가.

- 절차: spot A의 handler가 **A 자신에게** request를 보내고 `.Async(...)`로 기다린다.
- 검증: outbound admission과 target queue mutation 전에 닫힌 same-claim 오류로 끝난다. Handler inline
  실행과 timeout timer 할당은 0건이며 실패 뒤 A의 다음 작업이 정상 진행된다.
- 세부 동작: 구조적으로 완료할 수 없는 cycle을 operation 정의에서 제거한다.

### Track G — 언어 동등성

#### TD-G1 언어별 terminator 의미가 같다

우선순위: `P0`

- 절차: 모든 언어 구현에서 Track A~F를 실행한다.
- 검증: terminator 이름은 언어 관용을 따르되(`Async`/`await`/`async()`), **실행 줄 의미와 evidence
  순서가 동일**하다. blocking 언래핑 terminator를 제공하는 언어가 없다.

## 5. 완료 기준

- Track A~G의 `P0` 시나리오가 모든 언어에서 통과한다.
- **TD-A3(async 불변식)과 TD-B1(yield 인터리브)이 정확히 반대 결과를 낸다.** 둘 다 통과해야 한다 —
  하나라도 실패하면 terminator 의미가 무너진 것이다.
- **TD-E2·TD-E3가 통과한다.** Actor caller turn을 유지하는 동안에도 Spot lifecycle control claim이
  진행되고 노드 전역 직렬화가 없다.
- TD-C3가 `WorkerQueueFull` 없이 통과한다. I/O worker가 스레드를 점유하지 않는다.
- blocking 언래핑 terminator가 어느 언어에도 없다.
