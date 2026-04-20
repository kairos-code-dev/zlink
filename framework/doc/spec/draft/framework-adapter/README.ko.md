[스펙 목차](../../README.ko.md)

[개요](./overview.ko.md) | [use cases](./use-cases/README.ko.md) | [상호작용 모델](./interaction-model.ko.md) | [메시지 모델](./message-model.ko.md) | [channel topology](./channel-topology.ko.md) | [framework API](./framework-api.ko.md) | [검증](./usecase-validation.ko.md) | [.NET](./dotnet/README.ko.md) | [Java](./java/README.ko.md) | [Node.js](./node/README.ko.md) | [Python](./python/README.ko.md) | [Go](./go/README.ko.md) | [Rust](./rust/README.ko.md) | [C++](./cpp/README.ko.md)

# Draft -- ZLink Framework

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API, 동작, 프레임워크
> 통합 표면을 보장하지 않는다.
> 구현과 공개 헤더, 테스트, 바인딩 문서가 확정되면 정식 spec 문서에 나누어
> 반영한다.

## 1. 목적

이 초안 묶음은 zlink의 `.NET`, `Java`, `Node.js`, `Python`, `C++` 바인딩
위에 `ASP.NET Core`, `Spring Boot`, `NestJS`, `FastAPI`, standalone
host/runtime 사용자를 위한 `ZLink Framework` 방향을 정리한다. 제품 개요와
핵심 가치는 [overview.ko.md](./overview.ko.md)를 참고한다.

## 2. 문서 구성

아래 문서들은 각각 한 가지 주제만 다루며, 서로 범위가 겹치지 않게 구성했다.
번호 순서대로 읽으면 전체 그림을 자연스럽게 따라갈 수 있다.

| 순서 | 문서 | 다루는 범위 |
|:----:|------|------------|
| 1 | [overview.ko.md](./overview.ko.md) | 제품 개요, 핵심 차별점, 현재 우선 범위. "ZLink Framework가 무엇이고, 왜 필요한가"에 답한다. |
| 2 | [use-cases/README.ko.md](./use-cases/README.ko.md) | use case별 문서 목록과 관리 규칙. 모든 설계는 use case에서 출발한다. |
| 3 | [interaction-model.ko.md](./interaction-model.ko.md) | 사용자에게 보이는 상호작용 모델 분류. request-response, command, publish-subscribe 등 각 모델의 의미를 정의한다. |
| 4 | [message-model.ko.md](./message-model.ko.md) | `header + body` 메시지 구조, header 필드, body codec 방향. wire 수준 메시지 형식을 다룬다. |
| 5 | [channel-topology.ko.md](./channel-topology.ko.md) | channel grouping, Discovery, 수동 연결, 상호작용 모델과 내부 transport 매핑. 내부 배선이 어떻게 구성되는지 다룬다. |
| 6 | [framework-api.ko.md](./framework-api.ko.md) | `ASP.NET Core`, `Spring Boot`, `NestJS`, `FastAPI`, `C++` standalone host 기준의 API 표면 방향. 각 환경에서 handler와 client가 어떤 모양으로 보이는지 다룬다. |
| 7 | [dotnet/README.ko.md](./dotnet/README.ko.md) | `.NET`과 `ASP.NET Core` 전용 상세 초안. handler 인터페이스, 샘플, SPOT 통합, Registry 통합을 포함한다. |
| 8 | [java/README.ko.md](./java/README.ko.md) | `Java`와 `Spring Boot` 전용 상세 초안 진입점. |
| 9 | [node/README.ko.md](./node/README.ko.md) | `Node.js`와 `NestJS` 전용 상세 초안 진입점. |
| 10 | [python/README.ko.md](./python/README.ko.md) | `Python`과 `FastAPI` 전용 상세 초안 진입점. |
| 11 | [go/README.ko.md](./go/README.ko.md) | `Go`와 `net/http` 계열 전용 상세 초안 진입점. |
| 12 | [rust/README.ko.md](./rust/README.ko.md) | `Rust`와 `Axum` 전용 상세 초안 진입점. |
| 13 | [cpp/README.ko.md](./cpp/README.ko.md) | `C++` standalone host/runtime 전용 상세 초안 진입점. |
| 14 | [usecase-validation.ko.md](./usecase-validation.ko.md) | 각 use case를 현재 초안이 얼마나 설명하는지 점검하는 체크리스트. |

개요(1)로 전체 그림을 잡고, use case(2)로 무엇을 해결하려는지 본 뒤,
모델(3-4)로 설계 방향을 확인하고, topology(5)로 내부 매핑을 이해하고,
API 표면(6-7)으로 구체적인 모양을 보고, 마지막으로 검증(8)에서 빠진 부분을
확인하는 흐름이다.

언어별 상세 초안을 새로 읽을 때는 아래 순서를 기본으로 본다.

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

## 4. 문서 작성 원칙

- 새 요구가 생기면 먼저 `use-cases/` 아래에 케이스 문서를 추가한다.
- 그 다음 공통 초안 문서에서 필요한 개념을 보강한다.
- 마지막으로 `usecase-validation.ko.md`에서 그 요구가 현재 초안으로 설명되는지
  확인한다.

즉 이 초안 묶음은 "API를 먼저 적고 나중에 용도를 붙이는" 방식이 아니라,
"용도를 먼저 적고 API를 그 용도에 맞춰 좁히는" 방식을 따른다.

## 5. 언어별 상세 초안 작성 규칙

이 공통 묶음은 `.NET` 하나만 위한 문서가 아니다. 이후 `Java`, `Node.js`,
`Python`, `C++` 상세 초안도 이 묶음을 기준으로 같은 수준으로 작성할 수 있어야
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

### 5.3 언어별 디렉토리의 최소 문서 세트

`.NET` 수준의 상세 초안을 새 언어로 만들려면 최소한 아래 문서 세트가 필요하다.

| 문서 종류 | 역할 |
|----------|------|
| `README.ko.md` | 그 언어 묶음의 진입점. 문서 구조, 역할 분담, 핵심 방향을 정리한다. |
| 인터페이스 기준 문서 | 공용 interface / context / options / attribute 또는 decorator를 한 곳에 모은다. |
| channel messaging 주제 문서 | channel 등록, handler 모델, outbound client, dispatch 흐름을 설명한다. |
| channel messaging 샘플 문서 | 등록부터 handler, client 호출까지 한 번에 보이는 샘플을 둔다. |
| `SPOT` 주제 문서 | 해당 언어에서 `SPOT`을 지원하면 lifecycle, publish/subscribe, channel attach를 설명한다. |
| `SPOT` 샘플 문서 | room/stage/zone 같은 실제 흐름을 코드로 보여 준다. |
| `STREAM` 주제 문서 | packet handler, raw handler, open item을 분리해서 설명한다. |
| `STREAM` 샘플 문서 | 등록과 handler 코드를 한 번에 보여 준다. |
| Registry 주제 문서 | embedded/standalone, query surface, topology 조회를 설명한다. |

언어 특성상 어떤 축이 아직 미구현이면, 문서를 조용히 빼지 말고
"현재 범위 밖" 또는 "open items"로 명시해야 한다.

### 5.3.1 대표 프레임워크 기준

언어별 상세 초안은 아래 대표 프레임워크 또는 host를 기준으로 먼저 작성한다.

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

언어별 상세 초안은 아래 항목을 명확히 적어야 한다.

- local channel을 어떻게 등록하는가
- outbound channel을 어떻게 등록하는가
- 자동 연결과 수동 연결을 어떻게 설정하는가
- request/send/event 호출 시 기본 packet key를 어떻게 해석하는가
- timeout, packet override 같은 변형을 어떤 `options` 또는 동등한 구조로 두는가
- handler dispatch가 어떤 ingress를 기준으로 설명되는가
- outbound reply 수신은 어떤 경로로 처리되는가
- outbound-only 앱이 가능한가
- 샘플 코드가 실제 registration API와 인터페이스 이름과 맞는가

이 체크리스트를 만족하지 않으면, 공통 개념이 언어 표면으로 충분히 내려오지
않은 것으로 본다.

### 5.5 언어별 open item 처리 규칙

언어별 문서에서 아직 못 닫은 항목은 공통 문서와 섞지 않고, 언어 디렉토리 안의
별도 open item 문서로 뺀다. 예를 들어 `.NET`의 `stream-open-items.ko.md`처럼
분리하는 편이 맞다.

이렇게 해야 구현 가능한 계약과 미결 항목이 섞이지 않고, 다른 언어가 같은
수준으로 문서를 작성할 때도 빠진 부분을 한눈에 비교할 수 있다.
