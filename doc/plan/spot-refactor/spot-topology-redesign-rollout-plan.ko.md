# SPOT Topology Redesign Rollout Plan

이 문서는 구현 순서와 검증 게이트를 정리한 **실행 계획 문서**다.
설계 자체는 아래 draft 문서를 기준으로 삼는다.

- [SPOT Topic / Routed Topology Redesign Draft](../../draft/spot-topology-redesign.ko.md)

이 plan의 목적은 "무엇을 만들 것인가"를 다시 설명하는 것이 아니라, 그 설계를
**사용자 개입 없이 끝까지 반영하는 순서와 통과 조건**을 고정하는 것이다.

핵심 원칙은 아래와 같다.

1. 먼저 core를 바꾼다.
2. draft 문서 기준으로 core 반영 여부를 반복 리뷰한다.
3. 그 다음 `core/src` 전체를 POSD 기준으로 반복 리팩토링한다.
4. 모든 테스트, sample, perf를 통과시킨다.
5. 그 뒤에만 정식 문서와 bindings를 반영한다.

---

## 1. core 변경 뒤 문서 적용 리뷰 게이트

이번 작업은 pub/sub, routed, discovery, HWM, queue 정책을 함께 바꾸기 때문에,
core 코드가 일부만 바뀐 상태에서 bindings나 정식 문서로 넘어가면 빠진 항목을
놓치기 쉽다.

따라서 구현 순서는 아래처럼 고정한다.

1. 먼저 core를 변경한다.
2. core 빌드와 core 테스트를 다시 돌린다.
3. 그 다음 draft 문서를 기준으로 code review를 수행한다.
4. 문서의 각 항목이 실제 core 코드와 공개 헤더에 반영되었는지 하나씩 대조한다.
5. 미적용 항목이 하나라도 있으면 다시 core를 수정한다.
6. 수정 뒤 다시 build / test / review를 반복한다.
7. 미적용 항목이 `0`이 될 때까지 이 루프를 반복한다.
8. 그 뒤에만 다음 단계로 넘어간다.

즉 이 단계의 목적은 "core를 한 번 고쳤다"가 아니라, "draft의 모든 구현
요구사항이 core에 반영된 것이 확인됐다"를 통과 조건으로 삼는 것이다.

### 1.1 리뷰 체크 항목

core 변경 뒤 review에서는 최소 아래를 같이 확인해야 한다.

- `core/include/zlink.h`
- `core/include/zlink_enum.h`
- SPOT pub/sub 구현
- SPOT routed 구현
- discovery / manual peer lifecycle 구현
- auto HWM / manual HWM mapping 구현
- local delivery queue hard limit 구현
- snapshot / query / perf detail 노출

그리고 각 review 반복에서 아래를 기록해야 한다.

- 이번 반복에서 확인한 문서 항목
- 실제 반영된 코드 위치
- 아직 반영되지 않은 항목
- 다음 반복에서 수정할 항목

핵심은 "대충 대부분 반영됨" 상태로 다음 단계로 넘어가지 않는 것이다.

---

## 2. POSD 기반 리팩토링 게이트

문서 항목이 모두 core에 반영된 뒤에도 바로 bindings나 정식 문서 단계로 넘어가지
않는다. 그 다음에는 `core/src` 전체를 대상으로 POSD 기반 리팩토링 단계를
수행해야 한다.

이 단계의 목적은 "기능이 들어갔다"에서 멈추지 않고, 새 구조가 실제로도 변경
파급이 낮고 책임이 분명한 형태로 정리되었는지 확인하는 것이다.

범위는 아래처럼 고정한다.

- `core/src/api/`
- `core/src/core/`
- `core/src/sockets/`
- `core/src/services/`
- 그 밖의 `core/src` 전체

즉 특정 spot 파일 몇 개만 보는 것이 아니라, 이번 변경과 닿는 `core/src` 전체를
대상으로 본다.

### 2.1 POSD 리뷰 기준

이 단계는 저장소의 POSD 원칙을 그대로 따른다. 최소 아래 항목을 반복해서 본다.

1. 깊은 모듈
   - 인터페이스는 단순한데 구현 복잡도를 내부에서 충분히 흡수하는가
2. 정보 은닉
   - peer wiring, aggregate subscription, routed envelope, HWM 적용 규칙 같은
     설계 지식이 한 모듈 안에 가둬져 있는가
3. 복잡성을 아래로
   - 호출자가 알아야 할 내부 socket 평면 지식이 불필요하게 새어 나오지 않는가
4. 오류를 정의로 없애기
   - 특수 예외 경로를 API 또는 내부 계약 정리로 줄일 수 있는가
5. 위험 신호 제거
   - pass-through 메서드
   - 시간 순서대로만 나뉜 얕은 helper
   - 특수 코드와 범용 코드가 한 함수에 섞인 구조
   - 같은 의미가 여러 파일에 중복된 구조

### 2.2 POSD 리팩토링 반복 절차

이 단계는 아래 루프를 따른다.

1. `core/src` 전체에서 POSD 위험 신호를 먼저 열거한다.
2. 각 위험 신호가 어떤 POSD 원칙에 어긋나는지 기록한다.
3. 수정 방향을 두 가지 이상 검토하고, 더 나은 쪽을 선택한다.
4. core 코드를 수정한다.
5. 다시 build / test를 돌린다.
6. 다시 `core/src` 전체를 리뷰한다.
7. 아직 남은 POSD 기반 리팩토링 항목이 있으면 다시 반복한다.
8. 더 이상 진행할 POSD 기반 리팩토링 항목이 없다고 리뷰 로그에 기록된
   상태에서만 다음 단계로 넘어간다.

### 2.3 POSD hotspot

이번 변경 성격상 아래는 특히 다시 보기 쉬운 hotspot이다.

- discovery / manual peer lifecycle 경로가 서로 다른 지식을 중복하고 있지 않은가
- pub/sub aggregate subscription 관련 상태와 replay 로직이 여러 모듈에 흩어지지
  않았는가
- `internal-router` / `external-router` 책임이 다시 섞이지 않았는가
- HWM 계산과 수동 HWM mapping 지식이 여러 파일에 분산되지 않았는가
- local delivery queue / hard limit / disconnect 정책이 여러 helper에 얕게 퍼지지
  않았는가

---

## 3. 전체 검증 게이트

core 구현 반영과 POSD 기반 리팩토링이 끝난 뒤에는 최종 검증 게이트를 통과해야
한다. 이 단계의 목적은 실제 빌드 산출물과 sample, perf 경로까지 모두 정상
동작함을 확인하는 것이다.

이 단계는 아래 순서를 따른다.

1. core를 다시 빌드한다.
2. core 테스트를 모두 실행한다.
3. bindings/c native header와 runtime library를 먼저 동기화한다.
4. `bindings/c/samples`를 빌드하고 스모크 테스트를 수행한다.
5. `bindings/c/perf` single 전체 패턴 smoke를 수행한다.
6. `bindings/c/perf` multi 전체 패턴 smoke를 수행한다.
7. bindings 전체 테스트를 다시 실행한다.
8. 하나라도 실패하면 원인 분석 뒤 core 또는 binding을 수정하고 다시 반복한다.
9. 모든 검증이 성공할 때만 다음 단계로 넘어간다.

### 3.1 최소 통과 조건

- core 전체 테스트 성공
- bindings 전체 테스트 성공
- `bindings/c/samples` 스모크 테스트 성공
- `bindings/c/perf` single 전체 패턴 스모크 성공
- `bindings/c/perf` multi 전체 패턴 스모크 성공

특히 perf는 일부 패턴만 통과해서는 안 된다.

- single: 전체 패턴
- multi: 전체 패턴

### 3.2 같이 확인할 것

- perf runner가 실제 `core/build` runtime을 사용하고 있는지
- sample과 perf가 새 enum 이름, 새 HWM 옵션, 새 socket snapshot 이름을 따라가는지
- local delivery hard limit 정책이 sample/perf에서 unexpected hang이나 global
  pause를 만들지 않는지
- discovery 기반 auto-connect와 manual peer 경로가 sample/perf 양쪽에서 모두
  정상 동작하는지

---

## 4. 정식 문서 반영 게이트

전체 검증 게이트를 통과한 뒤에만 정식 문서를 수정한다. 이 순서를 강제하는 이유는
draft 설계를 먼저 정식 문서에 섞어 넣으면, 아직 검증되지 않은 계약과 설명이
공개 문서에 들어가 버리기 때문이다.

이 단계에서는 아래 문서를 순서대로 반영한다.

1. `doc/internals/`
2. `doc/spec/`
3. `doc/guide/`
4. `doc/spec/bindings/`

특히 `doc/spec/bindings/언어별/` 문서는 반드시 최신화해야 한다.

- 새 enum 이름
- 새 HWM 옵션 이름
- 제거된 이름
- 새 snapshot / query 노출
- discovery / peer 연결 의미 변화

즉 core와 perf가 먼저 검증된 뒤, 그 결과를 기준으로 정식 문서를 갱신한다.

---

## 5. bindings native 동기화 게이트

정식 문서를 반영한 뒤에는 각 바인딩의 native 폴더를 최신 core 기준으로
동기화해야 한다. 이 단계는 바인딩 수정 전에 먼저 수행해야 한다.

순서는 아래와 같다.

1. `core/include/` 공개 헤더 최신화 확인
2. `core/build` runtime 최신화 확인
3. 각 바인딩의 `native` 폴더에 최신 header / library 동기화

대상은 언어별 바인딩 전체다.

- `bindings/c`
- `bindings/cpp`
- `bindings/go`
- `bindings/python`
- `bindings/rust`
- `bindings/node`
- `bindings/java`
- `bindings/dotnet`
- 그 밖의 저장소 내 언어별 바인딩

---

## 6. bindings 라이브러리 반영 게이트

native 동기화가 끝난 뒤에는 각 바인딩 라이브러리를 새 계약에 맞춰 수정해야 한다.

이 단계에서 최소 아래를 반영한다.

- 새 enum 이름 반영
- 제거된 enum / API 이름 정리
- 새 HWM 옵션 이름 반영
- snapshot / query 구조 변화 반영
- discovery / peer 연결 의미 변화 반영
- sample / helper / wrapper 코드 최신화

특히 `doc/spec/bindings/언어별/` 문서와 실제 바인딩 public surface가 서로 다르면
안 된다. 문서와 라이브러리를 같이 맞춰야 한다.

---

## 7. bindings 검증 게이트

각 바인딩 라이브러리를 반영한 뒤에는 언어별로 다시 빌드, 테스트, sample, perf
검증을 수행해야 한다.

이 단계는 언어별로 아래 순서를 따른다.

1. native 폴더가 최신 core header / library를 보고 있는지 확인
2. 해당 바인딩 라이브러리 빌드
3. 해당 바인딩 전체 테스트 실행
4. 해당 바인딩의 sample 디렉터리가 있으면 sample 실행 확인
5. 해당 바인딩의 perf 디렉터리가 있으면 perf 스모크 테스트 실행

최소 확인 대상은 아래와 같다.

- C
  - 테스트
  - `bindings/c/samples`
  - `bindings/c/perf` single 전체 패턴
  - `bindings/c/perf` multi 전체 패턴
- C++
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행
- Go
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행
- Python
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행
- Rust
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행
- Node
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행
- Java
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행
- Dotnet
  - 테스트
  - sample 디렉터리가 있으면 실행 확인
  - perf 디렉터리가 있으면 스모크 테스트 실행

---

## 8. 최종 완료 조건

이번 작업의 최종 완료 조건은 아래를 모두 만족하는 상태다.

1. core 구현이 draft 문서와 1:1로 맞는다.
2. `core/src` 전체에 대해 POSD 기반 리팩토링이 더 이상 남아 있지 않다.
3. core 테스트, `bindings/c/samples`, `bindings/c/perf` single/multi 전체 패턴이
   모두 성공한다.
4. `doc/internals`, `doc/spec`, `doc/guide`, `doc/spec/bindings/언어별/` 문서가
   최신화되어 있다.
5. 각 바인딩의 native 폴더가 최신 core 계약과 runtime을 반영한다.
6. 각 바인딩 라이브러리가 최신 공개 계약을 반영한다.
7. 각 바인딩의 모든 테스트가 성공한다.
8. 각 바인딩의 sample 디렉터리가 저장소에 존재하면 실행까지 확인하고, perf
   디렉터리가 저장소에 존재하면 perf 스모크 테스트까지 성공한다.

이 조건을 모두 통과하기 전에는 작업을 완료로 보지 않는다.
