# IPC 데드락 분석 (ASIO Proactor)

## 요약
- `restart_input()`의 `_read_pending` 체크만으로는 **논리적 경쟁(missed wakeup)** 을 막기 어렵다.
- 핵심은 **상태 전이( backpressure 해제 )와 read 재무장(arm )의 원자적 결합**이며, 이는 **단일 실행 컨텍스트(ASIO strand/동일 io_context thread)에서 직렬화**되어야 한다.
- libzmq의 `speculative read` 패턴은 **IPC처럼 매우 빠른 전송에서 안정성을 높이는 설계**이며, ASIO에서도 유사하게 적용하는 것이 바람직하다.

## 1) `restart_input()`에서 `_read_pending` 체크만으로 충분한가? 아니면 mutex가 필요한가?
- **충분 조건**: `restart_input()`과 `on_read_complete()`가 **동일 io_context thread(또는 strand)에서 직렬 실행**된다면, `_read_pending` 체크만으로 충분하며 mutex는 불필요하다.
- **불충분 조건**: 두 함수가 **다른 스레드**에서 실행될 수 있다면, `_read_pending` 뿐 아니라 `_input_stopped`, `_pending_buffers`, `_inpos/_insize` 등 다수의 공유 상태에 대해 **데이터 레이스**가 발생한다. 이 경우:
  - mutex를 걸어도 되지만,
  - ASIO 권장 방식은 **strand(또는 `post/dispatch`로 동일 executor에 강제)** 를 통해 **직렬화**하는 것이다.
- 현 구조상 `io_thread`는 `io_context`를 직접 돌리고 있고(mailbox도 동일 io_context 사용), 정상 설계라면 **엔진 상태는 단일 thread로 귀결**되어야 한다. 그러나 실제 실행 경로가 분리되어 있거나(예: 외부에서 직접 호출), 혹은 일부 핸들러가 다른 executor에서 실행된다면 레이스가 현실화된다.

결론:
- **동일 thread 보장**이면 mutex 불필요, `_read_pending` 체크만으로도 논리 일관성 유지 가능.
- **보장 불명확**이면 mutex보다는 **strand 직렬화**가 정답이며, 그 전에는 `_read_pending`만으로 안전하지 않다.

## 2) libzmq의 speculative read 패턴(무조건 in_event_internal 호출)을 따라야 하는가?
- IPC처럼 **초저지연/고속 전송**에서, backpressure 해제 직후에 **즉시 읽기 시도**를 하는 것이 데드락 회피에 유리하다.
- libzmq는 `restart_input()`에서 **speculative read**로 즉시 데이터를 한 번 더 끌어오는 구조(`in_event_internal()`)를 사용하여 **read-arm 누락을 구조적으로 방지**한다.
- ASIO 대응은 다음 중 하나:
  1) **무조건 `start_async_read()` 호출** (idempotent하게 설계되어야 함)
  2) **동기 `read_some` 1회 시도 + EAGAIN 시 async 전환** (libzmq `in_event_internal`에 가장 근접)

결론:
- **따르는 것이 바람직**하며, 특히 IPC에서 재현되는 문제에는 효과적이다.
- 단, **동일 thread 직렬화**와 **read-in-flight 단일화 보장**이 전제되어야 한다.

## 3) 정확한 race condition이 어디서 발생하는가?

### 후보 1: read 재무장 누락 (missed wakeup)
- `restart_input()`은 backpressure 해제 후 read가 이미 pending이라고 가정하지만,
- 실제로는 `_read_pending`이 `false`로 떨어진 시점과 재무장 사이에 틈이 생긴다.
- IPC에서는 **데이터 도착이 read 재무장보다 빠르기 때문에** 이 틈이 치명적이다.

### 후보 2: 비직렬화로 인한 상태 경쟁
- `on_read_complete()`는 `_read_pending=false` 후 `_inpos/_insize` 등 입력 버퍼 상태를 수정한다.
- 같은 시점에 `restart_input()`이 다른 스레드에서 수행된다면:
  - `_read_pending` 플래그 판단이 꼬이고,
  - `start_async_read()`가 **버퍼 상태를 이동(memmove)** 하여,
  - `on_read_complete()`가 사용 중인 버퍼 포인터와 충돌 가능.
- 결과적으로 **read가 재무장되지 않거나 내부 상태가 깨져 입력이 정지**한다.

### 후보 3: backpressure 해제 후 pending buffer 미처리
- `restart_input()` 내부에서 `_pending_buffers` 처리 중 `rc == 0` (더 많은 데이터 필요)일 때,
  - 남은 버퍼를 보존하지만 `_input_stopped=false`로 전환한다.
  - 이 경우 **pending buffer가 남아있는데 input이 재개된 것으로 표시**되며,
  - 이후 새 read 경로에서는 `_pending_buffers`가 처리되지 않아 **데이터 순서/정지 위험**이 있다.
- 이 설계는 **speculative read 도입과 함께 재검토 필요**하다.

정리:
- 가장 위험한 레이스는 **read-arm 누락**과 **비직렬화된 상태 갱신**이다.
- 특히 `_read_pending`만으로 재무장을 판단하는 로직은 **handler가 직렬화되지 않을 때 취약**하다.

## 4) ASIO proactor 패턴에서 올바른 동기화 방법은?

### 권장 설계
1) **모든 엔진 상태 변경을 동일 executor로 직렬화**
   - `boost::asio::strand` 사용 또는
   - `post/dispatch`로 `io_context` 한 곳에서만 실행
2) **read-in-flight 단일화 보장**
   - `_read_pending`은 단일 executor 내에서만 접근
   - 재무장은 항상 같은 executor에서 실행
3) **speculative read 적용**
   - backpressure 해제 후 즉시 1회 read 시도
   - 실패 시 async read로 전환

### 구체적 실행 원칙
- `restart_input()`은 **직렬 executor에서만 호출**되어야 함
- `on_read_complete()`와 동일 executor 강제 (strand)
- **"read 재무장"은 항상 상태 전이와 묶어 실행**
  - backpressure 해제 시 `start_async_read()`를 **무조건 호출**하거나
  - `read_some` 1회 + async fallback

## 결론 및 제안
- `_read_pending` 체크만으로는 **설계 전제가 충족될 때만 안전**하다.
- **ASIO strand 직렬화 + speculative read**가 가장 안정적이며, IPC 초고속 환경에서도 안전하다.
- 현재 데드락은 **read 재무장 누락 / 상태 경쟁 / pending buffer 처리 경계** 중 하나가 핵심 원인일 가능성이 높다.

## 참고 위치
- `src/asio/asio_engine.cpp` (`on_read_complete`, `restart_input`, `start_async_read`)
- `src/session_base.cpp` (`write_activated`)
- `src/object.cpp` (mailbox command dispatch)
- `src/asio/asio_poller.cpp` (io_context loop)
