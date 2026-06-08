# Bingo TypeScript Sample

TypeScript Client/Server/Shared 구조로 Bingo sample 을 실행하고 Session stream,
API channel, Play actor/Spot, server timer push 흐름을 검증한다. Node framework 의
NestJS Bingo sample 은 이 TypeScript sample 을 기준으로 제공한다.

## 실행

```bash
cd framework/languages/node/samples/Bingo.Ts
npm run build
npm run start
```

## Topology

- `Client/`: 두 client 를 만들고 각 client 가 Session stream 연결 하나만 유지한다.
  요청 직후 response 를 확인하고, push 순서가 중요한 곳은 notify promise 를 먼저 만든다.
- `Server/Api/`: TypeScript API role entrypoint 와 handler 파일 구성을 제공한다.
- `Server/Session/`: stream endpoint 를 열고 인증 후 현재 session 을 Play actor 에 bind한다.
  client gameplay packet 은 bound actor 로 relay한다.
- `Server/Play/Domain/Bingo/`: 3 x 3 card 검증, draw order, winner 판정을 맡는 순수 게임
  규칙을 둔다. ZLink framework 타입을 참조하지 않는다.
- `Server/Play/Application/RoomAllocation/`: matching 요청을 room 배정 use case 로 처리한다.
- `Server/Play/Adapters/ZLink/`: actor, Entry Spot, room Spot, handler, notification publisher,
  bound session 연결을 맡는다.
- `Server/Registry/`: TypeScript Registry role entrypoint 를 제공한다.
- `Shared/`: TypeScript sample names 와 message helper 구성을 제공한다.

## Success Condition

- TypeScript entrypoint 가 TypeScript Registry, Session, Play, API process 를 시작한다.
- 두 client 가 Session stream endpoint 에만 연결한다.
- 첫 match 는 waiting room 을 만들고, 두 번째 match 는 같은 room 에 join해 자동으로 game 을 시작한다.
- client 는 `SubmitBingoCardReq` 로 3 x 3 card 를 제출하고 draw request 를 보내지 않는다.
- Play room Spot timer 가 번호를 뽑고 `BingoNumberDrawnNotify`, `BingoGameEndedNotify` 를 보낸다.
- TypeScript 컴파일이 통과한 산출물을 Node 로 실행한다.

## 회귀 테스트

`run_samples.sh` 와 `sample-regression.test.js` 에서 TypeScript 샘플 build/run 여부를
확인한다.
