# ZLink Framework for Node.js — Samples

이 디렉토리는 Node.js/NestJS 버전 framework 의 사용자 흐름을 self-check 로 검증한다.
각 sample 은 framework public API 또는 stream connector public API만 사용한다.

## 실행

```bash
./framework/languages/node/samples/run_samples.sh
```

## 포함된 sample

| Sample | 확인하는 흐름 |
|--------|---------------|
| `StreamingClient` | connector connect, request/reply, manual dispatch notification |
| `TicTacToe` | channel client, Spot, actor 기반 deterministic game |
| `TicTacToe.SessionGateway` | actor bound session push, reconnect token 갱신, stale guard 의미 |
| `Bingo` | room Spot, deterministic winner, timer-style tick, bound push |

## 성공 조건

모든 sample 은 성공하면 `PASS <SampleName>` 을 출력하고 0으로 종료한다.
실패하면 예외를 던져 `run_samples.sh` 전체가 실패한다.

## 회귀 테스트

sample 디렉토리, public API import guard, readiness guard 는
`test/contract/sample-regression.test.js` 에서 확인한다.
