# `core/perf` + `bindings/*/perf` PERF 정책 구현/리팩토링 감독 계획

> 성격: 이 문서는 구현 로그가 아니라, 현재 Codex가 **감독(manager)** 으로서
> 하위 Codex 에이전트들에게 작업을 분배하고, 결과를 직접 리뷰한 뒤,
> 1차로 PERF 문서 스펙 완전 구현을 끝내고,
> 2차로 PERF 문서의 리팩토링 원칙에 따른 추가 리팩토링을 반복 지시하는
> **감독용 실행 기준 문서**다.
>
> source of truth:
> - `doc/perf/PERF_POLICY.md`
> - `doc/perf/PERF_SINGLE_TEST_POLICY.md`
> - `doc/perf/PERF_MULTI_TEST_POLICY.md`
>
> 이번 PERF 정책 변경의 가장 큰 핵심은
> **perf 측정 surface가 recv only 기준으로 정리되었다는 점**이다.
> 감독과 하위 에이전트는 옵션, 실행기 구조, 결과 surface, metric 계약,
> smoke 검증 해석까지 모두 이 recv only 원칙을 최우선 기준으로 감사해야 한다.
>
> 대상 범위:
> - `core/perf`
> - `bindings/cpp/perf`
> - `bindings/dotnet/perf`
> - `bindings/go/perf`
> - `bindings/java/perf`
> - `bindings/node/perf`
> - `bindings/python/perf`
> - `bindings/rust/perf`

## 0. 최우선 운영 규칙

- 이번 라운드에서 가장 먼저 확인할 정책 변화는
  **perf가 recv only 기준으로 재정의되었다는 점**이다.
- 기존 send/attempt/inflight/outstanding 중심 해석이나 surface가 남아 있으면
  우선적으로 정책 불일치로 간주하고 제거 또는 수정한다.
- 감독은 직접 구현 완료 선언을 하지 않는다. 하위 Codex 에이전트가 수정했다고
  보고하면, 감독이 실제 파일과 테스트 결과를 읽고 직접 리뷰한다.
- 각 대상은 반드시 **2단계 순서**로 처리한다.
  1. PERF 문서 스펙 완전 구현
  2. PERF 정책 문서의 리팩토링 원칙에 따른 추가 리팩토링
- 1단계가 완료되기 전에는 2단계로 넘어가지 않는다.
- 2단계에서는 `doc/perf/PERF_POLICY.md` § 7.6 리팩토링 원칙을 기준으로,
  추가로 줄일 수 있는 복잡도, wrapper, 죽은 코드, ownership 불명확성,
  변경 증폭 지점이 남아 있는지 반복해서 찾고 수정하게 한다.
- 하위 에이전트가 “완료”라고 보고해도 감독 리뷰에서 추가 구현 누락이나 추가
  리팩토링 여지가 발견되면 완료로 처리하지 않고 다시 작업을 지시한다.
- 해당 대상에 대해 더 이상 스펙 미구현 내용이 없고, 더 이상 의미 있는
  리팩토링 항목도 없으며, smoke 테스트에도 문제가 없을 때만 그 perf 작업을
  완료 처리한다.
- 문서 해석이 애매하면 `README`나 기존 구현 관행보다 `doc/perf/*.md`를 우선한다.

## 1. 이번 라운드의 목표

- 갱신된 `doc/perf` 정책을 기준으로 `core/perf`와 모든 `bindings/*/perf`를 다시
  감사한다.
- 먼저 정책 스펙과 다른 구조, 옵션, 결과 surface, 실행 surface, 실패 처리,
  metric 계약, transport 처리, runner 동작을 모두 수정한다.
- 그 다음 PERF 정책의 리팩토링 원칙에 따라 추가 구조 개선이 필요한 부분을
  계속 찾아 수정한다.
- 각 대상 디렉터리에 대해 감독 리뷰 기준으로 더 이상 남은 작업이 없을 때까지
  반복한다.

## 2. 감독 진행표

감독 에이전트는 아래 표를 작업 보드로 사용하고, 각 단계 상태를 직접 갱신한다.

상태 규칙:
- `pending`: 아직 시작 전
- `in_progress`: 현재 작업/리뷰/재지시 중
- `rework`: 감독 리뷰 결과 추가 수정 지시가 내려간 상태
- `blocked`: 선행 이슈 때문에 현재 단계 진행 불가
- `failed`: smoke 테스트 또는 검증 근거 확인 실패
- `done`: 감독 리뷰상 남은 작업 없음

| 대상 | 1단계 스펙 구현 | 1단계 감독 리뷰 | 2단계 리팩토링 | 2단계 감독 리뷰 | smoke 확인 | 최종 상태 |
|------|------------------|------------------|----------------|------------------|------------|-----------|
| `core/perf` | pending | pending | pending | pending | pending | pending |
| `bindings/cpp/perf` | pending | pending | pending | pending | pending | pending |
| `bindings/dotnet/perf` | pending | pending | pending | pending | pending | pending |
| `bindings/go/perf` | pending | pending | pending | pending | pending | pending |
| `bindings/java/perf` | pending | pending | pending | pending | pending | pending |
| `bindings/node/perf` | pending | pending | pending | pending | pending | pending |
| `bindings/python/perf` | pending | pending | pending | pending | pending | pending |
| `bindings/rust/perf` | pending | pending | pending | pending | pending | pending |

## 3. 작업 순서

감독은 아래 고정 순서로 진행한다.

1. `core/perf`
2. `bindings/cpp/perf`
3. `bindings/dotnet/perf`
4. `bindings/go/perf`
5. `bindings/java/perf`
6. `bindings/node/perf`
7. `bindings/python/perf`
8. `bindings/rust/perf`

각 대상은 아래 운영 루프를 따른다.

1. 1단계 스펙 구현 작업을 에이전트에게 지시한다.
2. 에이전트가 스펙 미구현/정책 불일치 항목을 수정하고 smoke 테스트를 수행한다.
3. 감독이 직접 리뷰한다.
4. 스펙 미구현 또는 정책 불일치가 남아 있으면 1단계를 다시 지시한다.
5. 1단계 감독 리뷰에서 남은 항목이 0개일 때 2단계 리팩토링 작업을 지시한다.
6. 에이전트가 PERF 정책의 리팩토링 원칙에 따라 추가 리팩토링과 smoke 테스트를 수행한다.
7. 감독이 직접 리뷰한다.
8. 더 줄일 수 있는 구조 문제나 리팩토링 잔여 항목이 있으면 2단계를 다시 지시한다.
9. 더 이상 리팩토링 항목이 없고 smoke에도 문제가 없을 때만 해당 대상을 완료 처리한다.

## 4. 1단계: PERF 문서 스펙 완전 구현

### 4.1 단계 목표

- 대상 구현이 `doc/perf` 정책과 정확히 일치하도록 맞춘다.
- 문서와 다른 옵션, 결과 형식, 실패 처리, runner 동작, transport 처리, metric
  계약, environment surface를 모두 수정한다.
- “대체로 맞다”가 아니라 감독 리뷰 기준으로 **스펙 미구현 0건** 상태를 만든다.

### 4.2 에이전트 공통 지시

- 먼저 해당 디렉터리 구현과 `doc/perf` 정책 문서를 비교해 **정책 불일치 목록**을
  만든 뒤 수정한다.
- 수정은 “최소 변경”이 아니라 “정책 완전 준수”를 목표로 한다.
- 기존 구현이 정책과 충돌하면 구현을 바꾼다. 정책 위반을 문서화하거나 주석으로
  합리화하지 않는다.
- retry, workaround, 임시 우회, 실패 숨기기, `UNSUPPORTED` 오용을 추가하지
  않는다.
- 측정 의미를 바꾸는 리팩토링은 금지한다.

### 4.3 감독 리뷰 체크리스트

- 정책 준수 실행기가 core와 binding-local 실행기 규칙을 따르는가
- single과 multi entrypoint가 정책 문서와 같은 책임 분리를 가지는가
- RESULT line 계약이 정책 문서와 일치하는가
- 기본 perf surface가 정책 범위를 벗어나지 않는가
- cpu/mem, queue/debug/probe 계열이 기본 계약에 다시 섞여 있지 않은가
- retry/attempts 류 로직이 남아 있지 않은가
- inflight/outstanding 옵션/환경 변수가 남아 있지 않은가
- 정의된 transport 실패를 `UNSUPPORTED`로 숨기지 않는가
- core 라이브러리 버그를 perf 레이어 우회로 덮지 않는가

## 5. 2단계: PERF 리팩토링 원칙 기반 추가 리팩토링

### 5.1 단계 목표

- 1단계가 끝난 구현을 대상으로 `doc/perf/PERF_POLICY.md` § 7.6의 리팩토링
  원칙을 적용한다.
- 감독 리뷰에서 더 이상 의미 있는 리팩토링 항목이 없다고 판단될 때까지 반복한다.
- 단, perf 테스트의 의미를 해치지 않는 상태까지만 진행한다.
- 리팩토링의 목표는 코드 이동이 아니라 복잡도 감소, ownership 명확화,
  변경 증폭 감소, 정보 은닉 개선, 죽은 코드 제거다.

### 5.2 리팩토링 판단 기준

감독은 아래 항목이 남아 있으면 2단계 리팩토링을 다시 지시한다.

- 얕은 wrapper, pass-through 계층, 분기만 늘리는 구조가 남아 있음
- 죽은 코드, dead branch, orphan helper, 레거시 옵션이 남아 있음
- ownership/lifecycle/invariant 설명이 불명확한 구조가 남아 있음
- 패턴 의미, backpressure, routing, phase 의미가 helper 내부에 숨어 있음
- 공통화 이후 변경 증폭이 줄지 않고 여러 패턴이 한 helper에 과결합되어 있음
- hot path 의미를 읽기 어렵게 만드는 추상화가 남아 있음
- 정책이 요구하는 공통 구현 강제 대상이 제각각 흩어져 있음

### 5.3 리팩토링 시 금지사항

- 측정 의미 변경
- retry/workaround/실패 숨기기 추가
- inflight/outstanding 류 제어 재도입
- smoke 통과만 위한 임시 우회
- 정책에 없는 metric/result surface 재도입

## 6. smoke 테스트 규칙

- 하위 Codex 에이전트의 검증은 **perf smoke 테스트만** 사용한다.
- smoke 테스트의 정확한 의미는 `doc/perf/PERF_POLICY.md` § 3.2를 따른다.
- 즉, single과 multi를 각각 실행하고, `--pattern ALL --msg-sizes 64`를 사용하며,
  transport는 기본값 전체를 사용한다.
- 통과 기준은 모든 조합이 `fail` 없이 완료되어 `status=complete`가 되는 것이다.
- full perf, baseline 비교, 임의 축소 패턴 검증으로 대체하지 않는다.

## 7. 에이전트 보고 형식

- 발견한 스펙 미구현 또는 정책 불일치 목록
- 발견한 추가 리팩토링 항목 목록
- 수정한 파일 목록
- 수행한 smoke 테스트 명령
- 남아 있는 의심 지점 또는 미해결 항목

모호한 표현은 허용하지 않는다.

## 8. 에이전트 작업 요청 템플릿

### 8.1 1단계 요청 템플릿

```text
대상: <target-dir>

`doc/perf/PERF_POLICY.md`, `doc/perf/PERF_SINGLE_TEST_POLICY.md`,
`doc/perf/PERF_MULTI_TEST_POLICY.md`를 source of truth로 삼아
<target-dir> 구현을 감사하고, 정책 스펙과 다른 부분을 전부 수정하라.

요구사항:
- 먼저 스펙 미구현/정책 불일치 목록을 명시적으로 식별할 것
- 문서 정책과 다른 구현/옵션/metric/result/failure 처리/runner 동작을 수정할 것
- 측정 의미를 바꾸지 말 것
- retry, workaround, 실패 숨기기, `UNSUPPORTED` 오용을 추가하지 말 것
- 테스트는 perf smoke 테스트만 사용할 것
- smoke 의미와 통과 기준은 `doc/perf/PERF_POLICY.md` § 3.2를 따를 것

보고 형식:
- 스펙 미구현/정책 불일치 목록
- 수정 파일 목록
- 실행한 smoke 테스트 명령
- 남은 리스크 또는 미해결 항목
```

### 8.2 2단계 요청 템플릿

```text
대상: <target-dir>

1단계 스펙 구현이 완료된 상태를 기준으로,
`doc/perf/PERF_POLICY.md` § 7.6 리팩토링 원칙에 따라
<target-dir>에 남아 있는 추가 리팩토링 항목을 찾아 수정하라.

요구사항:
- 복잡도 감소, ownership 명확화, dead code 제거, 변경 증폭 감소 관점에서 감사할 것
- 얕은 wrapper, orphan helper, 레거시 옵션, 과결합 helper를 줄일 것
- 측정 의미를 바꾸지 말 것
- retry, workaround, 실패 숨기기, inflight/outstanding 류 제어를 추가하지 말 것
- 테스트는 perf smoke 테스트만 사용할 것
- smoke 의미와 통과 기준은 `doc/perf/PERF_POLICY.md` § 3.2를 따를 것

보고 형식:
- 추가 리팩토링 항목 목록
- 수정 파일 목록
- 실행한 smoke 테스트 명령
- 더 남은 리팩토링 후보 또는 미해결 항목
```

## 9. 완료 정의

아래가 모두 만족되어야 해당 대상의 perf 작업을 완료 처리한다.

- 1단계 감독 리뷰에서 스펙 미구현 또는 정책 불일치가 더 이상 발견되지 않는다.
- 2단계 감독 리뷰에서 더 이상 의미 있는 리팩토링 항목이 발견되지 않는다.
- 에이전트가 수행한 perf smoke 테스트가 정책 정의대로 보고되어 있고,
  감독이 그 근거를 확인했다.
- smoke 테스트에 문제가 없다.
- 완료 판정은 하위 에이전트의 자기신고가 아니라 감독 리뷰 기준으로 내려진다.

## 10. 비범위

- `core/bench/*` 리팩토링
- perf 정책 문서 자체의 대규모 재작성
- full perf 실행을 이번 작업의 에이전트 검증 단계에 포함하는 것
- baseline 수치 비교나 성능 우열 평가를 이번 완료 기준으로 사용하는 것
