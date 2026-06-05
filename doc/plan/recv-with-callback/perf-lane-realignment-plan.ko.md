# `core/perf` recv-with-callback lane 재정렬 계획

> 범위:
> [`core/perf/`](../../../core/perf),
> [`doc/perf/`](/home/hep7/project/kairos/zlink/doc/perf)

## 1. 목적

callback surface를 다시 넓게 열더라도 perf가 모든 `pattern x recv_mode` 조합을
같은 무게로 검증할 필요는 없다. 이번 계획은 callback 복원과 perf 운영
복잡도를 분리하는 데 목적이 있다.

고정 정책:

- single suite는 callback 모드만 기본 테스트 대상으로 둔다.
- multi suite는 recv 모드만 기본 테스트 대상으로 둔다.
- single의 dual-mode 예외는 `SPOT`만 둔다.
- multi의 dual-mode 예외는 `SPOT`, `STREAM`만 둔다.
- monitor는 perf pattern이 아니며, perf 관련 monitor 검증은 모두 callback 기준으로
  둔다.
- 이 perf 정책은 기본 검증 경로에 한정하며, `core`의
  `receive_callback`/`send_ready` 독립 상태 모델과는 별개다.

## 2. canonical lane

### 2.1 single = callback only

single suite는 callback receive 쪽 대표 surface를 검증하는 lane으로 고정한다.

정책:

- single 기본 문서와 예시는 callback 기준으로 쓴다.
- single 기본 runner와 policy test는 callback만 실행한다고 본다.
- single에서 dual-mode 비교가 필요한 예외 패턴은 `SPOT`만 허용한다.
- monitor 기반 ready/start gate와 monitor 검증은 callback 기준으로 동작해야 한다.

현재 코드 기준 해석:

- single에 `STREAM` pattern이 없으면 새 pattern을 억지로 추가하지 않는다.
- single의 기본 실행 경로는 callback only로 정리한다.
- single에서 recv 모드 측정은 일반 pattern 전체로 열지 않는다.
- `SPOT`만 예외적으로 `recv` / `callback` 두 모드 비교를 유지한다.

### 2.2 multi = recv only

multi suite는 fan-out, client orchestration, shutdown, monitor handoff를 가장
안정적으로 검증하는 recv lane으로 다시 고정한다.

정책:

- multi 기본 lane은 recv path를 사용한다.
- multi README와 예시는 recv를 기준으로 쓴다.
- multi 기본 runner와 policy test는 recv만 실행한다고 본다.
- multi에서 dual-mode 비교가 필요한 예외 패턴은 `SPOT`, `STREAM`만 허용한다.
- monitor 기반 connect-ready, delivery-ready gate와 monitor 검증은 callback 기준
  으로 유지한다.

## 3. public perf surface 정리

### 3.1 `--recv` 의미

`--recv` 옵션은 남겨 두더라도 지원 범위를 좁혀야 한다.

변경 방향:

- `--recv`는 "모든 조합을 동등 지원하는 matrix selector"가 아니다.
- 기본값은 suite별 canonical lane을 따른다.
  - single default: `callback`
  - multi default: `recv`
- 비기본 mode는 suite별 dual-mode 예외 패턴에서만 허용한다.
- README와 policy test에서는 이 예외 범위를 표로 고정한다.

### 3.2 support 표기

문서와 코드에서 아래 구분을 명시한다.

| suite | 기본 테스트 mode | dual-mode 예외 | monitor mode |
|---|---|---|---|
| single | `callback`만 | `SPOT` | `callback` |
| multi | `recv`만 | `SPOT`, `STREAM` | `callback` |

중요한 점:

- callback 복원과 perf 검증 범위를 분리해서 적는다.
- `SPOT`, `STREAM`만 perf dual-mode 예외라는 점을 표와 예시에서 거듭 명시한다.
- monitor는 모든 perf 관련 검증에서 callback 기준이라는 점을 분명히 적는다.

### 3.3 pattern별 허용 mode 고정

구현자가 runner와 policy test를 다시 해석하지 않도록 pattern별 허용 mode를
여기서 바로 고정한다.

#### single suite

| pattern | 허용 mode | 기본 mode |
|---|---|---|
| `PAIR` | `callback` | `callback` |
| `PUBSUB` | `callback` | `callback` |
| `DEALER_DEALER` | `callback` | `callback` |
| `DEALER_ROUTER` | `callback` | `callback` |
| `ROUTER_ROUTER` | `callback` | `callback` |
| `GATEWAY` | `callback` | `callback` |
| `SPOT` | `recv`, `callback` | `callback` |

#### multi suite

| pattern | 허용 mode | 기본 mode |
|---|---|---|
| `DEALER_DEALER` | `recv` | `recv` |
| `DEALER_ROUTER` | `recv` | `recv` |
| `ROUTER_ROUTER` | `recv` | `recv` |
| `PUBSUB` | `recv` | `recv` |
| `GATEWAY` | `recv` | `recv` |
| `SPOT` | `recv`, `callback` | `recv` |
| `STREAM` | `recv`, `callback` | `recv` |

#### monitor

| surface | 허용 mode | 기본 mode |
|---|---|---|
| socket/service monitor | `callback` | `callback` |

정책 요구:

- 허용되지 않은 `pattern x recv_mode` 조합은 runner가 시작 전에 즉시 실패한다.
- 여러 pattern을 한 번에 고를 때 하나라도 허용되지 않은 조합이 섞이면 전체 실행을
  실패로 본다.
- silent fallback으로 mode를 바꾸지 않는다.
- multi에서 `--recv callback`을 쓸 때는 실질적으로 `SPOT`, `STREAM`만 선택
  가능하다.

## 4. `core/perf` 구현 정렬

### 4.1 runner

직접 수정 대상으로 보는 파일:

- [`core/perf/run_benchmarks.sh`](../../../bindings/c/perf/run_benchmarks.sh)
- [`core/perf/run_benchmarks_multi.sh`](../../../bindings/c/perf/run_benchmarks_multi.sh)
- [`core/perf/run_comparison.py`](../../../bindings/c/perf/run_comparison.py)
- [`core/perf/single/run_comparison.py`](../../../bindings/c/perf/single/run_comparison.py)

정렬 요구:

- single 기본 `recv_mode`를 `callback`으로 둔다.
- multi 기본 `recv_mode`를 `recv`로 둔다.
- single 일반 pattern은 callback only로 고정한다.
- multi 일반 pattern은 recv only로 고정한다.
- single에서는 `SPOT`만 dual-mode override를 허용한다.
- multi에서는 `SPOT`, `STREAM`만 dual-mode override를 허용한다.
- monitor 관련 probe/contract는 callback 기준으로 고정한다.
- 허용되지 않은 mixed pattern selection은 runner가 시작 전에 실패시킨다.

### 4.2 perf 공통 helper

직접 수정 대상으로 보는 파일:

- [`core/perf/single/common/bench_common.hpp`](../../../bindings/c/perf/single/common/bench_common.hpp)
- [`core/perf/multi/common/perf_common_multi.hpp`](../../../bindings/c/perf/multi/common/perf_common_multi.hpp)

정렬 요구:

- callback 허용 여부만 hard-fail 하던 정책 함수는
  "기본 lane"과 suite별 dual-mode 예외 helper로 역할을 나눈다.
- single callback lane, multi recv lane 기본값을 helper에서 바로 읽히게 만든다.
- monitor/start gate helper는 callback 기준 infrastructure로 유지한다.

### 4.3 pattern 내부 mode 선택

이번 계획에서는 `recv`와 `callback`을 별도 파일이나 별도 public pattern으로 분리하지
않는다.

정렬 요구:

- 같은 pattern 안에서 `--recv recv` / `--recv callback`만으로 mode를 선택한다.
- `SPOT`, `STREAM` dual-mode 예외도 별도 callback 파일명이나 별도 pattern 이름을
  public contract로 노출하지 않는다.
- 기본 mode는 runner와 helper가 결정하고, 구현 상세가 내부적으로 어떻게 나뉘든
  문서와 public runner surface에서는 하나의 pattern으로 설명한다.
- callback 복원 자체를 이유로 callback 전용 파일이나 엔트리포인트를 유지해야 한다고
  문서화하지 않는다.

## 5. 문서 정렬

### 5.1 `core/perf` 사용자 문서

반드시 바꿔야 하는 항목:

- [`core/perf/README.md`](../../../bindings/c/perf/README.md)
- [`core/perf/README_KO.md`](../../../core/perf/README_KO.md)

문서 방향:

- single 섹션은 callback lane을 기본값으로 설명한다.
- multi 섹션은 recv lane을 기본값으로 설명한다.
- `--recv` 옵션은 single=`SPOT`, multi=`SPOT`/`STREAM` 예외에서만 의미가 크다고
  명시한다.
- monitor는 dual-mode 유지 대상이지만 perf pattern matrix에는 넣지 않고,
  callback 기준으로만 설명한다고 적는다.

### 5.2 `doc/perf`

문서가 있으면 아래를 반영한다.

- callback 복원 사실
- canonical lane 분리
- single=`SPOT`, multi=`SPOT`/`STREAM` dual-mode 예외
- single/multi 예시 명령의 기본값 변경
- monitor는 start gate/ready gate 표면이며 callback 기준이라는 점

## 6. 정책 테스트

직접 수정 대상으로 보는 파일:

- [`core/perf/single/tests/test_run_comparison_policy.py`](../../../bindings/c/perf/single/tests/test_run_comparison_policy.py)
- [`core/perf/single/tests/test_multi_run_comparison_policy.py`](../../../bindings/c/perf/single/tests/test_multi_run_comparison_policy.py)

회귀 방향:

- 기존:
  - "어떤 pattern이 callback unsupported인가"를 표로 고정
- 변경 후:
  - single default가 callback인지 확인
  - multi default가 recv인지 확인
  - single 일반 pattern이 callback only인지 확인
  - multi 일반 pattern이 recv only인지 확인
  - single=`SPOT`, multi=`SPOT`/`STREAM`만 dual-mode override가 허용되는지 확인
  - mixed pattern selection에서 허용되지 않은 조합이 즉시 실패하는지 확인
  - monitor 관련 정책이 callback 기준인지 확인
  - canonical lane 문구와 result naming이 일관적인지 확인

## 7. 완료 기준

아래가 모두 만족되면 perf 정렬 완료로 본다.

1. single README/runner/tests가 callback only 정책을 기본값으로 설명한다.
2. multi README/runner/tests가 recv only 정책을 기본값으로 설명한다.
3. single=`SPOT`, multi=`SPOT`/`STREAM` dual-mode 예외가 문서와 코드에 명시된다.
4. monitor가 perf dual-mode pattern처럼 서술되지 않고 callback 기준으로 고정된다.
5. callback 복원이 perf 전체 matrix 폭발로 이어지지 않는다.
