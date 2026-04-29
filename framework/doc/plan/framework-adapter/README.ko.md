[스펙 목차](../../spec/README.ko.md)

[Framework Adapter 정책](../../spec/draft/framework-adapter/policy/README.ko.md) | [.NET 초안](../../spec/draft/framework-adapter/bindings/dotnet/README.ko.md)

# Framework Adapter 구현 계획

이 디렉토리는 `framework-adapter` 초안을 실제 코드와 테스트로 옮길 때 쓰는 내부
진행 문서를 모은다.

`.NET` framework adapter 구현 루트는
`/home/hep7/project/kairos/zlink/framework/languages/dotnet`으로 고정한다.
`bindings/dotnet`은 backend 라이브러리와 기존 바인딩 테스트를 유지하는 위치로 보고,
framework 자체 구현은 이 plan 문서 기준으로 `framework/languages/dotnet` 아래에서
진행한다.

현재 기준 문서는 아래 하나다.

| 문서 | 역할 |
|------|------|
| [dotnet-execution-plan.ko.md](./dotnet-execution-plan.ko.md) | `.NET` framework adapter를 `framework/languages/dotnet` 아래에 실제로 구현하고, 테스트와 CI gate까지 닫는 실행 계획 |
| [full-implementation-and-sample-plan.ko.md](./full-implementation-and-sample-plan.ko.md) | framework, Stream Connector, Session Gateway, TicTacToe sample을 문서 기준으로 끝까지 구현하고 반복 리뷰, POSD 리팩토링, 전체 테스트까지 닫는 실행 계획 |
| [framework-dotnet-release-gate.ko.md](./framework-dotnet-release-gate.ko.md) | framework `.NET` release gate 구성과 실패 triage 기준 |

이 문서는 공개 API 계약 문서가 아니다. 실제 계약은 반드시
`framework/doc/spec/draft/framework-adapter/` 아래 초안 문서를 먼저 기준으로 읽고,
이 계획 문서는 "어떤 순서로 구현하고, 어디에서 검증하며, 언제 완료로 볼 것인가"를
확인하는 용도로 사용한다.
