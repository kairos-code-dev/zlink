# Sample POSD Review

## Current State

상태: `verified`

### Red Flags

- `verified`: 기존 TicTacToe sample은 `framework/languages/dotnet/samples/TicTacToe`
  아래의 `Client`, `Server`, `Shared` 구조를 유지한다.
- `verified`: 기존 sample을 `Direct/` project로 바꾸지 않는다. 기존 server/client/game
  흐름은 그대로 두고, framework public API 변경에 필요한 최소 호출 이름만 맞춘다.
- `verified`: Session Gateway sample은
  `framework/languages/dotnet/samples/TicTacToe(session-gateway)` 아래에 별도 project로
  만든다.
- `verified`: Session Gateway sample은 실제 framework routed channel/session gateway
  public API 위에서 동작해야 한다. sample 내부 `InMemoryRoutedChannel` 같은 대체
  transport로 성공시키면 완료가 아니다.

### Alternatives

- 대안 1: 기존 TicTacToe sample을 `Direct/`로 재구성하고 gateway sample과 나란히 둔다.
- 대안 2: 기존 TicTacToe sample을 그대로 두고,
  `TicTacToe(session-gateway)`에 gateway 버전만 별도 추가한다.

선택: 대안 2를 선택한다. 기존 sample은 이미 정상 동작하는 API/Play/client 흐름을
보여 주고 있으므로 지우거나 재구성하면 sample 사용자의 비교 기준을 잃는다.
Session Gateway는 다른 topology를 설명하는 별도 sample이므로 독립 디렉토리로 둔다.

수정 결과:

- 잘못 추가했던 `Direct/`, `SessionGateway/`, `TicTacToe.SmokeTests`,
  `Tools/TicTacToeSmoke` fake sample tree는 제거했다.
- 기존 `Client/Server/Shared/TicTacToe.sln` sample tree를 복원했다.
- 기존 client의 오래된 `ExecAsync(...)` 호출만 현재 connector API인 `Async(...)`로
  바꿨다.
- `TicTacToe(session-gateway)/TicTacToe.SessionGateway.csproj`를 별도 sample로
  추가했다.
- sample은 session host, play host, stream client를 한 process에서 띄우지만, 통신은
  실제 TCP stream connector와 framework routed channel을 사용한다.
- `OpenActorRelay(...).DispatchAsync(...)`로 client request를 play server에 전달하고,
  play server는 `IZLinkSessionGateway.SendToActor(...).Async(...)`로 client-facing
  notify를 보낸다.

남은 sample red flag:

- `verified`: queue 8 범위에서 새 sample red flag는 남지 않았다. reconnect 전용
  scenario는 framework integration test의 actor binding 교체 규칙과 session gateway
  smoke 근거로 확인하고, 기존 sample tree는 변경하지 않았다.

## Resume Rule

다음 작업자는 기존 `TicTacToe/` sample을 변경하지 않는다. Gateway sample 작업은
`TicTacToe(session-gateway)/` 안에서만 진행한다. 해당 sample은 아래 조건을 만족해야
완료다.

- Session server는 framework STREAM session과 `actorId -> stream` binding을 사용한다.
- API server는 application location store interface를 소유한다.
- Play server는 actor와 game room을 소유한다.
- ActorRelay와 SessionGateway는 framework routed channel 위에서 request sequence
  기준으로 reply를 돌려준다.
- reconnect smoke는 같은 `actorId`가 새 Session server에 bind되고 Play server의
  notify target이 바뀌는 것을 검증한다.

## Session Gateway Refactor

상태: `verified`

### Red Flags

- `verified`: `Program.cs`가 topology 생성, host 구성, client smoke, session handler,
  play proxy, packet DTO, JSON 직렬화를 모두 담고 있었다. 이는 정보 은닉이 약하고,
  sample을 읽는 사람이 Session server와 Play server 경계를 코드 구조로 파악하기
  어렵게 만든다.
- `verified`: JSON 직렬화 옵션과 timeout 값이 호출 지점에 직접 흩어져 있었다. 같은
  정책이 여러 곳에 반복되면 설정 변경 시 누락 위험이 커진다.
- `verified`: Play server와 Session server 책임이 한 파일에 섞여 있어, sample이 실제
  topology를 보여 주기보다 smoke script처럼 읽혔다.

### Alternatives

- 대안 1: 단일 `Program.cs`를 유지하고 region 또는 local function 이름만 정리한다.
- 대안 2: `Client`, `Session`, `Play`, `Contracts`, `Configuration`, `Scenario`로
  디렉토리를 나누고 각 파일이 하나의 책임만 갖게 한다.

선택: 대안 2를 선택했다. sample은 사용자가 topology를 이해하기 위한 코드이므로,
파일 경계가 runtime 책임 경계와 같아야 한다. 단일 파일 local function 정리는 줄 수를
줄일 뿐 정보 은닉을 만들지 못한다.

수정 결과:

- `Program.cs`는 sample scenario 실행과 결과 출력만 맡는다.
- `Scenario/SessionGatewaySampleScenario.cs`는 API, Session, Play host와 client lifecycle만
  조율한다.
- `Infrastructure/`는 embedded registry를 띄우고, API/Session/Play host는
  `UseDiscovery(...)`로 routed channel peer를 자동 발견한다. sample 안에는
  `UseManualConnections(...)`를 두지 않는다.
- `Api/`는 game 생성과 `gameId -> play node` 위치 조회를 맡는다. 위치 저장소는
  `GameLocationStore` 한 곳에 숨겨 두어 Redis 같은 외부 저장소로 바꿀 지점을 분명히 했다.
- `Session/`은 STREAM session과 `actorId -> stream` binding, actor relay를 맡는다.
- `Play/`는 session proxy와 `IZLinkSessionGateway.SendToActor(...).Async(...)` notify를
  맡는다.
- `Client/`는 stream connector request와 notify 수신만 맡는다.
- `Contracts/`, `Configuration/`, `Infrastructure/`로 공유 packet, 이름/timeout,
  endpoint 생성을 분리했다. 별도 sample codec helper나 `System.Text.Json` 직접 호출은
  만들지 않고, framework codec 등록과 `Zlink.Codecs.Json`의 public message extension을
  사용한다.

검증:

```bash
dotnet build "framework/languages/dotnet/samples/TicTacToe(session-gateway)/TicTacToe.SessionGateway.csproj" -c Debug
dotnet run --project "framework/languages/dotnet/samples/TicTacToe(session-gateway)/TicTacToe.SessionGateway.csproj" -c Debug --no-build
```

결과:

- session-gateway sample Debug build 통과.
- session gateway 실행 출력에서 `created=sample-game, reconnect=player-x`,
  `final=XXXOO...., status=Won, winner=player-x`,
  `notifications=state:13, joined:1`을 확인했다.
- sample 내부 fake transport나 old submit API 사용은 없다.

## TicTacToe Game Refactor

상태: `verified`

### Red Flags

- `verified`: 이전 sample은 한 client의 join과 고정 board notify만 확인했다. 실제
  틱택토 규칙, 두 player, turn 검증, 승패 판정이 없어서 session gateway topology를
  설명하는 smoke에 가까웠다.
- `verified`: Play game service가 `RoutingId`를 직접 알고 있었다. 게임 규칙과
  session-gateway 전달 위치가 섞이면 domain state를 읽을 때 transport 지식까지 함께
  추적해야 한다.
- `verified`: Session server가 Play node를 고정 설정으로만 알고 있었다. plan의
  location store 흐름을 보여 주지 못했다.

### Alternatives

- 대안 1: 기존 smoke에 move packet만 몇 개 추가한다.
- 대안 2: API server, Session server, Play server를 분리하고, API의 location store를
  통해 Play 위치를 조회한 뒤 Play가 실제 game state와 notify를 소유하게 한다.

선택: 대안 2를 선택했다. sample은 topology를 설명해야 하므로 고정 shortcut을 남기는
것보다 역할 경계를 코드 구조로 드러내는 편이 호출자 관점 복잡성을 줄인다.

수정 결과:

- `Api/ApiServerHostFactory.cs`, `Api/ApiSessionProxy.cs`, `Api/GameLocationStore.cs`를
  추가했다.
- Session server는 `CreateGameReq`와 `ResolveGameReq`를 actor relay로 API server에
  보내고, join/move는 resolved Play `RoutingId`로 relay한다.
- Play server는 `TicTacToeGameService`가 board, turn, winner를 소유하고,
  `PlayerSessionDirectory`가 actor별 session 위치만 소유한다.
- scenario는 `player-x`가 첫 번째 Session server에서 인증한 뒤 두 번째 Session
  server로 재접속하는 흐름, 두 player join, `X0 O3 X1 O4 X2` move sequence, 최종
  board `XXXOO....`와 winner `player-x`를 검증한다.

재점검:

- `System.Text.Json`, `SampleJson`, `InMemoryRoutedChannel`, native 직접 호출,
  old submit API 검색 결과는 sample 내부에서 없다.
- `UseManualConnections(...)`, discovery retry helper, 고정 discovery wait 검색 결과는
  sample 내부에서 없다.
- 새 framework 회귀 테스트가 stream dispatch 중 nested actor relay request의 packet
  name이 현재 stream packet name에 오염되지 않음을 확인한다.

## SessionProxy Usability Draft Review

상태: `implemented`

### Red Flags

- `implemented`: session sample을 자동 relay builder 중심으로 바꾸면 sample이 단순해
  보이지만, actor 생성 정책과 actor binding 시점이 framework 설정으로 숨어 버린다.
  사용자는 실제 서비스에서 actor가 이미 있을 때, actor type이 여러 개일 때, reconnect
  때 어떤 처리가 일어나는지 다시 찾아야 한다.
- `implemented`: session이나 session proxy에 handler 등록 표면을 두면 sample의
  message 처리 위치가 흐려진다. handler는 actor/node/spot 실행 문맥에만 있어야 sample
  구조와 runtime 책임 경계가 맞는다.

### Alternatives

- 대안 1: sample에서 자동 session relay builder를 사용한다.
- 대안 2: sample은 기존 session callback을 유지하고, 인증과 actor 배치를 직접 보여
  준다. framework helper는 `CreateActorAsync(...)`, `CreateRemoteActorAsync(...)`,
  `DispatchToActorAsync(...)`처럼 actor create와 relay mechanics만 숨긴다.

선택: 대안 2를 선택한다. sample은 "framework가 내 domain 정책을 대신 정한다"가 아니라
"session 경계에서 어떤 결정을 내려야 하는지"를 보여 주어야 한다. request sequence
보존과 relay envelope 조립은 helper로 숨기되, actor 배치와 dispatch 선택은 sample 코드에
남기는 것이 POSD 관점에서 더 명확하다.

수정 결과:

- usability draft의 session server 예시를 `stream.UseSession<TicTacToeSession>()`와
  명시적 `OnPacketAsync(...)` dispatch 형태로 바꿨다.
- 자동 authenticator, 자동 forward, 자동 actor binding builder 예시는 제거했다.
- session context helper는 `SessionProxy` 생성이나 local 전용 binding API가 아니라
  `CreateActorAsync(...)`와 `CreateRemoteActorAsync(...)`로 actor를 만드는 방향으로
  조정했다.
- actor handler 등록 예시는 기존 객체 모델에 맞춰 actor `Configure()`의
  `Context.AddPacket<THandler>()`로 정정했고, spot handler도 spot 객체 안에서 등록하는
  예시로 맞췄다.
- resolver 예시는 request 객체와 metadata를 제거하고 `actorId` 또는 `spotId`만 받도록
  단순화했다. sample에서 metadata 기반 route 분기를 보여 주지 않는다.
- `IZLinkActorSessionLocationWriter` sample은 registry discovery metadata를 infrastructure
  state로만 사용하도록 추가했다. resolver 호출 metadata를 되살리지 않고, writer와
  session route resolver 구현체 내부에서만 registry metadata를 읽고 쓴다.
- reconnect stale unbind가 새 session binding을 지우지 않도록 sample에 `BindingToken`
  조건부 삭제 규칙을 넣었다.
- 완료 기준도 "session code가 raw stream header를 보지 않는다"가 아니라
  "framework helper 없이 raw relay envelope를 직접 조립하지 않는다"로 조정했다.

검증:

결과: 자동 relay builder 표현은 draft에 남지 않았다. 이 항목은 draft와 POSD worklog 갱신이며,
sample code 변경은 아직 수행하지 않았다.
