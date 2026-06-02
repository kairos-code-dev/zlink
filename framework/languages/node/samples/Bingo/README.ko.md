# Bingo Sample

four-player matching, room Spot, host start, timer draw, bound push 를 하나의
deterministic 시나리오로 검증한다.

## 실행

```bash
node framework/languages/node/samples/Bingo/client/self-check.js
```

## Topology

- client: api server process 를 시작하고 `ready` 이벤트를 기다린 뒤 Bingo scenario command 를 보낸다.
- api-server: player matching 요청을 받는 역할. self-check 에서는 play/session 역할을 조립한 server role process 로 실행된다.
- play-server: Bingo room Spot 을 호스팅하는 역할
- session-server: `ZLinkStreamBindingRuntime` 으로 actor bound session push 를 client 로 전달하는 역할
- registry-server: 실제 배포에서는 topology 를 제공한다

## Success Condition

- 네 player 가 서로 다른 actor 로 session 에 bind 되고 같은 room 에 match 된다.
- 첫 참가자인 `p1` 이 host 가 된다.
- 네 명이 들어오기 전 host start 요청과 non-host start 요청은 거부된다.
- host start 뒤 room timer 가 draw tick 을 발생시킨다.
- 같은 draw sequence 에서 `p1`, `p3` 이 deterministic winners 가 된다.
- started, drawn, ended notification 이 모든 bound session 으로 전달된다.
- client 가 server process 의 관찰 가능한 준비 신호를 받은 뒤 self-check 를 진행한다.
- sample 이 별도 notification 저장소를 만들지 않고 framework bound session runtime 의
  transport 경로로 전송 결과를 관찰한다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 실행 및 import 정책을 확인한다.
