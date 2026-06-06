# ZLink Framework Node.js Sample And Guide Implementation Plan

> 이 문서는 구현 전 실행 계획 초안이다. 현재 공개 계약이 아니며,
> [구현 작업 Plan](./IMPLEMENTATION-PLAN.ko.md)의 Phase 9가 요구하는
> 사용자 guide 와 sample 동등성 기준을 구체화한다.
>
> 기준은 `framework/languages/dotnet/samples`, `framework/languages/dotnet/doc/guide`,
> 그리고 `framework/languages/dotnet/src` 코드다. 표면은 NestJS / TypeScript 로
> 옮기지만 시나리오, runtime 의미, 실패 판정은 dotnet 과 같아야 한다.

## 1. 목적

Phase 9는 regression green 만으로 끝나지 않는다. 사용자가 dotnet framework 와 같은
멘탈 모델로 Node framework 를 사용할 수 있어야 하고, dotnet sample 과 같은 제품
시나리오가 NestJS sample 로 실행되어야 한다. NestJS sample 은 TypeScript 를 기준으로
제공한다. NestJS 의 decorator, metadata, DI 사용 방식이 TypeScript 에서 가장 분명하게
드러나기 때문에 JavaScript NestJS sample 은 별도로 유지하지 않는다.

이 문서는 아래 네 가지를 고정한다.

- 어떤 guide 장을 작성해야 하는지
- 어떤 sample app 을 만들어야 하는지
- sample 이 어떤 명령으로 self-check 를 통과해야 하는지
- cross-language smoke 가 어떤 경로를 반드시 검증해야 하는지

## 2. Guide 산출물

Node guide 는 `framework/languages/node/doc/guide/` 아래에 둔다. 구현된 public API와
regression test가 닫힌 뒤 작성하며, 정식 spec 에 없는 API를 guide에 먼저 소개하지
않는다.

| Node guide | dotnet 대응 | 포함해야 하는 사용자 질문 |
|------------|-------------|---------------------------|
| `01-overview.ko.md` | `doc/guide/01-*` | Node framework 가 무엇을 해 주는가 |
| `02-getting-started.ko.md` | getting started 장 | NestJS app 에 어떻게 붙이는가 |
| `03-concepts.ko.md` | concepts 장 | channel, Spot, actor, stream 의 차이 |
| `04-feature-map.ko.md` | feature map 장 | dotnet 기능이 Node 에서 어떤 이름인지 |
| `05-channel-messaging.ko.md` | channel messaging 장 | request/send/publish 를 언제 쓰는가 |
| `06-spot.ko.md` | Spot 장 | SpotNode, Entry Spot, user Spot 을 어떻게 등록하는가 |
| `07-actor-session.ko.md` | actor/session guide | actor bind, session relay, bound session push |
| `08-stream.ko.md` | stream guide | stream node, header session, connector 사용 |
| `09-registry.ko.md` | registry guide | embedded registry 와 query 사용 |
| `10-monitoring.ko.md` | monitoring guide | runtime event 를 어떻게 받는가 |
| `11-interface-catalog.ko.md` | API catalog | 주요 public interface와 decorator 찾기 |
| `12-cross-language.ko.md` | language interop guide | dotnet/C++/Java 와 같은 wire 계약으로 붙는 법 |

Guide 작성 규칙:

- guide 는 사용법과 의도를 설명한다. backend adapter, native socket, inproc endpoint
  같은 내부 구현 설명은 internals 문서로 링크한다.
- guide 예제는 실제 sample 또는 contract test 에서 compile 되는 코드만 사용한다.
- public 이름이 바뀌면 spec, guide, sample README 를 같은 변경으로 갱신한다.

## 3. Sample 산출물

Node sample 은 `framework/languages/node/samples/` 아래에 둔다.

| Sample | dotnet 대응 | 검증하는 기능 |
|--------|-------------|---------------|
| `StreamingClient` | stream connector sample | connector 단독 connect/send/request/manual dispatch/reconnect |
| `Bingo.Ts` | Bingo sample | NestJS DI, channel, Spot, actor, stream session, bound push |

권장 디렉토리:

```text
samples/
|-- StreamingClient/
|   |-- Client/
|   |-- Server/
|   |-- README.ko.md
|   `-- package.json
|-- Bingo.Ts/
|   |-- Client/
|   |-- Server/
|   |   |-- Api/
|   |   |-- Play/
|   |   |-- Registry/
|   |   `-- Session/
|   |-- Shared/
|   |-- README.ko.md
|   `-- package.json
`-- run_samples.sh
```

다이어그램의 이름은 실제 디렉토리 역할만 나타낸다. dotnet sample 과 같은 역할을
제공하기 위해 샘플 최상위 역할 디렉토리는 dotnet sample 과 같은 `Client`, `Server`,
`Shared` 형태를 사용한다. 파일명과 함수명은 Node.js 관례를 따른다. NestJS sample 은
TypeScript 로 작성하고, framework public API와 stream connector public API만 사용해야
한다.

## 4. Sample Self-Check 기준

`samples/run_samples.sh` 는 모든 sample 을 순서대로 실행하고, 각 sample 이 직접
성공 조건을 출력하거나 종료 코드로 실패를 알려야 한다.

필수 확인:

- sample 이 framework/connector public API만 import 한다.
- readiness 를 숨기기 위해 sleep 만 추가하지 않는다. registry query, connector
  connection ready event, framework lifecycle signal 같은 관찰 가능한 상태를 기다린다.
- NestJS sample 에 application route store, metadata store, actor-session fallback
  resolver 가 없다.
- StreamingClient 는 manual dispatch mode 에서 request/reply 와 notification 을
  모두 처리한다.
- Bingo.Ts 는 deterministic scenario 에서 four-player auth, match, start, timer,
  bound push fanout 이 동작한다.

권장 명령:

```bash
./framework/languages/node/samples/run_samples.sh
```

## 5. Cross-Language Smoke 기준

cross-language smoke 는 Node framework 가 언어 중립 wire 계약을 지키는지 확인한다.
최소 경로는 다음 여섯 가지다.

| 경로 | 확인 기준 |
|------|-----------|
| Node client -> dotnet channel server request/reply | dotnet request handler 가 같은 payload 의미로 reply 한다 |
| Node client -> dotnet channel server one-way send | dotnet send handler 가 같은 packet 의미로 처리한다 |
| Node publisher -> dotnet fanout subscriber publish | dotnet publish handler 가 같은 topic/payload 의미로 처리한다 |
| dotnet client -> Node channel server | Node handler 가 dotnet client 요청에 같은 payload 의미로 reply 한다 |
| Node stream connector -> dotnet stream server | header session request/reply 와 notification dispatch 가 동작한다 |
| dotnet stream connector -> Node stream server | Node session `onDispatch` 와 `reply` 가 같은 header/payload 계약으로 동작한다 |

가능하면 추가로 아래 경로를 포함한다.

- Node TypeScript session sample -> dotnet Play server ActorGateway relay
- dotnet/C++/Java client -> Node Bingo bound session push 수신

cross-language smoke 는 sample smoke 와 별도로 둔다. sample 은 사용자 경험을, 이 smoke
는 wire 계약을 검증한다.

## 6. Phase 9 작업 순서

1. P0~P8 public API와 regression matrix를 green 으로 닫는다.
2. `guide/` 장 목차를 만들고, dotnet guide 장과 1:1 매핑표를 작성한다.
3. `StreamingClient` sample 을 먼저 구현해 connector 단독 사용성을 검증한다.
4. `Bingo.Ts` sample 을 구현해 NestJS DI, channel, Spot, actor, stream session,
   bound push를 고정한다.
5. `run_samples.sh` 를 CI release gate 에 연결한다.
6. `npm run verify:cross-language` 를 CI release gate 에 연결한다.
7. guide/spec/internals/sample README 링크 회귀 테스트를 실행한다.

## 7. 완료 기준

아래가 모두 충족되어야 Phase 9를 완료로 본다.

- `framework/languages/node/doc/guide/` 의 12개 장이 모두 존재한다.
- 모든 guide 예제는 실제 sample 또는 test에서 compile 된다.
- `StreamingClient`, `Bingo.Ts` sample 이 `run_samples.sh`에서 self-check 를 통과한다.
- `npm run verify:cross-language` 로 cross-language smoke 여섯 가지 필수 경로가
  통과한다.
- sample README 가 실행 명령, topology, success condition 을 설명한다.
- sample 과 guide 는 framework public API와 connector public API만 사용한다.
- 문서 링크 회귀 테스트가 guide/spec/internals/sample README 전체를 확인한다.

## 8. 회귀 테스트

이 문서는 아래 회귀 테스트와 함께 유지한다.

| 테스트 | 확인 기준 |
|--------|-----------|
| `sample-regression.test.js › node samples define the required sample directories and README files` | StreamingClient, Bingo.Ts, run_samples.sh 가 존재한다. |
| `sample-regression.test.js › node cross-language smoke covers channel send publish and stream connector paths` | Node↔dotnet channel request/send/publish, Node connector→dotnet stream, dotnet connector→Node stream 경로가 명시되어 있다. |
| `documentation-regression.test.js › node guide exposes the 12 required guide chapters` | guide 12개 장이 빠지지 않는다. |
