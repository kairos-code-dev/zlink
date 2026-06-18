<!-- framework-adapter-nav:start -->
[문서 목록](./README.ko.md) | [다음: ZLink Framework 공통 스펙](./spec/README.ko.md)
<!-- framework-adapter-nav:end -->

# ZLink Framework 문서

이 디렉토리는 `ZLink Framework`의 문서 진입점이다. 문서는 세 범주로 나뉜다.

- **공통 스펙** (`spec/`) — 언어와 무관한 정식 계약. 상호작용 모델, 메시지
  모델, channel topology, framework API, actor 모델, use case를 정의한다.
- **구현 계획** (`plan/`) — 정식 스펙과 sample scenario를 실제 코드, 테스트,
  sample runner로 옮길 때의 순서와 완료 기준을 정의한다.
- **언어별 문서** (`../languages/<lang>/doc/`) — 공통 스펙을 각 언어와 대표
  프레임워크 API로 구체화한다. 공통 의미를 재정의하지 않는다.

`.NET`, `C++`, `Java/Kotlin`, `Node.js` 네 언어가 정식 문서로 제공된다.

## 1. 읽는 순서

1. [공통 스펙 목록](./spec/README.ko.md)
2. [Use case 목록](./spec/use-cases/README.ko.md)
3. [Use case 검증](./spec/usecase-validation.ko.md)
4. [구현 계획](#3-구현-계획)
5. [언어별 문서](#4-언어별-문서)

## 2. 공통 스펙

| 문서 | 다루는 범위 |
|------|-------------|
| [공통 스펙 목록](./spec/README.ko.md) | 공통 스펙 전체 목록과 읽는 순서, 네이밍 정책 |
| [개요](./spec/overview.ko.md) | Framework의 목적과 우선 범위 |
| [상호작용 모델](./spec/interaction-model.ko.md) | request-response, command, publish-subscribe 같은 사용자 모델 |
| [메시지 모델](./spec/message-model.ko.md) | header/payload 구조와 metadata 정책 |
| [Channel topology](./spec/channel-topology.ko.md) | channel grouping, discovery, 수동 연결, 내부 transport 매핑 |
| [Framework API](./spec/framework-api.ko.md) | 언어별 framework API의 공통 방향 |
| [비동기 실행 정책](./spec/async-execution-policy.ko.md) | async submit, blocking 대안 금지, coroutine/adapter의 공통 의미 |
| [Actor 모델](./spec/actor-model.ko.md) | actor 위치, session binding, Entry Spot, user Spot, dispatch 기준 |
| [Session Actor Dispatch](./spec/session-actor-dispatch.ko.md) | session과 actor를 연결하는 helper와 routing 정책 |
| [공통 샘플 시나리오](./spec/sample/README.ko.md) | 정본 6종(Bingo, TicTacToe, SupportChat, DeliveryDispatch, ShoppingMall, GameQuest)의 언어 중립 샘플 기준 |
| [Use case 목록](./spec/use-cases/README.ko.md) | use case 문서 전체 목록과 관리 규칙 |
| [Use case 검증](./spec/usecase-validation.ko.md) | 현재 스펙이 use case를 얼마나 설명하는지 점검 |

언어 중립 샘플 설계는 [spec/sample/](./spec/sample/README.ko.md)에 둔다. 정본 6종은
모든 framework 언어(dotnet/java/kotlin/node/cpp)가 같은 역할 분리·메시지 이름·smoke
검증 순서로 구현한다. Bingo는 분리된 Session/API/Play gateway 구조를, TicTacToe는 API와
Play 서버만으로 구성한 직접 play 연결 구조를 맡는다.

## 3. 구현 계획

| 문서 | 다루는 범위 |
|------|-------------|
| [Framework codec extension 통합 계획](./plan/framework-codec-extension-unification-plan.ko.md) | JSON 기본값, Protobuf/MessagePack 선택 extension, custom codec, connector/HTTP client 적용 구조를 정리하는 순서 |

## 4. 언어별 문서

| 언어 | 상태 | 진입점 |
|------|------|--------|
| `.NET` | 정식 | [languages/dotnet/doc/README.ko.md](../languages/dotnet/doc/README.ko.md) |
| `Java/Kotlin` | 정식 | [languages/java/doc/README.ko.md](../languages/java/doc/README.ko.md) |
| `Node.js` | 정식 | [languages/node/doc/README.ko.md](../languages/node/doc/README.ko.md) |
| `C++` | 정식 | [languages/cpp/doc/README.ko.md](../languages/cpp/doc/README.ko.md) |

각 언어 문서는 `doc/{guide,spec,internals}`로 나뉜다. `guide`는 샘플과
튜토리얼, `spec`은 그 언어의 공개 계약, `internals`는 구현·검증 기준이다.

## 5. 유지 규칙

- 공통 의미를 바꿀 때는 `spec/`을 먼저 고치고, 언어 문서는 링크로 연결한다.
- 언어별 문서는 공통 의미를 해당 언어의 시그니처와 샘플로만 구체화한다.
- 새 문서를 추가하면 이 목록과 해당 디렉토리 `README.ko.md`를 함께 갱신한다.
- 구현 계획과 worklog는 [plan/](./plan)에 둔다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](./README.ko.md) | [다음: ZLink Framework 공통 스펙](./spec/README.ko.md)
<!-- framework-adapter-nav:bottom:end -->
