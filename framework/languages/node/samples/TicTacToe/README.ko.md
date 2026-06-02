# TicTacToe Sample

두 player 가 같은 game Spot 에 join 하고 deterministic 승패를 만든다.

## 실행

```bash
node framework/languages/node/samples/TicTacToe/Client/self-check.js
```

## Topology

- `Client/`: API 서버와 Play 서버 process 를 시작하고, API 서버의 실제 TCP channel
  endpoint 로 `RunTicTacToe` request 를 보낸다.
- `Server/Api/`: client 요청을 받고 `ZLinkChannelClient` 로 Play 서버에 `CreateGame`
  request 를 보낸다.
- `Server/Play/`: channel handler 에서 deterministic game 을 실행하고 reply 를 돌려준다.
- `Shared/`: board 규칙과 message shape 를 공유한다.

## Success Condition

- 두 player 가 같은 game Spot 에 join 한다.
- `p1` 이 deterministic move 순서로 승리한다.
- client → api → play request/reply 가 실제 TCP channel endpoint 를 통과한다.
- game Spot 이 timer 를 등록한다.
- `PlayerJoinedNotify` 와 `GameStateNotify` push 결과가 deterministic 순서로 남는다.
- client 가 Server process 의 관찰 가능한 준비 신호를 받은 뒤 self-check 를 진행한다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 실행 및 import 정책을 확인한다.
