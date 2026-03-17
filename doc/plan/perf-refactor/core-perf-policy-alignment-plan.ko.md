# `core/perf` 정책 정렬 리팩토링 계획

> 범위: [`doc/perf/`](/home/hep7/project/kairos/zlink/doc/perf) 정책 문서에 맞게
> [`core/perf/`](/home/hep7/project/kairos/zlink/core/perf) 구조, 문서, 실행기,
> 산출물 경계를 정렬하는 계획이다.

## 1. 목표

이 계획의 목표는 단순한 디렉터리 정리가 아니다. `core/perf`가 아래 성질을
구조적으로 만족하도록 만드는 것이다.

- single/multi 공식 실행 surface에 `--recv` 옵션을 추가해 정책이 정의한
  `recv` / `callback` 측정 모델을 명시적으로 선택할 수 있다.
- 정책 문서와 구현 설명이 서로 충돌하지 않는다.
- `core/perf` 소스 트리는 벤치마크 코드와 공식 스크립트만 담는다.
- 캐시, 임시 파일, 결과물은 정책이 허용한 위치와 규칙으로만 생성된다.
- 빌드/실행 경로는 저장소 규칙과 동일하게 `core/build/` 기준으로 설명된다.
- single/multi 성능 측정의 의미를 바꾸지 않고, 문서와 운영면의 복잡도만 줄인다.

## 2. 관련 기준 문서

- 통합 정책:
  [`doc/perf/PERF_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_POLICY.md)
- single 정책:
  [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_SINGLE_TEST_POLICY.md)
- multi 정책:
  [`doc/perf/PERF_MULTI_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_MULTI_TEST_POLICY.md)
- 현재 코어 perf 설명:
  [`core/perf/README.md`](/home/hep7/project/kairos/zlink/core/perf/README.md)
- 빌드 타깃 정의:
  [`core/perf/CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/perf/CMakeLists.txt)
- 저장소 공통 규칙:
  [`AGENTS.md`](/home/hep7/project/kairos/zlink/AGENTS.md)

## 2.1 작업 시작 전제

새로운 컨텍스트에서 이 문서만 보고 작업을 시작할 수 있도록, 아래 전제를
고정한다.

- 저장소 루트: `/home/hep7/project/kairos/zlink`
- 공식 빌드 디렉터리: `/home/hep7/project/kairos/zlink/core/build`
- 공식 single 실행기:
  [`core/perf/run_benchmarks.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh)
- 공식 multi 실행기:
  [`core/perf/run_benchmarks_multi.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks_multi.sh)
- single Python runner:
  [`core/perf/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/single/run_comparison.py)
- multi Python runner:
  [`core/perf/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/run_comparison.py)
- 기본 빌드 명령:
  - `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
  - `cmake --build core/build`

## 2.2 정책 기본 size / pattern 범위

문서의 "정책 기본 size 전부"라는 표현은 아래를 의미한다.

- single 기본 size:
  `64, 256, 1024, 65536, 131072, 262144`
- multi 기본 size:
  - `MULTI_DEALER_DEALER`, `MULTI_DEALER_ROUTER`, `MULTI_ROUTER_ROUTER`,
    `MULTI_PUBSUB`: `64, 256, 1024, 65536, 131072, 262144`
  - `MULTI_GATEWAY`, `MULTI_SPOT`: `64, 256, 1024, 65536, 131072, 262144`
  - `MULTI_STREAM_CALLBACK`: `64, 256, 1024, 65536`

이번 문서에서 다루는 패턴은 아래로 고정한다.

- single:
  `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`,
  `GATEWAY`, `SPOT`
- multi:
  `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB`, `GATEWAY`,
  `SPOT`, `STREAM_CALLBACK`

## 2.3 패턴별 현재 파일 위치

새 컨텍스트에서 탐색 비용을 줄이기 위해, 현재 패턴별 주요 소스 파일 위치를
여기에 고정한다.

### single

| Pattern | 파일 |
|------|------|
| `PAIR` | [`core/perf/single/src/perf_pair.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_pair.cpp) |
| `PUBSUB` | [`core/perf/single/src/perf_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_pubsub.cpp) |
| `DEALER_DEALER` | [`core/perf/single/src/perf_dealer_dealer.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_dealer_dealer.cpp) |
| `DEALER_ROUTER` | [`core/perf/single/src/perf_dealer_router.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_dealer_router.cpp) |
| `ROUTER_ROUTER` | [`core/perf/single/src/perf_router_router.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_router_router.cpp) |
| `GATEWAY` | [`core/perf/single/src/perf_gateway.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_gateway.cpp) |
| `SPOT` | [`core/perf/single/src/perf_spot.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_spot.cpp) |

### multi

| Pattern | client | server |
|------|--------|--------|
| `DEALER_DEALER` | [`core/perf/multi/src/perf_multi_dealer_dealer_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_dealer_dealer_client.cpp) | [`core/perf/multi/src/perf_multi_dealer_dealer_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_dealer_dealer_server.cpp) |
| `DEALER_ROUTER` | [`core/perf/multi/src/perf_multi_dealer_router_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_dealer_router_client.cpp) | [`core/perf/multi/src/perf_multi_dealer_router_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_dealer_router_server.cpp) |
| `ROUTER_ROUTER` | [`core/perf/multi/src/perf_multi_router_router_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_router_router_client.cpp) | [`core/perf/multi/src/perf_multi_router_router_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_router_router_server.cpp) |
| `PUBSUB` | [`core/perf/multi/src/perf_multi_pubsub_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_pubsub_client.cpp) | [`core/perf/multi/src/perf_multi_pubsub_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_pubsub_server.cpp) |
| `GATEWAY` | [`core/perf/multi/src/perf_multi_gateway_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_gateway_client.cpp) | [`core/perf/multi/src/perf_multi_gateway_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_gateway_server.cpp) |
| `SPOT` | [`core/perf/multi/src/perf_multi_spot_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_spot_client.cpp) | [`core/perf/multi/src/perf_multi_spot_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_spot_server.cpp) |
| `STREAM_CALLBACK` | [`core/perf/common/streamclient/perf_stream_client.cpp`](/home/hep7/project/kairos/zlink/core/perf/common/streamclient/perf_stream_client.cpp) | [`core/perf/multi/src/perf_multi_stream_callback_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_stream_callback_server.cpp) |

## 3. 현재 상태 구분

이 섹션은 팀장님 요청대로 현재 상태를 세 가지로 나눠서 본다.

### 3.1 이미 정책 골격과 맞는 부분

- [`core/perf/`](/home/hep7/project/kairos/zlink/core/perf) 아래에
  `single/`, `multi/`, `results/`가 존재한다.
- single 소스는
  [`core/perf/single/src/`](/home/hep7/project/kairos/zlink/core/perf/single/src),
  multi 소스는
  [`core/perf/multi/src/`](/home/hep7/project/kairos/zlink/core/perf/multi/src)에
  분리되어 있다.
- multi는 `_server.cpp` / `_client.cpp` 쌍 구조를 이미 사용하고 있다.
- 공식 실행 스크립트가 single/multi로 분리되어 있다.
  - [`core/perf/run_benchmarks.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh)
  - [`core/perf/run_benchmarks_multi.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks_multi.sh)
- 결과 저장 경로도 정책이 요구하는 `results/{single,multi}/report/` 구조를
  이미 사용한다.

즉, 대분류 구조와 파일 배치는 이미 정책 방향과 크게 어긋나지 않는다.

### 3.2 정책과 충돌하는 부분

#### 빌드 디렉터리 규칙 충돌

- [`core/perf/CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/perf/CMakeLists.txt)
  상단 주석은 `cmake -B build` 예시를 남기고 있다.
- [`core/perf/run_benchmarks.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh)
  는 `core/build/` 외에 `core/build/<platform>-<arch>`도 탐색한다.
- 이는 저장소 규칙의 "`core/build/`만 사용" 원칙과 충돌한다.

#### README와 정책 문서의 의미 불일치

- [`core/perf/README.md`](/home/hep7/project/kairos/zlink/core/perf/README.md)는
  single warmup을 시간 기반 옵션처럼 설명한다.
- 정책 문서는 single/multi 모두 `--recv`로 `recv` / `callback` 모델을 선택하는
  surface를 요구하지만, 현재 공식 스크립트 설명에는 이 옵션이 드러나지 않는다.
- single 정책은 warmup을 count 기반으로 정의하고, active 이후 bounded idle
  drain 허용 여부도 명시한다. 그런데 현재 README만이 아니라 스크립트와
  single 구현도 `PERF_SINGLE_WARMUP_SECONDS` 기반 시간 모델을 사용하고 있어,
  이 항목은 문서 불일치가 아니라 구현-정책 갭이다.
- README의 single execution model에는 `No retry/drain phase`가 적혀 있는데,
  정책은 active 이후 receiver 측 idle drain을 허용한다.
- README의 multi 설명은 wrapper 관점에 치우쳐 있고, 정책 문서가 요구하는
  server/client 프로세스 모델, READY/STOP 제어, phase 의미를 충분히 드러내지
  못한다.

#### 운영 산출물이 소스 트리를 오염시키는 상태

- 현재 `core/perf` 아래에 정책상 소스 트리에 상주할 이유가 없는 항목이 있다.
  - [`core/perf/__pycache__/`](/home/hep7/project/kairos/zlink/core/perf/__pycache__)
  - [`core/perf/single/__pycache__/`](/home/hep7/project/kairos/zlink/core/perf/single/__pycache__)
  - [`core/perf/multi/__pycache__/`](/home/hep7/project/kairos/zlink/core/perf/multi/__pycache__)
  - [`core/perf/single/tmp/`](/home/hep7/project/kairos/zlink/core/perf/single/tmp)
  - [`core/perf/tmp/`](/home/hep7/project/kairos/zlink/core/perf/tmp)
  - [`core/perf/common/streamclient/build/`](/home/hep7/project/kairos/zlink/core/perf/common/streamclient/build)
- [`core/perf/.gitignore`](/home/hep7/project/kairos/zlink/core/perf/.gitignore)는
  `tmp/`만 부분적으로 막고 있어 Python 캐시, streamclient build 산출물,
  suite 하위 임시물의 재유입을 구조적으로 막지 못한다.

#### 정책 문서가 요구하는 "공식 surface"와 코드 설명의 경계 불명확

- 공식 정책은 `doc/perf/`가 기준 문서라고 분명히 정하고 있다.
- 하지만 `core/perf/README.md`가 독자적인 실행 의미와 제약을 다시 서술하면서,
  일부는 정책과 중복되고 일부는 어긋난다.
- 결과적으로 사용자는 어느 문서를 신뢰해야 하는지 판단 비용이 생긴다.

### 3.3 아직 정책 위반이라고 단정할 수는 없지만 정리가 필요한 부분

- [`core/perf/README_KO.md`](/home/hep7/project/kairos/zlink/core/perf/README_KO.md)
  도 영문 README와 동일한 정합성 검토가 필요하다.
- 결과 디렉터리
  [`core/perf/results/`](/home/hep7/project/kairos/zlink/core/perf/results)는 정책상
  허용되지만, 저장소에서 추적할지 실행 산출물로만 취급할지는 명확히 정리할
  필요가 있다.
- helper와 공통 코드의 경계는 대체로 맞지만, policy-driven 설명이 부족해
  "어디까지 공통화가 허용되는지"가 문서만으로 즉시 드러나지 않는다.

## 4. 이번에 계획 문서에 반드시 추가해야 하는 항목

이 문서는 아래 내용을 명시적으로 담아야 한다.

### 4.1 현황 표

- 정책에 이미 부합하는 요소
- 정책과 충돌하는 요소
- 정리 대상이지만 구현 의미를 바꾸지 않아야 하는 요소

### 4.2 정리 대상 목록

- 삭제 대상 캐시/임시물
- `.gitignore` 강화 대상
- README/README_KO 수정 대상 문장
- CMake 주석과 스크립트 usage/help 수정 대상
- `core/build/` only 원칙으로 수렴해야 하는 경로 처리

### 4.3 가장 큰 기능 추가: `--recv` 옵션

- single 공식 실행기와 multi 공식 실행기에 `--recv recv|callback` 옵션을 추가한다.
- 기본값은 정책대로 `recv`로 유지한다.
- `--recv callback`일 때만 callback 측정 경로를 사용하게 한다.
- help, README, 결과 파일의 effective options에 선택된 recv 모델이 드러나야 한다.
- 결과 파일 이름에도 실행 recv 모드가 드러나야 한다.
- 결과 출력의 `Effective Options (start/result)`에도 `recv_mode` 항목이 반드시
  포함되어야 한다.
- 지원하지 않는 pattern 조합은 policy semantics에 맞춰 `fail` 또는 명시적
  미지원 처리 기준을 정리해야 한다.
- 이 기능은 shell wrapper만의 변경이 아니다. 인자 파싱, 비교 스크립트,
  하위 바이너리 전달 경로, pattern별 지원 범위 문서화까지 포함한다.

### 4.4 모드별 실행 계약

문서에는 `--recv`를 옵션 이름으로만 적지 말고, 실제 측정 엔진 계약을 함께
적어야 한다.

#### 바이너리 전달 경로 선결정

`--recv` 값은 shell wrapper에서 끝나는 옵션이 아니라 실제 benchmark 바이너리까지
전달되어야 한다. 아래 셋 중 어떤 방식으로 고정할지 Phase 2a 착수 전에 먼저
결정한다.

- 같은 바이너리 + CLI arg
  - 예: `perf_pair current tcp 1024 --recv callback`
- 환경 변수
  - 예: `PERF_RECV_MODE=callback`
- 별도 바이너리
  - 예: `perf_pair_recv`, `perf_pair_callback`

이 결정은 다음 항목에 직접 영향을 준다.

- `run_benchmarks.sh`
- `run_benchmarks_multi.sh`
- `single/run_comparison.py`
- `run_comparison.py`
- `core/perf/CMakeLists.txt`
- 바이너리 명명 규칙
- runner spawn 로직

따라서 문서상 Phase 2a의 첫 완료 조건은 "`--recv` 값이 바이너리에 도달하는
경로가 하나로 고정되었다"이다.

#### `--recv recv`

- recv 경로는 reactor 형식의 `recv + poller` 모델이다.
- recv는 poller `POLLIN` readiness를 기준으로 구동한다.
- readiness가 오면 `zlink_recv()` / `zlink_msg_recv()` 기반으로 비동기 drain을
  수행한다.
- send는 poller `POLLOUT` readiness 기반 backpressure를 사용한다.
- `EAGAIN` 시 동일 호출 흐름에서 retry loop를 돌리지 않고, writable readiness가
  다시 올 때까지 pending 상태로 유지한다.

#### `--recv callback`

- recv 경로는 `zlink_recv_handler()` / `zlink_recv_spot_handler()` 기반 callback
  모델이다.
- 측정 구간에서는 동기 recv loop와 poller recv를 섞지 않는다.
- send는 `zlink_socket_send_ready_handler()` /
  `zlink_spot_send_ready_handler()` 기반 backpressure를 사용한다.
- callback 안에서 send가 막히면 pending 상태만 기록하고, writable transition
  callback에서 재개한다.
- 동일 측정 구간에서 `recv` 모델과 `callback` 모델을 혼용하지 않는다.

### 4.5 backpressure 설계 명시

- `recv` 모드에서는 `POLLIN` / `POLLOUT` readiness가 수신 drain과 송신 재개를
  각각 담당한다.
- `callback` 모드에서는 recv callback과 send-ready callback이 공식 backpressure
  surface다.
- echo 서버, echo 클라이언트, one-way sender, one-way receiver의 pending 상태
  모델을 분리해서 적어야 한다.
- 문서에는 적어도 아래 ownership을 명시해야 한다.
  - 누가 pending/deque/bool flag를 소유하는가
  - 누가 drain completion을 판정하는가
  - 어떤 경로가 hot path이고 어떤 경로가 setup/teardown인가

### 4.6 handshake / start gate 계약

- 연결 준비와 benchmark start gate는 socket/service monitor 기반 readiness를
  사용해야 한다.
- monitor-ready 이전에는 warmup, active, drain 어떤 측정 phase도 시작하지
  않는다.
- sleep 기반 handshake, retry loop 기반 handshake, 첫 송수신 성공 여부를
  readiness 대용으로 사용하는 ad-hoc handshake는 금지한다.
- monitor-ready 이후 protocol self-check가 꼭 필요하면 bounded validation
  1회만 허용하고, 측정 구간으로 끌고 들어가지 않는다.
- single/multi 공통 엔진과 pattern 구현은 handshake를 자체 루프로 우회하지
  말고 monitor event를 공식 start signal로 사용해야 한다.

### 4.7 메트릭/출력 공통화 원칙

- throughput, bandwidth, latency, p95/p99, resource metric 계산과 보정 로직은
  가능한 한 공통 함수로 모은다.
- `RESULT` line 출력, markdown table 출력, `Effective Options` 출력, failure
  summary 출력도 공통 함수로 수렴한다.
- single/multi runner가 같은 출력 계약을 사용하도록 formatting surface를
  공통화한다.
- 결과 파일명 규칙도 single/multi가 같은 방식으로 사용하도록 공통화한다.
- 팀장님 결정에 따라 "패턴별 흐름이 파일에서 보여야 한다" 제약은 두지 않는다.
- 메트릭/출력뿐 아니라 mode별 공통 측정 엔진, phase 제어, drain/backpressure
  처리도 공통 모듈로 올릴 수 있다.
- 패턴별 파일은 topology, pattern config, role wiring 위주로 얇아져도 된다.

### 4.8 패턴별 지원 매트릭스

- 현재 코드 기준 지원 상태는 아래와 같이 분류한다.

| Suite | Pattern | 현재 `recv` 모드 | 현재 `callback` 모드 | 분류 | 근거/비고 |
|------|---------|------------------|----------------------|------|-----------|
| single | `PAIR` | 없음 | 있음 | callback만 구현 | [`perf_pair.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_pair.cpp)에서 handler 기반 수신만 사용 |
| single | `PUBSUB` | 없음 | 있음 | callback만 구현 | [`perf_pubsub.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_pubsub.cpp)에서 `zlink_recv_spot_handler()` 사용 |
| single | `DEALER_DEALER` | 없음 | 있음 | callback만 구현 | [`perf_dealer_dealer.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_dealer_dealer.cpp)에서 handler 기반 수신 |
| single | `DEALER_ROUTER` | 없음 | 있음 | callback만 구현 | [`perf_dealer_router.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_dealer_router.cpp)에서 router recv handler 사용 |
| single | `ROUTER_ROUTER` | 없음 | 있음 | callback만 구현 | [`perf_router_router.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_router_router.cpp)에서 recv handler 설치 |
| single | `GATEWAY` | 없음 | 있음 | callback만 구현 | [`perf_gateway.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_gateway.cpp)에서 client/server 모두 `zlink_recv_handler()` 사용 |
| single | `SPOT` | 없음 | 있음 | callback만 구현 | [`perf_spot.cpp`](/home/hep7/project/kairos/zlink/core/perf/single/src/perf_spot.cpp)에서 `zlink_recv_spot_handler()` 사용 |
| multi | `DEALER_DEALER` | 있음 | 없음 | recv만 구현 | client/helper가 recv/poll 기반이며 callback 수신 경로 없음 |
| multi | `DEALER_ROUTER` | 있음 | 없음 | recv만 구현 | client/helper와 server 구현이 recv 기반 |
| multi | `ROUTER_ROUTER` | 있음 | 없음 | recv만 구현 | client/helper와 server 구현이 recv 기반 |
| multi | `PUBSUB` | 있음 | 없음 | recv만 구현 | server가 `zlink_recv()` 기반, callback 수신 경로 없음 |
| multi | `GATEWAY` | 없음 | 있음 | callback만 구현 | client/server가 `zlink_recv_handler()` 기반 |
| multi | `SPOT` | 부분적 | 있음 | 정책 정렬 선행 필요 | callback 경로는 있으나 현재 recv 측은 thread/worker 혼합 모델이라 목표 recv 모드와 다름 |
| multi | `STREAM_CALLBACK` | 없음 | 있음 | callback만 구현 | [`perf_multi_stream_callback_server.cpp`](/home/hep7/project/kairos/zlink/core/perf/multi/src/perf_multi_stream_callback_server.cpp) 기준 callback variant만 존재 |

- 검증 명령은 이 매트릭스의 "즉시 지원" 케이스만 사용해야 한다.
- 단, multi의 `recv만 구현`은 현재 문서상 임시 분류다. 이것이 정책이 요구하는
  `POLLIN drain + POLLOUT backpressure` reactor recv 모드인지, 아니면 단순
  blocking recv/partial poll 기반인지 Phase 0에서 다시 검증한다.
- 만약 blocking recv loop 수준이면 "recv만 구현"이 아니라
  "`recv` 모드로 리팩토링 필요"로 재분류한다.

### 4.9 최종 목표 매트릭스

이 작업의 최종 결과물은 "일부 패턴에 `--recv` 옵션이 추가됨"이 아니다.
최종 목표는 아래 매트릭스를 전부 `지원` 상태로 만드는 것이다.

| Suite | Pattern | 최종 `recv` 모드 | 최종 `callback` 모드 | 완료 기준 |
|------|---------|------------------|----------------------|----------|
| single | `PAIR` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| single | `PUBSUB` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| single | `DEALER_DEALER` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| single | `DEALER_ROUTER` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| single | `ROUTER_ROUTER` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| single | `GATEWAY` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| single | `SPOT` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| multi | `DEALER_DEALER` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| multi | `DEALER_ROUTER` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| multi | `ROUTER_ROUTER` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| multi | `PUBSUB` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| multi | `GATEWAY` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| multi | `SPOT` | 지원 | 지원 | 정책 기본 size 전부에서 success |
| multi | `STREAM_CALLBACK` | 지원 | 지원 | 정책 기본 size 전부에서 success |

- 팀장님 요구사항에 따라 완료 시점에는 single/multi 모두 `recv`와
  `callback` 두 모드가 **모든 패턴/size에서 정상 동작**해야 한다.
- 따라서 현재 매트릭스의 `callback만 구현`, `recv만 구현`, `부분적` 상태는
  전부 임시 상태이며, 최종 산출물로 인정하지 않는다.
- `STREAM_CALLBACK`도 이름과 무관하게 최종적으로는 `recv` / `callback` 두 모드를
  모두 지원 대상으로 본다. 필요하면 패턴명과 구현명을 재정렬한다.

### 4.10 전달 보장 수준

- 완료 기준은 "CLI가 옵션을 받는다"가 아니다.
- 완료 기준은 아래를 모두 만족하는 것이다.
  - single 모든 패턴
  - multi 모든 패턴
  - 각 패턴의 정책 기본 size 전부
  - `--recv recv`
  - `--recv callback`
  - 공식 runner 경로
  - 공식 결과 파일 출력
- 어떤 패턴이라도 한 모드만 남아 있으면 이 작업은 미완료로 본다.
- 특정 size에서만 실패하거나 mode별로 부분 성공이면 문서상 완료 판정을 하지
  않는다.

### 4.11 단계별 실행 순서

- 1단계: 작업 트리 오염원 제거와 무시 규칙 정리
- 2a단계: 모드 전달 경로 확정 + CLI/runner 인프라
- 2b단계: single callback-only 패턴에 recv 모드 추가
- 2c단계: multi recv-only 패턴에 callback 모드 추가
- 2d단계: 남은 혼합/특수 패턴 정렬 + full matrix 점검
- 3단계: 정책 기준 문서와 README 역할 재정의
- 4단계: 빌드 디렉터리 설명 및 스크립트 기본값 정렬
- 5단계: 결과 저장/보존 규칙 점검
- 6단계: 실제 실행 검증

### 4.12 완료 판정

- `--recv` 옵션이 공식 실행 surface에 존재하고 동작할 것
- 문서 설명이 `doc/perf`와 모순되지 않을 것
- `core/build/` 외 경로를 공식 예시로 남기지 않을 것
- `core/perf` 소스 트리에 캐시/임시 산출물이 남지 않을 것
- single/multi 공식 실행기가 동일 결과 의미를 유지할 것
- single/multi의 모든 패턴이 `recv` / `callback` 두 모드 모두를 지원할 것
- 각 패턴은 정책 기본 size 전부에서 `success` 결과를 낼 것
- handshake/start gate는 monitor 기반 readiness로 구현되어 있을 것
- 진행 중 발견된 core 라이브러리 버그는 우회하지 않고 회귀 테스트 추가 +
  버그 수정으로 해결되었을 것

## 5. 구현 인벤토리

이 섹션은 실제 구현 누락을 막기 위한 작업 분해 표다. 구현은 아래 항목을 모두
완료해야 종료할 수 있다.

### 5.1 runner / surface

- 모드 전달 방식 결정
  - same binary + CLI arg
  - env var
  - split binary
  - 선택 근거와 영향 파일 기록
- `run_benchmarks.sh`
  - `--recv recv|callback` 인자 파싱
  - help/usage 반영
  - env 전달
  - effective options 반영
- `run_benchmarks_multi.sh`
  - `--recv recv|callback` 인자 파싱
  - help/usage 반영
  - env 전달
  - effective options 반영
- `single/run_comparison.py`
  - `--recv` 파싱
  - build/run 인자 전달
  - 결과 파일 options 기록
  - 결과 파일명에 recv mode 반영
  - `Effective Options`에 `recv_mode` 출력
  - 테이블/RESULT 출력 정렬
- `run_comparison.py`
  - `--recv` 파싱
  - server/client 양쪽 전달
  - 결과 파일 options 기록
  - 결과 파일명에 recv mode 반영
  - `Effective Options`에 `recv_mode` 출력
  - 테이블/RESULT 출력 정렬

### 5.2 공통 엔진

- single/multi 공통 메트릭 계산 함수
- single/multi 공통 출력 함수
- single/multi 공통 monitor handshake / start gate helper
- `recv` 모드 공통 엔진
  - `POLLIN` readiness
  - recv drain
  - `POLLOUT` backpressure
  - pending state 관리
- `callback` 모드 공통 엔진
  - recv callback 등록
  - send-ready callback 등록
  - pending state 관리
  - callback mode drain/flush completion
- phase state machine
  - warmup
  - settle
  - active
  - drain/teardown
- monitor-ready 이후에만 phase transition이 일어나도록 gate 관리

### 5.3 single pattern 구현

- single recv 모드 send-path architecture 결정
  - sender/receiver를 같은 poller event loop로 통합할지
  - sender thread를 유지하고 send만 nonblocking + `POLLOUT` readiness로 바꿀지
  - mode별 completion owner를 어떻게 둘지
- `PAIR`
  - `recv` 모드 구현
  - `callback` 모드 정렬
  - 정책 기본 size 검증
- `PUBSUB`
  - `recv` 모드 구현
  - `callback` 모드 정렬
  - 정책 기본 size 검증
- `DEALER_DEALER`
  - `recv` 모드 구현
  - `callback` 모드 정렬
  - 정책 기본 size 검증
- `DEALER_ROUTER`
  - `recv` 모드 구현
  - `callback` 모드 정렬
  - 정책 기본 size 검증
- `ROUTER_ROUTER`
  - `recv` 모드 구현
  - `callback` 모드 정렬
  - 정책 기본 size 검증
- `GATEWAY`
  - `recv` 모드 구현
  - `callback` 모드 정렬
  - 정책 기본 size 검증
- `SPOT`
  - `recv` 모드 구현
  - `callback` 모드 정렬
  - 정책 기본 size 검증

### 5.4 multi pattern 구현

- `DEALER_DEALER`
  - `recv` 모드 정렬
  - `callback` 모드 구현
  - server/client backpressure 검증
  - 정책 기본 size 검증
- `DEALER_ROUTER`
  - `recv` 모드 정렬
  - `callback` 모드 구현
  - server/client backpressure 검증
  - 정책 기본 size 검증
- `ROUTER_ROUTER`
  - `recv` 모드 정렬
  - `callback` 모드 구현
  - server/client backpressure 검증
  - 정책 기본 size 검증
- `PUBSUB`
  - `recv` 모드 정렬
  - `callback` 모드 구현
  - server/client backpressure 검증
  - 정책 기본 size 검증
- `GATEWAY`
  - `recv` 모드 구현
  - `callback` 모드 정렬
  - server/client backpressure 검증
  - 정책 기본 size 검증
- `SPOT`
  - `recv` 모드 정책 정렬
  - `callback` 모드 정렬
  - server/client backpressure 검증
  - 정책 기본 size 검증
- `STREAM_CALLBACK`
  - `recv` 모드 구현
  - `callback` 모드 정렬
  - server/client backpressure 검증
  - 정책 기본 size 검증

### 5.5 문서 / 정책 / 구조 정리

- `README.md`
- `README_KO.md`
- `PERF_POLICY.md`
- build path 정렬
- `.gitignore`
- 결과 저장 규칙 문구 정렬
- 결과 파일명에 `<recv_mode>` 반영

### 5.6 core 버그 대응

- perf 작업 중 core 라이브러리 버그를 발견하면 우회하지 않는다.
- 버그를 재현하는 회귀 테스트를 먼저 추가한다.
- 그 다음 core 버그를 수정한다.
- 수정 후 관련 테스트와 perf 경로를 다시 실행한다.
- 버그 발견 사실, 추가한 테스트, 수정 파일, 재검증 결과를 Phase Review에 남긴다.

## 6. 단계별 상세 작업 계획

각 단계는 구현, 자체 리뷰, 수정, 재리뷰를 한 묶음으로 끝낸다. 리뷰 없이 다음
단계로 넘어가지 않는다.

## 7. 리팩토링 방향

### 7.1 문서의 깊이를 높이고 중복 surface를 줄인다

- 정책의 source of truth는 `doc/perf`로 고정한다.
- `core/perf/README*.md`는 정책의 축약 안내서로만 남긴다.
- README는 정책을 재정의하지 말고 다음만 설명한다.
  - 어디서 빌드하는가
  - 어떤 스크립트가 공식 실행기인가
  - 결과가 어디에 저장되는가
  - 상세 의미는 어떤 정책 문서를 봐야 하는가

이렇게 해야 shallow duplicate documentation을 제거할 수 있다.

### 7.2 소스 트리와 실행 산출물의 경계를 분명히 한다

- `core/perf`는 코드와 공식 스크립트의 위치다.
- 캐시, 임시 파일, Python bytecode, streamclient 로컬 build 산출물은
  커밋 대상이 아니며 소스 구조의 일부도 아니다.
- 허용된 산출물은 정책에 정의된 결과 파일과 필요한 최소 디렉터리 골격뿐이다.

### 7.3 빌드 경로는 하나로 수렴한다

- 저장소 가이드에 맞게 공식 빌드 디렉터리는
  [`core/build/`](/home/hep7/project/kairos/zlink/core/build) 하나로 고정한다.
- perf 스크립트와 CMake 주석, README 예시가 모두 이 규칙을 따르게 한다.
- 플랫폼별 세부 산출물은 `core/build/` 내부 구조로 설명하고, 사용자가 별도
  루트 build 경로를 상상하게 만드는 예시는 제거한다.

### 7.4 정책 의미는 유지하고 설명 복잡도만 줄인다

- 측정 의미, phase, RESULT 포맷, skip/fail semantics는 변경하지 않는다.
- 다만 공식 실행 surface에는 정책이 이미 정의한 `--recv` 선택지를 실제 기능으로
  드러내야 한다.
- 즉, 이번 계획은 측정 모델을 새로 발명하는 것이 아니라, 정책에 이미 있는
  recv model 선택을 구현 surface에 노출하는 작업을 포함한다.

### 7.5 `--recv`는 CLI 옵션이 아니라 측정 엔진 선택이다

- `--recv recv`는 `recv + poller(POLLIN/POLLOUT)` 기반 reactor 모델이다.
- `--recv callback`은 `recv_handler + send_ready_handler` 기반 callback 모델이다.
- 두 모드는 recv 경로, send backpressure 경로, pending 상태 ownership이 서로
  다르므로, 단순 플래그 분기로 끝내면 안 된다.
- 문서와 구현은 모드별 책임을 아래 단위로 분리해야 한다.
  - wrapper / runner 인자 surface
  - 비교 스크립트와 결과 출력
  - pattern별 바이너리 측정 경로
  - backpressure state owner
  - 패턴별 지원 매트릭스
- 그리고 최종 결과는 부분 지원이 아니라 full matrix 지원이어야 한다.

### 7.6 메트릭과 출력은 깊은 공통 모듈로 모은다

- 메트릭 계산과 출력 포맷은 패턴별 차이가 거의 없으므로 공통화 이득이 크다.
- runner별로 비슷한 보정/출력 로직을 복제하지 말고 공통 함수로 수렴한다.
- 특히 아래 항목은 우선 공통화 대상이다.
  - latency triplet 보정
  - throughput/bandwidth 계산
  - resource metric 출력
  - `RESULT` line emitter
  - markdown table formatter
  - `Effective Options` emitter
  - failure summary formatter
- 추가로 아래 항목도 공통화 후보로 본다.
  - mode별 recv drain engine
  - mode별 send backpressure engine
  - phase state machine
  - pending/deque/bool flag state helper
  - pattern/role wiring config

## 8. 상세 작업 계획

## Phase 0. 기준선 고정 및 작업 목록 확정

목표:
- 어떤 항목이 현재 문제인지 먼저 명확히 고정한다.

작업:
- `doc/perf` 정책 문서 기준으로 `core/perf` 현황 표를 문서화한다.
- README, CMake, 실행 스크립트 usage/help의 정책 충돌 문장을 목록화한다.
- 소스 트리 내부 산출물 디렉터리를 인벤토리화한다.
- multi의 `recv만 구현` 패턴이 실제로 `POLLIN drain + POLLOUT backpressure`
  구조인지 확인한다.
- 현재 single/multi handshake가 monitor 기반 readiness인지, ad-hoc loop인지
  패턴별로 확인한다.
- single warmup 모델(count 기반 vs seconds 기반)을 이번 scope에서 해결할지,
  별도 후속 scope로 넘길지 명시한다.

산출물:
- 본 문서의 현황 표
- 수정 대상 파일 목록
- 구현 인벤토리 체크리스트
- 현재 상태 매트릭스
- 최종 목표 매트릭스

완료 조건:
- "무엇이 이미 맞고, 무엇이 충돌하며, 무엇을 추가해야 하는지"가 한 문서에서
  즉시 보인다.
- 모든 구현 항목이 체크리스트에 명시돼 있다.

리뷰 게이트:
- 현재 상태와 최종 목표가 혼동되지 않는가
- 구현해야 할 파일/패턴/모드가 하나도 빠지지 않았는가
- 검증 범위가 모든 패턴/기본 size를 덮는가
- multi recv 패턴의 현재 구현 수준을 과대평가하지 않았는가
- handshake 구현 수준을 monitor 기반 readiness로 정확히 분류했는가
- warmup 모델 갭의 처리 scope가 명시됐는가
- core 버그 대응 규칙이 구현 인벤토리에 포함되어 있는가

## Phase 1. surface / 문서 / 작업 트리 정비

목표:
- `core/perf`를 소스 트리로 유지하고, 캐시/임시물이 구조로 정착되는 것을 막는다.

작업:
- [`core/perf/.gitignore`](/home/hep7/project/kairos/zlink/core/perf/.gitignore)에
  아래 성격의 항목을 추가 검토한다.
  - `__pycache__/`
  - `single/__pycache__/`
  - `multi/__pycache__/`
  - `single/tmp/`
  - `tmp/`
  - `common/streamclient/build/`
- 이미 생성된 캐시/임시 산출물은 정리한다.
- 결과 디렉터리는 정책상 허용되는 골격만 유지하고, 임시 폴더와 혼용되지 않게
  한다.

주의:
- 결과 파일 보존 정책 자체를 약화하지 않는다.
- 실제 성능 결과물(`results/.../report/*.txt`)과 캐시/임시물은 분리해서 다룬다.

완료 조건:
- perf 실행 후에도 불필요한 캐시 산출물이 작업 트리에 남지 않는다.
- README/정책/usage surface가 앞으로의 구현 범위를 수용할 수 있다.

리뷰 게이트:
- README가 사용법만 남겼는가
- 정책 문서가 source of truth 역할을 하는가
- build path / results path / ignore 규칙이 서로 충돌하지 않는가

## Phase 2a. 모드 전달 경로 확정 + runner / comparison layer 구현

목표:
- 정책 문서가 정의한 recv model 선택을 실제 공식 CLI surface로 노출한다.

설계 계약:
- `recv` 모드: `POLLIN` 기반 recv drain + `POLLOUT` 기반 send backpressure
- `callback` 모드: recv callback + send-ready callback 기반 backpressure
- 두 모드는 같은 측정 구간에서 혼용하지 않는다.
- handshake/start gate는 socket/service monitor 기반 readiness를 사용한다.

작업:
- 바이너리 모드 전달 방식을 확정한다.
  - same binary + CLI arg / env / split binary 중 하나 선택
  - 선택 근거와 파급 파일을 기록
- [`core/perf/run_benchmarks.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh)
  에 `--recv recv|callback` 옵션을 추가한다.
- [`core/perf/run_benchmarks_multi.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks_multi.sh)
  에도 동일한 옵션을 추가하고, 내부 전달 경로를 정렬한다.
- [`core/perf/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/single/run_comparison.py)
  와 [`core/perf/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/run_comparison.py)
  에서 `--recv` 파싱, effective options 출력, 하위 프로세스 전달을 구현한다.
- single/multi 비교 스크립트가 선택된 recv 모델을 하위 바이너리 실행과
  결과 출력에 반영하는지 확인한다.
- pattern별 현재 상태와 최종 목표 상태를 분리해서 관리한다.
  - 현재 상태 매트릭스
  - 최종 목표 매트릭스
  - 패턴별 갭 목록
- 모든 패턴을 `recv` / `callback` 둘 다 지원하도록 구현 범위를 고정한다.
- single/multi 공통 메트릭 계산 함수와 출력 함수를 식별하고, 중복 로직을
  공통 모듈로 수렴시키는 계획을 함께 수립한다.
- single/multi 각 pattern이 monitor-ready 이후에만 측정을 시작하도록 start gate
  전달 경로를 정리한다.
- single/multi 각각에 대해 mode별 backpressure owner를 문서화한다.
  - recv 모드: poller readiness owner, pending state owner, drain completion owner
  - callback 모드: recv callback owner, send_ready callback owner, pending state owner
- echo 서버 / echo 클라이언트 / one-way sender / one-way receiver별로
  pending 모델을 분리해 적는다.
- 시작/결과 옵션 요약에 recv 모델이 기록되게 한다.

구체 확인 항목:
- 기본값은 `recv`
- `callback` 지정 시 recv API loop와 섞이지 않음
- help/usage/README 예시 반영
- pattern별 callback 지원 범위를 문서에서 즉시 알 수 있음
- `recv` 모드에서 `POLLIN` drain + `POLLOUT` backpressure가 구조로 드러남
- `callback` 모드에서 recv callback + send-ready callback 조합이 구조로 드러남
- handshake/start gate가 monitor event 기반으로 고정됨
- single/multi 모든 패턴이 최종적으로 두 모드를 모두 갖도록 갭이 관리됨
- 바이너리가 모드를 어떻게 아는지가 모호하지 않음

완료 조건:
- 팀원이 공식 runner에서 `--recv recv` / `--recv callback`를 선택하면 그 값이
  실제 바이너리 실행까지 손실 없이 전달된다.

산출물:
- mode별 실행 계약 섹션
- 현재 상태 매트릭스
- 최종 목표 매트릭스
- backpressure ownership 표
- 메트릭/출력 공통화 대상 목록
- 모드 전달 경로 결정 기록

리뷰 게이트:
- shell wrapper와 comparison layer 사이 인자 손실이 없는가
- effective options / 결과 파일 / stdout table이 동일한 recv mode를 보여주는가
- mode default가 모든 경로에서 `recv`로 일관적인가
- 바이너리 전달 경로가 CMake/runner/spawn 로직과 충돌하지 않는가
- monitor-ready 이전 측정 시작 경로가 남아 있지 않은가
- core 버그를 runner/workaround로 숨기는 경로가 없는가

## Phase 2b. single callback-only 패턴에 recv 모드 추가

목표:
- 현재 callback-only인 single 전 패턴에 recv 모드를 추가한다.

작업:
- single recv 모드의 send-path architecture를 확정한다.
  - sender/receiver 통합 poller event loop
  - sender thread 유지 + nonblocking send + `POLLOUT`
  중 하나 선택
- `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`,
  `GATEWAY`, `SPOT`에 recv 모드 구현

완료 조건:
- single 전 패턴이 `recv` / `callback` 두 모드 모두를 가진다.

리뷰 게이트:
- single recv 모드에서 send path 아키텍처가 명확히 결정됐는가
- mode contract가 모든 single 패턴에서 동일한가
- callback regression 없이 recv 모드가 추가됐는가
- core 결함이 발견되면 회귀 테스트 추가 후 수정하는 흐름으로 처리됐는가

## Phase 2c. multi recv-only 패턴에 callback 모드 추가

목표:
- 현재 recv-only 또는 partial인 multi 패턴에 callback 모드를 추가한다.

작업:
- `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`, `PUBSUB` callback 모드 구현
- `SPOT` recv 모드 정책 정렬
- `GATEWAY`, `STREAM_CALLBACK` recv 모드 구현 필요 여부 확인 후 반영

완료 조건:
- multi 전 패턴이 `recv` / `callback` 두 모드 모두를 가진다.

리뷰 게이트:
- echo/server/client별 pending 모델이 정책 계약과 일치하는가
- callback 경로가 send-ready handler 기반 backpressure를 실제로 사용하는가
- partial/특수 케이스가 남지 않았는가
- core 결함이 발견되면 회귀 테스트 추가 후 수정하는 흐름으로 처리됐는가

## Phase 2d. full matrix 점검

목표:
- 2b/2c 결과를 합쳐 full matrix 상태로 수렴시킨다.

작업:
- 현재 상태 매트릭스를 갱신한다.
- 남은 gap pattern을 제거한다.
- single/multi 전 패턴 dual-mode를 다시 점검한다.

완료 조건:
- 현재 상태 매트릭스가 최종 목표 매트릭스와 동일해진다.

리뷰 게이트:
- 지원 안 되는 패턴/size/mode가 하나도 남지 않았는가
- 임시 분기/임시 env/temporary bypass가 제거됐는가

## Phase 3. 공통 엔진 구현

목표:
- single/multi가 공유할 수 있는 메트릭/출력/phase/backpressure 엔진을 만든다.

작업:
- 메트릭 계산 공통화
- 출력 포맷 공통화
- monitor handshake / start gate 공통화
- `recv` 모드 공통 엔진 구현
- `callback` 모드 공통 엔진 구현
- phase state machine 공통화

완료 조건:
- runner와 pattern 구현이 공통 엔진을 사용해도 측정 의미가 유지된다.

리뷰 게이트:
- 공통화가 복잡도 감소로 이어지는가
- mode contract가 helper 안에서 깨지지 않는가
- handshake가 helper 안에서 sleep/retry loop로 변질되지 않았는가
- hot path에 retry/sleep/log/alloc가 새로 들어오지 않았는가
- core 버그를 helper 우회로 감추지 않았는가

## Phase 4. single full-matrix 확인 및 누락 정리

목표:
- single 전체 매트릭스가 실제로 닫혔는지 확인하고 남은 누락을 정리한다.

작업:
- single 전 패턴의 mode/size 결과를 다시 점검한다.
- Phase 2b, Phase 3 이후 남은 partial/edge case를 정리한다.
- single 결과 파일/테이블/metric 누락 여부를 재확인한다.

완료 조건:
- single 전체 패턴이 두 모드 모두에서 정책 기본 size 전부 `success`다.

리뷰 게이트:
- 각 패턴이 두 모드 모두에서 동일한 측정 의미를 유지하는가
- size별 partial success가 없는가
- single 결과 파일과 table 출력이 모드별로 일관적인가

## Phase 5. multi full-matrix 확인 및 누락 정리

목표:
- multi 전체 매트릭스가 실제로 닫혔는지 확인하고 남은 누락을 정리한다.

작업:
- multi 전 패턴의 mode/size 결과를 다시 점검한다.
- Phase 2c, Phase 3 이후 남은 partial/edge case를 정리한다.
- server/client 결과 파일/테이블/metric 누락 여부를 재확인한다.

완료 조건:
- multi 전체 패턴이 두 모드 모두에서 정책 기본 size 전부 `success`다.

리뷰 게이트:
- server/client 역할별 backpressure 계약이 맞는가
- mode별 pending owner가 일관적인가
- 모든 패턴/size에서 no_data/partial 없이 성공하는가

## Phase 6. 문서와 build path 최종 정렬

목표:
- 저장소 전체 규칙과 perf 전용 스크립트가 같은 빌드 경로 모델을 사용하게 한다.

작업:
- [`core/perf/CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/perf/CMakeLists.txt)
  상단 주석의 예시를 `core/build/` 기준으로 수정한다.
- [`core/perf/run_benchmarks.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh)
  의 기본 build-dir 해석과 help 문구를 `core/build/` 기준으로 정렬한다.
- [`core/perf/run_benchmarks_multi.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks_multi.sh)
  의 help 문구와 README 예시도 같은 기준을 사용하게 한다.

검토 포인트:
- "auto-detect 여러 경로"가 정말 필요한가
- 필요하다면 내부 호환만 남기고 문서/공식 surface에는 드러내지 않을 수 있는가
- 사용자가 루트 `build/`를 공식 경로로 오해할 여지를 없앴는가

완료 조건:
- 공식 문서와 help에서 `core/build/` 외 경로를 권장하지 않는다.

리뷰 게이트:
- 문서 예시와 실제 구현 명령이 정확히 일치하는가
- 정책/README/help 사이 충돌 문장이 없는가

## Phase 7. 결과 저장 규칙과 보존 정책 재확인

목표:
- 정책 문서가 정의한 결과 저장 규칙이 구현 설명과 동일하게 보이도록 만든다.

작업:
- single/multi 결과 파일 명명 예시를 정책과 같은 형식으로 맞춘다.
- README와 스크립트 help에서 결과 디렉터리 설명을 정책 wording과 맞춘다.
- 필요하면 결과 디렉터리 생성 책임을 실행 스크립트로 단순화해 설명한다.

완료 조건:
- 결과가 어디에 어떻게 쌓이는지 정책, README, 실행기 help가 같은 문장을 말한다.

리뷰 게이트:
- result file naming / location / retention이 정책과 일치하는가
- 결과 파일 이름만 보고 `recv` / `callback` 모드를 즉시 구분할 수 있는가
- `Effective Options (start/result)`에 `recv_mode`가 빠지지 않는가
- mode별 결과 파일에 옵션과 metric 누락이 없는가

## Phase 8. 전체 매트릭스 검증 및 종료 리뷰

목표:
- 최종 목표 매트릭스를 실제 실행으로 닫고 종료 가능 상태를 만든다.

검증 명령:

```bash
cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON
cmake --build core/build
./core/perf/run_benchmarks.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern PAIR --transports tcp --msg-sizes 64 --runs 1 --duration 1
./core/perf/run_benchmarks.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern PAIR --transports tcp --msg-sizes 64 --runs 1 --duration 1 --recv callback
./core/perf/run_benchmarks_multi.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern DEALER_DEALER --transports tcp --msg-sizes 64 --runs 1 --duration 1 --clients 10
```

검증 기준:
- 실행기가 `core/build/` 기준으로 정상 동작한다.
- `--recv recv` / `--recv callback` 선택이 single/multi 모든 패턴에서 실행 및
  결과 옵션에 반영된다.
- `recv` 모드에서는 `POLLIN` recv drain과 `POLLOUT` backpressure 경로가 실제로
  동작한다.
- `callback` 모드에서는 recv callback과 send-ready callback backpressure 경로가
  실제로 동작한다.
- 결과 파일이 정책 경로에 저장된다.
- README 예시와 실제 실행 경로가 일치한다.
- 캐시/임시물 재유입이 없다.
- 지원 매트릭스 표가 실제 코드 상태와 일치한다.
- 정책 기본 size 전부에서 누락 없이 success를 낸다.
- size별 partial 성공이나 mode별 partial 성공은 완료로 인정하지 않는다.

종료 리뷰 체크리스트:
- 현재 상태 매트릭스가 최종 목표 매트릭스로 완전히 수렴했는가
- single/multi 모든 패턴의 기본 size 전부가 `recv` / `callback` 모두 success인가
- 공식 runner 외 우회 경로 없이 재현 가능한가
- 문서/usage/help/result format/build path가 서로 일치하는가
- 결과 파일명 규칙이 정책/README/runner 구현에 일관되게 반영됐는가
- 남은 TODO, 임시 분기, 임시 env, 부분 지원 패턴이 없는가
- 바이너리 전달 경로 결정이 코드/문서/빌드 설정 전부에 일관되게 반영됐는가
- handshake/start gate가 monitor 기반 readiness로 구현됐는가
- 진행 중 발견된 core 버그마다 회귀 테스트가 추가되었고, 우회 없이 수정으로
  해결되었는가

## 9. 반복 리뷰 방식

각 phase는 아래 순서로 반복 리뷰한다.

1. 구현
2. 자체 리뷰
3. 누락 수정
4. 재실행
5. phase review 기록
6. 다음 phase 진행

각 phase review에는 최소 아래를 남긴다.

- 변경 파일 목록
- 구현된 패턴/모드
- 남은 갭 목록
- 실행한 검증 명령
- 실패/partial 항목
- 발견한 core 버그와 추가한 회귀 테스트
- 다음 phase 진입 조건 충족 여부

### 9.1 phase review 기록 템플릿

```md
## Phase X Review

- phase:
- date:
- owner:
- mode delivery decision:
- implemented patterns:
- validated modes:
- validated sizes:
- changed files:
- commands run:
- results:
- failures/partial:
- remaining gaps:
- next phase ready: yes/no
```

### 9.2 결정 기록 템플릿

새 컨텍스트에서 의사결정이 흩어지지 않도록, 아래 형식으로 문서 안에 바로
결정 로그를 남긴다.

## Decision Log

### D-001 mode delivery
- chosen: env var (`PERF_RECV_MODE`) + 공식 runner/comparison layer의 `--recv`
  surface
- alternatives considered:
  - same binary + CLI arg
  - split binary
- reason:
  - single/multi 전 패턴 바이너리 인자 파서를 한 번에 재작성하지 않고도
    공식 surface에서 mode를 손실 없이 전달할 수 있다.
  - shell wrapper, Python runner, 결과 파일명, Effective Options를 먼저
    정렬해 정책 surface를 고정하는 데 가장 작은 변경 증폭으로 대응된다.
- affected files:
  - [`core/perf/run_benchmarks.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh)
  - [`core/perf/run_benchmarks_multi.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks_multi.sh)
  - [`core/perf/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/single/run_comparison.py)
  - [`core/perf/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/run_comparison.py)
  - [`core/perf/CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/perf/CMakeLists.txt)

### D-002 single recv send-path
- chosen: follow-up required
- alternatives considered:
  - sender/receiver 통합 poller event loop
  - sender thread 유지 + nonblocking send + `POLLOUT`
- reason:
  - single 전 패턴을 공통 recv engine으로 옮기기 전에 현재 callback-only 구현과
    `core/bench/with_zmq/single/zlink`의 recv-path를 비교 검증해야 한다.
  - 이번 턴에서는 runner/documentation/surface 정렬까지만 반영했고, phase 2b의
    실제 엔진 선택과 패턴 적용은 미완료다.
- affected files:
  - 아직 미구현. phase 2b에서 single pattern 소스와 공통 helper를 함께 수정해야
    한다.

### D-003 warmup scope
- chosen: follow-up
- in-scope or follow-up:
  - single warmup count-vs-seconds 갭은 이번 턴에서 해결하지 못했다.
  - 공식 runner/help에는 "env override"로만 축소 표기하고, 정책 기준은 계속
    [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](/home/hep7/project/kairos/zlink/doc/perf/PERF_SINGLE_TEST_POLICY.md)
    로 유지한다.
- affected docs/files:
  - [`core/perf/README.md`](/home/hep7/project/kairos/zlink/core/perf/README.md)
  - [`core/perf/README_KO.md`](/home/hep7/project/kairos/zlink/core/perf/README_KO.md)
  - [`core/perf/run_benchmarks.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh)

### D-004 core bug handling
- bug summary: 없음
- regression test added: 없음
- fix summary: 없음
- affected files: 없음

## Phase Review

### Phase 0 Review

- phase: 0
- date: 2026-03-18
- owner: Codex
- mode delivery decision: env var + `--recv` surface로 확정 (D-001)
- implemented patterns: 없음
- validated modes: 현재 구현 기준 확인만 수행
- validated sizes: `PAIR/tcp/64`, `DEALER_DEALER/tcp/64`
- changed files:
  - [`doc/plan/perf-refactor/core-perf-policy-alignment-plan.ko.md`](/home/hep7/project/kairos/zlink/doc/plan/perf-refactor/core-perf-policy-alignment-plan.ko.md)
- commands run:
  - `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
  - `cmake --build core/build -j$(nproc)`
  - `./core/perf/run_benchmarks.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern PAIR --transports tcp --msg-sizes 64 --runs 1 --duration 1`
  - `./core/perf/run_benchmarks_multi.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern DEALER_DEALER --transports tcp --msg-sizes 64 --runs 1 --duration 1 --clients 10`
- results:
  - single/multi baseline 실행과 현재 surface/output 갭을 재확인했다.
  - multi `recv만 구현` 경로는 정책이 요구한 reactor recv / callback dual-mode로
    닫혀 있지 않다.
- failures/partial:
  - full matrix 미검증
  - single warmup 정책 갭 미해결
- remaining gaps:
  - phase 2b~5 전부
  - monitor 기반 common engine 미구현
- next phase ready: yes

### Phase 1 Review

- phase: 1
- date: 2026-03-18
- owner: Codex
- mode delivery decision: 유지
- implemented patterns: 없음
- validated modes: N/A
- validated sizes: N/A
- changed files:
  - [`core/perf/.gitignore`](/home/hep7/project/kairos/zlink/core/perf/.gitignore)
  - [`core/perf/README.md`](/home/hep7/project/kairos/zlink/core/perf/README.md)
  - [`core/perf/README_KO.md`](/home/hep7/project/kairos/zlink/core/perf/README_KO.md)
- commands run:
  - `python3 -m unittest core/perf/single/tests/test_run_comparison_policy.py core/perf/single/tests/test_multi_run_comparison_policy.py`
- results:
  - perf tree ignore 규칙을 강화했다.
  - README를 policy 링크 중심의 얇은 surface로 축소했다.
- failures/partial:
  - 기존 캐시/임시 디렉터리 자체는 작업 환경 정책 때문에 제거하지 못했다.
- remaining gaps:
  - 실제 binary dual-mode 미구현
- next phase ready: yes

### Phase 2a Review

- phase: 2a
- date: 2026-03-18
- owner: Codex
- mode delivery decision: env var (`PERF_RECV_MODE`)
- implemented patterns:
  - runner/comparison layer only
- validated modes:
  - single `PAIR`: `recv`, `callback`
  - multi `DEALER_DEALER`: `recv`, `callback`
- validated sizes: `64`
- changed files:
  - [`core/perf/run_benchmarks.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh)
  - [`core/perf/run_benchmarks_multi.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks_multi.sh)
  - [`core/perf/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/single/run_comparison.py)
  - [`core/perf/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/run_comparison.py)
  - [`core/perf/single/tests/test_run_comparison_policy.py`](/home/hep7/project/kairos/zlink/core/perf/single/tests/test_run_comparison_policy.py)
  - [`core/perf/single/tests/test_multi_run_comparison_policy.py`](/home/hep7/project/kairos/zlink/core/perf/single/tests/test_multi_run_comparison_policy.py)
- commands run:
  - `python3 -m unittest core/perf/single/tests/test_run_comparison_policy.py core/perf/single/tests/test_multi_run_comparison_policy.py`
  - `./core/perf/run_benchmarks.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern PAIR --transports tcp --msg-sizes 64 --runs 1 --duration 1 --recv callback`
  - `./core/perf/run_benchmarks_multi.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern DEALER_DEALER --transports tcp --msg-sizes 64 --runs 1 --duration 1 --clients 10 --recv recv`
  - `./core/perf/run_benchmarks_multi.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern DEALER_DEALER --transports tcp --msg-sizes 64 --runs 1 --duration 1 --clients 10 --recv callback`
- results:
  - 공식 runner, comparison layer, 결과 파일명, Effective Options start/result에
    `recv_mode`가 반영된다.
  - 결과 파일명 규칙은 `perf_<platform>_<recv_mode>_YYYYMMDD_HHMMSS[_<tag>].txt`
    로 정렬됐다.
- failures/partial:
  - binary는 아직 `PERF_RECV_MODE`를 실제 측정 엔진 선택에 사용하지 않는다.
  - full matrix validation 미완료.
- remaining gaps:
  - phase 2b~5 핵심 구현
  - mode별 backpressure/common engine 정렬
- next phase ready: no

### Phase 6 Review

- phase: 6
- date: 2026-03-18
- owner: Codex
- mode delivery decision: 유지
- implemented patterns: N/A
- validated modes: surface only
- validated sizes: N/A
- changed files:
  - [`core/perf/CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/perf/CMakeLists.txt)
  - [`core/perf/README.md`](/home/hep7/project/kairos/zlink/core/perf/README.md)
  - [`core/perf/README_KO.md`](/home/hep7/project/kairos/zlink/core/perf/README_KO.md)
- commands run:
  - `cmake -S . -B core/build -DZLINK_BUILD_TESTS=ON`
  - `cmake --build core/build -j$(nproc)`
- results:
  - 공식 문서 예시와 CMake 주석을 `core/build` 기준으로 정렬했다.
- failures/partial:
  - 일부 Python helper 내부의 레거시 build-dir fallback은 호환 목적으로 남아 있다.
- remaining gaps:
  - README/help와 실제 mode engine semantics 불일치
- next phase ready: yes

### Phase 2b Review

- phase: 2b
- date: 2026-03-18
- owner: Codex
- mode delivery decision: 유지. runner/comparison layer는 `PERF_RECV_MODE`를 기준으로
  recv 전용 바이너리 선택까지 수행한다.
- implemented patterns:
  - single `PAIR`, `PUBSUB`, `DEALER_DEALER`, `DEALER_ROUTER`, `ROUTER_ROUTER`
    recv 전용 바이너리 추가
  - single `GATEWAY`, `SPOT`는 아직 기존 callback 경로 유지
- validated modes:
  - single `PAIR`: `recv`
  - single `ALL/tcp`: `recv`
- validated sizes:
  - `PAIR/tcp/64`
  - single `ALL/tcp`: `64, 256, 1024, 65536, 131072, 262144`
- changed files:
  - [`core/perf/CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/perf/CMakeLists.txt)
  - [`core/perf/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/single/run_comparison.py)
  - [`core/perf/single/tests/test_run_comparison_policy.py`](/home/hep7/project/kairos/zlink/core/perf/single/tests/test_run_comparison_policy.py)
  - [`core/bench/with_zmq/single/common/bench_common_zlink.hpp`](/home/hep7/project/kairos/zlink/core/bench/with_zmq/single/common/bench_common_zlink.hpp)
- commands run:
  - `cmake --build core/build -j$(nproc)`
  - `python3 -m unittest core/perf/single/tests/test_run_comparison_policy.py core/perf/single/tests/test_multi_run_comparison_policy.py`
  - `./core/perf/run_benchmarks.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern PAIR --transports tcp --msg-sizes 64 --runs 1 --duration 1 --recv recv`
  - `./core/perf/run_benchmarks.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern ALL --transports tcp --runs 1 --duration 1 --recv recv`
- results:
  - single socket 패턴의 `recv` 모드는 기존 callback 전용 바이너리 대신 recv 전용
    바이너리를 사용하도록 연결됐다.
  - single `ALL/tcp` 기본 size 전부가 `recv`로 complete를 기록했다.
- failures/partial:
  - `GATEWAY`, `SPOT` recv 모드는 여전히 callback 구현에 의존한다.
  - transport matrix 전체와 callback full matrix는 아직 닫지 못했다.
- remaining gaps:
  - single service pattern recv 엔진
  - multi dual-mode 실엔진
  - phase 2c~5, phase 8
- next phase ready: no

### Phase 7 Review

- phase: 7
- date: 2026-03-18
- owner: Codex
- mode delivery decision: 유지
- implemented patterns: N/A
- validated modes:
  - single `PAIR`: callback filename/options 확인
  - multi `DEALER_DEALER`: recv/callback filename/options 확인
- validated sizes: `64`
- changed files:
  - [`core/perf/run_benchmarks.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh)
  - [`core/perf/run_benchmarks_multi.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks_multi.sh)
  - [`core/perf/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/single/run_comparison.py)
  - [`core/perf/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/run_comparison.py)
- commands run:
  - `./core/perf/run_benchmarks.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern PAIR --transports tcp --msg-sizes 64 --runs 1 --duration 1 --recv callback`
  - `./core/perf/run_benchmarks_multi.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern DEALER_DEALER --transports tcp --msg-sizes 64 --runs 1 --duration 1 --clients 10 --recv recv`
  - `./core/perf/run_benchmarks_multi.sh --reuse-build --build-dir /home/hep7/project/kairos/zlink/core/build --pattern DEALER_DEALER --transports tcp --msg-sizes 64 --runs 1 --duration 1 --clients 10 --recv callback`
- results:
  - result filename / Effective Options start/result / runner output에
    `recv_mode`가 노출된다.
- failures/partial:
  - 실제 dual-mode 엔진 검증은 되지 않았다.
- remaining gaps:
  - phase 2b~5, phase 8 종료 기준 전부
- next phase ready: no

## 10. 변경 대상과 비대상

### 변경 대상

- [`core/perf/.gitignore`](/home/hep7/project/kairos/zlink/core/perf/.gitignore)
- [`core/perf/README.md`](/home/hep7/project/kairos/zlink/core/perf/README.md)
- [`core/perf/README_KO.md`](/home/hep7/project/kairos/zlink/core/perf/README_KO.md)
- [`core/perf/CMakeLists.txt`](/home/hep7/project/kairos/zlink/core/perf/CMakeLists.txt)
- [`core/perf/run_benchmarks.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks.sh)
- [`core/perf/run_benchmarks_multi.sh`](/home/hep7/project/kairos/zlink/core/perf/run_benchmarks_multi.sh)
- [`core/perf/single/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/single/run_comparison.py)
- [`core/perf/run_comparison.py`](/home/hep7/project/kairos/zlink/core/perf/run_comparison.py)
- `--recv` 지원 범위에 따라 필요한 single/multi 개별 벤치 바이너리

### 원칙적으로 비대상

- RESULT line 포맷
- 정책 문서가 이미 정의한 phase semantics
- 성능 개선 목적의 hot path 변경

단, `--recv` 도입에 필수적인 범위의 인자 전달, 지원 패턴 확장, 최소 측정 경로
보정은 허용한다.

## 11. 리스크와 대응

### 리스크 1. README를 줄이다가 실제 사용 정보까지 사라질 수 있다

대응:
- README는 얇게 만들되, 공식 명령 예시와 정책 링크는 남긴다.

### 리스크 2. 빌드 경로 정렬 중 레거시 사용 흐름이 깨질 수 있다

대응:
- 내부 호환이 필요하면 일단 유지하되, 공식 surface에서는 감춘다.
- 최종적으로 제거할지는 실제 CI/개발자 사용 흔적을 본 뒤 결정한다.

### 리스크 3. 결과물 정리와 정책상 허용된 결과 저장을 혼동할 수 있다

대응:
- `results/`는 허용된 산출물, `tmp/__pycache__/build`는 제거 대상이라는 경계를
  문서에 명시한다.

### 리스크 4. `--recv`가 단순 옵션처럼 보이면서 실제 구현 복잡도가 숨겨질 수 있다

대응:
- mode별 실행 계약, backpressure 모델, pattern별 지원 매트릭스를 별도 섹션으로
  문서화한다.
- 지원 대상 패턴과 추가 구현 필요 패턴을 분리해 rollout 범위를 고정한다.

### 리스크 5. 일부 패턴만 dual-mode가 되고 나머지가 남은 상태에서 완료로 오인될 수 있다

대응:
- 현재 상태 매트릭스와 최종 목표 매트릭스를 분리한다.
- 완료 판정에 "모든 패턴/모든 기본 size/두 모드 모두 success"를 명시한다.

## 12. 완료 판정 문장

이 계획의 완료는 "`core/perf`가 `doc/perf` 정책을 다시 설명하느라 충돌을
만드는 디렉터리"에서 "`정책을 그대로 실행하는 얇은 reference implementation
surface`"로 바뀌는 것을 의미한다.

즉, 완료 후에는 아래가 동시에 성립해야 한다.

- `doc/perf`가 규칙의 단일 source of truth다.
- `core/perf`는 그 규칙을 어기지 않는 실행기와 소스 구조만 제공한다.
- `core/build/` only 규칙이 문서, 스크립트, 예시에 일관되게 반영된다.
- 소스 트리 안에 정책 외 캐시/임시물이 구조적으로 남지 않는다.
