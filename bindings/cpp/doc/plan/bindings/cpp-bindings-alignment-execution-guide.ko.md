# C++ Bindings Core Alignment 실행 가이드

> 상태: 완료
> 기준 문서: `bindings/cpp/plan/bindings/2026-03-26-cpp-core-alignment-plan.ko.md`
> 대상 범위: `bindings/cpp/`, `doc/bindings/`, `bindings/cpp/plan/bindings/`
> 목적: C++ bindings를 최신 `core` public surface에 맞춰 끝까지 정렬하는 실행 순서와 완료 판정 기준 고정
> 최종 종료 판정: `미적용 사항이 없습니다.`

## 1. 문서 목적

이 문서는 메인 플랜 문서의 내용을 실제 코드 변경 순서와 완료 판정 기준으로
고정하는 실행 문서다.

이 문서는 새 설계를 제안하지 않는다.
설계 authority는 아래 메인 플랜 문서 하나로 고정한다.

- [`2026-03-26-cpp-core-alignment-plan.ko.md`](./2026-03-26-cpp-core-alignment-plan.ko.md)
  - 목적 / 상태 / 설계 원칙:
    [`1. 목적`](./2026-03-26-cpp-core-alignment-plan.ko.md#1-목적),
    [`2. 현재 상태 요약`](./2026-03-26-cpp-core-alignment-plan.ko.md#2-현재-상태-요약),
    [`3. 설계 원칙`](./2026-03-26-cpp-core-alignment-plan.ko.md#3-설계-원칙)
  - 고정 결정 / public surface:
    [`3.1 이번 작업에서 고정하는 결정`](./2026-03-26-cpp-core-alignment-plan.ko.md#31-이번-작업에서-고정하는-결정),
    [`5.1 구현 완료 후 기대 public surface`](./2026-03-26-cpp-core-alignment-plan.ko.md#51-구현-완료-후-기대-public-surface)
  - 실행 구조 / old-to-new 대응:
    [`6. 실행 전략`](./2026-03-26-cpp-core-alignment-plan.ko.md#6-실행-전략),
    [`6.6 old-to-new 대응표`](./2026-03-26-cpp-core-alignment-plan.ko.md#66-old-to-new-대응표)
  - 단계별 구현 / 검증:
    [`7. 상세 단계`](./2026-03-26-cpp-core-alignment-plan.ko.md#7-상세-단계),
    [`9. 파일별 실행 체크리스트`](./2026-03-26-cpp-core-alignment-plan.ko.md#9-파일별-실행-체크리스트),
    [`11. 완료 기준`](./2026-03-26-cpp-core-alignment-plan.ko.md#11-완료-기준),
    [`12. 구현 완료 시 검증 절차`](./2026-03-26-cpp-core-alignment-plan.ko.md#12-구현-완료-시-검증-절차),
    [`13. 실행 순서 요약`](./2026-03-26-cpp-core-alignment-plan.ko.md#13-실행-순서-요약)

실행 중 설계 판단이 필요해 보이면 먼저 메인 플랜을 갱신하고, 그 다음 이 guide를
맞춘 뒤 코드를 수정한다. 코드와 실행 가이드만 바꿔서 설계 불일치를 남기지 않는다.

## 2. 실행 authority

단일 설계 authority:

- [`2026-03-26-cpp-core-alignment-plan.ko.md`](./2026-03-26-cpp-core-alignment-plan.ko.md)

이 가이드는 아래 내용을 메인 플랜에서 그대로 따른다.

- `socket_t`는 `message_t` 기반 `send`/`recv` overload만 가진다
- bytes/string 변환은 `message_t`가 담당한다
- callback registration은 native C callback typedef passthrough로 고정한다
- 서비스 계층은 `registry_t`, `registry_query_client_t`, `discovery_t`,
  `spot_node_t`, `spot_t`로 고정한다
- `compat.hpp`는 deprecated thin shim만 남기고 umbrella include에서는 제외한다
- 검증 자산은 `samples`, `tests/contract` 2층으로 나눈다
- `bindings/cpp/perf/**`는 별도 작업으로 두고 이 guide의 완료 기준에서 제외한다

자동 실행 관계:

- 수동 실행 기준 문서는 이 guide와 메인 플랜이다.
- 자동 실행이 필요하면 [`run_cpp_bindings_alignment_execution.sh`](./run_cpp_bindings_alignment_execution.sh)
  를 사용한다.
- 이 스크립트는 내부적으로 공통 supervisor인
  [`core/tools/run_codex_execution_guide_loop.sh`](../../../../core/tools/run_codex_execution_guide_loop.sh)
  를 호출한다.
- 공통 supervisor는 guide / master plan / logs / gate label만 주입받는 제너릭
  루프이고, bindings 전용 정책은 이 guide와 메인 플랜이 결정한다.
- 실행 wrapper 자체는 별도 `lock`을 두지 않는다.
  같은 작업을 병렬 실행해야 하면 `--logs-dir` 또는 `--gate-label`을 분리해서
  상태 파일 충돌을 피한다.

## 3. 중단 금지 규칙

아래 경우가 아니면 멈추지 않는다.

- 메인 플랜만으로는 해결할 수 없는 C++ public surface 계약 충돌
- 사용자 작업과 직접 충돌하는 워크트리 변경 발견
- `bindings/cpp/`, `doc/bindings/`, `bindings/cpp/plan/bindings/`만으로 해결할 수 없는 blocker

위 경우가 아니면:

1. 첫 미완료 slice를 잡는다.
2. 코드 수정과 샘플/contract test 정리를 같이 한다.
3. 관련 검증을 끝낸다.
4. 이 guide 상태를 갱신한다.
5. 다음 미완료 slice로 바로 넘어간다.

이 가이드는 commit / push를 기본 규칙으로 강제하지 않는다.
commit / push는 사용자 지시가 있을 때만 수행한다.

병렬 실행 규칙:

- 기본값으로 두 개 이상 동시에 돌리지 않는다.
- 병렬 실행이 필요하면 실행 단위마다 `--logs-dir`을 분리한다.
- gate status 파일 충돌을 피하려면 `--gate-label`도 함께 분리한다.
- wrapper는 실행 자체를 막지 않는다.

## 4. 기본 실행 명령

메인 플랜이 고정한 기본 검증 흐름은 아래와 같다.

```bash
./bindings/cpp/build.sh ON ON

ctest --test-dir core/build -L contract --output-on-failure
ctest --test-dir core/build -L sample-smoke --output-on-failure -j1
```

전체 샘플 수동 실행 대상:

```bash
./core/build/bindings/cpp/sample_cpp_pair_recv
./core/build/bindings/cpp/sample_cpp_pair_callback
./core/build/bindings/cpp/sample_cpp_pubsub_recv
./core/build/bindings/cpp/sample_cpp_pubsub_callback
./core/build/bindings/cpp/sample_cpp_dealer_router_recv
./core/build/bindings/cpp/sample_cpp_dealer_router_callback
./core/build/bindings/cpp/sample_cpp_stream_recv
./core/build/bindings/cpp/sample_cpp_stream_callback
./core/build/bindings/cpp/sample_cpp_spot_recv
./core/build/bindings/cpp/sample_cpp_spot_callback
```

실행 중 gate가 오래 걸리면 아래 명령으로 같은 셸에서 추적한다.

```bash
./core/tools/run_execution_gate_loop.sh --label cpp_bindings_alignment_gate --count 1
```

스크립트 smoke 확인:

```bash
./bindings/cpp/plan/bindings/run_cpp_bindings_alignment_execution.sh --max-iterations 0
```

위 명령은 공통 supervisor까지 실제로 호출하지만 Codex iteration은 돌리지 않는
최소 점검 경로다. wrapper가 supervisor의 `max-iterations=0` 종료를 smoke 성공으로
해석하므로 종료 코드는 `0`이어야 한다.

## 5. 남은 작업 체크리스트

상태 값은 아래 네 개만 쓴다.

- `미착수`
- `진행중`
- `검증중`
- `완료`

### 5.1 Slice 1. 기반 공통 계층

메인 플랜 참조:

- `7. 상세 단계`의 `Phase 1`
- `9. 파일별 실행 체크리스트`의 `Slice 1`

상태: `완료`

대상:

- `include/zlink/common.hpp`
- `include/zlink/types.hpp`
- `include/zlink/error.hpp`
- `include/zlink/context.hpp`
- `include/zlink/message.hpp`

작업:

- 최신 enum/type/context option 정렬
- 삭제된 message helper 제거
- `message_t` bytes/string 변환 API 추가
- routing-id / fixed-string helper 정리

완료 기준:

- 공통 헤더 compile smoke 통과

진행 메모:

- `core/include/zlink.h` 최신 enum/type 기준으로 `types.hpp`, `context.hpp`,
  `message.hpp`를 우선 정렬했다.
- 삭제된 `zlink_msg_more` / `zlink_msg_get` / `zlink_msg_set` 기반 helper를
  제거하고 `message_t`의 copy/refcnt/bytes-string helper를 추가했다.
- `types.hpp`에서 `routing_id`/`subscribe`/`xpub_*`/`stream_notify` 같은
  구형 socket-option alias를 걷어내고 dedicated method / domain option 경로만
  남기도록 정리했다.
- 공통 헤더 syntax smoke는 통과했고, `message_t` contract test 재편은
  `Slice 5`의 contract test 최소 세트 작업에서 이어서 닫는다.

### 5.2 Slice 2. raw socket / monitor / poller

메인 플랜 참조:

- `7. 상세 단계`의 `Phase 2`, `Phase 3`, `Phase 4`
- `9. 파일별 실행 체크리스트`의 `Slice 2`

상태: `완료`

대상:

- `include/zlink/socket.hpp`
- `include/zlink/monitor.hpp`
- `include/zlink/poller.hpp`
- `include/zlink/runtime.hpp`

작업:

- `socket_t`를 `send`/`recv` overload 중심으로 재구성
- callback registration을 native callback passthrough로 정렬
- socket monitor RAII wrapper와 generic poller 정렬
- generic poller만 남기도록 정리

완료 기준:

- raw socket 샘플 8종 전체 빌드
- monitor/option/callback-mode contract test 통과

진행 메모:

- `monitor.hpp`를 최신 `zlink_socket_monitor_open/handler/recv/snapshot/close`
  모델로 전환했다.
- `socket.hpp`를 최신 multipart `send`/`recv`, topic `publish`/`subscribe`,
  native callback passthrough, option-domain API, `attach_discovery()` 중심
  표면으로 재구성했다.
- `poller.hpp`는 `spot`/`receiver` 전용 얕은 helper를 제거하고 generic
  socket/fd poller만 남기도록 정리했다.
- `zlink/socket.hpp`, `zlink/poller.hpp` 단독 syntax smoke는 통과했다.
- 이후 `Slice 3` 서비스 헤더와 `Slice 5` contract 테스트/CMake 재편을 진행해
  최신 surface 기준 빌드는 다시 이어졌다.
- `tests/contract`의 raw socket / callback / option / monitor 런타임은 모두
  녹색으로 복구됐다.
- `samples/` 8종 raw socket 타깃도 추가해 빌드되지만, 전체 `sample-smoke`
  종료 판정은 `Slice 5`의 통합 재실행이 끝나야 닫는다.

### 5.3 Slice 3. 서비스 계층

메인 플랜 참조:

- `7. 상세 단계`의 `Phase 5`
- `9. 파일별 실행 체크리스트`의 `Slice 3`

상태: `완료`

대상:

- `include/zlink/services/registry.hpp`
- `include/zlink/services/query.hpp`
- `include/zlink/services/discovery.hpp`
- `include/zlink/services/spot.hpp`
- `include/zlink/service_monitor.hpp`

작업:

- `registry_t`를 bind/snapshot/query 모델로 전환
- `registry_query_client_t` 추가
- `discovery_t`를 fixed service-view 모델로 전환
- `spot_node_t`, `spot_t`를 unified 최신 모델로 재구성

완료 기준:

- `spot_recv`, `spot_callback` 샘플 빌드 및 `sample-smoke` 포함
- service contract test 통과

진행 메모:

- `services/registry.hpp`, `services/discovery.hpp`, `services/spot.hpp`를 최신
  `bind/snapshot/query/fixed-service-view/unified-spot` 계약으로 재작성했다.
- 신규 `service_monitor.hpp`, `services/query.hpp`를 추가하고
  `include/zlink.hpp` umbrella에 연결했다.
- `service` contract runtime은 통과한다.
- `spot_recv`, `spot_callback` 샘플 소스와 CMake 타깃은 추가됐다.
- unified `spot_t` self-delivery는 sub monitor의 `spot_filter_applied`와
  pub monitor snapshot의 `ZLINK_MONITOR_STATE_SEND_READY`를 함께 확인한 뒤
  `recv`/`callback` 경로를 진행하도록 정리했다.
- `spot_recv`, `spot_callback`, service/callback contract는 모두 같은 준비
  조건으로 수렴했고 반복 실행까지 통과했다.

### 5.4 Slice 4. compat / umbrella / 문서 표면

메인 플랜 참조:

- `7. 상세 단계`의 `Phase 6`, `Phase 8`
- `9. 파일별 실행 체크리스트`의 `Slice 4`

상태: `완료`

대상:

- `include/zlink/compat.hpp`
- `include/zlink.hpp`
- `API_DRAFT.md`
- `doc/bindings/cpp.ko.md`
- `doc/bindings/cpp.md`
- `README.doxygen.md`
- `Doxyfile`

작업:

- `compat.hpp` 축소
- umbrella header include 정리
- 문서/예제를 새 surface 기준으로 정리

완료 기준:

- umbrella header 단독 compile smoke 통과
- 문서 예제와 샘플 이름 일치

진행 메모:

- `compat.hpp`를 최신 public C API 위의 deprecated thin shim만 남기도록 축소했다.
- `include/zlink.hpp`는 계속 `compat.hpp`를 제외한 최신 public surface만 노출한다.
- `doc/bindings/cpp.ko.md`, `doc/bindings/cpp.md`, `API_DRAFT.md`,
  `TESTING.md`, `README.doxygen.md`, `Doxyfile`를 최신
  `message_t`/multipart/registry-query/unified-spot 모델과 `core/build/`
  검증 경로 기준으로 정리했다.
- `zlink.hpp`와 `zlink/compat.hpp` 단독 syntax smoke는 통과했다.
- 문서 예제/샘플 이름도 `pair/pubsub/dealer_router/stream/spot`의
  `recv/callback` 10종과 일치하도록 맞췄다.

### 5.5 Slice 5. 샘플 / contract test

메인 플랜 참조:

- `7. 상세 단계`의 `Phase 7`
- `9. 파일별 실행 체크리스트`의 `Slice 5`
- `12. 구현 완료 시 검증 절차`

상태: `완료`

대상:

- `bindings/cpp/samples/**`
- `bindings/cpp/tests/**`
- `bindings/cpp/CMakeLists.txt`
- `bindings/cpp/build.sh`
- `bindings/cpp/TESTING.md`

작업:

- `samples/` 디렉토리 신설
- `tests/contract/` 최소 세트로 재편
- `sample-smoke`, `contract` 라벨 정리
- `build.sh` 인터페이스를 메인 플랜과 일치하게 갱신

완료 기준:

- `./bindings/cpp/build.sh ON ON` 성공
- `ctest -L contract` 통과
- `ctest -L sample-smoke` 통과
  - 대상은 `pair`, `pubsub`, `dealer_router`, `stream`, `spot`의 `recv/callback` 전부

별도 작업 메모:

- 기존 `bindings/cpp/perf/**`는 유지한다.
- perf 복구/확장은 팀장님 별도 트랙으로 진행한다.
- 이 guide는 perf 변경 없이도 종료 가능해야 한다.

진행 메모:

- `CMakeLists.txt`를 구형 포팅 테스트 열거 방식에서
  `tests/contract/test_cpp_contract_*.cpp` 최소 세트 등록 방식으로 교체했다.
- `tests/contract/`에 `message/socket/callback_mode/options/monitor/service`
  최소 contract test executable을 추가했다.
- `samples/common/`과 패턴별 `recv`/`callback` 10개 샘플을 추가하고
  `sample-smoke` CTest 등록까지 연결했다.
- `build.sh`를 `./bindings/cpp/build.sh [RUN_TESTS] [RUN_SAMPLES]` 인터페이스로
  유지하되 실제 빌드 디렉토리는 저장소 규칙에 맞춰 `core/build/`로 전환했다.
- `spot_t::publish(message_t&)`의 single-part 경로는
  direct native publish 경로로 단순화했다.
- `core/tests/e2e/spot/test_spot_service_introspection.cpp`에
  `test_spot_unified_spot_callback_self_delivery` 회귀를 추가했고, 이 C API
  회귀는 20회 반복 실행을 통과했다.
- unified `spot_t` self-delivery는 sub monitor와 pub monitor를 분리해
  `spot_filter_applied`와 `ZLINK_MONITOR_STATE_SEND_READY` 준비 조건을 함께
  확인한 뒤 `recv`/`callback` 경로를 진행하도록 정리했다.
- `test_cpp_contract_service`에 unified `spot_t` self-delivery recv 계약을
  추가해 샘플 smoke만이 아니라 contract에서도 같은 준비 조건을 검증한다.
- `sample_cpp_spot_recv`, `sample_cpp_spot_callback`,
  `test_cpp_contract_callback_mode`는 각 20회 반복 실행을 통과했다.
- 최종 검증으로 `./bindings/cpp/build.sh ON ON`,
  `ctest --test-dir core/build -L contract --output-on-failure`,
  `ctest --test-dir core/build -L sample-smoke --output-on-failure -j1`,
  전체 샘플 10종 수동 실행이 모두 통과했다.
- 현재 워크트리 기준으로도 같은 검증 순서를 `core/build/`에서 다시 실행해
  `contract` 6건, `sample-smoke` 10건, 수동 샘플 10종이 모두 통과함을
  재확인했다.

## 6. 종료 판정

아래가 모두 만족되면 종료한다.

- guide의 `5.1`~`5.5`가 전부 `완료`
- 메인 플랜의 `11. 완료 기준`을 모두 만족
- 메인 플랜의 `12. 구현 완료 시 검증 절차`가 모두 실행됨
- 결과 요약에서 더 이상 남은 미적용 항목이 없음

최종 종료 문구:

```text
미적용 사항이 없습니다.
```
