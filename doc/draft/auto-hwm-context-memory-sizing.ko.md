[스펙 목차](../README.ko.md)

# Draft -- auto-HWM 개선 정책: context 메모리 산정과 단위 예산

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`와 정식 spec 문서에 없는
> API, 상수, 기본 동작을 보장하지 않는다.
> 구현과 공개 헤더, 관련 테스트, 정식 문서가 확정되면 적절한 spec 문서로
> 나누어 반영한다.

## 1. 목적

이 초안은 기존 auto-HWM 계산을 보완 옵션으로 남기는 문서가 아니라, 기존
계산 정책을 개선된 정책으로 대체하기 위한 기준을 정리한다. 사용자가 context
메모리 값을 어떻게 정해야 하는지, 그리고 라이브러리가 그 값을 HWM으로 어떻게
바꿔야 하는지를 정의한다.

이 문서의 핵심 방향은 아래와 같다.

- context 메모리는 HWM을 무조건 크게 만드는 값이 아니라, 전체 큐 메모리 상한이다.
- 사용자는 소켓별 HWM 숫자를 직접 계산하지 않고, client 수와 spot 수 기준으로
  context 메모리를 정한다.
- 라이브러리는 소켓 역할, client 수, spot 수, `MsgUnit(B)`을 기준으로 적절한
  HWM을 계산한다.
- 큰 메시지 one-way fanout에서 큐가 깊어져 latency가 초 단위로 커지는 상황을
  기본 정책에서 막는다.

## 2. 배경

기존 auto-HWM은 context 메모리 예산을 큐 슬롯으로 바꾸는 성격이 강했다.
이 방식은 메모리 사용량을 설명하기는 쉽지만, 작은 메시지에서는 HWM이 너무 크게
나오고 큰 메시지 fanout에서는 큐 체류 시간이 커질 수 있다.

따라서 이 초안의 방향은 기존 정책 위에 또 다른 예외 경로를 추가하는 것이
아니다. 기존 정책의 문제를 고쳐, 기본 auto-HWM 계산 자체가 역할, 연결 수,
spot 수, `MsgUnit(B)`에 맞게 동작하도록 바꾸는 것이다.

특히 one-way `PUB/SUB`와 `SPOT`은 요청/응답처럼 자연스러운 in-flight 제한이
없다. publish 쪽이 계속 밀어 넣으면 subscriber 쪽 큐가 깊어지고, payload에
찍힌 송신 시각과 수신 시각 차이는 큐 체류 시간을 포함한다. 따라서 throughput
테스트에서는 latency가 크게 보일 수 있다.

이 초안은 측정값 기반 adaptive 정책을 기본으로 쓰지 않는다. 평균 메시지 크기나
consumer 처리량을 계속 측정해서 HWM을 바꾸면 정책이 흔들리고 디버깅이 어려워질
수 있기 때문이다. 대신 사용자가 쉽게 예측할 수 있는 정적 단위 예산을 사용한다.

## 3. 사용자 메모리 산정 공식

기본 `balanced` profile에서 context 메모리 권장값은 아래 공식으로 계산한다.

```text
recommended_context_memory =
  64 MiB
  + pub_client_count * 1 MiB
  + spotnode_local_spot_count * 256 KiB
  + spotnode_client_count * 1 MiB
  + other_client_count * 512 KiB
```

계산 결과는 64 MiB 단위로 올림한다. 최소 권장값은 128 MiB이다.

각 항목의 뜻은 아래와 같다.

| 항목 | 의미 |
|---|---|
| `64 MiB` | context runtime, 내부 상태, transport 여유분을 위한 기본 예산 |
| `pub_client_count` | 일반 `PUB` / `XPUB` 소켓이 fanout하는 subscriber 수 |
| `spotnode_local_spot_count` | 한 SpotNode 안에 만들어진 local spot handle 수 |
| `spotnode_client_count` | SpotNode가 fanout하거나 mesh로 연결하는 peer/client 수 |
| `other_client_count` | `ROUTER`, `DEALER`, `SUB` 등 fanout이 아닌 client-facing 연결 수 |

이 공식은 정확한 RSS 예측식이 아니다. 사용자가 context budget을 정할 때 쓰는
운영 가이드이다. 실제 HWM은 이 budget 안에서 다시 소켓 역할과 `MsgUnit(B)`에
따라 낮아질 수 있다.

## 4. Profile별 단위 예산

기본 profile은 `balanced`로 둔다. profile이 공개 옵션으로 아직 노출되지 않은
구현에서는 `balanced` 값을 기본 정책으로 사용한다.

| Profile | PUB client | SpotNode client | Spot local spot | 그 외 client |
|---|---:|---:|---:|---:|
| `low_latency` | 512 KiB | 512 KiB | 128 KiB | 256 KiB |
| `balanced` | 1 MiB | 1 MiB | 256 KiB | 512 KiB |
| `throughput` | 4 MiB | 4 MiB | 512 KiB | 1 MiB |

각 profile의 의도는 아래와 같다.

- `low_latency`
  큐를 짧게 유지한다. backpressure가 더 자주 걸릴 수 있다.
- `balanced`
  기본 운영값이다. 큰 메시지 one-way fanout에서 초 단위 latency를 피하는 쪽을
  우선한다.
- `throughput`
  큐 여유를 크게 둔다. latency가 커질 수 있으므로 perf throughput 또는 별도
  튜닝이 필요한 환경에서만 사용한다.

## 5. 예시

### 5.1 PUB 100 clients

```text
64 MiB + 100 * 1 MiB = 164 MiB
```

권장 설정값은 192 MiB 또는 256 MiB이다.

### 5.2 SpotNode 1개, local spot 2개, peer/client 100개

```text
64 MiB + 2 * 256 KiB + 100 * 1 MiB
= 약 164.5 MiB
```

권장 설정값은 192 MiB 또는 256 MiB이다.

### 5.3 ROUTER 100 clients

```text
64 MiB + 100 * 512 KiB = 114 MiB
```

권장 설정값은 128 MiB이다.

### 5.4 256 KiB fanout에서 HWM 감각

`balanced` profile에서 PUB client 단위 예산은 1 MiB이다.

```text
1 MiB / 256 KiB = 4 messages
```

따라서 256 KiB 메시지를 100 clients로 fanout하는 기본 HWM은 client당 몇 개
수준으로 낮아진다. 이 값은 throughput을 무조건 최대로 밀기 위한 값이 아니라,
큰 메시지가 큐에 오래 쌓이는 상황을 피하기 위한 기본값이다.

## 6. 라이브러리 내부 HWM 계산식

라이브러리는 사용자가 설정한 context budget을 절대 상한으로 보고, 아래 순서로
HWM을 계산한다.

```text
context_budget = user configured context memory budget
queue_budget = context_budget - reserve_budget

socket_unit_budget = profile_unit_budget(socket_role)
socket_count = max(1, role_count(socket))

socket_budget_cap = socket_unit_budget * socket_count
socket_budget = min(socket_budget_cap, fair_share(queue_budget, socket))

per_connection_budget = socket_budget / socket_count
memory_hwm = per_connection_budget / MsgUnit(B)

effective_hwm =
  clamp(
    min(memory_hwm, size_class_cap(MsgUnit(B))),
    floor(socket_role, MsgUnit(B)),
    hard_cap(socket_role)
  )
```

`fair_share(queue_budget, socket)`은 같은 context 안의 auto-HWM 대상 소켓들이
전체 budget을 초과하지 않도록 나누어 주는 값이다. 구현은 단순하게 소켓 역할별
weight와 count를 사용한다.

```text
socket_weight = role_weight(socket_role) * socket_count
fair_share = queue_budget * socket_weight / total_weight
```

초기 weight는 아래처럼 둔다.

| Role | Weight |
|---|---:|
| `fanout` | 2 |
| `recv_ingress` | 1 |
| `routed` | 2 |
| `control` | 0.25 |

이 weight는 "메모리를 더 달라"는 의미가 아니라, 같은 context 안에서 어느 역할이
큐 pressure를 더 크게 만들 수 있는지를 나타내는 분배 기준이다.

## 7. MsgUnit별 HWM 상한

메모리 예산만으로 HWM을 계산하면 작은 메시지에서 값이 너무 커질 수 있다.
따라서 `MsgUnit(B)` 구간별 상한을 둔다.

| `MsgUnit(B)` | `size_class_cap` |
|---:|---:|
| `<= 1 KiB` | 8192 |
| `<= 4 KiB` | 4096 |
| `<= 16 KiB` | 2048 |
| `<= 64 KiB` | 512 |
| `<= 128 KiB` | 128 |
| `<= 256 KiB` | 64 |
| `> 256 KiB` | 32 |

최종 HWM은 `memory_hwm`과 `size_class_cap` 중 작은 값을 사용한다.
따라서 context 메모리를 크게 잡아도 큰 메시지 큐가 무한히 깊어지지 않는다.

## 8. SpotNode 적용 방식

SPOT은 외부 spot handle만 계산하면 안 된다. SpotNode 내부 소켓도 같은 정책으로
계산해야 한다.

| SPOT 대상 | Role | Count 기준 | HWM 방향 |
|---|---|---|---|
| spot pub handle | `fanout` | local subscriber 수 또는 peer 수 | `SNDHWM` |
| spot sub handle | `recv_ingress` | local publisher 수 또는 peer 수 | `RCVHWM` |
| `local-pub` | `fanout` | local sub spot 수 | `SNDHWM` |
| `mesh-pub` | `fanout` | active peer 수 | `SNDHWM` |
| `ingress-sub` | `recv_ingress` | local pub spot 수 | `RCVHWM` |
| `mesh-xsub` | `recv_ingress` | active peer 수 | `RCVHWM` |
| `internal-router` | `routed` | routed spot 수 | `SNDHWM`, `RCVHWM` |
| `external-router` | `routed` | active peer 수 | `SNDHWM`, `RCVHWM` |
| `peer_ctrl_pub/sub` | `control` | active peer 수 | fixed 또는 낮은 cap |

SpotNode 메모리 산정 가이드는 아래 항목을 쓴다.

```text
spotnode_local_spot_count * profile.spot_local_spot_bytes
+ spotnode_client_count * profile.spotnode_client_bytes
```

내부 적용은 아래 공개 옵션 또는 그에 대응하는 내부 경로를 사용한다.

```c
ZLINK_SPOT_NODE_OPT_PUB_HWM
ZLINK_SPOT_NODE_OPT_SUB_HWM
ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM
ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM
```

auto-HWM v2에서는 local fanout과 mesh fanout을 같은 숫자로 뭉개지 않고, 각 내부
소켓의 count 기준으로 따로 계산한다.

## 9. 수동 설정 우선순위

사용자가 HWM을 수동으로 설정한 방향은 auto-HWM이 덮어쓰지 않는다.

우선순위는 아래와 같다.

1. 개별 socket 또는 spot handle의 수동 HWM
2. SpotNode 옵션으로 설정한 수동 HWM
3. auto-HWM v2 계산값
4. 기존 socket type 기본값

perf runner에서 `--hwm`, `--send-hwm`, `--recv-hwm`을 주는 경우도 수동 설정으로
본다. 이 값은 외부 spot handle뿐 아니라 SpotNode 내부 데이터 소켓에도 전달되어야
한다.

## 10. Context budget이 권장값보다 작거나 클 때

권장 공식은 사용자가 처음 값을 고르는 가이드이다. 실제 설정값이 권장보다 작거나
커도 동작은 아래처럼 명확해야 한다.

- 권장값보다 작으면 HWM이 더 낮아지고, backpressure가 더 빨리 걸릴 수 있다.
- 권장값보다 크면 HWM이 어느 정도 커질 수 있지만, `size_class_cap`과 role hard
  cap이 상한을 막는다.
- `throughput` profile을 명시하지 않는 한, 큰 context budget만으로 큰 메시지
  큐가 깊어지면 안 된다.

즉 context budget은 전체 안전 상한이고, HWM의 기본 성격은 role, count,
`MsgUnit(B)`가 정한다.

## 11. 구현 체크리스트

- [ ] context budget 문서와 기본값을 `balanced` 기준으로 정리한다.
- [ ] auto-HWM 계산에서 context memory를 전체 cap으로만 사용한다.
- [ ] PUB, SUB, ROUTER, STREAM 등 일반 소켓 role mapping을 정리한다.
- [ ] SpotNode 내부 소켓별 role과 count 기준을 분리한다.
- [ ] `MsgUnit(B)` 구간별 `size_class_cap`을 추가한다.
- [ ] 수동 HWM 설정 방향은 auto 재계산이 덮어쓰지 않게 한다.
- [ ] perf runner의 SpotNode 수동 HWM 전달 경로를 회귀 테스트에 넣는다.
- [ ] one-way large message fanout perf에서 latency 해석을 throughput 모드와
  latency 모드로 분리한다.
