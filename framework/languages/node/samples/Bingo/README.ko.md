# Bingo Sample

four-player matching, room actor flow, host start, timer draw, bound push 를 하나의
deterministic 시나리오로 검증한다.

## 실행

```bash
node framework/languages/node/samples/Bingo/Client/self-check.js
```

## Topology

- `Client/`: Registry, Session, Play, API 서버 process 를 시작하고 네 player client 를
  만든다. 각 client 는 Session 서버에 `AuthenticateReq`, `MatchBingoReq`,
  `StartBingoGameReq` 를 보내고, Session 서버가 API/Play channel 로 인증과 actor relay 를
  수행한다.
- `Server/Api/`: `main.js` 가 `BingoApiModule` 을 정의하고 `NestFactory` 로
  application context 를 시작한다. `bingo.api` channel 은 `handlerGroups: ['api']` 로
  API handler group 을 선택하고, API handler provider 는 `zlinkHandlerGroup('api', ...)`
  로 묶는다. match handler 는 `ZLINK_CHANNEL_CLIENT` 를 주입받아 Play channel 로
  `AllocateBingoRoom` request 를 보낸다.
- `Server/Play/`: `main.js` 가 `BingoPlayModule` 을 정의하고 `bingo.play` channel 을
  연다. `handlerGroups: ['play']` 로 Play handler group 을 선택하고,
  `zlinkHandlerGroup('play', ...)` 로 Play channel handler provider 를 묶는다. actor
  factory, entry spot, room spot, timer handler, notification publisher 가 같은 mode 의
  room 을 만들고 player join/start/draw/push 흐름을 실행한다.
- `Server/Session/`: `main.js` 가 session 역할의 Nest application context 를 시작하고
  `AuthenticateSessionHandler` 와 `BingoSession` relay 구조를 제공한다.
- `Server/Registry/`: `main.js` 가 registry 역할의 Nest application context 를 시작한다.
- `Shared/`: sample name, timing, message helper, Bingo card 계약을 공유한다.

## Success Condition

- 네 player 가 서로 다른 actor 로 bind 되고 같은 room 에 match 된다.
- 첫 참가자인 `player-1` 이 host 가 된다.
- 네 명이 들어오기 전 host start 요청과 non-host start 요청은 거부된다.
- host start 뒤 room timer 가 draw tick 을 실행한다.
- timer draw 뒤 같은 draw sequence 에서 `player-1`, `player-3` 이 winner 가 된다.
- started, drawn, ended notification 이 모든 bound session 으로 전달된다.
- client 가 Server process 의 관찰 가능한 준비 신호를 받은 뒤 실제 TCP route/channel
  request 를 보낸다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 실행 및 import 정책을 확인한다.
