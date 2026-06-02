# Bingo Sample

API 서버가 Play channel 로 room allocation request 를 보내는 흐름을 검증한다.
전체 four-player join, host start, timer draw, bound push 흐름은 dotnet sample 과
동일한 구조로 추가 구현해야 한다.

## 실행

```bash
node framework/languages/node/samples/Bingo/Client/self-check.js
```

## Topology

- `Client/`: Registry, Session, Play, API 서버 process 를 시작하고 API channel 의 실제
  TCP endpoint 로 `AuthenticatePlayerReq`, `MatchBingoApiReq` request 를 보낸다.
- `Server/Api/`: `api-server-host-factory.js` 가 `bingo.api` channel 서버를 구성하고
  `handlerGroups: ['api']` 로 API handler group 을 노출한다. match handler 는
  `.NET` sample 과 같이 Play channel 로 `AllocateBingoRoom` request 를 보낸다.
- `Server/Play/`: `play-server-host-factory.js` 가 `bingo.play` channel 서버를 구성하고,
  `handlerGroups: ['play']` 로 Play handler group 을 노출한다.
  `Handlers/allocate-bingo-room-handler.js` 가 같은 mode 의 room id 를 할당한다.
- `Server/Session/`: `session-server-host-factory.js` 가 session 역할 서버를 구성한다.
- `Server/Registry/`: `registry-host-factory.js` 가 registry 역할 서버를 구성한다.
- `Shared/`: Bingo card 계약을 공유한다.

## Success Condition

- API match handler 가 `.NET` sample 과 같이 Play channel 의 `AllocateBingoRoom`
  handler 로 request 를 보낸다.
- request payload 는 `mode` 만 포함한다.
- 같은 mode 의 두 allocation request 는 같은 room id 를 돌려준다.
- client 가 Server process 의 관찰 가능한 준비 신호를 받은 뒤 API channel request 를 보낸다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 실행 및 import 정책을 확인한다.
