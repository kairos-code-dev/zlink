# Service Option Surface 정리 계획

## 1. 목적

이 문서는 service facade의 public option surface를
internal socket role 기준에서 service 기준으로 정리하기 위한 계획을 정의한다.

원래 `service-monitor-readiness-plan.ko.md` 안에 함께 있었지만,
review 범위를 분리하기 위해 별도 문서로 분리했다.

핵심 목표는 다음과 같다.

- 사용자가 internal socket 구조를 몰라도 option을 설정할 수 있게 한다.
- `Gateway`, `Receiver`, `SpotPub`, `SpotSub` 기준으로 public API를 단순화한다.
- 실제로 필요한 옵션만 남겨 public surface를 줄인다.
- perf/sample/documentation이 같은 service 의미를 사용하게 한다.

이 문서는 generic service option만 다룬다.
service identity인 `routing_id` 정책은 별도 문서에서 다룬다.

- `service-routing-id-policy-plan.ko.md`

같은 public subject는 monitor와 registry topology에서도 그대로 사용한다.

- `Gateway`
- `Receiver`
- `SpotPub`
- `SpotSub`

`Discovery`는 representative RID와 monitor 대상은 맞지만,
1차 option surface 대상에는 넣지 않는다.

이유:

- `Discovery`는 read-only consumer 성격이 강하다.
- 현재 public 사용면에서 반복적으로 요구되는 socket option이 거의 없다.
- option surface를 넓히기보다 monitor/readiness와 RID identity를 먼저 정리하는 편이 낫다.

즉 목표 상태는 다음 한 줄이다.

```text
socket-role option API -> service-level option API
```

## 2. 왜 필요한가

현재 일부 API는 internal socket role을 public에 그대로 노출한다.

예:

- `zlink_receiver_setsockopt(receiver, ZLINK_RECEIVER_SOCKET_ROUTER, ...)`
- `zlink_receiver_setsockopt(receiver, ZLINK_RECEIVER_SOCKET_DEALER, ...)`
- `zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_PUB, ...)`
- `zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_SUB, ...)`
- `zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER, ...)`

이 방식의 문제는 다음과 같다.

- 사용자가 internal socket 구조를 알아야 한다.
- public API가 owner/internal 구현에 종속된다.
- 내부 socket 구성이 바뀌면 public API 의미도 흔들린다.
- 실제 사용자는 “어느 socket인가”보다
  “이 service의 송수신 동작에 어떤 옵션을 줄 것인가”에 더 관심이 있다.

## 3. 검토 기준

이 문서의 판단 기준은 다음 두 가지다.

- 현재 public API가 어떤 socket role을 노출하는가
- `core/perf`에서 실제로 반복적으로 쓰는 옵션이 무엇인가

추가 전제:

- 이번 정리에서는 API 호환성을 유지할 필요가 없다.
- 따라서 새 service-level option API를 도입할 때
  기존 socket-role 기반 public API를 같은 작업 안에서 제거할 수 있다.

검토 결과,
실제로 반복적으로 쓰는 옵션은 매우 제한적이다.

공통적으로 유지 가치가 큰 옵션:

- `SNDHWM`
- `RCVHWM`
- `SNDTIMEO`
- `RCVTIMEO`
- `LINGER`

service 성격에 따라 추가 검토가 필요한 옵션:

- `SpotPub`의 `NODROP` 계열 옵션
- `SpotPub` async publish mode / queue HWM / queue full policy
- `SpotSub` local queue full policy

반대로 public에서 제외 가능한 항목:

- `SpotNode`/`Receiver` internal `DEALER` role
- raw `ROUTING_ID` generic option 노출
- raw `ROUTER_MANDATORY`
- internal monitor socket 옵션
- generic TLS socket option 노출

주의:

- `ROUTING_ID` 자체가 불필요하다는 뜻은 아니다.
- representative RID는 generic option이 아니라
  별도 identity 정책으로 다루는 편이 더 적절하다.

TLS는 기존 dedicated API가 더 적절하다.

- `zlink_gateway_set_tls_client`
- `zlink_receiver_set_tls_server`
- `zlink_spot_node_set_tls_client`
- `zlink_spot_node_set_tls_server`

## 4. 서비스별 옵션 표

아래 표는 “public으로 남길 옵션” 기준의 1차 초안이다.

| 서비스 | 현재 벤치에서 실제 쓰는 옵션 | public으로 남길 옵션 | public에서 제외할 것 | 비고 |
|---|---|---|---|---|
| `Gateway` | `SNDHWM`, `RCVHWM`, `SNDTIMEO`, `RCVTIMEO` | `SNDHWM`, `RCVHWM`, `SNDTIMEO`, `RCVTIMEO`, `LINGER` | internal monitor socket 옵션, raw `ROUTING_ID`, raw `ROUTER_MANDATORY` | 이미 service-level `setsockopt` 형태라 방향은 맞다 |
| `Receiver` | `ROUTER`에 `SNDHWM`, `RCVHWM`, `SNDTIMEO`, `RCVTIMEO`, 일부 `LINGER` | `SNDHWM`, `RCVHWM`, `SNDTIMEO`, `RCVTIMEO`, `LINGER` | `ZLINK_RECEIVER_SOCKET_DEALER` public 노출, registry/heartbeat dealer 옵션 | `DEALER`는 등록/heartbeat 내부 용도라 public target으로 둘 이유가 약하다 |
| `SpotPub` | `PUB`에 `SNDHWM`, `SNDTIMEO`, `LINGER`, `XPUB_NODROP`; 별도 async queue 옵션 | `SNDHWM`, `SNDTIMEO`, `LINGER`, `NODROP` 계열 1개, async `MODE`, `QUEUE_HWM`, `QUEUE_FULL_POLICY` | internal `DEALER` 옵션, `SpotNode socket_role` 노출 | queue 계열은 socket option보다 service option으로 보는 편이 맞다 |
| `SpotSub` | `SUB`에 `RCVHWM`, `RCVTIMEO`, `LINGER`, 현재는 `XPUB_NODROP`도 사용 | `RCVHWM`, `RCVTIMEO`, `LINGER`, 필요하면 local queue policy 1개 | internal `DEALER` 옵션, raw `XPUB_NODROP` 이름 그대로 노출 | 현재 `XPUB_NODROP`는 사실상 local sub queue 정책이라 이름 변경이 필요하다 |

## 5. Spot 계열에서 이름을 바로잡아야 하는 항목

특히 `Spot`은 현재 public 이름과 실제 의미가 어긋난 항목이 있다.

대표적으로 `SpotSub`에 대한 `XPUB_NODROP`는
“SUB socket의 raw XPUB option”이라기보다
local subscriber queue의 full-policy에 가깝다.

따라서 다음과 같이 정리하는 것이 좋다.

- 유지 가능:
  `SpotPub`의 publish-side `NODROP` 또는 strict backpressure 의미
- 이름 변경 필요:
  `SpotSub`의 local queue full-policy
- 제거:
  `SpotNode socket_role`을 알아야만 접근 가능한 public option path

권장 이름 예:

- `ZLINK_SPOT_PUB_OPT_NODROP`
- `ZLINK_SPOT_SUB_OPT_QUEUE_NODROP`
- `ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY`

즉, option 이름도 socket type이 아니라 service 의미를 따라가야 한다.

RID 관련 identity API는 이 문서가 아니라
`service-routing-id-policy-plan.ko.md`에서 별도로 정의한다.

## 6. 권장 public API 모양

1차 권장 모양은 generic socket-role 기반 API가 아니라
service별 option API다.

```c
int zlink_gateway_set_option(void *gateway,
                             int option,
                             const void *optval,
                             size_t optvallen);

int zlink_receiver_set_option(void *receiver,
                              int option,
                              const void *optval,
                              size_t optvallen);

int zlink_spot_pub_set_option(void *pub,
                              int option,
                              const void *optval,
                              size_t optvallen);

int zlink_spot_sub_set_option(void *sub,
                              int option,
                              const void *optval,
                              size_t optvallen);
```

더 나은 2차 형태는 typed config/object 기반이다.

```c
typedef struct zlink_gateway_options_t
{
    int sndhwm;
    int rcvhwm;
    int sndtimeo_ms;
    int rcvtimeo_ms;
    int linger_ms;
} zlink_gateway_options_t;
```

하지만 1차 구현에서는 다음 원칙이면 충분하다.

- service별 `set_option`
- 남길 option enum 수를 작게 유지
- service 의미와 직접 연결되는 이름 사용
- representative RID는 generic option이 아니라 dedicated identity API로 분리

### 6.1 `get_option`에 대한 현재 결정

1차 범위에서는 `get_option` 계열 API를 넣지 않는다.

이유:

- 지금 문제의 핵심은 설정 surface 단순화이지,
  runtime introspection API 추가가 아니다.
- 대부분의 사용 예는 “설정하고 사용”으로 끝난다.
- `get_option`을 넣는 순간 option value source-of-truth와
  normalization 정책까지 함께 정의해야 한다.

즉 현재 범위는 다음으로 제한한다.

- `set_option`: 포함
- `get_option`: 1차 비범위

## 7. 현재 API 기준 유지 / 이름 변경 / 제거 초안

현재 public API를 기준으로 보면 다음과 같이 정리하는 것이 가장 자연스럽다.

| 현재 API | 처리 방향 | 이유 |
|---|---|---|
| `zlink_gateway_setsockopt(gateway, option, ...)` | 유지 또는 `zlink_gateway_set_option`으로 이름만 정리 | 이미 service-level API라 구조적으로 큰 문제는 없다 |
| `zlink_receiver_setsockopt(receiver, ZLINK_RECEIVER_SOCKET_ROUTER, option, ...)` | `zlink_receiver_set_option(receiver, option, ...)`로 단순화 | 실제 public target은 ROUTER 하나면 충분하다 |
| `zlink_receiver_setsockopt(receiver, ZLINK_RECEIVER_SOCKET_DEALER, option, ...)` | 제거 | registry/heartbeat용 internal socket이므로 public target으로 둘 이유가 약하다 |
| `zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_PUB, option, ...)` | `zlink_spot_pub_set_option(pub, option, ...)`로 이동 | 사용자는 `SpotPub` 의미로 설정하는 편이 자연스럽다 |
| `zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_SUB, option, ...)` | `zlink_spot_sub_set_option(sub, option, ...)`로 이동 | 사용자는 `SpotSub` 의미로 설정하는 편이 자연스럽다 |
| `zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_DEALER, option, ...)` | 제거 | internal control plane용 socket이다 |
| `zlink_spot_node_setsockopt(node, ZLINK_SPOT_NODE_SOCKET_NODE, ZLINK_SPOT_NODE_OPT_*, ...)` | `SpotPub` service option으로 이동 | async publish mode/queue 정책은 publish behavior에 더 가깝다 |
| raw `ROUTING_ID` generic option | generic option surface에서는 제외 | representative RID는 별도 identity 정책으로 다루는 편이 더 명확하다 |

여기서 중요한 점은 다음이다.

- `Gateway`는 이미 public shape가 비교적 괜찮다.
- `Receiver`는 socket-role을 걷어내면 된다.
- `Spot`은 `SpotNode` 중심 옵션 API를 `SpotPub`/`SpotSub` 중심으로 재배치해야 한다.

호환성 비고려 전제에서의 정리 원칙:

- 기존 socket-role 기반 public API는 deprecated로 오래 유지하지 않는다.
- 새 API가 들어가는 릴리스에서 기존 API를 제거하거나 internal/private으로 내린다.
- bindings도 같은 릴리스에서 같이 정리한다.

## 8. service별 option enum 초안

Gateway:

```c
#define ZLINK_GATEWAY_OPT_SNDHWM    1
#define ZLINK_GATEWAY_OPT_RCVHWM    2
#define ZLINK_GATEWAY_OPT_SNDTIMEO  3
#define ZLINK_GATEWAY_OPT_RCVTIMEO  4
#define ZLINK_GATEWAY_OPT_LINGER    5
```

Receiver:

```c
#define ZLINK_RECEIVER_OPT_SNDHWM    1
#define ZLINK_RECEIVER_OPT_RCVHWM    2
#define ZLINK_RECEIVER_OPT_SNDTIMEO  3
#define ZLINK_RECEIVER_OPT_RCVTIMEO  4
#define ZLINK_RECEIVER_OPT_LINGER    5
```

SpotPub:

```c
#define ZLINK_SPOT_PUB_OPT_SNDHWM             1
#define ZLINK_SPOT_PUB_OPT_SNDTIMEO           2
#define ZLINK_SPOT_PUB_OPT_LINGER             3
#define ZLINK_SPOT_PUB_OPT_NODROP             4
#define ZLINK_SPOT_PUB_OPT_MODE               5
#define ZLINK_SPOT_PUB_OPT_QUEUE_HWM          6
#define ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY  7
```

SpotSub:

```c
#define ZLINK_SPOT_SUB_OPT_RCVHWM             1
#define ZLINK_SPOT_SUB_OPT_RCVTIMEO           2
#define ZLINK_SPOT_SUB_OPT_LINGER             3
#define ZLINK_SPOT_SUB_OPT_QUEUE_NODROP       4
#define ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY  5
```

핵심은 다음과 같다.

- 공통 socket option은 service별 enum으로 다시 노출한다.
- 실제 의미가 service queue 정책인 항목은 queue option으로 승격한다.
- raw zmq socket option 이름을 무조건 그대로 public에 노출하지 않는다.

## 9. 사용 예시

예시 1: Gateway

```c
int sndhwm = 1024;
int rcvhwm = 1024;
int rcvtimeo_ms = 1000;

zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_SNDHWM,
                         &sndhwm, sizeof(sndhwm));
zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_RCVHWM,
                         &rcvhwm, sizeof(rcvhwm));
zlink_gateway_set_option(gateway, ZLINK_GATEWAY_OPT_RCVTIMEO,
                         &rcvtimeo_ms, sizeof(rcvtimeo_ms));
```

예시 2: Receiver

```c
int linger_ms = 0;
int sndtimeo_ms = 1000;

zlink_receiver_set_option(receiver, ZLINK_RECEIVER_OPT_LINGER,
                          &linger_ms, sizeof(linger_ms));
zlink_receiver_set_option(receiver, ZLINK_RECEIVER_OPT_SNDTIMEO,
                          &sndtimeo_ms, sizeof(sndtimeo_ms));
```

예시 3: SpotPub

```c
int sndhwm = 4096;
int mode = ZLINK_SPOT_PUB_MODE_ASYNC;
int full_policy = ZLINK_SPOT_PUB_QUEUE_FULL_EAGAIN;

zlink_spot_pub_set_option(pub, ZLINK_SPOT_PUB_OPT_SNDHWM,
                          &sndhwm, sizeof(sndhwm));
zlink_spot_pub_set_option(pub, ZLINK_SPOT_PUB_OPT_MODE,
                          &mode, sizeof(mode));
zlink_spot_pub_set_option(pub, ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY,
                          &full_policy, sizeof(full_policy));
```

예시 4: SpotSub

```c
int rcvhwm = 4096;
int queue_nodrop = 1;

zlink_spot_sub_set_option(sub, ZLINK_SPOT_SUB_OPT_RCVHWM,
                          &rcvhwm, sizeof(rcvhwm));
zlink_spot_sub_set_option(sub, ZLINK_SPOT_SUB_OPT_QUEUE_NODROP,
                          &queue_nodrop, sizeof(queue_nodrop));
```

## 10. 구현 우선순위

option surface 정리는 monitor/readiness 작업과 독립적으로 진행할 수 있다.
다만 구현 순서는 다음이 자연스럽다.

1. `Receiver`
- 가장 단순하다.
- `ROUTER`만 public target으로 남기고 `DEALER`를 제거하면 된다.

2. `Spot`
- 사용성 개선 효과가 가장 크다.
- `SpotNode socket_role`을 `SpotPub`/`SpotSub` 기준으로 재배치해야 한다.

3. `Gateway`
- 이미 service-level에 가까우므로 우선순위는 낮다.
- 이름을 `setsockopt`로 둘지 `set_option`으로 맞출지만 결정하면 된다.

기본 방침:

- monitor/readiness 구현과 강하게 결합하지 않는다.
- 다만 public facade 정리라는 큰 방향은 동일하게 유지한다.

## 11. 재리뷰 결과

현재 문서에서 명확해진 점:

- “어떤 옵션을 남길 것인가”가 service별로 보인다.
- “현재 API를 어떻게 처리할 것인가”가 유지/이름 변경/제거 기준으로 보인다.
- `Spot`의 queue 정책처럼 이름과 의미가 어긋난 항목을 바로잡는 이유가 드러난다.

여전히 남아 있는 결정 1개:

- `Gateway`는 현재 `setsockopt` 이름을 유지할지,
  다른 service와 맞춰 `set_option`으로 통일할지

현재 의견:

- 새 API를 대대적으로 정리하는 시점이라면 이름도 `set_option`으로 통일하는 편이 낫다.
- 다만 구조적으로는 이미 service-level이라 우선순위는 `Receiver`와 `Spot`보다 낮다.
