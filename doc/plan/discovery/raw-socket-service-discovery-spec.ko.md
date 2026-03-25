# Discovery / Registry 일반 소켓 서비스 자동 연결 상세 스펙

## 1. 목적

이 문서는 `core`의 discovery / registry를 확장해
raw `ROUTER`, `DEALER`, `PUB`, `SUB`도
`service_name` 기준으로 위치투명하게 자동 연결되도록 만드는
상세 스펙과 내부 설계를 정의한다.

이번 설계에서 가장 중요한 결정은 다음이다.

- `service_name`의 ownership은 socket이 아니라 discovery가 가진다.
- discovery를 소켓에 attach하면, 그 소켓은 discovery가 대표하는 서비스에 속한다고 정의한다.
- 호환성 제약은 두지 않는다. 필요하면 public / internal surface를 함께 변경한다.

즉 discovery를 단순한 "registry subscriber"가 아니라
"특정 service family + 특정 service_name에 대한 canonical view"로 재정의한다.

핵심 목표:

- discovery 생성 시 `service_name`을 고정한다.
- registry / discovery는 `service_name + socket_role + endpoint`를 provider identity로 관리한다.
- raw `ROUTER`, `DEALER`, `PUB`, `SUB`가 discovery attach만으로 서비스 자동 연결을 사용하게 한다.
- `gateway`, `spot_node`도 같은 설계 철학으로 정리 가능하도록 구조를 맞춘다.
- role 매칭 규칙을 중앙화해 shallow wrapper와 hidden coupling을 줄인다.

v1 범위 고정:

- 이번 구현 범위의 직접 대상은 raw `ROUTER`, `DEALER`, `PUB`, `SUB`,
  `gateway`, `spot_node` 전체다.
- `spot`은 독립 attach 대상이 아니라 `spot_node` 내부 구성으로 정렬한다.
- raw socket뿐 아니라 기존 service family도 이번 단계에서 discovery-owned service model로 함께 전환한다.
- 즉 `gateway`, `spot_node`, 그리고 그에 종속된 `spot` constructor / attach / registration model 개편도 v1 직접 구현 범위다.

## 1.1 핵심 결정 요약

아래 표는 이 문서 전체에서 반복 참조하는 canonical 결정이다.
뒤 섹션의 세부 규칙은 이 표를 구체화하는 수준으로 읽는다.

| 결정 축 | canonical 규칙 |
| --- | --- |
| service ownership | `service_name`은 discovery만 소유한다. 소켓 constructor와 raw socket handle은 `service_name`을 저장하지 않는다. |
| discovery identity | discovery는 하나의 `(service_type, service_name)` view만 대표한다. |
| attach 의미 | 소켓에 discovery를 attach하면 그 소켓은 discovery가 대표하는 service view participant가 되고, service-mode lifecycle ownership도 discovery로 이동한다. |
| provider identity | registry / discovery provider identity는 `service_type + service_name + service_role + endpoint`다. |
| role 매칭 | 허용 조합은 role 매칭 표와 중앙 helper에서만 정의한다. |
| peer ownership | attach 이후 peer 집합은 discovery 기반 자동 연결 계층이 단독 소유한다. attached socket의 종료 순서도 discovery가 관리한다. |
| attach 상태 금지 API | attach 상태에서는 manual `connect`, manual `disconnect`, `unbind`, 개별 socket `close/destroy`, 다중 advertise bind를 금지한다. |
| compatibility | wire/source compatibility는 유지 대상이 아니다. coordinated rollout을 전제로 한다. |
| migration | `gateway`와 `spot_node`는 discovery attach 대상으로, `spot`은 `spot_node` 내부 구성으로 두고 dual surface 없이 discovery-owned model로 일괄 전환한다. |

연관 문서:

- [`../registry/spot-node-topology-introspection-plan.ko.md`](../registry/spot-node-topology-introspection-plan.ko.md)
- [`../direct-callback-recv/registry-topology-introspection-plan.ko.md`](../direct-callback-recv/registry-topology-introspection-plan.ko.md)
- [`../service/service-control-path-architecture.ko.md`](../service/service-control-path-architecture.ko.md)

## 2. 요구사항 정리

### 2.1 서비스 묶음 규칙

자동 연결의 1차 기준은 항상 `service_name`이다.

이 절은 [`1.1 핵심 결정 요약`](#11-핵심-결정-요약)의
`service ownership`, `discovery identity`, `attach 의미`를 구체화한다.

- 같은 `service_name`이 아니면 절대 자동 연결하지 않는다.
- discovery 하나는 정확히 하나의 `service_name`만 대표한다.
- discovery를 attach한 소켓은 attach된 discovery의 `service_name`에 속한다.
- 같은 프로세스에서 여러 서비스를 보려면 discovery도 서비스 수만큼 만든다.

### 2.2 role별 매칭 규칙

같은 `service_name` 안에서 최종 연결 여부는
로컬 socket role과 remote provider role의 조합으로 결정한다.

고정 규칙:

- `gateway` -> 같은 서비스의 `gateway`만 연결
- `spot` -> 같은 서비스의 `spot`만 연결
- `pub` -> 같은 서비스의 `sub`만 연결
- `sub` -> 같은 서비스의 `pub`만 연결
- `router` -> 같은 서비스의 `router`, `dealer`를 모두 연결
- `dealer` -> 같은 서비스의 `router`, `dealer`를 모두 연결

명시적으로 금지되는 조합:

- `pub` -> `pub`
- `sub` -> `sub`
- `pub/sub` <-> `router/dealer`
- raw role <-> `gateway`
- raw role <-> `spot`

### 2.3 fan-out 규칙

후보가 여러 개면 모두 연결한다.

- `router`는 같은 서비스의 모든 `router`, `dealer` endpoint에 연결한다.
- `dealer`도 같은 서비스의 모든 `router`, `dealer` endpoint에 연결한다.
- `pub`는 같은 서비스의 모든 `sub` endpoint에 연결한다.
- `sub`는 같은 서비스의 모든 `pub` endpoint에 연결한다.

### 2.4 self-connect 규칙

같은 서비스에서 자기 자신의 advertise endpoint는 자동 연결 대상에서 제외한다.

### 2.5 manual peer와의 관계

discovery attach 상태와 manual connect/disconnect는 혼용하지 않는다.

이 절의 canonical source는 [`1.1 핵심 결정 요약`](#11-핵심-결정-요약)의
`peer ownership`, `attach 상태 금지 API`다.

이유:

- peer ownership이 이중화된다.
- 자동 disconnect가 manual peer를 끊을 수 있다.
- 정책 설명이 어려워진다.

기본 정책:

- attach 전에는 기존 raw `connect` / `disconnect`를 그대로 허용한다.
- attach 후에는 discovery 기반 자동 연결이 peer 집합의 sole owner가 된다.
- attach 이후 manual connect/disconnect 혼용은 막는다.
- 별도 detach API는 두지 않는다.
- attach 이후 service participant 종료는 개별 socket `close/destroy`가 아니라 discovery `destroy`로 수행한다.

attach 상태에서 추가로 금지하는 동작:

- manual `connect`
- manual `disconnect`
- advertise endpoint를 제거하는 `unbind`
- attach된 service participant를 개별적으로 끝내는 socket `close/destroy`

즉 service mode에 들어간 소켓은
"bind된 대표 endpoint를 광고하고, peer와 lifecycle은 discovery가 관리한다"는 계약을 유지해야 한다.

## 3. 문제 정의

### 3.1 현재 구조의 한계

현재 discovery / registry는 사실상 아래 모델에 가깝다.

- discovery는 service family 단위로 provider 집합을 본다.
- provider 구분은 `gateway`와 `spot` 수준에서 끝난다.
- 소켓이 어느 서비스에 속하는지는 각 서비스 소켓이 따로 안다.

이 구조는 같은 서비스 안에 여러 raw role이 공존할 때 부족하다.

예:

```text
service = "order-bus"
providers = {
  router endpoint A,
  dealer endpoint B,
  dealer endpoint C
}
```

이 경우 필요한 개념은
"이 discovery가 `order-bus`를 대표한다"는 사실과
"같은 서비스 안에서 role별로 누구를 붙일지"다.

그런데 지금 구조는 service ownership이 각 소켓/서비스 객체에 흩어져 있어,
discovery attach가 "어느 서비스 view에 join하는가"를 직접 표현하지 못한다.

### 3.2 service ownership을 discovery로 올려야 하는 이유

이번 요구사항의 본질은 socket별 임의 메타데이터가 아니라
"서비스 view를 소켓에 attach한다"는 모델이다.

따라서 더 적합한 구조는 아래다.

```text
discovery = (service_family, service_name) view
socket attach = 그 view의 participant가 됨
role matching = attach된 소켓의 socket role로 결정
```

이렇게 하면:

- socket마다 `service_name`을 따로 저장할 필요가 없다.
- attach API 의미가 더 분명하다.
- 동일 서비스에 대한 observer / cache / update seq를 discovery가 집중 관리할 수 있다.
- 서비스 선택 책임이 한 모듈로 모여 POSD 관점에서 더 깊은 추상화가 된다.

## 4. 설계 원칙

### 4.1 discovery는 service view다

discovery의 개념 모델을 아래로 고정한다.

```text
discovery = 특정 service_family + 특정 service_name의 provider view
```

즉 discovery는 더 이상 "family 전체를 스캔하는 핸들"이 아니다.
하나의 논리 서비스를 대표하는 canonical read / control surface다.

### 4.2 service 선택 책임은 discovery가 가진다

금지:

- socket마다 `service_name`을 따로 들고 attach 시 비교하는 구조
- 동일 서비스 문자열을 gateway / spot / raw socket / discovery가 중복 저장하는 구조

권장:

- `service_name` ownership은 [`1.1 핵심 결정 요약`](#11-핵심-결정-요약)을 따른다.
- discovery를 attach한 소켓은 discovery에서 `service_name`을 읽는다.
- register / unregister도 discovery가 대표하는 서비스명으로 수행한다.

### 4.3 role 매칭 정책은 중앙화한다

role별 연결 가능 조합은 공통 helper 한 곳에서 정의한다.

- `local_role -> allowed_remote_roles`
- `role valid for family`
- `role derived from socket type`

registry validate, discovery filter, runtime connect diff가 모두 같은 helper를 쓴다.

### 4.4 family와 role은 분리된 축이다

family는 "어떤 종류의 서비스 view를 다루는가"를 뜻하고,
role은 "같은 서비스 안에서 어떤 peer와 연결 가능한가"를 뜻한다.

원칙:

- raw socket family 하나를 추가한다.
- raw socket family 안에서 `router/dealer/pub/sub` role을 구분한다.
- `gateway`, `spot`은 각자 family를 유지하되 role도 명시적으로 가진다.

### 4.5 attach된 자동 연결 ownership은 단일해야 한다

attach 이후 peer 집합은 discovery 기반 자동 연결 계층이 단독 소유한다.
세부 금지 API는 [`2.5 manual peer와의 관계`](#25-manual-peer와의-관계)와
[`13.5 attach 상태 API 금지 규칙`](#135-attach-상태-api-금지-규칙)을 따른다.

## 5. 개념 모델

### 5.1 service view

service view는 아래 identity를 가진다.

```text
service_family + service_name
```

discovery는 이 service view 하나를 대표한다.

### 5.2 provider descriptor

provider descriptor는 아래 정보를 가진다.

```text
service_family + service_name + socket_role + endpoint
```

설명:

- `service_family`: gateway / spot / raw socket family
- `service_name`: 논리 서비스 이름
- `socket_role`: 같은 서비스 안의 연결 규칙
- `endpoint`: 실제 transport endpoint

### 5.3 socket attachment

소켓에 discovery를 attach한다는 것은 다음 의미를 가진다.

이 절의 canonical source는 [`1.1 핵심 결정 요약`](#11-핵심-결정-요약)의
`attach 의미`, `peer ownership`이다.

- 이 소켓은 attach된 discovery의 service view에 속한다.
- 이 소켓의 local role은 socket type 또는 service family에서 도출된다.
- provider register / unregister / peer refresh는 이 service view 기준으로 수행된다.
- attach 이후 service-mode lifecycle 종료는 discovery destroy 경로가 담당한다.

cardinality 규칙:

- discovery 하나에는 여러 소켓이 attach될 수 있다.
- attach된 모든 소켓은 같은 `(service_family, service_name)` view를 공유한다.
- 소켓 하나에는 discovery를 최대 1개만 attach할 수 있다.

즉 discovery는 "서비스 단위 공유 view"이고,
소켓은 그 view의 participant다.

## 6. Public Surface 제안

### 6.1 discovery 생성 시 service_name 고정

`zlink_discovery_new()`는 `service_name`까지 받도록 변경한다.

이 surface는 [`1.1 핵심 결정 요약`](#11-핵심-결정-요약)의
`service ownership`, `discovery identity`를 API로 드러낸다.

예시:

```c
void *zlink_discovery_new(void *ctx,
                          zlink_service_type_t service_type,
                          const char *service_name);
```

의미:

- `service_type`: 어떤 family를 볼 것인가
- `service_name`: 그 family 안에서 어떤 서비스 하나를 볼 것인가

제약:

- `service_name`은 비어 있으면 안 된다.
- discovery 생성 후 service_name은 변경할 수 없다.

### 6.2 raw socket attach API

raw socket에는 별도 `set_service_name()` API를 두지 않는다.

raw socket용 최소 public control-path API:

```c
int zlink_socket_attach_discovery(void *socket, void *discovery);
```

attach 성공 의미:

- raw socket은 discovery의 `service_name`에 속한다.
- raw socket은 discovery의 family / role 정책을 따른다.
- bind 후 provider register, discovery update 후 peer refresh를 수행한다.
- attach 이후 개별 socket close/destroy 대신 discovery destroy가 종료를 담당한다.

추가 계약:

- attach는 bind 전에도 bind 후에도 가능하다.
- bind 후 attach하면 attach 성공 직후 현재 advertise endpoint로 즉시 register를 시도한다.
- attach 전 bind했다면 그 bind endpoint가 service advertise endpoint가 된다.
- attach 상태 제약은 [`1.1 핵심 결정 요약`](#11-핵심-결정-요약)과
  [`13.5 attach 상태 API 금지 규칙`](#135-attach-상태-api-금지-규칙)을 따른다.
- discovery destroy는 attach된 service participant를 cascade shutdown하고, 이후 해당 socket handle은 무효가 된다.

### 6.3 gateway / spot에도 같은 철학 적용

호환성 제약이 없으므로 기존 `gateway_new(ctx, service_name)` /
`spot_new(ctx, service_name)` /
`spot_node_new(ctx, service_name)` 모델도 이번 작업에서 같이 바꾼다.

전환 정책의 canonical source는 [`1.1 핵심 결정 요약`](#11-핵심-결정-요약)의
`migration`이다.

v1에서 고정할 public 방향:

- `gateway_new(ctx)`로 단순화
- `spot_new(ctx)`로 단순화
- `spot_node_new(ctx)`로 단순화
- 서비스 선택 ownership은 attach된 `gateway` 또는 `spot_node` discovery가 담당
- `spot`은 독립 attach 대상이 아니라 `spot_node` 내부 구성으로 유지

고정 canonical surface:

```c
void *zlink_gateway_new(void *ctx);
void *zlink_spot_new(void *ctx);
void *zlink_spot_node_new(void *ctx);
int zlink_gateway_attach_discovery(void *gateway, void *discovery);
int zlink_spot_node_attach_discovery(void *node, void *discovery);
```

전환 규칙:

- constructor에서 `service_name` 인자는 제거한다.
- service_name은 attach된 `gateway` 또는 `spot_node` discovery에서만 읽는다.
- bind 전 attach, bind 후 attach 모두 허용한다.
- attach 없는 bind는 허용하되 provider register는 하지 않는다.
- attach 없는 `gateway` / `spot_node`는 service-bound automatic mode가 아니라 local-only mode로 본다.
- `spot`은 local runtime / facade surface로 남고 service-bound automatic mode의 직접 소유자가 아니다.

필수 적용 범위:

- `gateway`는 더 이상 constructor에서 `service_name`을 소유하지 않는다.
- `spot_node`도 더 이상 constructor에서 `service_name`을 소유하지 않는다.
- `spot`도 더 이상 constructor에서 service ownership을 소유하지 않는다.
- discovery attach 대상은 `gateway`, `spot_node`, raw socket이다.
- `spot`은 `spot_node` 내부 구성으로 참여하고,
  service binding / register / peer refresh ownership은 `spot_node`가 가진다.

이 문서의 canonical 방향은 다음이다.

- service ownership은 discovery에 있다.
- `gateway`, `spot_node`, raw socket constructor는 service_name을 모를 수 있다.
- `spot`은 독립 attach 대상이 아니므로 service_name을 직접 선택하지 않는다.

### 6.4 role 도출

raw socket role은 public API에서 따로 받지 않고
소켓 type에서 자동 도출한다.

매핑:

- raw `ROUTER` -> `router`
- raw `DEALER` -> `dealer`
- raw `PUB` -> `pub`
- raw `SUB` -> `sub`

`gateway`, `spot`은 각 서비스 family에서 고정 role을 가진다.

## 7. Family / Role 모델

### 7.1 discovery family 확장

`zlink_service_type_t`는 아래처럼 정리한다.

```c
typedef enum zlink_service_type_t
{
    ZLINK_SERVICE_TYPE_GATEWAY = 0x3001,
    ZLINK_SERVICE_TYPE_SPOT = 0x3002,
    ZLINK_SERVICE_TYPE_SOCKET = 0x3003
} zlink_service_type_t;
```

### 7.2 role enum

내부 protocol / runtime에서 공통으로 쓰는 role enum을 둔다.

예시:

```c
enum service_role_t
{
    service_role_gateway = 1,
    service_role_spot = 2,
    service_role_router = 3,
    service_role_dealer = 4,
    service_role_pub = 5,
    service_role_sub = 6
};
```

### 7.3 family-role 유효 조합

고정 규칙:

- `GATEWAY` family에는 `gateway` role만 유효
- `SPOT` family에는 `spot` role만 유효
- `SOCKET` family에는 `router`, `dealer`, `pub`, `sub` role만 유효

이 규칙은 registry register validation과 discovery attach validation 모두에 적용한다.

## 8. Registry / Discovery 프로토콜 확장

### 8.1 register 계열 payload

기존 register / unregister / heartbeat / update-weight 흐름에
`socket_role` 필드를 추가한다.

개념 shape:

```text
msg_register
service_type
service_role
service_name
endpoint
weight
```

동일 확장을 적용할 message:

- `msg_register`
- `msg_unregister`
- `msg_heartbeat`
- `msg_update_weight`
- `msg_service_list`
- `msg_registry_sync`

raw socket family의 weight 정책:

- raw socket provider의 기본 weight는 항상 `1`이다.
- v1에서는 raw socket용 weight update public API를 추가하지 않는다.
- registry wire에 `weight` 필드는 유지하되, raw socket family에서는 fixed value로 취급한다.

이 결정으로 wire format은 family 간에 통일되고,
raw socket surface에는 불필요한 control-path가 늘어나지 않는다.

### 8.2 service list shape

service list는 `service view`를 기준으로 provider를 전달한다.

개념 shape:

```text
service_type
service_name
provider_count
  provider_role
  endpoint
  routing_id
  weight
```

의미:

- registry broadcast는 v1에서도 전체 service view 목록을 계속 전파한다.
- discovery는 수신한 전체 목록 중 자기 `(service_type, service_name)`에 해당하는 row만 로컬 캐시에 반영한다.
- 같은 서비스 안에서 role이 다른 provider를 한 번에 볼 수 있다.

이 정책을 고정하는 이유:

- registry broadcast 경로를 서비스별 fan-out 채널로 다시 설계할 필요가 없다.
- 기존 registry broadcast architecture를 최대한 재사용할 수 있다.
- discovery ownership 전환의 핵심은 "캐시 범위 고정"이지 "wire fan-out topology 개편"이 아니다.

### 8.3 호환성 정책

호환성은 목표가 아니다.

전제:

- registry, discovery, attached socket은 함께 변경한다.
- 구버전 wire compatibility layer는 두지 않는다.
- 기존 public API와 source-level 호환도 필요하면 깨도 된다.

운영 의미:

- `register`, `unregister`, `heartbeat`, `service-list`, `sync` 메시지 shape가 모두 바뀐다.
- 구버전과 신버전이 같은 registry/discovery mesh에 섞여 동작하는 rolling upgrade는 지원하지 않는다.
- 배포 단위는 registry, discovery, service participant를 포함한 coordinated rollout이어야 한다.
- 실행 가이드와 자동 실행 스크립트의 단계별 commit / push는 구현 추적 목적일 뿐, 혼합 버전 운영 허용을 뜻하지 않는다.

## 9. Registry 저장 모델

### 9.1 service view key

registry의 상위 key는 아래다.

```text
service_type + service_name
```

같은 서비스 view 아래에 role별 provider 집합을 둔다.

### 9.2 provider key

provider identity는 아래다.

```text
service_type + service_name + service_role + endpoint
```

이 구조면 같은 서비스 안에 `router`, `dealer`, `pub`, `sub`가 공존해도 모호성이 없다.

### 9.3 provider entry

provider entry가 유지할 정보:

- endpoint
- representative routing_id
- role
- weight
- registered_at
- last_heartbeat
- source_registry

### 9.4 registry 조회 surface 확장

registry에 저장 모델이 늘어나면 조회 surface도 같이 확장해야 한다.

용어 구분:

- `service_type`
  어떤 서비스 family인지 구분하는 상위 분류다.
  예: `GATEWAY`, `SPOT`, `SOCKET`
- `service_name`
  그 family 안에서의 구체적인 논리 서비스 이름이다.
  예: `order-bus`, `price-feed`

즉 registry에서 서비스 view identity는 아래다.

```text
service_type + service_name
```

조회 관점에서:

- `service_name`은 주 조회 키다.
- `service_type`은 같은 이름이 여러 family에 존재할 수 있으므로 namespace 역할을 한다.

필수 방향:

- 기존 registry topology / service summary / status 계열 조회에서
  raw socket family 정보가 보이게 해야 한다.
- 기존 `gateway`, `spot`만 전제로 한 registry summary 모델을
  raw `router/dealer/pub/sub`까지 포함하는 모델로 확장해야 한다.

v1 필수 요구:

- `zlink_registry_topology_snapshot()` / `query()`가
  raw socket family row를 반환할 수 있어야 한다.
- topology row에서 role 구분이 가능해야 한다.
- `zlink_registry_service_summary_snapshot()`도
  raw socket family를 집계할 수 있어야 한다.
- 실제 주 사용 경로는 `service_name` 중심 query가 되어야 한다.

v1 canonical query shape:

- topology row에는 `service_role` 필드를 추가한다.
- topology filter에도 `service_role` 필드를 추가한다.
- service summary filter는 최소 `service_type`, `service_name`, `service_role`를 받을 수 있어야 한다.

예시:

```c
typedef struct zlink_registry_topology_filter_t
{
    uint16_t service_type;
    uint16_t service_role;
    const char *service_name;
    const zlink_routing_id_t *routing_id;
} zlink_registry_topology_filter_t;
```

```c
typedef struct zlink_registry_service_summary_filter_t
{
    uint16_t service_type;
    uint16_t service_role;
    const char *service_name;
} zlink_registry_service_summary_filter_t;
```

정렬 규칙:

- topology query 결과는 `(service_type, service_name, service_role, endpoint)` 오름차순
- service summary 결과는 `(service_type, service_name, service_role)` 오름차순

권장 확장:

- topology entry 또는 filter에 `service_role`을 추가해
  `router`, `dealer`, `pub`, `sub`, `gateway`, `spot`를 구분 가능하게 한다.
- service summary는 `(service_type, service_name, service_role)` 기준으로 drill-down 가능하게 한다.

권장 조회 흐름:

- 전체 운영 상태 확인: `snapshot`
- 특정 서비스 확인: `query(service_type, service_name)`
- 특정 서비스 안의 특정 role 확인:
  `query(service_type, service_name, service_role)`

원칙:

- registry는 여전히 global summary다.
- local socket/spot/gateway 세부 상태를 그대로 복사하지는 않는다.
- 하지만 어떤 서비스에 어떤 role provider가 등록돼 있는지는
  조회 가능해야 한다.

이 문서 기준으로 registry 조회 확장은 이번 작업 범위에 포함한다.
별도 후속 문서로 미루지 않는다.

## 10. Discovery 저장 모델

### 10.1 discovery state

discovery는 단일 서비스 view만 캐시한다.

필요 상태:

- fixed `service_type`
- fixed `service_name`
- provider list for that one service
- registry seq / update seq
- observer set
- report dealer / bootstrap state

공유 규칙:

- 같은 discovery instance를 여러 소켓이 공유할 수 있다.
- observer set은 attach된 소켓 수만큼 늘 수 있다.
- discovery destroy는 attach된 소켓을 blocker로 보지 않고 attached service participant 전체를 cascade shutdown한다.

즉 기존처럼 여러 서비스명을 map으로 들고 있을 필요가 없다.
필요하면 내부 구현도 단일 서비스 view 중심으로 단순화한다.
이 구조는 [`1.1 핵심 결정 요약`](#11-핵심-결정-요약)의
`service ownership`, `discovery identity` 구현 형태다.

### 10.2 provider snapshot 구조

`provider_info_t`에는 `socket_role`을 추가한다.

예시:

```c
struct provider_info_t
{
    std::string service_name;
    std::string endpoint;
    zlink_routing_id_t routing_id;
    uint16_t socket_role;
    uint32_t weight;
    uint64_t registered_at;
};
```

`service_name`은 discovery fixed field와 중복될 수 있지만,
snapshot / event / topology surface에서 self-contained row를 유지하려면 보관해도 된다.

### 10.3 observer semantics

observer callback은 다중 서비스 이름을 넘길 필요가 약해진다.

권장 방향:

- `on_service_update()`는 "내가 보고 있는 서비스 view가 변경됐다"는 의미만 가진다.
- 인자로 `service_name`을 다시 넘기지 않거나,
  넘기더라도 항상 discovery fixed service_name과 동일하다.

후속 리팩터링에서는 observer surface 자체를 단순화할 수 있다.

## 11. Socket Attachment 설계

### 11.1 local attachment state

discovery를 attach한 소켓은 다음 상태만 들면 된다.

- attached discovery pointer
- local role
- advertise endpoint
- registration state
- discovery-managed peer endpoints
- active peer endpoints
- refresh seq

중요:

- 소켓은 별도 `service_name`을 저장하지 않는다.
- 필요할 때 discovery에서 `service_name`을 읽는다.
- service-attached 소켓은 advertise endpoint를 최대 1개만 가진다.
- attach된 service participant는 개별 socket close/destroy 경로를 허용하지 않는다.

즉 local attachment state는 [`1.1 핵심 결정 요약`](#11-핵심-결정-요약)의
`service ownership`, `attach 상태 금지 API`를 구현하는 최소 상태만 가진다.

### 11.2 attach lifecycle

canonical lifecycle:

1. 소켓 생성
2. 필요 시 socket option / routing id / TLS 설정
3. discovery 생성 with `(service_type, service_name)`
4. 소켓에 discovery attach
5. bind 시 advertise endpoint 확정
6. registry register
7. discovery update 수신
8. role matching 기반 peer diff 계산
9. connect / disconnect 반영
10. discovery destroy 시 unregister, disconnect, socket close, handle invalidation

bind 후 attach 경로도 허용되므로 실제 순서는 두 가지 모두 가능하다.

- `attach -> bind -> register`
- `bind -> attach -> register`

두 경우 모두 register 시점 이후의 steady state는 동일해야 한다.

종료 규칙:

- attach된 service participant의 정상 종료는 discovery destroy가 유일한 canonical 경로다.
- discovery destroy는 attached socket마다 unregister, peer disconnect, socket close를 수행한다.
- destroy 완료 후 attached socket handle은 더 이상 유효하지 않다.

### 11.3 bind와 register 관계

provider 등록은 bind 완료 후에만 수행한다.

attach만 하고 아직 bind하지 않은 소켓은 consumer 역할만 수행한다.
bind가 되면 provider 역할도 수행한다.

service-attached 소켓의 advertise 계약은 아래로 고정한다.

- advertise endpoint는 정확히 1개다.
- 첫 bind 성공 endpoint가 advertise endpoint가 된다.
- attach 상태에서 두 번째 bind를 추가해 복수 advertise endpoint를 갖는 것은 지원하지 않는다.
- 기존 raw socket의 multi-bind capability는 service mode에서는 의도적으로 제한한다.

이유:

- registry provider identity를 단순하게 유지한다.
- unregister / heartbeat / self-filtering 기준이 명확해진다.
- 서비스 자동 연결의 개념 모델을 "서비스당 하나의 대표 endpoint"로 고정할 수 있다.

### 11.4 attach validation

attach 시 검증:

- discovery family와 소켓 family가 맞는가
- 소켓 type에서 도출한 role이 그 family에 유효한가
- 이미 다른 discovery가 attach되어 있지 않은가
- manual peer 관리 상태와 충돌하지 않는가
- attach 후 복수 advertise bind를 허용하는 사용 패턴과 충돌하지 않는가
- discovery가 아직 살아 있고 close / destroy 진행 중이 아닌가

raw socket attach는 `ZLINK_SERVICE_TYPE_SOCKET` discovery만 허용한다.

## 12. Runtime 동작 상세

### 12.1 refresh 입력

refresh는 아래 입력을 사용한다.

- discovery fixed `service_name`
- discovery provider snapshot
- local role
- local advertise endpoint

### 12.2 peer filtering 절차

1. discovery에서 같은 service view provider snapshot 획득
2. local role 기준으로 허용된 remote role만 선택
3. self advertise endpoint 제거
4. target endpoint set 생성
5. 기존 active discovery-managed set과 diff 계산
6. 신규 endpoint는 `connect`
7. 제거된 endpoint는 `disconnect`

### 12.3 duplicate connect 방지

다음은 connect 대상에서 제외:

- self endpoint
- 이미 active set에 있는 endpoint
- role 매칭 테이블에 없는 provider

### 12.4 destroy cleanup

discovery destroy 시 attach state가 있는 service participant는 아래 순서로 cleanup한다.

1. discovery를 `destroying` 상태로 전환해 새 attach / refresh / register를 막음
2. attached socket 각각에 대해 discovery observer 제거
3. bind된 advertise endpoint가 있으면 registry에 마지막 unregister 전송 시도
4. discovery-managed peer disconnect
5. socket close 수행
6. attachment state clear
7. socket handle 무효화
8. 마지막으로 discovery 자체를 destroy

원칙:

- registry 마지막 unregister 전송은 시도해야 한다.
- 일부 원격 전송 실패가 있어도 로컬 socket close와 handle invalidation은 계속 진행한다.
- destroy 이후 attached socket을 다시 사용하는 것은 정의하지 않는다.

bind된 뒤 discovery를 attach한 소켓이 attach 직후 register에 실패하면
attach 호출 자체를 실패로 처리하고 attachment state를 롤백한다.

이 정책은 "attach 성공 = 그 service view participant로 정상 편입"이라는 계약을 보장한다.

attach 상태에서 `unbind`를 시도하면 실패로 처리한다.
service mode는 provider endpoint의 존재를 registry contract 일부로 보기 때문이다.

## 13. 에러 정책

### 13.1 discovery 생성 실패

다음 경우 discovery 생성 실패:

- `service_type`이 유효하지 않음
- `service_name`이 비어 있음

권장 errno:

- invalid `service_type` -> `EINVAL`
- empty `service_name` -> `EINVAL`

### 13.2 attach 실패

다음 경우 attach 실패:

- socket type이 지원되지 않음
- discovery family가 socket family와 맞지 않음
- role이 family에 유효하지 않음
- 이미 discovery가 attach됨
- manual peer 관리 상태와 충돌함
- 이미 복수 bind 상태라 service advertise endpoint를 단일하게 정할 수 없음
- bind 후 attach 경로에서 즉시 register에 실패함
- discovery가 shutdown / destroy 진행 중임

권장 errno:

- socket type이 지원되지 않음 -> `ENOTSUP`
- discovery family mismatch -> `EINVAL`
- role invalid for family -> `EINVAL`
- already attached -> `EBUSY`
- manual peer 상태 충돌 -> `EBUSY`
- multi-bind state 충돌 -> `EBUSY`
- immediate register failure after bind -> register 실패 원인 errno 전파
- discovery shutdown / destroy 중 -> `ESHUTDOWN`

### 13.3 registry register 실패

registry는 아래 조합을 reject한다.

- 알 수 없는 family
- family에 허용되지 않는 role
- 빈 `service_name`
- 빈 endpoint

권장 errno / reply 의미:

- invalid family -> protocol-level invalid type
- invalid role for family -> protocol-level invalid type
- empty `service_name` -> invalid endpoint / invalid request 계열
- empty endpoint -> invalid endpoint

registry ack 상세 코드는 기존 register ack 체계를 재사용하되,
호출자 표면에는 최종적으로 `EINVAL` 또는 기존 protocol mapping errno로 수렴시킨다.

### 13.4 discovery destroy 실패

다음 경우 discovery destroy 실패:

- discovery가 이미 shutdown / destroy 진행 중임
- 내부 cleanup 준비에 필요한 핵심 상태가 이미 손상돼 cascade shutdown을 시작할 수 없음

권장 errno:

- already destroying / shutdown -> `ESHUTDOWN`
- internal invalid lifecycle state -> `EFSM`

### 13.5 attach 상태 API 금지 규칙

attach 상태의 raw socket에서 아래 API는 실패해야 한다.

이 절은 [`1.1 핵심 결정 요약`](#11-핵심-결정-요약)의
`attach 상태 금지 API`를 errno 규칙으로 구체화한 것이다.

- manual `connect`
- manual `disconnect`
- `unbind`
- 개별 socket `close/destroy`

권장 errno:

- attached state에서 허용되지 않는 수동 peer 조작 -> `EFSM`
- attached state에서 허용되지 않는 개별 종료 -> `EFSM`
- 두 번째 advertise bind 시도 -> `EBUSY`

## 14. 테스트 계획

### 14.1 protocol 단위 테스트

- role 포함 register / unregister / heartbeat / service-list encode / decode
- invalid family-role 조합 reject
- 같은 `service_name` 안에서 다중 role provider 유지

### 14.2 discovery 단위 테스트

- discovery 생성 시 fixed `service_name` 보관
- service list에서 자기 `service_name` view만 반영
- provider snapshot에 role 포함
- 하나의 discovery를 여러 소켓이 공유 가능
- discovery destroy가 attached service participant를 cascade shutdown
- destroy 완료 후 attached socket handle이 무효 상태로 전환됨

### 14.3 raw socket integration 테스트

같은 서비스 기준:

- `router-router` 자동 연결
- `router-dealer` 자동 연결
- `dealer-dealer` 자동 연결
- `pub-sub` 자동 연결

attach 시점:

- `attach -> bind` 경로에서 정상 register / auto-connect
- `bind -> attach` 경로에서 즉시 register / auto-connect

금지 조합:

- `pub-pub` 미연결
- `sub-sub` 미연결
- `router/dealer`와 `pub/sub` 미연결
- raw socket과 `gateway` 미연결
- raw socket과 `spot` 미연결

서비스 분리:

- role이 맞아도 discovery의 `service_name`이 다르면 연결되지 않음

self filtering:

- 자기 advertise endpoint에는 connect하지 않음

provider churn:

- provider 제거 시 자동 disconnect
- provider 추가 시 자동 connect

advertise 계약:

- attach 상태에서 두 번째 bind는 실패
- attach 전에 이미 복수 bind인 raw socket은 attach 실패
- attach 상태의 `unbind`는 실패
- attach 상태의 manual `connect` / `disconnect`는 실패

### 14.4 회귀 테스트

- 기존 `gateway` discovery 경로 유지 또는 동일 철학으로 마이그레이션 후 의미 보존
- 기존 `spot` discovery 경로 유지 또는 동일 철학으로 마이그레이션 후 의미 보존
- discovery attach 이전 raw socket manual 사용 경로 회귀 보존
- registry topology / service summary 기존 조회 계약 회귀 보존
- destroy / attach / bind / query 경합에서 기존 lifecycle errno 계약 회귀 보존

gateway / spot / spot_node 영향 분석:

- constructor에서 `service_name`이 제거되면 기존 생성 경로를 직접 호출하는 테스트는 전부 수정 대상이다.
- 영향 범위는 최소 `gateway`, `spot_node`, 그리고 `spot_node`를 통해 `spot`을 구성하는 helper를 직접 호출하는 integration / e2e 테스트 전부다.
- 마이그레이션 전략은 점진적 dual surface가 아니라 일괄 전환으로 고정한다.
- 즉 기존 constructor 호출부를 남겨 두고 병행 유지하지 않는다.
- 테스트 수정 순서는 아래로 고정한다.
  1. discovery 생성 helper를 `(service_type, service_name)` 기준으로 먼저 바꾼다.
  2. `gateway`, `spot`, `spot_node` 생성 helper에서 constructor 인자를 제거한다.
  3. 각 테스트에서 `gateway` 또는 `spot_node` constructor 직후 discovery attach를 명시한다.
  4. `spot` 관련 테스트는 독립 attach가 아니라 `spot_node` attach 이후 bind / start / refresh 시점을 검증한다.
- 회귀 목적 테스트는 "예전 constructor shape"를 보존하는 것이 아니라,
  "같은 서비스 자동 연결 semantics"와 "기존 data-plane semantics"를 보존하는 쪽으로 다시 고정한다.

### 14.5 신규 회귀 테스트 작성 원칙

- 새 기능 검증 테스트와 별도로 회귀 테스트를 명시적으로 추가한다.
- 회귀 테스트는 "예전 기능이 그대로 동작한다"를 증명하는 목적을 가진다.
- gateway / spot_node / registry / raw socket 기존 경로를 각각 최소 1개 이상 독립 테스트로 고정한다.
- 회귀 테스트는 lane에 계속 남겨 두고, 일회성 repro 테스트로 끝내지 않는다.

## 15. 구현 단계 제안

### 15.1 Phase 1: discovery ownership 전환

- `zlink_discovery_new()`에 `service_name` 추가
- discovery 내부 상태를 단일 service view 기준으로 재정의
- observer / snapshot 코드에서 다중 서비스 map 의존 제거

### 15.2 Phase 2: protocol / registry role 확장

- role enum 추가
- register / service-list / sync wire 확장
- registry provider identity를 role-aware로 변경

### 15.3 Phase 3: raw socket attach

- raw socket attach API 추가
- raw type -> role 도출 helper 추가
- bind/register/refresh/disconnect lifecycle 구현
- attach 상태의 `connect` / `disconnect` / `unbind` / 개별 `close/destroy` gate 구현

### 15.4 Phase 4: gateway / spot 정렬

- `gateway`, `spot_node`의 service ownership을 discovery로 이동
- `spot`은 `spot_node` 내부 구성으로 정렬
- constructor의 `service_name` 인자를 제거하거나 동등한 무서비스 생성 surface로 전환
- discovery attach semantics를 family 전체에서 일관되게 맞춤
- 기존 `gateway`, `spot`, `spot_node` 테스트와 helper를 일괄 전환
- dual constructor나 compatibility wrapper는 두지 않음

완료 기준:

- `gateway`, `spot_node`는 service_name을 discovery에서만 읽는다.
- `spot`은 `spot_node`에 종속된 runtime participant로 service에 참여한다.
- constructor는 service ownership을 갖지 않는다.
- attach 이후 register / refresh / teardown semantics가 raw socket과 같은 ownership 모델로 정렬된다.

### 15.5 Phase 5: docs / tests 정리

- API 문서 개편
- regression / integration 정리
- topology / monitor 문서와 의미 정합성 맞춤
- Phase / Step / guide 체크리스트 매핑을 문서에서 바로 추적 가능하게 유지

### 15.6 Phase 6: POSD 리팩토링 정리

- discovery-owned service model 도입 후 중복된 service ownership 상태 제거
- role matching / attach lifecycle / register ownership 관련 중복 helper 통합
- gateway / spot_node / raw socket에 흩어진 service-bound control-path 조건문 정리
- shallow wrapper나 change amplification이 생긴 부분 제거

완료 기준:

- service ownership, attach lifecycle, peer ownership 규칙이 각 family마다 따로 설명되지 않고 공통 모델로 설명된다.
- 동일 정책을 여러 클래스에서 반복 구현한 코드가 공통 모듈로 수렴한다.
- public API 의미가 constructor / attach / bind 단계에서 일관된다.

## 16. 구현 매핑

이 섹션은 실제 코드베이스 기준으로
어느 파일에서 어떤 성격의 변경이 필요한지 고정한다.

### 16.1 public API / surface

주요 수정 지점:

- `core/include/zlink.h`
- `core/src/api/service_discovery_api.cpp`
- `core/src/api/zlink.cpp`
- `core/src/api/socket_api.cpp`
- `core/src/api/service_spot_api.cpp`
- `core/src/api/service_spot_node_api.cpp`
- `core/src/api/service_gateway_api.cpp`

예상 변경:

- `zlink_discovery_new()` 시그니처에 `service_name` 추가
- `zlink_service_type_t`에 `ZLINK_SERVICE_TYPE_SOCKET` 추가
- raw socket용 `zlink_socket_attach_discovery()` 선언 / 구현 추가
- `zlink_spot_new()` 생성 surface에서 constructor service ownership 제거
- `spot`이 독립 attach 대상이 아님을 public surface에 반영
- `gateway` / `spot` / `spot_node` 생성 surface에서 constructor service ownership 제거
- attach 상태 raw socket의 `connect` / `disconnect` / `unbind` / `close/destroy` gate 추가

원칙:

- raw socket service mode용 새 public API는 최소 1개만 추가한다.
- raw socket role 설정 API는 추가하지 않는다.
- raw socket role은 socket type에서 자동 도출한다.

### 16.2 discovery protocol / registry

주요 수정 지점:

- `core/src/services/discovery/discovery_protocol.hpp`
- `core/src/services/discovery/registry.hpp`
- `core/src/services/discovery/registry_state.cpp`
- `core/src/services/discovery/registry.cpp`
- `core/src/services/discovery/discovery_registry_client.cpp`

예상 변경:

- internal service family에 raw socket family 추가
- role enum 및 family-role validation helper 추가
- register / unregister / heartbeat / update-weight / service-list / sync payload에 role 필드 추가
- registry key를 `service_type + service_name + service_role + endpoint`를 다룰 수 있는 구조로 확장
- raw socket family의 fixed weight `1` 정책 반영
- registry topology / summary query surface에 role-aware read model 반영

### 16.3 discovery runtime

주요 수정 지점:

- `core/src/services/discovery/discovery.hpp`
- `core/src/services/discovery/discovery.cpp`
- `core/src/services/discovery/discovery_update.cpp`
- `core/src/services/discovery/discovery_bootstrap.cpp`
- `core/src/services/discovery/discovery_access.cpp`

예상 변경:

- discovery state에 fixed `service_name` 추가
- `provider_info_t`에 `socket_role` 추가
- multi-service map 중심 state를 single service view 중심 state로 축소
- service list 수신 후 자기 `(service_type, service_name)`만 캐시에 반영
- attached participant를 cascade shutdown하는 destroy 경로 추가

### 16.4 raw socket service attachment

주요 수정 지점:

- `core/src/api/zlink.cpp`
- `core/src/api/socket_api.cpp`
- raw socket handle / socket base 관련 소유 상태가 정의된 코드

이 영역은 현재 별도 service attachment abstraction이 없으므로,
새 attachment 상태를 추가하는 위치를 먼저 정하고 그 후 나머지 작업을 진행한다.

필수 구현 항목:

- raw socket -> local role 도출
- attached discovery pointer 저장
- advertise endpoint 1개 계약 저장
- bind 후 register
- discovery update 후 connect / disconnect diff 반영
- attach 상태 API gate

설계 규칙:

- raw socket service attachment 상태는 한 구조체로 모은다.
- socket base 전역에 산발적으로 bool / string을 추가하지 않는다.
- lifecycle / ownership 설명이 2-3문장 안에 가능해야 한다.

### 16.5 gateway / spot 영향 범위

주요 수정 지점:

- `core/src/services/gateway/gateway_lifecycle.cpp`
- `core/src/services/gateway/gateway_refresh.cpp`
- `core/src/services/gateway/gateway_facade.cpp`
- `core/src/api/service_spot_api.cpp`
- `core/src/services/spot/spot_node_lifecycle.cpp`
- `core/src/services/spot/spot_node_control.cpp`
- `core/src/api/service_spot_node_api.cpp`

v1 목표:

- `gateway`, `spot_node`도 discovery-owned service model로 실제 전환
- `spot`은 `spot_node` 내부 구성으로 정렬
- constructor의 service ownership 제거
- discovery attach 이후 register / unregister / refresh 경로를 새 모델로 정렬
- role-aware protocol에서도 기존 gateway / spot data-plane semantics 보존

즉 v1에서 `gateway`와 `spot_node`는 public constructor / attach semantics 개편 대상이고,
`spot`은 그 내부 ownership 모델에 종속되도록 정렬한다.

### 16.6 테스트

주요 수정 지점:

- 기존 discovery e2e / integration 테스트 파일
- raw socket integration 테스트 신규 파일

권장 추가 위치:

- `core/tests/integration/discovery/`
- 필요 시 `core/tests/e2e/discovery/`

필수 테스트 축:

- protocol encode / decode
- registry merge / service-list broadcast
- registry topology / summary query expansion
- discovery single-service filtering
- raw socket attach lifecycle
- raw socket auto connect / disconnect matrix
- attach 상태 API gate
- gateway / spot regression
- registry query regression
- 기존 manual mode regression

## 17. 구현 체크리스트

아래 순서대로 진행하면 된다.

매핑 규칙:

- `15절 Phase`는 설계 관점의 큰 묶음이다.
- `17절 Step`은 실제 코드 작업 단위다.
- 실행 가이드 `5.x`는 commit / push / 검증이 가능한 실행 단위다.
- 대응 관계는 다음과 같다.
  - `15.1 Phase 1` <-> `17.2 Step B` <-> guide `5.2`
  - `15.2 Phase 2` <-> `17.1 Step A`, `17.3 Step C` <-> guide `5.1`, `5.3`
  - `15.3 Phase 3` <-> `17.4 Step D`, `17.5 Step E` <-> guide `5.4`
  - `15.4 Phase 4` <-> `17.6 Step F` <-> guide `5.5`
  - `15.5 Phase 5` <-> `17.8 Step H` 일부 <-> guide `5.6`, `5.8`
  - `15.6 Phase 6` <-> `17.9 Step I` <-> guide `5.7`

### 17.1 Step A: protocol / enum 정리

- `zlink_service_type_t`에 `ZLINK_SERVICE_TYPE_SOCKET` 추가
- internal `service_type_*` 상수에 raw socket family 추가
- `service_role_t` 정의
- family-role validation helper 추가

진행 메모:

- 2026-03-25: `ZLINK_SERVICE_TYPE_SOCKET`, internal raw socket family, `service_role_t`,
  family-role validation helper를 `core/include/zlink.h`,
  `core/src/services/discovery/discovery_protocol.hpp`에 반영하고
  `./core/build/bin/unittest_service_mode_policy`로 검증함.

완료 기준:

- protocol layer가 raw socket family와 role 개념을 이해한다.

### 17.2 Step B: discovery surface ownership 전환

- `zlink_discovery_new(ctx, service_type, service_name)`로 변경
- discovery state에 fixed `service_name` 저장
- discovery snapshot / observer semantics를 single service view 기준으로 정리
- discovery destroy를 attached service participant cascade shutdown 경로로 재정의

완료 기준:

- discovery 하나가 정확히 하나의 서비스만 대표한다.

### 17.3 Step C: registry wire / state 확장

- register / unregister / heartbeat / service-list / sync payload에 role 추가
- registry key / merge / expire 로직 role-aware 변경
- discovery provider snapshot에 role 반영
- registry topology / service summary query가 raw role을 노출하도록 확장

완료 기준:

- 같은 서비스 안에 다중 role provider를 registry가 정확히 보존한다.
- registry query에서도 같은 서비스 안의 다중 role provider를 식별할 수 있다.

### 17.4 Step D: raw socket attachment 도입

- raw socket attach API 추가
- local role 도출
- attached discovery pointer 저장
- advertise endpoint 단일 계약 구현

완료 기준:

- raw socket이 discovery service view participant가 된다.

### 17.5 Step E: bind / register / peer refresh 연결

- `attach -> bind` 경로 구현
- `bind -> attach` 경로 구현
- register / unregister / heartbeat 연동
- discovery update 기반 connect / disconnect diff 구현

완료 기준:

- raw socket이 같은 서비스의 허용 role peer와 자동으로 연결 / 해제된다.

### 17.6 Step F: gateway / spot ownership 전환

- `gateway` 생성 surface에서 constructor service_name 제거
- `spot` 생성 surface에서 constructor service_name 제거
- `spot_node` 생성 surface에서 constructor service_name 제거
- `gateway_attach_discovery`, `spot_node_attach_discovery`가 service ownership source가 되도록 정렬
- `spot`은 `spot_node` 내부 구성으로 두고 독립 attach surface를 추가하지 않음
- bind / register / unregister / refresh 경로가 discovery fixed service_name을 사용하도록 변경

완료 기준:

- `gateway`, `spot_node`는 discovery attach 없이는 service에 속하지 않는다.
- `spot`은 `spot_node`에 종속된 runtime participant로만 service에 참여한다.
- `gateway`와 `spot_node`의 service selection이 constructor가 아니라 discovery attach에서 결정된다.

### 17.7 Step G: attach 상태 API gate

- attach 상태 `connect` 금지
- attach 상태 `disconnect` 금지
- attach 상태 `unbind` 금지
- attach 상태 개별 socket `close/destroy` 금지
- 다중 advertise bind 금지

이 step은 [`13.5 attach 상태 API 금지 규칙`](#135-attach-상태-api-금지-규칙)을
코드로 닫는 실행 단계다.

완료 기준:

- service mode ownership 규칙을 위반하는 public API 호출이 모두 실패한다.

### 17.8 Step H: regression / cleanup

- gateway / spot constructor / attach model 전환
- gateway / spot regression 적응
- raw socket / registry / lifecycle 회귀 테스트 추가
- discovery destroy cascade shutdown 검증
- 문서 / API 주석 정리

완료 기준:

- raw socket, gateway, spot_node가 모두 discovery-owned service model로 정렬된다.
- data-plane semantics는 유지되고 service ownership semantics만 일관되게 바뀐다.

### 17.9 Step I: POSD 리팩토링

- service ownership 관련 중복 필드 제거
- attach / register / peer refresh 정책 공통화
- gateway / spot / raw socket 사이의 반복 조건문과 얕은 래퍼 정리
- 변경 증폭이 큰 경로를 재구성해 이후 service family 추가 비용을 낮춤

완료 기준:

- 새 모델의 핵심 정책이 공통 코드에 모여 있다.
- family별 예외 규칙이 최소화되어 있다.
- 같은 정책 변경 시 수정 파일 수가 눈에 띄게 줄어든다.

## 18. Definition of Done

- discovery가 생성 시 `(service_type, service_name)`를 고정한다.
- discovery를 attach한 소켓은 discovery의 `service_name`에 속한다고 정의된다.
- raw `ROUTER`, `DEALER`, `PUB`, `SUB`가 discovery attach만으로 서비스 자동 연결을 사용한다.
- `gateway`는 discovery attach만으로 서비스 선택과 자동 연결을 수행한다.
- `spot_node`는 discovery attach만으로 서비스 선택과 자동 연결을 수행한다.
- `spot`은 `spot_node` 내부 구성으로 참여하고 독립 attach 대상이 아니다.
- registry / discovery가 `service_name + socket_role + endpoint`를 기준으로 provider를 관리한다.
- registry 조회 surface가 raw `router/dealer/pub/sub` role 정보를 노출한다.
- 같은 서비스 안에서 role 매칭 규칙이 코드와 문서에서 동일하다.
- service-attached raw socket의 advertise endpoint 계약이 단일 endpoint로 고정된다.
- attach 상태의 raw socket에서 manual `connect` / `disconnect` / `unbind`가 금지된다.
- attach 상태의 service participant에서 개별 socket `close/destroy`가 금지된다.
- discovery destroy가 attach된 service participant를 unregister, disconnect, close까지 포함해 cascade shutdown한다.
- discovery destroy 완료 후 attach되었던 socket handle은 무효다.
- discovery 하나를 여러 소켓이 공유하는 수명 계약이 테스트로 검증된다.
- `gateway`, `spot_node`, raw socket이 모두 같은 discovery-owned service ownership 모델로 설명 가능하다.
- 회귀 테스트가 추가되어 기존 manual mode / gateway / spot / registry 조회 계약이 보호된다.
- 구현 후 POSD 기준 리팩토링이 적용되어 service ownership 관련 중복 상태와 얕은 래퍼가 줄어든다.

## 19. 열린 이슈

이번 문서에서 후속 과제로 남기는 항목:

- observer callback surface를 service-name-free 형태로 단순화할지
- raw socket용 topology / introspection public API 확장
- `XPUB`, `XSUB` 자동 연결 지원
- raw socket service mode의 single advertise bind 제한이 기존 multi-bind 사용자에게 주는 제약과 대안 surface 검토
- `spot_node`가 discovery-owned model로 전환될 때 mesh topology 구성 책임이 constructor/setup 코드에서 attach/runtime 쪽으로 이동하는지 검토
