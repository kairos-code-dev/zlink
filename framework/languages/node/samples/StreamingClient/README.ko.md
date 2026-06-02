# StreamingClient Sample

Stream connector 단독 사용 흐름을 확인한다.

## 실행

```bash
node framework/languages/node/samples/StreamingClient/src/self-check.js
```

## Topology

이 self-check 는 public connector transport factory 를 사용해 in-memory connection 을
연결한다. 실제 network endpoint 대신 connector 가 쓰는 frame 계약을 검증한다.

## Success Condition

- connector 가 connected 상태로 전환된다.
- request frame 에 대해 response frame 이 도착한다.
- manual dispatch 로 notification handler 가 실행된다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 실행 및 import 정책을 확인한다.
