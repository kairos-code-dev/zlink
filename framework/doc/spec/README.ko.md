<!-- framework-adapter-nav:start -->
[문서 목록](../README.ko.md) | [이전: ZLink Framework 문서](../README.ko.md) | [다음: ZLink Framework Overview](./overview.ko.md)
<!-- framework-adapter-nav:end -->

[Framework 문서](../README.ko.md) | [공통 스펙 초안](./draft/README.ko.md)

[개요](./overview.ko.md) | [use cases](./use-cases/README.ko.md) | [상호작용 모델](./interaction-model.ko.md) | [메시지 모델](./message-model.ko.md) | [channel topology](./channel-topology.ko.md) | [framework API](./framework-api.ko.md) | [비동기 실행](./async-execution-policy.ko.md) | [Actor 모델](./actor-model.ko.md) | [Session Actor Dispatch 사용성](./session-actor-dispatch.ko.md) | [공통 샘플](./sample/README.ko.md) | [Session Gateway 보관본](./archive/session-gateway.ko.md) | [검증](./usecase-validation.ko.md) | [.NET](../../languages/dotnet/doc/README.ko.md) | [.NET Session Actor Dispatch](../../languages/dotnet/doc/spec/session-actor-dispatch.ko.md) | [Java](../../languages/java/doc/draft/README.ko.md) | [Node.js](../../languages/node/doc/draft/README.ko.md) | [Python](../../languages/python/doc/draft/README.ko.md) | [Go](../../languages/go/doc/draft/README.ko.md) | [Rust](../../languages/rust/doc/draft/README.ko.md) | [C++](../../languages/cpp/doc/draft/README.ko.md)

# ZLink Framework 공통 스펙

> 이 문서 묶음은 언어 중립 **공통 스펙**이다. 여기서 정의한 의미는 언어별
> 문서가 재정의하지 않고 각 언어 표면으로만 구체화한다. 아직 닫히지 않은
> 항목은 [draft/](./draft/README.ko.md)에 분리한다.

## 1. 목적

이 묶음은 zlink의 `.NET`, `Java`, `Node.js`, `Python`, `Go`, `Rust`, `C++` 바인딩
위에 `ASP.NET Core`, `Spring Boot`, `NestJS`, `FastAPI`, `net/http` / `Gin`,
`Axum`, standalone host/runtime 사용자를 위한 `ZLink Framework` 방향을 정리한다.
제품 개요와 핵심 가치는 [overview.ko.md](./overview.ko.md)를 참고한다.

## 1.1 버전 기준

지원 언어와 런타임 버전은 binding 문서가 먼저 명시해야 한다. 특히 `.NET`
문서에서는 아래 기준을 공통으로 적용한다.

- 최소 지원 런타임은 `.NET 8` (`net8.0`)이다.
- 주 개발 기준은 `.NET 10` (`net10.0`)이다.
- 최소 지원 C# 언어 버전은 `C# 12`다.
- 문서와 샘플은 최소 지원 버전에서 성립하지 않는 `preview`, `latest`,
  `C# 13`, `C# 14` 전용 문법을 전제로 쓰지 않는다.

즉 바인딩 구현과 샘플이 더 높은 런타임에서 함께 개발되더라도, 공개 framework
계약은 먼저 "어디까지를 최소 지원으로 볼 것인가"를 분명히 적어야 한다.

## 2. 문서 구성

아래 문서들은 각각 한 가지 주제만 다루며, 서로 범위가 겹치지 않게 구성했다.
번호 순서대로 읽으면 전체 그림을 자연스럽게 따라갈 수 있다.

| 순서 | 문서 | 다루는 범위 |
|:----:|------|------------|
| 1 | [overview.ko.md](./overview.ko.md) | 제품 개요, 핵심 차별점, 현재 우선 범위. "ZLink Framework가 무엇이고, 왜 필요한가"에 답한다. |
| 2 | [Use case 목록](./use-cases/README.ko.md) | use case별 문서 목록과 관리 규칙. 모든 설계는 use case에서 출발한다. |
| 3 | [interaction-model.ko.md](./interaction-model.ko.md) | 사용자에게 보이는 상호작용 모델 분류. request-response, command, publish-subscribe 등 각 모델의 의미를 정의한다. |
| 4 | [message-model.ko.md](./message-model.ko.md) | 서버 간 multipart `header + payload` 메시지 구조, STREAM 단일 packet 경계, header 필드, payload codec 방향. wire 수준 메시지 형식을 다룬다. |
| 5 | [channel-topology.ko.md](./channel-topology.ko.md) | channel grouping, Discovery, 수동 연결, 상호작용 모델과 내부 transport 매핑. 내부 배선이 어떻게 구성되는지 다룬다. |
| 6 | [framework-api.ko.md](./framework-api.ko.md) | `ASP.NET Core`, `Spring Boot`, `NestJS`, `FastAPI`, `C++` standalone host 기준의 API 표면 방향. 각 환경에서 handler와 client가 어떤 모양으로 보이는지 다룬다. |
| 7 | [비동기 실행과 coroutine 정책](./async-execution-policy.ko.md) | async submit, blocking 대안 금지, coroutine/adapter의 공통 의미를 정의한다. |
| 8 | [actor-model.ko.md](./actor-model.ko.md) | actor 개념을 cross-binding 기준으로 정의한다. actor 라이프사이클 (Entry Spot / session bind / user Spot join), application 로직 vs framework 자동 처리, outbound actor 호출, session actor dispatch 패턴, 등록 표면을 다룬다. |
| 9 | [Session Actor Dispatch](./session-actor-dispatch.ko.md) | actor 모델의 한 use case로서 session actor dispatch의 cross-binding 사용성 결정 사항. typed handler 의미, route resolver 계약, helper 의미, `SessionProxy` 의미, error 매트릭스를 다룬다. 구체 .NET 시그니처와 등록 코드, sample은 [bindings/dotnet/session-actor-dispatch.ko.md](../../languages/dotnet/doc/spec/session-actor-dispatch.ko.md)에 분리되어 있다. |
| 10 | [공통 샘플 시나리오](./sample/README.ko.md) | Bingo와 TicTacToe의 언어 중립 샘플 기준. 서버 역할, 메시지 흐름, handler 등록 방식 차이를 정의한다. |
| 11 | [Session Gateway 보관본](./archive/session-gateway.ko.md) | 이전 session gateway/actor relay 초안의 보관본. 현재 public API 기준이 아니며, 배경과 문제 맥락을 확인할 때만 사용한다. 현재 기준은 위 §9이다. |
| 12 | [.NET 문서](../../languages/dotnet/doc/README.ko.md) | `.NET`과 `ASP.NET Core` 전용 문서. handler 인터페이스, 샘플, SPOT 통합, Registry 통합을 포함한다. |
| 13 | [Java 문서](../../languages/java/doc/draft/README.ko.md) | `Java`와 `Spring Boot` 전용 문서 진입점. |
| 14 | [Node.js 문서](../../languages/node/doc/draft/README.ko.md) | `Node.js`와 `NestJS` 전용 문서 진입점. |
| 15 | [Python 문서](../../languages/python/doc/draft/README.ko.md) | `Python`과 `FastAPI` 전용 문서 진입점. |
| 16 | [Go 문서](../../languages/go/doc/draft/README.ko.md) | `Go`와 `net/http` 계열 전용 문서 진입점. |
| 17 | [Rust 문서](../../languages/rust/doc/draft/README.ko.md) | `Rust`와 `Axum` 전용 문서 진입점. |
| 18 | [C++ 문서](../../languages/cpp/doc/draft/README.ko.md) | `C++` standalone host/runtime 전용 문서 진입점. |
| 19 | [Use case 검증](./usecase-validation.ko.md) | 각 use case를 현재 스펙이 얼마나 설명하는지 점검하는 체크리스트. |

개요(1)로 전체 그림을 잡고, use case(2)로 무엇을 해결하려는지 본 뒤,
모델(3-4)로 설계 방향을 확인하고, topology(5)로 내부 매핑을 이해하고,
API 표면(6)과 비동기 실행 정책(7)을 본 다음, actor 모델(8)과 그 use case 정책(9)을
잡고, 공통 샘플(10)로 대표 흐름을 확인한다. 이전 보관본은 §11에서 배경만 확인하고,
언어별 상세(12-18)로 내려간 뒤, 마지막으로 검증(19)에서 빠진 부분을 확인하는
흐름이다.

언어별 상세 문서를 새로 읽을 때는 아래 순서를 기본으로 본다.

1. 공통 문서로 의미를 먼저 이해한다.
2. 해당 언어 `README.ko.md`로 진입한다.
3. 그 언어의 인터페이스 기준 문서, 주제 문서, 샘플 문서를 순서대로 읽는다.

## 3. 각 문서의 범위 원칙

각 문서가 다루는 내용이 겹치지 않도록, 아래 원칙을 따른다.

| 개념 | 다루는 곳 | 다른 문서에서는 |
|------|----------|---------------|
| 제품 정의, 차별점, transport 축, 우선 범위 | overview | 필요하면 overview를 링크 |
| 상호작용 모델 분류와 모델별 의미 | interaction-model | 필요하면 interaction-model을 링크 |
| 메시지 구조, header 필드, codec | message-model | 필요하면 message-model을 링크 |
| channel grouping, Discovery, 내부 매핑 | channel-topology | 필요하면 channel-topology를 링크 |
| 프레임워크별 API 표면, DI, handler 등록 | framework-api, dotnet/ | 필요하면 해당 문서를 링크 |
| actor 개념, 라이프사이클, session bind, user Spot join, session actor dispatch | actor-model, session-gateway-usability | 필요하면 해당 문서를 링크 |

## 4. 문서 작성 원칙

- 새 요구가 생기면 먼저 `use-cases/` 아래에 케이스 문서를 추가한다.
- 그 다음 공통 문서에서 필요한 개념을 보강한다.
- 마지막으로 `usecase-validation.ko.md`에서 그 요구가 현재 스펙으로 설명되는지
  확인한다.

즉 이 문서 묶음은 "API를 먼저 적고 나중에 용도를 붙이는" 방식이 아니라,
"용도를 먼저 적고 API를 그 용도에 맞춰 좁히는" 방식을 따른다.

## 5. 언어별 상세 문서 작성 규칙

이 공통 묶음은 `.NET` 하나만 위한 문서가 아니다. 이후 `Java`, `Node.js`,
`Python`, `C++` 상세 문서도 이 묶음을 기준으로 같은 수준으로 작성할 수 있어야
한다.

그래서 언어별 디렉토리는 아래 규칙을 따른다.

### 5.1 공통 문서를 다시 정의하지 않는다

언어별 문서는 아래 의미를 새로 정의하면 안 된다.

- 상호작용 모델 이름과 의미
- message header의 공통 의미
- channel grouping과 Discovery/수동 연결의 기본 관계
- use case 판정 결과

이 의미를 바꾸고 싶으면 먼저 공통 문서를 수정해야 한다.

### 5.2 언어별 문서는 공통 개념을 구체화한다

언어별 문서는 아래 내용을 해당 언어 관용구로 내려 적어야 한다.

- registration API 이름
- client / publisher / manager 인터페이스 이름
- context, options, attribute/decorator/interface 같은 언어별 표면
- DI 또는 lifecycle 통합 방식
- 샘플 코드에서의 실제 호출 모양

즉 공통 문서가 "무슨 의미를 가져야 하는가"를 정하면, 언어 문서는
"그 의미가 이 언어에서 어떤 시그니처와 샘플로 보이는가"를 적는다.

### 5.2.1 네이밍 규칙

framework 문서의 public 이름 규칙은
[doc/spec/bindings/README.md](/home/hep7/project/kairos/zlink/doc/spec/bindings/README.md)의
`Naming Policy`를 그대로 따른다. 이 공통 문서와 언어별 상세 문서는 아래 규칙을
같이 지켜야 한다.

- 개념 이름은 바인딩 간 최대한 동일하게 유지한다.
- 실제 언어 문서에서 허용하는 변형은 언어별 케이싱 차이와, overloading이 없는
  언어에서의 최소 접미사뿐이다.
- 단어 교체, 단어 생략, 의미가 같은 별도 이름 추가는 허용하지 않는다.
- 파라미터 조합이 다르다는 이유만으로 이름을 늘리지 않는다.
- async submit, blocking 대안 금지, coroutine adapter의 공통 의미는
  [비동기 실행과 coroutine 정책](./async-execution-policy.ko.md)을 따른다.
- builder terminator 이름은 공통 의미를 유지하되, 각 언어의 비동기 네이밍 관례에 맞춰
  투영한다. 예를 들어 `.NET` awaitable terminator는 `SubmitAsync(...)`이고,
  Node.js `Promise` terminator는 `submit(...)`이다.

문서에서 우선 따라야 할 언어별 케이싱은 아래와 같다.

- Python: 모든 public API는 `snake_case`
- Java: 메서드는 `camelCase`, 클래스와 annotation은 `PascalCase`
- C#: public API 전체를 `PascalCase`
- Go: exported 이름은 `PascalCase`
- Rust: 메서드는 `snake_case`, 타입은 `PascalCase`
- C++: 메서드는 `snake_case`, 타입은 `_t` 접미사
- Node/TypeScript: 메서드는 `camelCase`, 클래스는 `PascalCase`

즉 framework adapter 문서도 `sendWithRoutingId`, `request_callback`,
`publishToTopic`, `recvTimeout` 같은 이름을 쓰지 않고, 가능하면 canonical
action 이름을 유지해야 한다. 예를 들면 아래처럼 맞춘다.

- `Send`, `Request`, `Publish`
- `SendTo`, `RequestTo`
- `SendChannel`, `RequestChannel`
- `Connect`, `Bind`, `Close`
- `CreateAsync`, `GetAsync`, `ListAsync`

정리하면:

- 공통 문서는 이름의 의미와 canonical 단어 구성을 정한다.
- 언어 문서는 그 이름을 각 언어 케이싱 규칙에 맞게만 변환한다.
- 샘플 코드와 본문 설명도 같은 규칙을 따라야 한다.

### 5.3 언어별 디렉토리의 최소 문서 세트

`.NET` 수준의 상세 문서을 새 언어로 만들려면 최소한 아래 문서 세트가 필요하다.

| 문서 종류 | 역할 |
|----------|------|
| `README.ko.md` | 그 언어 묶음의 진입점. 문서 구조, 역할 분담, 핵심 방향을 정리한다. |
| 인터페이스 기준 문서 | 공용 interface / context / configuration surface / attribute 또는 decorator를 한 곳에 모은다. 공용 계약과 내부 runtime 구현의 분리 기준은 [framework-api.ko.md §2.5](./framework-api.ko.md#25-public-contract와-runtime-구현의-분리-기준)를 따른다. |
| channel messaging 주제 문서 | channel 등록, handler 모델, outbound client, dispatch 흐름을 설명한다. |
| channel messaging 샘플 문서 | 등록부터 handler, client 호출까지 한 번에 보이는 샘플을 둔다. |
| `SPOT` 주제 문서 | 해당 언어에서 `SPOT`을 지원하면 lifecycle, publish/subscribe, channel attach를 설명한다. |
| `SPOT` 샘플 문서 | room/stage/zone 같은 실제 흐름을 코드로 보여 준다. |
| Actor / Entry Spot 주제 문서 | actor factory, Entry Spot registry, user Spot registry, actor packet handler, join/leave lifecycle handler를 설명한다. |
| Actor / Entry Spot 샘플 문서 | Entry Spot에서 인증 또는 target Spot 선택을 처리하고, user Spot에서 domain packet을 처리하는 흐름을 한 예시 안에 보여 준다. |
| `STREAM` 주제 문서 | framework Header 기반 packet session과 open item을 분리해서 설명한다. |
| `STREAM` 샘플 문서 | 등록과 handler 코드를 한 번에 보여 준다. |
| Monitoring 주제 문서 | socket/discovery/registry/spot runtime event와 등록 모델을 설명한다. |
| Registry 주제 문서 | embedded/standalone, query surface, topology 조회를 설명한다. |

언어 특성상 어떤 축이 아직 미구현이면, 문서를 조용히 빼지 말고
"현재 범위 밖" 또는 "open items"로 명시해야 한다.

### 5.3.1 대표 프레임워크 기준

언어별 상세 문서은 아래 대표 프레임워크 또는 host를 기준으로 먼저 작성한다.

| 언어 | 대표 기준 |
|------|-----------|
| `.NET` | `ASP.NET Core` |
| `Java` | `Spring Boot` |
| `Node.js` | `NestJS` |
| `Python` | `FastAPI` |
| `Go` | `net/http`, 필요하면 `Gin` |
| `Rust` | `Axum` |
| `C++` | standalone host/runtime |

`C++`는 다른 언어처럼 기존 웹 프레임워크 위 adapter로 보기보다,
zlink framework가 host, lifecycle, dispatch loop를 직접 포함하는 standalone
runtime으로 설명하는 편이 맞다.

### 5.4 언어별 문서의 최소 체크리스트

언어별 상세 문서은 아래 항목을 명확히 적어야 한다.

- local channel을 어떻게 등록하는가
- outbound channel을 어떻게 등록하는가
- 자동 연결과 수동 연결을 어떻게 설정하는가
- request/send/event 호출 시 기본 packet key를 어떻게 해석하는가
- timeout, packet override 같은 변형을 어떤 `options` 또는 동등한 구조로 두는가
- send/publish의 기본 async submit과 `SendTimeout` 기반 backpressure 대기를
  어떻게 설명하는가
- handler dispatch가 어떤 ingress를 기준으로 설명되는가
- outbound reply 수신은 어떤 경로로 처리되는가
- outbound-only 앱이 가능한가
- `STREAM`을 지원하면 session callback이 transport callback에서 직접 실행되지
  않고 비동기 실행 단위로 넘어가는지, 같은 session callback 직렬성이 보장되는지
  설명하는가
- actor/session 모델을 지원하면 actor가 `Spot`에 attach된 뒤
  `OnDispatch` 계열 callback이 해당 `Spot` 실행 문맥에서 실행되는지 설명하는가
- actor/session 모델을 지원하면 Entry Spot public 표면을 별도 섹션으로 설명하는가
- Entry Spot에서 actor packet handler를 등록하는 API와 예시가 있는가
- user Spot에서 actor packet handler를 등록하는 API와 예시가 있는가
- Entry Spot과 user Spot의 actor packet handler 인자 차이를 설명하는가
- actor join/leave lifecycle handler를 `AddActorJoined` / `AddActorLeft`에 해당하는
  registry 등록 표면으로 설명하는가
- actor join/leave lifecycle을 `OnJoinActor` / `OnLeaveActor` 같은 Spot method
  override로만 설명하지 않는가
- Entry Spot registry와 user Spot registry가 서로 다른 namespace라서 같은 actor
  type과 packet 이름을 다르게 매핑할 수 있음을 설명하는가
- 같은 registry 안에서 actor packet, joined lifecycle, left lifecycle 중복 등록이
  startup validation 오류임을 설명하는가
- actor/session 모델의 회귀 테스트는 join 직후 packet, spot 이동 직후 packet,
  stale session packet을 구분해서 검증하는가
- stream session 회귀 테스트는 callback task dispatch, 같은 session callback
  직렬성, enqueue 진입점만 허용되는지 검증하는가
- `SPOT`을 지원하면 Spot 타입 기준 factory 등록, `RoutingId` 기준 생성과 조회,
  lifecycle timer, 외부 spot publish 표면을 어떻게 설명하는가
- monitoring을 지원하면 socket/discovery/registry/spot runtime event를 어떤
  typed event와 등록 surface로 설명하는가
- 샘플 코드가 실제 registration API와 인터페이스 이름과 맞는가

이 체크리스트를 만족하지 않으면, 공통 개념이 언어 표면으로 충분히 내려오지
않은 것으로 본다.

### 5.5 언어별 open item 처리 규칙

언어별 문서에서 아직 못 닫은 항목은 공통 문서와 섞지 않고, 언어 디렉토리 안의
별도 open item 문서로 뺀다. 예를 들어 `.NET`의 `stream-open-items.ko.md`처럼
분리하는 편이 맞다.

이렇게 해야 구현 가능한 계약과 미결 항목이 섞이지 않고, 다른 언어가 같은
수준으로 문서를 작성할 때도 빠진 부분을 한눈에 비교할 수 있다.
