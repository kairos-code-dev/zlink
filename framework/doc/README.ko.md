<!-- framework-adapter-nav:start -->
[문서 목록](README.ko.md) | [다음: ZLink Framework 공통 스펙](framework/common/README.ko.md)
<!-- framework-adapter-nav:end -->

# ZLink Framework 문서

이 디렉토리는 `ZLink Framework` 문서의 진입점이다. 문서는 **컴포넌트별**로 나뉜다.

| 컴포넌트 | 진입점 | 내용 |
|---------|--------|------|
| **Framework adapter** | [framework/common/](framework/common/README.ko.md) | 언어 중립 공통 스펙(`framework/common/`) + 언어별 guide·spec·internals(`framework/<lang>/`) |
| **HTTP Client** | [http-client/](http-client/dotnet/README.ko.md) | 언어별 HTTP client 문서(`http-client/<lang>/`) |
| **Stream Connector** | [stream-connector/](stream-connector/cpp/guide/INDEX.ko.md) | C++ Stream Connector 라이브러리 문서(`stream-connector/cpp/`) |

아래는 **Framework adapter** 컴포넌트 상세다. `framework/common/`이 언어 중립 정식 계약,
`framework/<lang>/`이 각 언어 표면이다. `.NET`·`C++`·`Java/Kotlin`·`Node.js` 네 언어가
정식 문서로 제공된다.

## 1. 읽는 순서

1. [공통 스펙 목록](framework/common/README.ko.md)
2. [Use case 목록](framework/common/use-cases/README.ko.md)
3. [Use case 검증](framework/common/usecase-validation.ko.md)
4. [구현 계획](#3-구현-계획)
5. [언어별 문서](#4-언어별-문서)

## 2. 공통 스펙

| 문서 | 다루는 범위 |
|------|-------------|
| [공통 스펙 목록](framework/common/README.ko.md) | 공통 스펙 전체 목록과 읽는 순서, 네이밍 정책 |
| [개요](framework/common/overview.ko.md) | Framework의 목적과 우선 범위 |
| [상호작용 모델](framework/common/interaction-model.ko.md) | request-response, command, publish-subscribe 같은 사용자 모델 |
| [메시지 모델](framework/common/message-model.ko.md) | header/payload 구조와 metadata 정책 |
| [Channel topology](framework/common/channel-topology.ko.md) | channel grouping, discovery, 수동 연결, 내부 transport 매핑 |
| [Framework API](framework/common/framework-api.ko.md) | 언어별 framework API의 공통 방향 |
| [비동기 실행 정책](framework/common/async-execution-policy.ko.md) | async submit, blocking 대안 금지, coroutine/adapter의 공통 의미 |
| [Actor 모델](framework/common/actor-model.ko.md) | actor 위치, session binding, Entry Spot, user Spot, dispatch 기준 |
| [Session Actor Dispatch](framework/common/session-actor-dispatch.ko.md) | session과 actor를 연결하는 helper와 routing 정책 |
| [공통 샘플 시나리오](framework/common/sample/README.ko.md) | 정본 6종(Bingo, TicTacToe, SupportChat, DeliveryDispatch, ShoppingMall, GameQuest)의 언어 중립 샘플 기준 |
| [Use case 목록](framework/common/use-cases/README.ko.md) | use case 문서 전체 목록과 관리 규칙 |
| [Use case 검증](framework/common/usecase-validation.ko.md) | 현재 스펙이 use case를 얼마나 설명하는지 점검 |

언어 중립 샘플 설계는 [framework/common/sample/](framework/common/sample/README.ko.md)에 둔다. 정본 6종은
모든 framework 언어(dotnet/java/kotlin/node/cpp)가 같은 역할 분리·메시지 이름·smoke
검증 순서로 구현한다. Bingo는 분리된 Session/API/Play gateway 구조를, TicTacToe는 API와
Play 서버만으로 구성한 직접 play 연결 구조를 맡는다.

## 3. 구현 계획

| 문서 | 다루는 범위 |
|------|-------------|
| [Framework codec extension 통합 계획](plan/framework-codec-extension-unification-plan.ko.md) | JSON 기본값, Protobuf/MessagePack 선택 extension, custom codec, connector/HTTP client 적용 구조를 정리하는 순서 |

## 4. 언어별 문서

| 언어 | 상태 | 진입점 |
|------|------|--------|
| `.NET` | 정식 | [framework/dotnet/README.ko.md](framework/dotnet/README.ko.md) |
| `Java/Kotlin` | 정식 | [framework/java/README.ko.md](framework/java/README.ko.md) |
| `Node.js` | 정식 | [framework/node/README.ko.md](framework/node/README.ko.md) |
| `C++` | 정식 | [framework/cpp/README.ko.md](framework/cpp/README.ko.md) |

각 언어 문서는 `framework/<lang>/{guide,spec,internals}`로 나뉜다. `guide`는 샘플과
튜토리얼, `spec`은 그 언어의 공개 계약, `internals`는 구현과 검증 기준이다.
언어별 framework 문서는 모두 이 디렉토리 아래의 `framework/<lang>/`에서 관리한다.
`framework/languages/<lang>/doc/` 아래에 새 문서를 만들지 않는다. 나중에 언어별 guide,
spec, internals를 수정할 때도 이 위치를 기준으로 진행한다.

## 5. 유지 규칙

- 공통 의미를 바꿀 때는 `framework/common/`을 먼저 고치고, 언어 문서는 링크로 연결한다.
- 언어별 문서는 공통 의미를 해당 언어의 시그니처와 샘플로만 구체화한다.
- 새 문서를 추가하면 이 목록과 해당 디렉토리 `README.ko.md`를 함께 갱신한다.
- 구현 계획과 worklog는 [plan/](plan)에 둔다.
- HTTP Client 문서는 [http-client/<lang>/](http-client/dotnet/README.ko.md)에서 관리한다.

---
<!-- framework-adapter-nav:bottom:start -->
[문서 목록](README.ko.md) | [다음: ZLink Framework 공통 스펙](framework/common/README.ko.md)
<!-- framework-adapter-nav:bottom:end -->
