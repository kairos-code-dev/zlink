[스펙 목차](../README.ko.md)

# Draft -- socket 기본 HWM 및 SPOT admission HWM 정책

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`와 정식 spec 문서에 없는
> API, enum, 기본 동작을 보장하지 않는다.
> 구현과 공개 헤더, 관련 테스트, 정식 문서가 확정되면 적절한 spec 문서로
> 나누어 반영한다.

## 1. 목적

이 초안은 일반 socket의 기본 HWM 모드와 `SpotNode`와 `Spot` 사이의 HWM 적용 범위를
단순하게 정리한다.
핵심 결정은 아래와 같다.

- HWM은 `Spot`이 `SpotNode`로 메시지를 밀어 넣는 admission control이다.
- SPOT 서비스에서 사용자가 조절할 수 있는 HWM은 `SpotNode`의 논리 채널별 admission
  HWM뿐이다.
- `Spot` 자체에는 HWM 설정 API를 열지 않는다.
- `SpotNode`의 사용자 설정 채널은 `router`, `pubsub` 두 가지로 제한한다.
- 실제 적용은 `Spot`의 send 방향과 `SpotNode`의 recv 방향에만 강제한다.
- `SpotNode` 내부 relay, external router, 최종 local delivery는 HWM `0`으로 고정한다.
- HWM 설정 변경은 이후 생성되는 `Spot`에만 적용한다.
  이미 생성된 `Spot`의 socket HWM은 바꾸지 않는다.
- 일반 socket의 기본값은 auto-HWM `balanced`로 바꾼다.
  계산식은 변경하지 않고, 기존 message unit 기반 계산과 profile별 cap 정책을
  그대로 사용한다.
- 사용자는 일반 socket과 `SpotNode` admission HWM 모두에서 명시 override를 사용할 수
  있다. 일반 socket에서는 기존 HWM option과 profile, message unit 설정을 사용하고,
  SPOT에서는 `SpotNode` admission option만 사용한다.

이 정책의 목표는 중간 relay 경로에서 HWM 때문에 메시지가 누락되거나 실패하는
상황을 없애고, 사용자가 이해할 수 있는 입구 지점에서만 압력을 제어하는 것이다.

## 2. 배경

`SpotNode`에는 사용자 API 경계와 내부 relay 경로가 함께 있다. 이 둘을 같은 HWM
정책으로 처리하면 문제가 생긴다.

사용자 API 경계에서는 HWM이 의미가 있다. `Spot`이 너무 빨리 보내면 send 호출에서
압력이 보이고, 사용자는 재시도하거나 실패 처리할 수 있다.

반대로 `SpotNode` 내부 relay는 이미 수락한 메시지를 전달하는 중간 경로이다.
여기서 HWM 때문에 send가 실패하면 사용자가 직접 재시도할 수 없는 위치에서 메시지
전달이 끊긴다. blocking send로 바꾸면 느린 peer나 느린 `Spot` 하나가 전체 relay를
멈출 수 있다.

따라서 이 초안은 HWM 적용 지점을 아래처럼 나눈다.

```text
+----------+     +----------+     +----------+     +----------+
| Spot     | --> | Node     | --> | Relay    | --> | Target   |
| sender   |     | ingress  |     | path     |     | receiver |
+----------+     +----------+     +----------+     +----------+
     bounded           bounded             0              0
```

`Spot sender`와 `Node ingress`는 사용자가 조절할 수 있는 압력 경계이다.
`Relay path`와 `Target receiver`는 이미 수락한 메시지를 전달하는 구간이므로
HWM 제한을 두지 않는다.

## 3. 설계 원칙

### 3.1 HWM은 admission control이다

SPOT에서 HWM은 내부 relay 메모리 예산이나 peer별 delivery 제한이 아니다.
HWM은 `Spot`이 `SpotNode`로 새 메시지를 넣는 속도를 제한하는 admission control이다.

이 정의를 기준으로 하면 사용자가 조절할 수 있는 값은 `SpotNode` 입구에만 있다.
사용자는 내부 socket 이름, relay 방향, inproc endpoint를 알 필요가 없다.

### 3.2 relay와 delivery는 HWM 0으로 고정한다

`SpotNode`가 이미 받은 메시지는 중간 경로에서 HWM 실패로 누락되면 안 된다.
따라서 아래 구간은 HWM `0`으로 고정한다.

- `SpotNode`에서 `Spot`으로 들어가는 최종 local delivery
- `SpotNode`의 external router send/recv
- `SpotNode` 내부 pub/sub fanout relay
- `SpotNode` mesh relay
- 그 밖에 사용자가 직접 재시도할 수 없는 중간 전달 경로

여기서 HWM `0`은 제한을 두지 않는다는 뜻이다. 무제한 queue 사용을 권장한다는 뜻이
아니라, 중간 relay에서 HWM 실패를 만들지 않겠다는 의미이다. 전체 입력 압력은
`Spot -> SpotNode` admission HWM으로 조절한다.

### 3.3 SpotNode admission 기본값은 profile balanced이다

`SpotNode` admission HWM 설정은 profile 기반으로 동작한다. 기본 profile은
`balanced`이다. 사용자는 `router`, `pubsub` admission 채널별로 profile을 바꾸거나,
필요한 경우 숫자 HWM으로 override할 수 있다.

이 profile은 `SpotNode` admission HWM의 기본값을 결정한다. 일반 socket도 기본
profile은 `balanced`로 바뀌지만, 일반 socket의 auto-HWM 계산식은 바꾸지 않는다.
일반 socket은 기존처럼 message unit, socket 역할, profile별 min/max cap을 사용해
HWM을 계산한다.

relay와 delivery socket은 `SpotNode` admission profile의 영향을 받지 않고 항상
HWM `0`이다.

초기 권장값은 아래와 같다.

| Profile | Admission HWM |
|---|---:|
| `low_latency` | 8 |
| `balanced` | 16 |
| `throughput` | 32 |

값이 작을수록 `Spot` send에서 압력이 빨리 보인다. 값이 클수록 burst를 더 흡수하지만
`SpotNode` 내부 relay에 쌓일 수 있는 메시지도 늘어난다.

## 4. 적용 모델

### 4.1 router admission

router admission은 routed request/reply 또는 routed send가 `Spot`에서 `SpotNode`로
들어오는 입구를 뜻한다.

| 구간 | 방향 | HWM |
|---|---|---:|
| `Spot` routed sender | send | router admission HWM |
| `SpotNode` routed ingress | recv | router admission HWM |
| `SpotNode` routed relay / external router | send/recv | 0 |
| `SpotNode -> Spot` routed delivery | send | 0 |
| `Spot` routed receiver | recv | 0 |

`Spot`에서 router admission HWM이 8이면, `Spot`의 send 방향 HWM과 `SpotNode`의
recv 방향 HWM이 모두 8로 설정된다. 반대 방향인 `SpotNode` send와 `Spot` recv는
0으로 고정한다.

### 4.2 pubsub admission

pubsub admission은 publish 메시지가 `Spot`에서 `SpotNode`로 들어오는 입구를 뜻한다.

| 구간 | 방향 | HWM |
|---|---|---:|
| `Spot` publisher | send | pubsub admission HWM |
| `SpotNode` pub/sub ingress | recv | pubsub admission HWM |
| `SpotNode` local fanout / mesh relay | send/recv | 0 |
| `SpotNode -> Spot` subscriber delivery | send | 0 |
| `Spot` subscriber | recv | 0 |

`Spot`에서 pubsub admission HWM이 16이면, `Spot` publisher의 send 방향 HWM과
`SpotNode` pub/sub ingress recv 방향 HWM이 모두 16으로 설정된다. subscriber가
읽는 최종 local inbound queue는 0으로 고정한다.

### 4.3 external router와 relay

external router는 특정 사용자 API 호출자가 직접 재시도할 수 있는 경계가 아니다.
`SpotNode`가 이미 수락한 메시지를 외부 peer로 전달하는 중간 경로이다.
따라서 external router의 send/recv HWM은 0으로 고정한다.

이 정책은 느린 peer 하나를 HWM 실패로 처리하지 않는다. 대신 전체 유입 압력은
`Spot -> SpotNode` admission HWM으로 앞단에서 제한한다. 이 초안의 구현 범위에는
peer별 pending queue나 congestion state를 추가하지 않는다. 더 강한 peer별 혼잡
제어는 이 초안에서 구현하지 않는다.

### 4.4 일반 socket HWM 정책

일반 socket은 기본값만 바뀐다. `PAIR`, `PUB`, `SUB`, `DEALER`, `ROUTER`,
`STREAM` 같은 기본 socket은 기존 auto-HWM 계산식을 유지하되, 기본 상태가
auto-HWM `balanced`가 된다.

일반 socket의 HWM은 기존 정책대로 아래 입력값을 사용해 계산한다.

- socket 역할과 타입
- effective message unit
- profile별 min/max cap
- 명시적 `ZLINK_OPT_SNDHWM`, `ZLINK_OPT_RCVHWM` override

사용자가 명시적으로 `ZLINK_OPT_SNDHWM` 또는 `ZLINK_OPT_RCVHWM`을 설정하면 그 값이
auto-HWM 계산값보다 우선한다. 사용자가 profile이나 message unit을 바꾸면 기존
계산식에 그 입력값이 반영된다.

따라서 일반 socket에 대해 `balanced` profile을 선택해도 HWM이 단순히 16으로
고정되는 것은 아니다. 일반 socket은 기존 계산식으로 산출된 값을 사용한다.
이 초안에서 8, 16, 32로 제시한 값은 `SpotNode` admission HWM의 기본값이다.

## 5. public API / enum 영향

이 초안은 `SpotNode` HWM 설정을 socket 방향별 옵션에서 admission 채널별 옵션으로
정리한다. 일반 socket은 public API와 auto-HWM 계산식은 유지하되, 기본값을
auto-HWM `balanced`로 바꾼다.
아래 public 이름은 구현 기준이다. enum 숫자는 `core/include/zlink_enum.h`에서
정하되, 제거 또는 대체된 기존 enum 숫자는 예약 상태로 남기고 다른 의미로
재사용하지 않는다.

### 5.1 추가할 public surface

| 구분 | 초안 이름 | SpotNode admission HWM | 의미 |
|---|---|---:|---|
| enum | 기존 `zlink_auto_hwm_profile_t` 사용 | - | 새 profile enum 추가 없음 |
| enum value | `ZLINK_AUTO_HWM_PROFILE_LOW_LATENCY` | 8 | 작은 admission queue |
| enum value | `ZLINK_AUTO_HWM_PROFILE_BALANCED` | 16 | 기본 admission profile |
| enum value | `ZLINK_AUTO_HWM_PROFILE_THROUGHPUT` | 32 | 큰 admission queue |
| spot node option | `ZLINK_SPOT_NODE_OPT_ROUTER_HWM_PROFILE` | `BALANCED` | router admission profile |
| spot node option | `ZLINK_SPOT_NODE_OPT_ROUTER_HWM` | profile 값 | router admission 숫자 override |
| spot node option | `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM_PROFILE` | `BALANCED` | pubsub admission profile |
| spot node option | `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM` | profile 값 | pubsub admission 숫자 override |

숫자 override가 설정되면 해당 채널의 profile 기본값보다 우선한다. admission HWM
숫자 override는 양수만 유효하다. `ZLINK_SPOT_NODE_OPT_ROUTER_HWM` 또는
`ZLINK_SPOT_NODE_OPT_PUBSUB_HWM`에 `0`을 설정하면 숫자 override를 해제하고 해당
채널 profile 값으로 돌아간다. 이 규칙 때문에 SpotNode admission HWM은 숫자
override로 HWM `0`을 설정할 수 없다. HWM `0`은 relay와 delivery 고정 정책에만
사용한다.

일반 socket에서 사용하는 `ZLINK_OPT_SNDHWM`, `ZLINK_OPT_RCVHWM`,
`ZLINK_OPT_AUTO_HWM_MSG_UNIT_BYTES`, `ZLINK_CTX_OPT_AUTO_HWM_PROFILE`의 의미는 이
초안에서 변경하지 않는다. 다만 context의 기본 동작은 auto-HWM enabled와
`ZLINK_AUTO_HWM_PROFILE_BALANCED`를 사용하도록 바꾼다. 수동 HWM을 명시한 socket은
기존처럼 그 값이 우선한다.

### 5.2 변경할 기본값

| 이름 | 기존 기본값 | 새 기본값 | 영향 |
|---|---:|---:|---|
| `ZLINK_CTX_AUTO_HWM_ENABLE_DFLT` | `0` | `1` | 일반 socket 기본 HWM 모드가 auto-HWM이 됨 |
| `ZLINK_CTX_AUTO_HWM_PROFILE_DFLT` | `ZLINK_AUTO_HWM_PROFILE_BALANCED` | 유지 | 기본 profile은 balanced |
| 일반 socket `SNDHWM` / `RCVHWM` | 수동 HWM 기본값 | auto-HWM 계산값 | 기존 계산식으로 산출 |
| SpotNode router admission profile | 방향별 routed HWM option | `ZLINK_AUTO_HWM_PROFILE_BALANCED` | 기본 admission HWM 16 |
| SpotNode pubsub admission profile | 방향별 pub/sub HWM option | `ZLINK_AUTO_HWM_PROFILE_BALANCED` | 기본 admission HWM 16 |

`ZLINK_CTX_OPT_AUTO_HWM_ENABLE`은 제거하지 않는다. 기본값만 enabled로 바꾼다.
사용자가 이 option을 `0`으로 설정하면 일반 socket auto-HWM을 명시적으로 끌 수 있다.
이는 일반 socket 기본값에 대한 명시적 해제이다. `SpotNode` admission HWM은 별도
`SpotNode` option으로 제어하며, `SpotNode` relay와 delivery HWM `0` 고정 정책은
context auto-HWM enable 값과 무관하게 유지한다.

### 5.3 대체할 SpotNode HWM option

기존 방향별 HWM option은 public 계약에서 제거하고, admission 채널별 option으로
대체한다.

| 기존 이름 | 대체 이름 | 변경 이유 |
|---|---|---|
| `ZLINK_SPOT_NODE_OPT_PUB_HWM` | `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM` | pub/sub admission 하나로 통합 |
| `ZLINK_SPOT_NODE_OPT_SUB_HWM` | `ZLINK_SPOT_NODE_OPT_PUBSUB_HWM` | subscriber recv HWM 설정 오해 제거 |
| `ZLINK_SPOT_NODE_OPT_ROUTED_SEND_HWM` | `ZLINK_SPOT_NODE_OPT_ROUTER_HWM` | router admission 하나로 통합 |
| `ZLINK_SPOT_NODE_OPT_ROUTED_RECV_HWM` | `ZLINK_SPOT_NODE_OPT_ROUTER_HWM` | routed recv HWM 설정 오해 제거 |

기존 option 이름은 호환 이름으로 남기지 않는다. 호환성 유지를 목표로 하지 않기
때문이다. `PUB_HWM`, `SUB_HWM`, `ROUTED_SEND_HWM`, `ROUTED_RECV_HWM`의 enum 숫자는
예약 상태로 남기고 다른 의미로 재사용하지 않는다.

### 5.4 제거할 hard limit option과 macro

| 기존 이름 | 구분 | 변경 방향 |
|---|---|---|
| `ZLINK_SPOT_NODE_OPT_SUB_QUEUE_HARD_LIMIT` | spot node option | 제거 |
| `ZLINK_SPOT_NODE_OPT_ROUTED_QUEUE_HARD_LIMIT` | spot node option | 제거 |
| `ZLINK_SPOT_NODE_SUB_QUEUE_HARD_LIMIT_DFLT` | macro | 제거 |
| `ZLINK_SPOT_NODE_ROUTED_QUEUE_HARD_LIMIT_DFLT` | macro | 제거 |

hard limit option은 새 정책에서 의미가 없다. local inbound queue가 깊어졌다는
이유로 target이나 routed recv plane을 disconnect하지 않기 때문이다.
이 option들의 enum 숫자도 예약 상태로 남기고 다른 의미로 재사용하지 않는다.

### 5.5 Spot HWM 설정 금지

`Spot` handle에는 별도 HWM 설정 API를 제공하지 않는다. `Spot`의 send 방향 HWM은
생성 시점에 `SpotNode` admission 설정을 캡처해 적용한다. `Spot`의 recv 방향 HWM은
0으로 고정한다.

`SpotNode` HWM 설정을 변경해도 이미 생성된 `Spot`에는 적용하지 않는다. 새 설정은
변경 이후 생성되는 `Spot`부터 적용한다. 이 규칙은 ZeroMQ 계열 HWM이 연결과 socket
생성 시점의 의미를 강하게 가지기 때문에 필요하다.

## 6. hard limit disconnect 제거

local inbound queue가 깊어졌다는 이유로 `Spot` 또는 routed recv plane을 disconnect하지
않는다. 제거 대상 의미는 아래와 같다.

- per-spot pending message count hard limit 초과
- 해당 target을 disconnected set에 넣는 동작
- routed recv queue pending count hard limit 검사
- hard limit 초과 시 `EAGAIN`을 연결 해제 원인으로 바꾸는 동작
- hard limit 초과 시 routed recv plane을 close하고 disconnected로 mark하는 동작

byte 단위 staging pause/resume은 별도 문제이다. 구현이 메모리 폭주를 피하기 위해
node 내부 poller 입력을 잠시 멈추는 것은 허용할 수 있다. 다만 그 동작은
disconnect가 아니라 내부 흐름 제어여야 한다.

## 7. auto-HWM과 snapshot 출력 기준

auto-HWM planner는 admission socket과 relay/delivery socket을 구분해야 한다.

| 값 | 의미 |
|---|---|
| bounded 양수 | admission HWM이 적용된 방향 |
| `0` | 방향은 있지만 HWM 제한이 없는 relay/delivery 방향 |
| `-` | 해당 socket 타입에서 그 방향이 의미 없음 |

perf와 monitoring snapshot은 `0`과 `-`를 섞으면 안 된다. 예를 들어 `PUB`의 recv
방향은 데이터 수신 방향이 아니므로 `-`로 표시할 수 있다. 반면 `Spot SUB`의 recv
방향은 의미가 있지만 제한하지 않으므로 `0`으로 표시해야 한다.

## 8. 설계 대안 검토

### 8.1 대안 A: 모든 socket에 SpotNode admission HWM 적용

이 방식은 설명이 단순해 보인다. `SpotNode` admission profile의 8, 16, 32 값을
일반 socket과 내부 relay socket까지 그대로 적용한다.

하지만 일반 socket의 기존 auto-HWM 계산식을 우회하게 되고, relay와 delivery
경로에서도 HWM 실패가 발생한다. 사용자가 직접 제어할 수 없는 중간 경로에서
메시지가 막히거나 실패하므로 선택하지 않는다.

### 8.2 대안 B: external peer별 pending queue 추가

이 방식은 peer별 backpressure를 정교하게 다룰 수 있다. HWM에 걸린 peer만 pending
queue에 넣고, `POLLOUT` 또는 send-ready 이벤트 때 재전송할 수 있다.

단점은 peer별 congestion state, queue cap, recovery event, monitoring 계약이 모두
필요하다는 점이다. 현재 목표보다 복잡하다. 이 초안은 이 대안을 선택하지 않는다.

### 8.3 대안 C: SpotNode admission HWM만 사용자 제어

이 방식은 HWM 의미가 분명하다. 사용자는 `SpotNode`의 `router`, `pubsub` admission
HWM만 설정하고, 라이브러리는 실제 socket 방향을 내부에서 강제한다. 중간 relay와
최종 delivery는 HWM 0으로 고정한다.

단점은 느린 peer나 느린 receiver 때문에 내부 queue가 증가할 수 있다는 점이다.
하지만 그 압력은 `Spot -> SpotNode` 입구의 작은 HWM으로 앞단에서 조절한다. 이
초안은 이 대안을 선택한다.

일반 socket은 이 대안의 적용 대상이 아니다. 일반 socket은 기본 profile만
auto-HWM `balanced`로 바뀌고, HWM 값은 기존 계산식대로 계산한다.

## 9. 구현 전 확인 항목

구현 계획을 작성할 때는 아래 항목을 반드시 확인한다.

- `SpotNode` public HWM 옵션을 `router`, `pubsub` admission 채널로만 노출한다.
- `SpotNode` admission 기본 profile은 `balanced`로 둔다.
- profile 값과 숫자 override 우선순위를 명확히 구현한다.
- 일반 socket 기본값은 auto-HWM enabled와 `balanced` profile로 바꾼다.
- 일반 socket auto-HWM 계산식과 기존 public option 의미는 유지한다.
- `ZLINK_CTX_OPT_AUTO_HWM_ENABLE=0`일 때 일반 socket auto-HWM이 꺼지는지 확인한다.
- `ZLINK_CTX_OPT_AUTO_HWM_ENABLE=0`이 `SpotNode` admission HWM과 relay/delivery HWM
  `0` 고정 정책을 바꾸지 않는지 확인한다.
- 명시적 일반 socket HWM override가 auto-HWM 계산값보다 우선하는지 확인한다.
- `Spot` 생성 시점에 현재 `SpotNode` admission HWM을 캡처한다.
- 이미 생성된 `Spot`에는 이후 HWM 설정 변경을 적용하지 않는다.
- `Spot` send HWM과 `SpotNode` recv HWM이 같은 admission 값으로 설정되는지 확인한다.
- `SpotNode` send HWM과 `Spot` recv HWM은 0으로 고정한다.
- external router, local fanout, mesh relay HWM은 0으로 고정한다.
- per-spot local fanout pending count disconnect 경로를 제거한다.
- routed recv queue hard limit disconnect 경로를 제거한다.
- `ZLINK_SPOT_NODE_OPT_*_QUEUE_HARD_LIMIT`와 관련 default macro를 public header,
  binding, 문서에서 제거한다.
- 제거 또는 대체된 SpotNode option enum 숫자를 다른 의미로 재사용하지 않는다.
- perf Auto-HWM detail 출력에서 `0`과 `-`를 구분한다.

## 10. 회귀테스트 기준

구현 후에는 최소한 아래 회귀테스트가 필요하다.

| 테스트 | 기대 결과 |
|---|---|
| SpotNode 기본 router profile | `balanced` |
| SpotNode 기본 pubsub profile | `balanced` |
| router profile별 admission HWM | `8`, `16`, `32` |
| pubsub profile별 admission HWM | `8`, `16`, `32` |
| 일반 socket 기본 HWM 모드 | auto-HWM enabled |
| 일반 socket 기본 profile | `balanced` |
| `ZLINK_CTX_AUTO_HWM_ENABLE_DFLT` | `1` |
| `ZLINK_CTX_OPT_AUTO_HWM_ENABLE=0` | 일반 socket auto-HWM 비활성화 |
| `ZLINK_CTX_OPT_AUTO_HWM_ENABLE=0` + SpotNode | SpotNode admission/relay 정책 유지 |
| 일반 socket auto-HWM 계산 | 기존 계산식과 동일 |
| 일반 socket `balanced` profile | message unit 기반 산출값 유지 |
| 일반 socket 명시 HWM override | auto-HWM 계산값보다 우선 |
| router 숫자 override | profile 값보다 우선 |
| pubsub 숫자 override | profile 값보다 우선 |
| Spot 생성 후 SpotNode HWM 변경 | 기존 Spot에는 미적용 |
| Spot 생성 후 SpotNode HWM 변경 | 새 Spot에는 적용 |
| Spot routed sender SNDHWM snapshot | router admission HWM |
| SpotNode routed ingress RCVHWM snapshot | router admission HWM |
| Spot publisher SNDHWM snapshot | pubsub admission HWM |
| SpotNode pub/sub ingress RCVHWM snapshot | pubsub admission HWM |
| SpotNode routed/local delivery SNDHWM snapshot | `0` |
| Spot routed/subscriber RCVHWM snapshot | `0` |
| external router SNDHWM/RCVHWM snapshot | `0` |
| local fanout / mesh relay HWM snapshot | `0` |
| pub/sub local fanout pending count 초과 | target disconnect 없음 |
| routed local recv pending count 초과 | routed recv plane disconnect 없음 |
| 제거된 SpotNode option 이름 | public header와 binding surface에 없음 |
| 제거된 SpotNode option enum 숫자 | 다른 의미로 재사용하지 않음 |
| dispatch callback 후 nonblocking drain | readable 이벤트 뒤 실제 drain 가능 |
| perf Auto-HWM detail | `0`과 `-`가 구분되어 출력 |

테스트는 fake 성공 경로를 허용하지 않는다. pending count 초과 테스트는 실제로
기존 hard limit보다 많은 메시지를 local inbound에 쌓은 뒤, 대상이 disconnect되지
않고 나중에 drain 가능한지 확인해야 한다.

## 11. 문서 반영 방향

구현이 끝난 뒤 정식 문서는 아래처럼 나누어 반영한다.

- `doc/spec/core/service/spot.ko.md`
  public 옵션 의미, 삭제된 hard limit 옵션, HWM 적용 범위를 계약으로 설명한다.
- `doc/guide/07-3-spot.ko.md`
  사용자 관점에서 admission HWM profile, 숫자 override, dispatch callback 뒤
  nonblocking drain 책임을 설명한다.
- `doc/internals/spot-internals.ko.md`
  내부 소켓 배선과 relay/delivery HWM `0` 정책을 다이어그램으로 설명한다.
- `doc/spec/bindings/*`
  새 SpotNode admission HWM 옵션과 제거된 hard limit 옵션을 binding surface에 맞춰
  반영한다.

정식 문서 반영 전에는 이 초안 내용을 기존 spec에 섞지 않는다.
