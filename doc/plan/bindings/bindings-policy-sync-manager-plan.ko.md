# `bindings/*` 정책 동기화 + POSD 리팩토링 감독 계획

> 성격: 이 문서는 구현 로그가 아니라, 현재 main Codex가 **감독(manager)** 으로서
> 언어별 하위 Codex 에이전트들에게 작업을 분배하고, 응답을 직접 리뷰한 뒤,
> 1차로 `bindings/README.md` 정책 문서의 미구현/불일치 항목을 전부 구현하게 하고,
> 2차로 POSD 원칙 기반 리팩토링을 반복 지시하는
> **감독용 실행 기준 문서**다.
>
> source of truth:
> - `bindings/README.md`
> - `core/include/zlink.h`
>
> 이번 라운드의 핵심은 각 언어 바인딩이 제각각 가진 public surface, ownership,
> option facade, naming, failure contract, service/monitor exposure를
> `bindings/README.md` 기준으로 다시 정렬하는 것이다.
> 감독과 하위 에이전트는 “기존 구현 관성”보다 정책 문서를 우선하며,
> 특히 POSD 구조 원칙, public surface 중복 제거, raw option bag 비노출,
> canonical naming, core 계약 준수를 최우선으로 감사해야 한다.
>
> 추가 최우선 원칙:
> - 모든 `bindings/*` 라이브러리는 **가능한 한 고성능으로 동작하도록 구현되어야 한다.**
> - 정책 동기화와 POSD 리팩토링은 readability나 표면 정리만이 목적이 아니며,
>   불필요한 allocation, 복사, wrapper hop, hidden conversion, needless abstraction,
>   hot path branch를 줄여 실제 런타임 비용을 낮추는 방향이어야 한다.
> - 문서 정책을 만족하더라도 hot path 성능을 의미 있게 악화시키는 구현은 완료로 보지 않는다.
> - 다만 zero-copy, ownership move, buffer reuse, batching 같은 성능 기법은
>   **절대 규칙이 아니라 참고 기준**이다. 각 언어 런타임, safety, ownership model,
>   API 명확성, 유지보수성, 실제 hot path 특성에 맞을 때 적용한다.
>
> 대상 범위:
> - `bindings/cpp`
> - `bindings/dotnet`
> - `bindings/go`
> - `bindings/java`
> - `bindings/node`
> - `bindings/python`
> - `bindings/rust`

## 0. 최우선 운영 규칙

- main Codex는 감독 역할만 수행한다.
- 실제 구현 변경은 반드시 언어별 Codex 에이전트에게 작업 요청을 내려 수행하게 한다.
- 하위 에이전트가 “완료”라고 보고해도 감독은 직접 파일과 테스트 근거를 읽고 리뷰한다.
- 모든 정책 구현과 리팩토링은 **bindings 런타임 성능 보존 또는 개선**을 전제로 한다.
- API 표면 정리 때문에 불필요한 복사, boxing, heap allocation, string 변환, virtual indirection,
  callback hop, lock 경합이 hot path에 추가되면 정책 준수만으로 완료 처리하지 않는다.
- 성능 개선 기법은 무조건 적용하지 않는다. 언어별 제약과 correctness, safety, API 단순성을 함께 보고
  상황에 맞는 수단만 채택한다.
- 각 언어는 반드시 **2단계 순서**로 처리한다.
  1. `bindings/README.md` 정책 문서 미구현/불일치 항목 전부 수정
  2. POSD 원칙 기반 추가 리팩토링
- 1단계 감독 리뷰에서 남은 정책 누락이 0건이 되기 전에는 2단계로 넘어가지 않는다.
- 2단계에서는 `bindings/README.md`의 `POSD Structure Policy` 및 관련 public surface 규칙을 기준으로,
  복잡도 감소, 의미 중복 제거, ownership 명확화, change amplification 감소가
  더 가능하면 반복해서 수정하게 한다.
- 감독 리뷰에서 추가 누락, 구조 문제, 문서 불일치가 발견되면 완료 처리하지 않고
  재지시한다.
- 특정 언어에 대해 문서의 모든 내용이 구현되었고, POSD 리팩토링 여지도 감독 리뷰상
  더 없으며, 해당 언어 검증까지 확인되었을 때만 완료 처리한다.
- 문서 해석이 애매하면 해당 언어의 기존 관례보다 `bindings/README.md`와
  `core/include/zlink.h`를 우선한다.

## 1. 이번 라운드의 목표

- 모든 언어 바인딩을 `bindings/README.md` 기준으로 다시 감사한다.
- 먼저 각 언어별로 정책이 아직 적용되지 않은 부분을 **명시적 리스트**로 만들고 수정한다.
- 그 다음 POSD 원칙에 따라 public surface와 내부 구조를 다시 정리한다.
- 이 전체 작업은 각 바인딩이 가능한 한 낮은 오버헤드와 높은 처리량, 낮은 지연 특성을 유지하도록 만드는 것을 전제로 한다.
- 감독은 하위 에이전트의 응답을 리뷰하고, 필요하면 추가 지시사항을 내려 이 과정을 반복한다.
- 모든 언어가 1단계와 2단계를 모두 끝낼 때까지 종료하지 않는다.

## 2. 감독 진행표

감독 에이전트는 아래 표를 작업 보드로 사용하고, 각 단계 상태를 직접 갱신한다.

상태 규칙:
- `pending`: 아직 시작 전
- `in_progress`: 현재 작업 요청/리뷰/재지시 중
- `rework`: 감독 리뷰 결과 추가 수정 지시가 내려간 상태
- `blocked`: 선행 이슈 때문에 현재 단계 진행 불가
- `failed`: 검증 근거 확인 실패
- `done`: 감독 리뷰상 남은 작업 없음

| 대상 | 1단계 정책 구현 | 1단계 감독 리뷰 | 2단계 POSD 리팩토링 | 2단계 감독 리뷰 | 검증 확인 | 최종 상태 |
|------|------------------|------------------|----------------------|------------------|-----------|-----------|
| `bindings/cpp` | pending | pending | pending | pending | pending | pending |
| `bindings/dotnet` | pending | pending | pending | pending | pending | pending |
| `bindings/go` | pending | pending | pending | pending | pending | pending |
| `bindings/java` | pending | pending | pending | pending | pending | pending |
| `bindings/node` | pending | pending | pending | pending | pending | pending |
| `bindings/python` | pending | pending | pending | pending | pending | pending |
| `bindings/rust` | pending | pending | pending | pending | pending | pending |

## 3. 작업 순서

감독은 아래 고정 순서로 진행한다.

1. `bindings/cpp`
2. `bindings/dotnet`
3. `bindings/go`
4. `bindings/java`
5. `bindings/node`
6. `bindings/python`
7. `bindings/rust`

각 대상은 아래 운영 루프를 따른다.

1. 1단계 정책 구현 작업을 해당 언어 Codex 에이전트에게 지시한다.
2. 에이전트가 `bindings/README.md`를 기준으로 정책 미구현/불일치 목록을 만들고 수정한다.
3. 에이전트가 검증 결과와 함께 응답한다.
4. 감독이 직접 리뷰한다.
5. 정책 미구현 또는 문서 불일치가 남아 있으면 1단계를 다시 지시한다.
6. 1단계 감독 리뷰에서 남은 항목이 0개일 때 2단계 POSD 리팩토링을 지시한다.
7. 에이전트가 구조 단순화와 중복 제거를 수행하고 검증 결과와 함께 응답한다.
8. 감독이 직접 리뷰한다.
9. 의미 있는 리팩토링 항목이 남아 있으면 2단계를 다시 지시한다.
10. 더 이상 정책 누락도 없고, 더 이상 의미 있는 POSD 리팩토링 항목도 없을 때만 해당 언어를 완료 처리한다.

## 4. 언어별 담당 및 기본 검증 진입점

감독은 언어별로 에이전트 ownership을 고정하고, 리뷰와 재지시도 같은 ownership 단위로 반복한다.

| 대상 | 담당 에이전트 | 주 검토 범위 | 기본 검증 진입점 |
|------|----------------|--------------|------------------|
| `bindings/cpp` | `codex-cpp-binding-agent` | C++ public surface, typed options, service/monitor, ownership | `bindings/cpp/tests/run_tests.sh` |
| `bindings/dotnet` | `codex-dotnet-binding-agent` | .NET public API, facade/value object, service layer, docs/samples alignment | `bindings/dotnet/tests/run_tests.sh` |
| `bindings/go` | `codex-go-binding-agent` | Go surface, option/value object, error/failure contract, monitor/service | `go test ./...` |
| `bindings/java` | `codex-java-binding-agent` | Java API naming, builder/facade 구조, service/monitor, ownership | `bindings/java/tests/run_tests.sh` |
| `bindings/node` | `codex-node-binding-agent` | TS/JS surface, type declarations, socket hierarchy, docs/test alignment | `bindings/node/tests/run_tests.sh` |
| `bindings/python` | `codex-python-binding-agent` | Pythonic surface, value objects, ownership, sample/test alignment | `bindings/python/tests/run_tests.sh` |
| `bindings/rust` | `codex-rust-binding-agent` | Rust type system mapping, ownership/lifetime, surface/options/service | `cargo test` |

- 기본 검증 진입점은 출발점일 뿐이다.
- 수정 범위가 더 넓으면 surface test, typecheck, sample verification, 문서 정합성 확인을 추가해야 한다.
- 감독은 에이전트가 “무엇을 실행했는가”뿐 아니라 “왜 그 검증이 수정 범위를 커버하는가”까지 확인한다.
- 성능에 민감한 경로를 건드린 경우, 에이전트는 추가로 allocation/copy/hot path overhead 관점의 근거를 보고해야 한다.

## 5. 감독과 하위 에이전트의 역할 분리

### 5.1 main Codex 감독 역할

- 대상 언어별로 작업 요청을 생성한다.
- 하위 에이전트가 제출한 불일치 목록이 충분한지 먼저 본다.
- 수정 결과가 문서 요구사항을 실제로 충족하는지 파일 단위로 리뷰한다.
- 수정이 hot path 성능을 악화시키지 않았는지 확인한다.
- 리뷰에서 누락, 과잉 노출, naming 불일치, ownership 불명확성, raw surface 재노출,
  POSD 위반, 불필요한 성능 오버헤드가 보이면 추가 지시사항을 내린다.
- 각 단계 완료 여부를 표에 반영한다.

### 5.2 언어별 Codex 에이전트 역할

- 담당 언어 디렉터리만 책임지고 감사 및 수정한다.
- 먼저 정책 미구현/불일치 목록을 명시적으로 식별한 뒤 수정한다.
- 수정한 파일, 검증 명령, 남은 의심 지점을 보고한다.
- 성능 민감 경로를 변경한 경우 allocation, copy, abstraction cost 변화를 함께 보고한다.
- 감독 리뷰 결과가 오면 추가 지시사항을 반영해 다시 수정한다.

## 6. 1단계: 정책 문서 완전 구현

### 6.1 단계 목표

- 해당 언어 바인딩이 `bindings/README.md`의 의미 계약과 public API 정책을 정확히 따르도록 맞춘다.
- 문서에 정의된 Required 범위는 모두 구현하고, Target 범위는 해당 언어가 이미 구현 중인 경우
  정책대로 맞춘다.
- 이 과정에서 public API 정리와 구조 개선이 hot path 성능 저하로 이어지지 않게 한다.
- “대체로 비슷하다”가 아니라 감독 리뷰 기준으로 **정책 미구현 0건** 상태를 만든다.

### 6.2 에이전트 공통 지시

- 먼저 해당 언어 구현과 `bindings/README.md`를 비교해 **정책 미적용 항목 목록**을 만든다.
- 목록은 반드시 public surface, naming, option facade, error mapping, ownership,
  monitor/service exposure, coverage scope, POSD 위반 징후를 포함해 점검한다.
- 기존 구현이 문서와 충돌하면 구현을 바꾼다.
- 언어별 관례는 표현 방식에만 적용하고, 의미 계약은 바꾸지 않는다.
- raw option bag, legacy convenience, 얕은 compat wrapper, 숨은 failure path를
  public에 새로 추가하지 않는다.
- core 상태 오류를 바인딩이 임의 추론하거나 숨기지 않는다.
- 입력 값 검증, 범위 검증, overflow/truncation 방지는 바인딩에서 선제적으로 처리한다.
- 성능 민감 경로에서는 불필요한 복사, 임시 객체 생성, 문자열 재인코딩, 계층적 우회 호출을 피한다.

### 6.3 1단계 핵심 감사 축

- `Public API Coverage Scope`의 Required surface가 모두 존재하는가
- 구현된 Target surface가 있다면 정책 규칙을 따르는가
- out-of-scope API를 public에 노출하고 있지 않은가
- `Error Mapping`이 raw errno API 노출이 아니라 언어별 에러 타입으로 정리되어 있는가
- `Context Option Facade`와 `Socket Options`가 typed facade로 정리되어 있는가
- `Message` lifecycle과 diagnostic API가 canonical surface를 따르는가
- blocking / non-blocking naming 구분이 문서와 일치하는가
- 같은 능력을 여러 타입/이름/경로로 중복 노출하고 있지 않은가
- generic base에 socket-type-specific capability를 올려두지 않았는가
- TLS, monitor, service layer, registry/query client surface가 정책 설명과 일치하는가
- raw compat base, hidden transport switch, temporal sequencing 의존 API가 남아 있지 않은가
- send/recv, message ownership, callback, monitor, service hot path에 불필요한 allocation/copy/indirection이 추가되지 않았는가

### 6.4 고성능 구현 감사 체크리스트

아래 항목은 **절대 체크박스가 아니라 참고용 판단 기준**이다.
감독과 에이전트는 각 언어의 런타임 특성, ownership/safety 제약, API 복잡도,
실제 병목 위치를 고려해 적용 여부를 판단한다.

- send/recv hot path에서 avoid 가능한 heap allocation이 남아 있지 않은가
- message payload 전달이 가능한 곳에서 zero-copy 또는 ownership move 경로를 사용하고 있는가
- language-native buffer와 native message 사이에서 불필요한 재복사나 재인코딩이 없는가
- 작은 convenience API 때문에 공통 hot path에 boxing, wrapper object 생성, dynamic dispatch가 추가되지 않았는가
- callback 기반 API가 필요 이상으로 trampoline, closure capture, thread hop을 만들지 않는가
- FFI 경계에서 문자열/바이트 변환을 반복하지 않고 필요한 최소 횟수로 제한하는가
- routing id, topic, metadata 같은 반복 사용 값에 avoid 가능한 재가공 비용이 없는가
- option facade가 조회/설정 시 불필요한 map/dictionary/raw bag 경유를 만들지 않는가
- monitor/service 이벤트 경로가 디버그 편의용 래퍼 때문에 과도한 객체 생성을 유발하지 않는가
- recv 결과와 multipart 처리에서 buffer reuse 또는 ownership 이전 기회가 있는데 놓치고 있지 않은가
- batch 처리 또는 multipart 전송/수신에서 part별 중간 래핑이 과도하지 않은가
- lock, synchronized, mutex, channel hop 등 동기화 비용이 hot path에 불필요하게 들어가 있지 않은가
- 예외/에러 surface 정리가 정상 경로 비용 증가로 이어지지 않는가
- POSD 리팩토링 결과로 abstraction은 줄고 실제 런타임 비용도 함께 줄었는가

### 6.5 정책 섹션별 감사 체크리스트

- `Public API Coverage Scope`
- `Utility API`
- `Error Mapping`
- `Context Option Facade`
- `Message Diagnostic API`
- `Message Lifecycle Canonical Names`
- `Context Lifecycle Canonical Names`
- `TLS Handle Polymorphism`
- `Monitor Ready Contract`
- `POSD Structure Policy`
- `Public Surface Rules`
- 문서 하위의 socket/service/monitor/registry/query/poller/proxy 관련 세부 규칙

- 각 언어 에이전트는 위 항목을 기준으로 “적용 완료 / 불일치 / 비적용 사유”를 남겨야 한다.
- 감독은 불일치 목록이 특정 섹션에 편중되어 있으면, 누락된 섹션 재감사를 다시 지시한다.

### 6.6 감독 리뷰 체크리스트

- 에이전트가 제출한 정책 미적용 목록이 실제 누락 범위를 충분히 포착했는가
- 수정 결과가 `bindings/README.md`의 Required 의미 계약을 충족하는가
- public API 이름과 책임 경계가 `core/include/zlink.h` 기준으로 설명 가능한가
- option/value object가 깊은 모듈 역할을 하고 있는가
- 얕은 wrapper나 legacy alias가 그대로 남아 있지 않은가
- public surface가 언어 관례에 맞더라도 의미 계약을 바꾸지 않았는가
- 문서에 없는 C API를 임의로 public 노출하지 않았는가
- 성능 민감 경로에서 새 abstraction 비용이 과도하지 않은가

## 7. 2단계: POSD 기반 추가 리팩토링

### 7.1 단계 목표

- 1단계가 끝난 구현을 대상으로 `bindings/README.md`의 `POSD Structure Policy`와
  관련 public surface 규칙을 기준으로 추가 리팩토링을 수행한다.
- 추가 리팩토링은 구조 정리와 함께 hot path 비용 감소까지 목표로 한다.
- 감독 리뷰에서 더 이상 의미 있는 구조 개선 항목이 없다고 판단될 때까지 반복한다.

### 7.2 리팩토링 판단 기준

감독은 아래 항목이 남아 있으면 2단계 리팩토링을 다시 지시한다.

- 같은 능력을 여러 public 타입/메서드/alias로 반복 노출하고 있음
- shallow wrapper, compat layer, pass-through facade가 의미 없이 남아 있음
- option 검증, ownership 규칙, failure contract가 여러 위치에 분산되어 있음
- change amplification이 큰 구조가 남아 있음
- temporal decomposition이 심해 사용자가 호출 순서를 외워야 함
- generic base와 concrete type의 책임 경계가 흐려져 있음
- dead code, orphan helper, legacy naming, 반쯤 남은 migration layer가 남아 있음
- domain object/value object가 깊은 모듈이 아니라 단순 데이터 운반체로만 남아 있음
- hot path에서 avoid 가능한 allocation, copy, branch, lock, callback indirection이 남아 있음
- zero-copy/send ownership move/buffer reuse 기회가 있는데 API 또는 내부 구조 때문에 활용하지 못하고 있음
- batch 또는 multipart 처리에서 공통 경로가 part 단위 래핑/검사/변환을 반복하고 있음

### 7.3 리팩토링 시 금지사항

- 문서 의미 계약 변경
- 코어가 보장하지 않는 의미를 바인딩이 새로 추론해 추가
- 임시 alias나 backward-compat wrapper를 이유 없이 유지
- raw option bag, hidden switch, internal sequencing 노출 재도입
- 검증 통과만 위한 우회
- 성능 개선 명목으로 ownership/correctness/정책 계약을 깨는 최적화
- 상황에 맞지 않는 zero-copy, buffer reuse, batching을 억지로 도입해 API 명확성이나 안정성을 해치는 최적화

## 8. 감독 리뷰 라운드 기록 규칙

- 각 언어는 감독과 에이전트 간의 왕복을 라운드 단위로 남긴다.
- 라운드가 추가될 때마다 가장 마지막에 append 한다.
- 감독 리뷰에서 “추가 지시 없음”이 나오기 전까지 라운드를 종료로 보지 않는다.

라운드 기록 템플릿:

```text
### <target-dir> Round <n>
- 단계: 1단계 정책 구현 | 2단계 POSD 리팩토링
- 에이전트 보고 요약:
  - 불일치/리팩토링 항목:
  - 수정 파일:
  - 검증:
- 감독 리뷰 결과:
  - 승인 항목:
  - 재작업 항목:
  - 추가 지시사항:
- 상태 갱신:
  - 정책 구현:
  - 감독 리뷰:
  - 리팩토링:
  - 검증:
  - 최종 상태:
```

## 9. 검증 규칙

- 하위 에이전트는 해당 언어에 맞는 빌드, 테스트, 샘플, surface 검증 중
  정책 변경의 영향이 직접 드러나는 검증만 사용한다.
- 검증은 “최소 1회 실행”이 아니라 수정 범위를 실제로 커버해야 한다.
- 컴파일만 통과시키고 public surface 회귀를 확인하지 않은 경우 완료로 보지 않는다.
- 검증 실패, 미실행, 환경 제약은 모두 명시적으로 보고한다.
- 성능 민감 경로를 수정한 경우, 최소한 정성적이라도 성능 영향 평가를 포함해야 한다.

성능 영향 평가는 가능하면 아래 항목을 포함한다.

아래 항목 역시 참고용이다. 측정 가능성, 언어 특성, 변경 범위에 따라 일부만 적용할 수 있다.

- 변경 전 대비 allocation/copy 수 감소 또는 유지 근거
- zero-copy, ownership move, buffer reuse 적용 여부
- 새 abstraction이 hot path에 추가한 간접 호출/객체 생성/동기화 비용 유무
- 측정이 어려운 경우에도 코드 경로상 비용 증감에 대한 정성적 설명

## 10. 에이전트 보고 형식

- 발견한 정책 미적용/불일치 목록
- 발견한 추가 POSD 리팩토링 항목 목록
- 수정한 파일 목록
- 수행한 검증 명령
- 검증 결과 요약
- 성능 영향 평가
- 남아 있는 의심 지점 또는 미해결 항목

모호한 표현은 허용하지 않는다.

## 11. 에이전트 작업 요청 템플릿

### 11.1 1단계 요청 템플릿

```text
대상: <target-dir>

`bindings/README.md`와 `core/include/zlink.h`를 source of truth로 삼아
<target-dir> 구현을 감사하고, 정책 문서가 아직 적용되지 않은 부분을 전부 식별하고 수정하라.

요구사항:
- 먼저 정책 미적용/불일치 목록을 명시적으로 식별할 것
- public surface, naming, option facade, error mapping, ownership, monitor/service exposure를
  모두 점검할 것
- Required 범위는 전부 맞출 것
- 이미 구현된 Target 범위가 있다면 정책대로 정렬할 것
- raw option bag, legacy convenience, shallow wrapper를 public에 남기지 말 것
- core 계약과 다른 의미를 바인딩이 임의로 만들지 말 것
- hot path에서 avoid 가능한 allocation, copy, wrapper hop, hidden conversion도 함께 제거하거나 줄일 것
- zero-copy, ownership move, buffer reuse, batching 기회가 있으면 문서 계약을 깨지 않는 범위에서 검토하되,
  언어 특성과 구현 복잡도에 맞을 때만 적용할 것
- 검증 명령과 결과를 반드시 첨부할 것

보고 형식:
- 정책 미적용/불일치 목록
- 수정 파일 목록
- 수행한 검증 명령
- 검증 결과 요약
- 성능 영향 평가
- 남은 리스크 또는 미해결 항목
```

### 11.2 2단계 요청 템플릿

```text
대상: <target-dir>

1단계 정책 구현이 완료된 상태를 기준으로,
`bindings/README.md`의 `POSD Structure Policy`와 public surface 규칙에 따라
<target-dir>에 남아 있는 추가 리팩토링 항목을 찾아 수정하라.

요구사항:
- 복잡도 감소, ownership 명확화, 중복 제거, change amplification 감소 관점에서 감사할 것
- shallow wrapper, compat layer, orphan helper, legacy alias를 줄일 것
- generic base와 concrete type의 책임 경계를 다시 점검할 것
- 문서 의미 계약을 바꾸지 말 것
- raw surface와 hidden sequencing을 public에 재도입하지 말 것
- hot path의 allocation, copy, needless indirection도 계속 줄일 것
- zero-copy/send ownership move/buffer reuse/batching을 막는 구조가 있으면 검토하되,
  항상 제거 대상으로 단정하지 말고 실제 이득과 복잡도 trade-off를 보고 판단할 것
- 검증 명령과 결과를 반드시 첨부할 것

보고 형식:
- 추가 POSD 리팩토링 항목 목록
- 수정 파일 목록
- 수행한 검증 명령
- 검증 결과 요약
- 성능 영향 평가
- 더 남은 리팩토링 후보 또는 미해결 항목
```

## 12. 종료 조건

- 모든 대상 언어가 1단계 정책 구현을 끝냈다.
- 모든 대상 언어가 1단계 감독 리뷰에서 `done`이다.
- 모든 대상 언어가 2단계 POSD 리팩토링을 끝냈다.
- 모든 대상 언어가 2단계 감독 리뷰에서 `done`이다.
- 모든 대상 언어의 검증 근거가 확인되었다.
- 감독 리뷰 기준으로 더 이상 정책 미적용 항목도, 의미 있는 POSD 리팩토링 항목도 남아 있지 않다.
- 감독 리뷰 기준으로 성능을 의미 있게 악화시키는 구조나 구현이 남아 있지 않다.

이 조건을 모두 만족할 때만 본 라운드를 종료한다.
