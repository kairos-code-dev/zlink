<!-- framework-adapter-nav:start -->
[문서 목록](./README.ko.md) | [다음: ZLink Framework 공통 스펙](./spec/README.ko.md)
<!-- framework-adapter-nav:end -->

# ZLink Framework 문서

이 디렉토리는 `ZLink Framework`의 문서 진입점이다. 문서는 두 축으로 나뉜다.

- **공통 스펙** (`spec/`) — 언어와 무관한 정식 계약. 상호작용 모델, 메시지
  모델, channel topology, framework API, actor 모델, use case를 정의한다.
- **언어별 문서** (`../languages/<lang>/doc/`) — 공통 스펙을 각 언어와 대표
  프레임워크 표면으로 구체화한다. 공통 의미를 재정의하지 않는다.

`.NET`은 정식 문서로 승격되었고, 그 외 언어는 아직 초안(`draft/`) 단계다.

## 1. 읽는 순서

1. [공통 스펙 목록](./spec/README.ko.md)
2. [Use case 목록](./spec/use-cases/README.ko.md)
3. [Use case 검증](./spec/usecase-validation.ko.md)
4. [언어별 문서](#3-언어별-문서)

## 2. 공통 스펙

| 문서 | 다루는 범위 |
|------|-------------|
| [공통 스펙 목록](./spec/README.ko.md) | 공통 스펙 전체 목록과 읽는 순서, 네이밍 정책 |
| [개요](./spec/overview.ko.md) | Framework의 목적과 우선 범위 |
| [상호작용 모델](./spec/interaction-model.ko.md) | request-response, command, publish-subscribe 같은 사용자 모델 |
| [메시지 모델](./spec/message-model.ko.md) | header/payload 구조와 metadata 정책 |
| [Channel topology](./spec/channel-topology.ko.md) | channel grouping, discovery, 수동 연결, 내부 transport 매핑 |
| [Framework API](./spec/framework-api.ko.md) | 언어별 framework API의 공통 방향 |
| [Actor 모델](./spec/actor-model.ko.md) | actor 위치, session binding, Entry Spot, user Spot, dispatch 기준 |
| [Session Actor Dispatch](./spec/session-actor-dispatch.ko.md) | session과 actor를 연결하는 helper와 routing 정책 |
| [Use case 목록](./spec/use-cases/README.ko.md) | use case 문서 전체 목록과 관리 규칙 |
| [Use case 검증](./spec/usecase-validation.ko.md) | 현재 스펙이 use case를 얼마나 설명하는지 점검 |
| [Session Gateway 보관본](./spec/archive/session-gateway.ko.md) | 이전 초안의 제거 이력과 배경 (참고용) |

공통 스펙 영역의 미확정 초안은 [spec/draft/](./spec/draft/README.ko.md)에 둔다.

언어 중립 샘플 설계는 [spec/sample/tictactoe/](./spec/sample/tictactoe/README.ko.md)에
둔다. 일반(stream 직결)과 session actor dispatch 두 구성을 같은 게임 규칙으로 비교한다.

## 3. 언어별 문서

| 언어 | 상태 | 진입점 |
|------|------|--------|
| `.NET` | 정식 | [languages/dotnet/doc/README.ko.md](../languages/dotnet/doc/README.ko.md) |
| `Java` | 초안 | [languages/java/doc/draft/README.ko.md](../languages/java/doc/draft/README.ko.md) |
| `Node.js` | 초안 | [languages/node/doc/draft/README.ko.md](../languages/node/doc/draft/README.ko.md) |
| `Python` | 초안 | [languages/python/doc/draft/README.ko.md](../languages/python/doc/draft/README.ko.md) |
| `C++` | 초안 | [languages/cpp/doc/draft/README.ko.md](../languages/cpp/doc/draft/README.ko.md) |
| `Go` | 초안 | [languages/go/doc/draft/README.ko.md](../languages/go/doc/draft/README.ko.md) |
| `Rust` | 초안 | [languages/rust/doc/draft/README.ko.md](../languages/rust/doc/draft/README.ko.md) |

각 언어 문서는 `doc/{guide,spec,internals,draft}`로 나뉜다. `guide`는 샘플과
튜토리얼, `spec`은 그 언어의 공개 계약, `internals`는 구현·검증 기준,
`draft`는 아직 닫히지 않은 항목이다.

## 4. 유지 규칙

- 공통 의미를 바꿀 때는 `spec/`을 먼저 고치고, 언어 문서는 링크로 연결한다.
- 언어별 문서는 공통 의미를 해당 언어의 시그니처와 샘플로만 구체화한다.
- 새 문서를 추가하면 이 목록과 해당 디렉토리 `README.ko.md`를 함께 갱신한다.
- 아직 닫히지 않은 항목은 정식 문서에 섞지 않고 해당 `draft/`로 분리한다.
- 구현 계획과 worklog는 [plan/](./plan)에 둔다.
