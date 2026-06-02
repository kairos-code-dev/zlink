# TicTacToe Sample

두 player 가 같은 game Spot 에 join 하고 deterministic 승패를 만든다.

## 실행

```bash
node framework/languages/node/samples/TicTacToe/client/self-check.js
```

## Topology

- client: channel client 로 match 요청을 보낸다.
- server: `ZLinkSpotManager` 로 game Spot 을 만들고 actor 를 join 한다.
- shared: board 규칙과 message shape 를 공유한다.

## Success Condition

- 두 player 가 같은 game Spot 에 join 한다.
- `p1` 이 deterministic move 순서로 승리한다.
- channel request 기록이 남는다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 실행 및 import 정책을 확인한다.
