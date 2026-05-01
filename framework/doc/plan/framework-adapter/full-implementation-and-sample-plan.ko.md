[계획 목록](./README.ko.md) | [Framework Adapter 정책](../../spec/draft/framework-adapter/policy/README.ko.md) | [.NET 초안](../../spec/draft/framework-adapter/bindings/dotnet/README.ko.md)

# Framework Adapter 전체 구현 및 샘플 완료 계획

이 문서는 `framework-adapter` 초안에 적힌 기능을 코드, 테스트, 샘플까지 빠짐없이
반영하기 위한 실행 계획이다. 작업자는 이 문서를 기준으로 스스로 구현, 리뷰,
리팩토링, 테스트를 반복해야 하며, 중간에 사람의 판단을 기다리지 않는다.

이 계획은 공개 API 계약이 아니다. 실제 계약은 항상
`framework/doc/spec/draft/framework-adapter/` 아래 초안 문서를 기준으로 한다.
이 계획은 그 계약을 어떤 순서로 코드에 옮기고, 어떤 검증을 통과해야 완료로 볼지
정리한다.

## 1. 목표

최종 목표는 아래 조건을 모두 만족하는 것이다.

1. framework 코드가 현재 draft spec의 public API, 실행 의미, 실패 의미를 모두
   구현한다.
2. framework 구현을 반복 리뷰해서 문서에 있는 기능 중 미반영 항목이 하나도 남지
   않는다.
3. framework 구현을 POSD 기준으로 반복 리팩토링해서 더 이상 명확한 리팩토링 항목이
   남지 않는다.
4. TicTacToe sample과 관련 sample code가 draft spec의 흐름을 그대로 따른다.
5. sample도 문서 반영 누락 점검과 POSD 리팩토링을 반복해서 더 이상 이슈가 남지
   않는다.
6. framework 테스트, connector 테스트, sample smoke 테스트가 모두 성공한다.

## 2. 기준 문서

작업자는 아래 문서를 모두 입력으로 읽고, 서로 충돌하면 더 구체적인 문서를 우선한다.
이 계획의 코드 반영 범위는 `.NET` framework adapter, `.NET` stream connector,
TicTacToe sample이다. 다른 언어 binding 초안은 같은 개념을 확인하는 보조 자료로만
사용하고, 이 계획의 완료 조건에는 넣지 않는다.

1. 공통 정책
   - [policy/README.ko.md](../../spec/draft/framework-adapter/policy/README.ko.md)
   - [overview.ko.md](../../spec/draft/framework-adapter/policy/overview.ko.md)
   - [framework-api.ko.md](../../spec/draft/framework-adapter/policy/framework-api.ko.md)
   - [interaction-model.ko.md](../../spec/draft/framework-adapter/policy/interaction-model.ko.md)
   - [message-model.ko.md](../../spec/draft/framework-adapter/policy/message-model.ko.md)
   - [channel-topology.ko.md](../../spec/draft/framework-adapter/policy/channel-topology.ko.md)
   - [session-gateway.ko.md](../../spec/draft/framework-adapter/policy/session-gateway.ko.md)
2. `.NET` framework 상세
   - [README.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/README.ko.md)
   - [handler-interfaces.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/handler-interfaces.ko.md)
   - [lifecycle-and-failure-semantics.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/lifecycle-and-failure-semantics.ko.md)
   - [behavior-matrix.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/behavior-matrix.ko.md)
   - [regression-test-matrix.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/regression-test-matrix.ko.md)
   - [backend-dependency-policy.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/backend-dependency-policy.ko.md)
   - [implementation-scope-and-nongoals.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/implementation-scope-and-nongoals.ko.md)
3. channel, monitoring, registry, SPOT, STREAM, connector
   - [aspnet-core-channel-messaging.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/aspnet-core-channel-messaging.ko.md)
   - [channel-messaging-samples.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/channel-messaging-samples.ko.md)
   - [aspnet-core-monitoring.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/aspnet-core-monitoring.ko.md)
   - [aspnet-core-registry.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/aspnet-core-registry.ko.md)
   - [aspnet-core-spot.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/aspnet-core-spot.ko.md)
   - [spot-samples.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/spot-samples.ko.md)
   - [stage-wrapper-on-spot.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/stage-wrapper-on-spot.ko.md)
   - [aspnet-core-stream.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/aspnet-core-stream.ko.md)
   - [stream-open-items.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/stream-open-items.ko.md)
   - [stream-samples.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/stream-samples.ko.md)
   - [streaming-client.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/streaming-client.ko.md)
   - [unity-stream-connector.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/unity-stream-connector.ko.md)
4. sample spec
   - [tictactoe-game-sample.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/tictactoe-game-sample.ko.md)
   - [README.ko.md](../../spec/sample/tictactoe/README.ko.md)
   - [direct.ko.md](../../spec/sample/tictactoe/direct.ko.md)
   - [session-gateway.ko.md](../../spec/sample/tictactoe/session-gateway.ko.md)
5. use case validation
   - [usecase-validation.ko.md](../../spec/draft/framework-adapter/usecase-validation.ko.md)
   - [use-cases/README.ko.md](../../spec/draft/framework-adapter/use-cases/README.ko.md)
   - [01-service-to-service-rpc.ko.md](../../spec/draft/framework-adapter/use-cases/01-service-to-service-rpc.ko.md)
   - [02-playhouse-play-to-api.ko.md](../../spec/draft/framework-adapter/use-cases/02-playhouse-play-to-api.ko.md)
   - [03-worker-dispatch.ko.md](../../spec/draft/framework-adapter/use-cases/03-worker-dispatch.ko.md)
   - [04-domain-event-fanout.ko.md](../../spec/draft/framework-adapter/use-cases/04-domain-event-fanout.ko.md)
   - [05-cache-invalidation-and-config-refresh.ko.md](../../spec/draft/framework-adapter/use-cases/05-cache-invalidation-and-config-refresh.ko.md)
   - [06-stage-state-sync.ko.md](../../spec/draft/framework-adapter/use-cases/06-stage-state-sync.ko.md)
   - [07-real-time-notification-fanout.ko.md](../../spec/draft/framework-adapter/use-cases/07-real-time-notification-fanout.ko.md)
   - [08-scatter-gather-query.ko.md](../../spec/draft/framework-adapter/use-cases/08-scatter-gather-query.ko.md)
   - [09-workflow-orchestration.ko.md](../../spec/draft/framework-adapter/use-cases/09-workflow-orchestration.ko.md)

## 3. 작업 원칙

- 문서에 있는 기능을 임의로 축소하지 않는다.
- 호환성은 고려하지 않는다. 현재 draft spec이 요구하는 public surface를 우선한다.
- 구현 중 새 판단이 필요하면 먼저 기존 draft spec에 맞는지 확인한다. 그래도 모호하면
  같은 주제를 더 좁게 다루는 문서, `.NET` binding 문서, 공통 정책 문서, POSD 원칙
  순서로 판단하고 계속 진행한다.
- 두 draft 문서가 충돌하면 더 구체적인 문서를 우선한다. 구체성이 같으면 public API를
  더 단순하게 만들고 내부 구현으로 복잡성을 흡수하는 쪽을 선택한다.
- 위 규칙으로도 판단할 수 없으면 `framework/doc/plan/framework-adapter/worklog/` 아래
  작업 로그에 가정과 선택 이유를 남기고, 필요한 draft 문서도 같은 의미로 맞춘 뒤
  진행한다. 이 경우에도 사람의 결정을 기다리지 않는다.
- 한 단계가 끝날 때마다 코드 리뷰를 수행하고, 발견한 이슈를 고친 뒤 다시 리뷰한다.
- 리뷰에서 이슈가 없다고 판단될 때만 다음 단계로 넘어간다.
- POSD 리팩토링은 기능 구현 후 별도 단계로 반복한다.
- 테스트 실패는 계획 종료 조건을 만족하지 못한 상태로 본다.

### 3.1 중간 상태 재개 규칙

작업자가 이 계획을 이미 일부 진행된 저장소에서 다시 시작하면, 처음부터 다시
판단하지 않고 아래 순서로 계속 진행한다.

1. `framework/doc/plan/framework-adapter/worklog/implementation-checklist.md`,
   `posd-review.md`, `sample-posd-review.md`를 먼저 읽는다.
2. `Autonomous Resume Queue` 안에 `pending` 항목이 있으면 그 queue에서 가장 위에
   있는 항목을 현재 작업으로 잡는다. Reference Documents 표의 일반 `pending`은
   queue의 문서 대조 단계에서 해소하며, queue보다 먼저 잡지 않는다.
3. queue 밖에서 새 `pending` 항목을 추가해야 하면 아래 우선순위를 따른다.
   - 공개 API와 draft spec 불일치
   - 실제 구현이 없는 smoke marker 또는 placeholder
   - sample 계약 불일치
   - POSD red flag
   - 테스트와 smoke 누락
4. smoke marker, stub, TODO, 임시 로그만으로 완료 조건을 만족했다고 보지 않는다.
   특히 `session-gateway` smoke가 실제 Session server, Session Actor Dispatch,
   Location Store, Routed Channel, SPOT game room 경로를 실행하지 않으면 완료가 아니다.
5. 경로, project 이름, solution 상태가 현재 저장소와 다르면 실제 저장소 구조를
   기준으로 고친 뒤 worklog에 선택 이유를 남긴다.
6. 다음 구현 방향이 둘 이상 가능하면 POSD 원칙에 따라 두 대안을 비교하고, 더 깊은
   모듈이 되는 쪽을 선택한다. 이 판단도 worklog에 남기고 진행한다.
7. 모호한 점이 있어도 사람에게 질문하지 않는다. draft spec, 더 구체적인 문서,
   `.NET` binding 문서, 공통 정책, POSD 원칙 순서로 해석하고 계속 진행한다.
8. 한 번 테스트가 통과해도 `implementation-checklist.md`에 `pending`이 남아 있으면
   종료하지 않는다. 다음 `pending` 항목으로 넘어간다.

중간 상태에서 가장 중요한 금지 사항은 **부분 smoke를 완료로 포장하는 것**이다.
부분 smoke는 진행 상황을 확인하는 용도로만 사용하고, 계획의 완료 판정에는 실제
기능 구현과 문서 대조 결과를 함께 요구한다.

### 3.2 현재 재개 기준 작업 순서

현재 저장소에서 이어서 작업할 때는 아래 순서를 반드시 따른다. 이 순서는 Phase
번호보다 우선하는 재개 순서다.

이 순서의 API 근거는 아래 draft 문서를 우선해서 확인한다.

- send/publish call builder:
  [handler-interfaces.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/handler-interfaces.ko.md),
  [lifecycle-and-failure-semantics.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/lifecycle-and-failure-semantics.ko.md)
- async submit 의미:
  [framework-api.ko.md](../../spec/draft/framework-adapter/policy/framework-api.ko.md),
  [interaction-model.ko.md](../../spec/draft/framework-adapter/policy/interaction-model.ko.md),
  [streaming-client.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/streaming-client.ko.md)
- routed channel과 session gateway:
  [session-gateway.ko.md](../../spec/draft/framework-adapter/policy/session-gateway.ko.md)
- TicTacToe sample:
  [tictactoe-game-sample.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/tictactoe-game-sample.ko.md)

1. 기존 TicTacToe sample을 보존한다.
   - 경로: `framework/languages/dotnet/samples/TicTacToe`
   - 유지 project: `Shared`, `Server`, `Client`, `TicTacToe.sln`
   - 금지: 기존 sample을 `Direct/` project로 재구성하거나 파일을 gateway sample과
     공유하지 않는다.
   - 허용: framework public API 이름 변경에 필요한 최소 호출부 수정만 한다.
2. 오래된 public submit API 이름과 draft 예시를 먼저 정리한다.
   - `SendAsync(...)`를 framework public builder 실행 함수로 남기지 않는다.
   - actor/session stream send/reply builder도 public 실행 함수는 `Async(...)`다.
   - draft sample에 남은 `.SendAsync(...)` 예시는 `.Async(...)`로 맞춘다.
   - `WebSocket.SendAsync(...)`처럼 외부 transport API 이름은 이 금지 대상이 아니다.
3. framework `.NET` runtime의 공통 async submit runtime을 먼저 구현한다.
   - 기존 channel send/publish/request와 SPOT send/publish/request가 이 runtime을
     사용한다.
   - routed channel은 아직 없더라도, runtime은 routed submit이 같은 경로를 재사용할
     수 있게 socket submit, pending item, deadline, cleanup 정책을 숨긴다.
   - public API는 `SendAsync(...)`라는 별도 메서드가 아니라
     `Send(...).Async(...)` call builder 형태다.
   - blocking send를 `Task.Run`으로 감싸지 않는다.
   - framework `.NET` 구현은 ready 신호를 여러 경로에서 중복 감시하지 않는다.
     pending queue drain은 framework가 소유한 socket의 `OnSendReady(...)` callback
     한 곳에서 시작한다. 이 callback 아래에서 `.NET` binding이
     `zlink_send_ready_handler`에 연결되고, core는 `ZLINK_POLLOUT` readiness를
     사용한다.
   - nonblocking submit, bounded pending queue, ready drain, timeout,
     cancellation, runtime stop 처리를 framework 내부에 숨긴다.
4. framework `.NET` runtime에 routed channel public API를 구현한다.
   - `AddRoutedChannel(...)`
   - `IZLinkRoutedClient`
   - `IZLinkRoutedSendCall`
   - `IZLinkRoutedRequestCall`
   - routed handler registry와 dispatch
   - request sequence 기준 reply matching
   - routed send/request도 3번의 공통 async submit runtime을 사용한다.
5. framework `.NET` runtime에 session gateway와 actor relay API를 구현한다.
   - session server의 `actorId -> stream` binding
   - actor relay
   - session gateway
   - reconnect 시 같은 `actorId`가 새 stream으로 교체되는 동작
6. 위 framework API가 실제로 동작한 뒤에만 gateway sample을 만든다.
   - 경로: `framework/languages/dotnet/samples/TicTacToe(session-gateway)`
   - 기존 `TicTacToe/` sample과 project/file을 공유하지 않는다.
   - sample 내부 `InMemoryRoutedChannel` 같은 대체 transport로 성공시키지 않는다.
7. monitoring, registry, stage wrapper, Unity connector 범위를 문서와 코드로 대조한다.
   구현 범위에 들어가면 구체 queue 항목으로 추가해서 구현하고, non-goal이면
   `not-applicable` 이유를 worklog에 남긴다.
8. 기존 sample build, gateway sample smoke, framework 전체 test를 다시 실행한다.
9. 문서 대조 리뷰와 POSD 리뷰를 반복하고, 모든 worklog `pending`을 해소한다.

## 4. Phase 0: 기준선 수집

### 4.1 현재 코드 inventory

아래 명령으로 현재 public surface와 오래된 API 이름을 찾는다.

```bash
rg -n "WithDontWait|\\.Sync\\(|\\bExec\\(|ExecAsync|dontWait|dont_wait|\\.SendAsync\\(|\\bpublic ValueTask SendAsync\\b" framework/languages/dotnet
if [ -d framework/samples ]; then
  rg -n "WithDontWait|\\.Sync\\(|\\bExec\\(|ExecAsync|dontWait|dont_wait|\\.SendAsync\\(|\\bpublic ValueTask SendAsync\\b" framework/samples
fi
rg -n "WithDontWait|\\.Sync\\(|\\bExec\\(|ExecAsync|dontWait|dont_wait|\\.SendAsync\\(|\\bpublic ValueTask SendAsync\\b" framework/doc/spec/draft/framework-adapter
```

완료 조건:

- 코드와 sample에서 오래된 public submit API가 모두 목록화되어 있다.
- `WebSocket.SendAsync(...)`처럼 외부 transport API 호출은 금지 대상이 아니다.
  framework public builder 실행 함수나 draft sample 호출부에 남은 `.SendAsync(...)`만
  제거 대상으로 분류한다.
- 각 항목이 framework, stream connector, sample 중 어느 소유인지 분류되어 있다.

### 4.2 프로젝트와 테스트 경로 확인

구현 전에 실제 solution, project, test project, sample project 경로를 확인한다.

```bash
find framework/languages/dotnet \( -name "*.sln" -o -name "*.csproj" \) -print | sort
find framework/languages/dotnet/samples -name "*.csproj" 2>/dev/null | sort
find framework/samples -name "*.csproj" 2>/dev/null | sort
```

완료 조건:

- 실제 존재하는 framework solution과 test project 목록이 작업 로그에 정리되어 있다.
- 계획에 적힌 sample project가 아직 없으면 sample spec에 맞춰 생성할 프로젝트 목록이
  정리되어 있다.
- 기존 TicTacToe sample은 `framework/languages/dotnet/samples/TicTacToe`로 고정한다.
- Session Gateway sample은
  `framework/languages/dotnet/samples/TicTacToe(session-gateway)`로 고정한다.
- `.NET` project는 별도 문서가 명시하지 않는 한 `net8.0`을 기준 target framework로
  맞춘다.
- 필수 테스트 명령의 경로가 실제 경로와 다르면 실제 경로 기준으로 계획 문서를 먼저
  수정한다.

### 4.3 문서별 구현 체크리스트 생성

기준 문서마다 public API, 실행 의미, 실패 의미, 회귀 테스트 항목을 추출해서
`framework/doc/plan/framework-adapter/worklog/implementation-checklist.md`에 기록한다.

완료 조건:

- 기준 문서마다 `API`, `Behavior`, `Failure`, `Test`, `Sample` 항목이 있다.
- 각 항목은 `pending`, `implemented`, `verified`, `not-applicable` 중 하나의 상태를
  가진다.
- `not-applicable`은 이 계획의 `.NET` 범위 밖이거나 문서가 명시한 non-goal일 때만
  사용할 수 있고, 이유를 함께 적는다.

## 5. Phase 1: Framework public API 반영

### 5.1 Send / Publish call builder

구현 대상:

- `IZLinkSendCall`
- `IZLinkPublishCall`
- `IZLinkSpotClient.SendChannel(...)`
- `IZLinkSpotClient.Publish(...)`
- `IZLinkSpotPublisherClient.Publish(...)`
- actor context와 session context가 돌려주는 send call

근거 draft:

- [handler-interfaces.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/handler-interfaces.ko.md)
- [lifecycle-and-failure-semantics.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/lifecycle-and-failure-semantics.ko.md)
- [framework-api.ko.md](../../spec/draft/framework-adapter/policy/framework-api.ko.md)

반영 내용:

- `WithDontWait()` 제거
- `Sync()` 제거
- `ValueTask Async(CancellationToken cancellationToken = default)` 추가
- `SendAsync(...)`를 새 public method로 추가하지 않는다. draft의 비동기 submit
  표면은 `Send(...).Async(...)`다.
- send/publish에는 `WithTimeout(...)`을 추가하지 않음
- send/publish backpressure 대기 한계는 channel 또는 socket의 `SendTimeout` 사용
- framework async submit runtime은 core blocking send를 호출해서 timeout을
  맡기지 않는다. socket/channel의 `SendTimeout` 값을 읽어 pending submit deadline으로
  사용한다.
- `SendTimeout` 값 해석은 현재 `.NET` binding/core option 의미와 맞춘다.
  `.NET`에서 `null`은 core `-1`과 같은 무한 대기이고, `TimeSpan.Zero`는 no-wait
  submit이다. 양수 값은 해당 시간까지만 pending submit을 유지한다. 음수
  `TimeSpan`은 `.NET` option setter에서 허용하지 않는다.
- framework channel/socket option의 기본 `SendTimeout`은
  `TimeSpan.FromMilliseconds(200)`으로 설정한다. async submit runtime은 core socket
  기본값을 직접 사용하거나 추정하지 않고, framework가 socket/channel option에
  설정한 resolved `SendTimeout` 값을 읽어 사용한다. 사용자가 option에서
  `SendTimeout = null`을 명시한 경우에만 core `-1`과 같은 무한 대기로 본다.
- 이 규칙은 framework socket/channel submit에 적용한다. stream connector public
  options에는 `SendTimeout`을 두지 않고, connector request reply 대기는
  `RequestTimeout`만 사용한다.

완료 조건:

- public framework 코드에서 `WithDontWait()`와 `Sync()`가 없다.
- sample과 tests가 send/publish 호출에서 `.Async(...)`를 사용한다.
- publish도 send와 같은 async submit 의미를 가진다.

### 5.2 Request call builder

구현 대상:

- `IZLinkRequestCall`
- channel request
- spot channel request
- actor/session context request
- routed request
- stream connector typed request

반영 내용:

- `Async<TReply>(...)`를 canonical 실행 함수로 사용
- request packet submit은 send와 같은 async submit 경로 사용
- `WithTimeout(...)`은 reply 대기 시간만 의미
- submit 단계 backpressure는 `SendTimeout` 정책 사용
- 단, stream connector typed request는 connector public option의
  `RequestTimeout`만 사용한다. stream connector options와 send builder에는
  `SendTimeout`/send timeout `WithTimeout(...)`을 남기지 않는다.
- submit 실패, cancellation, runtime stop 시 pending request 제거

완료 조건:

- request timeout과 send timeout이 테스트에서 분리 검증된다.
- pending request map은 실패 경로에서 누수되지 않는다.
- reply matching은 message name이 아니라 request sequence 기준이다.

### 5.3 Hosting, monitoring, registry, stage wrapper

구현 대상:

- ASP.NET Core hosting adapter
- monitoring adapter
- registry adapter
- SPOT 위 stage wrapper
- backend dependency policy

반영 내용:

- monitoring과 registry는 각 draft 문서의 public surface와 실패 의미를 따른다.
- backend dependency는 framework가 직접 소유할 dependency와 외부 package 사용 범위를
  문서대로 유지한다.
- stage wrapper는 SPOT 실행 context, handler 등록, lifecycle 의미를 숨기고 사용자가
  stage 단위로 사용할 수 있는 깊은 모듈로 만든다.
- implementation scope 문서가 non-goal로 둔 항목은 구현하지 않고 체크리스트에
  `not-applicable`로 남긴다.

완료 조건:

- monitoring, registry, stage wrapper 문서의 public API가 코드와 일치한다.
- scope/non-goal 문서와 반대로 구현된 기능이 없다.
- backend dependency 문서와 project reference/package reference가 일치한다.

## 6. Phase 2: Async submit runtime 구현

### 6.1 공통 submit queue

구현 대상:

- channel client submit
- publisher submit
- spot channel submit
- spot publish submit
- stream write submit
- routed send/request submit

반영 내용:

- blocking send를 `Task.Run`으로 감싸지 않는다.
- 먼저 nonblocking send를 시도한다.
- 바로 성공하면 completed `ValueTask`를 돌려준다.
- would-block이면 bounded pending queue에 넣는다.
- framework `.NET` runtime은 pending queue drain을 socket `OnSendReady(...)`
  callback 한 곳에 연결한다. `zlink_send_ready_handler`와 `ZLINK_POLLOUT`은 그
  아래 native/core 계층의 구현 경로이며, framework가 별도로 세 경로를 모두
  감시하지 않는다.
- `OnSendReady(...)` callback에서 queue를 batch drain한다.
- queue 한계는 high water mark, `SendTimeout`, cancellation, runtime stop으로 닫는다.
- `SendTimeout`은 core blocking send timeout이 아니라 async pending deadline으로
  재사용한다. `.NET` `SendTimeout = null`은 ready callback이 올 때까지 무한 대기,
  `TimeSpan.Zero`는 HWM 발생 시 즉시 실패, 양수 값은 그 시간 안에 submit하지 못하면
  실패로 처리한다.
- framework channel/socket option에서 별도 설정이 없을 때는 framework가
  `SendTimeout = TimeSpan.FromMilliseconds(200)`을 설정한다. async submit runtime은
  이 resolved option 값을 읽는다. core socket 기본값인 `sndtimeo = -1`은 framework
  기본 동작으로 의존하지 않는다.

완료 조건:

- immediate fast path는 불필요한 heap allocation을 만들지 않는다.
- pending queue는 무한으로 증가하지 않는다.
- ready callback은 한 item만 처리하고 끝나지 않고 batch budget 안에서 drain한다.
- runtime stop 시 pending submit이 모두 완료 또는 실패한다.

### 6.2 Message 소유권

반영 내용:

- retry 중 같은 frame이 중복 전송되지 않는다.
- native `Message`는 완료 또는 실패 시 한 번만 dispose된다.
- serialization 결과는 가능한 retry 동안 재사용한다.
- publish는 subscriber 수만큼 payload를 다시 직렬화하지 않는다.

완료 조건:

- 중복 dispose와 누수를 잡는 unit test가 있다.
- publish path에서 topic frame과 payload frame 재사용 정책이 분명하다.

## 7. Phase 3: Stream Connector API 반영

구현 대상:

- `ZlinkStreamSendBuilder`
- `ZlinkStreamRequestBuilder`
- JSON / MessagePack / Protobuf / Auto codec extension
- Unity wrapper

반영 내용:

- `Exec()` 제거
- `ExecAsync()` 제거
- send builder는 `ValueTask Async(CancellationToken)` 사용
- send builder에는 `WithTimeout(...)`을 두지 않는다.
- request builder는 `ValueTask<TReply> Async<TReply>(CancellationToken)` 사용
- stream connector public options에는 `SendTimeout`을 두지 않고, request reply 대기는
  `RequestTimeout`만 사용한다.
- callback request 실행 함수도 `Async(callback)` 또는 동등한 canonical 이름으로 통일
- connector send도 blocking write를 `Task.Run`으로 감싸지 않고 transport async write를 사용

완료 조건:

- `framework/languages/dotnet/src/Systems.Zlink.Stream.Connector*`에서 `Exec` 이름이 없다.
- stream connector public surface에 `ZlinkStreamConnectorOptions.SendTimeout`과 send
  builder `WithTimeout(...)`이 없다.
- `streaming-client.ko.md`의 API와 코드가 일치한다.
- Unity adapter sample도 새 API를 사용한다.
- JSON, MessagePack, Protobuf, Auto codec extension의 옵션 처리와 자동 선택 규칙이
  각 codec 문서와 코드에서 같은 의미를 가진다.

## 8. Phase 4: Session Gateway / Routed Channel 구현

구현 대상:

- router-to-router channel 연결
- `IZLinkRoutedClient`
- `IZLinkRoutedSendCall`
- `IZLinkRoutedRequestCall`
- actor relay
- session gateway
- actorId binding
- location store abstraction

반영 내용:

- routed send/request는 `routerChannelId + targetNodeRid`를 명시한다.
- routed request reply matching은 request sequence 기준이다.
- actorId는 actorId와 같은 domain key로 사용한다.
- session gateway API에는 sessionId를 public routing key로 노출하지 않는다.
- session server는 target session gateway 생성 또는 binding API를 제공한다.

완료 조건:

- `session-gateway.ko.md` 회귀 테스트 항목이 모두 구현되어 있다.
- same message name 동시 request가 sequence 기준으로 정확히 분리된다.
- actor dispatch timeout과 actor binding 없음 오류가 문서대로 처리된다.

## 9. Phase 5: Framework 문서 반영 리뷰 반복

이 단계는 코드 구현 후 반드시 수행한다.

### 9.1 리뷰 절차

1. 기준 문서의 public interface 블록을 모두 추출한다.
2. 코드 public interface와 이름, 반환 타입, timeout/cancellation 위치를 대조한다.
3. 문서의 failure semantics 표를 테스트 목록과 대조한다.
4. 문서의 sample code를 실제 sample code와 대조한다.
5. `implementation-checklist.md`의 상태를 갱신한다.
6. 누락 항목을 수정한다.
7. 다시 1번으로 돌아간다.

종료 조건:

- 기준 문서에 있는 public 기능 중 코드에 없는 항목이 없다.
- 코드에 있는 public 기능 중 문서에 없는 항목이 없다.
- `implementation-checklist.md`에 `pending` 항목이 없다.
- `not-applicable` 항목은 모두 이유가 적혀 있다.
- 오래된 API 이름 검색 결과가 없다.

검증 명령:

```bash
rg -n "WithDontWait|\\.Sync\\(|\\bExec\\(|ExecAsync|dontWait|dont_wait|\\.SendAsync\\(|\\bpublic ValueTask SendAsync\\b" framework/languages/dotnet
if [ -d framework/samples ]; then
  rg -n "WithDontWait|\\.Sync\\(|\\bExec\\(|ExecAsync|dontWait|dont_wait|\\.SendAsync\\(|\\bpublic ValueTask SendAsync\\b" framework/samples
fi
```

검색 결과가 framework public builder 실행 함수나 draft sample 호출부이면 이 단계는
완료가 아니다. `WebSocket.SendAsync(...)`처럼 외부 transport API 호출은 허용한다.

## 10. Phase 6: Framework POSD 리팩토링 반복

### 10.1 리뷰 기준

반복마다 아래 red flag를 찾는다.

- pass-through method가 public API 대부분을 차지하는가
- timeout, codec, routing, sequence 정책이 여러 모듈에 흩어져 있는가
- send, publish, request submit queue 구현이 중복되어 있는가
- request pending map과 sequence 관리가 여러 곳에서 중복되는가
- sample 편의를 위해 `Zlink.Framework` runtime에 특수 코드가 들어갔는가
- public API가 내부 transport detail을 노출하는가

### 10.2 리팩토링 절차

1. red flag 목록을 작성한다.
2. 각 항목마다 POSD 원칙 위반 근거를 적는다.
3. 최소 두 가지 대안을 검토한다.
4. 호출자 복잡성이 줄어드는 대안을 선택한다.
5. 코드를 수정한다.
6. 관련 테스트를 실행한다.
7. 다시 1번으로 돌아간다.

반복 결과는 `framework/doc/plan/framework-adapter/worklog/posd-review.md`에 남긴다.
각 항목에는 red flag, 위반한 원칙, 검토한 대안, 선택한 대안, 수정 결과가 있어야 한다.

종료 조건:

- 새 red flag가 더 이상 발견되지 않는다.
- submit queue, request pending, message ownership, codec, routing 정책이 각각 한
  모듈 안에 숨겨져 있다.
- public API가 구현 세부사항을 설명하지 않아도 사용할 수 있다.

## 11. Phase 7: Sample 구현

### 11.1 기존 TicTacToe sample 보존

구현 대상:

- `framework/languages/dotnet/samples/TicTacToe`
- 기존 `Shared`, `Server`, `Client`, `TicTacToe.sln`
- 기존 API server, Play server, stream client, game room 책임
- file log

반영 내용:

- 기존 sample을 `Direct/` project로 바꾸지 않는다.
- 기존 sample file을 session gateway sample과 공유하지 않는다.
- API 서버와 Play 서버 분리
- Play 서버는 stream client 인증 후 actorId로 actor 생성
- actorId는 actorId와 동일
- TicTacToe game room은 SPOT 기반으로 동작
- opponent joined와 game state push는 `Notify` 접미사 사용
- request packet은 `Req`, response packet은 `Res` 접미사를 사용한다.
- client-facing server push는 sample spec의 더 구체적인 규칙을 따라 `Notify` 접미사를
  사용한다. `Msg` 접미사는 별도 응답이나 push 의미가 없는 일반 one-way command를
  draft가 명시할 때만 사용하며, TicTacToe server push에는 사용하지 않는다.
- client는 `ZlinkStreamConnector.Request(...).Async<TReply>()`를 사용
- stream session은 `PlaySession` 이름 사용
- actor는 `PlayActor`, game room은 `PlayGameRoom` 이름 사용

완료 조건:

- 기존 `TicTacToe.sln`이 build된다.
- 두 client가 같은 room에 join한다.
- 상대 입장 알림을 받는다.
- A/B가 번갈아 move request를 보내고 game state reply를 받는다.
- 승리 또는 draw가 file log와 smoke test에서 확인된다.

### 11.2 Session Gateway TicTacToe sample

구현 대상:

- `framework/languages/dotnet/samples/TicTacToe(session-gateway)`
- session server
- play server
- api server
- session gateway
- actor relay
- location store in-memory 구현
- routed channel
- reconnect smoke

선행 조건:

- `Zlink.Framework` runtime에 `AddRoutedChannel(...)`, `IZLinkRoutedClient`,
  routed handler registry가 실제 구현되어 있다.
- `Zlink.Framework` runtime에 session gateway, actor relay, `actorId -> stream` binding API가
  실제 구현되어 있다.
- routed request/reply matching은 message name이 아니라 request sequence 기준으로
  검증되어 있다.

반영 내용:

- client는 TCP 연결 하나만 session server에 유지한다.
- `CreateMatchReq` 같은 API 요청도 session server를 통해 api server로 전달된다.
- session server는 actorId 기준으로 play server location을 조회한다.
- play server는 session gateway를 통해 client-facing notify와 request를 보낸다.
- location store는 sample에서는 in-memory로 시작하고, Redis 구현 가능 지점을
  interface로 둔다.
- file log로 message flow를 확인할 수 있어야 한다.
- sample 내부 대체 transport로 routed channel을 흉내 내지 않는다. framework
  routed channel API를 사용해야 한다.

완료 조건:

- client 재접속 후에도 같은 actorId binding을 회복한다.
- session server가 바뀌어도 play server actor와 다시 연결된다.
- play server 이동 시 client TCP 연결을 유지하고 relay target만 바꿀 수 있다.
- sample sequence diagram의 모든 메시지가 실제 log에 남는다.
- 기존 `framework/languages/dotnet/samples/TicTacToe` sample과 project/file을
  공유하지 않는다.

## 12. Phase 8: Sample 문서 반영 리뷰 반복

### 12.1 리뷰 절차

1. [direct.ko.md](../../spec/sample/tictactoe/direct.ko.md)를 sample code와 대조한다.
2. [session-gateway.ko.md](../../spec/sample/tictactoe/session-gateway.ko.md)를 sample
   code와 대조한다.
3. [tictactoe-game-sample.ko.md](../../spec/draft/framework-adapter/bindings/dotnet/tictactoe-game-sample.ko.md)를
   sample code와 대조한다.
4. [README.ko.md](../../spec/sample/tictactoe/README.ko.md)를 sample code와 대조한다.
5. packet suffix, project name, directory ownership, log, smoke scenario를 확인한다.
6. 누락이 있으면 sample code나 문서를 수정한다.
7. 다시 1번으로 돌아간다.

종료 조건:

- sample 문서의 서버 구성, sequence, client scenario가 모두 코드에 존재한다.
- TicTacToe README의 공통 규칙이 기존 TicTacToe sample과 session gateway sample
  양쪽에 반영되어 있다.
- sample code에 `PlayHouse` 이름이 없다.
- `SampleShared`에 client 전용 파일이 섞여 있지 않다.
- sample smoke가 기존 TicTacToe sample과 session gateway sample 양쪽 모두 성공한다.

## 13. Phase 9: Sample POSD 리팩토링 반복

리뷰 기준:

- sample 전용 helper가 `Zlink.Framework` runtime에 들어가지 않았는가
- sample shared project가 packet 계약만 담고 있는가
- server/client 책임이 디렉토리와 namespace로 분명히 분리되어 있는가
- game rule, session auth, relay routing, file log가 서로 섞이지 않았는가
- test scenario가 구현 순서에 과도하게 의존하지 않는가

절차:

1. sample red flag를 작성한다.
2. POSD 원칙 위반 근거를 적는다.
3. 대안을 둘 이상 검토한다.
4. 더 깊은 모듈이 되는 쪽으로 수정한다.
5. direct smoke와 gateway smoke를 다시 실행한다.
6. red flag가 없어질 때까지 반복한다.

반복 결과는 `framework/doc/plan/framework-adapter/worklog/sample-posd-review.md`에
남긴다.

종료 조건:

- sample 코드가 framework usage 예제로 읽힌다.
- sample 구현 세부사항이 public framework API 설계를 오염시키지 않는다.
- red flag 목록이 비어 있다.

## 14. Phase 10: 전체 테스트와 완료 판정

### 14.1 필수 명령

아래 명령은 모두 성공해야 한다.

```bash
/home/hep7/.dotnet/dotnet build framework/languages/dotnet/Zlink.Framework.sln -c Debug
/home/hep7/.dotnet/dotnet build framework/languages/dotnet/Zlink.Framework.sln -c Release
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj -c Debug -f net8.0
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.RuntimeTests/Zlink.Framework.RuntimeTests.csproj -c Release -f net8.0
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.MonitoringRuntimeTests/Zlink.Framework.MonitoringRuntimeTests.csproj -c Release -f net8.0
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Zlink.Framework.MultiProcessTests/Zlink.Framework.MultiProcessTests.csproj -c Release -f net8.0
/home/hep7/.dotnet/dotnet test framework/languages/dotnet/tests/Systems.Zlink.Stream.Connector.Tests/Systems.Zlink.Stream.Connector.Tests.csproj -c Release -f net8.0
```

명령에 적힌 경로가 아직 없으면 Phase 0에서 확인한 실제 test project를 기준으로 먼저
문서를 수정한다. framework adapter 기능을 검증하는 test project가 없으면 `net8.0`
기준으로 생성한다.

기존 TicTacToe sample은 항상 아래 명령으로 검증한다.

```bash
/home/hep7/.dotnet/dotnet build framework/languages/dotnet/samples/TicTacToe/TicTacToe.sln -c Debug
```

Session Gateway sample 명령은
`framework/languages/dotnet/samples/TicTacToe(session-gateway)` project를 만든 뒤
그 실제 project 이름으로 worklog에 고정한다. 이 smoke는 framework routed channel,
session actor dispatch API, SPOT game room을 사용해야 하며, sample 내부 대체 transport로
통과시키면 완료가 아니다.

위 smoke project를 만든 뒤에는 아래 형식의 명령을 실제 project 경로로 바꿔
worklog에 고정한다. 이 예시 명령은 완료 근거로 사용할 수 없다.

```bash
/home/hep7/.dotnet/dotnet run --project "framework/languages/dotnet/samples/TicTacToe(session-gateway)/TicTacToe.SessionActorDispatch.csproj" -c Release --no-build
```

### 14.2 완료 판정

완료로 보려면 아래가 모두 참이어야 한다.

- 오래된 API 이름 검색 결과가 없다.
- framework draft spec의 모든 public API가 코드에 있다.
- monitoring, registry, stage wrapper, stream connector, Unity connector 범위가 기준
  문서와 일치한다.
- code review 반복 결과 미반영 문서 항목이 없다.
- `implementation-checklist.md`에 `pending` 항목이 없다.
- framework POSD red flag가 없다.
- sample 문서와 sample code가 일치한다.
- sample POSD red flag가 없다.
- 모든 필수 테스트와 smoke가 성공한다.
- file log에서 기존 TicTacToe sample과 session gateway sample message flow를
  확인할 수 있다.

## 15. 실패 시 원칙

- 테스트가 실패하면 실패를 우회하지 않는다. 실패 원인을 고치고 같은 테스트를 다시
  실행한다.
- 문서와 코드가 다르면 먼저 어느 쪽이 draft spec의 의도와 맞는지 판단하고, 둘을
  같은 상태로 맞춘다.
- 경로, project 이름, test project가 계획과 다르면 실제 repository 구조를 확인한 뒤
  계획 문서를 먼저 맞추고 계속 진행한다.
- 구현이 복잡해지면 public API를 늘리기보다 내부 모듈을 깊게 만든다.
- sample을 맞추기 위해 `Zlink.Framework` runtime에 특수 분기를 넣지 않는다.
- 한 번의 리뷰로 끝났다고 보지 않는다. 리뷰에서 이슈가 0개일 때까지 반복한다.
- 시간이 오래 걸리거나 범위가 커 보여도 계획을 축소하지 않는다. 현재 턴에서 모두
  끝내기 어렵다면 worklog의 다음 `pending` 항목과 검증 명령을 구체적으로 남기고,
  다음 작업자가 즉시 이어서 구현할 수 있게 한다.
- `session-gateway`처럼 아직 실제 구현이 없는 항목은 smoke marker를 제거하거나
  실제 구현으로 대체한다. marker를 유지해야 하는 과도기에는 반드시
  `implementation-checklist.md`에 `pending`으로 남겨 완료로 오해하지 않게 한다.
