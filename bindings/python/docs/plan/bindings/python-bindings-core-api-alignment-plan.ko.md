# Python Bindings Core 최신 API 정렬 계획

작성일: 2026-03-26
대상: `bindings/python`
기준 소스:
- `core/include/zlink.h`
- `doc/guide/*.md`
- `bindings/python/src/zlink/**`
- `bindings/python/tests/**`

## 1. 목표

`python` 바인딩을 최신 `core`의 공식 공개 표면에 다시 맞춘다. 기준은 현재
네이티브 라이브러리에 우연히 남아 있는 호환 심볼이 아니라,
`core/include/zlink.h`와 `doc/guide`에 문서화된 API다.

핵심 목표는 다음과 같다.

- Python FFI 레이어가 공식 헤더에 없는 구 심볼 의존을 제거한다.
- Python 공개 API를 최신 `core`의 소유권/라이프사이클/콜백 모델에 맞게 재설계한다.
- 서비스 계층을 최신 `Discovery` / `Socket Family` / `Spot` / `Registry` 모델로 정렬한다.
- 현재 존재하는 `bytes` / buffer protocol / `memoryview` fast path는 버리지 않고
  새 contract 아래로 재배치한다.
- Python 사용자-facing 검증 자산을 `examples/` + binding contract test 조합으로
  재편한다.
- 저장소 fail-fast 정책에 맞지 않는 retry/sleep 기반 테스트와 helper를 제거한다.

## 2. 현재 상태 요약

### 2.0 진행 메모

- 2026-03-26: Phase 1 범위의 `_ffi.py` / `_enums.py` 1차 정렬 완료.
  - 공식 헤더 밖 eager symbol lookup 제거
  - `zlink_set_option`, `zlink_get_option`, socket/service monitor,
    registry/discovery/spot canonical downcall binding 추가
  - `tests/test_native_contract.py` native symbol smoke 추가
  - `python -m pytest -q`: 통과
  - `python -m pytest -q tests/integration`: 통과
- 2026-03-26: Phase 2 범위의 `_core.py` / `tests/test_core_api_alignment.py`
  1차 정렬 완료.
  - `Received*` aggregate가 native recv 결과를 직접 소유하도록 lifecycle 모델 재구성
  - `recv_into()` direct fill, `Message.from_`, `Message.wrap_buffer`
    contract test 추가
  - `python -m pytest -q`: 통과
- 2026-03-26: Phase 3 정렬 완료.
  - `Socket.setsockopt()` / `getsockopt()` 제거
  - integration/test 호출부를 canonical option API와 `recv_message()` /
    `recv_multipart()` 경로로 이동
  - poller + callback 배타성 contract test 추가
- 2026-03-26: Phase 4 범위의 `_discovery.py` / `_spot.py` / `__init__.py`
  정렬 완료.
  - `Registry.bind(pub, router)`, `Discovery(ctx, service_type, service_name)`
    반영
  - unified `Spot` / `SpotNode` facade 정리
  - `Receiver` public export 제거, `_native.py`는 `_ffi.py` 기반 shim으로 축소
  - `_enums.py`의 unused `ReceiverSocketRole` 잔재 제거
- 2026-03-26: Phase 5 범위 정렬 완료.
  - integration helper의 retry/sleep 제거
  - redundant plain PUB/SUB integration 삭제
  - `Socket.set_recv_handler()`, `set_subscribe_handler()`,
    `set_send_ready_handler()`와 `Spot.set_send_ready_handler()` 반영
  - `bindings/python/examples/`를 `pair/pubsub/dealer-router/stream/spot`
    recv/callback inventory로 확장
  - `doc/bindings/python*.md`를 callback/recv canonical surface 기준으로 갱신
  - `python -m pytest -q bindings/python/tests`: 통과
  - `python -m pytest -q bindings/python/tests/integration`: `4 passed`
  - final recheck:
    `python -m pytest -q`,
    `python -m pytest -q tests/integration`,
    `run_python_bindings_alignment_execution.sh --max-iterations 0`,
    examples 10개 smoke 통과

### 2.1 가장 큰 구조 문제

현재 Python 바인딩은 최신 `core`를 직접 반영한 표면이 아니라, 구 세대 C API와
호환용 심볼을 전제로 작성되어 있다.

대표 예:

- [`src/zlink/_ffi.py`](/home/hep7/project/kairos/zlink/bindings/python/src/zlink/_ffi.py)
  가 `zlink_setsockopt`, `zlink_getsockopt`, `zlink_msg_send`,
  `zlink_msg_recv`, `zlink_monitor_recv`, `zlink_discovery_new_typed`,
  `zlink_receiver_*`, `zlink_spot_pub_*`, `zlink_spot_sub_*` 같은 공식 헤더
  비중심 심볼에 직접 의존한다.
- [`src/zlink/_discovery.py`](/home/hep7/project/kairos/zlink/bindings/python/src/zlink/_discovery.py)
  는 `Receiver` 중심 서비스 모델을 public surface에 그대로 노출한다.
- [`src/zlink/_spot.py`](/home/hep7/project/kairos/zlink/bindings/python/src/zlink/_spot.py)
  는 split `spot_pub` / `spot_sub` 모델을 public `Spot` 구현의 중심으로 사용한다.
- [`src/zlink/_core.py`](/home/hep7/project/kairos/zlink/bindings/python/src/zlink/_core.py)
  는 raw `send` / `recv(size)` 중심인데, 최신 core의 multipart / callback /
  subscription / routing-id 모델과 직접 연결되는 canonical surface가 부족하다.

### 2.2 공식 헤더 기준으로 이미 어긋난 심볼들

공식 헤더에는 없거나 더 이상 canonical surface가 아닌데 Python이 직접 의존하는
심볼 예시는 다음과 같다.

- 옵션/메시지: `zlink_setsockopt`, `zlink_getsockopt`, `zlink_msg_send`,
  `zlink_msg_recv`, `zlink_msg_more`
- 모니터링: `zlink_monitor_recv`, `zlink_socket_monitor`
- 서비스 discovery/receiver: `zlink_discovery_new_typed`,
  `zlink_discovery_get_receivers`, `zlink_receiver_*`
- registry: `zlink_registry_set_endpoints`, `zlink_registry_start`,
  `zlink_registry_setsockopt`
- spot: `zlink_spot_pub_*`, `zlink_spot_sub_*`

현재 일부 심볼은 네이티브 라이브러리에 남아 있을 수 있지만, 공식 헤더 표면이
아니므로 Python 바인딩 정렬 작업의 기반으로 삼으면 안 된다.

### 2.3 최신 core가 요구하는 방향

최신 `core`는 다음 방향으로 재편되어 있다.

- 옵션 계층:
  - 공통 옵션은 `zlink_set_option` / `zlink_get_option`
  - 특화 옵션은 `zlink_set_router_option`, `zlink_set_pub_option`,
    `zlink_set_sub_option`, `zlink_set_stream_option`
  - 라우팅 ID / 구독은 전용 API
    `zlink_set_routing_id`, `zlink_get_routing_id`,
    `zlink_set_subscription`, `zlink_unset_subscription`
- 메시지 계층:
  - canonical send/recv는 `zlink_send`, `zlink_send_rid`, `zlink_recv`,
    `zlink_publish`, `zlink_subscribe`
- 콜백/이벤트 계층:
  - `zlink_recv_handler`, `zlink_subscribe_handler`, `zlink_send_ready_handler`
  - `zlink_socket_monitor_open`, `zlink_socket_monitor_recv`,
    `zlink_monitor_status`
  - `zlink_service_monitor_open`, `zlink_service_monitor_recv`
- 서비스 계층:
  - `Registry`: bind/config/snapshot/query
  - `Discovery`: `(ctx, service_type, service_name)` 기반 단일 service view
  - raw socket discovery attach: `zlink_socket_attach_discovery`
  - unified `Spot`: `zlink_spot_new`, `zlink_publish`, `zlink_subscribe`
  - `SpotNode`: topology/lifecycle/snapshot 역할

### 2.4 현재 Python 바인딩의 장점

정렬 작업은 단순 삭제가 아니라, 이미 있는 Python 장점을 새 contract 아래로
흡수해야 한다.

- `send()` 경로는 `bytes`뿐 아니라 buffer protocol 입력을 이미 일부 받는다.
- `recv_into()` 경로는 caller-owned writable buffer 재사용이 가능하다.
- `ctypes` 기반이라 wheel 포함 native artifact 선택 로직이 이미 존재한다.
- 테스트에 `bench_fastpath`가 있어 hot path 회귀를 감시할 기반이 있다.

### 2.5 2026-03-26 baseline 즉시 확인된 실패

2026-03-26에 `bindings/python`에서 아래 baseline 명령을 직접 실행해 현재 실패를
확인했다.

```bash
cd bindings/python && python -m pytest -q tests/test_version.py tests/test_enums.py
```

현재 주요 실패 원인은
[`_ffi.py`](/home/hep7/project/kairos/zlink/bindings/python/src/zlink/_ffi.py)가
라이브러리 로드 시점에 `zlink_stream_attach_len32be`를 eager bind 하다가,
현재 `core/build/lib/libzlink.so`에 해당 심볼이 없어 import 단계에서 즉시
`AttributeError`를 일으키는 점이다.

이 실패는 테스트 문제보다 FFI contract 정렬 문제에 가깝다. 따라서 초기 작업 우선순위는
"테스트를 우회해서 통과시키기"가 아니라, `_ffi.py`의 eager symbol binding 구조를
최신 공식 헤더 기준으로 재정렬하는 것이다.

## 3. 설계 원칙

- 공식 헤더 우선:
  - Python FFI contract는 `core/include/zlink.h` 기준으로만 정의한다.
- Python 우선:
  - out parameter, pointer-like helper, role별 얕은 wrapper 확산을 피한다.
  - public API는 "행위는 `Socket` / `Spot`, 데이터와 ownership은 `Message` 또는
    `Received*` aggregate"로 설명 가능해야 한다.
- POSD 우선:
  - 동일 개념을 여러 Python 타입으로 중복 노출하지 않는다.
  - 구 세대 호환 래퍼를 유지하기 위해 전체 복잡도를 키우지 않는다.
- 호환 심볼 금지:
  - 네이티브 라이브러리에 남아 있더라도 공식 헤더 밖 심볼은 신규 코드에서
    사용하지 않는다.
- 라이프사이클 명확화:
  - `close()`, context manager, callback pinning, borrowed buffer lifetime이
    API 설명만으로 이해 가능해야 한다.
- copy/borrow 명시:
  - 복사 경로와 borrow 경로는 메서드 이름만 보고 구분 가능해야 한다.
  - 내부 heuristic으로 복사 여부가 달라지는 API는 canonical surface로 채택하지
    않는다.
- hot path 절제:
  - hot path에서 자동 UTF-8 encode/decode, 숨은 `list` materialize,
    불필요한 `bytes()` 복사를 만들지 않는다.
- 성능 보존:
  - `bytes`, `bytearray`, `memoryview`, `array`, `mmap` 등 Python buffer
    protocol fast path를 유지한다.
- 검증 우선:
  - 각 단계마다 단위/통합 검증 기준을 둔다.

## 3.1 범위 고정 결정

이번 작업에서 아래 항목은 더 이상 열어두지 않고 고정한다.

- 공식 표면 기준:
  - Python FFI binding은 `core/include/zlink.h`에 선언된 공개 함수/enum/struct에만
    의존한다.
- `Receiver`:
  - 최신 core 공식 표면과 불일치하므로 유지 대상이 아니다.
  - `Receiver`는 deprecated 유지보다 삭제를 기본 방침으로 한다.
- `Spot`:
  - split `spot_pub` / `spot_sub` 모델은 유지하지 않는다.
  - public `Spot`은 unified `zlink_spot_new` 기반으로 다시 구현한다.
- `Discovery(ctx, service_type)`:
  - 유지하지 않는다.
  - `service_name` 없는 discovery view는 최신 모델과 맞지 않으므로 새 생성자만
    남긴다.
- `Registry.set_endpoints()` / `start()`:
  - 새 canonical API는 `bind(pub, router)`다.
  - 기존 메서드는 유지하지 않는다.
- 테스트 전략:
  - core 포팅 테스트 확대는 중단한다.
  - 새 검증 자산은 `examples/`, contract tests 두 축으로만 추가한다.

## 3.2 비목표

- `core/tests`의 transport/protocol/reconnect matrix를 Python에서 다시 구현하는 것
- 최신 core에 없는 호환 심볼을 Python에서 계속 노출하기 위한 adapter layer 유지
- 별도 성능 전용 실행물을 이번 범위에 넣는 것
- `Receiver`, split `Spot`, old getsockopt 모델을 장기 호환 API로 승격하는 것

## 4. 공개 API 재정렬 방향

### 4.1 유지할 축

- `Context`
- `Socket`
- `Message`
- `Poller`
- `MonitorSocket`
- `Discovery`
- `Registry`
- `SpotNode`
- `Spot`

### 4.2 축소/제거/치환 대상

| 현재 Python 표면 | 문제 | 목표 방향 |
|---|---|---|
| `Receiver` | 최신 core 공식 서비스 모델과 불일치 | `Socket` + `Discovery` attach 모델로 치환 |
| split `spot_pub` / `spot_sub` 기반 `Spot` | unified `zlink_spot_new` 와 불일치 | unified `Spot` handle로 재작성 |
| `Registry.set_endpoints()` + `start()` | 최신 core는 `bind()` 중심 | `Registry.bind(pub, router)`로 치환 |
| `Discovery(ctx, service_type)` | 최신 core는 service name 고정 view | `Discovery(ctx, service_type, service_name)`로 치환 |
| `Socket.setsockopt()` / `getsockopt()` old zmq-style | 최신 core는 option family 분리 | 전용 option API + helper로 재설계 |
| `Socket.recv(size)` | caller가 size를 임의 추정해야 함 | `recv_message()`, `recv_into()`, `recv_multipart()` 계층으로 재설계 |
| `Spot.recv()` returns raw `(topic, list[bytes])` | copy/borrow 경계 불명확 | `ReceivedTopicMessage` aggregate로 치환 |

### 4.3 임시 호환 정책

레거시 API를 한 번에 삭제하지 말고 다음 순서로 간다.

1. 최신 core 기반 신규 내부 contract 구축
2. 신규 Python API 추가
3. 기존 API를 신규 API 위에서 재구현 가능한 범위만 `DeprecationWarning`으로 유지
4. 재구현이 억지인 API는 early removal 후보로 분류

`Receiver`와 split `Spot`은 억지 호환이 복잡도를 크게 올리므로, 강한 삭제/치환
후보로 본다.

### 4.4 검증 전략

Python 바인딩 테스트는 `core` 동작을 다시 증명하는 대규모 포팅 테스트가 아니라,
"Python binding이 최신 C API를 안전하고 Python답게 노출하는가"만 검증해야 한다.

최종 방향:

- `examples/`를 새로 만들고 패턴별 `recv` / `callback` 예제를 제공한다.
- `tests/`는 작고 명확한 contract test 집합만 유지한다.
- 성능은 별도 산출물보다 API/구현 계약과 fast-path regression test로 관리한다.

남길 테스트:

- native library load / symbol smoke
- `Context` / `Socket` / `Message` lifecycle
- `send` / `recv_into` / multipart mapping
- callback mode와 polling mode 배타성
- option / routing-id / subscription 매핑
- monitor / service-monitor wrapper
- unified `Spot` / `Discovery` / `Registry` contract
- Python 예외 전파와 ownership 규칙
- buffer protocol fast path (`bytes`, `bytearray`, `memoryview`)

삭제 대상:

- transport matrix 복제
- reconnect / protocol corner case 대량 포팅
- `core` correctness를 다시 검증하는 테스트
- sleep/retry 기반 flaky helper

샘플 후보:

- `examples/pair_recv.py`
- `examples/pair_callback.py`
- `examples/pubsub_recv.py`
- `examples/pubsub_callback.py`
- `examples/dealer_router_recv.py`
- `examples/dealer_router_callback.py`
- `examples/stream_recv.py`
- `examples/stream_callback.py`
- `examples/spot_recv.py`
- `examples/spot_callback.py`

## 4.5 Python 스타일 API 결정

이번 작업에서 raw 계층 public API는 Python 스타일로 다음 원칙을 따른다.

- `Socket`의 행위 이름은 `send`, `recv_*`, `subscribe`, `unsubscribe`,
  `set_option` 계열로 통일한다.
- payload 입력은 `bytes` 전용이 아니라 bytes-like object 전체를 받는다.
- hot path 기본값은 텍스트가 아니라 binary다.
- 문자열 convenience API가 필요하면 `send_text`, `recv_text`처럼 copy 경로를
  이름으로 분리한다.
- receive 결과는 단순 `bytes`만 반환하지 않고 ownership이 필요한 경우
  `ReceivedMessage`, `ReceivedMultipart`, `ReceivedTopicMessage` 같은 aggregate로
  통일한다.
- zero-copy borrow가 가능한 경로는 `memoryview` 기반으로 노출하되, owner object가
  살아있는 동안만 유효하다는 계약을 명확히 문서화한다.
- `Context`, `Socket`, `Message`, `Spot`, `Registry`, `Discovery`는 모두
  context manager를 제공한다.
- public hot path에서는 자동 decode, 자동 JSON, 자동 list flatten 같은 숨은
  할당을 넣지 않는다.

## 4.5.1 canonical API 초안

아래 시그니처는 상세 구현을 강제하는 ABI가 아니라, public surface 정렬 방향을
고정하기 위한 초안이다.

```python
class Context:
    def close(self) -> None: ...
    def __enter__(self) -> "Context": ...
    def __exit__(self, exc_type, exc, tb) -> None: ...

class Message:
    @classmethod
    def from_(cls, data: Buffer) -> "Message": ...
    @classmethod
    def wrap_buffer(cls, data: Buffer) -> "Message": ...
    def to_bytes(self) -> bytes: ...
    def view(self) -> memoryview: ...

class Socket:
    def send(self, data: Buffer | Message, *, flags: int = 0) -> int: ...
    def send_multipart(
        self, parts: list[Buffer | Message], *, flags: int = 0
    ) -> None: ...
    def recv_message(self, *, flags: int = 0) -> "ReceivedMessage": ...
    def recv_multipart(self, *, flags: int = 0) -> "ReceivedMultipart": ...
    def recv_into(self, buffer: Buffer, *, flags: int = 0) -> int: ...
    def set_option(self, option, value) -> None: ...
    def get_option(self, option): ...
    def set_routing_id(self, routing_id: Buffer) -> None: ...
    def get_routing_id(self) -> bytes: ...
    def subscribe(self, topic: Buffer) -> None: ...
    def unsubscribe(self, topic: Buffer) -> None: ...

class Discovery:
    def __init__(self, ctx: Context, service_type, service_name: str): ...

class Registry:
    def bind(self, pub_endpoint: str, router_endpoint: str) -> None: ...

class Spot:
    def publish(self, topic: str | bytes, payload, *, flags: int = 0) -> None: ...
    def recv(self, *, flags: int = 0) -> "ReceivedTopicMessage": ...
    def set_handler(self, handler) -> None: ...
```

## 4.5.2 성능 계약

- `bytes` 입력은 추가 복사 없이 바로 전달 가능한 경로를 유지한다.
- writable contiguous buffer는 `recv_into()`에서 직접 채운다.
- `memoryview` / `bytearray` / `array('B')` / `mmap` 등 buffer protocol 입력은
  불필요한 `bytes()` 변환을 만들지 않는다.
- borrow path는 명시적 API에서만 허용하고, copy path와 섞지 않는다.
- callback hot path에서는 topic decode를 선택적으로 만들고, raw bytes topic 경로를
  별도로 둔다.
- multi-part 수신은 part별 hidden copy 대신 aggregate owner가 native lifetime을
  관리한다.
- 성능 회귀 검증은 canonical API와 perf runner smoke 기준으로 유지한다.

## 5. 단계별 실행 계획

### Phase 0. 기준 정리

- `core/include/zlink.h` 기준 공식 함수/enum/struct 목록 확정
- Python FFI에서 비공식 심볼 lookup 목록 제거 대상 확정
- 레거시 public API 제거/치환 목록 확정

완료 기준:

- 메인 플랜과 execution guide가 코드 변경 범위를 고정한다.

### Phase 1. FFI / native contract 재정렬

- `_ffi.py`를 공식 헤더 기준 downcall/struct layout으로 재작성
- `ctypes` 시그니처를 최신 `core` 함수군에 맞춤
- socket monitor / service monitor / registry / discovery / spot 새 표면 추가

완료 기준:

- 공식 헤더 비기재 심볼 direct lookup이 남지 않는다.

### Phase 2. Socket / Message / Received canonical API

- `_core.py` 중심 raw 계층 재설계
- `recv(size)` 의존 표면 축소
- `Message` / `Received*` ownership 모델 도입
- context manager 추가

완료 기준:

- Python 사용자는 size guess 없이 canonical receive API를 사용할 수 있다.
- copy path와 borrow path가 이름으로 구분된다.

### Phase 3. option / monitor / poller 계층 정렬

- old `setsockopt` / `getsockopt` 경로 제거
- 전용 option family helper 도입
- socket monitor / service monitor / snapshot wrapper 정리
- poller가 새 receive/callback 표면과 충돌하지 않도록 정리

완료 기준:

- old zmq-style option API가 canonical surface에서 제거된다.

### Phase 4. service / discovery / registry / spot 정렬

- `Receiver` 제거
- `Discovery(ctx, service_type, service_name)` 도입
- `Registry.bind(pub, router)` 도입
- unified `Spot` / `SpotNode` 재설계

완료 기준:

- 최신 core service model과 맞지 않는 split abstraction이 public surface에 남지
  않는다.

### Phase 5. examples / tests / docs 정리

- `examples/` 추가
- contract test만 유지
- fail-fast 정책에 맞지 않는 sleep/retry 제거
- migration note와 API 문서 정리

완료 기준:

- 사용자-facing 예제와 contract test가 새 API를 설명한다.

## 6. 파일 단위 작업 범위

우선 수정 대상:

- `bindings/python/src/zlink/_ffi.py`
- `bindings/python/src/zlink/_core.py`
- `bindings/python/src/zlink/_monitor.py`
- `bindings/python/src/zlink/_poller.py`
- `bindings/python/src/zlink/_discovery.py`
- `bindings/python/src/zlink/_spot.py`
- `bindings/python/src/zlink/__init__.py`
- `bindings/python/tests/**`
- `bindings/python/plan/bindings/**`

후속 문서 대상:

- `doc/bindings/**`
- `bindings/python/examples/**`

## 7. 테스트 및 검증 명령

현재 즉시 가능한 smoke:

```bash
./bindings/python/plan/bindings/run_python_bindings_alignment_execution.sh --max-iterations 0
```

현재 baseline 진단:

```bash
cd bindings/python && python -m pytest -q tests/test_version.py tests/test_enums.py
cd bindings/python && python -m pytest -q
```

기능 검증:

```bash
cd bindings/python && python -m pytest -q
```

최종 상태 검증 예시:

```bash
cd bindings/python && python -m pytest -q
cd bindings/python && python -m pytest -q tests/integration
```

주의:

- 2026-03-26 직접 확인한 baseline 주요 실패 원인은 `_ffi.py`가
  `zlink_stream_attach_len32be`를 eager bind 하다가 import 단계에서 즉시 실패하는
  점이다.
- serial lane이 필요한 테스트는 동시에 여러 `pytest` 프로세스로 돌리지 않는다.
- flaky를 retry로 덮지 않는다.

## 8. 구현 착수 전 체크리스트

- `core/include/zlink.h`를 기준으로 비공식 심볼 목록을 다시 확인했는가
- 삭제 대상 `Receiver` / split `Spot` / old option API를 문서에서 먼저 고정했는가
- Python 스타일 API의 copy/borrow 경계를 이름으로 설명할 수 있는가
- context manager와 explicit `close()` ownership 규칙이 정리되었는가
- tests가 fail-fast 정책을 어기지 않는가

## 9. 최종 완료 기준

아래 조건을 모두 만족하면 완료다.

- Python FFI가 공식 헤더 밖 심볼에 직접 의존하지 않는다.
- Python 공개 API가 최신 `core` service / option / monitor / spot 모델과 정렬된다.
- `Receiver`, split `Spot`, old `setsockopt` / `getsockopt`가 canonical surface에서
  제거된다.
- Python다운 API와 성능 계약이 `examples/`와 tests에서 동시에 검증된다.
- execution guide의 남은 작업 체크리스트가 모두 `완료`다.
- 미적용 사항이 없습니다.
