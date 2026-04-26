[스펙 목차](../README.ko.md)

# Draft -- auto-HWM message unit 공통 소켓 옵션

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`와 정식 spec 문서에 없는
> API, 상수, 기본 동작을 보장하지 않는다.
> 구현과 공개 헤더, 관련 테스트, 바인딩 문서, 정식 문서가 확정되면 이 내용을
> 정식 spec 문서에 나누어 반영한다.

## 1. 목적

auto-HWM은 context 메모리 예산을 HWM 메시지 개수로 바꾸기 위해 메시지 하나가
차지한다고 보는 기준 크기를 사용한다. 이 기준 크기를 이 문서에서는
**message unit**이라고 부른다.

현재 자동 HWM 계산은 내부 고정값을 사용한다. 이 방식은 간단하지만, 실제
벤치마크와 운영 환경에서는 문제가 생긴다.

- 64B 메시지와 64KiB 메시지가 같은 HWM 계산 단위를 쓰면 메모리 예산 설명이
  맞지 않는다.
- client/server 역할에 따라 메시지 크기 특성이 다를 수 있다.
- stream 계열과 일반 메시지 계열은 기본 메시지 단위가 다르게 잡히는 편이
  자연스럽다.
- SPOT 내부의 spotnode 공유 소켓과 spot endpoint 소켓은 같은 transport를 써도
  큐에 쌓이는 메시지 성격이 다를 수 있다.

따라서 message unit을 공통 소켓 옵션으로 노출해서, 새 API 함수를 추가하지 않고
기존 `zlink_set_option()` / `zlink_get_option()` 경로로 설정할 수 있게 한다.

## 2. 새 공통 소켓 옵션

### 2.1 이름

새 옵션 이름은 아래로 둔다.

```c
ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES
```

이 옵션은 `zlink_option_t`에 들어가는 **공통 소켓 옵션**이다.
enum 값은 기존 공통 옵션과 충돌하지 않는 다음 값을 사용한다.

```c
ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES = 0x3034
```

사용 예시는 아래와 같다.

```c
int msg_unit = 4096;
zlink_set_option(socket, ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES,
                 &msg_unit, sizeof(msg_unit));
```

### 2.2 타입과 값

| 항목 | 값 |
|------|----|
| 타입 | `int` |
| 단위 | byte |
| 기본값 | `0` |
| 허용값 | `0` 또는 양수 |
| 오류 | 음수이면 `EINVAL` |

`0`은 사용자가 값을 직접 정하지 않았다는 뜻이다. 이 경우 socket type별 기본값을
사용한다.

### 2.3 기본값

socket type별 기본 message unit은 아래와 같다.

| Socket type | 기본 `MsgUnit(B)` | 이유 |
|-------------|-------------------|------|
| `ZLINK_SOCKET_STREAM` | `1024` | stream은 작은 packet 단위 처리와 외부 TCP 흐름을 함께 고려한다 |
| 그 외 socket | `4096` | 일반 메시지 계열은 payload와 envelope, metadata 여유를 함께 본다 |

내부 core socket type 기준으로는 `ZLINK_CORE_SOCKET_STREAM`만 `1024`를 쓰고,
그 외 타입은 `4096`을 쓴다.

non-STREAM 기본 `MsgUnit(B)=4096`은 서버 간 통신의 평균 메시지 크기를 4KiB로
보는 정책값이다. 계산된 HWM이 목표 동시성에 비해 너무 작다면 먼저
`ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB` 값을 늘려야 한다.
`MsgUnit(B)`를 낮추는 것은 실제 평균 메시지 크기가 더 작다는 근거가 있을 때만
한다.

## 3. 의미

`ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`는 메시지 허용 최대 크기가 아니다.
이 옵션은 HWM을 메시지 개수로 환산하기 위한 계획 단위이다. 실제 메시지 크기
제한이나 transport buffer 크기를 직접 바꾸지 않는다.

| 옵션 | 의미 |
|------|------|
| `ZLINK_OPT_MAXMSGSIZE` | inbound 메시지를 받아들일 수 있는 최대 크기 제한 |
| `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` | auto-HWM 예산을 메시지 개수로 환산할 때 쓰는 계획 단위 |

예를 들어 `ZLINK_OPT_MAXMSGSIZE=1048576`은 1MiB 메시지를 허용한다는 뜻이지만,
대부분 메시지가 4KiB 근처라면 auto-HWM message unit은 `4096`으로 둘 수 있다.

## 4. 계산 방식

auto-HWM은 아래 순서로 계산한다.

```text
context total memory budget
-> budget domain
-> runtime reserve
-> auto/manual buffer accounting
-> queue budget
-> role group budget within the domain
-> message unit
-> group message slots
-> scope / target division
-> SNDHWM / RCVHWM
```

자동 HWM 계산은 소켓 하나만 보고 끝내면 안 된다. 같은 context 안에서 같은 예산을
함께 쓰는 소켓 묶음을 먼저 정해야 한다. 이 문서에서는 이 묶음을 **budget
domain**이라고 부른다.

초기 구현의 budget domain은 아래처럼 둔다.

| 대상 | Budget domain |
|------|---------------|
| 일반 core socket | 해당 socket의 auto-HWM plan |
| STREAM socket | 해당 STREAM socket의 auto-HWM plan |
| SPOT spotnode 공유 소켓 | 같은 spotnode runtime의 shared scope |
| SPOT spot endpoint/attachment 소켓 | 같은 spot runtime의 per-spot scope |

일반 core socket은 기존 동작과 호환되도록 소켓 단위 domain으로 시작한다. SPOT은
내부 소켓 수와 spot 수가 많기 때문에 반드시 shared/per-spot domain을 분리한다.
일반 core socket의 소켓 단위 domain은 초기 구현 범위이다. 이 방식은 기존 동작과
호환되지만 context 전체 memory limit을 모든 일반 socket에 전역으로 강제하는
모델은 아니다. 일반 socket의 context-wide global domain은 별도 후속 설계에서
다룬다.

공통 계산식은 아래와 같다.

```text
context_budget = context_total_memory_budget_bytes
runtime_reserve = context_budget * reserve_ratio
auto_buffer_bytes = sum(planned auto-managed socket buffer cost in domain)
manual_buffer_bytes = sum(planned user-managed socket buffer cost in domain)
queue_budget = clamp_queue_budget(context_budget
                                  - runtime_reserve
                                  - auto_buffer_bytes)
role_budget = queue_budget * role_weight
```

출력에서는 `role_budget`을 `RoleGroup(B)`로 표시한다. scope별로 다시 나눈 예산은
`ScopeGroup(B)`로 표시한다.

`GroupSlots` 계산은 아래와 같다. 일반 core socket처럼 scope 분리가 없는 경우에는
`ScopeGroup(B)`가 `RoleGroup(B)`와 같다.

```text
GroupSlots = ScopeGroup(B) / MsgUnit(B)
```

정수 나눗셈은 아래 원칙을 따른다.

```text
slots = ScopeGroup(B) / MsgUnit(B)
if ScopeGroup(B) > 0 and slots == 0:
  slots = 1
```

message unit이 role budget보다 큰 경우에도 최소 1개 메시지는 계획할 수 있어야
한다. 단, 수동 HWM을 설정하지 않은 자동 계산에서 이 최소값은 "예산 안에서 가능한
최소 진행 단위"이지, 큰 큐를 보장한다는 뜻은 아니다.

`MsgUnit(B)`는 아래 순서로 정한다.

1. 소켓에 `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`가 양수로 설정되어 있으면 그 값을 쓴다.
2. 설정값이 `0`이면 socket type별 기본값을 쓴다.
3. 음수 설정은 실패하며 기존 값은 바꾸지 않는다.

이 옵션은 `GroupSlots` 계산에 직접 영향을 준다. 반면 `SNDBUF` / `RCVBUF`는
buffer 예산과 planning connection 수를 기준으로 계산하므로, message unit이
transport buffer 크기를 직접 정하지 않는다.

## 5. buffer 예산 정책

buffer 예산은 `MsgUnit(B)`와 독립적으로 계산한다. `MsgUnit(B)`는 HWM queue를
메시지 슬롯으로 환산하는 값이고, `SNDBUF` / `RCVBUF`는 OS 또는 transport 계층의
byte buffer 설정이기 때문이다.

기본 원칙은 아래와 같다.

```text
1. 자동 관리 buffer 비용만 context budget에서 먼저 차감한다.
2. 사용자가 SNDBUF / RCVBUF를 직접 설정했으면 그 값은 수동 override로 적용한다.
3. 수동 buffer 값은 auto-HWM queue budget에서 차감하지 않고 별도로 표시한다.
4. 사용자가 설정하지 않은 buffer는 socket/transport별 고정 기본값을 쓴다.
5. 남은 예산으로 HWM queue budget을 계산한다.
6. MsgUnit(B)는 queue budget을 GroupSlots로 바꿀 때만 쓴다.
```

즉 auto-HWM 예산 계산은 아래 순서로 진행한다.

```text
context_budget
-> runtime_reserve
-> auto_buffer_bytes
-> manual_buffer_bytes
-> queue_budget
-> role group budget
-> MsgUnit(B)
-> GroupSlots
-> HWM
```

계산식은 아래와 같다.

```text
runtime_reserve = context_budget * reserve_ratio
auto_buffer_bytes = sum((auto_sndbuf + auto_rcvbuf)
                        * planned_buffer_connections)
manual_buffer_bytes = sum((manual_sndbuf + manual_rcvbuf)
                          * planned_buffer_connections)
queue_budget = clamp_queue_budget(context_budget
                                  - runtime_reserve
                                  - auto_buffer_bytes)
```

`auto_sndbuf` / `auto_rcvbuf`와 `manual_sndbuf` / `manual_rcvbuf`는 아래 순서로
정한다.

1. 사용자가 `ZLINK_OPT_SNDBUF` / `ZLINK_OPT_RCVBUF`를 직접 설정했으면 그 값을
   수동 buffer로 적용한다.
2. 직접 설정하지 않았으면 고정 기본값을 자동 관리 buffer로 쓴다.
3. inproc처럼 OS socket buffer 의미가 약한 transport에서는 자동 buffer 비용을 `0`으로
   둘 수 있다.

`planned_buffer_connections`는 buffer 비용을 몇 개 transport connection에 대해
계획할지 나타낸다.

```text
planned_buffer_connections =
  max(active_transport_connections, planned_transport_connections, 1)
```

SNDBUF/RCVBUF는 일반적으로 transport connection마다 적용되는 OS buffer 설정이다.
따라서 ROUTER/STREAM처럼 한 소켓이 여러 transport connection을 갖는 경우에는
기본 buffer라도 connection 수만큼 budget에서 차감해야 한다. inproc 또는 내부
메모리 경로처럼 OS buffer 의미가 약한 경로는 `planned_buffer_connections`를
계산하더라도 buffer 비용을 `0`으로 둘 수 있다.

초기 기본값은 아래로 둔다.

| 대상 | 기본 `SNDBUF` | 기본 `RCVBUF` |
|------|---------------|---------------|
| TCP/TLS/WS/WSS 일반 socket | `262144` | `262144` |
| STREAM socket | `262144` | `262144` |
| inproc 또는 내부 in-memory 경로 | `0` 또는 미설정 | `0` 또는 미설정 |

`262144`는 256KiB이다. 이 값은 현재 stream 문서의 호환 기본값과도 맞고,
수십 MiB 단위 buffer가 자동으로 설정되는 문제를 피한다. STREAM을 더 크게 잡을
필요가 있는지는 perf 결과로 따로 검증한다.

예를 들어 context budget이 128MiB이고, runtime reserve가 10%이며, 자동 buffer
기본값이 양방향 256KiB이고, 계획 transport connection이 1개라면 아래처럼
계산한다.

```text
Context(B) = 134217728
Runtime(B) = 13421772
AutoBuffer(B) = (262144 + 262144) * 1 = 524288
ManualBuffer(B) = 0
Queue(B) = 134217728 - 13421772 - 524288 = 120381668
```

계획 transport connection이 100개라면 buffer 비용은 아래처럼 커진다.

```text
AutoBuffer(B) = (262144 + 262144) * 100 = 52428800
ManualBuffer(B) = 0
Queue(B) = 134217728 - 13421772 - 52428800 = 68377156
```

이렇게 계산해야 client 수가 늘어날 때 OS buffer 계획 비용이 HWM queue 예산에
반영된다.

사용자가 buffer를 직접 설정한 경우에는 그 값을 수동 override로 적용한다.

```c
int sndbuf = 1048576;
int rcvbuf = 1048576;
zlink_set_option(socket, ZLINK_OPT_SNDBUF, &sndbuf, sizeof(sndbuf));
zlink_set_option(socket, ZLINK_OPT_RCVBUF, &rcvbuf, sizeof(rcvbuf));
```

이 경우 해당 소켓의 planned buffer 비용은 아래와 같다.

```text
AutoBuffer(B) = 0
ManualBuffer(B) = (1048576 + 1048576) * planned_buffer_connections
Queue(B) = Context(B) - Runtime(B) - AutoBuffer(B)
```

따라서 perf와 monitor 출력은 자동 관리 buffer와 수동 buffer를 나누어 보여야 한다.

```text
AutoBuffer(B) = auto-HWM이 관리하는 buffer 비용
ManualBuffer(B) = 사용자가 직접 설정한 buffer 비용
Queue(B) = Context(B) - Runtime(B) - AutoBuffer(B)
```

사용자가 매우 큰 `SNDBUF` / `RCVBUF`를 직접 설정해도 그 값은 auto-HWM queue
budget에서 차감하지 않는다. 수동 buffer 설정은 사용자가 특정 socket의 OS buffer를
직접 책임지겠다는 뜻이므로, auto-HWM은 그 값을 임의로 줄이지 않고 queue budget도
예상 밖으로 줄이지 않는다.

이 말은 실제 프로세스 전체 메모리가 context budget을 절대 넘지 않는다는 뜻이
아니다. context budget은 auto-HWM이 자동으로 관리하는 queue와 자동 buffer의 계획
예산이다. 수동 buffer는 사용자가 명시적으로 budget 밖에서 책임지는 override로
본다.

수동 여부는 방향별로 독립적으로 판단한다. 예를 들어 사용자가 `SNDBUF`만 직접
설정하고 `RCVBUF`는 설정하지 않았다면 `SNDBUF` 비용은 `ManualBuffer(B)`로
표시하고, `RCVBUF` 기본값 비용은 `AutoBuffer(B)`로 계산한다.

`clamp_queue_budget()`은 아래 원칙을 따른다.

```text
remaining = context_budget - runtime_reserve - auto_buffer_bytes
if remaining <= 0:
  queue_budget = 0
else:
  queue_budget = remaining
```

초기 구현에서는 `minimum_queue_budget`을 강제로 더하지 않는다. 자동 buffer가 예산을
소진했는데도 최소 queue budget을 더하면 context budget 설명이 깨지기 때문이다.
queue budget이 0이면 자동 HWM은 최소 진행값만 적용하거나 수동 HWM이 있으면 그
값을 유지한다.

## 6. client/server와 service 내부 소켓

message unit은 context 옵션이 아니라 소켓 옵션이어야 한다.

이유는 아래와 같다.

- client socket과 server socket이 같은 패턴에서도 다른 메시지 크기를 가질 수 있다.
- request/reply echo 패턴은 요청 크기와 응답 크기가 같을 수도 있지만, 실제
  서비스에서는 다를 수 있다.
- SPOT 계열은 spotnode 공유 소켓과 spot endpoint 소켓의 성격이 다르다.
- stream socket은 일반 메시지 socket과 다른 기본 단위를 쓰는 편이 낫다.

따라서 perf와 서비스 내부 구현은 필요한 경우 각 소켓 생성 직후 이 옵션을
개별로 설정한다.

단, 이 옵션은 SPOT의 per-spot 예산 분리 문제를 대신 해결하지 않는다.
message unit은 같은 budget을 몇 개의 메시지 슬롯으로 볼지만 정한다. spotnode
공유 budget과 per-spot budget을 나누는 정책은 별도 계산 계층에서 처리해야 한다.

## 7. SPOT shared/per-spot 예산 분리

SPOT 계열은 message unit 적용과 별개로 HWM 예산 scope를 나누어 계산해야 한다.
같은 role budget을 쓰더라도 spotnode 공유 소켓과 spot endpoint 소켓은 의미가
다르기 때문이다.

SPOT 내부 큐는 아래 두 종류로 나눈다.

```text
shared queue
-> spotnode 내부 공유 소켓
-> 여러 spot, peer, route target을 한 소켓이 처리

per-spot queue
-> 각 spot endpoint 또는 attachment 소켓
-> 소켓 자체가 이미 개별 spot 단위
```

### 7.1 공통 입력

두 scope는 같은 context/role/message unit 입력에서 시작한다.

```text
domain_queue_budget = context_budget - runtime_reserve - auto_buffer_bytes
role_budget = domain_queue_budget * role_weight
msg_unit = socket override or socket type default
role_slots = role_budget / msg_unit
```

`domain_queue_budget`은 해당 scope의 자동 관리 buffer 비용을 뺀 뒤의 예산이다.
SPOT shared scope와 per-spot scope는 같은 context를 쓰더라도 buffer 비용과
scope count가 다를 수 있으므로 각각 따로 계산한다.
SPOT 표에서 `RoleGroup(B)`는 role 전체 budget이고, `ScopeGroup(B)`는 shared 또는
per-spot 계산에 실제로 사용한 budget이다.

### 7.2 shared scope

spotnode 공유 소켓은 전체 role budget을 기준으로 계산하되, 실제로 fanout 또는
ingress 대상이 되는 logical target 수로 나눈다.

```text
targets = max(active_targets, planned_targets, 1)
scope_group_budget = role_budget
scope_group_slots = scope_group_budget / msg_unit
target_slots = scope_group_slots / targets
hwm = clamp_floor_to_budget(role_floor, target_slots)
```

target 수는 소켓 역할에 따라 다르게 잡는다.

| Role | target 기준 |
|------|-------------|
| `fanout` | `local_sub_count + active_peer_count` |
| `recv_ingress` | `local_pub_count + active_peer_count` |
| `routed` | `active_peer_count` |
| `control` | `active_peer_count` |

planned target은 아직 연결이 충분히 맺히기 전 과대 HWM을 피하기 위한 값이다.
perf에서는 client 수 또는 service client 수를 planned target으로 사용한다.
일반 runtime에서는 현재 runtime snapshot과 설정 가능한 planned count가 있으면
그 값을 함께 사용한다. planned count를 알 수 없으면 최소값 `1`을 사용하되,
monitor 출력에 `Reason`으로 그 사실을 드러낸다.

### 7.3 per-spot scope

per-spot 소켓은 소켓 자체가 이미 개별 spot 단위이므로, role budget을 먼저 전체
spot 수로 나눈다.

```text
spot_count = max(planned_spot_count, current_spot_count, 1)
scope_group_budget = role_budget / spot_count
per_spot_slots = scope_group_budget / msg_unit
hwm = clamp_floor_to_budget(role_floor, per_spot_slots)
```

per-spot scope에서는 기본적으로 `active_connections`로 한 번 더 나누지 않는다.
이미 `spot_count`로 전체 role budget을 나누었기 때문이다. 단, per-spot 소켓
하나가 실제로 여러 pipe를 직접 가진다는 것이 확인되는 경우에만 그 pipe 수로
추가 분할할 수 있다.

### 7.4 floor 처리

role floor는 예산을 초과하면서까지 강제로 보장하지 않는다. floor가 예산보다
크면 context memory limit 의미가 깨질 수 있기 때문이다.

```text
clamp_floor_to_budget(floor, slots):
  if slots <= 0: return 1
  if slots < floor: return max(1, slots)
  return max(floor, slots)
```

예를 들어 128MiB context, fanout role, `MsgUnit(B)=4096`이면 아래처럼 계산된다.

```text
Queue(B) = 80530636
fanout RoleGroup(B) = 40265318
role_slots = RoleGroup(B) / 4096 = 9830
```

spot이 100개이면:

```text
ScopeGroup(B) = 40265318 / 100 = 402653
per_spot_slots = 402653 / 4096 = 98
per_spot_hwm = 98
```

spot이 1000개이면:

```text
ScopeGroup(B) = 40265318 / 1000 = 40265
per_spot_slots = 40265 / 4096 = 9
role_floor = 16
per_spot_hwm = 9
```

두 번째 경우에는 floor보다 작은 값이 나오지만, 예산을 지키기 위해 `9`를 그대로
사용한다.

### 7.5 HWM 축소와 pending queue

auto-HWM 재계산으로 새 HWM이 현재 HWM보다 작아질 수 있다. 이때 이미 pending
message가 새 HWM보다 많이 쌓여 있다면 즉시 낮추는 방식은 불필요한 backpressure
스파이크를 만들 수 있다.

따라서 자동 HWM 축소는 아래 원칙을 따른다.

```text
if new_hwm >= current_hwm:
  apply immediately
else if pending_messages <= new_hwm:
  apply immediately
else:
  defer shrink until pending_messages <= new_hwm
```

초기 구현에서 pending message를 정확히 알기 어렵다면, HWM 증가와 신규 socket
생성 시 적용은 즉시 하고, HWM 감소는 다음 안전한 refresh 시점으로 미룬다. 이
정책은 throughput을 위해 무조건 큰 HWM을 유지하자는 뜻이 아니라, 재계산 순간에
이미 쌓인 메시지 때문에 불필요한 흔들림이 생기지 않게 하려는 것이다.

deferred shrink는 영구히 미뤄지면 안 된다. 구현은 아래 시점 중 하나 이상에서
감소 적용을 다시 시도해야 한다.

- send/recv drain 뒤 pending message가 줄었을 때
- monitor snapshot 또는 socket refresh가 실행될 때
- auto-HWM periodic refresh가 실행될 때
- peer disconnect 또는 pipe detach로 pending queue가 줄었을 때

위 시점에서도 pending 값을 알 수 없으면, 감소 적용을 바로 강제하지 않고 다음
refresh로 넘기되 monitor snapshot의 reason에 deferred 상태를 드러낸다.

## 8. 수동 HWM과의 관계

사용자가 `ZLINK_OPT_SNDHWM` 또는 `ZLINK_OPT_RCVHWM`을 직접 설정하면 그 HWM 값이
우선한다. 이때 message unit은 auto-HWM의 HWM 계산에는 영향을 주지 않는다.

다만 `SNDBUF` / `RCVBUF` 자동 계산과 monitor snapshot 설명에는 여전히 auto-HWM
계산 입력으로 남을 수 있다. 구현에서는 수동 HWM 상태에서도 snapshot이 혼동을
주지 않도록 `auto_hwm_effective_message_bytes`에 실제 계산 입력값을 유지한다.

`zlink_get_option(..., ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES, ...)`는 사용자가 설정한
override 값을 반환한다. 값이 `0`이면 socket type별 기본값을 사용한다는 뜻이다.
실제 적용된 message unit은 monitor snapshot의
`auto_hwm_effective_message_bytes`로 확인한다.

## 9. monitoring과 perf 출력

monitor snapshot의 `auto_hwm_effective_message_bytes`는 실제 적용된
`MsgUnit(B)`를 보여야 한다.

perf 출력에서는 아래 항목을 반드시 확인할 수 있어야 한다.

- `Context(B)`
- `Queue(B)`
- `AutoBuffer(B)`
- `ManualBuffer(B)`
- `Runtime(B)`
- `MsgUnit(B)`
- `RoleGroup(B)`
- `ScopeGroup(B)`
- `GroupSlots`
- `Managed`
- `Active`
- `Scope`
- `ScopeCount`
- `SNDHWM`
- `RCVHWM`

SPOT 계열 출력은 spotnode와 spot을 분리한다.

```text
Auto-HWM common
Auto-HWM spotnode
Auto-HWM spot
Auto-HWM budget
```

일반 패턴은 기존 endpoint/socket 중심 표를 유지하되 `MsgUnit(B)`를 budget 표에
포함한다.

SPOT 계열 표에는 shared/per-spot 계산 차이를 검증할 수 있도록 `Scope`와
`ScopeCount`를 포함한다.

```text
Auto-HWM spotnode:
  | Socket   | Scope  | Role   | ScopeCount | MsgUnit(B) |
  |----------|--------|--------|------------|------------|
  | data_pub | shared | fanout | 100        | 4096       |

  | RoleGroup(B) | ScopeGroup(B) | GroupSlots | SNDHWM |
  |--------------|---------------|------------|--------|
  | 40265318     | 40265318      | 9830       | 98     |

Auto-HWM spot:
  | Socket   | Scope    | Role   | ScopeCount | MsgUnit(B) |
  |----------|----------|--------|------------|------------|
  | data_pub | per-spot | fanout | 100        | 4096       |

  | RoleGroup(B) | ScopeGroup(B) | GroupSlots | SNDHWM |
  |--------------|---------------|------------|--------|
  | 40265318     | 402653        | 98         | 98     |
```

## 10. bindings/c/perf 적용 방향

`bindings/c/perf`는 메시지 크기를 알고 있으므로, 벤치마크 실행 중인
`msg_size`를 소켓별 message unit으로 전달한다.

기본 적용 원칙은 아래와 같다.

- runner는 size별 실행 전에 현재 `msg_size`를 환경 변수로 전달한다.
- C perf helper는 socket 생성 직후 `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`를 설정한다.
- 설정값은 `max(msg_size + overhead, socket_type_default)` 형태로 잡는다.
- stream은 최소 `1024`, 그 외 socket은 최소 `4096`을 사용한다.

초기 구현에서는 overhead를 별도 상수로 크게 잡지 않고, 아래 계산을 우선 사용한다.

```text
STREAM:     max(msg_size, 1024)
non-STREAM: max(msg_size, 4096)
```

이렇게 하면 작은 메시지에서는 과도하게 큰 HWM을 피하고, 큰 메시지에서는
context 예산에 맞춰 HWM이 줄어든다.
다만 작은 메시지 성능에 미치는 영향은 반드시 perf로 검증한다. 64B처럼 작은
메시지에서 HWM이 지나치게 작아지면 overhead 또는 최소 message unit 정책을 다시
조정한다.

기본 `MsgUnit(B)`는 평균 메시지 크기 가정이므로, 성능 목표를 맞추기 위해 먼저
낮추는 값이 아니다. perf 결과에서 HWM이 목표 동시성에 비해 작다면 context
memory budget을 늘리는 것이 우선이다.

## 11. 권장 context memory budget 산정

권장 context memory budget은 고정 상수로 정하지 않고 perf sweep으로 찾는다.
message unit 기본값은 평균 메시지 크기 가정이므로 유지하고, 튜닝 대상은
`ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB`로 둔다.

고정 기준은 아래와 같다.

```text
non-STREAM MsgUnit(B) = 4096
STREAM MsgUnit(B) = 1024
AutoBuffer default = 262144 + 262144 per planned connection
ManualBuffer(B) = auto-HWM budget 밖의 사용자 override
```

권장값을 찾기 위한 기본 sweep은 아래처럼 둔다.

| 축 | 값 |
|----|----|
| context memory | `128`, `256`, `512`, `1024` MiB |
| clients | `100`, `1000`, 필요하면 `5000` |
| msg sizes | `64`, `1024`, `4096`, `65536` |
| patterns | `DEALER_ROUTER`, `PUBSUB`, `SPOT`, `SPOT_REQREP`, `SPOT_SENDSEND`, `STREAM` |

판정 기준은 throughput 하나만 보지 않는다. 아래 조건을 함께 확인한다.

1. FAIL이 없어야 한다.
2. 64B에서 목표 처리량을 만족해야 한다.
3. p95/p99 latency가 비정상적으로 튀지 않아야 한다.
4. 계산된 HWM이 목표 동시성에 비해 지나치게 작지 않아야 한다.
5. `AutoBuffer(B)`, `Queue(B)`, `RoleGroup(B)`, `ScopeGroup(B)`,
   `GroupSlots`가 계산식과 맞아야 한다.

권장값은 하나의 숫자로만 정하지 않고 deployment tier로 문서화한다.

| Tier | 예시 기준 |
|------|-----------|
| small | `clients <= 100`, 작은 payload 중심 |
| medium | `clients <= 1000`, 일반 서버 간 메시지 |
| large | `clients >= 1000` 또는 큰 payload 비중이 높음 |

계산된 HWM이 너무 작으면 먼저 context memory budget을 늘린다.
`ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`를 낮추는 것은 실제 평균 메시지 크기가 기본
가정보다 작다는 근거가 있을 때만 한다.

## 12. 구현 계획

### 12.1 core API와 enum

1. `core/include/zlink_enum.h`의 `zlink_option_t`에
   `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`를 추가한다.
2. generated 또는 복사본 성격의 binding include 파일도 같은 값을 유지한다.
3. `core/include/zlink.h` 주석과 정식 spec 반영 시 옵션 의미를 설명한다.

### 12.2 socket option 처리

1. 공통 socket option 테이블에 새 옵션을 추가한다.
2. `set_option`은 `int` 타입만 허용한다.
3. 값이 음수이면 `EINVAL`을 반환한다.
4. 값이 바뀌면 auto-HWM policy를 다시 계산한다.
5. `get_option`은 사용자가 설정한 raw 값 또는 effective 값을 혼동하지 않도록
   계약을 명확히 정한다. 이 초안에서는 `get_option`이 raw 설정값을 반환한다.
   실제 적용값은 monitor snapshot의 `auto_hwm_effective_message_bytes`로 확인한다.

### 12.3 auto-HWM 계산

1. 내부 고정 message unit 상수를 제거하거나 fallback 기본값으로만 남긴다.
2. `auto_hwm_socket_plan_for_role()`이 socket type과 socket별 message unit
   override를 받아 계산하도록 바꾼다.
3. override가 `0`이면 socket type별 기본값을 쓴다.
4. `auto_hwm_context_plan_t`의 `effective_message_bytes`가 context 고정값처럼
   보이지 않도록 구조를 정리한다.
5. `auto_hwm_socket_plan_t`에 최종 적용된 `effective_message_bytes`를 유지한다.
6. 자동 buffer 예산은 고정 비율이 아니라 planned `SNDBUF` / `RCVBUF` 비용을 기준으로
   계산한다.
7. 수동 `SNDBUF` / `RCVBUF`는 auto-HWM queue budget에서 차감하지 않고 별도
   진단값으로 표시한다.
8. queue 예산은 context budget에서 runtime reserve와 자동 buffer 비용을 뺀
   나머지로 계산한다.
9. planned 자동 buffer 비용은 transport connection 계획 수를 곱해서 계산한다.
10. HWM 감소 재계산은 pending queue가 새 HWM 이하로 내려간 뒤 적용하거나 안전한
   refresh 시점으로 미룬다.

### 12.4 SPOT 내부 소켓

1. SPOT 내부 raw socket 생성 후 auto-HWM helper 적용 전에 message unit을 정한다.
2. spotnode 공유 소켓과 spot endpoint 소켓을 구분해서 설정할 수 있게 한다.
3. SPOT auto-HWM helper에 scope를 추가한다.
4. spotnode 공유 소켓은 shared scope 공식으로 계산한다.
5. spot endpoint와 attachment 소켓은 per-spot scope 공식으로 계산한다.
6. floor는 `clamp_floor_to_budget()` 원칙을 따른다.
7. perf에서는 spotnode/spot 모두 현재 message size를 기준으로 설정한다.
8. 일반 runtime에서는 사용자가 별도 설정하지 않으면 socket type별 기본값을 쓴다.

### 12.5 bindings/c/perf

1. runner가 size별 실행 환경에 현재 메시지 크기를 넣는다.
2. C perf helper가 socket 생성 직후 공통 옵션으로 message unit을 설정한다.
3. SPOT perf helper도 spotnode와 spot handle에 설정을 반영한다.
4. 출력 표에서 `MsgUnit(B)`와 `GroupSlots`가 size별로 달라지는지 확인한다.
5. SPOT 출력 표에서 `Scope`와 `ScopeCount`가 shared/per-spot 계산 기준을
   보여주는지 확인한다.
6. `AutoBuffer(B)`가 context 고정 비율이 아니라 실제 planned 자동 buffer 비용으로
   출력되는지 확인한다.
7. `BufferConnections` 또는 같은 의미의 필드가 출력되어 buffer 비용이 몇 개
   transport connection 기준인지 확인할 수 있게 한다.
8. `ManualBuffer(B)`가 수동 `SNDBUF` / `RCVBUF` 설정 비용을 별도로 보여주는지
   확인한다.
9. context memory sweep을 실행해서 패턴별 권장 budget tier를 산정한다.

### 12.6 문서

1. `doc/spec/core/socket/README.ko.md`와 영문 문서에 공통 소켓 옵션을 추가한다.
2. `doc/guide/12-socket-options.ko.md`에 사용자가 언제 조정해야 하는지 설명한다.
3. `doc/spec/core/monitoring.ko.md`와 영문 문서에
   `auto_hwm_effective_message_bytes`가 실제 `MsgUnit(B)`임을 명확히 한다.
4. `doc/internals/socket-option-defaults.ko.md`와 영문 문서에 기본값을 반영한다.
5. `doc/internals/spot-internals.ko.md`와 영문 문서에 SPOT 내부 적용 경계를
   설명한다.
6. `bindings/c/perf/README.md`에 size별 message unit 적용과 출력 해석을 추가한다.
7. auto-HWM 문서에서 `AutoBuffer(B)`가 고정 비율 예산이 아니라 planned 자동 buffer
   비용임을 설명한다.
8. 수동 buffer는 auto-HWM queue budget에서 차감하지 않고 별도 표시한다는 점을
   설명한다.
9. perf sweep 결과로 찾은 권장 context memory budget tier를 문서화한다.

### 12.7 bindings

1. `bindings/go/include/zlink_enum.h`는 core enum과 같은 값을 갖게 한다.
2. C 바인딩은 core header를 기준으로 빌드되므로 별도 enum 복사본이 있는지 확인한다.
3. `doc/spec/bindings/README.md`의 공통 socket option 목록에 새 옵션을 추가한다.
4. `doc/spec/bindings/cpp/README.md`, `doc/spec/bindings/python/README.md`,
   `doc/spec/bindings/java/README.md`에서 공통 옵션 노출 규칙을 확인하고 필요한
   항목을 추가한다.

## 13. 회귀 테스트 계획

### 13.1 core unit/integration

1. 기본값 테스트
   - STREAM socket의 effective message unit이 `1024`인지 확인한다.
   - non-STREAM socket의 effective message unit이 `4096`인지 확인한다.
2. set/get 테스트
   - `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES=8192` 설정 후 get이 `8192`를 반환하는지
     확인한다.
   - `0` 설정 시 type별 기본 effective 값으로 돌아가는지 monitor snapshot으로
     확인한다.
   - 음수 설정이 `EINVAL`로 실패하고 기존 값이 유지되는지 확인한다.
3. 재계산 테스트
   - message unit 변경 후 `SNDHWM` / `RCVHWM`이 다시 계산되는지 확인한다.
   - 수동 HWM 설정 후에는 message unit 변경이 수동 HWM을 덮어쓰지 않는지
     확인한다.
4. monitoring 테스트
   - snapshot의 `auto_hwm_effective_message_bytes`가 실제 적용값을 담는지
     확인한다.
   - `GroupSlots = ScopeGroup(B) / MsgUnit(B)` 관계가 성립하는지 확인한다.
   - `AutoBuffer(B)`가 기본 `SNDBUF` / `RCVBUF` 기준으로 계산되는지 확인한다.
   - `Queue(B) = Context(B) - Runtime(B) - AutoBuffer(B)` 관계가 성립하는지 확인한다.
   - `AutoBuffer(B)`가 `(SNDBUF + RCVBUF) * planned_buffer_connections` 관계를
     만족하는지 확인한다.
5. buffer 설정 테스트
   - 수동 `SNDBUF` / `RCVBUF` 설정이 `ManualBuffer(B)` 계산에 반영되는지 확인한다.
   - 수동 buffer 설정 후 HWM queue budget이 줄어들지 않는지 확인한다.
   - buffer 미설정 시 256KiB 기본값이 적용되는지 확인한다.
6. HWM 감소 테스트
   - 새 계산값이 현재 HWM보다 작고 pending message가 더 많으면 즉시 낮추지 않는지
     확인한다.
   - pending message가 새 HWM 이하로 내려간 뒤 감소값이 적용되는지 확인한다.

### 13.2 SPOT 테스트

1. spotnode 공유 소켓과 spot endpoint 소켓의 snapshot에서 `MsgUnit(B)`가 확인되는지
   테스트한다.
2. per-spot 소켓에 큰 message unit을 설정했을 때 HWM이 줄어드는지 확인한다.
3. 기존 spot request/reply, send/send, pub/sub 테스트가 통과하는지 확인한다.
4. spot 수를 늘렸을 때 `ScopeGroup(B)=RoleGroup(B)/spot_count` 기준으로 per-spot
   HWM이 줄어드는지 확인한다.
5. floor보다 작은 per-spot budget이 나오면 floor를 강제하지 않고 예산 기준 값이
   적용되는지 확인한다.

### 13.3 bindings/c/perf smoke

AGENTS.md의 Benchmark Build Rules를 따른다.

1. `core/build` runtime을 먼저 다시 빌드한다.
2. runner가 출력하는 `libzlink.so` 경로가 `core/build`인지 확인한다.
3. stale runtime 검사를 통과한 뒤 아래 smoke를 실행한다.

```bash
cd bindings/c/perf
./run_benchmarks_multi.sh --pattern dealer_router --msg-sizes 64,1024,65536 --duration 1 --clients 2
./run_benchmarks_multi.sh --pattern spot_reqrep --msg-sizes 64,1024,65536 --duration 1 --clients 2
./run_benchmarks_multi.sh --pattern stream --msg-sizes 64,1024,65536 --duration 1 --clients 2
```

확인할 항목은 아래와 같다.

- size별 `MsgUnit(B)`가 달라진다.
- size가 커질수록 `GroupSlots`가 줄어든다.
- fail 없이 결과가 출력된다.
- SPOT 표는 spotnode와 spot을 나누어 출력한다.
- SPOT 표의 `ScopeCount`가 shared/per-spot 계산 기준을 설명한다.
- `AutoBuffer(B)`는 고정 비율이 아니라 planned 자동 buffer 비용으로 출력된다.
- `ManualBuffer(B)`는 수동 buffer 설정이 있을 때 별도로 출력된다.
- buffer connection 계획 수가 출력되거나 로그에서 확인된다.
- context memory sweep 결과로 권장 budget tier를 산정할 수 있다.

## 14. 완료 기준

이 설계는 아래 조건을 모두 만족해야 완료로 본다.

1. 새 공통 소켓 옵션이 core enum, core option 처리, binding include에 반영된다.
2. auto-HWM 계산에서 고정 `1280` 대신 socket별 message unit을 사용한다.
3. STREAM 기본값은 `1024`, non-STREAM 기본값은 `4096`이다.
4. perf에서는 메시지 size별로 message unit이 적용된다.
5. SPOT shared/per-spot scope가 서로 다른 공식으로 계산된다.
6. per-spot floor는 context 예산을 초과해 강제되지 않는다.
7. 자동 buffer 예산은 planned `SNDBUF` / `RCVBUF` 비용으로 계산되고, queue 예산은
   그 나머지로 계산된다.
8. 수동 buffer 예산은 queue 예산에서 차감하지 않고 별도 진단값으로 표시된다.
9. planned 자동 buffer 비용은 transport connection 계획 수를 곱해 계산된다.
10. HWM 감소는 pending queue가 새 HWM보다 큰 상태에서 즉시 강제되지 않는다.
11. monitor snapshot과 perf 표에서 실제 `MsgUnit(B)`, `Scope`, `ScopeCount`가
   확인된다.
12. perf 표의 `AutoBuffer(B)`는 context 고정 비율이 아니라 planned 자동 buffer
   비용으로 출력된다.
13. perf sweep으로 권장 context memory budget tier를 산정하고 문서화한다.
14. core/build 전체 빌드가 성공한다.
15. 관련 회귀 테스트가 통과한다.
16. bindings/c/perf smoke가 stale runtime 없이 통과한다.
17. 정식 spec, guide, internals, bindings 문서가 업데이트된다.
18. 미반영 항목이 0개임을 코드와 문서 양쪽에서 재검토한다.

## 15. 무인 구현 진행 규칙

이 문서를 기준으로 개발을 진행할 때는 아래 규칙을 따른다. 이 섹션은 구현 중
추가 확인 대기 없이 합리적인 기본값으로 끝까지 진행하기 위한 기준이다.

### 15.1 고정 결정값

아래 값은 구현 중 다시 질문하지 않고 그대로 적용한다.

| 항목 | 값 |
|------|----|
| 새 옵션 이름 | `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` |
| 새 옵션 enum 값 | `0x3034` |
| 옵션 타입 | `int` |
| 옵션 단위 | byte |
| 옵션 기본 raw 값 | `0` |
| STREAM effective 기본값 | `1024` |
| non-STREAM effective 기본값 | `4096` |
| 자동 `SNDBUF` 기본값 | `262144` |
| 자동 `RCVBUF` 기본값 | `262144` |
| inproc/internal buffer 비용 | `0` |
| runtime reserve | context budget의 `10%` |
| manual buffer | auto-HWM queue budget에서 차감하지 않음 |
| manual HWM | auto-HWM 적용값보다 우선 |
| SPOT scope | `shared`, `per-spot` |

### 15.2 public enum과 monitor 필드

아래 public enum 값과 monitor field 이름은 구현 중 다시 질문하지 않고 그대로 쓴다.

새 recalc reason은 아래 값을 사용한다.

```c
ZLINK_AUTO_HWM_RECALC_REASON_DEFERRED_SHRINK = 5
```

monitor snapshot에는 아래 field를 추가한다.

| Field | 타입 | 의미 |
|-------|------|------|
| `auto_hwm_scope` | `uint32_t` | `0=none`, `1=shared`, `2=per_spot` |
| `auto_hwm_scope_count` | `uint32_t` | HWM을 나눈 scope target 수 |
| `auto_hwm_role_group_budget_bytes` | `uint64_t` | role 전체 budget |
| `auto_hwm_scope_group_budget_bytes` | `uint64_t` | scope 계산에 실제 사용한 budget |
| `auto_hwm_auto_buffer_bytes` | `uint64_t` | auto-managed buffer budget |
| `auto_hwm_manual_buffer_bytes` | `uint64_t` | user-managed buffer diagnostic |
| `auto_hwm_buffer_connections` | `uint32_t` | buffer 비용을 곱한 계획 connection 수 |
| `auto_hwm_deferred_sndhwm` | `int32_t` | 지연 중인 SNDHWM, 없으면 `-1` |
| `auto_hwm_deferred_rcvhwm` | `int32_t` | 지연 중인 RCVHWM, 없으면 `-1` |

기존 `auto_hwm_group_budget_bytes`는 호환을 위해 유지한다. 새 구현에서는
`auto_hwm_scope_group_budget_bytes`와 같은 값을 넣는다. 새 문서와 perf 출력은
`RoleGroup(B)`와 `ScopeGroup(B)`를 우선 사용한다.

내부 scope enum은 아래 값을 사용한다.

```text
auto_hwm_scope_none = 0
auto_hwm_scope_shared = 1
auto_hwm_scope_per_spot = 2
```

### 15.3 planned count fallback 규칙

planned count를 알 수 없을 때는 아래 fallback을 사용한다.

| 대상 | fallback |
|------|----------|
| 일반 core socket | `max(attached_pipe_count, 1)` |
| STREAM socket | `max(attached_pipe_count, 5000)` |
| perf 일반 multi | `PERF_MULTI_CLIENTS` |
| perf service/spot | `PERF_MULTI_SERVICE_CLIENTS`가 있으면 사용, 없으면 `PERF_MULTI_CLIENTS` |
| SPOT shared fanout | `max(local_sub_count + active_peer_count, planned_count, 1)` |
| SPOT shared recv | `max(local_pub_count + active_peer_count, planned_count, 1)` |
| SPOT shared routed/control | `max(active_peer_count, planned_count, 1)` |
| SPOT per-spot | `max(current_spot_count, planned_spot_count, 1)` |

`planned_count`가 fallback `1`로만 결정되면 monitor reason 또는 detail 출력에
`fallback_planned_count`를 드러낸다.

### 15.4 구현 순서

구현은 아래 순서로 진행한다. 앞 단계가 빌드와 관련 테스트를 통과하기 전에는
다음 단계로 넘어가지 않는다.

1. core enum과 공통 socket option 처리
2. auto-HWM message unit 계산 반영
3. auto/manual buffer accounting 반영
4. monitoring snapshot 필드와 reason 보강
5. SPOT shared/per-spot scope 계산 반영
6. bindings include와 bindings 문서 반영
7. bindings/c/perf size별 message unit 적용과 표 출력 반영
8. core/socket/SPOT 회귀 테스트 추가 또는 수정
9. 정식 spec, guide, internals, perf README 반영
10. perf smoke와 context memory sweep 실행
11. 문서와 코드의 미반영 항목 재검토

### 15.5 단계별 검증 게이트

각 단계는 최소 아래 검증을 통과해야 완료로 본다.

| 단계 | 필수 검증 |
|------|-----------|
| core enum/option | option set/get, 음수 `EINVAL`, raw `0` 반환 |
| auto-HWM 계산 | STREAM `1024`, non-STREAM `4096`, size override 반영 |
| buffer accounting | `AutoBuffer(B)`, `ManualBuffer(B)`, `Queue(B)` 공식 검증 |
| SPOT scope | shared/per-spot HWM 계산이 서로 다른 scope 공식 사용 |
| monitoring | `MsgUnit(B)`, `AutoBuffer(B)`, `ManualBuffer(B)`, `Scope`, `ScopeCount` 확인 |
| bindings/perf | size별 `MsgUnit(B)`와 `GroupSlots` 변화 확인 |
| 문서 | draft와 정식 문서 사이 미반영 항목 0개 |

### 15.6 실패 시 처리 규칙

실패가 나오면 같은 단계 안에서 원인을 찾아 수정하고 재검증한다.

- build 실패: 실패한 target과 직접 관련된 compile error부터 수정한다.
- core test 실패: 계약 또는 계산식 불일치인지 먼저 확인한다.
- SPOT test 실패: scope count, per-spot budget, deferred shrink 적용 순서를 먼저
  확인한다.
- perf fail: stale runtime, 출력된 `libzlink.so` 경로, HWM/Buffer 표 계산값,
  backpressure failure 순서로 확인한다.
- perf 성능 저하: `MsgUnit(B)` 기본값을 먼저 낮추지 않는다. context memory sweep
  결과로 권장 memory tier를 찾는다.

임시 우회나 TODO를 남기지 않는다. 단, 구현 범위 밖으로 확인된 일반 socket
context-wide global domain은 이 문서의 초기 구현 범위가 아니므로 별도 후속
문서로 남길 수 있다.

### 15.7 perf 실행 규칙

`bindings/c/perf`는 AGENTS.md의 Benchmark Build Rules를 따른다.

1. `core/src` 또는 `core/include`를 바꾼 뒤에는 반드시 `cmake --build core/build`를
   먼저 실행한다.
2. perf runner가 출력하는 runtime `libzlink.so` 경로가 `core/build` 아래인지 확인한다.
3. stale runtime 검사가 실패하면 perf를 계속 돌리지 않고 core/build를 다시 빌드한다.
4. smoke는 최소 아래를 실행한다.

```bash
cd bindings/c/perf
./run_benchmarks_multi.sh --pattern dealer_router --msg-sizes 64,1024,65536 --duration 1 --clients 2
./run_benchmarks_multi.sh --pattern spot_reqrep --msg-sizes 64,1024,65536 --duration 1 --clients 2
./run_benchmarks_multi.sh --pattern stream --msg-sizes 64,1024,65536 --duration 1 --clients 2
```

5. smoke 통과 후 권장 memory tier 산정을 위해 context memory sweep을 실행한다.
   sweep은 최소 `128`, `256`, `512`, `1024` MiB를 포함한다.

### 15.8 정식 문서 반영 파일

구현 후 최소 아래 파일을 확인하고 필요한 내용을 반영한다.

| 구분 | 파일 |
|------|------|
| core context spec | `doc/spec/core/context.ko.md`, `doc/spec/core/context.md` |
| core socket spec | `doc/spec/core/socket/README.ko.md`, `doc/spec/core/socket/README.md` |
| monitoring spec | `doc/spec/core/monitoring.ko.md`, `doc/spec/core/monitoring.md` |
| SPOT spec | `doc/spec/core/service/spot.ko.md`, `doc/spec/core/service/spot.md` |
| guide | `doc/guide/12-socket-options.ko.md` |
| internals defaults | `doc/internals/socket-option-defaults.ko.md` |
| internals defaults | `doc/internals/socket-option-defaults.md` |
| SPOT internals | `doc/internals/spot-internals.ko.md`, `doc/internals/spot-internals.md` |
| perf | `bindings/c/perf/README.md` |
| bindings common | `doc/spec/bindings/README.md` |
| binding specs | `doc/spec/bindings/cpp/README.md` |
| binding specs | `doc/spec/bindings/python/README.md` |
| binding specs | `doc/spec/bindings/java/README.md` |

파일이 존재하지 않거나 해당 계약을 담는 더 적절한 정식 문서가 있으면, 같은
의미를 빠뜨리지 않는 범위에서 그 문서에 반영한다.

### 15.9 최종 리뷰 체크리스트

종료 전 아래 항목을 반복해서 확인한다.

1. `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`가 core와 binding include에 같은 값으로
   반영되었는가.
2. `MsgUnit(B)`가 socket별 effective 값으로 계산되고 snapshot에 출력되는가.
3. `AutoBuffer(B)`만 queue budget에서 차감되는가.
4. `ManualBuffer(B)`가 별도 진단값으로만 표시되는가.
5. SPOT `shared`와 `per-spot`이 서로 다른 `ScopeGroup(B)`로 계산되는가.
6. per-spot floor가 예산을 초과해 강제되지 않는가.
7. HWM shrink가 pending queue가 큰 상태에서 즉시 강제되지 않는가.
8. perf 표가 pattern 시작 아래, 결과표 위에 한 번만 출력되는가.
9. 정식 spec, guide, internals, bindings, perf README가 구현과 일치하는가.
10. draft의 모든 요구가 코드/테스트/문서에 반영되었는가.
