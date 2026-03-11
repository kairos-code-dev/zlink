# Service Option Surface 정리 계획

## 1. 목적

이 문서는 service facade의 public option surface를
현재 canonical public subject 기준으로 정리한다.

이 문서의 canonical 전제는 다음 두 문서다.

- [`direct-callback-recv-rewrite-spec.ko.md`](./direct-callback-recv-rewrite-spec.ko.md)
- [`spot-node-direct-facade-plan.ko.md`](./spot-node-direct-facade-plan.ko.md)

핵심 목표:

- internal socket role 중심 public API를 service 의미 중심으로 정리한다.
- callback-only recv 모델과 충돌하는 option을 surface에서 제거한다.
- `Gateway`, `SpotPub`, `SpotSub` 기준으로 설명을 통일한다.

## 2. 범위와 우선순위

### 2.1 범위

이 문서는 다음만 다룬다.

- 어떤 option family를 public surface에 남길지
- 어떤 option이 legacy/`ENOTSUP`/삭제 대상인지
- service-level naming 원칙

이 문서는 다음을 확정하지 않는다.

- RID identity 정책
- monitor/readiness event semantics
- `spot_node` direct facade의 세부 lifecycle

각각의 canonical 문서는 다음이다.

- RID 정책:
  [`service-routing-id-policy-plan.ko.md`](./service-routing-id-policy-plan.ko.md)
- monitor/readiness:
  [`service-monitor-readiness-plan.ko.md`](./service-monitor-readiness-plan.ko.md)
- `spot_node` direct facade 지원 범위:
  [`spot-node-direct-facade-plan.ko.md`](./spot-node-direct-facade-plan.ko.md)

### 2.2 public subject

현재 public option subject는 다음으로 고정한다.

- `Gateway`
- `SpotPub`
- `SpotSub`

두지 않는다.

- `Receiver`
- `SpotNode socket_role`
- internal `DEALER` role

`Discovery`는 1차 option surface 대상에 넣지 않는다.

## 3. 정리 원칙

### 3.1 naming

- service-level API를 우선한다.
- internal socket role 이름을 public에 직접 노출하지 않는다.
- representative RID는 generic option이 아니라 dedicated identity API로 분리한다.

### 3.2 callback-only recv와의 정렬

callback-only recv 모델로 의미가 사라진 항목은 삭제한다.

대표 예:

- `RCVTIMEO`
- public recv queue policy
- public recv queue full policy

반대로 send path나 transport tuning 의미가 남아 있는 항목은 유지 가능하다.

대표 예:

- `SNDHWM`
- `RCVHWM`
- `SNDTIMEO`
- `LINGER`
- `SNDBUF`
- `RCVBUF`

### 3.3 Spot legacy option 처리

`SpotPub`의 async queue 계열은 현재 canonical spec에서
public support 대상이 아니다.

즉 다음 항목은 enum 이름이 남더라도 동작 계약은 `ENOTSUP`다.

- `ZLINK_SPOT_PUB_OPT_MODE`
- `ZLINK_SPOT_PUB_OPT_QUEUE_HWM`
- `ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY`

`SpotSub`의 public recv queue 계열도 삭제 대상으로 본다.

- `ZLINK_SPOT_SUB_OPT_QUEUE_NODROP`
- `ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY`

## 4. 서비스별 option 표

### 4.1 Gateway

| 항목 | API/상수 | 처리 | 비고 |
|---|---|---|---|
| send HWM | `ZLINK_GATEWAY_OPT_SNDHWM` | 유지 | send path |
| recv HWM | `ZLINK_GATEWAY_OPT_RCVHWM` | 유지 | receive path tuning |
| send timeout | `ZLINK_GATEWAY_OPT_SNDTIMEO` | 유지 | send-side only |
| recv timeout | `ZLINK_GATEWAY_OPT_RCVTIMEO` | 삭제 | callback-only recv로 의미 없음 |
| linger | `ZLINK_GATEWAY_OPT_LINGER` | 유지 | close/destroy |
| send buffer | `ZLINK_GATEWAY_OPT_SNDBUF` | 유지 | socket buffer tuning |
| recv buffer | `ZLINK_GATEWAY_OPT_RCVBUF` | 유지 | socket buffer tuning |

추가 원칙:

- 과거 `receiver` option surface는 unified `gateway`로 흡수한다.
- internal monitor socket option과 raw `ROUTING_ID` generic option은 남기지 않는다.

### 4.2 SpotPub

| 항목 | API/상수 | 처리 | 비고 |
|---|---|---|---|
| send HWM | `ZLINK_SPOT_PUB_OPT_SNDHWM` | 유지 | publish path |
| send timeout | `ZLINK_SPOT_PUB_OPT_SNDTIMEO` | 유지 | publish path |
| linger | `ZLINK_SPOT_PUB_OPT_LINGER` | 유지 | destroy/close |
| nodrop | `ZLINK_SPOT_PUB_OPT_NODROP` | 유지 | slow peer policy |
| mode | `ZLINK_SPOT_PUB_OPT_MODE` | `ENOTSUP` | legacy async queue 정책 |
| queue hwm | `ZLINK_SPOT_PUB_OPT_QUEUE_HWM` | `ENOTSUP` | legacy async queue 정책 |
| queue full policy | `ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY` | `ENOTSUP` | legacy async queue 정책 |
| send buffer | `ZLINK_SPOT_PUB_OPT_SNDBUF` | 유지 | socket buffer tuning |
| recv buffer | `ZLINK_SPOT_PUB_OPT_RCVBUF` | 유지 | socket buffer tuning |

설명:

- `SNDBUF` / `RCVBUF`는 facade의 논리적 publish direction이 아니라
  underlying transport socket의 OS buffer tuning을 의미한다.
- 따라서 `SpotPub`에 `RCVBUF`가 남는 것은 "pub이 recv API를 가진다"는 뜻이 아니라,
  full-duplex transport layer의 receive buffer 크기를 조정할 수 있다는 의미다.

### 4.3 SpotSub

| 항목 | API/상수 | 처리 | 비고 |
|---|---|---|---|
| recv HWM | `ZLINK_SPOT_SUB_OPT_RCVHWM` | 유지 | fanout / mesh receive path |
| recv timeout | `ZLINK_SPOT_SUB_OPT_RCVTIMEO` | 삭제 | callback-only recv로 의미 없음 |
| linger | `ZLINK_SPOT_SUB_OPT_LINGER` | 유지 | destroy/close |
| queue nodrop | `ZLINK_SPOT_SUB_OPT_QUEUE_NODROP` | 삭제 | public recv queue 제거 |
| queue full policy | `ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY` | 삭제 | public recv queue 제거 |
| send buffer | `ZLINK_SPOT_SUB_OPT_SNDBUF` | 유지 | socket buffer tuning |
| recv buffer | `ZLINK_SPOT_SUB_OPT_RCVBUF` | 유지 | socket buffer tuning |

설명:

- `SNDBUF` / `RCVBUF`는 facade의 논리적 subscribe direction이 아니라
  underlying transport socket의 OS buffer tuning을 의미한다.
- 따라서 `SpotSub`에 `SNDBUF`가 남는 것도 "sub가 publish를 한다"는 뜻이 아니라,
  full-duplex transport layer의 send buffer 크기를 조정할 수 있다는 의미다.

## 5. 권장 public API 모양

1차 권장 모양은 service별 `set_option`이다.

```c
int zlink_gateway_set_option(void *gateway,
                             zlink_gateway_option_t option,
                             const void *optval,
                             size_t optvallen);

int zlink_spot_pub_set_option(void *pub,
                              zlink_spot_pub_option_t option,
                              const void *optval,
                              size_t optvallen);

int zlink_spot_sub_set_option(void *sub,
                              zlink_spot_sub_option_t option,
                              const void *optval,
                              size_t optvallen);
```

`get_option`은 1차 범위에 넣지 않는다.

이유:

- 현재 문제의 핵심은 설정 surface 단순화다.
- callback-only 전환과 같이 value normalization 정책까지 넓히면 범위가 커진다.

## 6. 현재 API 정리 방향

| 현재 API | 처리 방향 | 이유 |
|---|---|---|
| `zlink_gateway_setsockopt(gateway, option, ...)` | `zlink_gateway_set_option()`으로 정리 가능 | 이미 service-level API에 가깝다 |
| `zlink_receiver_setsockopt(...)` | 삭제 후 `gateway`로 흡수 | `receiver` public type 제거 |
| `zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_PUB, ...)` | `zlink_spot_pub_set_option()` 또는 node default pub option으로 재배치 | service 의미가 더 직접적이다 |
| `zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_SUB, ...)` | `zlink_spot_sub_set_option()` 또는 node default sub option으로 재배치 | service 의미가 더 직접적이다 |
| `zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER, ...)` | 제거 | internal control plane |
| raw `ROUTING_ID` generic option | generic option surface에서 제외 | RID 정책 문서로 분리 |

## 7. 예시

### 7.1 Gateway

```c
int sndhwm = 1024;
int rcvhwm = 1024;

zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_SNDHWM,
                         &sndhwm, sizeof(sndhwm));
zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_RCVHWM,
                         &rcvhwm, sizeof(rcvhwm));
```

### 7.2 SpotPub

```c
int sndhwm = 4096;
int nodrop = 1;

zlink_spot_pub_set_option(pub, ZLINK_SPOT_PUB_OPT_SNDHWM,
                          &sndhwm, sizeof(sndhwm));
zlink_spot_pub_set_option(pub, ZLINK_SPOT_PUB_OPT_NODROP,
                          &nodrop, sizeof(nodrop));
```

### 7.3 SpotSub

```c
int rcvhwm = 4096;
int linger_ms = 0;

zlink_spot_sub_set_option(sub, ZLINK_SPOT_SUB_OPT_RCVHWM,
                          &rcvhwm, sizeof(rcvhwm));
zlink_spot_sub_set_option(sub, ZLINK_SPOT_SUB_OPT_LINGER,
                          &linger_ms, sizeof(linger_ms));
```

## 8. 구현 우선순위

1. `Gateway`
- `receiver` option 흡수와 함께 canonical surface 확정

2. `SpotPub` / `SpotSub`
- legacy async queue / recv queue 정책 정리
- node-level default option과 facade option의 경계 확정

3. bindings
- language별 이름과 enum을 같은 기준으로 정렬

## 9. Definition of Done

- `Receiver`가 option 문서의 canonical public subject에서 제거되어 있다.
- `Gateway`, `SpotPub`, `SpotSub`만 public option surface로 남아 있다.
- callback-only recv와 모순되는 `RCVTIMEO`/recv queue 정책이 surface에서 제거되어 있다.
- `SpotPub` async queue 계열은 public support가 아니라 `ENOTSUP`로 명시되어 있다.
- option naming이 internal socket role이 아니라 service 의미를 따르고 있다.
