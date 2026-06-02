# Bingo Sample

four-player matching, room Spot, host start, timer draw, bound push 를 하나의
deterministic 시나리오로 검증한다.

## 실행

```bash
node framework/languages/node/samples/Bingo/Client/self-check.js
```

## Topology

- `Client/`: Registry, Session, Play, API 서버 process 를 시작하고 API 서버의 실제
  TCP route endpoint 로 Bingo scenario request 를 보낸다.
- `Server/Api/`: `api-server-host-factory.js` 가 route 서버를 구성하고,
  `Handlers/` 의 authenticate/match handler 가 요청을 처리한다.
- `Server/Play/`: `play-server-host-factory.js` 가 route 서버를 구성하고,
  `Handlers/run-bingo-room-handler.js` 가 Bingo room 흐름을 실행한다.
- `Server/Session/`: `session-server-host-factory.js` 가 session 역할 서버를 구성한다.
- `Server/Registry/`: `registry-host-factory.js` 가 registry 역할 서버를 구성한다.
- `Shared/`: Bingo card 계약을 공유한다.

## Success Condition

- 네 player 가 서로 다른 actor 로 session 에 bind 되고 같은 room 에 match 된다.
- 첫 참가자인 `p1` 이 host 가 된다.
- 네 명이 들어오기 전 host start 요청과 non-host start 요청은 거부된다.
- host start 뒤 room timer 가 draw tick 을 발생시킨다.
- 같은 draw sequence 에서 `p1`, `p3` 이 deterministic winners 가 된다.
- started, drawn, ended notification 이 모든 bound session 으로 전달된다.
- client 가 Server process 의 관찰 가능한 준비 신호를 받은 뒤 TCP route request 를 보낸다.
- sample 이 별도 notification 저장소를 만들지 않고 framework bound session runtime 의
  transport 경로로 전송 결과를 관찰한다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 실행 및 import 정책을 확인한다.
