# TicTacToe Sample

두 player 가 같은 game Spot 에 join 하고 deterministic 승패를 만든다.

## 실행

```bash
node framework/languages/node/samples/TicTacToe/client/self-check.js
```

## Topology

- client: api server 와 play server process 를 시작하고 `ready` 이벤트를 기다린 뒤 game command 를 보낸다.
- api-server: self-check 에서 인증/API 역할의 준비 상태를 노출한다.
- play-server: `ZLinkModule.forRoot(...)` 로 provider 를 만들고 `ZLinkSpotManager` 로 game Spot 을 만든다.
- shared: board 규칙과 message shape 를 공유한다.

## Success Condition

- 두 player 가 같은 game Spot 에 join 한다.
- `p1` 이 deterministic move 순서로 승리한다.
- channel request 기록이 남는다.
- game Spot 이 timer 를 등록한다.
- `PlayerJoinedNotify` 와 `GameStateNotify` push 결과가 deterministic 순서로 남는다.
- client 가 server process 의 관찰 가능한 준비 신호를 받은 뒤 self-check 를 진행한다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 실행 및 import 정책을 확인한다.
