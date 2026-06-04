# `doc/perf` callback-to-recv 정책 정렬 계획

> `superseded`
>
> 이 문서는 callback 지원 범위를 줄이던 이전 perf 계획이다.
> 현재 baseline은
> [`doc/plan/recv-with-callback/perf-lane-realignment-plan.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/recv-with-callback/perf-lane-realignment-plan.ko.md)
> 를 따른다.
> 현재 기준은 `single=callback only`, `multi=recv only`,
> `SPOT`/`STREAM` dual-mode 예외, monitor callback 고정이다.

> 범위:
> [`doc/perf/`](/home/hep7/project/kairos/zlink/doc/perf),
> [`core/perf/`](/home/hep7/project/kairos/zlink/core/perf)

## 1. 목적

현재 perf 정책 문서는 `--recv callback`을 거의 전 패턴의 일반 옵션처럼
서술한다. 이번 계획의 목적은 이를 실제 public support matrix에 맞게 줄이는
것이다.

고정 정책:

- single suite
  - `SPOT`만 `recv` / `callback` dual-mode
  - 나머지 single pattern은 `recv` only
  - `STREAM`은 single suite 대상이 아님
- multi suite
  - `SPOT`, `STREAM`만 `recv` / `callback` dual-mode
  - 나머지 multi pattern은 `recv` only
- monitor는 callback 유지 대상이지만 perf pattern이 아니므로 `--recv` matrix에
  넣지 않는다

## 2. public perf surface 고정

### 2.1 `--recv` 의미

`--recv`는 더 이상 "모든 패턴에 공통으로 적용되는 옵션"이 아니다.

| suite | pattern | 허용 mode |
|---|---|---|
| single | `SPOT` | `recv`, `callback` |
| single | `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `GATEWAY` | `recv`만 |
| multi | `MULTI_SPOT` | `recv`, `callback` |
| multi | `MULTI_STREAM` | `recv`, `callback` |
| multi | `MULTI_DEALER_DEALER`, `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`, `MULTI_PUBSUB`, `MULTI_GATEWAY` | `recv`만 |

정책 요구:

- 지원하지 않는 조합에서 `--recv callback`을 주면 runner는 즉시 실패해야 한다.
- silent fallback으로 `recv`로 바꾸지 않는다.
- 별도 패턴명으로 callback variant를 늘리지 않는다.

## 3. 패턴명 정리

### 3.1 제거 대상 legacy pattern 이름

- `STREAM_CALLBACK`
- `MULTI_STREAM_CALLBACK`

정책 기준 canonical surface는 아래 하나뿐이다.

- `STREAM` / `MULTI_STREAM`
- 수신 방식은 `--recv recv` 또는 `--recv callback`으로 선택

곧 "패턴명에 callback이 박힌 legacy alias"는 정책 문서에서 뺀다.

### 3.2 유지 이유

- pattern과 receive mode는 서로 다른 축이다.
- `STREAM_CALLBACK` 같은 이름은 mode를 pattern 이름에 중복으로 encode한다.
- 이후 support matrix를 읽을 때 `pattern x recv_mode` 대신 `pattern alias x recv_mode`
  형태가 되어 설명 비용만 늘어난다.

## 4. `doc/perf` 문서 수정 항목

### 4.1 `PERF_POLICY.md`

반드시 바꿔야 하는 내용:

- 공통 원칙의 callback 모델 설명에 "callback coverage는 제한적"이라는 문장을 추가
- single/multi callback 허용 패턴 표 추가
- `PAIR --recv callback` 같은 예시 제거
- STREAM 섹션에서 `MULTI_STREAM_CALLBACK` 언급 제거
- `STREAM`은 pattern, `callback`은 `--recv` mode라는 축을 명확히 분리

### 4.2 `PERF_SINGLE_TEST_POLICY.md`

반드시 바꿔야 하는 내용:

- `--recv callback`은 single에서 `SPOT`만 허용한다고 명시
- `STREAM_CALLBACK` 언급 제거
- pattern matrix에 single callback 지원 패턴이 `SPOT`뿐임을 표로 고정
- direct example이 있다면 `SPOT --recv callback`으로 교체

### 4.3 `PERF_MULTI_TEST_POLICY.md`

반드시 바꿔야 하는 내용:

- `--recv callback`은 multi에서 `MULTI_SPOT`, `MULTI_STREAM`만 허용한다고 명시
- `MULTI_STREAM_CALLBACK` 별도 패턴 문구 제거
- source/binary 표에서 public pattern 이름은 `MULTI_STREAM` 하나만 사용
- 구현 메모가 필요하면 "recv/callback mode별 server entrypoint가 달라도 public
  pattern은 `MULTI_STREAM` 하나"라고만 적는다
- callback 예시는 `MULTI_SPOT` 또는 `MULTI_STREAM`으로 교체

### 4.4 `core/perf` 사용자 문서

반드시 바꿔야 하는 내용:

- [`core/perf/README.md`](/home/hep7/project/kairos/zlink/core/perf/README.md)의
  callback 허용 패턴 표를 새 matrix로 고정
- [`core/perf/README_KO.md`](/home/hep7/project/kairos/zlink/core/perf/README_KO.md)의
  callback 허용 패턴 표를 새 matrix로 고정
- single에서 `PAIR`, `GATEWAY` 등의 callback 예시 제거
- multi에서 `STREAM_CALLBACK` 별도 패턴 예시 제거

## 5. `core/perf` 구현 정렬 항목

정책 문서 수정과 `core/perf` 구현 정리는 같은 작업에서 함께 끝낸다.

### 5.1 runner / matrix

- `run_comparison.py`
- `single/run_comparison.py`
- `run_benchmarks.sh`
- `run_benchmarks_multi.sh`
- single/multi unit tests

필수 정렬:

- callback 허용 패턴 표를 코드에 명시
- `STREAM_CALLBACK` / `MULTI_STREAM_CALLBACK` alias 제거
- `SPOT` / `STREAM`만 dual-mode로 남김
- 지원하지 않는 pattern x recv_mode 조합은 즉시 실패
- silent fallback 없음

직접 수정 대상으로 보는 파일:

- [`core/perf/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/run_comparison.py)
- [`core/perf/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/single/run_comparison.py)
- [`core/perf/run_benchmarks.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh)
- [`core/perf/run_benchmarks_multi.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks_multi.sh)
- [`core/perf/CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/perf/CMakeLists.txt)
- [`core/perf/multi/common/perf_common.hpp`](/home/hep7/project/kairos/zlink/core/perf/multi/common/perf_common.hpp)
- [`core/perf/single/tests/test_run_comparison_policy.py`](/home/hep7/project/kairos/zlink/core/perf/single/tests/test_run_comparison_policy.py)
- [`core/perf/single/tests/test_multi_run_comparison_policy.py`](/home/hep7/project/kairos/zlink/core/perf/single/tests/test_multi_run_comparison_policy.py)

### 5.2 core perf 바이너리

- single: `SPOT` callback path 유지
- multi: `SPOT`, `STREAM` callback path 유지
- 기타 pattern의 callback bench 바이너리/분기는 제거한다

직접 수정 대상으로 보는 파일:

- [`core/perf/single/src/perf_spot.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_spot.cpp)
- [`core/perf/single/src/perf_gateway.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_gateway.cpp)
- [`core/perf/single/src/perf_pair.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_pair.cpp)
- [`core/perf/single/src/perf_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_pubsub.cpp)
- [`core/perf/single/src/perf_router_router.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_router_router.cpp)
- [`core/perf/multi/src/perf_multi_spot_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_spot_client.cpp)
- [`core/perf/multi/src/perf_multi_stream_callback_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_stream_callback_server.cpp)
- [`core/perf/multi/src/perf_multi_gateway_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_gateway_server.cpp)
- [`core/perf/multi/src/perf_multi_gateway_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_gateway_client.cpp)
- [`core/perf/multi/src/perf_multi_dealer_dealer_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_dealer_dealer_server.cpp)
- [`core/perf/multi/src/perf_multi_pubsub_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_pubsub_client.cpp)

## 6. monitor와 perf의 관계

monitor는 callback 유지 대상이지만 perf pattern은 아니다. 따라서:

- callback 유지 정책 문서에는 monitor를 포함한다
- perf `--recv` support matrix에는 monitor를 넣지 않는다
- perf start gate는 계속 monitor delivery-ready event를 사용한다
- 이렇게 해야 "monitor callback이 남아 있다"와 "monitor가 perf dual-mode pattern이다"를
  구분할 수 있다

## 7. 완료 기준

아래가 모두 만족되면 문서 정렬을 완료로 본다.

1. `doc/perf/` 어디에도 `STREAM_CALLBACK` / `MULTI_STREAM_CALLBACK`가 남지 않는다.
2. single 정책 문서에서 callback 허용 패턴은 `SPOT`만 남는다.
3. multi 정책 문서에서 callback 허용 패턴은 `MULTI_SPOT`, `MULTI_STREAM`만 남는다.
4. monitor callback 유지가 perf dual-mode 확대 근거처럼 서술되지 않는다.
5. 예시 명령이 모두 새 matrix와 일치한다.
