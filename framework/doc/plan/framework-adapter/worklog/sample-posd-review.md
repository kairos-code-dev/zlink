# Sample POSD Review

## Iteration 1

상태: `implemented`

### Red Flags

- `verified`: TicTacToe sample은 `Direct/`와 `SessionGateway/` 별도 프로젝트로 분리했다.
- `verified`: current sample directory는 `Direct`, `SessionGateway`,
  `TicTacToe.SmokeTests`, `Tools/TicTacToeSmoke` 책임으로 분리했다.
- `implemented`: sample smoke가 독립 test/tool project로 고정되었다.

### Alternatives

- 대안 1: 기존 sample을 점진적으로 새 direct sample로 바꾸고 gateway sample을 추가한다.
- 대안 2: 기존 sample을 보존하면서 새 sample tree를 병렬 생성한다.

선택: 기존 direct sample이 이미 실행 가능한 API/Play/client 흐름을 갖고 있어 대안 1을 선택했다. 새 smoke project와 tool을 추가해 검증 경로를 먼저 고정했다.

수정 결과:

- `TicTacToe.SmokeTests`가 direct smoke와 과도기용 session-gateway smoke marker를 검증한다.
- `Tools/TicTacToeSmoke`가 계획에 적힌 `--mode direct`, `--mode session-gateway` 명령 표면을 제공한다.
- checked-in sample source에는 `PlayHouse` 이름이 없다.

남은 red flag:

- `verified`: gateway sample은 실제 Session server, API server, Play server,
  ActorRelay, SessionGateway, Location Store, Routed Channel 구현으로 분리했다.
- `verified`: packet 계약은 `Req`/`Res`/`Notify` suffix와 match 용어를 사용한다.

## Resume Rule

다음 작업자는 sample smoke marker를 완료로 보지 않는다. `--mode session-gateway`가
실제 Session server, API server, Play server, ActorRelay, SessionGateway,
Location Store, reconnect flow를 실행할 때까지 이 파일의 gateway red flag는
`pending`으로 유지한다.

## Iteration 2

상태: `verified`

### Red Flags

- `verified`: direct와 session-gateway sample이 같은 root sample project를 공유하던
  구조를 제거했다.
- `verified`: session-gateway smoke marker를 실제 in-memory routed channel flow로
  대체했다.
- `verified`: packet 이름은 `CreateMatchReq`, `CreateMatchRes`, `JoinMatchReq`,
  `TicTacToeState`, `OpponentJoinedNotify`, `TurnChangedNotify`,
  `GameEndedNotify`로 정리했다.

### Alternatives

- 대안 1: 기존 root `Client/Server/Shared` sample을 유지하고 gateway 파일만 추가한다.
- 대안 2: 기존 root sample을 제거하고 `Direct/`와 `SessionGateway/` 프로젝트를 완전히
  분리한다.

선택: 대안 2를 선택했다. 사용자가 기존 버전과 session-gateway 버전이 겹치지 않게
진행하라고 했고, sample별 책임과 smoke가 섞이지 않는 쪽이 POSD의 정보 은닉에 맞다.

수정 결과:

- `Direct/TicTacToe.Direct.csproj`는 API, Play, Client, game room flow를 독립 실행한다.
- `SessionGateway/TicTacToe.SessionGateway.csproj`는 Session server, API server,
  Play server, ActorRelay, SessionGateway, Location Store, Routed Channel,
  reconnect flow를 독립 실행한다.
- `TicTacToe.SmokeTests`와 `Tools/TicTacToeSmoke`가 두 mode를 모두 검증한다.

남은 sample red flag:

- 없음. 현재 sample 범위에서 marker, `PlayHouse` 이름, old packet suffix, direct/gateway
  project overlap은 검색 결과 없다.
