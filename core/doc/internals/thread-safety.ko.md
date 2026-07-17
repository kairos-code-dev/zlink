[English](thread-safety.md) | [한국어](thread-safety.ko.md)

# Thread-Safety 구현 상세

이 문서는 zlink의 공개 thread-safety 계약 뒤에 있는 구현 세부 사항을
다룹니다. 사용자 관점의 가이드(무엇을 할 수 있고 없는지)는
[스레드 안전성 가이드](../guide/11-thread-safety.ko.md)를 참고하세요.

## 1. 개요

zlink의 공개 핸들(소켓, SPOT, 모니터)은
기본적으로 thread-safe이지만, 모든 API의 비용이 같지는 않습니다.
내부적으로 라이브러리는 모든 공개 API를 세 가지 계층 중 하나로 분류하고,
각 계층은 고유한 순서 의미론과 성능 제약, 에러 규칙을 따릅니다.

3계층 계약(three-tier contract)은 내부 설계 도구다. 사용자에게는 "자유롭게
보내고, 언제든 설정하고, 명확한 에러 코드로 닫기"로 보인다. 이 문서는
각 계층을 어떻게 구현하는지 설명한다.

## 2. Three-Tier Contract (형식 정의)

### 2.1 Hot Path Guaranteed

**대상 API:**

- `zlink_send()`
- `zlink_publish()`
- 허용된 callback 내 동일 handle `send` / `publish`

**순서 의미론:**

- 단일 스레드 순차: 한 스레드에서의 호출은 호출 순서를 보존합니다.
- 다중 스레드 동시: 각 메시지는 온전하게 전달됩니다. 인터리빙 순서는
  내부 직렬화 순서를 따르며 호출자 스케줄링 순서를 따르지 않습니다.
  스레드 간 순서는 보장되지 않습니다.
- Callback 스레드 send와 worker 스레드 send가 같은 핸들에서 동시에
  발생하면 동일한 concurrent 계약이 적용됩니다.

**성능 제약:**

- Send 경로에 broad lock 없음.
- Steady state에서 호출당 할당 없음.
- 최소 원자 연산 (admission gate의 acquire/release, 필요 시 CAS).
- 짧은 critical section만 허용 — retry loop, backoff wait 없음.
- 기존 send queue publication 경로를 재사용; 추가 wakeup 없음.

**Close 수락 후:** 새 send 진입은 `ESHUTDOWN`으로 실패합니다. 이미
enqueue된 메시지는 teardown 전에 소진됩니다 (drain-then-close).

### 2.2 Control Path Serialized

**대상 API:**

- `zlink_bind()` / `zlink_connect()` / `zlink_disconnect()`
- `zlink_set_option()` / `zlink_get_option()`
- `zlink_set_subscription()` / `zlink_unset_subscription()`
- `zlink_*_monitor_open()`
- `zlink_send_ready_handler()`
- 현재 공개 계약에 남아 있는 snapshot/query 함수

**정확성 우선 직렬화:**

- Same-handle concurrent control-path 호출은 안전합니다. 실행 순서는
  내부 직렬화가 결정하며, 호출자 스케줄링 순서와는 다릅니다.
- 성공적으로 반환된 control-path 호출의 효과는 이후 admitted된 모든
  호출에서 관측 가능합니다.

**경량 런타임 읽기 vs 무거운 조회:**

경량 읽기(`ZLINK_OPT_EVENTS`,
`ZLINK_OPT_LAST_ENDPOINT`, routing-id 조회)는 control path에 속하지만, heavy query/snapshot 호출의
전체 직렬화 비용을 수반하지 않습니다. 이들은 경량 서브셋으로 분류됩니다
— 항상 thread-safe이지만, 가장 무거운 직렬화 레인을 거치지 않습니다.

**Close 수락 후:** 새 control-path 진입은 `ESHUTDOWN`으로 실패합니다.
진행 중인 mutation은 정상 완료되거나 `ESHUTDOWN`으로 수렴합니다.

### 2.3 Lifecycle Strict

**대상 API:**

- `zlink_close()` (소켓)
- `zlink_spot_destroy()` / `zlink_mesh_node_destroy()`
- Monitor 핸들 `close` / `destroy`

**입장 허용 게이트(Admission gate) 메커니즘:**

Lifecycle 게이트는 두 가지 상태를 추적하는 단일 원자 워드다. 즉 closing
bit 와 in-flight(현재 실행 중인 API 호출) 카운트다. 이 덕분에 광범위 잠금(broad lock) 없이
빠른 실패(fail-fast) 결정이 가능하다.

```mermaid
stateDiagram-v2
    [*] --> Operational
    Operational --> Operational : API enter (in-flight++)
    Operational --> Operational : API exit (in-flight--)
    Operational --> Closing : close accepted (closing bit set)
    Operational --> Operational : close rejected (EBUSY, no latch)
    Closing --> Closed : drain complete, teardown
    Closed --> [*]
```

| 조건 | errno | 의미 |
|---|---|---|
| 같은 핸들에 in-flight admitted API 존재 | `EBUSY` | 다른 스레드가 실행 중; close 거부 |
| Close 이미 수락됨, 새 API 진입 | `ESHUTDOWN` | 핸들 종료 중; 새 작업 불가 |
| 이중 close / destroy | `EALREADY` | 이미 종료 진행 중 |

**핵심 규칙:**

- **`EBUSY` 는 fail-fast, no-latch(잠금 없는 빠른 실패).** 실패한 close 는 핸들을 closing
  상태로 영구 전이시키지 않는다. `EBUSY` 후 핸들은 이전 operational 상태로 완전히 복귀한다.
- **Drain-then-close(소진 후 닫기).** Close 가 수락되면 모든 enqueue 된 메시지가
  teardown 전에 소진된다. Drain 은 best-effort 가 아니다 — close 가
  수락된 시점에 enqueue 된 모든 메시지를 반드시 소진한다.
- **콜백에서의 self-close.** Send-ready 또는 monitor 콜백이
  자기 핸들의 `close` 를 호출하면, 실제 teardown 은 콜백 복귀(epilogue)까지
  지연된다. 콜백 내 use-after-free 를 방지한다.
- **STREAM raw 콜백 제한.** STREAM raw 콜백 내에서 `close` 를
  호출하면 `EBUSY` 로 실패한다 — raw dispatch가 in-flight 상태이기 때문이다.

## 3. Subject별 구현 참고

### 3.1 Raw Socket

Raw socket은 최우선 hot-path subject입니다. `send()` 구현은 내부
send queue에 발행합니다 — 단일 스레드 send에 쓰는 것과 같은
경로에 concurrent 진입을 위한 admission gate를 추가한 것입니다.

- **입장 허용 게이트:** 소켓당 단일 `atomic<uint32_t>` 워드가
  in-flight 카운트와 closing bit 를 추적합니다
  (`socket_base.hpp` / `socket_base.cpp`).
- **Send queue publication:** 동시 producer 들이 기존
  pipe/YPipe 인프라를 통해 enqueue 한다. I/O 스레드 consumer
  측은 바뀌지 않는다.
- **Control-path lock:** `bind`, `connect`, `set_option` 등은
  hot-path 입장 허용 게이트와 상태나 캐시 라인을 공유하지 않는
  별도 직렬화 경로를 거친다.

### 3.2 SPOT / SPOT Node

- **공개 계약:** `zlink_spot_publish`는 hot-path 계층을 따릅니다. `MeshNode`는
  membership과 설정을 소유하며, 구독 변경과 peer mutation은 control path를
  따릅니다.
- **Internal child:** `spot_pub` / `spot_sub`는 내부 구현 단위입니다.
  공개 thread-safety 계약의 직접 대상이 아닙니다 — parent/facade
  계약이 이들을 포함합니다. Child ordering과 open/destroy
  선형화는 내부 구현 관심사입니다.

### 3.3 Monitor

Monitor는 control-plane 중심 subject입니다.

- `monitor_open` / `monitor_close`는 control-path serialized입니다.
- Monitor delivery는 parent 핸들의 상태를 관찰하되, parent의
  hot path에 broad lock을 도입하지 않습니다.
- Parent의 hot-path send는 절대 monitor delivery에 의해 블로킹되면
  안 됩니다.

## 4. Service Public API Guard

`service_public_api.hpp` 는 SPOT과 SPOT Node가 lifecycle 과 control-path 계층을 구현할 때 쓰는
`service_public_api_guard_t` 클래스를 제공합니다.

**구현:**

가드는 단일 `atomic<uint32_t>` 를 쓰며, 하나의 워드에 두 필드를
패킹합니다:

- **Bit 31 (closing bit):** close/destroy 가 수락되면 설정됩니다.
- **Bits 0-30 (in-flight count):** 현재 입장 허용된 공개 API 호출
  수를 추적합니다.

**각 계층이 가드에 매핑되는 방식:**

| 계층 | 가드 역할 |
|---|---|
| Lifecycle strict | `begin_close_or_fail_busy()` 가 in-flight count 와 closing bit 를 원자적으로 확인합니다. in-flight > 0 이면 `EBUSY`, closing bit 가 이미 설정되어 있으면 `ESHUTDOWN` 을 반환합니다. 성공하면 closing bit 를 설정합니다. |
| Control path serialized | `enter_public_api()` 가 closing bit 를 확인한 후 in-flight count 를 증가시킵니다. Closing bit 가 설정되어 있으면 `ESHUTDOWN` 을 반환합니다. 모든 control-path 호출이 이 게이트를 거쳐 직렬화를 제공합니다. |
| Hot path | Send 경로는 가드의 broad lock 경로를 우회합니다. Control-path 직렬화와의 경합을 피하기 위해 별도의 최소 비용 입장 허용(소켓 수준 입장 허용 게이트)을 사용합니다. |

**Cancel close:** `cancel_close()` 가 closing bit 를 지워 no-latch
속성을 뒷받침한다 — 상위 수준에서 `begin_close_or_fail_busy()` 가
실패하면 핸들을 operational 상태로 복원할 수 있다.

## 5. Callback Dispatch 구현

콜백마다 실행 스레드가 다르다:

- **Socket message 핸들러**(`zlink_recv_handler`)는 async mailbox 처리를
  통해 I/O 스레드에서 실행된다.
- **Monitor 핸들러**는 service-control 런타임 스레드에서 실행된다 —
  monitor 이벤트를 recv 루프로 비우는 전용 task(`monitor_handler_task`)이며,
  부모의 I/O 스레드가 아니다.
- **Send-ready 핸들러**는 *호출자의* send 스레드에서 동기적으로 실행될 수
  있다. arm 된 알림이 send 경로의 `notify_send_ready_if_armed()` 에서 inline
  으로 발화한다.
- **MeshNode ready handler**(`zlink_mesh_node_set_ready_handler`)는
  SPOT dispatch worker pool 에서 실행된다.

dispatch 메커니즘은 원자적 load 로 핸들러 포인터를 읽으며,
hot path 에 광범위 잠금 없이 핸들러 교체의 가시성을 보장한다.

**핸들러 로딩:**

```cpp
// 필드는 socket_dispatch_bridge_t 에 있음
handler = socket_msg_handler.load(std::memory_order_acquire);
```

모든 핸들러 함수 포인터와 관련 subject/userdata 포인터는
`memory_order_acquire` load 를 씁니다. Setter 함수는 대응하는
`memory_order_release` store 를 씁니다. 이 덕분에 콜백 dispatch가
핸들러 포인터를 읽을 때, setter 스레드가 핸들러를 설치하기 전에 쓴
모든 데이터도 함께 볼 수 있습니다.

**콜백 진입/퇴장:**

```cpp
enter_callback_api();   // marks callback as in-flight
handler(subject, userdata);
leave_callback_api();   // clears in-flight flag
```

`enter_callback_api` / `leave_callback_api` 쌍은 `close` 가 콜백을 in-flight
연산으로 인식하게 한다. 콜백 종류에 따라 콜백 실행 중 `close` 는 `EBUSY` 로
거부되거나(STREAM raw), close 를 수락하고 epilogue 로 지연한다(send-ready/monitor).
자세한 것은 아래 참고.

**STREAM raw 콜백 제약 근거:**

STREAM raw 콜백은 더 엄격한 제한을 가진다 — raw 콜백 내에서의
`close` 는 항상 `EBUSY` 로 실패한다. Send-ready/monitor 콜백에서
`close` 가 epilogue 로 지연되는 것과 달리, STREAM raw dispatch는 지연 close 를
지원하지 않는다.

**Send-ready 핸들러 `EDEADLK` 제약 근거:**

자기 콜백 내에서 send-ready 핸들러를 교체하면 재진입(reentrant) dispatch
상황이 벌어진다. 이를 감지해 `EDEADLK` 로 거부한다.

## 6. 설계 원칙

### Hot path / control-plane 분리

- Hot-path 상태와 control-plane 상태는 별도 데이터 구조를 씁니다.
- Hot-path 캐시 라인과 control-plane 캐시 라인을 분리해 false sharing 을 방지합니다.
- Hot-path 입장 허용과 lifecycle 입장 허용은 최소한의 필요 상태만
  공유합니다 (closing bit 확인).

### Hot path: 최소 비용

- Send 당 최소 원자 연산 (입장 허용의 acquire/release, 구조적으로 필요한 경우에만 CAS).
- 짧은 critical section — sleep, retry, 할당 없음.
- 기존 send queue publication 경로 재사용.

### Control path: 직렬화 허용, hot path 저해 금지

- Control-path 연산은 내부 직렬화 레인과 짧은 critical section 을 쓸 수 있습니다.
- Control-plane lock 은 hot-path 입장 허용 게이트와 캐시 라인이나 lock 인스턴스를 공유하면 안 됩니다.
- Control-path 호출은 동시에 진행 중인 hot-path send 를 절대 블로킹하면 안 됩니다.

### Lifecycle: 단일 워드 입장 허용 게이트

- 입장 허용 게이트는 단일 원자 워드 — 다단계 잠금 프로토콜 없음.
- No-latch 빠른 실패: 거부된 close 는 잔여 상태를 남기지 않습니다.
- Drain-then-close: teardown 은 enqueue 된 메시지가 소비될 때까지
  기다린 후 자원 정리를 진행합니다.
