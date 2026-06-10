# ZLink Framework for Node.js — 문서

> 이 문서 묶음은 `ZLink Framework` 의 **Node.js / NestJS 버전**을 정의한다.
> 기준은 `framework/languages/dotnet` 의 정식 계약과 **소스 코드**다. 목표는
> 하나다 — **이 문서대로 구현하면 .NET 버전과 동일한 사용성·기능·구조를 가진
> Node.js framework 가 나온다.**
>
> 개념·의미론·동작은 dotnet 과 동일하고, 표면만 NestJS / TypeScript 로 옮긴다.
> 그 번역 규칙은 [표면 매핑 정책](./internals/dotnet-to-node-surface-mapping.ko.md)
> 이 소유한다. dotnet 문서와 표기가 어긋나면 dotnet **코드**가 기능의 최종
> 기준이다.
>
> 이 묶음은 **구현 기준 문서**다. 사용자 가이드(usability), 정식 계약(spec),
> 내부 정책(internals), 샘플 실행 기준을 분리해서 다룬다.

## 비동기 API 네이밍 투영

공통 실행 의미는
[비동기 실행과 coroutine 정책](../../../doc/spec/async-execution-policy.ko.md)을 따른다.
Node framework 의 서버와 client network API 는 `Promise` 기반 비동기 함수로 투영한다.
`Async` suffix 는 옮기지 않고, `connect()`, `close()`, `submit()`, `waitFor()`,
`start()`, `stop()`, `handle()` 처럼 동작 이름과 `Promise<T>` 반환 타입으로
비동기 계약을 드러낸다.

codec 변환, packet name 계산, 값 객체 생성처럼 network I/O 를 하지 않는 순수 helper 는
동기 함수일 수 있다.

## 0. 먼저 읽어야 하는 문서

- [**구현 작업 Plan**](./IMPLEMENTATION-PLAN.ko.md) — 참조 파일·작업 순서·코드
  검증 결정. **구현은 여기서 시작한다.**
- [끝까지 구현하는 실행 프롬프트](./IMPLEMENTATION-PROMPT.ko.md) — 새 작업 세션에
  그대로 전달할 수 있는 구현 완료 지시문
- [Sample and Guide Implementation Plan](./sample-implementation-plan.ko.md) —
  Phase 9 의 사용자 guide, sample, cross-language smoke 완료 기준
- [.NET → Node.js 표면 매핑 정책](./internals/dotnet-to-node-surface-mapping.ko.md)
  — 모든 문서가 따르는 번역 규칙(호스트/언어/백엔드 매핑)
- [기존 드래프트](./draft/README.ko.md) — 초기 표면 설계 기록. 구현 기준은 `spec/`,
  `internals/`, `guide/`, `sample-implementation-plan` 이다.

## 1. 정식 계약 (`spec/`)

NestJS 표면의 **정식 계약**이다. 구현은 이 문서를 직접 보면서 한다.

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

## 1.5 사용자 가이드 (`guide/`)

NestJS 애플리케이션 개발자를 위한 사용법 문서다. 내부 구현 설명은 넣지 않고,
public API를 어떤 상황에서 어떻게 쓰는지 설명한다.

| 문서 | 범위 |
|------|------|
| [01-overview](./guide/01-overview.ko.md) | Node framework 개요 |
| [02-getting-started](./guide/02-getting-started.ko.md) | NestJS 등록과 provider 사용 |
| [03-concepts](./guide/03-concepts.ko.md) | channel, Spot, actor, stream 개념 |
| [04-feature-map](./guide/04-feature-map.ko.md) | .NET 기능 이름의 Node 대응 |
| [05-channel-messaging](./guide/05-channel-messaging.ko.md) | request, send, publish 사용 |
| [06-spot](./guide/06-spot.ko.md) | Spot manager, outbound, timer |
| [07-actor-session](./guide/07-actor-session.ko.md) | actor bind, relay, bound session |
| [08-stream](./guide/08-stream.ko.md) | stream session 과 connector |
| [09-registry](./guide/09-registry.ko.md) | embedded registry 와 topology query |
| [10-monitoring](./guide/10-monitoring.ko.md) | typed runtime event 관찰 |
| [11-interface-catalog](./guide/11-interface-catalog.ko.md) | 주요 public interface 목록 |
| [12-cross-language](./guide/12-cross-language.ko.md) | cross-language smoke 기준 |

## 2. 내부 정책 (`internals/`)

framework 경계, backend 의존, lifecycle, 회귀 기준을 정의한다. spec 만으로
못 정하는 **횡단 결정**에서 참조한다. 이 중 구현 전에 반드시 읽어야 하는 건
`dotnet-to-node-surface-mapping`(키스톤), `backend-dependency-policy`,
`lifecycle-and-failure-semantics` 셋이고, 나머지는 해당 부분 구현 시 참조하는
레퍼런스다.

| 문서 | 범위 |
|------|------|
| [dotnet-to-node-surface-mapping](./internals/dotnet-to-node-surface-mapping.ko.md) | **이식 기준**(번역 규칙) |
| [backend-dependency-policy](./internals/backend-dependency-policy.ko.md) | backend 교체 가능성, public surface 격리 |
| [di-capability-exposure-policy](./internals/di-capability-exposure-policy.ko.md) | capability 별 DI 노출 규칙 |
| [lifecycle-and-failure-semantics](./internals/lifecycle-and-failure-semantics.ko.md) | 시동/종료/실패 의미 |
| [behavior-matrix](./internals/behavior-matrix.ko.md) | 기능별 동작 매트릭스 |
| [implementation-scope-and-nongoals](./internals/implementation-scope-and-nongoals.ko.md) | 범위·비목표 |
| [regression-test-matrix](./internals/regression-test-matrix.ko.md) | 회귀 테스트 기준 |

## 3. 구현 순서 권장

1. `internals/dotnet-to-node-surface-mapping` 으로 번역 규칙을 고정한다.
2. `spec/nestjs-overview` 의 backend 어댑터(포트 구현)부터 만든다 — 유일한
   backend 스왑 지점이다.
3. Node binding public API gap 을 닫는다. framework 는 binding internal/native
   경로를 직접 우회하지 않는다.
4. `spec/handler-interfaces` 의 계약을 TypeScript 로 옮긴다(백엔드 독립).
5. host lifecycle 을 붙인 뒤 channel messaging 으로 첫 수직 슬라이스를 닫는다.
6. channel 이후에는 `spot` 과 `registry/base monitoring` 을 병행할 수 있다.
   다만 spot monitoring source 는 spot runtime 이 생긴 뒤에만 닫는다.
7. actor core 를 구현한 뒤 stream/session relay 와 Stream Connector 를 붙인다.
8. `internals/regression-test-matrix` 와 `sample-implementation-plan` 으로 dotnet 과
   동등성을 검증하고, P9 에서 사용자 guide, sample, cross-language smoke 까지 닫는다.

## 4. 회귀 테스트

이 README 는 아래 문서 회귀 테스트와 함께 유지한다.

| 테스트 | 확인 기준 |
|--------|-----------|
| `documentation-regression.test.js › node README links every guide chapter` | `README.ko.md`가 12개 guide 장을 모두 연결한다. |
| `documentation-regression.test.js › node documentation relative markdown links resolve` | 구현 순서와 참조 문서 링크가 깨지지 않는다. |
| `sample-regression.test.js › node samples define the required sample directories and README files` | Phase 9 guide/sample/cross-language smoke 기준 문서가 README 에서 연결된다. |
