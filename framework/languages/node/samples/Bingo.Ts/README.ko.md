# Bingo TypeScript Sample

TypeScript client entrypoint 로 Bingo JS 서버 process 를 시작하고 같은 TCP channel
메시징 경로를 검증한다. `Bingo/` 는 JavaScript 샘플이고, 이 디렉토리는 TypeScript
사용자가 보는 기본 샘플이다.

## 실행

```bash
cd framework/languages/node/samples/Bingo.Ts
npm run build
npm run start
```

## Topology

- `Client/self-check.ts`: TypeScript 로 작성된 self-check entrypoint 이다.
- `../Bingo/Server/*`: JavaScript Bingo 서버 process 를 재사용한다. 서버 구조와 handler
  group 등록은 JavaScript 샘플과 동일하다.
- `../Bingo/Client/bingo-client-app.js`: 현재 framework public client helper 를 통해
  four-player flow 를 실행한다.

## Success Condition

- TypeScript entrypoint 가 Registry, Session, Play, API process 를 시작한다.
- JavaScript Bingo 샘플과 같은 four-player auth, match, start, timer, push fanout 결과를
  검증한다.
- TypeScript 컴파일이 통과한 산출물을 Node 로 실행한다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 TypeScript 샘플 build/run 여부를
확인한다.
