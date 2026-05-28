# Python Bindings Core Alignment 실행 가이드

> 상태: 완료
> 기준 문서: `bindings/python/plan/bindings/python-bindings-core-api-alignment-plan.ko.md`
> 대상 범위: `bindings/python/`, `doc/bindings/`, `bindings/python/plan/bindings/`
> 목적: Python bindings를 최신 `core` public surface와 Python 스타일 API 철학에 맞춰 끝까지 정렬하는 실행 순서와 완료 판정 기준 고정
> 최종 종료 판정: `미적용 사항이 없습니다.`

## 1. 문서 목적

이 문서는 메인 플랜 문서의 내용을 실제 코드 변경 순서와 완료 판정 기준으로
고정하는 실행 문서다.

이 문서는 새 설계를 제안하지 않는다.
설계 authority는 아래 메인 플랜 문서 하나로 고정한다.

- [`python-bindings-core-api-alignment-plan.ko.md`](./python-bindings-core-api-alignment-plan.ko.md)
  - 목적 / 상태 / 설계 원칙:
    [`1. 목표`](./python-bindings-core-api-alignment-plan.ko.md#1-목표),
    [`2. 현재 상태 요약`](./python-bindings-core-api-alignment-plan.ko.md#2-현재-상태-요약),
    [`3. 설계 원칙`](./python-bindings-core-api-alignment-plan.ko.md#3-설계-원칙)
  - 고정 결정 / public API:
    [`3.1 범위 고정 결정`](./python-bindings-core-api-alignment-plan.ko.md#31-범위-고정-결정),
    [`4. 공개 API 재정렬 방향`](./python-bindings-core-api-alignment-plan.ko.md#4-공개-api-재정렬-방향),
    [`4.5 Python 스타일 API 결정`](./python-bindings-core-api-alignment-plan.ko.md#45-python-스타일-api-결정),
    [`4.5.1 canonical API 초안`](./python-bindings-core-api-alignment-plan.ko.md#451-canonical-api-초안),
    [`4.5.2 성능 계약`](./python-bindings-core-api-alignment-plan.ko.md#452-성능-계약)
  - 단계별 구현 / 검증:
    [`5. 단계별 실행 계획`](./python-bindings-core-api-alignment-plan.ko.md#5-단계별-실행-계획),
    [`6. 파일 단위 작업 범위`](./python-bindings-core-api-alignment-plan.ko.md#6-파일-단위-작업-범위),
    [`9. 최종 완료 기준`](./python-bindings-core-api-alignment-plan.ko.md#9-최종-완료-기준)

실행 중 설계 판단이 필요해 보이면 먼저 메인 플랜을 갱신하고, 그 다음 이 guide를
맞춘 뒤 코드를 수정한다. 코드와 실행 가이드만 바꿔서 설계 불일치를 남기지 않는다.

## 2. 실행 authority

단일 설계 authority:

- [`python-bindings-core-api-alignment-plan.ko.md`](./python-bindings-core-api-alignment-plan.ko.md)

이 가이드는 아래 내용을 메인 플랜에서 그대로 따른다.

- `Socket`은 `send`, `recv_message`, `recv_multipart`, `recv_into`를 canonical
  raw API로 가진다
- payload 입력은 Python buffer protocol 중심으로 받는다
- copy path와 borrow path는 이름으로 구분한다
- `bytes`, writable buffer, `memoryview` fast path는 유지한다
- `Received*` aggregate가 raw recv 결과와 lifecycle aggregate를 동시에 담당한다
- 검증 자산은 `examples`, `contract tests` 두 층으로 나눈다

자동 실행 관계:

- 수동 실행 기준 문서는 이 guide와 메인 플랜이다.
- 자동 실행이 필요하면
  [`run_python_bindings_alignment_execution.sh`](./run_python_bindings_alignment_execution.sh)
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

- 메인 플랜만으로는 해결할 수 없는 Python public API 계약 충돌
- 사용자 작업과 직접 충돌하는 워크트리 변경 발견
- `bindings/python/`, `doc/bindings/`, `bindings/python/plan/bindings/`만으로
  해결할 수 없는 blocker

위 경우가 아니면:

1. 첫 미완료 slice를 잡는다.
2. 코드 수정과 example/contract 정리를 같이 한다.
3. 관련 검증을 끝낸다.
4. 이 guide 상태를 갱신한다.
5. 다음 미완료 slice로 바로 넘어간다.

이 가이드는 commit / push를 기본 규칙으로 강제하지 않는다.
commit / push는 사용자 지시가 있을 때만 수행한다.

## 4. 기본 실행 명령

현재 즉시 가능한 smoke:

```bash
./bindings/python/plan/bindings/run_python_bindings_alignment_execution.sh --max-iterations 0
```

위 smoke는 실행 wrapper 경로와 supervisor 연결만 확인한다.

현재 baseline 진단 명령:

```bash
cd bindings/python && python -m pytest -q tests/test_version.py tests/test_enums.py
cd bindings/python && python -m pytest -q
```

주의:

- 현재 baseline에서는 일부 테스트가 기존 Python 바인딩의 구 심볼 의존 때문에
  실패할 수 있다.
- 2026-03-26 직접 확인한 baseline 주요 실패 원인은
  [`src/zlink/_ffi.py`](/home/hep7/project/kairos/zlink/bindings/python/src/zlink/_ffi.py)
  의 eager symbol binding 이 `zlink_stream_attach_len32be`를 import 시점에 요구해,
  `core/build/lib/libzlink.so` 로딩 단계에서 즉시 `AttributeError`가 나는 점이다.
- baseline은 현 상태 진단 명령이지 완료 판정 명령이 아니다.

최종 상태 검증 흐름:

```bash
cd bindings/python && python -m pytest -q
cd bindings/python && python -m pytest -q tests/integration
```

실행 중 gate가 오래 걸리면 아래 명령으로 같은 셸에서 추적한다.

```bash
./core/tools/run_execution_gate_loop.sh --label python_bindings_alignment_gate --count 1
```

스크립트 smoke 확인:

```bash
./bindings/python/plan/bindings/run_python_bindings_alignment_execution.sh --max-iterations 0
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

### 5.1 Slice 1. FFI native contract / struct layout 재정렬

메인 플랜 참조:

- `Phase 0`
- `Phase 1`

상태: `완료`

대상:

- `src/zlink/_ffi.py`
- `src/zlink/_enums.py`

작업:

- 공식 헤더 밖 심볼 lookup 제거
- 최신 core 함수/enum/struct 기준 `ctypes` 시그니처 재정의
- socket monitor / service monitor / registry / discovery / spot downcall 정렬

완료 기준:

- 공식 헤더 비기재 심볼 direct lookup이 남지 않는다
- native symbol smoke test 통과

### 5.2 Slice 2. `Message` / `Socket` / `Received*` canonical API

메인 플랜 참조:

- `Phase 2`
- `4.5`

상태: `완료`

대상:

- `src/zlink/_core.py`
- `src/zlink/__init__.py`

작업:

- `Socket` raw 계층을 `send`, `recv_message`, `recv_multipart`, `recv_into` 중심으로
  재구성
- `Message.from_` / `Message.wrap_buffer` 표면 고정
- context manager 추가
- `Received*` aggregate lifecycle 모델 구현

완료 기준:

- `recv(size)` guess path가 canonical surface로 남지 않는다
- copy path와 borrow path contract test 통과
- buffer protocol fast path 회귀가 없다

진행 메모:

- 2026-03-26: `Socket.send`, `send_multipart`, `recv_message`,
  `recv_multipart`, `recv_into`, `publish`, `recv_topic_message`,
  `subscription_event`, `Message.from_`, `Message.wrap_buffer`,
  `ReceivedMessage`, `ReceivedMultipart`, `ReceivedTopicMessage` 1차 반영
- 2026-03-26: `Received*`가 native recv 결과를 직접 소유하도록 lifecycle 모델
  재구성하고 `recv_into()`를 caller-owned writable buffer direct fill 경로로 정리
- `tests/test_core_api_alignment.py`로 copy/borrow contract와 canonical recv/poller/
  monitor smoke를 추가
- legacy `setsockopt()` / `getsockopt()` 경로 정리는 메인 플랜 Phase 3에 맞춰
  Slice 3에서 처리한다

### 5.3 Slice 3. monitor / poller / option 계층

메인 플랜 참조:

- `Phase 3`

상태: `완료`

대상:

- `src/zlink/_monitor.py`
- `src/zlink/_poller.py`
- `src/zlink/_core.py`

작업:

- old `setsockopt` / `getsockopt` 경로 정리
- 전용 option family helper 추가
- socket/service monitor wrapper 정리
- poller와 callback/recv API의 배타성 규칙 문서화
- poller와 callback mode가 동시에 같은 socket/spot sub handle을 구동하지 않도록
  public contract와 테스트를 맞춘다

완료 기준:

- old option API가 canonical surface에서 제거된다
- poller와 callback/recv 배타성 contract test가 통과한다
- monitor snapshot/service monitor contract test 통과

진행 메모:

- 2026-03-26: `Socket.set_router_option`, `set_pub_option`, `set_sub_option`,
  `set_stream_option` 및 대응 getter 반영
- `src/zlink/_monitor.py`를 `zlink_socket_monitor_open`,
  `zlink_socket_monitor_recv`, `zlink_monitor_status`,
  `zlink_service_monitor_recv` 기준으로 유지
- `src/zlink/_poller.py`를 `zlink_poller_*` 기준으로 유지하고
  poller + callback 배타성 contract test 추가
- 2026-03-26: `Socket.setsockopt()` / `getsockopt()` 제거, 관련 integration/test
  호출부를 canonical option API로 이동

### 5.4 Slice 4. discovery / registry / spot / topology 계층

메인 플랜 참조:

- `Phase 4`

상태: `완료`

대상:

- `src/zlink/_discovery.py`
- `src/zlink/_spot.py`

작업:

- `Receiver` 제거
- `Discovery(ctx, service_type, service_name)` 도입
- `Registry.bind(pub, router)` 도입
- unified `Spot` / `SpotNode` 재구성
- split `spot_pub` / `spot_sub` public surface 제거
- `ReceivedTopicMessage` 기반 recv/callback contract 정리

완료 기준:

- split service abstraction이 public surface에 남지 않는다
- `Receiver`와 split `Spot`이 `__init__.py` public surface에서 제거된다
- discovery/registry/spot contract test 통과

진행 메모:

- 2026-03-26: `Registry.bind(pub, router)`, `Discovery(ctx, service_type,
  service_name)`, `SpotNode.connect_peer()/disconnect_peer()/attach_discovery()`
  반영
- 2026-03-26: `Spot`을 unified publish/subscribe facade로 정리하고
  `ReceivedTopicMessage` 기반 recv/callback contract 반영
- 2026-03-26: `Receiver`를 public export에서 제거하고 `_native.py`를
  `_ffi.py` 기반 compatibility shim으로 축소
- 2026-03-26: `_enums.py`에 남아 있던 unused `ReceiverSocketRole` 잔재 제거로
  split service abstraction 내부 흔적까지 정리

### 5.5 Slice 5. examples / tests / migration docs

메인 플랜 참조:

- `Phase 5`

상태: `완료`

대상:

- `tests/**`
- `examples/**`
- `doc/bindings/**`

작업:

- `examples/` 추가
- canonical 예제 인벤토리를 메인 플랜 기준으로 채운다
  (`pair_recv`, `pair_callback`, `pubsub_recv`, `pubsub_callback`,
  `dealer_router_recv`, `dealer_router_callback`, `stream_recv`,
  `stream_callback`, `spot_recv`, `spot_callback`)
- contract test만 유지
- sleep/retry 제거
- `Socket` direct callback surface가 예제/contract test를 설명할 만큼 실제 코드에
  연결돼 있어야 한다
- migration note와 Python API 사용 예제 정리

완료 기준:

- `python -m pytest -q` 통과
- `tests/integration` 통과
- fast-path regression test 통과
- examples가 새 API와 recv/callback 양쪽 모델을 설명한다

진행 메모:

- 2026-03-26: integration helper의 retry/sleep 루프를 제거하고 bounded
  poller wait + canonical recv API로 정리
- 2026-03-26: redundant plain PUB/SUB integration matrix를 제거하고
  contract 중심 시나리오만 유지
- 2026-03-26: `bindings/python/examples/`에 canonical pair/pubsub/
  dealer-router/spot 예제 추가
- 2026-03-26: `doc/bindings/python.md`, `python.ko.md`를 최신 public surface
  기준으로 갱신
- 2026-03-26: `Socket.set_recv_handler()`,
  `set_subscribe_handler()`, `set_send_ready_handler()`,
  `Spot.set_send_ready_handler()`를 Python public surface에 연결
- 2026-03-26: example inventory를 메인 플랜 기준 10개 파일로 채우고
  callback/recv 양쪽 경로 smoke를 확인
- 2026-03-26: `python -m pytest -q bindings/python/tests` 기준 통과,
  `python -m pytest -q bindings/python/tests/integration`
  기준 `4 passed`, example smoke 통과
- 2026-03-26: final recheck로 `python -m pytest -q`,
  `python -m pytest -q tests/integration`,
  `run_python_bindings_alignment_execution.sh --max-iterations 0`,
  examples 10개 smoke를 다시 확인

## 6. 종료 판정 규칙

아래를 모두 만족해야 종료다.

- 체크리스트가 모두 `완료`
- 메인 플랜의 고정 결정과 코드가 일치
- 검증 명령 결과가 기록됨
- 미적용 사항이 없습니다.
