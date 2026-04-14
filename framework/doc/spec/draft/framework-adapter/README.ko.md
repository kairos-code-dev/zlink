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
정리한다.

`ZLink Framework`의 목표는 새 transport를 만드는 일이 아니다. 목표는 프레임워크 사용자가
raw `DEALER`, `ROUTER`, `PUB`, `SUB`, `SPOT`를 직접 다루지 않고도, 기존에
HTTP나 gRPC를 쓰던 감각에 가깝게 zlink 기반 서버 간 메시징을 쓰게 만드는 일이다.

## 2. 이 초안이 전제하는 방향

- 메시지는 기본적으로 `header + body` 멀티파트로 다룬다.
- `body` codec은 `protobuf`, `json`을 우선 고려하고, 나중에 다른 codec을
  추가할 수 있게 연다.
- 연결 방식은 수동 endpoint 설정과 Discovery 기반 자동 연결을 모두 지원한다.
- 서비스 묶음은 `service_name` 기준으로 본다.
- 위치투명성과 provider 선택을 위해 별도 gateway나 전용 로드밸런서를 먼저 두는
  모델보다, `service_name + Discovery + client-side selection`만으로 직접
  location-transparent 호출이 가능해야 한다.
- 프레임워크 표면은 각 프레임워크 사용자가 익숙한 방식에 최대한 맞춘다.
- 문서 구조는 use case를 먼저 정리하고, 그 use case를 만족하는지 나중 문서에서
  검증하는 순서를 따른다.

## 3. 문서 구성

| 문서 | 역할 |
|------|------|
| [overview.ko.md](./overview.ko.md) | `ZLink Framework`의 제품 개요와 핵심 가치 |
| [use-cases/README.ko.md](./use-cases/README.ko.md) | 케이스별 문서 목록과 관리 규칙 |
| [interaction-model.ko.md](./interaction-model.ko.md) | 사용자에게 보일 상호작용 모델 |
| [message-model.ko.md](./message-model.ko.md) | `header + body` 메시지 초안 |
| [service-topology.ko.md](./service-topology.ko.md) | service grouping, Discovery, topology 매핑 |
| [framework-api.ko.md](./framework-api.ko.md) | `ASP.NET Core`, `Spring`, `NestJS` 표면 방향 |
| [dotnet/README.ko.md](./dotnet/README.ko.md) | `.NET`과 `ASP.NET Core` 전용 초안 |
| [usecase-validation.ko.md](./usecase-validation.ko.md) | 각 use case를 현재 초안이 얼마나 설명하는지 점검 |

## 4. 문서 작성 원칙

- 새 요구가 생기면 먼저 `use-cases/` 아래에 케이스 문서를 추가한다.
- 그 다음 공통 초안 문서에서 필요한 개념을 보강한다.
- 마지막으로 `usecase-validation.ko.md`에서 그 요구가 현재 초안으로 설명되는지
  확인한다.

즉 이 초안 묶음은 "API를 먼저 적고 나중에 용도를 붙이는" 방식이 아니라,
"용도를 먼저 적고 API를 그 용도에 맞춰 좁히는" 방식을 따른다.
