# 연결 수 기반 auto-HWM 수정 계획

> **이 문서는 현재 공개 계약이 아닌 구현 전 draft 계획이다.**
> 아래 내용은 `auto-HWM` 정책을 연결 수에 맞게 조정하기 위한 설계와 실행 순서를
> 정리한다. 정식 공개 계약은 아직 `core/include/zlink.h`와
> `core/doc/spec/core/context.ko.md`를 기준으로 한다.

## 배경

현재 auto-HWM은 profile, 소켓 역할, 메시지 단위로 HWM을 계산한다. 연결 수를 함수 인자로
받고 `buffer_connections`까지 계산하지만, 실제 HWM 산식에는 사용하지 않는다.

현재 핵심 흐름은 다음과 같다.

```text
unit_budget_bytes = basis_hwm * basis_message_unit
socket_message_slots = ceil(unit_budget_bytes / effective_message_unit)
final_hwm = min(socket_message_slots, size_cap)
```

`balanced` profile에서 일반 메시지의 기준 단위는 `4096` bytes이고, 기준 HWM은 `256`이다.
따라서 `auto-HWM message unit = 4 KiB`이면 peer 하나와 방향 하나당 큐 예산은 다음과 같다.

```text
256 messages * 4 KiB = 1 MiB
```

1000개 `SpotNode`가 full mesh로 연결되면 한 노드는 보통 자기 자신을 제외한 999 peer와
연결된다. PUBSUB mesh만 보더라도 양방향 큐 예산은 약 1.95 GiB가 된다.

```text
999 peers * 2 directions * 256 * 4 KiB = 1,998 MiB
```

`external-router`까지 같은 조건으로 양방향 full mesh를 구성하면 같은 크기가 한 번 더 붙어
약 3.90 GiB까지 커질 수 있다. 이 값은 idle RSS가 아니라 큐가 HWM까지 찼을 때의 payload
예산이다. 그래도 기본 profile의 최악 예산으로는 너무 크다.

이 문서의 메모리 계산은 HWM으로 제한되는 payload queue 예산만 다룬다. data-plane batch
buffer, staged queue 구조체, pipe metadata, route/subscription table, thread stack, allocator
overhead, OS socket buffer는 이 계산에 포함하지 않는다. 따라서 아래 숫자는 총 RSS 예측값이
아니라 HWM 정책 변경으로 직접 줄일 수 있는 부분이다.

## 문제

단순히 연결 수로 나누는 방식도 좋은 해법이 아니다.

```text
hwm = total_budget / (connection_count * message_unit)
```

이 산식을 그대로 쓰면 1000 peer 근처에서 peer당 HWM이 `1`까지 떨어질 수 있다. 그러면 정상
traffic의 짧은 burst도 흡수하지 못하고 backpressure, `EAGAIN`, drop이 너무 자주 발생할 수
있다.

따라서 수정 목표는 두 가지를 동시에 만족해야 한다.

1. peer 수가 많을 때 노드 하나의 총 큐 예산이 과도하게 커지지 않아야 한다.
2. peer당 최소 burst 흡수 능력은 유지해야 한다.

## 선택한 방향

연속 수식으로 매 연결 변화마다 HWM을 바꾸지 않는다. profile별 bucket을 두고, bucket이
바뀔 때만 HWM을 바꾼다. bucket 경계에는 hysteresis를 둬서 연결 수가 경계 근처에서 오르내릴
때 HWM이 계속 흔들리지 않게 한다.

핵심 정책은 다음과 같다.

```text
connection count -> peer-count bucket
profile + policy class -> profile_hwm_4k, profile_size_cap
bucket -> bucket_hwm_4k
base_hwm_4k = min(profile_hwm_4k, bucket_hwm_4k)
unit_budget_bytes = base_hwm_4k * 4096
scaled_slots = ceil(unit_budget_bytes / effective_message_unit)
final_hwm = clamp(scaled_slots, 1, profile_size_cap)
```

bucket 값은 `4 KiB` 메시지 기준의 HWM이다. 최종 HWM은 반드시 기존 auto-HWM처럼
`effective_message_unit`으로 다시 환산한다. 이 순서를 지켜야 큰 메시지에서 peer당 큐
예산이 다시 커지지 않는다.

`SNDBUF` / `RCVBUF`는 이 계획에서 다루지 않는다. transport buffer는 기본값 `-1`을 유지해서
OS 기본값과 TCP 자동 조정에 맡긴다.

## 기본 bucket 제안

아래 값은 일반 메시지 `4 KiB` 기준의 초기 기본값이다. 최종 숫자는 perf 측정과 1000-node
시뮬레이션 결과를 보고 조정한다.

| peer 수 | compact | low_latency | balanced | throughput |
|---------|--------:|------------:|---------:|-----------:|
| 1-64 | 64 | 128 | 256 | 512 |
| 65-128 | 64 | 64 | 128 | 256 |
| 129-512 | 32 | 32 | 64 | 128 |
| 513-2048 | 16 | 16 | 32 | 64 |
| 2049+ | 8 | 8 | 16 | 32 |

이 표에서 값은 바이트가 아니라 `4 KiB` 기준의 peer당 방향당 HWM 메시지 개수다. 실제
`message_unit`이 `4 KiB`보다 크면 HWM slot 수는 줄고, `4 KiB`보다 작으면 늘 수 있다. profile
cap은 기존처럼 최종 slot 수의 상한으로 적용한다.

예를 들어 `balanced`, `1000 SpotNode`, `4 KiB` 조건이면 한 노드는 999 peer를 보므로
`513-2048` bucket에 들어가고 peer당 HWM은 `32`가 된다.

```text
999 peers * 2 directions * 32 * 4 KiB = 255,744 KiB
```

즉 PUBSUB mesh 양방향 기준 노드 하나의 payload queue 예산은 약 250 MiB다.
`external-router`까지 같은 방식으로 양방향 full mesh를 구성하면 약 500 MiB다.

주의할 점은 현재 SPOT routed 경로에 small-message latency 보정이 있다는 것이다.
`apply_spot_routed_latency_floor()`가 connection bucket 적용 뒤에 HWM을 다시 키우면
`external-router`가 이 예산을 지키지 못한다. connection-aware 경로에서는 routed latency
보정도 bucket 상한을 넘지 못하게 하거나, bucket 계산 전에 profile 기본값에만 반영해야 한다.

같은 조건에서 `message_unit = 64 KiB`이면 peer당 HWM은 다음처럼 줄어든다.

```text
unit_budget_bytes = 32 * 4 KiB = 128 KiB
scaled_slots = ceil(128 KiB / 64 KiB) = 2
999 peers * 2 directions * 2 * 64 KiB = 255,744 KiB
```

즉 메시지 단위가 커져도 같은 bucket의 바이트 예산은 대체로 유지된다.

100개 `SpotNode` 조건에서는 한 노드가 99 peer를 보므로 `65-128` bucket에 들어간다.
`balanced` 기준 HWM은 `128`이다.

```text
99 peers * 2 directions * 128 * 4 KiB = 99 MiB
```

## hysteresis

bucket 경계에는 20-25% 정도의 여유 구간을 둔다. 예를 들어 `1-64` bucket과 `65-128` bucket
경계에서는 다음처럼 동작한다.

```text
현재 1-64 bucket:
  peers >= 80 이 되면 65-128 bucket으로 이동

현재 65-128 bucket:
  peers <= 48 이 되면 1-64 bucket으로 이동
```

이 방식은 연결 수가 `63`, `64`, `65` 근처에서 흔들릴 때 HWM이 반복 변경되는 일을 막는다.

구현은 bucket index를 socket auto-HWM 상태에 저장하거나, 최근 적용한 HWM을 기준으로 다음
bucket 이동 여부를 판단한다. 새 public API를 추가하지 않는다.

## 적용 범위

우선 연결 수에 따른 총 메모리 문제가 큰 SPOT mesh 역할에 적용한다.

| 역할 | 적용 여부 | 이유 |
|------|-----------|------|
| `auto_hwm_role_spot_data` | 부분 적용 | `mesh-pub`처럼 remote peer 수에 비례하는 경로에 적용한다. local fanout 전용 socket은 별도 판단한다. |
| `auto_hwm_role_recv_ingress` | 적용 | `mesh-xsub`, ingress receive 경로가 peer 수에 비례해 커질 수 있다. |
| `auto_hwm_role_routed` | 적용 | `external-router`는 full mesh에서 SNDHWM과 RCVHWM 양쪽 모두 peer 수에 비례해 커질 수 있다. |
| `auto_hwm_role_stream` | 보류 | STREAM은 연결과 메시지 특성이 다르므로 별도 측정 후 다룬다. |
| `auto_hwm_role_control` | 보류 | control socket은 큐를 작게 유지하되 data path와 같은 산식을 적용하지 않는다. |
| `auto_hwm_role_peer_queue` | 보류 | DEALER/PAIR 일반 경로는 SPOT mesh와 별도로 회귀 위험을 봐야 한다. |

초기 구현은 policy class만으로 일괄 적용하지 않는다. 같은 `spot_data`라도 remote mesh socket과
local-only socket의 비용 구조가 다르기 때문이다. 첫 구현은 호출부에서 connection bucket 사용을
명시적으로 켠 socket에만 bucket을 적용한다. 그 외 role과 local-only 경로는 기존 산식을 유지한다.

## admission queue와 per-peer HWM의 관계

SPOT 송신 경로에는 이미 노드 단위 admission queue가 있다. public publish와 routed send는
data-plane socket을 직접 만지지 않고 `publish_ingress_queue` 또는 `routed_send_queue`에 먼저
enqueue된다. 이 queue는 message 개수와 byte limit으로 public API의 backpressure 의미를 만든다.

connection-aware HWM은 이 admission queue를 대체하지 않는다. 역할은 다음처럼 분리한다.

| 계층 | 적용 대상 | 역할 |
|------|-----------|------|
| node admission queue | `publish_ingress_queue`, `routed_send_queue` | public send/publish 호출에서 노드 단위 backpressure와 timeout 의미를 결정한다. |
| per-peer socket HWM bucket | `mesh-pub`, `mesh-xsub`, `external-router` | data-plane이 transport pipe로 넘긴 뒤 peer별 queue가 무한히 커지지 않게 하는 보조 상한이다. |

따라서 송신측에서는 admission queue가 먼저 노드 단위 총량을 제한한다. per-peer SNDHWM bucket은
느린 peer나 transport backpressure가 생겼을 때 data-plane socket 내부에 쌓이는 추가 queue를
제한한다. 이 둘을 같은 예산으로 중복 계산하면 안 된다.

수신측 `mesh-xsub`와 `external-router` RCVHWM은 public admission queue보다 transport ingress에
가깝다. peer 수에 비례해 커지는 수신 pipe 예산을 직접 줄이는 효과가 있으므로, connection bucket
적용 효과는 수신/ingress 경로에서 더 뚜렷하게 나타날 수 있다.

구현 시 public API 오류 의미는 admission queue가 계속 소유한다. per-peer HWM bucket 때문에
data-plane 내부 socket에서 `EAGAIN`이 발생하면 기존 pending/staged 경로와 pollout 재시도 경로로
흡수해야 하며, public 호출자에게 새로운 오류 의미를 노출하지 않는다.

## 재계산 방식

현재 runtime은 연결 수와 local pub/sub 수를 snapshot으로 얻고 auto-HWM planner에 전달하는
흐름을 이미 가진다. 다만 planner가 그 값을 버리고 있다.

수정 후에는 다음 순서로 동작하게 한다.

1. peer 연결, 연결 해제, local pub/sub 수 변화가 생기면 runtime이 auto-HWM 재계산을 예약한다.
2. 기존 `ZLINK_CTX_OPT_AUTO_HWM_RECALC_DEBOUNCE_MS` 값을 사용해 짧은 변화는 묶어서 처리한다.
3. 재계산 시점에 `managed_connections`와 `active_hwm_connections` 중 큰 값을 사용한다.
4. 해당 값으로 bucket을 고른다.
5. hysteresis 조건을 통과한 경우에만 새 HWM을 적용한다.
6. 수동 `SNDHWM` / `RCVHWM` 설정이 있는 소켓은 계속 수동 값이 우선한다.

`zlink_ctx_auto_hwm_recalculate()`는 기존처럼 즉시 재계산을 수행한다. 즉시 재계산에서도
hysteresis는 적용한다. 사용자가 강제로 profile이나 message unit을 바꾼 경우에는 bucket이
같더라도 HWM 값이 달라질 수 있으므로 다시 적용한다.

## 구현 단계

### 1. planner 상태와 산식 정리

- `core/src/runtime/core/auto_hwm_policy.cpp`에서 버려지는 `buffer_connections`를 실제 산식에
  반영한다.
- profile별 bucket table을 내부 helper로 둔다.
- bucket table 값은 최종 HWM이 아니라 `4 KiB` 기준 HWM으로 둔다. 기존 auto-HWM과 같이
  `unit_budget_bytes = bucket_hwm_4k * 4096`을 먼저 만들고, 이를 `effective_message_unit`으로
  나누어 최종 slot 수를 얻는다.
- connection bucket 적용 여부를 명시하는 planner 입력을 둔다. 현재 `socket_base_t`의 일반
  auto-HWM 경로는 `buffer_cost_enabled_ = true`로 호출하므로, 이 인자를 그대로 gate로 쓰면
  일반 PUB/SUB/ROUTER socket까지 함께 바뀔 수 있다. 첫 구현에서는 일반 socket 기본 경로가
  기존 산식을 유지하도록 별도 flag를 추가하거나, 호출부의 값을 명확히 분리한다.
- `scope_count_`는 local fanout처럼 여러 local consumer를 한 socket이 공유하는 경우에만
  사용하고, peer mesh 연결 수와 혼동하지 않는다.

### 2. SPOT 적용 경로 제한

- `spot_internal_auto_hwm_policy_t` 호출부에서 peer 수가 의미 있는 socket만
  connection bucket 적용 flag를 켠다.
- SPOT internal control socket과 local-only socket은 기존처럼 연결 수 기반 축소를 적용하지
  않는다.
- `mesh_pub`, `mesh_xsub`, `external_router`는 연결 수 기반 bucket을 적용한다.
- local fanout, pub ingress, control socket처럼 remote peer 수와 직접 비례하지 않는 socket은
  첫 구현에서 제외한다.
- `external_router`의 small-message latency 보정은 connection bucket 상한을 넘지 못하게
  수정한다. 1000-node, `balanced`, `4 KiB`에서 routed HWM이 `128`로 다시 올라가면 이 계획은
  실패한 것으로 본다.

### 3. hysteresis 구현

- socket별 마지막 auto-HWM bucket 또는 마지막 적용 HWM을 보관한다.
- 새 bucket이 인접 경계 안에서만 움직이면 기존 HWM을 유지한다.
- profile 변경, message unit 변경, auto-HWM enable 전환은 hysteresis 유지보다 사용자 의도가
  우선하므로 재적용한다.

### 4. monitoring 보강

기존 monitor snapshot은 auto-HWM role, profile, unit budget, message unit, applied HWM을
보여준다. 이번 변경 후에는 디버깅을 위해 최소한 다음 값이 내부 또는 monitor detail에 필요하다.

| 값 | 목적 |
|----|------|
| observed connection count | 현재 HWM bucket을 고른 입력값 확인 |
| selected bucket | HWM이 어느 peer 구간에서 선택되었는지 확인 |
| bucket-limited HWM | profile 기본값에서 얼마나 줄었는지 확인 |

새 public field 추가가 부담되면 첫 구현에서는 테스트 전용 접근자나 internal snapshot으로 검증하고,
정식 public monitoring 확장은 별도 변경으로 분리한다.

### 5. 문서 반영

구현이 끝난 뒤에만 정식 문서를 고친다.

- `core/doc/spec/core/context.ko.md`
- `core/doc/spec/core/monitoring.ko.md`
- `core/doc/guide/10-performance.ko.md`
- `core/doc/guide/12-socket-options.ko.md`
- `core/doc/internals/spot-internals.ko.md`
- `core/doc/internals/spot-internals.md`
- 영어 대응 문서

정식 문서에는 구현된 bucket 값과 hysteresis 규칙만 적는다. 아직 구현하지 않은 STREAM,
control, 일반 DEALER/PAIR 정책을 보장처럼 쓰지 않는다.

현재 `core/doc/internals/spot-internals.ko.md`와 영어 대응 문서에는 relay/delivery socket이
HWM `0`을 쓴다는 설명과, `mesh-pub` / `external-router`가 auto-HWM admission 값을 쓴다는 설명이
섞여 있다. 구현 시 실제 effective HWM을 기준으로 이 모순을 정리해야 한다. 이 계획의 전제는 현재
`mesh-pub`, `mesh-xsub`, `external-router`가 auto-HWM 또는 override HWM을 적용받는다는 것이다.

## 테스트 계획

### 단위 테스트

- `auto_hwm_socket_plan_for_role()`에서 `managed_connections = 1, 64, 65, 128, 129, 512,
  513, 999, 2049`를 넣어 profile별 HWM을 검증한다.
- connection bucket 적용 flag가 꺼져 있으면 기존 HWM 계산이 유지되는지 검증한다.
- manual `SNDHWM` / `RCVHWM`이 있으면 bucket 정책이 덮어쓰지 않는지 검증한다.
- `message_unit = 1 KiB`, `4 KiB`, `64 KiB`, `128 KiB`에서 bucket 값이 `4 KiB` 기준 바이트
  예산으로 환산되는지 검증한다. 큰 메시지에서 minimum HWM을 무조건 보장해 바이트 예산을
  깨뜨리는 구현은 실패로 본다.
- SPOT routed small-message latency 보정이 connection bucket 상한을 넘기지 않는지 검증한다.

### SPOT runtime 테스트

- peer 연결 수가 늘어난 뒤 `mesh_pub`, `mesh_xsub`, `external_router`의 적용 HWM이 bucket에 맞게
  줄어드는지 확인한다.
- peer 연결 수가 줄어든 뒤 hysteresis 경계를 벗어난 경우에만 HWM이 커지는지 확인한다.
- profile 변경과 message unit 변경은 bucket이 같아도 재계산되는지 확인한다.

### 회귀 테스트

- 기존 SPOT publish/subscribe scenario
- routed request/reply scenario
- STREAM 관련 테스트
- `test_ctx_options`
- monitoring contract 테스트
- C perf runner의 auto-HWM detail 출력 테스트

## 성능 확인 기준

최소한 아래 조건을 비교한다.

| 조건 | 확인 내용 |
|------|-----------|
| 10-node mesh | 기존 대비 throughput 회귀가 큰지 확인 |
| 100-node mesh | `balanced`에서 노드당 PUBSUB queue 예산이 약 100 MiB 수준인지 확인 |
| 1000-node 산식 검증 | `balanced`, `4 KiB`에서 PUBSUB 약 250 MiB, router 포함 약 500 MiB인지 확인 |
| small payload perf | 작은 메시지에서 bucket cap 때문에 지나치게 drop이 늘지 않는지 확인 |
| large payload perf | 큰 메시지에서 `4 KiB` 기준 bucket 예산과 cap이 의도대로 동작하는지 확인 |

1000-node 실제 실행이 어렵다면 planner 단위 테스트와 runtime snapshot 테스트로 산식을 먼저
고정하고, 별도 장기 perf에서 실제 RSS와 throughput을 측정한다.

### C multi perf 실행 절차

SPOT 성능 확인은 C multi runner를 기준으로 한다.

현재 비교 기준 baseline은 다음 파일로 둔다.

```text
bindings/c/perf/baseline/perf_c_multi_linux_20260619_053400.txt
```

이 baseline의 기록 조건은 Linux, Release build, commit `db4ca1ec3`, `runs=1`,
`clients=100`, auto-HWM `balanced`, message sizes `64,256,1024,4096,65536,131072`,
patterns `MULTI_DEALER_DEALER,MULTI_DEALER_ROUTER,MULTI_ROUTER_ROUTER,MULTI_PUBSUB,
MULTI_SPOT,MULTI_SPOT_REQREP,MULTI_SPOT_SENDSEND,MULTI_STREAM`, transports
`tcp,tls,ws,wss`이다. 새 결과는 같은 runner와 같은 조건을 우선 맞춰 비교한다.

성능 측정은 같은 코드와 같은 장비에서도 5% 이상의 오차가 발생할 수 있다. 따라서 단일 실행에서
5% 안팎으로 움직인 값은 회귀로 단정하지 않는다. 5%를 넘는 차이가 보이면 같은 조건으로 재실행해
반복되는지 확인하고, SPOT 계열 핵심 지표가 반복해서 10% 이상 나빠질 때 bucket 값이나 적용 범위를
다시 조정한다.

```bash
cmake --build core/build
./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT --transport tcp --msg-sizes 4096
./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_REQREP --transport tcp --msg-sizes 4096
./bindings/c/perf/run_benchmarks_multi.sh --pattern SPOT_SENDSEND --transport tcp --msg-sizes 4096
```

`bindings/c/perf/run_benchmarks_multi.sh`는 `core/build`의 runtime library를 사용한다. core
source를 바꾼 뒤 `cmake --build core/build`를 먼저 실행하지 않은 수치는 비교 근거로 쓰지
않는다. runner가 출력하는 실제 `libzlink.so` 경로를 확인한다.

large payload 회귀는 같은 runner로 SPOT 계열 subset을 다시 돌려 확인한다.

```bash
./bindings/c/perf/run_benchmarks_multi.sh \
  --pattern SPOT,SPOT_REQREP,SPOT_SENDSEND \
  --transport tcp \
  --msg-sizes 4096,65536,131072
```

처음부터 전체 suite를 돌리지 않는다. 먼저 SPOT TCP subset으로 HWM 변화와 throughput 회귀를
확인하고, bucket 값이 안정되면 전체 multi suite를 실행한다. 전체 suite는 기본 pattern과
transport 조합이 많아 오래 걸리므로 최종 회귀 확인 단계에서 사용한다.

perf 출력에서는 결과 수치뿐 아니라 `Auto-HWM spotnode`와 `Auto-HWM spot handles` 표를 함께
확인한다.

| 확인 항목 | 기준 |
|-----------|------|
| `MsgUnit(B)` | 실행 중인 payload size와 같아야 한다. |
| `SndHWM` / `RcvHWM` | peer 수 bucket과 message unit 환산 결과와 맞아야 한다. |
| `SndBuf(KB)` / `RcvBuf(KB)` | 기본 경로에서는 `SNDBUF` / `RCVBUF=-1` 정책을 반영해야 한다. |
| SPOT routed row | small-message latency 보정이 connection bucket 상한을 넘기지 않아야 한다. |

## 완료 조건

- `SNDBUF` / `RCVBUF` 기본 `-1` 정책은 유지된다.
- SPOT mesh data path는 연결 수 bucket에 따라 HWM이 줄어든다.
- 1000-node, `balanced`, `4 KiB` 기준 PUBSUB queue 예산이 노드당 약 250 MiB 수준으로
  계산된다.
- 일반 `4 KiB` 메시지 기준 HWM이 `1`까지 떨어지는 단순 나눗셈 정책은 쓰지 않는다.
- 수동 HWM 설정은 기존처럼 항상 우선한다.
- 정식 spec과 guide는 실제 구현과 테스트가 끝난 뒤에만 갱신된다.
