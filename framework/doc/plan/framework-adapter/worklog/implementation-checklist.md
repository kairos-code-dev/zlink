# Framework Adapter Implementation Checklist

이 파일은 `full-implementation-and-sample-plan.ko.md` Phase 0에서 만든 작업 로그다.
상태 값은 `pending`, `implemented`, `verified`, `not-applicable`만 사용한다.

## Phase 0 Inventory

### Project Paths

상태: `implemented`

- Framework solution: `framework/languages/dotnet/Zlink.Framework.sln`
- Framework tests:
  - `framework/languages/dotnet/tests/Zlink.Framework.Tests/Zlink.Framework.Tests.csproj`
  - `framework/languages/dotnet/tests/Zlink.Framework.RuntimeTests/Zlink.Framework.RuntimeTests.csproj`
  - `framework/languages/dotnet/tests/Zlink.Framework.MonitoringRuntimeTests/Zlink.Framework.MonitoringRuntimeTests.csproj`
  - `framework/languages/dotnet/tests/Zlink.Framework.MultiProcessTests/Zlink.Framework.MultiProcessTests.csproj`
  - `framework/languages/dotnet/tests/Systems.Zlink.Stream.Connector.Tests/Systems.Zlink.Stream.Connector.Tests.csproj`
- Current TicTacToe sample projects:
  - `framework/languages/dotnet/samples/TicTacToe/Client/TicTacToe.Client.csproj`
  - `framework/languages/dotnet/samples/TicTacToe/Server/TicTacToe.Server.csproj`
  - `framework/languages/dotnet/samples/TicTacToe/Shared/TicTacToe.Shared.csproj`
  - `framework/languages/dotnet/samples/TicTacToe/TicTacToe.SmokeTests/TicTacToe.SmokeTests.csproj`
  - `framework/languages/dotnet/samples/TicTacToe/Tools/TicTacToeSmoke/TicTacToeSmoke.csproj`
- Planned sample projects not yet present:
  - `framework/languages/dotnet/samples/TicTacToe/Api/TicTacToe.Api.csproj`
  - `framework/languages/dotnet/samples/TicTacToe/Gateway/Session/TicTacToeGateway.Session.csproj`
  - `framework/languages/dotnet/samples/TicTacToe/Gateway/Api/TicTacToeGateway.Api.csproj`
  - `framework/languages/dotnet/samples/TicTacToe/Gateway/Play/TicTacToeGateway.Play.csproj`

`framework/samples` 디렉토리는 현재 없으므로 sample 범위는 실제 경로인
`framework/languages/dotnet/samples`를 기준으로 진행한다. 계획 문서의 검증 명령도
없는 경로에서 실패하지 않도록 조건부 검색으로 수정했다.

### Old Submit API Inventory

상태: `verified`

초기 발견 항목:

- Framework core: `WithDontWait()`, `Sync()`가 channel, SPOT, actor channel send/publish call에 남아 있었다.
- Stream connector: `Exec()`, `ExecAsync()`가 core builder와 JSON, MessagePack, Protobuf, Auto codec wrapper에 남아 있었다.
- Tests and fixtures: framework tests, stream connector tests, doc fixtures, test host가 오래된 실행 이름을 사용했다.
- Sample: current TicTacToe client가 `ExecAsync<TReply>()`를 사용했다.

처리 결과:

- 위 항목은 모두 `.Async(...)`로 변경되었다.
- 오래된 public submit API 이름은 현재 검색 결과가 없다.

현재 검증:

```bash
rg -n "WithDontWait|\\.Sync\\(|\\bExec\\(|ExecAsync|dontWait|dont_wait" framework/languages/dotnet framework/doc/spec/draft/framework-adapter
if [ -d framework/samples ]; then
  rg -n "WithDontWait|\\.Sync\\(|\\bExec\\(|ExecAsync|dontWait|dont_wait" framework/samples
fi
```

검색 결과 없음.

## Reference Documents

### policy/README.ko.md

- API: `pending`
- Behavior: `pending`
- Failure: `pending`
- Test: `pending`
- Sample: `pending`

### policy/overview.ko.md

- API: `pending`
- Behavior: `pending`
- Failure: `pending`
- Test: `pending`
- Sample: `pending`

### policy/framework-api.ko.md

- API: `implemented` -- send/publish/request 실행 이름은 `Async(...)`로 맞췄다.
- Behavior: `implemented` -- 오래된 no-wait public option은 제거했다.
- Failure: `pending`
- Test: `verified` -- framework, runtime, multiprocess test 통과.
- Sample: `pending`

### policy/interaction-model.ko.md

- API: `implemented` -- submit builder의 public 실행 함수는 `Async(...)`만 남았다.
- Behavior: `implemented` -- stream connector send는 transport async write를 직접 사용한다.
- Failure: `pending`
- Test: `verified` -- connector request sequence test와 framework request tests 통과.
- Sample: `pending`

### policy/message-model.ko.md

- API: `pending`
- Behavior: `pending`
- Failure: `pending`
- Test: `pending`
- Sample: `pending`

### policy/channel-topology.ko.md

- API: `pending`
- Behavior: `pending`
- Failure: `pending`
- Test: `pending`
- Sample: `pending`

### policy/session-gateway.ko.md

- API: `pending`
- Behavior: `pending`
- Failure: `pending`
- Test: `pending`
- Sample: `pending`

### bindings/dotnet/handler-interfaces.ko.md

- API: `implemented` -- `IZLinkSendCall`, `IZLinkPublishCall`에서 `WithDontWait()`와 `Sync()` 제거.
- Behavior: `implemented` -- in-repo caller는 send/publish에서 `.Async(...)`를 사용한다.
- Failure: `pending`
- Test: `verified` -- `Zlink.Framework.Tests`, `Zlink.Framework.RuntimeTests`, `Zlink.Framework.MultiProcessTests` 통과.
- Sample: `pending`

### bindings/dotnet/streaming-client.ko.md

- API: `implemented` -- stream connector core/JSON/MessagePack/Protobuf/Auto codec wrapper에서 `Exec`, `ExecAsync` 제거.
- Behavior: `implemented` -- send builder는 `ValueTask Async(...)`, request builder는 `ValueTask<TReply> Async<TReply>(...)`를 사용한다.
- Failure: `pending`
- Test: `verified` -- `Systems.Zlink.Stream.Connector.Tests` 통과.
- Sample: `implemented` -- TicTacToe client가 connector `.Async<TReply>(...)`를 사용한다.

### TicTacToe sample specs

- API: `pending`
- Behavior: `implemented` -- direct smoke는 두 client join, notify, 교대 move, 승리 상태를 검증한다.
- Failure: `pending`
- Test: `verified` -- `TicTacToe.SmokeTests`와 direct/session-gateway smoke command 통과.
- Sample: `pending` -- SessionGateway는 아직 실제 routed channel/session server/play server 분리 구현이 아니라 smoke marker 수준이다.

## Known Gaps

- `pending`: full SessionGateway routed channel 구현은 아직 없다. 현재 `--mode session-gateway` smoke는 actorId 안정성 marker와 direct runtime smoke를 함께 확인한다.
- `pending`: sample packet 이름은 기존 `Game` 모델을 유지한다. draft의 `Match`, `OpponentJoinedNotify`, `TurnChangedNotify`, `GameEndedNotify` 계약으로 완전히 정리해야 한다.
- `pending`: plan에 적힌 API/Server/Client project split 중 `Api` 별도 project는 아직 만들지 않았다. 기존 `TicTacToe.Server` 안의 `Api`/`Play` 구성으로 direct smoke를 검증한다.
- `pending`: solution 파일은 기존 duplicate project entry 때문에 `dotnet sln add`가 실패했다. 새 smoke project는 직접 csproj 명령으로 검증했다.

## Autonomous Resume Queue

이 저장소가 현재 상태에서 다시 열리면 아래 순서대로 사람에게 묻지 않고 계속
진행한다.

1. `pending`: solution 파일의 duplicate project entry를 정리한 뒤 smoke project를
   solution에 추가한다. 정리 후 Debug/Release build를 다시 실행한다.
   - 유지할 entry:
     - `Systems.Zlink.Stream.Connector.Json` GUID `{930BD426-7750-4A83-9960-FB690DFEB1D8}`
     - `Systems.Zlink.Stream.Connector` GUID `{EF560B85-2ED8-43D1-B6C4-8B9A86EA5A99}`
   - 제거할 duplicate entry와 관련 `ProjectConfigurationPlatforms`, `NestedProjects` 행:
     - `Systems.Zlink.Stream.Connector.Json` GUID `{4ED534BB-0AB0-43C4-A08E-9CCD313F09F9}`
     - `Systems.Zlink.Stream.Connector` GUID `{A5301BBD-A51D-421D-A3E6-07EB5F2FB4E3}`
   - 정리 뒤 실행:
     ```bash
     /home/hep7/.dotnet/dotnet sln framework/languages/dotnet/Zlink.Framework.sln add \
       framework/languages/dotnet/samples/TicTacToe/Tools/TicTacToeSmoke/TicTacToeSmoke.csproj \
       framework/languages/dotnet/samples/TicTacToe/TicTacToe.SmokeTests/TicTacToe.SmokeTests.csproj
     /home/hep7/.dotnet/dotnet build framework/languages/dotnet/Zlink.Framework.sln -c Debug
     /home/hep7/.dotnet/dotnet build framework/languages/dotnet/Zlink.Framework.sln -c Release
     ```
2. `pending`: `WithMessageName(...)` public builder 이름을 draft의
   `WithPacketName(...)`으로 맞춘다. 내부 envelope 필드명은 필요한 만큼만 유지하고,
   public surface에는 packet 용어를 사용한다.
3. `pending`: TicTacToe shared packet을 draft와 맞춘다.
   - `CreateGame*` -> `CreateMatch*`
   - `GameState` -> `TicTacToeState`
   - `PlayerJoinedNotify` -> `OpponentJoinedNotify`
   - `GameStateNotify`를 `TurnChangedNotify`와 `GameEndedNotify`로 분리
   - request는 `Req`, response는 `Res`, one-way push는 `Notify` 접미사를 사용
4. `pending`: direct sample project 구조를 계획과 맞춘다. `Api`, `Server` 또는
   `Play`, `Client`, `Shared`, `Tools`, `TicTacToe.SmokeTests` 책임을 분리하고
   `SampleShared` 또는 `Shared`에는 packet 계약만 둔다.
5. `pending`: 실제 Session Gateway sample을 구현한다.
   - Session server는 client stream과 `actorId -> stream` binding을 소유한다.
   - API server는 in-memory location store interface를 소유한다.
   - Play server는 actor와 game room을 소유한다.
   - ActorRelay와 SessionGateway는 routed channel 위에서 request sequence 기준으로
     reply를 돌려준다.
   - reconnect smoke는 같은 `actorId`가 새 Session server에 bind되고 Play server의
     notify target이 바뀌는 것을 log와 assertion으로 검증한다.
6. `pending`: framework submit runtime을 bounded pending queue + ready drain 모델로
   통합한다. `Task.Run`으로 blocking send를 감싸지 않는다는 조건을 unit test로
   확인한다.
7. `pending`: 문서 대조 리뷰를 다시 실행하고 이 파일의 모든 `pending`을
   `implemented`, `verified`, `not-applicable` 중 하나로 바꾼다. `not-applicable`은
   이유를 반드시 쓴다. 위 `Reference Documents`의 일반 `pending` 항목은 이 단계에서
   문서별로 해소한다. 대조 중 구체 구현 작업이 발견되면 그 작업을 7번보다 앞에 새
   queue 항목으로 추가하고 즉시 구현한다.
8. `pending`: POSD 리뷰를 반복해 `posd-review.md`와 `sample-posd-review.md`에 새
   red flag가 없다고 기록한다.
9. `pending`: Phase 10의 모든 build/test/smoke 명령을 다시 실행한다.

재개 시 첫 작업자는 위 목록의 1번부터 시작한다. 이미 완료된 항목이 있으면 근거
명령과 파일 경로를 적고 다음 항목으로 넘어간다.

## Autonomy Review Log

### Iteration 1

문제:

- 없는 `framework/samples` 경로가 `rg` 명령에 직접 들어가면 검증 중 에러가 날 수 있다.
- old submit API inventory가 `verified` 상태인데도 현재 남아 있는 항목처럼 읽혔다.
- solution duplicate 정리 방법이 구체적이지 않아 다음 작업자가 다시 판단해야 했다.

수정:

- 계획서와 worklog의 old API 검색 명령을 조건부 `framework/samples` 검색으로 바꿨다.
- inventory 문장을 초기 발견 항목과 처리 결과로 나눴다.
- 유지/제거할 solution GUID와 후속 명령을 resume queue에 적었다.

### Iteration 2

문제:

- Direct sample suffix 규칙에서 `Msg`와 `Notify`가 충돌할 수 있었다.
- Phase 10 필수 명령에 runtime, monitoring, stream connector test가 빠져 있었다.
- current/planned sample project 목록이 smoke project 생성 후 상태와 맞지 않았다.

수정:

- client-facing push는 `Notify`를 우선한다는 구체 규칙을 계획서에 추가했다.
- Phase 10 필수 명령에 runtime, monitoring, stream connector test를 추가했다.
- 현재 존재하는 smoke project와 아직 없는 gateway/API project를 분리해서 적었다.

### Iteration 3

리뷰 결과:

- 현재 문서에는 사용자 판단을 기다려야만 다음 단계로 갈 수 있는 항목이 없다.
- 남은 구체 구현 `pending` 항목은 모두 `Autonomous Resume Queue`에 구현 순서와 검증 방법이 있다.
- reference 문서별 일반 `pending` 항목은 queue 7번의 문서 대조 리뷰에서 해소한다.
- 부분 smoke marker는 완료로 보지 않는다는 규칙이 계획서와 sample POSD worklog에 모두 있다.
