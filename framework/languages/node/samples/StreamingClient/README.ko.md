# StreamingClient Sample

Stream connector 단독 사용 흐름을 확인한다.

## 실행

```bash
node framework/languages/node/samples/StreamingClient/client/self-check.js
```

## Topology

- `server/main.js`: TCP stream endpoint 를 열고 connector request frame 에 response
  frame 과 notification frame 을 돌려준다.
- `client/self-check.js`: public stream connector 로 server TCP endpoint 에 접속하고
  request/reply 와 manual notification dispatch 를 확인한다.

## Success Condition

- connector 가 connected 상태로 전환된다.
- request frame 에 대해 response frame 이 도착한다.
- manual dispatch 로 notification handler 가 실행된다.
- client 와 server 사이의 payload 는 실제 TCP stream frame 으로 이동한다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 실행 및 import 정책을 확인한다.
