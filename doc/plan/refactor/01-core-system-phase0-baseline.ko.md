# `[01]` `core` 시스템 리팩토링 Phase 0 Baseline

> 상태: draft
> 목적: 구조 리팩토링 시작 전 기능/성능 기준선 고정

| nav | link |
| --- | --- |
| 목록 | [README](README.ko.md) |
| 이전 | [00 상위 계획](00-core-system-posd-refactor-plan.ko.md) |
| 다음 | [02 Phase 1 Ownership Map](02-core-system-phase1-ownership-map.ko.md) |

## 1. 목적

이 문서는 `core` 전체 구조 리팩토링에 들어가기 전에
현재 워크스페이스의 기능/성능 기준선을 고정하기 위한 실행 문서다.

Phase 0의 목적은 단순하다.

- 나중에 "무엇이 좋아졌는지"보다 먼저 "무엇이 깨졌는지"를 즉시 알 수 있게 한다.
- 구조 변경 승인을 감이나 인상에 의존하지 않게 만든다.
- 이후 phase가 공통으로 참조하는 baseline 파일과 실행 명령을 고정한다.

## 2. 리팩토링 전 실측 baseline 입력

이 절의 baseline은 자동으로 확정하지 않는다.
리팩토링 전 실측 데이터는 팀장님이 제공하는 값을 기준으로 채운다.

즉 이 문서에서 baseline은 "현재 워크스페이스의 최근 파일"이 아니라
"리팩토링 직전 실측하여 승인된 기준 데이터"다.

### 2.1 baseline commit

single과 multi는 **반드시 같은 기준 commit**을 사용한다.
commit이 다르면 baseline으로 승인하지 않는다.

- baseline commit: `1901fef8`
- tree 상태: clean
- 실행 옵션 요약: Release, Linux 6.6.87.2-microsoft-standard-WSL2, Intel Core Ultra 7 265K (20 cores)
- 측정 일시: 2026-03-16
- 승인 메모:

### 2.2 single 실측 baseline

- 보고서 파일 경로: [perf_linux_20260316_163947.txt](perf_linux_20260316_163947.txt)
- runs: 1
- duration_seconds: 5
- io_threads: 2
- patterns: PAIR, PUBSUB, DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, GATEWAY, SPOT
- transports: inproc, ipc, tcp, tls, ws, wss
- msg_sizes: 64, 256, 1024, 65536, 131072, 262144

### 2.3 multi 실측 baseline

- 보고서 파일 경로: [perf_linux_20260316_170306.txt](perf_linux_20260316_170306.txt)
- build: Release
- clients: 100
- runs: 1
- patterns: DEALER_DEALER, DEALER_ROUTER, ROUTER_ROUTER, PUBSUB, GATEWAY, SPOT, STREAM_CALLBACK
- transports: tcp, tls, ws, wss
- msg_sizes: 64, 256, 1024, 65536, 131072, 262144
- 승인 메모:

### 2.4 입력 규칙

팀장님이 실측 데이터를 제공하면 아래 원칙으로 이 절을 채운다.

- single과 multi는 **반드시 같은 baseline commit**에서 측정한다.
  commit이 다르면 baseline으로 승인하지 않는다.
- tree 상태는 **clean**(uncommitted change 없음)이어야 한다.
- 실행 옵션 요약(build type, compiler, OS, CPU)을 필수로 남긴다.
- 보고서 파일 경로를 함께 남긴다.
- 이후 perf gate는 이 절에 기록된 실측 baseline을 기준으로 본다.

## 3. 기능 기준선 명령

리팩토링 시작 전에 아래 명령을 기준 명령으로 사용한다.

```bash
./core/tests/run_test_lanes.sh
./core/tests/run_test_lanes.sh --include-e2e
```

설명:

- 기본 acceptance는 lane runner 기준으로 본다.
- `e2e`는 구조 변경 범위가 service/lifecycle/monitoring까지 닿을 때 반드시 포함한다.
- thread-safe/lifecycle 변경이 포함되면 관련 stress/tsan 보조 lane도 별도 기록한다.

## 4. 성능 기준선 명령

single:

```bash
./core/perf/run_benchmarks.sh --pattern ALL
```

multi:

```bash
./core/perf/run_benchmarks_multi.sh --pattern ALL
```

주의:

- 정책상 실행 진입점은 반드시 위 스크립트를 사용한다.
- 환경 변수 조합으로 스크립트를 우회하지 않는다.
- 팀장님이 제공한 실측 결과를 baseline 절에 먼저 반영한 뒤 비교한다.
- 구조 리팩토링 단계에서 benchmark harness(벤치마크 실행 환경/도구)를 수정했다면
  harness 변경 전/후 baseline을 분리해서 남긴다.

## 5. 우선 감시 패턴

전체 패턴을 다 보되,
구조 리팩토링에서는 아래를 1차 감시 패턴으로 본다.

### 5.1 single

- `PAIR`
- `PUBSUB`
- `DEALER_ROUTER`
- `GATEWAY`
- `SPOT`

### 5.2 multi

- `DEALER_DEALER`
- `PUBSUB`
- `GATEWAY`
- `SPOT`
- `STREAM_CALLBACK`

이 패턴들이 중요한 이유는 다음과 같다.

- socket 의미 계층, service 계층, engine/transport 계층의 회귀를 넓게 잡을 수 있다.
- `GATEWAY`, `SPOT`은 ownership과 lifecycle 회귀를 빠르게 드러낸다.
- `STREAM_CALLBACK`은 callback fast path와 transport stack 경계를 같이 건드린다.

## 6. hot path(성능 핵심 경로) 관찰 포인트

Phase 0에서는 성능 숫자만 보지 않고,
다음 관찰 포인트를 같이 고정한다.

```text
관찰 영역 전체 맵

  data path (steady-state)              lifecycle path (setup/teardown)
  ─────────────────────────             ──────────────────────────────
  send/recv loop                        socket create → open → active
      |                                     |
      v                                     v
  socket_base_t                         service_runtime_base_t
  (xsend/xrecv)                         (state machine, registry)
      |                                     |
      v                                     v
  asio_engine_t                         socket_base_t
  (speculative I/O,                     (process_destroy, check_destroy,
   gather write,                         finalize_destroy)
   buffer)                                  |
      |                                     v
      v                                 reaper_t
  transport                             (final free)
  (tcp/ipc/ws/wss/tls)

  주 관찰:                              주 관찰:
  - alloc/copy 증가 여부                - close owner 중복 여부
  - branch 증가 여부                    - drain 대기 경로
  - callback depth 증가 여부            - sleep 기반 의존 여부
```

### 6.1 socket / lifecycle

- [socket_base.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.hpp)
- [socket_base.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp)
- [own.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/own.hpp)
- [own.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/own.cpp)
- [reaper.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/reaper.hpp)
- [reaper.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/reaper.cpp)

주 관찰 항목:

- destroy path에서 close owner 중복 여부
- mailbox / reaper 경유 비용 증가 여부
- drain 대기 경로의 sleep 기반 의존 여부

### 6.2 engine / transport

- [asio_engine.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/engine/asio/asio_engine.hpp)
- [asio_engine.cpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/engine/asio/asio_engine.cpp)
- [i_engine.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/engine/i_engine.hpp)

주 관찰 항목:

- read/write steady-state 경로의 branch 증가 여부
- speculative I/O / gather write 훼손 여부
- handshake 이후 callback depth 증가 여부

### 6.3 service

- [service_runtime_base.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/common/service_runtime_base.hpp)
- [service_control_runtime.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/control/service_control_runtime.hpp)
- [gateway_runtime.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/gateway/gateway_runtime.hpp)
- [spot_runtime.hpp](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/spot/spot_runtime.hpp)

주 관찰 항목:

- service stop/destroy 경로의 owner 단일화 여부
- monitor/readiness/state transition의 일관성
- service runtime에서 internal socket topology 누수 여부

## 7. baseline 저장 규칙

리팩토링 각 phase 시작 전후로 아래를 남긴다.

- 기준 commit hash
- single 보고서 파일 경로
- multi 보고서 파일 경로
- 실행 명령
- 실측 제공자 또는 승인자
- 변경된 주요 파일 목록
- regression 여부
- regression이 있으면 원인 후보 1차 메모

## 8. Phase 0 종료 조건

아래가 모두 충족되면 Phase 0 완료로 본다.

- 팀장님이 제공한 리팩토링 전 실측 baseline이 문서에 반영되어 있다.
- 기능/성능 실행 명령이 고정되어 있다.
- 주요 hot path 관찰 파일이 정리되어 있다.
- 이후 phase가 baseline 없이 진행되지 않는다.

## 9. 다음 단계

Phase 0 다음 작업은 Phase 1 ownership map 정리다.

우선 대상:

- `socket_base`
- `own`
- `reaper`
- `service_runtime_base`
- `gateway_runtime`
- `spot_runtime`

이 단계가 끝나기 전에는
대규모 파일 이동이나 public facade 정리를 먼저 하지 않는다.

참조 문서:

- `doc/perf/refactor/02-core-system-phase1-ownership-map.ko.md`
- `doc/perf/refactor/03-core-system-phase1-resource-inventory.ko.md`
