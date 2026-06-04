# TicTacToe.SessionGateway Sample

Session 서버가 client-facing gateway 가 되고, API 서버와 Play 서버가 각각 인증,
match 생성, actor binding, round 진행을 담당하는 샘플이다. .NET
`TicTacToe.SessionGateway` 샘플처럼 actor bound session push 와 reconnect-safe binding
의미를 Node public framework API 위에서 확인한다.

## 실행

```bash
node framework/languages/node/samples/TicTacToe.SessionGateway/Client/self-check.js
```

## Topology

- `Client/`: 두 route client 를 만든다. 각 client 는 Session 서버에
  `AuthenticateSessionReq` 를 보낸 뒤 match 생성, 참가, mark 배치를 요청한다.
- `Server/Session/`: client packet 을 받고 API/Play 서버로 route request 를 보낸다.
  인증된 actor id 와 session id 를 저장해 이후 request 에 붙인다.
- `Server/Api/`: `AuthenticateActorReq` 로 actor id 를 확인하고, `CreateMatchReq` 를
  Play 서버에 전달한다.
- `Server/Play/`: actor manager, bound session runtime, match room 을 소유한다.
  `EnsurePlayerActorReq`, `JoinMatchReq`, `PlaceMarkReq`,
  `SessionGatewayNotificationsReq` 를 처리한다.
- `Server/Registry/`: 실제 배포 topology 의 registry role 을 보존한다.
- `Shared/Actors/`: bound session 으로 opponent joined, turn changed, game ended push 를
  보내는 player actor 를 정의한다.
- `Shared/Contracts/`: round 규칙과 packet 이름을 공유한다.

## Success Condition

- `p1`, `p2` 두 actor 가 각각 Session 서버에서 인증되고 Play 서버에 보장된다.
- `p1` 이 match 를 만들고 `p2` 가 같은 match 에 참가한다.
- `p1`, `p2` 가 `0,3,1,4,2` 순서로 mark 를 두고 최종 board 는 `XXXOO....` 가 된다.
- 최종 status 는 `Won` 이고 winner 는 `p1` 이다.
- `OpponentJoinedNotify`, `TurnChangedNotify`, `GameEndedNotify` 가 actor bound session
  push 로 전달된다.
- client 는 Server process 의 관찰 가능한 `ready` 이벤트를 받은 뒤 TCP route request 로
  scenario 를 진행한다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 실행, role 분리, API/Session/Play
handler 구성, packet 이름, bound session push, synthetic `RunSessionGateway` 경로 부재를
확인한다.
