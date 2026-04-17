[스펙 목차](../../README.ko.md)

# Draft -- ZLink Framework

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API, 동작, 프레임워크
> 통합 표면을 보장하지 않는다.
> 구현과 공개 헤더, 테스트, 바인딩 문서가 확정되면 정식 spec 문서에 나누어
> 반영한다.

## 1. 목적

이 초안 묶음은 zlink의 `.NET`, `Java`, `Node.js` 바인딩 위에
`ASP.NET Core`, `Spring`, `NestJS` 사용자를 위한 `ZLink Framework` 방향을
정리한다. 제품 개요와 핵심 가치는 [overview.ko.md](./overview.ko.md)를 참고한다.

## 2. 문서 구성

아래 문서들은 각각 한 가지 주제만 다루며, 서로 범위가 겹치지 않게 구성했다.
번호 순서대로 읽으면 전체 그림을 자연스럽게 따라갈 수 있다.

| 순서 | 문서 | 다루는 범위 |
|:----:|------|------------|
| 1 | [overview.ko.md](./overview.ko.md) | 제품 개요, 핵심 차별점, 현재 우선 범위. "ZLink Framework가 무엇이고, 왜 필요한가"에 답한다. |
| 2 | [use-cases/README.ko.md](./use-cases/README.ko.md) | use case별 문서 목록과 관리 규칙. 모든 설계는 use case에서 출발한다. |
| 3 | [interaction-model.ko.md](./interaction-model.ko.md) | 사용자에게 보이는 상호작용 모델 분류. request-response, command, publish-subscribe 등 각 모델의 의미를 정의한다. |
| 4 | [message-model.ko.md](./message-model.ko.md) | `header + body` 메시지 구조, header 필드, body codec 방향. wire 수준 메시지 형식을 다룬다. |
| 5 | [service-topology.ko.md](./service-topology.ko.md) | service grouping, Discovery, 수동 연결, 상호작용 모델과 내부 transport 매핑. 내부 배선이 어떻게 구성되는지 다룬다. |
| 6 | [framework-api.ko.md](./framework-api.ko.md) | `ASP.NET Core`, `Spring`, `NestJS`별 API 표면 방향. 각 프레임워크에서 handler와 client가 어떤 모양으로 보이는지 다룬다. |
| 7 | [dotnet/README.ko.md](./dotnet/README.ko.md) | `.NET`과 `ASP.NET Core` 전용 상세 초안. handler 인터페이스, 샘플, SPOT 통합, Registry 통합을 포함한다. |
| 8 | [usecase-validation.ko.md](./usecase-validation.ko.md) | 각 use case를 현재 초안이 얼마나 설명하는지 점검하는 체크리스트. |

개요(1)로 전체 그림을 잡고, use case(2)로 무엇을 해결하려는지 본 뒤,
모델(3-4)로 설계 방향을 확인하고, topology(5)로 내부 매핑을 이해하고,
API 표면(6-7)으로 구체적인 모양을 보고, 마지막으로 검증(8)에서 빠진 부분을
확인하는 흐름이다.

## 3. 각 문서의 범위 원칙

각 문서가 다루는 내용이 겹치지 않도록, 아래 원칙을 따른다.

| 개념 | 다루는 곳 | 다른 문서에서는 |
|------|----------|---------------|
| 제품 정의, 차별점, transport 축, 우선 범위 | overview | 필요하면 overview를 링크 |
| 상호작용 모델 분류와 모델별 의미 | interaction-model | 필요하면 interaction-model을 링크 |
| 메시지 구조, header 필드, codec | message-model | 필요하면 message-model을 링크 |
| service grouping, Discovery, 내부 매핑 | service-topology | 필요하면 service-topology를 링크 |
| 프레임워크별 API 표면, DI, handler 등록 | framework-api, dotnet/ | 필요하면 해당 문서를 링크 |

## 4. 문서 작성 원칙

- 새 요구가 생기면 먼저 `use-cases/` 아래에 케이스 문서를 추가한다.
- 그 다음 공통 초안 문서에서 필요한 개념을 보강한다.
- 마지막으로 `usecase-validation.ko.md`에서 그 요구가 현재 초안으로 설명되는지
  확인한다.

즉 이 초안 묶음은 "API를 먼저 적고 나중에 용도를 붙이는" 방식이 아니라,
"용도를 먼저 적고 API를 그 용도에 맞춰 좁히는" 방식을 따른다.
