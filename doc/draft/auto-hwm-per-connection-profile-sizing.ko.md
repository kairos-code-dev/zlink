[스펙 목차](../README.ko.md)

# Draft -- auto-HWM 개선 정책: per-connection profile 기반 HWM

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`와 정식 spec 문서에 없는
> API, 상수, 기본 동작을 보장하지 않는다.
> 구현과 공개 헤더, 관련 테스트, 정식 문서가 확정되면 적절한 spec 문서로
> 나누어 반영한다.

## 1. 목적

이 초안은 기존 auto-HWM의 context memory budget 기반 계산을 대체하기 위한
새 정책을 정의한다.

핵심 방향은 아래와 같다.

- HWM은 connection 하나 또는 delivery target 하나에 적용되는 queue depth이다.
- connection 수가 늘거나 줄어도 connection 하나에 필요한 최적 HWM은 바뀌지
  않는다.
- 전체 필요 메모리는 connection 수에 비례해서 증가하며, 이 메모리를 확보하는
  책임은 사용자에게 있다.
- 라이브러리는 전체 메모리 예산을 나눠 HWM을 낮추지 않는다.
- 라이브러리는 profile, socket 역할, 기준 message unit을 사용해 per-connection
  HWM을 정한다.
- 사용자에게는 소켓별 connection 하나당 필요한 메모리 계산 공식을 제공한다.

이 정책에서는 context의 auto-HWM memory budget 옵션을 제거한다. 대신 profile
옵션만 남겨 사용자가 latency 우선, 균형, throughput 우선 중 하나를 선택하게
한다.

## 2. 배경

기존 context memory budget 방식은 context 전체의 자동 HWM 대상 소켓과 connection
수를 기준으로 queue budget을 나눠 가졌다. 이 방식은 전체 메모리를 하나의
상한으로 설명하기 쉽지만, HWM의 본질과 맞지 않는 문제가 있다.

HWM은 "전체 context가 사용할 메시지 개수"가 아니라, 각 connection 또는 delivery
target이 얼마나 많은 메시지를 queue에 둘 수 있는지를 정하는 값이다. 따라서
connection 수가 늘었다는 이유로 connection 하나의 HWM이 낮아지면, 같은 소켓을
같은 workload로 쓰는데도 topology 규모에 따라 latency와 throughput 특성이
달라진다.

이 초안은 HWM을 아래처럼 다시 정의한다.

```text
HWM = per-connection queue depth
```

connection 수가 늘 때 변하는 것은 HWM이 아니라 전체 필요 메모리이다.

```text
total_queue_memory ~= per_connection_memory * connection_count
```

따라서 auto-HWM은 "전체 memory budget을 나누는 planner"가 아니라
"소켓 역할과 profile에 맞는 per-connection HWM selector"가 되어야 한다.

## 3. 공개 계약 방향

### 3.1 변경되는 public API / enum 요약

아래 표는 이 draft가 의도하는 public contract 변경 사항이다. 최종 기준은 구현 후
`core/include/zlink.h`와 `core/include/zlink_enum.h`에 반영된 내용이다.

| 구분 | 이름 | 변경 |
|---|---|---|
| context option enum | `ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB` | 제거. ABI 호환이 필요하면 reserved/deprecated no-op으로만 유지 |
| default macro | `ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT` | 제거 |
| context option enum | `ZLINK_CTX_OPT_AUTO_HWM_STREAM_BOOTSTRAP` | 제거. ABI 호환이 필요하면 deprecated no-op |
| context option enum | `ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP` | 제거. ABI 호환이 필요하면 deprecated no-op |
| default macro | `ZLINK_CTX_AUTO_HWM_STREAM_BOOTSTRAP_DFLT` | 제거 |
| default macro | `ZLINK_CTX_AUTO_HWM_SPOT_BOOTSTRAP_DFLT` | 제거 |
| default macro | `ZLINK_CTX_AUTO_HWM_ENABLE_DFLT` | `0`으로 변경. 새 기본 동작은 HWM 1000 유지 |
| context option enum | `ZLINK_CTX_OPT_AUTO_HWM_ENABLE` | 유지 |
| context option enum | `ZLINK_CTX_OPT_AUTO_HWM_PROFILE` | 유지 |
| context option enum | `ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS` | 유지 |
| profile enum | `zlink_auto_hwm_profile_t` | 유지 |
| profile enum value | `ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY` | 유지 |
| profile enum value | `ZLINK_AUTO_HWM_PROFILE_BALANCED` | 유지 |
| profile enum value | `ZLINK_AUTO_HWM_PROFILE_THROUGHPUT` | 유지 |
| socket option enum | `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` | 유지. 새 scaling 정책의 핵심 입력 |
| function | `zlink_ctx_auto_hwm_recalculate()` | 유지. connection 수 재분배가 아니라 profile/message-unit 재적용 의미 |
| monitor detail flag | `ZLINK_MONITOR_SNAPSHOT_DETAIL_AUTO_HWM_BUDGET` | 제거. ABI 호환이 필요하면 deprecated alias로만 유지 |
| monitor detail flag | `ZLINK_MONITOR_SNAPSHOT_DETAIL_AUTO_HWM_BUFFERS` | 유지 가능. buffer 진단 전용 |
| snapshot fields | `auto_hwm_total_memory_budget_bytes` 등 budget/fair-share 필드 | 제거 또는 deprecated zero-fill |

새 enum 값은 추가하지 않는다. 제거한 enum 숫자는 다른 의미로 재사용하지 않는다.

### 3.2 바인딩 public surface 영향

각 binding은 아래 항목을 같은 의미로 반영해야 한다.

- memory budget context option getter/setter 제거 또는 deprecated no-op 처리
- stream/spot bootstrap context option getter/setter 제거 또는 deprecated no-op 처리
- auto-HWM profile getter/setter 유지
- auto-HWM enable getter/setter 유지
- message unit socket option 유지
- versioned native header/library 동기화

binding 문서에는 제거된 옵션이 새 정책에서 HWM 계산에 영향을 주지 않는다는 점을
명확히 적는다.

### 3.3 제거할 context 옵션

아래 옵션은 새 공개 계약에서 제거한다.

- `ZLINK_CTX_OPT_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB`
- `ZLINK_CTX_AUTO_HWM_TOTAL_MEMORY_BUDGET_MB_DFLT`

제거 후에는 `zlink_ctx_set()`과 `zlink_ctx_get()`으로 이 옵션을 사용할 수 없다.
구현에서 ABI 호환을 위해 숫자 값을 내부 reserved 값으로 남길 수는 있지만,
정식 public header와 정식 spec에서는 더 이상 사용 가능한 옵션으로 설명하지
않는다. 제거한 enum 숫자는 다른 의미로 재사용하지 않는다.

### 3.4 유지할 context 옵션

아래 옵션은 유지한다.

- `ZLINK_CTX_OPT_AUTO_HWM_ENABLE`
- `ZLINK_CTX_OPT_AUTO_HWM_PROFILE`
- `ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS`

`ZLINK_CTX_OPT_AUTO_HWM_STREAM_BOOTSTRAP`과
`ZLINK_CTX_OPT_AUTO_HWM_SPOT_BOOTSTRAP`은 새 정책에서 HWM 계산에 필요하지 않다.
두 옵션은 public contract에서 제거한다. ABI나 binding compatibility 때문에 숫자
값을 바로 지울 수 없으면 한 릴리스 동안 deprecated no-op으로만 남긴다. 어떤 경우도
두 옵션은 HWM 계산에 영향을 주면 안 된다.

최종 구현 기준은 rollout plan의 공개 계약 게이트에서 `core/include/zlink.h`와
`core/include/zlink_enum.h`를 대조해 확정한다.

### 3.5 profile enum

profile enum은 유지한다.

```c
typedef enum zlink_auto_hwm_profile_t
{
    ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY = 1,
    ZLINK_AUTO_HWM_PROFILE_BALANCED = 2,
    ZLINK_AUTO_HWM_PROFILE_THROUGHPUT = 3
} zlink_auto_hwm_profile_t;
```

기본 profile은 `ZLINK_AUTO_HWM_PROFILE_BALANCED`이다.

### 3.6 기본 HWM

자동 HWM을 사용하지 않는 경우 기본 HWM은 기존 기본값 `1000`이다.

```text
default SNDHWM = 1000
default RCVHWM = 1000
```

자동 HWM은 명시적으로 켰을 때 profile 기반 값으로 HWM을 설정한다.
`ZLINK_CTX_AUTO_HWM_ENABLE_DFLT`는 `0`으로 바꾼다. 즉 새 기본 동작은 "기존 HWM
1000 유지"이고, auto-HWM은 사용자가 선택하는 정책이다. sample, perf, binding
문서는 이 opt-in 의미를 같은 방식으로 설명해야 한다.

## 4. 기준 message unit

auto-HWM은 message unit을 기준으로 queue depth를 계산한다. 여기서 message
unit은 실제 traffic의 통계 평균을 뜻하지 않는다. 이 값은 queue 크기를 정할 때
사용하는 **sizing unit**이다.

즉 `4096 B`라는 기준은 "서버 간 메시지 평균이 항상 4 KiB"라는 뜻이 아니다.
"기본 profile은 서버 간 일반 메시지를 4 KiB 단위로 queue sizing한다"는 뜻이다.
workload의 주 메시지가 이 기준보다 훨씬 크거나 작으면 사용자가
`ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`로 자기 workload 기준을 명시해야 한다.

기본 sizing unit은 아래와 같다.

| Socket group | Default message unit |
|---|---:|
| `STREAM` | `1024 B` |
| non-STREAM message sockets | `4096 B` |

`STREAM`의 `1024 B`도 실제 평균 chunk 크기를 보장하는 값이 아니다. STREAM은
byte stream 성격이 강하고 작은 chunk 단위로 흐르는 경우가 많으며, 보통 사용자
연결과 1:1로 붙고 수천에서 10,000개까지 connection 수가 커질 수 있다. 따라서
기본 queue sizing unit을 1 KiB로 보수적으로 두고, HWM 숫자 자체도 일반 message
socket보다 낮게 둔다.

non-STREAM message socket의 `4096 B`는 서버 간 제어 메시지와 작은 업무 메시지를
위한 기본 sizing 기준이다. 대용량 payload가 주 workload인 서비스는 이 기본값을
그대로 쓰면 HWM이 실제 의도보다 깊어질 수 있다. 그 경우 message unit을 payload
기준에 맞게 올려야 한다.

`ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`는 유지한다. 사용자가 이 값을 설정하면 planner는
설정된 message unit을 사용한다. 이 옵션은 connection 수와 무관하며, 같은 소켓의
message sizing 기준만 바꾼다.

기준값이 틀리면 auto-HWM도 사용자의 workload에 맞지 않는다. 이 정책은 모든
서비스의 평균 메시지 크기를 자동으로 추정하지 않는다. 기본 sizing unit은 안전한
시작점이고, 사용자가 workload를 알고 있으면 명시적으로 조정하는 것이 맞다.

## 5. Profile별 HWM 기준값

profile은 기준 message unit에서의 per-connection HWM을 선택한다. 실제 의미는
HWM 숫자 하나가 아니라 아래 byte envelope이다.

```text
profile_queue_envelope =
  basis_hwm * basis_message_unit
```

예를 들어 non-STREAM `balanced`는 `128 @ 4096 B`이므로 connection 하나의 한
방향 queue envelope가 `512 KiB`이다.

### 5.1 기본 테이블

| Socket group | Profile | Basis unit | SNDHWM | RCVHWM | 의도 |
|---|---|---:|---:|---:|---|
| non-STREAM | `low_latency` | 4096 B | 64 | 64 | queue 체류 시간을 짧게 유지 |
| non-STREAM | `balanced` | 4096 B | 128 | 128 | latency와 throughput 균형 |
| non-STREAM | `throughput` | 4096 B | 256 | 256 | burst 흡수와 처리량 우선 |
| STREAM | `low_latency` | 1024 B | 16 | 16 | 수천 client에서 낮은 메모리와 짧은 queue 유지 |
| STREAM | `balanced` | 1024 B | 64 | 64 | 일반적인 1:1 stream 처리 기준 |
| STREAM | `throughput` | 1024 B | 256 | 256 | stream burst 흡수, 일반 소켓보다 낮은 envelope 유지 |

이 값은 connection 수에 따라 변하지 않는다.

### 5.2 역할별 보정

역할별로 같은 profile 안에서도 queue 깊이를 다르게 둘 수 있다. 초기 구현 기준은
아래와 같다.

| Policy class | non-STREAM low_latency | non-STREAM balanced | non-STREAM throughput |
|---|---:|---:|---:|
| `fanout` / `spot_data` | 64 | 128 | 256 |
| `routed` | 64 | 128 | 256 |
| `peer_queue` | 64 | 128 | 256 |
| `recv_ingress` | 64 | 128 | 256 |
| `control` | 16 | 16 | 32 |

| Policy class | STREAM low_latency | STREAM balanced | STREAM throughput |
|---|---:|---:|---:|
| `stream` | 16 | 64 | 256 |

STREAM은 일반 message socket보다 낮은 HWM을 사용한다. 이유는 STREAM이 fanout
socket이 아니라 사용자 connection과 1:1로 붙는 경우가 많고, 기본 운영 규모가
수천 connection 이상일 수 있기 때문이다. connection 하나가 대량 메시지를 오래
쌓아 두는 동작은 STREAM의 기본 가정이 아니다. 큰 burst를 흡수해야 하는 서비스는
`throughput` profile이나 수동 HWM override를 사용한다.

`control`은 데이터 plane이 아니므로 깊은 queue가 필요하지 않다. `routed`는
request/reply와 send/send echo 계열에서 100 client 이상 burst를 받아야 하므로
`balanced` 기준 128을 유지한다.

### 5.3 message unit scaling

사용자가 message unit을 명시하면 HWM은 기준 message unit 대비 inverse scaling을
적용한다. 목적은 profile이 정한 byte envelope를 유지하면서, 이를 메시지 개수로
환산하는 것이다.

```text
scaled_hwm =
  ceil(basis_hwm * basis_message_unit / effective_message_unit)
```

그 후 profile별 min/max cap을 적용한다.

| Socket group | Profile | Min HWM | Max HWM |
|---|---|---:|---:|
| non-STREAM | `low_latency` | 1 | 256 |
| non-STREAM | `balanced` | 1 | 512 |
| non-STREAM | `throughput` | 1 | 1024 |
| STREAM | `low_latency` | 1 | 64 |
| STREAM | `balanced` | 1 | 128 |
| STREAM | `throughput` | 1 | 512 |

예를 들어 non-STREAM `balanced`에서 기준값은 `128 @ 4096 B`이다.

```text
profile_queue_envelope = 128 * 4096 = 524288 B
```

사용자가 effective message unit을 64 KiB로 명시하면 같은 envelope 안에 들어갈 수
있는 메시지 개수는 8개다.

```text
effective_message_unit = 65536 B
scaled_hwm = ceil(128 * 4096 / 65536) = 8
```

즉 큰 메시지에서는 queue depth가 줄어든다. 반대로 effective message unit을 1 KiB로
명시하면 계산값은 512가 된다.

```text
effective_message_unit = 1024 B
scaled_hwm = ceil(128 * 4096 / 1024) = 512
```

이때 profile cap이 계산값을 제한한다. cap은 아주 작은 message unit이 들어왔을 때
HWM이 과도하게 커지는 것을 막는다. 하지만 connection 수가 늘어도 이 값은 변하지
않는다.

## 6. 총 메모리 계산 공식

라이브러리는 context memory budget을 받지 않는다. 대신 사용자는 아래 공식을
사용해 필요한 메모리를 산정한다.

### 6.1 기본 공식

```text
per_connection_queue_bytes =
  sndhwm * send_message_unit
  + rcvhwm * recv_message_unit

estimated_connection_memory =
  per_connection_queue_bytes * overhead_factor
```

초기 문서에서는 `overhead_factor = 1.5`를 권장한다. 운영 환경에서 allocator,
multipart frame 수, transport buffer, TLS buffer 여유를 더 보수적으로 잡고 싶으면
`2.0`을 사용한다.

### 6.2 socket별 공식

#### PAIR / DEALER / ROUTER

```text
socket_queue_memory =
  peer_count
  * (sndhwm * send_message_unit + rcvhwm * recv_message_unit)
  * overhead_factor
```

`ROUTER`는 peer routing id별 outbound pipe와 inbound queue를 고려해
`peer_count`를 곱한다.

#### PUB / XPUB

```text
publisher_queue_memory =
  subscriber_count
  * sndhwm
  * publish_message_unit
  * overhead_factor
```

fanout에서는 subscriber 수가 늘수록 총 queue memory가 선형 증가한다. 이때 HWM은
바뀌지 않는다.

#### SUB / XSUB

```text
subscriber_queue_memory =
  publisher_count
  * rcvhwm
  * subscribe_message_unit
  * overhead_factor
```

#### STREAM

```text
stream_queue_memory =
  connection_count
  * (sndhwm * stream_send_unit + rcvhwm * stream_recv_unit)
  * overhead_factor
```

STREAM은 OS socket buffer와 TLS buffer 영향이 크다. 따라서 실제 운영 산정에서는
아래를 추가한다.

```text
stream_total_memory =
  stream_queue_memory
  + connection_count * (sndbuf + rcvbuf + transport_overhead)
```

#### SPOT one-way data plane

```text
spot_publish_memory =
  active_publish_targets
  * sndhwm
  * publish_message_unit
  * overhead_factor
```

`active_publish_targets`는 전체 spot handle 수가 아니라, 실제 한 번의 publish에서
동시에 받는 target 수이다.

#### SpotNode routed plane

```text
spot_routed_memory =
  routed_target_count
  * (sndhwm * routed_send_unit + rcvhwm * routed_recv_unit)
  * overhead_factor
```

SpotNode의 routed delivery queue hard limit는 HWM과 별도 정책이다. hard limit는
느린 local target을 언제 delivery plane에서 끊을지 결정하며, per-connection HWM
공식과 섞지 않는다.

## 7. monitor snapshot 진단값

기존 snapshot에는 context memory budget과 fair-share 결과를 설명하는 필드가
있다. 새 정책에서는 이 필드들이 혼동을 만든다. 공개 snapshot은 사용자가
"현재 소켓에 어떤 auto-HWM 입력이 적용됐고, 실제 HWM이 무엇인가"를 확인하는
용도로 좁힌다. 전체 메모리 산정은 snapshot 필드가 아니라 6장의 공식으로
계산한다.

### 7.1 public contract로 유지할 필드

아래 필드는 새 정책에서도 공개 진단값으로 의미가 있다.

| 필드 | 유지 이유 |
|---|---|
| `auto_hwm_enabled` | auto-HWM이 이 소켓에 적용 가능한 상태인지 확인한다. |
| `auto_hwm_profile` | profile table의 어느 행을 썼는지 확인한다. |
| `auto_hwm_unit_budget_bytes` | 기준 HWM과 기준 message unit이 만든 per-connection byte envelope이다. |
| `auto_hwm_size_cap` | message unit scaling 뒤 어떤 cap이 적용됐는지 확인한다. |
| `auto_hwm_applied_sndhwm` | 실제 적용된 send HWM이다. |
| `auto_hwm_applied_rcvhwm` | 실제 적용된 recv HWM이다. |
| `auto_hwm_effective_message_bytes` | scaling에 사용한 message unit이다. |
| `auto_hwm_effective_sndbuf` | 메모리 산정에 참고할 실제 send buffer 값이다. |
| `auto_hwm_effective_rcvbuf` | 메모리 산정에 참고할 실제 recv buffer 값이다. |

`auto_hwm_unit_budget_bytes`는 새 정책에서 아래 뜻으로 재정의한다.

```text
auto_hwm_unit_budget_bytes =
  basis_hwm * basis_message_unit
```

즉 전체 context budget share가 아니라, profile이 의도한 connection 하나당 queue
byte envelope이다.

### 7.2 제거 또는 deprecated 처리할 필드

아래 필드는 새 정책에서 공개 계약으로 유지하지 않는다. ABI를 유지해야 하면 값을
`0` 또는 `-1`로 채우고 정식 문서에서 deprecated로 표시한다. 제거한 필드는 새
public 설명에서 HWM 계산의 근거로 사용하지 않는다.

| 필드 | 처리 이유 |
|---|---|
| `auto_hwm_total_memory_budget_bytes` | context memory budget 옵션이 제거된다. |
| `auto_hwm_queue_budget_bytes` | context budget을 queue budget으로 나누지 않는다. |
| `auto_hwm_transport_budget_bytes` | HWM 계산 입력이 아니다. |
| `auto_hwm_runtime_reserve_bytes` | context budget 방식의 reserve 값이다. |
| `auto_hwm_socket_queue_share_bytes` | fair-share 계산이 제거된다. |
| `auto_hwm_socket_message_slots` | fair-share 결과였고 applied HWM과 중복된다. |
| `auto_hwm_estimated_max_memory_bytes` | 라이브러리가 전체 메모리 상한을 보장하지 않는다. |
| `auto_hwm_observed_count` | connection 수는 HWM을 낮추는 입력이 아니다. |
| `auto_hwm_planning_count` | connection 수 기반 planning이 제거된다. |
| `auto_hwm_context_total_planning_count` | context 전체 planning count가 필요 없다. |
| `auto_hwm_managed_connections` | HWM 계산 입력으로 공개하지 않는다. |
| `auto_hwm_active_hwm_connections` | HWM 계산 입력으로 공개하지 않는다. |
| `auto_hwm_base_floor_per_connection` | min HWM은 profile cap으로 설명한다. |
| `auto_hwm_effective_publish_fanout` | fanout 수로 HWM을 재분배하지 않는다. |
| `auto_hwm_scope` | 내부 grouping 값이다. |
| `auto_hwm_scope_count` | scope 기반 재분배가 제거된다. |
| `auto_hwm_auto_buffer_bytes` | buffer 총량 계산은 사용자 공식으로 옮긴다. |
| `auto_hwm_manual_buffer_bytes` | buffer 총량 계산은 사용자 공식으로 옮긴다. |
| `auto_hwm_buffer_connections` | connection 수는 별도 관측값이지 auto-HWM 입력이 아니다. |
| `auto_hwm_requested_sndbuf` | effective buffer 값과 중복되며 HWM 결정 근거가 아니다. |
| `auto_hwm_requested_rcvbuf` | effective buffer 값과 중복되며 HWM 결정 근거가 아니다. |
| `auto_hwm_deferred_sndhwm` | 내부 전이 상태이다. |
| `auto_hwm_deferred_rcvhwm` | 내부 전이 상태이다. |

`auto_hwm_role`과 `auto_hwm_policy_class`도 그대로 public contract에 두지 않는다.
현재 값은 `core/src` 내부 enum 숫자이고, 공개 enum으로 정의되어 있지 않다. 이
draft에서는 새 공개 enum을 추가하지 않는다. 두 필드는 제거하거나, ABI 호환이
필요하면 deprecated 필드로만 남긴다. perf runner는 내부 검증 편의를 위해 role
이름을 출력할 수 있지만, 그 출력은 public API 계약이 아니다.

### 7.3 detail flag 정리

`ZLINK_MONITOR_SNAPSHOT_DETAIL_AUTO_HWM_BUDGET`은 이름이 새 정책과 맞지 않는다.
public header에서는 제거하거나 deprecated alias로만 남긴다. 새 detail flag를
추가하지 않는다면 기존 bit는 "auto-HWM policy detail" 의미로만 내부 호환 처리하고,
정식 문서에는 budget이라는 뜻으로 설명하지 않는다.

`ZLINK_MONITOR_SNAPSHOT_DETAIL_AUTO_HWM_BUFFERS`는 effective buffer 진단을 위해
유지할 수 있다. 다만 total buffer budget이나 connection 기반 총량을 보장한다는
의미로 설명하지 않는다.

### 7.4 이번 변경에서 추가하지 않을 필드

아래 필드는 유용할 수 있지만 이번 변경에서는 추가하지 않는다. 추가가 필요하면
별도 draft와 public API 리뷰를 거쳐 진행한다.

- `auto_hwm_basis_message_bytes`
- `auto_hwm_basis_hwm`
- `auto_hwm_memory_formula_version`
- `auto_hwm_estimated_per_connection_memory_bytes`

이번 구현에서는 ABI 변경 폭을 줄이기 위해 새 필드를 추가하지 않고, 기존 필드의
의미 정리와 deprecated 처리를 우선한다.

## 8. 재계산 정책

connection 변화는 HWM 재계산 사유가 아니다. HWM은 connection 수와 무관하기
때문이다.

아래 변화만 auto-HWM 재계산 사유가 된다.

- auto-HWM enable 변경
- auto-HWM profile 변경
- socket role 또는 policy class 변경
- `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` 변경
- 수동 HWM override 설정 또는 해제
- transport 특성 변경으로 STREAM/non-STREAM group이 바뀌는 경우

`zlink_ctx_auto_hwm_recalculate(ctx)`는 유지할 수 있다. 다만 새 의미는 "현재
connection 수를 다시 세어 HWM을 나눈다"가 아니라 "현재 profile, role, message
unit 기준으로 다시 적용한다"이다.

## 9. 예시

### 9.1 non-STREAM balanced 기본

```text
basis_hwm = 128
basis_message_unit = 4096 B
unit_budget = 128 * 4096 = 524288 B
```

connection 하나의 한 방향 queue envelope는 약 512 KiB이다.

양방향 소켓에서 send/recv 모두 같은 기준을 쓰면:

```text
per_connection_queue_bytes =
  128 * 4096 + 128 * 4096
  = 1048576 B
```

overhead factor 1.5를 적용하면 connection 하나당 약 1.5 MiB를 잡는다.

### 9.2 ROUTER 100 clients

```text
peer_count = 100
sndhwm = 128
rcvhwm = 128
message_unit = 4096 B
overhead_factor = 1.5

socket_queue_memory =
  100 * (128 * 4096 + 128 * 4096) * 1.5
  = 약 150 MiB
```

clients가 1000개가 되면 HWM은 그대로 128이고, 필요 메모리는 약 1.5 GiB로
증가한다.

### 9.3 256 KiB message

```text
effective_message_unit = 262144 B
balanced non-STREAM basis = 128 @ 4096 B

scaled_hwm = ceil(128 * 4096 / 262144) = 2
```

256 KiB payload에서는 connection 하나당 queue depth가 2로 줄어든다. connection
수가 늘어도 2는 바뀌지 않는다.

### 9.4 STREAM balanced

```text
basis_hwm = 64
basis_message_unit = 1024 B
unit_budget = 64 KiB
```

STREAM은 1 KiB chunk 기준으로 balanced HWM을 64로 둔다. STREAM은 보통 fanout이
아니라 사용자 connection과 1:1로 붙기 때문에, 일반 message socket보다 더 낮은
queue depth를 기본값으로 사용한다. 64 KiB chunk로 가정하면:

```text
scaled_hwm = ceil(64 * 1024 / 65536) = 1
```

10,000 clients에서 1 KiB 기준 balanced 값을 그대로 쓰면 send/recv queue envelope는
connection 하나당 128 KiB이다.

```text
stream_queue_memory =
  10000 * (64 * 1024 + 64 * 1024) * 1.5
  = 약 1.9 GiB
```

여기에 OS socket buffer, TLS buffer, application buffer를 별도로 더해야 한다.

## 10. 기존 draft와의 관계

이 초안은 아래 기존 draft의 context memory budget 중심 정책을 대체한다.

- `doc/draft/auto-hwm-context-memory-sizing.ko.md`
- `doc/draft/auto-hwm-msg-unit-socket-option.ko.md` 중 context budget share 관련 절
- `doc/draft/auto-hwm-recalculation-policy.ko.md` 중 connection 변화 기반 HWM
  재계산 관련 절
- `doc/draft/spot-topology-redesign.ko.md` 중 context 예산과 scope count로 HWM을
  낮추는 설명

다만 아래 항목은 유지한다.

- profile enum과 profile 선택 개념
- `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`
- 수동 HWM override 우선순위
- SpotNode 내부 socket별 role/policy class 분리
- SpotNode routed delivery queue hard limit는 HWM과 별도 정책이라는 원칙

## 11. 구현 완료 조건

이 draft가 구현 완료로 간주되려면 아래 조건을 모두 만족해야 한다.

1. context memory budget 옵션이 public contract에서 제거되거나 deprecated no-op으로
   명확히 정리된다.
2. auto-HWM 계산이 connection 수 또는 context 전체 planning count로 HWM을 낮추지
   않는다.
3. profile과 message unit만으로 같은 socket role의 per-connection HWM이 결정된다.
4. STREAM 기본 message unit은 1024 B, non-STREAM 기본 message unit은 4096 B이다.
5. 수동 HWM override가 auto-HWM보다 우선한다.
6. monitor snapshot과 perf 출력이 새 의미를 혼동 없이 보여 준다.
7. 정식 spec, guide, internals, binding 문서가 새 정책으로 갱신된다.
8. 모든 binding이 memory budget 옵션 제거 또는 deprecated 처리와 profile 설정을
   같은 의미로 노출한다.
9. perf smoke는 실제 result row를 생성해야 하며, 0-result 성공 경로가 없어야 한다.

## 12. 회귀 테스트 항목

구현 단계에서 아래 회귀 테스트를 추가하거나 기존 테스트에 명시적으로 반영한다.

### 12.1 public option 계약

| 테스트 | 기대 결과 |
|---|---|
| removed memory budget option set/get | 제거 시 `EINVAL` 또는 해당 binding의 config error 반환 |
| deprecated memory budget option set/get | deprecated 선택 시 성공하더라도 HWM 계산에 영향 없음 |
| removed bootstrap option set/get | 제거 시 `EINVAL` 또는 해당 binding의 config error 반환 |
| deprecated bootstrap option set/get | deprecated 선택 시 성공하더라도 HWM 계산에 영향 없음 |
| profile set/get | `low_latency`, `balanced`, `throughput` round-trip 성공 |
| invalid profile value | `EINVAL` 또는 binding별 config error |
| auto-HWM disabled default | 기본 socket HWM이 `1000`으로 유지 |
| auto-HWM enabled opt-in | profile table 값이 적용 |

### 12.2 planner 공식

| 테스트 | 기대 결과 |
|---|---|
| non-STREAM balanced 기본 | `4096 B` 기준 `SNDHWM=128`, `RCVHWM=128` |
| non-STREAM low_latency 기본 | `4096 B` 기준 `64` |
| non-STREAM throughput 기본 | `4096 B` 기준 `256` |
| STREAM balanced 기본 | `1024 B` 기준 `64` |
| STREAM low_latency 기본 | `1024 B` 기준 `16` |
| STREAM throughput 기본 | `1024 B` 기준 `256` |
| non-STREAM 64 KiB message unit | `ceil(128 * 4096 / 65536) = 8` |
| STREAM 64 KiB message unit | `ceil(64 * 1024 / 65536) = 1` |
| small message unit cap | profile별 max cap을 넘지 않음 |
| huge message unit floor | 최소 HWM `1` 유지 |

### 12.3 connection 수 독립성

| 테스트 | 기대 결과 |
|---|---|
| ROUTER 1 client vs 100 clients | 같은 profile/message unit이면 HWM 동일 |
| PUB 1 subscriber vs 100 subscribers | 같은 profile/message unit이면 HWM 동일 |
| SPOT 1 target vs 100 targets | 같은 profile/message unit이면 HWM 동일 |
| STREAM 1 client vs 10000 clients | 같은 profile/message unit이면 HWM 동일 |

connection 수가 늘면 총 필요 메모리 산정값만 증가해야 한다. HWM 숫자가 줄어들면
실패다.

### 12.4 manual override와 재계산

| 테스트 | 기대 결과 |
|---|---|
| manual `SNDHWM` 설정 후 auto-HWM recalc | `SNDHWM` 유지 |
| manual `RCVHWM` 설정 후 auto-HWM recalc | `RCVHWM` 유지 |
| message unit 변경 후 recalc | auto-managed HWM만 공식대로 갱신 |
| profile 변경 후 recalc | auto-managed HWM만 새 profile 값으로 갱신 |
| connection attach/detach 후 recalc | HWM 변경 없음 |

### 12.5 monitor snapshot

| 테스트 | 기대 결과 |
|---|---|
| budget/fair-share 필드 deprecated | 값이 `0`이거나 문서화된 deprecated 값 |
| internal role/policy numeric fields | 공개 enum 없으면 deprecated 값으로 채움 |
| requested/deferred/buffer total fields | 제거 또는 deprecated 값으로 채움 |
| `auto_hwm_unit_budget_bytes` | `basis_hwm * basis_message_unit` 의미로 채움 |
| applied HWM fields | 실제 socket에 적용된 HWM과 일치 |
| effective message bytes | default 또는 `ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES` 반영 |
| effective buffer fields | 실제 socket buffer 값 또는 문서화된 effective 값과 일치 |
| STREAM/non-STREAM 구분 | 각 socket group의 basis unit이 snapshot과 perf 출력에서 확인 가능 |

### 12.6 SPOT 성능 회귀

| 테스트 | 기대 결과 |
|---|---|
| `MULTI_SPOT_REQREP` 100 clients 64B tcp balanced | 200 Kops/s 이상 또는 기존 회복 기준 이상 |
| `MULTI_SPOT_SENDSEND` 100 clients 64B tcp balanced | 200 Kops/s 이상 또는 기존 회복 기준 이상 |
| `MULTI_SPOT` 100 clients sizes tcp balanced | 모든 size에서 실제 result row 생성 |
| `MULTI_SPOT` large message | message unit scaling으로 HWM이 낮아짐 |
| SPOT routed hard limit | `ZLINK_SPOT_NODE_ROUTED_QUEUE_HARD_LIMIT_DFLT=500` 유지 |

### 12.7 perf 성공 조건

| 테스트 | 기대 결과 |
|---|---|
| C perf runtime path | `core/build` 아래 `libzlink.so` 사용 |
| single perf smoke | expected result row와 actual result row 일치 |
| multi perf smoke | expected result row와 actual result row 일치 |
| fake/0-result guard | result row 0개 성공 처리 없음 |
| binding perf smoke | 각 binding perf가 실제 result row 출력 |
