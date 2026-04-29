# Sample POSD Review

## Iteration 1

상태: `implemented`

### Red Flags

- `pending`: 현재 TicTacToe sample은 direct와 session gateway가 분리되어 있지 않다.
- `pending`: current sample directory가 계획의 Api/Server/Client/SmokeTests/Tools 구분을 아직 완전히 반영하지 않는다.
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

- `pending`: gateway sample은 실제 Session server, ActorRelay, Location Store, Routed Channel 구현으로 분리해야 한다.
- `pending`: shared packet 계약이 draft의 `Req`/`Res`/`Notify` suffix와 match 용어로 완전히 맞지 않는다.

## Resume Rule

다음 작업자는 sample smoke marker를 완료로 보지 않는다. `--mode session-gateway`가
실제 Session server, API server, Play server, ActorRelay, SessionGateway,
Location Store, reconnect flow를 실행할 때까지 이 파일의 gateway red flag는
`pending`으로 유지한다.
