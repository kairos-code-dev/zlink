# Python Core 11 POSD·DDD 설계 검토

> 이 기록은 Python Core 11 raw-only 작업의 POSD·DDD self-review다. 구현자와 분리된
> frontier reviewer의 최종 `CLEAN` 판정은 포함하지 않는다.

## 현재 판단

Python binding은 Core 11 raw contract만 연결해야 한다. 구현 전 source에는 raw socket과 message를
제공하는 binding 안에 Framework 기능과 Core 11에 없는 native 선언이 함께 있었다. 이번 작업은 기존
raw public surface를 유지하면서 Framework 책임, 이전 FFI와 호출부 우회를 제거하는 범위로 정했다.
새 public API나 샘플 전용 adapter는 추가하지 않았다.

## DDD event storming 결과

Python binding에서 호출자가 관찰하거나 책임지는 사건을 먼저 적으면 다음 흐름이다.

| Event | Command와 actor | 상태·실패 의미 |
|-------|-----------------|----------------|
| `ContextCreated` | application caller가 `create_context` 실행 | native context handle은 Python Context가 소유한다 |
| `MessageAllocated` | caller가 message factory 실행 | 초기화된 message는 한 owner만 가진다 |
| `MessageMoved` | socket send command | 성공한 send 뒤 source message를 다시 사용하지 않는다 |
| `MessageReceived` | socket receive command | 수신 parts owner가 `Received` envelope로 이동한다 |
| `CallbackAttached` | caller가 handler 등록 | Python callback reference는 native callback보다 오래 유지된다 |
| `CallbackInvoked` | Core I/O thread가 callback 실행 | callback 중 Python object를 읽고 쓸 때만 Python 실행 경계를 복원한다 |
| `HandleClosed` | caller가 close 실행 | close 성공 뒤 새 API 진입은 거부되고 callback·pending request가 정리된다 |
| `ReceiveTimedOut` 또는 `ReceiveNoData` | caller가 nonblocking receive 실행 | no-data는 오류가 아니며 함수군별 정해진 반환 형태를 유지한다 |

### 경계와 aggregate 후보

- **Core raw bounded context**: `Context`, `Message`, `RoutingId`, raw socket, monitor, poller,
  timer와 Core error code를 같은 의미로 관리한다.
- **Python adapter boundary**: `ctypes` layout, native callback trampoline, reference lifetime와
  Python exception mapping을 숨긴다. public contract에는 native symbol이나 Core 내부 자료구조를
  노출하지 않는다.
- **Framework service bounded context**: `Spot`, `Actor`, service dispatch와 bound STREAM session의
  lifecycle은 Framework가 소유한다. Core 11 Python binding이 이 용어를 재정의하지 않는다.
- **Context aggregate**: native context와 그 context에서 만든 socket·timer의 종료 순서를 관리한다.
- **Message/Received aggregate**: parts의 초기화·이동·close와 `Received`의 재사용 상태를 함께 관리한다.
- **Socket aggregate**: native handle, pending request, callback reference와 close gate를 관리한다.
- **Package adapter**: 승인된 Core prefix의 header·library와 bundled runtime의 provenance를 한 곳에서
  결정한다. repository `core/build`를 consumer fallback으로 해석하지 않는다.

## POSD red flags와 선택

### 확인한 red flag

1. `zlink/__init__.py`와 `contracts/__init__.py`가 raw contract와 service contract를 동시에
   재수출한다. common path를 읽기 위해 caller가 Framework service surface까지 배워야 하므로
   overexposure와 domain-boundary leak가 있다.
2. `ffi.py`가 raw layout과 Core 11에 없는 service symbol을 한 `_Lib`에 등록한다. header 변경 한
   번이 여러 service wrapper와 FFI 선언에 전파되는 information leakage다.
3. `socket_base_impl.py`와 `message_materializer.py`가 raw routing과 service routing을 한 receive·send
   path에서 분기한다. 같은 message ownership 규칙을 두 경계가 나누어 관리하는 temporal
   decomposition과 special-general mixture다.
4. `router_spot_support.py`, `stream_actor_support.py`와 service operation 객체는 raw socket method를
   전달하면서 service 정책을 호출부에 노출한다. raw binding 기준으로는 shallow/pass-through layer다.
5. `setup.py`와 `_native_loader.py`가 repository Core path를 암묵적으로 선택한다. package input과
   runtime provenance가 서로 다른 모듈에 누출되어 clean consumer가 어떤 Core를 읽는지 숨겨진다.

### 비교한 설계

| 대안 | 장점 | 위험 |
|------|------|------|
| A. 기존 raw modules를 유지하고 service branch·export·FFI·fixture만 제거 | caller surface의 raw 의미와 검증된 message/socket 구현을 보존하고 변경 경계를 Core 11 계약에 맞출 수 있다 | 기존 raw path에 남은 Core 10 signature를 모두 찾아 고쳐야 한다 |
| B. Python package를 새 raw facade와 새 native provider 중심으로 다시 구성 | 책임 경계를 한 번에 다시 그릴 수 있다 | public API와 ownership semantics를 재작성하게 되고, 새 pass-through layer와 회귀 범위가 커진다 |

대안 A를 선택한다. POSD 관점에서 이미 존재하는 raw module이 충분히 깊은 동작을 갖고 있어
그 위에 새 facade를 추가하는 것보다 service 책임을 소유 경계에서 제거하는 편이 caller 복잡성과
변경 증폭을 함께 줄인다. DDD 관점에서도 Framework service를 Core raw bounded context 밖으로
밀어내므로 용어와 lifecycle owner가 한 곳에 남는다.

## 구현 불변식

- raw FFI는 `core/include`의 Core 11 allowlist에 있는 symbol과 layout만 선언한다.
- `Message`와 `Received`가 native parts를 소유하며, send 성공 뒤 source owner를 재사용하지 않고,
  close와 예외 경로에서 각 part를 정확히 한 번만 닫는다.
- socket·timer·monitor·poller handle의 close는 idempotent contract를 따르고, callback reference는
  native registration보다 먼저 해제되지 않는다.
- nonblocking no-data를 오류로 바꾸거나 다른 함수군의 반환형으로 섞지 않는다.
- package build는 명시한 Core candidate prefix만 사용하고, clean consumer는 repository source 또는
  `core/build` fallback을 읽지 않는다.

## 구현 후 self-review 결과

- Framework 기능을 raw package root, FFI, socket branch, fixture, sample과 perf에서 제거했다. source scan과
  optimization guard에 이전 export, dynamic fallback 또는 raw path 우회가 남아 있지 않다.
- `Message`와 `Received`의 native parts ownership, callback reference lifetime, close gate와 no-data 반환
  형태를 production path와 contract test에서 다시 확인했다.
- `tests/hot-path-cost-inventory.json`의 5개 비용 항목은 allocation, copy, lock, GIL과 no-cost로 분류되어
  있고 `unclassified`는 0건이다. `Py_BEGIN_ALLOW_THREADS`, part failure cleanup과 native bridge guard가
  유지된다.
- Package provenance를 setup script와 candidate builder에 모았다. build는 명시적인 `ZLINK_CORE_PREFIX`를
  받고 clean consumer는 repository `src`와 `core/build`를 fallback으로 사용하지 않는다.
- POSD red flag를 다시 읽은 결과, 이 변경에는 새 public pass-through facade, 중복 codec/provider, caller에
  native 결정 노출이 없다. 확인 가능한 Critical·High·Medium finding은 없었다.

이 결과는 같은 checkout에서 수행한 Codex self-review다. 독립 reviewer가 동일한 source manifest와 fresh
evidence를 승인하기 전에는 전체 작업을 `CLEAN`으로 표시하지 않는다.
