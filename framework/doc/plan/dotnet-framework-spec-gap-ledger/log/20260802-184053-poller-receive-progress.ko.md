# .NET Framework socket poller 수신 진행 기록

이 기록은 2026-08-02 18:40 KST에 `.NET Framework`만 대상으로 진행한
`zlink poller` 수신 정렬 작업의 현재 상태를 남긴다. 공통 계약 문서와 언어별 exact
interface는 다시 작성하지 않았고, 이미 반영된 계약을 구현·검증 기준으로 사용했다.

## 범위와 책임 경계

- native Core는 `11.1.0`을 기준으로 유지했다. Core source, public header와 Core API/ABI는
  수정하지 않았다.
- Framework STREAM ingress에서 packet callback 등록을 제거하고, public binding의
  `RecvPart`를 Framework receive loop가 사용하도록 유지했다.
- Framework가 native socket의 `Recv`, `RecvPart` 또는 `Subscribe`를 호출하는 경로는 public
  binding `IPoller`의 readiness 확인 뒤에만 호출하도록 정렬했다.
- Framework 내부에는 `IZLinkBackendSocketPoller` seam을 두고, .NET wrapper가 public
  `Systems.Zlink.Zlink.CreatePoller()`와 `IPoller.Add(...)`를 소유한다. Framework는
  binding private API나 reflection을 사용하지 않는다.
- generic Framework receive poller는 기본적으로 `PollIn` readiness를 소유한다. 다만
  asynchronous request를 같은 socket에서 발행하는 DEALER와 raw ROUTER는 해당 public
  poller에 `PollCompletion`도 함께 등록해 binding request-progress owner를 둘로 나누지
  않는다. STREAM, channel ROUTER와 SUB는 receive readiness만 등록한다. 이미 한 poller가
  inbound와 completion을 함께 소유하는 `ZLinkManagedMeshNode` 경로는 같은 책임 경계를
  유지했다.
- Framework-managed `RawMeshMonitor`는 native socket이 아니라 managed event queue이므로
  socket poller 대상에서 제외했다. native socket monitor는 public `ZlinkPoll.Poll`로
  readiness를 확인한 뒤 `TryRecv`한다.

## 반영한 runtime 경로

다음 수신 경로에 poller readiness gate를 적용했다.

| Framework 경로 | 수신 호출 | readiness owner |
|---|---|---|
| STREAM node | `IZLinkBackendStreamSocket.RecvPart` | `IZLinkBackendSocketPoller` |
| client-server server | router `Recv` | `IZLinkBackendSocketPoller` |
| fanout subscriber | subscriber `Subscribe` | `IZLinkBackendSocketPoller` |
| client-server client control | dealer `Recv` | `IZLinkBackendSocketPoller` |
| raw router service port | native router `Recv` | public `IPoller` |
| MeshNode raw ingress | native router `Recv` | 기존 public `IPoller` |
| stream socket monitor | monitor `TryRecv` | public `ZlinkPoll.Poll` |

STREAM receive loop는 readiness 이후 raw part를 batch로 읽고, frame 조립·peer별 오류
격리·application queue admission을 수행한다. queue admission이 실패하면 다음
`RecvPart`를 호출하지 않는다. Framework STREAM ingress에는 `OnPacket` 계열 callback을
등록하지 않는다.

client-server control loop는 `ApplyAdmission`의 state lock을 보유한 상태에서 시작된다.
native `Poller.Wait`가 동기 호출이므로 async 경계를 먼저 통과하도록 조정해 poller wait가
state lock을 잡은 호출자를 block하지 않게 했다. 이 수정 전에는 client-server 회귀가
lock 대기로 종료되지 않았다. 같은 경로에서 admission request가 synchronously complete될
때 `ApplyAdmission`과 상위 `OnAdmitted`가 state lock을 재진입하는 경합도 확인되어,
admission task도 async 경계를 먼저 통과하도록 조정했다. 두 경계는 poller 도입에 필요한
동시성 회귀로 기록한다.

## 현재 검증 결과

| 검증 | 결과 |
|---|---:|
| .NET Framework build (`Zlink.Framework.csproj --no-restore --nologo`) | 성공, error 0, warning 0 |
| binding STREAM poller regression (`test_stream_socket`) | `20/20` 통과 |
| `StreamSessionForcedCleanupTests` | `17/17` 통과 |
| `FanoutAutomaticDiscoveryTests` | `6/6` 통과 |
| `InboundDispatchOptionsTests` | `16/16` 통과 |
| client-server blocking/liveness 회귀 | `1/1` 통과 |
| 현재 변경 대상 targeted 조합 (`66` tests) | `66/66` 통과 |
| 전체 `Zlink.Framework.UnitTests` | `1420/1424` 통과 |

binding STREAM test는 public `IPoller`로 `PollIn` 이벤트를 확인한 뒤 public
`RecvPart`를 호출하고, source routing id와 payload를 검증한다. Framework fake socket도
receive poller 없이는 시작할 수 없도록 내부 test seam을 구현해 readiness gate 누락을
컴파일·실행 단계에서 드러내도록 했다.

이전 targeted 조합 실행에서는 `StreamSessionForcedCleanupTests`의 강제 cleanup 시점
assertion이 한 차례 실패하고 testhost가 중단되었으며, binding `RequestReplySupport`의
늦은 request callback 예외가 함께 관찰되었다. 개별 조합 재실행과 crash blame 수집을
포함한 현재 targeted 조합은 `66/66`으로 통과했다. 따라서 해당 예외는 현재 고정된
재현 결함으로 판정하지 않지만, fresh package와 전체 UnitTests에서도 다시 확인한다.

fresh package를 사용한 전체 UnitTests는 `1420/1424`로 종료되었다. 실패한 4건 중
`StreamSessionForcedCleanupTests`의 강제 cleanup timing assertion은 단독 실행에서
통과했다. 나머지 3건은 현재 dirty worktree의 `ZLinkAsyncSubmitter`가 .NET error
contract에 맞춰 반환하는 `ZLinkFrameworkException(DeadlineExceeded)`와 테스트의
기존 `TimeoutException` exact-type 기대가 맞지 않은 상태였다. 이를 unrelated로
제외하지 않고 테스트를 계약에 맞게 수정했으며, 최신 source 기준 전체 gate에서 다시
확인해야 한다.

## 남은 검증과 현재 판정

현재 판정은 `runtime implementation 진행 중`이다. 다음 항목을 완료해야 poller 작업을
닫을 수 있다.

1. 현재 변경을 포함한 .NET Framework 전체 targeted regression과 전체 UnitTests를 다시
   실행한다. 앞서 관찰된 조합 실행 경합과 `ZLinkAsyncSubmitter` timeout contract 정렬
   결과가 재발하는지도 함께 확인한다.
2. fresh local package를 다시 만들고 native Core `11.1.0` provenance와 package cache가
   실제 소비 경로에 반영되었는지 확인한다.
3. clean consumer와 package contract 검증을 실행한다.
4. `git diff --check`를 실행한다.
5. 구현 후 read-only Claude Opus review에서 STREAM recv ownership, poller와 callback 혼용,
   request-progress 경합, backpressure, disposal, allocation·lock contention, POSD·DDD
   위험을 다시 확인한다.
6. process E2E는 unit 결과와 분리해 기록한다. 아직 실행하지 않은 process E2E를 완료로
   표시하지 않는다.

따라서 현재 문서는 Core나 공통 spec 변경 완료를 의미하지 않으며, `.NET Framework`의
수신 경로 정렬과 회귀 검증의 중간 evidence다.

## 2026-08-02 19:26 KST 업데이트

Claude Opus read-only 후속 리뷰를 완료했다. 리뷰 결과는
`/home/hep7/.claude/plans/perform-a-read-only-post-implementation-joyful-narwhal.md`에
기록되어 있다. 리뷰에서 확인된 High 항목 중 다음은 현재 변경에 반영했다.

- STREAM node-wide admission gate와 충돌하던 node-level `reservedControlSlots`를
  제거했다. 이미 받은 control event는 `EnqueueControl`을 통해서만 priority queue에
  들어가도록 호출 형태를 명시했다.
- serial queue admission 결과를 `Accepted`, `QueueFull`, `Closed`로 구분했다. queue가
  가득 찬 경우는 STREAM backpressure로 유지하고, 닫힌 경우는 peer terminal 처리와
  수신 중단을 구분할 수 있게 했다.
- routing id가 없는 STREAM raw part는 해당 part를 폐기하고 error를 기록한 뒤 receive
  loop를 계속한다. 예상하지 못한 loop-level exception도 즉시 ingress 전체를 종료하지
  않고 backoff 후 재시도한다. 한 peer의 malformed frame 처리와 다른 peer의 수신을
  분리하는 회귀 테스트를 추가했다.
- 동기 native poller wait를 수행하는 STREAM receive/monitor loop와 channel receive loop는
  `LongRunning` runtime task에서 실행하도록 했다. client-server control loop도 연결별
  소유 task를 `LongRunning`으로 생성한다.

이번 수정 직후의 targeted 결과는 `SerialExecutorTests`와
`StreamSessionForcedCleanupTests` 합계 `47/47` 통과이며, Framework build도
error 0, warning 0이다. 다만 아직 최신 소스 기준 fresh package와 전체 UnitTests를
다시 실행하지 않았으므로 이 결과만으로 작업 완료를 판정하지 않는다.

이전 전체 UnitTests의 3건 `ZLinkAsyncSubmitter` timeout 예외 불일치는 현재 dirty
worktree의 구현과 테스트 계약이 어긋난 상태였으므로 “unrelated”로 닫지 않았다.
테스트를 `DeadlineExceeded` observable behavior에 맞게 정렬했으며 최신 전체 gate에서
재실행한다. 강제 cleanup timing assertion도 단독·병렬 실행 결과를 분리해 기록한다.

Claude Opus가 지적한 다음 항목은 아직 닫지 않았다.

- `ZLinkRawRouterServicePort`의 `PollIn | PollCompletion` 소유권과 `RequestAsync`
  progress worker의 책임 경계
- reconnect 시 동일 routing id의 이전 disconnect가 새 연결을 차단할 수 있는 순서 경합
- `RecvPart`의 `hasMore` 계약 확인
- STREAM reassembly buffer의 복사·보유량과 상한/회수 정책

따라서 현재 판정은 여전히 `runtime implementation 진행 중`이다. 다음 단계는 위 남은
소유권·수명·allocation 항목을 정리한 뒤, 최신 source로 fresh Core `11.1.0` package와
consumer를 다시 만들고 targeted/full test 및 Claude Opus 최종 read-only review를
재실행하는 것이다.

## 2026-08-02 19:41 KST Codex read-only review

사용자 요청에 따라 이번 후속 구현 리뷰는 Claude Opus가 아니라 Codex로 진행했다. 현재
source, .NET Framework runtime contract, binding public surface, Core STREAM contract와
직접 native receive 호출 지점을 다시 대조했다. 공통 spec과 언어별 exact interface 문서는
수정하지 않았다.

### 확인된 항목

- Framework가 직접 호출하는 native socket receive 경로는 STREAM의 `RecvPart`, channel
  `ROUTER`의 `Recv`, fanout `SUB`의 `Subscribe`, client control `DEALER`의 `Recv`, raw
  router service port의 `Recv`로 분리되어 있다. 각 경로는 public `IPoller`의 `PollIn`
  readiness를 확인한 뒤 `DontWait` receive를 호출한다.
- STREAM Framework ingress에는 packet callback 등록이 없고, binding public
  `IStreamSocket.RecvPart`만 사용한다. source routing id를 보존하고, `hasMore`에 따른
  multipart source routing id 경계를 확인하며, segmented frame과 한 raw part 안의 여러
  frame을 같은 receive owner에서 조립한다.
- `ZLinkRawRouterServicePort`는 `PollIn`만 등록한다. `RequestAsync`의 completion은
  binding request-progress worker가 소유하므로 이 port의 반복 `TryReceive`가
  `PollCompletion`을 대신 소유하지 않는다.
- `ZLinkManagedMeshNode`의 기존 combined `PollIn | PollCompletion` 소유권은 유지했다.
  managed ready queue를 읽는 `DrainReady`는 native socket receive가 아니며, socket
  monitor wrapper의 native event receive는 public `ZlinkPoll.Poll` 뒤에 실행된다.
- Framework 코드에서 reflection, binding private/internal API, `InternalsVisibleTo`
  우회는 확인되지 않았다.
- Application HWM은 explicit process budget, OS/managed heap 중 작은 값, 단일 후보,
  physical memory fallback과 profile ratio를 test seam으로 검증한다. `0`의 unlimited
  semantics와 기존 `MaxMessageSize` validation도 targeted test에 포함되어 있다.

### 아직 닫히지 않은 review finding

1. **High — reconnect stale event 경합**

   `ZLinkStreamNodeRuntime`은 `ConnectionReady`에서 routing id를 disconnected 목록에서
   제거하지만, 이전 연결의 늦은 `Disconnected` event가 그 뒤에 도착하면 같은 routing id를
   다시 차단할 수 있다. 현재 `ZLinkBackendSocketMonitorEvent`에는 연결 lifetime을
   구분할 generation이 없으므로, 새 연결의 receive state가 stale event에 의해 제거되는
   순서 경합을 아직 닫지 못했다. Core 또는 binding contract를 우회하는 Framework 내부
   retry/지연으로 숨기지 않고, event identity를 확인할 수 있는 owning layer의 계약과
   함께 해결해야 한다.

2. **Medium — full UnitTests timing 안정성**

   Codex review용 serialized full run은 `1424/1427` 통과로 종료됐다. 실패는 다음과
   같다.

   - `LogicalMulticastSubmitsEachPositiveRemoteOnceRegardlessOfWeight`: full run에서만
     5초 readiness timeout. 단독 재실행은 `1/1` 통과했다.
   - `Stream_node_shutdown_upper_bound_does_not_wait_for_terminal_cancellation_callback`:
     full run에서만 cleanup timing assertion 실패. 단독 재실행은 `1/1` 통과했다.
   - `ZLinkAsyncSubmitter` timeout 테스트는 command pending-admission timeout과
     `DeadlineExceeded` pending item timeout을 구분하도록 정렬했고, 현재 class run은
     `26/26` 통과했다. pending waiter capacity 초과는 `TimeoutException`, 이미 등록된
     pending item의 deadline은 `ZLinkFrameworkException(DeadlineExceeded)`인 기존 구현
     의미를 유지한다.

   따라서 단독 green 결과를 full gate 통과로 확대하지 않는다. cleanup lifetime과
   process-wide resource contention을 full run에서 다시 확인해야 한다.

3. **Medium — OS memory limit detector 범위 확인 필요**

   현재 .NET resolver는 cgroup memory limit, `GC.GetGCMemoryInfo().TotalAvailableMemoryBytes`,
   physical memory fallback을 사용한다. 계약에 열거된 Windows Job Object와 process
   address-space limit을 .NET 구현이 별도로 확인한다는 source/test evidence는 아직
   없다. `GC` 값이 해당 환경의 유효 상한을 이미 대표하는지 target runtime별로 확인하고,
   대표하지 않으면 owning detector에 추가한다. Framework에서 process 전체 memory를
   직접 제한하는 우회는 허용하지 않는다.

4. **Low — control-only executor의 unused reserved-slot 표현**

   `_controlIngress`는 `EnqueueControl`만 사용하므로 `ZLinkStreamSessionSerialExecutor`
   기본 `reservedControlSlots` 정책이 이 executor에서는 실질적으로 사용되지 않는다.
   per-session application/control queue의 reservation과 node-wide control ingress를
   생성자 표현으로 분리하면 POSD 관점의 정책 누출과 불필요한 설정 인자를 줄일 수 있다.

5. **Low — STREAM frame materialization 비용**

   pooled receive buffer로 peer별 큰 buffer의 보유를 줄였지만, frame을 async queue에
   넘기기 위해 header와 payload를 새 `Message`로 복사한다. 현재 ownership 안전성에는
   필요한 복사로 보이나, allocation/copy contention이 없다고 판정할 benchmark나 soak
   evidence는 아직 없다. zero-copy를 추가하기 전에 Message ownership과 handler terminal
   lifetime을 유지하는 대안을 별도로 비교해야 한다.

### Codex review 판정

현재 판정은 `runtime implementation 진행 중`, `review not passed`다. STREAM recv와
poller 책임 경계는 주요 누락 없이 정렬됐지만, reconnect lifetime 경합과 full gate의
불안정성이 남아 있어 `주요 이슈 없음` 또는 `완료`로 기록하지 않는다.

다음 검증 순서는 다음과 같다.

1. reconnect stale event를 generation-aware owning layer 계약으로 해결할 수 있는지
   확정하고 회귀 테스트를 추가한다.
2. cleanup timing과 multicast full-run 실패를 반복 실행해 code defect와 test/process
   contention을 분리한다.
3. OS memory limit detector의 .NET target별 semantics를 확인하고 HWM full test를
   실행한다.
4. 최신 source로 fresh Core `11.1.0` package와 isolated consumer를 다시 만들고,
   package contract와 process E2E를 UnitTests와 분리해 검증한다.
5. 위 결과를 반영한 Codex final read-only review 뒤에만 이 작업의 완료 여부를 판정한다.

## 2026-08-02 20:05 KST Codex follow-up verification

최근 `AsyncSubmitter` timeout assertion을 현재 구현의 observable contract에 맞게
수정한 뒤 전체 UnitTests를 다시 실행했다.

- `Zlink.Framework.UnitTests`: `1426/1427` 통과, `1` 실패, 총 `1427`개, 약 2분 39초
- 실패: `Stream_node_shutdown_upper_bound_does_not_wait_for_terminal_cancellation_callback`
  의 cleanup timing assertion (`Expected: 0`, `Actual: 1`)
- 같은 테스트 standalone 재실행: `1/1` 통과
- `ZLinkAsyncSubmitterTests` standalone: `26/26` 통과
- `git diff --check`의 .NET Framework, .NET binding, 진행 log 대상 경로: 통과

따라서 `AsyncSubmitter`의 이전 예외 타입 불일치는 해소되었지만, 전체 실행은 아직
green이 아니다. cleanup timing 실패는 standalone과 full run의 결과가 달라 code defect와
process-wide contention을 추가로 분리해야 한다. 이 결과만으로 테스트를 완화하거나
완료로 기록하지 않는다.

reconnect stale `Disconnected` event finding도 그대로 열린 상태다. Core 11.1.0의
`ConnectionReady` monitor value는 connection generation이 아니라 현재 ready count이며,
현재 .NET binding monitor event에는 connection lifetime identity가 없다. 따라서 Framework
내부 지연·retry·reflection으로 숨기지 않고, Core/binding owner가 제공할 수 있는 event
identity 계약이 필요한 별도 gap으로 유지한다.

현재 판정은 `runtime implementation 진행 중`, `Codex review not passed`다. 남은 순서는
full-run timing 반복 확인, .NET memory limit detector target semantics 확인, fresh Core
`11.1.0` package/consumer 검증, 그리고 최종 Codex read-only review다.

## 2026-08-02 20:08 KST Codex final read-only review

사용자 요청에 따라 Claude Opus를 사용하지 않고 Codex로 최종 read-only review를 수행했다.
공통 spec과 언어별 exact interface 문서는 다시 작성하지 않았으며, 현재 dirty worktree의
unrelated 변경도 덮어쓰지 않았다.

### 최종 확인 결과

- Framework가 직접 호출하는 native `DEALER`, `ROUTER`, `SUB`, `STREAM` receive 경로는
  모두 public ZLink poller의 `PollIn` readiness를 확인한 뒤 `DontWait` receive를 호출한다.
  STREAM은 packet callback을 등록하지 않고 binding의 public `RecvPart`만 사용한다.
- STREAM receive owner는 source routing id와 `hasMore`를 보존하고, segmented frame과 한
  raw part 안의 여러 frame을 조립한다. frame 완성 뒤 Application HWM admission을 수행하며,
  queue가 가득 차면 다음 receive를 발행하지 않고 capacity 회복 뒤 재개한다.
- malformed frame은 해당 peer의 receive state와 연결만 종료하고 다른 peer의 receive를
  중단하지 않는다. `Message`와 pooled buffer의 disposal은 receive owner와 admission
  계층에서 한 번씩만 수행한다.
- `ZLinkManagedMeshNode`의 native receive는 기존 combined `PollIn | PollCompletion`
  poller가 소유한다. RouteMesh monitor와 SPOT subscribe는 각각 Framework의 managed
  monitor/dispatch queue를 읽는 경로이므로 native socket receive 누락으로 세지 않았다.
- Application HWM은 explicit `ProcessMemoryLimitBytes` 우선, OS limit과 managed heap 중
  작은 값, 단일 후보, physical memory fallback, profile ratio를 적용한다. explicit `0`,
  양수 HWM, `MaxMessageSize` validation을 테스트했다.
- 동일 routing id에 대한 `ConnectionReady`와 application ingress의 동시 session 생성은
  single-flight creation table로 합쳐진다. 이로써 중복 scoped session 생성과 조기 disposal
  경합을 제거했다.
- Framework에서 binding private/internal API, reflection, `InternalsVisibleTo` 우회는
  확인되지 않았다. Core source/header/API는 이번 변경에서 수정하지 않았고 native Core는
  `11.1.0`을 유지했다.

### 검증 결과

| 검증 | 결과 |
|---|---:|
| .NET Framework build (`--no-restore`) | 성공, warning 0 / error 0 |
| binding STREAM poller/`RecvPart` regression | `20/20` 통과 |
| HWM 및 주요 Framework targeted regression | `70/70` 통과 |
| fresh package의 isolated Core consumer | 통과, native Core `11.1.0` |
| fresh package restore 뒤 전체 `Zlink.Framework.UnitTests` | `1427/1427` 통과 |
| .NET Framework/binding/log 대상 `git diff --check` | 통과 |

fresh package는 `.artifacts/wsl/nuget/Systems.Zlink.11.1.2.nupkg`로 만들었으며 native
Core provenance와 isolated consumer를 확인했다. 기존 `~/.nuget`의 같은 package version
cache가 artifact와 달라서 해당 cache directory를 `/tmp/zlink-systems-zlink-cache.FIYf4g/`
아래로 recoverable move한 뒤 `--force --no-cache` restore를 수행했다. 복원된 package와
artifact의 SHA-256은
`4990027ee8faa6300bd1bc7305d1c64c0662c3424ff33aea771efcc0c002b1dd0`으로 일치한다.

### 남은 gap과 판정

다음 항목은 현재 .NET Framework 코드만으로 안전하게 닫을 수 없어 완료로 표시하지
않는다.

1. **High — reconnect stale `Disconnected` event의 lifetime identity**

   같은 routing id에 대해 새 `ConnectionReady` 뒤 이전 연결의 늦은 `Disconnected`가
   도착하면 Framework의 disconnected set이 새 연결까지 차단할 수 있다. Core `11.1.0`의
   `ConnectionReady.Value`는 connection generation이 아니라 ready count이며, 현재
   binding monitor event에도 connection lifetime identity가 없다. Framework 내부 지연,
   retry, reflection으로 숨기지 않고 Core/binding owner가 event identity 계약을 제공하는
   별도 작업으로 남긴다.

2. **Medium — Windows Job Object와 process address-space limit의 detector evidence**

   현재 .NET resolver에는 cgroup, `GC.GetGCMemoryInfo().TotalAvailableMemoryBytes`,
   physical memory fallback이 있다. Windows Job Object와 별도 process address-space
   후보를 target runtime별로 확인하는 source/test evidence는 아직 없다. 이는 HWM 계산
   의미의 후속 gap이며, Framework가 process 전체 memory를 직접 제한하는 방식으로 보완하지
   않는다.

3. **Low — allocation/lock contention의 수치 evidence와 control executor 정리**

   pooled receive buffer와 single receive owner로 불필요한 보유와 receive 경합을 줄였지만,
   frame materialization copy가 없는 동작이라고 판정할 benchmark/soak 결과는 없다.
   `_controlIngress`의 reserved-slot 표현도 correctness blocker는 아니지만 POSD 관점의
   후속 정리 후보로 남긴다.

full UnitTests에서 이전에 관찰된 cleanup timing 실패와 `AsyncSubmitter` 예외 타입 불일치는
최신 source와 fresh package에서 재현되지 않았으므로 닫았다. 최종 판정은 **현재 .NET
Framework의 poller-gated receive, STREAM recv ingress, Application HWM 구현과 회귀 gate는
green**이지만, **reconnect lifetime contract와 OS detector parity gap이 남아 전체 spec
parity 완료는 아니다**이다.

## 2026-08-02 20:38 KST Codex review 및 최신 검증

현재 source를 다시 읽고, 직전 Codex finding에 대한 수정과 최신 검증 결과를 반영했다.
공통 spec과 .NET exact interface 문서는 다시 작성하지 않았으며, unrelated dirty worktree
변경은 보존했다.

### 수정 및 재확인

- `ZLinkManagedMeshNode.Start()`에서 poller 등록 뒤 startup 단계가 실패하면 poller를 먼저
  dispose하고 socket을 닫도록 순서를 고쳤다. 이전 순서는 native poller가 등록된 socket을
  먼저 닫을 수 있어 Core pipe cleanup 경합을 만들 수 있었다. 정상 종료 경로도 poller를
  먼저 dispose한 뒤 socket을 dispose한다.
- Framework receive poller는 public `Systems.Zlink.Zlink.CreatePoller()`와 `IPoller.Add`만
  사용한다. STREAM ingress는 packet callback을 등록하지 않고 public `RecvPart`를 호출한다.
- receive-only socket은 `PollIn`을 사용하고, asynchronous request를 같은 socket에서
  진행하는 DEALER와 raw ROUTER는 `PollIn | PollCompletion`을 같은 poller에 등록한다.
  따라서 하나의 native socket에 receive owner와 request-completion owner가 동시에 생기지
  않는다.
- Application HWM resolver는 cgroup 후보를 모두 확인해 가장 작은 유한값을 선택하고,
  Windows Job Object 또는 Linux/macOS process address-space limit을 확인할 수 있으면 OS
  후보에 포함한다. 그 결과를 .NET managed heap 후보와 비교해 작은 값을 사용하며, 기존
  explicit `ProcessMemoryLimitBytes` 우선 semantics와 physical-memory fallback을 유지한다.
- package contract snapshot의 `Systems.Zlink` dependency를 현재 Framework 중앙 설정인
  `11.1.2`에 맞췄다. 이는 binding package version이며, package 안의 native Core 파일은
  `libzlink.so.11.1.0`으로 `11.1.0`을 유지한다. Core source, public header, Core API/ABI는
  수정하지 않았다.

### 최신 실행 결과

| 검증 | 결과 |
|---|---:|
| .NET UnitTests build (`--no-restore --no-incremental --maxcpucount:1`) | 성공, warning 0 / error 0 |
| `InboundDispatchOptionsTests` | `16/16` 통과 |
| 전체 `Zlink.Framework.UnitTests` | `1427/1427` 통과, skipped 0 |
| `verify_packaged_contract.sh` | exit 0, package contract·clean consumer·HTTP consumer 통과 |
| native package | `Systems.Zlink` package `11.1.2`, native Core `11.1.0` |
| native package SHA-256 | `4990027ee8faa6300b1dc7305d1c64c0662c3424ff33aea771efcc0c002b1dd0` |
| .NET Framework/binding/contract/log 대상 `git diff --check` | 통과 |

첫 `verify_packaged_contract.sh` 실행에서는 중앙 package version `11.1.2`와 frozen
snapshot의 `11.1.0`이 달라 실패했다. Framework와 AspNetCore snapshot을 `11.1.2`로
정렬한 뒤 같은 verifier를 다시 실행해 exit 0을 확인했다. 이 package version 정렬은
native Core version 변경으로 해석하지 않는다.

### 현재 판정

현재 .NET Framework의 STREAM recv ingress, Framework socket receive poller 적용,
Application HWM 계산과 전체 UnitTests 및 package consumer gate는 통과했다. 다만 전체
spec parity 완료로 판정할 수 없는 다음 조건은 남아 있다.

1. **High — reconnect stale `Disconnected` event**: Core `11.1.0`과 현재 public .NET
   monitor event는 동일 routing id의 physical connection lifetime identity를 제공하지
   않는다. Framework 내부 delay/retry/reflection으로 숨기지 않고 Core/binding owner의
   별도 public contract gap으로 유지한다.
2. **Low — performance evidence와 control executor 표현**: pooled buffer와 단일 receive
   owner는 적용했지만 frame materialization copy의 allocation/lock contention을 수치로
   증명하는 benchmark 또는 soak 결과가 없다. `_controlIngress`의 reserved-slot 정책
   표현도 POSD 후속 정리 대상으로 남긴다.

따라서 **UnitTests 및 이번 .NET runtime 수정 범위의 회귀 gate는 완료**했지만,
**reconnect lifetime contract와 process E2E를 포함한 Phase A 전체 완료는 아니다**.

## 2026-08-02 21:12 KST Codex final review 및 전체 .NET test gate

최신 source를 기준으로 `review codex` 범위의 receive 경로, Application HWM 계산, Message
ownership, queue admission과 lifecycle cleanup을 다시 대조했다. 이번 round에서도 공통 spec과
`.NET` exact interface 문서는 다시 작성하지 않았고, dirty worktree의 unrelated 변경은 보존했다.

### 추가 수정

- `ZLinkApplicationHwmResolver`가 macOS에서 `/proc/meminfo`에 의존해 physical-memory
  fallback을 놓치지 않도록 `hw.memsize` 조회를 추가했다. cgroup/Job Object/process
  address-space limit과 managed heap limit을 확인할 수 없는 경우에도 Auto 계산이 physical
  memory total로 진행되는 계약을 유지한다.
- `FanoutAutomaticDiscoveryTests.AutomaticRuntime_OwnsOneSocketPerPublisherIdentity`가
  subscriber 객체 생성 직후 endpoint 연결 기록을 읽어 발생하던 순서 경합을 수정했다. 고정
  sleep을 늘리지 않고 두 endpoint가 실제로 기록된 observable 상태를 기다린다.

### Codex review 결과

- Framework의 native socket receive path는 public `Zlink.CreatePoller()`/`IPoller.Add`로
  readiness를 확인한 뒤 non-blocking receive를 호출한다. Channel ROUTER/SUB, ClientServer
  DEALER, STREAM `RecvPart`, managed Mesh ROUTER와 raw service ROUTER를 다시 확인했다.
- STREAM Framework ingress에는 packet callback 등록이 없고 public `RecvPart`만 사용한다.
  source routing id, multipart boundary, segmented/multiple frame 조립, queue admission 전후의
  ownership과 HWM pause/resume 순서를 유지한다.
- Application HWM은 explicit `ApplicationHwmBytes`와 `ProcessMemoryLimitBytes`를 우선하고,
  그 외에는 finite OS limit과 .NET managed heap limit 중 작은 값을 선택한다. 둘 다 없으면
  physical memory total을 사용하며 `ApplicationHwmBytes = 0`의 unlimited 의미를 유지한다.
- review에서 새 Critical/High runtime implementation finding은 추가되지 않았다. 다만
  Core 11.1.0 public monitor event에 physical connection lifetime identity가 없어 같은
  routing id의 이전 `Disconnected`가 새 connection에 적용될 수 있는 High contract gap은
  그대로 남겼다. Framework 내부 delay/retry/reflection으로 감추지 않는다.

### 최신 검증 결과

| 검증 | 결과 |
|---|---:|
| UnitTests build (`--no-restore --no-incremental --maxcpucount:1`) | 성공, warning 0 / error 0 |
| `InboundDispatchOptionsTests` | `16/16` 통과 |
| 전체 `Zlink.Framework.UnitTests` | `1427/1427` 통과, skipped 0 |
| 전체 .NET solution test | `1886/1886` 통과, skipped 0, exit 0 |
| `Zlink.Framework.M5FoundationTests` executable smoke | exit 0; xUnit test project가 아님 |
| `verify_packaged_contract.sh` | exit 0; package contract·clean consumer·HTTP consumer 통과 |
| package provenance | `Systems.Zlink 11.1.2`, native Core `11.1.0` |
| native package SHA-256 | `4990027ee8faa6300b1dc7305d1c64c0662c3424ff33aea771efcc0c002b1dd0` |
| Framework/binding/contract/log 대상 `git diff --check` | 통과 |

solution count는 Stream Connector 142, Framework Unit 1427, Contract 76, HTTP 63,
SampleRegression 134, Locations.Redis 40, ObservabilityOps 4의 합계다. Negative configuration
로그와 일부 forced-stop lifecycle 로그는 해당 회귀 assertion이 의도적으로 발생시킨 출력이며,
최종 test result에는 실패가 없다.

### 현재 판정

이번 .NET runtime 구현 범위와 모든 .NET unit/contract/regression test gate는 완료됐다.
다음은 구현 완료로 오인하지 않고 별도 gap으로 유지한다.

1. **High — reconnect lifetime identity**: Core/binding owner가 public event identity
   contract를 제공해야 한다.
2. **Medium — target OS evidence**: Windows Job Object와 Linux/macOS detector는 source에
   반영했지만 Windows/macOS 실행 환경의 detector evidence는 아직 없다.
3. **Medium — process E2E**: Config 1~14 일부 scenario와 aggregate process evidence가
   남아 있으며 Config 12·14 fail-closed guard는 계속 exit 2다.
4. **Medium — DN-TEST-005**: 중앙 regression matrix의 일부 행은 exact test/scenario/script
   reference가 없어 실행 가능한 reference 분모가 닫히지 않았다.
5. **Low — performance evidence**: pooled buffer와 single receive owner를 적용했지만
   allocation, copy, lock contention을 수치로 확정할 benchmark/soak evidence는 없다.

따라서 **현재 .NET runtime과 전체 unit test gate는 완료**했지만, **reconnect contract,
target OS/process E2E와 중앙 matrix까지 포함한 Phase A 전체 완료는 아니다**.

## 2026-08-02 22:05 KST Codex 재리뷰 — STREAM pre-receive 보강과 RouteMesh HWM 재분류

최신 변경 뒤 `.NET` production receive path와 Application HWM의 admission 시점을 다시
대조했다. 공통 spec과 exact interface 문서는 수정하지 않았고, Core source/header/API/ABI와
unrelated dirty worktree 변경도 건드리지 않았다.

### 추가 수정

- `ZLinkStreamNodeRuntime`가 첫 application frame으로 HWM에 도달한 뒤 다음 poller 대기와
  `RecvPart` 호출로 진행하지 않도록 pre-receive capacity 확인을 추가했다. capacity가 회복될
  때까지 receive loop가 대기하고, 회복 뒤에만 public `RecvPart`를 다시 호출한다.
- `StreamSessionForcedCleanupTests`에 HWM pause 중 다음 `RecvPart` 호출 횟수가 늘지 않는지
  확인하는 회귀 test와 회복 뒤 receive 재개 검증을 추가했다.

### Codex finding

기존 STREAM 경로와 달리 managed RouteMesh 경로에는 다음 순서 차이가 남아 있다.

```text
native Recv -> ProcessReceived -> EnqueueOwned mailbox
             -> dispatch pump -> Track application payload
```

`ZLinkManagedMeshNode.ReceiveLoop`가 poller 뒤 native `Recv`를 수행하고, 수신 payload를
`OwnedMailbox`에 먼저 넣는다. `ZLinkMeshDispatchPump`의 `TrackApplication`은 mailbox에서
꺼낸 뒤에 실행된다. 따라서 Application HWM이 host-wide pending application payload의
admission budget이라는 계약에 비해, RouteMesh mailbox가 HWM accounting 전에 계속 채워질
수 있다. 현재 mailbox 자체에는 별도 message/byte budget이 있지만 이것은 Application HWM
admission을 대신하지 않는다.

이 항목은 다음 이유로 이번 round에서 임의 수정하지 않고 **Medium runtime implementation
gap**으로 남긴다.

- native receive와 dispatch pump가 서로 다른 owner이므로 단순히 receive loop의 sleep 또는
  retry를 넣으면 HWM admission과 control ingress의 책임이 섞인다.
- lease를 mailbox enqueue 시점으로 옮기면 이미 admission된 record를 HWM full 상태에서도
  dispatch해야 하므로 pump의 `CanDrainApplication`과 mailbox claim 순서를 함께 재설계해야
  한다. 일부 경로만 바꾸면 deadlock 또는 lease double-dispose가 발생할 수 있다.
- Core 11.1.0 변경이나 binding private API 우회 없이, RouteMesh mailbox의 admission owner와
  lease ownership을 한 계층으로 정한 뒤 regression test를 추가해야 한다.

따라서 STREAM HWM pre-receive 계약은 이번 수정으로 닫았지만, **모든 Framework application
ingress가 동일한 pre-receive admission 의미를 갖는다고 판정하지 않는다**. 기존 log의
“새 Critical/High runtime implementation finding 없음” 문구는 이 finding으로 보정한다.

### 최신 재검증

| 검증 | 결과 |
|---|---:|
| STREAM/HWM targeted tests | `43/43` 통과 |
| 전체 `Zlink.Framework.UnitTests` | `1428/1428` 통과, skipped 0 |
| 전체 .NET solution test | `1887/1887` 통과, skipped 0, exit 0 |
| Framework/binding/ledger 대상 `git diff --check` | 통과 |

solution 합계는 Stream Connector 142, Framework Unit 1428, Contract 76, HTTP 63,
SampleRegression 134, Locations.Redis 40, ObservabilityOps 4이다. 일부 host startup failure와
forced-stop 로그는 negative configuration/lifecycle 회귀 test가 의도적으로 발생시킨 출력이며,
최종 test result에는 실패가 없다.

### 현재 판정

1. **STREAM recv ingress와 pre-receive HWM pause/resume**: source와 regression test가 통과했다.
2. **RouteMesh mailbox HWM admission 시점**: Medium runtime implementation gap으로 남았다.
3. **High reconnect lifetime identity, Medium target OS/process E2E/DN-TEST-005**: 이전 판정과
   같이 Core/binding contract 또는 별도 process evidence가 필요한 gap으로 남았다.

따라서 UnitTests와 solution regression gate는 green이지만, RouteMesh를 포함한 전체
Application HWM runtime parity와 Phase A 전체 완료는 아직 아니다.

## 2026-08-02 21:58 KST Codex review — RouteMesh admission owner 보강과 최종 gate

직전 review에서 남긴 RouteMesh의 late accounting finding을 실제 owner 경계에서 수정했다.
공통 formal spec과 exact interface 문서는 다시 작성하지 않았다.

### 수정 내용

- `ZLinkSpotNodeInitializer`가 node를 `Start()`하기 전에 host의
  `ZLinkInboundDispatchBudget`을 managed RouteMesh node와 dispatch pump에 함께 설정한다.
- `ZLinkManagedMeshNode`의 native ROUTER receive loop는 HWM이 full이면 다음 native `Recv`를
  발행하기 전에 capacity 회복을 기다린다. native에서 직접 분류한 Node/Channel/Spot/Actor
  application record는 `OwnedMailbox`에 넣기 전에 payload lease를 획득한다.
- local submit과 mailbox에 이미 대기 중인 application record는 `OwnedMailbox.TryClaim`에서
  mailbox lock을 잡은 상태로 budget lock을 기다리지 않도록 admission을 분리했다. pump은
  claim 시 획득한 lease를 typed route/subscribe/actor record로 넘기고 dispatch 또는 drop 때
  한 번만 반환한다.
- `MeshReceiveBatch.Reset`, queued record, route/subscribe/actor batch와 빈 actor frame batch의
  disposal 경로를 다시 확인해 message와 lease가 각각 한 owner에서 해제되도록 했다.
- `StatefulServiceRuntimeTests.RouteMesh_ApplicationHwm_AdmitsOneMailboxAndResumesAfterRelease`
  를 추가했다. 1-byte host HWM에서 두 actor mailbox 중 하나만 admission되고 첫 record의
  batch disposal 뒤 두 번째 mailbox가 재개되는 observable behavior를 검증한다.

### Codex 재검토 결과

- production Framework의 native `Recv`/`RecvPart` 호출을 다시 전수 검색했다. Channel
  ROUTER/SUB, ClientServer DEALER, STREAM, managed Mesh ROUTER, raw ROUTER와 native monitor
  경로는 public poller readiness 뒤에 non-blocking receive를 호출한다.
- STREAM Framework ingress에는 packet callback 등록이 없으며 public `RecvPart` 경로만
  사용한다. source routing id, multipart boundary, segmented/multiple frame 조립과
  admission 전후 ownership은 기존 regression과 함께 유지된다.
- `Zlink.Framework` production source에서 binding private/internal 접근, reflection을 통한
  binding 우회, `InternalsVisibleTo` 추가는 확인되지 않았다. 기존 framework test/handler
  reflection은 binding private API 접근과 다른 범위다.
- POSD 관점에서 native receive, mailbox admission, dispatch lease의 책임을 node/pump/batch에
  분리했다. budget lock과 mailbox lock을 역순으로 획득하지 않도록 했고, pump이 이미
  admission된 record를 다시 계산하지 않도록 lease를 transfer한다.

### 최신 검증 결과

| 검증 | 결과 |
|---|---:|
| UnitTests build (`--no-restore --no-incremental --maxcpucount:1`) | 성공, warning 0 / error 0 |
| RouteMesh/STREAM/HWM targeted UnitTests | `128/128` 통과 |
| RouteMesh HWM 회귀 단독 | `1/1` 통과 |
| 전체 `.NET` UnitTests | `1429/1429` 통과, skipped 0 |
| 전체 `.NET` solution test | `1888/1888` 통과, skipped 0, exit 0 |
| `verify_packaged_contract.sh` | exit 0; package contract·clean consumer·HTTP consumer 통과 |
| package provenance | `Systems.Zlink 11.1.2`, package native `libzlink.so.11.1.0` = Core 11.1.0 |
| native package SHA-256 | `4990027ee8faa6300b1dc7305d1c64c0662c3424ff33aea771efcc0c002b1dd0` |
| public API snapshot | `399d5e99932d10574db163537bf6858f49a221331512358f06b2140c083e549a` |
| `.NET` source/test/ledger 대상 `git diff --check` | 통과 |

전체 solution 합계는 Stream Connector 142, Framework Unit 1429, Contract 76, HTTP 63,
SampleRegression 134, Locations.Redis 40, ObservabilityOps 4이다. Full UnitTests 실행 중 한
초기 시도에서 testhost가 Core `fast_mutex_t::lock()` 내부 abort로 종료한 사례가 있었으나,
같은 binary와 동일 명령을 재실행해 `1428/1428`, 이후 새 회귀 test 포함 solution에서
`1429/1429`와 `1888/1888`을 확인했다. 해당 일회성 native abort는 Core source 변경이나
managed assertion failure가 아니므로, 반복 재현되지 않은 환경/수명 안정성 관찰 항목으로
별도 기록한다.

### 현재 판정

1. **RouteMesh mailbox Application HWM admission**: unit-level runtime gap은 수정했다.
   native direct ingress와 local mailbox claim 모두 dispatch 전 lease를 확보하며, lease
   ownership transfer와 disposal 회귀가 통과했다.
2. **STREAM recv ingress와 poller**: Framework 경로의 callback 혼용 없이 public recv와
   poller 조건을 유지한다.
3. **남은 완료 조건**: mixed-topology process E2E, Config 1~14의 미구현·fail-closed 행,
   Windows/macOS detector 실행 evidence, reconnect physical connection lifetime identity,
   allocation/lock contention benchmark/soak은 별도 gap이다.

따라서 이번 review round의 `.NET` source·unit·solution·package gate는 green이지만, process
E2E와 Core/binding lifetime contract까지 포함한 Phase A 전체 완료로 표현하지 않는다.

## 2026-08-02 23:51 KST Codex 최종 재검토와 회귀 재실행

### STREAM active multipart batch 경계 보강

Codex의 첫 재검토에서 확인된 Medium finding은 `ReceiveBatchSize` 경계에서 이미 시작한
multipart의 마지막 part가 outer HWM 대기에 막힐 수 있다는 점이었다. `ZLinkStreamNodeRuntime`의
outer receive gate와 inner batch gate를 모두 `_multipartRoutingId is null` 조건 아래에 두어,
transport multipart가 시작된 동안에는 final part까지 public `RecvPart`로 소비하도록 수정했다.
multipart가 끝난 뒤에는 다시 `FlushReceiveStates()`와 Application HWM gate를 먼저 확인하므로,
이미 대기 중인 세 번째 application part는 HWM이 회복될 때까지 dequeue하지 않는다.

회귀 test는 다음 순서를 확인한다.

1. 첫 raw part에 첫 complete frame과 두 번째 frame의 prefix를 넣고 `hasMore=true`로 시작한다.
2. 빈 multipart part 63개를 더 읽어 `ReceiveBatchSize=64` 경계에서 active multipart 상태를 만든다.
3. HWM이 찬 상태에서 final part와 세 번째 application part를 추가한다.
4. final part는 dequeue되지만 세 번째 part는 dequeue되지 않는지 확인한다.
5. 첫 handler를 끝낸 뒤 pending frame이 재개되는지 확인한다.

테스트 double의 `RecvPartCount`는 poller의 stale readiness로 발생한 빈 receive 시도도 포함할 수
있으므로, 회귀 판단에는 성공적으로 dequeue한 `DequeuedPartCount`를 사용한다. 이를 통해 poller
호출 시도 수와 실제 transport part 경계를 분리했다.

### Codex 최종 검토

다음 명령으로 Codex read-only review를 실행했다.

```text
codex exec -C /home/hep7/project/kairos/zlink -s read-only --color never --ephemeral
```

- 실행 model: `gpt-5.6-luna`
- reasoning: `max`
- session: `019fc2de-0bc9-77d0-b468-2b55922ee091`
- 범위: `.NET` STREAM receive runtime, dispatch budget, 관련 regression test와 지정된 공통 spec
- 결과: confirmed implementation finding 없음
- severity gate: Critical 0, High 0, Medium 0

Codex는 HWM gate, active multipart의 batch 경계 통과, frame/Message ownership 경로에서
확인된 결함을 찾지 못했다. disposal counter와 전체 producer 구현을 포함한 별도 ownership
계측은 evidence gap으로 남겼지만, 이번 검토 범위에서 결함으로 판정하지 않았다.

### 최신 검증

| 검증 | 결과 |
|---|---:|
| UnitTests build (`--no-restore --no-incremental --maxcpucount:1`) | 성공, warning 0 / error 0 |
| `StreamSessionForcedCleanupTests` | `21/21` 통과 |
| active multipart HWM regression | `1/1` 통과 |
| 전체 `.NET` UnitTests | `1431/1431` 통과, skipped 0 |
| 전체 `.NET` solution test | `1890/1890` 통과, skipped 0, exit 0 |
| bindings `.NET` tests | `142/142` 통과, skipped 0 |
| `verify_packaged_contract.sh` | exit 0; package contract·clean consumer·HTTP consumer 통과 |
| package provenance | `Systems.Zlink 11.1.2`, package native `libzlink.so.11.1.0` = Core 11.1.0 |
| public API snapshot | `399d5e99932d10574db163537bf6858f49a221331512358f06b2140c083e549a` |

전체 solution 합계는 Stream Connector 142, Framework Unit 1431, Contract 76, HTTP 63,
SampleRegression 134, Locations.Redis 40, ObservabilityOps 4이다. 전체 UnitTests와 solution에서
출력된 duplicate registration, invalid timer, bind error는 negative configuration/lifecycle
test가 의도적으로 발생시킨 host log이며 최종 결과는 exit 0이다.

최종 재검토 전 한 번의 solution 실행에서는 테스트 double의 receive 시도 카운터를 실제
dequeue 수로 잘못 사용해 새 regression test가 실패했다. production code failure가 아니며,
성공 dequeue 카운터를 분리한 뒤 UnitTests와 solution을 다시 실행해 모두 통과했다.

### 남은 gap

이번 round는 `.NET` Framework source, public binding recv surface, unit/contract/regression와
package gate를 green으로 만들었다. 다음 항목은 완료로 표시하지 않는다.

1. Core/binding public connection lifetime identity가 없어 same RID의 stale disconnect를
   Framework만으로 안전하게 구분할 수 없다.
2. Windows Job Object와 macOS detector의 실제 실행 환경 evidence가 없다.
3. Config 1~14 mixed-topology process E2E와 aggregate fail-closed 행이 남아 있다.
4. DN-TEST-005 matrix의 일부 행은 exact test/scenario/script reference가 없다.
5. allocation, copy, lock contention benchmark/soak evidence가 없다.

따라서 이번 round의 runtime·unit·package gate는 완료했지만, reconnect contract, process E2E,
target OS evidence와 성능 계측까지 포함한 Phase A 전체 완료는 아니다.

### Commit / push

- commit: `095dbdb2dc8ac1b752d07d32992118b6fffb10a6`
- message: `dotnet: align poller recv ingress and application HWM`
- push: `origin/agent/framework-contract-runtime-update`
- local `HEAD`와 remote branch SHA가 일치한다.
- staged 범위에는 .NET Framework/binding source·test, package 검증 파일, 이 ledger와 현재
  progress log만 포함했다. 공통 formal spec, .NET E2E/sample, Core와 다른 언어 변경은
  worktree에 남겨 두고 commit에서 제외했다.
