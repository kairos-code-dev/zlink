# TicTacToe.SessionGateway Sample

stream session gateway 를 통해 actor 가 client 로 bound session push 를 보내는 흐름을
검증한다.

## 실행

```bash
node framework/languages/node/samples/TicTacToe.SessionGateway/client/self-check.js
```

## Topology

- session-server: client stream session 을 받는 역할
- play-server: actor 와 game Spot 을 호스팅하는 역할
- api-server: match 시작 요청을 받는 역할
- registry-server: 실제 배포에서는 topology 를 제공한다

이 self-check 는 현재 public framework API 위에서 actor bound session 의미와 reconnect
token 갱신을 deterministic 하게 검증한다.

## Success Condition

- 같은 actor id 가 reconnect 뒤에도 같은 actor 인스턴스를 유지한다.
- 새 binding token 으로 bound session push 가 도착한다.
- 이전 stale token push 는 새 session 을 지우거나 사용하지 못한다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 실행 및 import 정책을 확인한다.
