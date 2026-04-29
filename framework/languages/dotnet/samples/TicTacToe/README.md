# TicTacToe Samples

이 디렉토리는 두 샘플을 서로 겹치지 않는 프로젝트로 둔다.

| 경로 | 목적 |
|------|------|
| `Direct/` | client가 Play 서버에 직접 연결하는 일반 TicTacToe 흐름 |
| `SessionGateway/` | client가 Session 서버에만 연결하고 Play 서버와는 routed channel로 통신하는 흐름 |
| `TicTacToe.SmokeTests/` | 두 샘플 smoke 검증 |
| `Tools/TicTacToeSmoke/` | `--mode direct`, `--mode session-gateway` CLI smoke |

공통 packet 규칙은 `Req`, `Res`, `Notify` 접미어를 사용한다. 두 샘플 모두
`CreateMatchReq`, `AuthenticateReq`, `JoinMatchReq`, `PlaceMarkReq`,
`OpponentJoinedNotify`, `TurnChangedNotify`, `GameEndedNotify` 이름을 사용한다.

SessionGateway smoke는 marker가 아니다. `Session Server`, `Api Server`,
`Play Server`, `ActorRelay`, `SessionGateway`, `Location Store`, routed channel,
reconnect flow를 실제 객체로 실행하고, `actorId`가 `session-1`에서 `session-2`로
다시 bind된 뒤 notify가 새 Session 서버로 도착하는지 검증한다.
