# Bingo TypeScript Sample

TypeScript Client/Server/Shared 구조로 Bingo sample 을 실행하고 같은 TCP route/channel
메시징 경로를 검증한다. Node framework 의 NestJS sample 은 이 TypeScript sample 을
기준으로 제공한다.

## 실행

```bash
cd framework/languages/node/samples/Bingo.Ts
npm run build
npm run start
```

## Topology

- `Client/`: TypeScript self-check, client app, player client, notification inbox 구조를
  제공한다.
- `Server/Api/`: TypeScript API role entrypoint 와 handler 파일 구성을 제공한다.
- `Server/Session/`: TypeScript Session role entrypoint 와 session handler 구성을 제공한다.
- `Server/Play/`: TypeScript Play role entrypoint 와 actor, entry spot, room spot, handler
  파일 구성을 제공한다.
- `Server/Registry/`: TypeScript Registry role entrypoint 를 제공한다.
- `Shared/`: TypeScript sample names 와 message helper 구성을 제공한다.

## Success Condition

- TypeScript entrypoint 가 TypeScript Registry, Session, Play, API process 를 시작한다.
- four-player auth, match, start, timer, push fanout 결과를 검증한다.
- TypeScript 컴파일이 통과한 산출물을 Node 로 실행한다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 TypeScript 샘플 build/run 여부를
확인한다.
