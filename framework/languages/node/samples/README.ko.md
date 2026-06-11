# ZLink Framework for Node.js — Samples

이 디렉토리는 Node.js/NestJS 버전 framework 의 사용자 흐름을 self-check 로 검증한다.
각 sample 은 framework public API 또는 stream connector public API만 사용한다.
샘플 애플리케이션 코드는 `@zlink-systems/framework`, `@zlink-systems/nestjs`,
`@zlink-systems/stream-connector`가 공개한 entrypoint와 token만 사용한다.
`packages/framework/dist/internal`, `packages/framework/dist/runtime/*` 같은 runtime 구현
경로를 샘플에서 직접 import하지 않는다. public contract에 없는 기능이 필요하면 샘플
우회 코드가 아니라 framework API를 먼저 정리한다.
`@zlink-systems/framework` package는 root entrypoint만 export한다. NestJS adapter가
runtime 구현을 내부적으로 사용하는 것은 adapter 구현 세부이며, sample이나 application
코드가 따라 쓰는 경로가 아니다.
NestJS framework sample 은 TypeScript 를 기준으로 제공한다. NestJS 의 decorator,
metadata, DI 사용 방식은 TypeScript 프로젝트에서 가장 자연스럽게 드러나기 때문에
JavaScript NestJS sample 을 별도로 유지하지 않는다.

샘플 실행은 각 샘플 루트의 `run_sample.sh` 또는 `run_sample.ps1` 이 맡는다. 이 스크립트가 서버 역할을
별도 process 로 시작하고 readiness 를 확인한 뒤 client self-check 를 실행한다. Client
코드는 이미 실행 중인 endpoint 로 사용자 흐름만 검증하며 서버 process 를 직접 시작하지
않는다. 이렇게 나누면 샘플 코드는 실제 애플리케이션 역할에 집중하고, 테스트용 process
정리는 runner 한 곳에서 볼 수 있다.

Client에서 request, push, final state를 검증하는 DSL 형태의 흐름은
`<sample>-client-scenario.ts` 파일과 `<Sample>ClientScenario` class에 둔다. `ClientApp`,
`self-check`, `TestScenario` 같은 이름은 샘플 실행 흐름의 책임을 흐리므로 쓰지 않는다.

## 실행

Linux 또는 WSL 에서는 shell runner 를 실행한다.

```bash
./framework/languages/node/samples/run_samples.sh
```

Windows PowerShell 에서는 PowerShell runner 를 실행한다.

```powershell
.\framework\languages\node\samples\run_samples.ps1
```

개별 샘플만 실행할 때는 샘플 루트의 runner 를 호출한다.

```bash
./framework/languages/node/samples/TicTacToe.Ts/run_sample.sh
./framework/languages/node/samples/Bingo.Ts/run_sample.sh
```

```powershell
.\framework\languages\node\samples\TicTacToe.Ts\run_sample.ps1
.\framework\languages\node\samples\Bingo.Ts\run_sample.ps1
```

## Configuration

Node 샘플은 NestJS module 구성을 기준으로 설정을 주입한다. 서버 role 은 endpoint 값을
직접 만들지 않고, runner 가 준비한 실행 설정을 NestJS provider 또는
`ZLinkModule.forRootFactory(...)`에서 읽어 ZLink framework 옵션으로 넘긴다.

샘플 runner 는 테스트 실행에 필요한 포트를 고르고 서버 process 를 시작한다. 서버
코드는 자기 role 의 NestJS application context 만 만들며 다른 서버나 client 를 직접
시작하지 않는다.

서버 설정 파일과 설정 loader 는 각 `Server/Configuration` 또는 `Client/Configuration`
아래에 둔다. `Shared` 아래에는 여러 role 이 함께 쓰는 message DTO, protobuf, codec 같은
통신 계약만 둔다.

`TicTacToe.Ts` 는 stream payload 와 샘플 내부 message 계약에 MessagePack payload 를
사용한다. `Bingo.Ts` 는 Protobuf payload 를 사용한다.

## 포함된 sample

| Sample | 확인하는 흐름 |
|--------|---------------|
| `TicTacToe.Ts` | 기본 NestJS channel, HTTP API, stream connector, actor game 흐름 |
| `Bingo.Ts` | NestJS DI, channel client/server, Spot, actor, session relay, bound push |

## 성공 조건

모든 sample 은 성공하면 `PASS <SampleName>` 을 출력하고 0으로 종료한다. 실패하면
client self-check 또는 runner 의 readiness 검사가 실패하고 runner 전체가 실패한다.

## 회귀 테스트

sample 디렉토리, public API import guard, readiness guard 는
`test/contract/sample-regression.test.js` 에서 확인한다.
