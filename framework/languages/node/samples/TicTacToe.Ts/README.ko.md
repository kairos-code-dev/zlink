# TicTacToe TypeScript Sample

두 player 가 HTTP API 로 game 을 만들고, Play stream endpoint 에 각각 연결해
인증, 참가, mark 배치를 수행하는 샘플이다. .NET TicTacToe 샘플과 같은 사용자
흐름을 Node public framework API 와 stream connector public API 위에서 보여준다.
Node framework 의 기본 TicTacToe NestJS sample 은 이 TypeScript sample 을 기준으로
제공한다.

## 실행

```bash
cd framework/languages/node/samples/TicTacToe.Ts
npm run build
npm run start
```

## Topology

- `Client/`: API 서버와 Play 서버 process 를 시작한다. HTTP `/games` 로 game 을
  만든 뒤 두 stream connector 를 Play stream endpoint 에 연결한다.
- `Server/Api/`: HTTP `/games` 요청을 받고 Play channel 의 `CreateGame` request 로
  game 생성을 위임한다. Play stream session 이 보내는 `AuthenticatePlayerReq` 도
  API channel handler 로 처리한다.
- `Server/Play/`: Play channel 의 `CreateGame` handler 로 game room 을 만들고,
  stream endpoint 에서 `AuthenticateReq`, `JoinGameReq`, `PlaceMarkReq` 를 처리한다.
- `Server/Play/Actors/`: player actor 를 보관하고 stream notification 대상을 추적한다.
- `Server/Play/EntrySpot/`, `Server/Play/GameSpots/`: 참가, 생성, timer 등록, mark 배치
  handler 를 분리한다.
- `Shared/`: packet 이름, channel 이름, timeout 같은 샘플 계약을 공유한다.

## Success Condition

- client 가 HTTP `/games` 로 game 을 만들고 Play stream endpoint 를 받는다.
- `p1`, `p2` 가 각각 stream connector 로 연결한 뒤 `AuthenticateReq` 를 보낸다.
- 두 actor 가 같은 game 에 `JoinGameReq` 로 참가하고 mark 는 `X`, `O` 로 배정된다.
- `p1`, `p2` 가 `0,3,1,4,2` 순서로 `PlaceMarkReq` 를 보내고 최종 board 는
  `XXXOO....` 가 된다.
- 최종 status 는 `Won` 이고 winner 는 `p1` 이다.
- `PlayerJoinedNotify` 와 `GameStateNotify` 가 stream send packet 으로 전달된다.
- client 는 Server process 의 관찰 가능한 `ready` 이벤트를 받은 뒤 self-check 를
  진행한다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 실행, public API 사용, HTTP/stream
흐름, packet 이름, 축약 `RunTicTacToe` 경로 부재를 확인한다.
