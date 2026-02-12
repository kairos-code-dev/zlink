# SPOT Pub/Sub Thread-Safety 개선 계획

## 1. 목적

이 문서는 SPOT의 동시성 모델을 개선하기 위한 실행 계획을 정의한다.

- `spot_pub`: 현재 mutex 직렬화 기반 thread-safe를 유지하면서 락 경합을 줄인다.
- `spot_sub`: 현재 문서상 non-thread-safe인 API를 안전하게 확장할 수 있는 경로를 정의한다.
- 목표는 "완전 lock-free"가 아니라, API 의미를 보존하면서 안전성과 성능을 동시에 개선하는 것이다.

## 2. 현재 상태 요약

### 2.1 spot_pub

- 발행 경로는 호출자 스레드에서 직접 PUB 소켓으로 송신한다.
- 멀티스레드 동시 발행은 `_pub_sync` mutex로 직렬화한다.
- 관련 파일:
`core/src/services/spot/spot_node.cpp`,
`core/src/services/spot/spot_node.hpp`,
`core/include/zlink.h`,
`doc/api/spot.ko.md`

### 2.2 spot_sub

- 메시지는 `spot_sub_t::_queue` (`std::deque`)에 저장되며 `recv()`가 소비한다.
- 큐 및 핸들러 상태, 구독 인덱스가 `spot_node_t::_sync`에 강하게 결합되어 있다.
- 문서상 `spot_sub_*` 대부분 API는 non-thread-safe 계약이다.
- 관련 파일:
`core/src/services/spot/spot_sub.cpp`,
`core/src/services/spot/spot_sub.hpp`,
`core/src/services/spot/spot_node.cpp`,
`doc/api/spot.ko.md`

## 3. 문제 정의

### 3.1 pub 경합

- 현재 발행은 소켓 단일 writer 제약 때문에 mutex 직렬화가 필요하다.
- 높은 publish 동시성에서 `_pub_sync`가 hotspot이 될 수 있다.

### 3.2 sub 락 결합

- `spot_sub` 큐 접근과 구독/핸들러 제어가 노드 전역 락에 결합되어 있다.
- 결과적으로 `publish`, `process_sub`, `subscribe/unsubscribe`, `set_handler`, `recv`가 서로 영향 범위를 넓게 공유한다.

### 3.3 API 계약의 모호성

- `spot_pub_publish`만 thread-safe로 명시되고, `spot_sub`는 대부분 non-thread-safe로 문서화되어 있다.
- 실제로 일부 경로는 내부 락으로 보호되지만, 외부 계약이 약해 사용자가 안전하게 조합하기 어렵다.

## 4. 목표와 비목표

### 4.1 목표

- `spot_pub_publish`의 고동시성 경합 완화.
- `spot_sub` 제어 경로와 데이터 경로 락 분리.
- `spot_sub` API의 thread-safety 계약을 단계적으로 확대.
- destroy/handler-clear/recv 경쟁 조건을 명확한 상태 머신으로 고정.
- 기존 애플리케이션 동작을 깨지 않는 점진적 전환.

### 4.2 비목표

- 초기 단계에서 기존 `zlink_spot_pub_publish`를 완전 비동기 의미로 바꾸지 않는다.
- raw socket 노출 정책은 유지한다.
- `zlink_spot_sub_recv`를 다중 소비자(MPMC) API로 만들지 않는다.

## 5. 대안 비교

| 대안 | 요약 | 장점 | 단점 |
|---|---|---|---|
| A. 현행 유지 + 미세 최적화 | `_pub_sync`, `_sync` 구조 유지 | 리스크 낮음 | 구조적 병목 해소 한계 |
| B. 권장안: 제어/데이터 경로 분리 + publish 비동기 옵션 | lock 범위 축소, queue 기반 단계적 전환 | 성능/안전/호환성 균형 | 구현 복잡도 중간 |
| C. 전면 lock-free 재설계 | 모든 hot path lock-free 지향 | 최고 성능 잠재력 | C++03/플랫폼 호환성 및 검증 비용 큼 |

권장안은 **B**로 한다.

## 6. 권장 아키텍처

### 6.1 핵심 원칙

- 원칙 1: 소켓 접근 단일 소유자(Single Owner) 유지.
- 원칙 2: 데이터 평면(메시지 전달)과 제어 평면(구독/핸들러 변경) 분리.
- 원칙 3: API 계약을 먼저 고정하고 구현을 단계적으로 교체.
- 원칙 4: 기존 동기 publish 의미는 기본값으로 보존.

### 6.2 동시성 모델(목표 상태)

- `spot_node` worker가 SUB 수신과 handler callback 실행을 담당.
- `spot_sub`별 큐는 노드 전역 락과 분리된 전용 동기화 단위로 운영.
- publish는 두 모드를 제공:
`sync`(기본, 기존 의미 유지),
`async`(옵션, queue enqueue 후 반환).
- `spot_sub_recv`는 단일 소비자 제약을 명시하고, 동시 recv 시 `EBUSY`를 반환.

### 6.3 API/옵션 제안

- 노드 옵션 추가(초안):
`ZLINK_SPOT_NODE_OPT_PUB_MODE = {SYNC, ASYNC}`
- 큐 수위 옵션 추가(초안):
`ZLINK_SPOT_NODE_OPT_PUB_QUEUE_HWM`
- 비동기 모드 운영 상태 조회(초안):
`zlink_spot_node_get_stats(...)` (enqueue/drop/backlog)

기존 API 시그니처는 유지하고, 새 옵션은 `zlink_spot_node_setsockopt` 확장으로 도입한다.

## 7. 단계별 구현 계획

### Phase 0: 준비 및 기준선 고정

- SPOT 스레드 경쟁 시나리오를 재현하는 스트레스 테스트 추가.
- 기준 성능 측정:
throughput, p99 latency, lock hold time, queue depth.
- 기준 데이터 저장 위치:
`doc/plan/baseline/` 하위 확장.

완료 기준:
- 현재 구조에서 회귀 검출 가능한 테스트/벤치 세트 확보.

### Phase 1: spot_sub 락 분리

작업:
- `spot_sub_t`에 큐 전용 동기화(예: `_queue_sync`, `_queue_cv`) 도입.
- `_node->_sync`는 구독 인덱스와 핸들러 상태 전이 관리에 집중.
- `recv` 동시 호출 방지 플래그 도입(원자 상태 또는 경량 mutex 가드).

영향 파일:
`core/src/services/spot/spot_sub.hpp`,
`core/src/services/spot/spot_sub.cpp`,
`core/src/services/spot/spot_node.cpp`

완료 기준:
- publish/subscription 변경 중 recv 안정성 확보.
- handler 활성/해제 시 race 재현 케이스 무오류.

### Phase 2: 제어 평면 직렬화 정교화

작업:
- subscribe/unsubscribe/set_handler 요청을 내부 제어 큐로 직렬화하는 경로 추가.
- 요청 적용 시점(linearization point)을 명시.
- destroy 시 in-flight 제어 요청과 callback drain 순서 보장.

영향 파일:
`core/src/services/spot/spot_node.hpp`,
`core/src/services/spot/spot_node.cpp`,
`core/src/services/spot/spot_sub.cpp`

완료 기준:
- subscribe/unsubscribe와 publish 동시 실행 시 일관된 가시성 보장.
- destroy와 set_handler(NULL) 경합 시 deadlock/EBUSY 오동작 제거.

### Phase 3: publish async 옵션(Opt-in)

작업:
- pub async 큐(MPSC) 추가.
- worker thread가 큐를 drain하여 실제 PUB 송신 수행.
- `SYNC` 모드 기본 유지, `ASYNC` 모드 opt-in.
- HWM 초과 정책 정의:
기본 `EAGAIN` 반환, 옵션으로 drop 허용 가능.

구현 참고:
- 기존 `mailbox/ypipe` 재사용 가능성 검토.
- 단, `ypipe`는 SPSC 전제이므로 MPSC에서 별도 직렬화 또는 래퍼 필요.

영향 파일:
`core/src/services/spot/spot_node.hpp`,
`core/src/services/spot/spot_node.cpp`,
`core/include/zlink.h`

완료 기준:
- async 모드에서 frame interleave 0건.
- 동시 publish 성능 개선 확인(Phase 0 대비 목표치 달성).

### Phase 4: 문서/바인딩/기본값 검토

작업:
- C API 문서와 가이드에 thread-safety 계약 반영.
- bindings(Java/Python/Node/C++)의 계약 설명 동기화.
- 충분한 soak 기간 후 async 기본값 전환 여부 결정.

영향 파일:
`doc/api/spot.md`,
`doc/api/spot.ko.md`,
`doc/guide/07-3-spot.md`,
`doc/guide/07-3-spot.ko.md`,
`doc/internals/services-internals.md`,
`doc/internals/services-internals.ko.md`

완료 기준:
- 코드/문서 계약 불일치 0건.
- 바인딩 예제/테스트 전부 통과.

## 8. 테스트 전략

### 8.1 기능 테스트

- publish/recv 기본 시나리오 회귀.
- handler 기반 수신과 recv 기반 수신 상호배타 검증.
- subscribe/pattern/unsubscribe 일관성 검증.
- 후보 테스트 파일:
`core/tests/spot/test_spot_pubsub_basic.cpp`,
`core/tests/spot/test_spot_pubsub_scenario.cpp`,
`core/tests/spot/test_spot_pubsub_threadsafe.cpp`(신규)

### 8.2 동시성 테스트

- 다중 producer가 동일 pub handle 공유.
- publish + subscribe/unsubscribe 동시 실행.
- set_handler(clear) + publish 폭주.
- destroy + in-flight recv/callback 경합.
- TSAN 실행 경로(권장):
`cmake -B build-tsan -DZLINK_BUILD_TESTS=ON -DCMAKE_CXX_FLAGS=\"-fsanitize=thread\"`
후 `ctest --output-on-failure`

### 8.3 성능 테스트

- TPS, p50/p99 latency, CPU 사용률, queue drop rate.
- lock contention 프로파일(가능 시 TSAN/helgrind/플랫폼 도구 병행).
- baseline 비교 산출물:
`doc/plan/baseline/spot_threadsafe_before.txt`,
`doc/plan/baseline/spot_threadsafe_after_phase3.txt`

## 9. 리스크와 완화

| 리스크 | 설명 | 완화 |
|---|---|---|
| 의미 변화 | async 모드에서 publish 성공 의미가 enqueue 성공으로 바뀜 | 기본값 SYNC 유지, 명확한 문서화 |
| 메모리 증가 | 큐 backlog 누적 | HWM/드롭 정책/통계 노출 |
| 순서 보장 혼선 | sync/async 혼합 시 순서 기대 차이 | 모드별 ordering 계약 명문화 |
| 파괴 시 경쟁 | destroy 중 in-flight 작업 존재 | state machine + drain barrier |
| 플랫폼 호환성 | lock-free 구현의 플랫폼 편차 | portable fallback(뮤텍스 기반) 유지 |

## 10. 수용 기준 (Definition of Done)

- 모든 기존 SPOT 테스트 통과.
- 신규 동시성 스트레스 테스트 안정 통과(반복 실행 기준).
- 문서상 thread-safety 계약과 구현 일치.
- SYNC 모드에서 기존 동작/호환성 유지.
- ASYNC 모드에서 목표 성능 개선 달성.

## 11. 실행 우선순위

1. Phase 0 (기준선/재현성 확보)
2. Phase 1 (spot_sub 락 분리)
3. Phase 2 (제어 평면 직렬화)
4. Phase 3 (pub async opt-in)
5. Phase 4 (문서/바인딩 반영)

## 12. 정량 목표

| 항목 | 목표 |
|---|---|
| 안정성 | 신규 동시성 테스트 1,000회 반복 시 실패 0건 |
| 성능(throughput) | 8 producer 기준 SYNC 모드 동등 이상, ASYNC 모드 +20% 이상 |
| 지연시간 | ASYNC 모드 p99 latency 20% 이상 개선 또는 동등 성능에서 CPU 절감 |
| 호환성 | 기존 SPOT API/테스트 회귀 0건 |

## 13. 롤아웃 전략

- 1단계: Phase 1/2를 기본 ON으로 배포(동작 의미 변화 없음).
- 2단계: ASYNC 모드는 실험 옵션으로 배포(기본 OFF).
- 3단계: 운영 baseline 누적 후 기본값 전환 여부 결정.
- 4단계: 기본값 전환 시 최소 1개 minor release 동안 롤백 옵션 유지.

## 14. 오픈 이슈

- `spot_sub_recv` 동시 호출 정책을 `EBUSY`로 고정할지, 내부 직렬화로 허용할지 결정 필요.
- async 모드의 drop 정책 기본값(`EAGAIN` vs best-effort drop) 결정 필요.
- ASYNC 기본 전환 시점은 soak 결과 기반으로 결정.

## 부록. 구현 진행 현황 (2026-02-12)

- Phase 1 구현 완료:
  `spot_sub` 큐 전용 동기화 분리, 동시 `recv` 시 `EBUSY`, 회귀 테스트 추가.
- Phase 2 부분 반영:
  detached sub 접근 검증 강화, destroy/remove 경합 직렬화 강화.
- Phase 3 1차 구현 완료:
  `zlink_spot_node_setsockopt` 확장으로 async publish opt-in 제공.
  - `ZLINK_SPOT_NODE_OPT_PUB_MODE` (`SYNC` 기본, `ASYNC` 선택)
  - `ZLINK_SPOT_NODE_OPT_PUB_QUEUE_HWM`
  - `ZLINK_SPOT_NODE_OPT_PUB_QUEUE_FULL_POLICY` (`EAGAIN` 기본, drop 선택)
- API 문서 및 시나리오 테스트(async mode/local delivery, 옵션 검증) 반영.
