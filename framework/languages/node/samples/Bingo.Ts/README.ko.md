# Bingo TypeScript Sample

TypeScript Client/Server/Shared 구조로 Bingo sample 을 실행하고 Session stream,
API channel, Play actor/Spot, server timer push 흐름을 검증한다. Node framework 의
NestJS Bingo sample 은 이 TypeScript sample 을 기준으로 제공한다.

## 실행

Linux 또는 WSL:

```bash
cd framework/languages/node/samples/Bingo.Ts
./run_sample.sh
```

Windows PowerShell:

```powershell
cd framework\languages\node\samples\Bingo.Ts
.\run_sample.ps1
```

`BINGO_REDIS_ENDPOINT`가 설정되어 있지 않으면 runner가 Redis Docker container를
Docker가 배정한 loopback port로 띄우고, self-check가 끝나면 정상/실패와 관계없이
container를 정리한다. 외부 Redis를 쓰려면 `BINGO_REDIS_ENDPOINT`를 지정한다. runner는 실행마다
고유한 `BINGO_REDIS_KEY_PREFIX`도 전달하므로 같은 Redis를 쓰는 다른 테스트의 match queue
key와 섞이지 않는다.

## Topology

- `run_sample.sh`, `run_sample.ps1`: Registry, Play, API, Session 서버 process 를 시작하고 readiness 를
  확인한 뒤 client self-check 를 실행한다.
- `Client/`: 두 client 를 만들고 각 client 가 Session stream 연결 하나만 유지한다.
  요청 직후 response 를 확인하고, push 순서가 중요한 곳은 notify promise 를 먼저 만든다.
  Client 코드는 서버 process 를 직접 시작하지 않는다.
- `Server/Api/`: API channel 과 matching handler 를 제공한다. Play channel client 는
  내장 Registry/Discovery 연결로 peer 를 찾는다.
- `Server/Session/`: stream endpoint 를 열고 인증 후 현재 session 을 Play actor 에 bind한다.
  API/Play/notification channel client 는 내장 Registry/Discovery 연결로 peer 를 찾고,
  client gameplay packet 은 bound actor 로 relay한다.
- `Server/Play/Domain/Bingo/`: 3 x 3 card 검증, draw order, winner 판정을 맡는 순수 게임
  규칙을 둔다. ZLink framework 타입을 참조하지 않는다.
- `Server/Play/Application/RoomAllocation/`: matching 요청을 room 배정 use case 로 처리한다.
- `Server/Play/Adapters/ZLink/`: actor, Entry Spot, room Spot, handler, notification publisher,
  bound session 연결을 맡는다.
- `Server/Registry/`: 내장 ZLink Registry host 를 실행한다. Session 과 API 는 peer
  endpoint 를 직접 들고 있지 않고 Registry/Discovery 를 통해 연결 대상을 찾는다.
- `Server/Configuration/`: 서버 role 의 channel 이름, 서비스 이름, timeout, 실행 endpoint 설정을 둔다.
- `Client/Configuration/`: client self-check 실행 설정을 둔다.
- `Client/bingo-client-scenario.ts`: 인증, matching, card 제출, draw push, final state 검증을
  순서대로 표현하는 client scenario 를 둔다.
- `Shared/Contracts/`: client 와 server 가 함께 쓰는 Protobuf message DTO, `.proto`, codec 만 둔다.

샘플 코드는 framework runtime 내부 파일을 직접 import하지 않는다. NestJS provider token,
framework builder, channel client, stream connector처럼 공개된 API만 사용한다.

## Success Condition

- runner 가 TypeScript Registry, Play, API, Session process 를 시작한다.
- Play 와 API 는 각 channel server endpoint 를 bind하고, Session 과 API 는
  Registry/Discovery 로 필요한 peer endpoint 를 찾는다.
- 두 client 가 Session stream endpoint 에만 연결한다.
- 첫 match 는 waiting room 을 만들고, 두 번째 match 는 같은 room 에 join해 자동으로 game 을 시작한다.
- client 는 `SubmitBingoCardReq` 로 3 x 3 card 를 제출하고 draw request 를 보내지 않는다.
- Play room Spot timer 가 번호를 뽑고 `BingoNumberDrawnNotify`, `BingoGameEndedNotify` 를 보낸다.
- game 이 끝나면 room Spot 은 actor 에 정리 의도를 표시하고 `leaveActor` 로 Entry Spot 에
  돌려보낸다.
- Entry Spot 은 actor 가 다시 들어온 뒤 `destroyActor` 를 호출한다. destroy 는
  `onLeaveActor` 를 추가로 호출하지 않는다.
- `onDisconnectActor` 는 연결 끊김 상태만 표시한다. disconnect 는 room leave 나 actor destroy 로
  처리하지 않는다.
- TypeScript 컴파일이 통과한 산출물을 Node 로 실행한다.

## 회귀 테스트

`run_samples.sh`, `run_samples.ps1`, `sample-regression.test.js` 에서 TypeScript 샘플 build/run 여부를
확인한다.
