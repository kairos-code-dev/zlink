# ZLink Framework for Node.js -- 문서

> 이 묶음은 `Node.js`, `NestJS`용 ZLink Framework 정식 문서다. 문서는
> `guide/`(사용법·케이스 스터디), `spec/`(공개 계약), `internals/`(구현·검증 기준)로
> 나뉜다. 공통 의미는 [공통 스펙](../../../doc/spec/README.ko.md)을 따르며, 여기서는
> 그 의미를 Node.js / NestJS 표면으로만 구체화한다. dotnet 문서와 표기가 어긋나면
> dotnet **코드**가 기능의 최종 기준이고, 그 번역 규칙은
> [표면 매핑 정책](./internals/dotnet-to-node-surface-mapping.ko.md)이 소유한다.

비동기 실행, `Promise`, helper 동기 함수의 공통 의미는
[비동기 실행과 coroutine 정책](../../../doc/spec/async-execution-policy.ko.md)을 따른다.
Node framework 의 서버와 client network API 는 `Promise` 기반 비동기 함수로 투영한다.
`Async` suffix 는 옮기지 않고, `connect()`, `close()`, `submit()`, `waitFor()`,
`start()`, `stop()`, `handle()` 처럼 동작 이름과 `Promise<T>` 반환 타입으로 비동기
계약을 드러낸다. codec 변환, packet name 계산, 값 객체 생성처럼 network I/O 를 하지
않는 순수 helper 는 동기 함수일 수 있다.

## 1. 사용자 guide

NestJS 애플리케이션 개발자가 읽고 바로 따라 쓸 수 있도록 기능과 사용법을 설명한다.
내부 backend adapter나 binding wrapper 구조는 guide에서 설명하지 않고, 필요하면
`internals/` 문서로 연결한다.

| 문서 | 범위 |
|------|------|
| [01-overview](./guide/01-overview.ko.md) | Node framework 개요 |
| [02-getting-started](./guide/02-getting-started.ko.md) | NestJS 등록과 provider 사용 |
| [03-concepts](./guide/03-concepts.ko.md) | channel, Spot, actor, stream 개념 |
| [04-channel-messaging](./guide/04-channel-messaging.ko.md) | request, send, publish 사용 |
| [05-spot](./guide/05-spot.ko.md) | Spot manager, outbound, timer |
| [06-actor-session](./guide/06-actor-session.ko.md) | actor bind, relay, bound session |
| [07-stream](./guide/07-stream.ko.md) | stream session 과 connector |
| [08-registry](./guide/08-registry.ko.md) | embedded registry 와 topology query |
| [09-monitoring](./guide/09-monitoring.ko.md) | typed runtime event 관찰 |
| [10-feature-map](./guide/10-feature-map.ko.md) | .NET 기능 이름의 Node 대응 |
| [11-interface-catalog](./guide/11-interface-catalog.ko.md) | 주요 public interface 목록 |
| [12-grpc-alternative](./guide/12-grpc-alternative.ko.md) | gRPC/HTTP 대비 도입 판단 + 케이스 스터디 |

도입 판단과 아키텍처 매핑을 위한 도메인별 케이스 스터디는
[12-grpc-alternative](./guide/12-grpc-alternative.ko.md)에서 진입하며, `guide/case-studies/`
13–18에 둔다.

## 2. 공개 계약 spec

NestJS 표면의 **정식 계약**이다. 현재 Node 코드와 regression test에 존재하는 public
API만 설명한다.

| 문서 | 범위 |
|------|------|
| [handler-interfaces](./spec/handler-interfaces.ko.md) | 모든 interface·decorator·context·options 카탈로그 |
| [nestjs-overview](./spec/nestjs-overview.ko.md) | module bootstrap, DI, lifecycle, backend 어댑터 |
| [nestjs-channel-messaging](./spec/nestjs-channel-messaging.ko.md) | channel 등록, outbound client, dispatch, filter |
| [nestjs-spot](./spec/nestjs-spot.ko.md) | SPOT lifecycle, publish/subscribe, channel attach |
| [nestjs-actor](./spec/nestjs-actor.ko.md) | actor factory, Entry Spot, bound session |
| [nestjs-stream](./spec/nestjs-stream.ko.md) | header session, single `onDispatch`, registration, lifecycle |
| [nestjs-registry](./spec/nestjs-registry.ko.md) | registry startup, query, topology |
| [nestjs-monitoring](./spec/nestjs-monitoring.ko.md) | runtime 이벤트 등록, typed event |
| [session-actor-dispatch](./spec/session-actor-dispatch.ko.md) | session → actor relay dispatch 정식 정의 |
| [spot-node](./spec/spot-node.ko.md) | SpotNode 등록·관리 표면 |
| [stage-wrapper-on-spot](./spec/stage-wrapper-on-spot.ko.md) | stage 상위 모델을 SPOT 위에 감싸는 조건 |

## 3. 내부 기준 (`internals/`)

`internals/`는 유지보수자를 위한 framework 경계, backend 의존, lifecycle, 회귀
기준을 정의한다. spec 만으로 못 정하는 **횡단 결정**에서 참조한다. 이식 기준의
키스톤은 `dotnet-to-node-surface-mapping`이다.

| 문서 | 범위 |
|------|------|
| [dotnet-to-node-surface-mapping](./internals/dotnet-to-node-surface-mapping.ko.md) | **이식 기준**(번역 규칙) |
| [backend-dependency-policy](./internals/backend-dependency-policy.ko.md) | backend 교체 가능성, public surface 격리 |
| [di-capability-exposure-policy](./internals/di-capability-exposure-policy.ko.md) | 역할 별 DI 노출 규칙 |
| [lifecycle-and-failure-semantics](./internals/lifecycle-and-failure-semantics.ko.md) | 시동/종료/실패 의미 |
| [behavior-matrix](./internals/behavior-matrix.ko.md) | 기능별 동작 매트릭스 |
| [implementation-scope-and-nongoals](./internals/implementation-scope-and-nongoals.ko.md) | 범위·비목표 |
| [regression-test-matrix](./internals/regression-test-matrix.ko.md) | 회귀 테스트 기준 |
| [cross-language-smoke](./internals/cross-language-smoke.ko.md) | cross-language wire 계약 smoke 기준 |

## 4. 정본 샘플 (`guide/samples/`)

정본 샘플의 per-app 문서는 `guide/samples/`에 둔다(코드: `samples/*.Ts`). Bingo와
TicTacToe를 제외한 정본 샘플(SupportChat, DeliveryDispatch, ShoppingMallCheckout,
GameQuest)은 JSON codec, Registry/Discovery 자동 연결, NestJS provider scan 기반
handler 자동 등록을 사용한다. 이 기준은
[공통 샘플 포팅 기준](../../../doc/spec/sample/README.ko.md#샘플-포팅-기준)을 따른다.

| 문서 | 범위 |
|------|------|
| [bingo-game-sample](./guide/samples/bingo-game-sample.ko.md) | Session/Api/Play/Registry, Entry Spot, room Spot, timer, bound push |
| [tictactoe-game-sample](./guide/samples/tictactoe-game-sample.ko.md) | Api/Play 두 서버, 수동 연결, typed session dispatch |
| [supportchat-sample](./guide/samples/supportchat-sample.ko.md) | Session/Api/Support/Registry, conversation Spot, idle/close timer, reconnect |
| [deliverydispatch-sample](./guide/samples/deliverydispatch-sample.ko.md) | channel + fanout, 재배정 timer, 고객 stream push |
| [shoppingmall-checkout-sample](./guide/samples/shoppingmall-checkout-sample.ko.md) | event-sourcing, idempotency, 보상, projection rebuild, scale-out |
| [gamequest-sample](./guide/samples/gamequest-sample.ko.md) | stateless API scale-out, PlayerId owner routing, fanout + event sourcing |

> 위 정본 샘플은 모두 `samples/*.Ts` TypeScript 실행 코드로 self-check 한다(`Bingo.Ts`,
> `TicTacToe.Ts`, `SupportChat.Ts`, `DeliveryDispatch.Ts`, `ShoppingMallCheckout.Ts`,
> `GameQuest.Ts`). 각 시나리오의 서버 역할·메시지 이름·smoke 순서 정본은 공통 샘플 spec이
> 소유하고, 위 문서는 그 정본을 Node/NestJS 표면으로 구체화한다.

## 5. 회귀 테스트

이 README 는 아래 문서 회귀 테스트와 함께 유지한다.

| 테스트 | 확인 기준 |
|--------|-----------|
| `documentation-regression.test.js › node README links every guide chapter` | `README.ko.md`가 12개 guide 장을 모두 연결한다. |
| `documentation-regression.test.js › node documentation relative markdown links resolve` | 문서 간 상대 링크가 깨지지 않는다. |
| `documentation-regression.test.js › node guide exposes the 12 required guide chapters` | guide 01–12가 제목과 회귀 테스트 섹션을 갖춘다. |
| `sample-regression.test.js › node samples define the required sample directories and README files` | 정본 샘플 디렉토리와 README 가 README 에서 연결된다. |
