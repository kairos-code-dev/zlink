# ZLink Framework for Node.js — Samples

이 디렉토리는 Node.js/NestJS 버전 framework 의 사용자 흐름을 self-check 로 검증한다.
각 sample 은 framework public API 또는 stream connector public API만 사용한다.
NestJS framework sample 은 TypeScript 를 기준으로 제공한다. NestJS 의 decorator,
metadata, DI 사용 방식은 TypeScript 프로젝트에서 가장 자연스럽게 드러나기 때문에
JavaScript NestJS sample 을 별도로 유지하지 않는다. client self-check 는 서버 process
를 시작한 뒤 실제 TCP endpoint 로 channel 또는 route request 를 보내며,
stdin/stdout command protocol 로 application message 를 주고받지 않는다.

## 실행

```bash
./framework/languages/node/samples/run_samples.sh
```

## 포함된 sample

| Sample | 확인하는 흐름 |
|--------|---------------|
| `StreamingClient` | connector connect, request/reply, manual dispatch notification |
| `Bingo.Ts` | NestJS DI, channel client/server, Spot, actor, session relay, bound push |

## 성공 조건

모든 sample 은 성공하면 `PASS <SampleName>` 을 출력하고 0으로 종료한다.
실패하면 예외를 던져 `run_samples.sh` 전체가 실패한다.

## 회귀 테스트

sample 디렉토리, public API import guard, readiness guard 는
`test/contract/sample-regression.test.js` 에서 확인한다.
