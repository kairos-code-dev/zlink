# Service Routing ID 정책 계획

## 1. 목적

이 문서는 public service-facing handle의 representative `routing_id`
정책을 공통 규칙으로 고정한다.

이 문서의 canonical 전제는
[`direct-callback-recv-rewrite-spec.ko.md`](./direct-callback-recv-rewrite-spec.ko.md)
다.

핵심 목표:

- 모든 public service-facing handle이 대표 `routing_id`를 갖게 한다.
- `routing_id`를 monitor와 registry topology에서 공통 식별자로 쓴다.
- first-use 이후 identity 변경을 금지해 handle identity를 안정화한다.

## 2. 범위와 비범위

### 2.1 범위

이 문서의 범위에 들어가는 public subject:

- `Gateway`
- `SpotPub`
- `SpotSub`
- `Discovery`

각 subject는 representative `routing_id`를 가진다.

### 2.2 비범위

다음은 public representative identity 대상으로 보지 않는다.

- `SpotNode` internal `DEALER`
- internal monitor socket
- registry cluster sync socket
- raw benchmark/test 전용 socket

## 3. 서비스별 representative RID

| 서비스 | representative socket | representative RID |
|---|---|---|
| `Gateway` | `ROUTER` | `router rid` |
| `SpotPub` | `PUB` | `pub rid` |
| `SpotSub` | `SUB` | `sub rid` |
| `Discovery` | `SUB` | `sub rid` |

중요한 점:

- `SpotPub`와 `SpotSub`는 서로 다른 subject이므로 RID도 서로 다를 수 있다.
- `SpotNode`는 wiring owner이지 RID topology subject가 아니다.

## 4. 생성과 override 정책

### 4.1 기본 정책

- representative socket을 생성할 때 RID가 없으면 자동 생성한다.
- 사용자가 override를 지정하면 자동 생성 대신 그 값을 쓴다.
- 생성된 RID는 monitor와 registry reporting의 대표 식별자로 재사용한다.

### 4.2 first-use 정책

override는 허용하되 first-use 이전까지만 허용한다.

first-use 예:

- `gateway`: bind/connect/register 전
- `spot_pub`: 첫 publish 또는 attach/use 전
- `spot_sub`: 첫 subscribe/handler attach 또는 attach/use 전
- `discovery`: connect/subscribe 전

first-use 이후 변경 시 권장 동작:

- `EFSM` 또는 equivalent error 반환

### 4.3 권장 API

```c
int zlink_gateway_set_routing_id(void *gateway,
                                 const void *data,
                                 size_t size);

int zlink_spot_pub_set_routing_id(void *pub,
                                  const void *data,
                                  size_t size);

int zlink_spot_sub_set_routing_id(void *sub,
                                  const void *data,
                                  size_t size);

int zlink_discovery_set_routing_id(void *discovery,
                                   const void *data,
                                   size_t size);
```

getter:

```c
int zlink_gateway_routing_id(void *gateway, zlink_routing_id_t *out);
int zlink_spot_pub_routing_id(void *pub, zlink_routing_id_t *out);
int zlink_spot_sub_routing_id(void *sub, zlink_routing_id_t *out);
int zlink_discovery_routing_id(void *discovery, zlink_routing_id_t *out);
```

## 5. 충돌 정책

### 5.1 기본 정책

- RID 충돌 책임은 사용자에게 있다.
- 구현이 충돌을 조용히 무시하면 안 된다.
- 충돌이 감지되면 반드시 오류를 반환해야 한다.

### 5.2 registry key 범위

registry domain에서 entry key는 다음 조합으로 본다.

```text
service_kind + representative routing_id + service_name
```

이유:

- 하나의 `Gateway`가 여러 `service_name`을 소비할 수 있다.
- `routing_id`만 단독 key로 쓰면 multi-service gateway를 표현하기 어렵다.

## 6. monitor와의 관계

monitor event는 representative RID를 담을 수 있어야 한다.

RID의 의미:

```text
이 event가 어떤 public service subject에서 나왔는가
```

service-level event든 peer 변화 event든
event subject의 representative RID는 그대로 유지한다.

## 7. registry와의 관계

registry topology entry의 대표 식별자도 representative RID를 쓴다.

즉 registry는 다음 전제를 따른다.

- `Gateway` entry는 `router rid`
- `SpotPub` entry는 `pub rid`
- `SpotSub` entry는 `sub rid`
- `Discovery` entry는 `sub rid`

`instance_id`는 보조 grouping 정보는 될 수 있어도
1차 identity key는 아니다.

## 8. 구현 차이와 우선순위

이미 비교적 맞는 부분:

- `Gateway`는 router RID 생성/override 경로가 있다.

보강이 필요한 부분:

- `SpotPub` representative RID
- `SpotSub` representative RID
- `Discovery` representative RID
- getter API와 monitor/registry 연결

권장 순서:

1. 공통 getter/setter API shape 확정
2. `Gateway` first-use 규칙 확정
3. `SpotPub` / `SpotSub` RID contract 추가
4. `Discovery` RID contract 추가

## 9. 테스트 계획

- auto-generated RID가 비어 있지 않음
- user override가 적용됨
- first-use 이후 override가 실패함
- override 충돌 시 오류 발생
- monitor event에서 representative RID로 service 구분 가능
- registry topology entry가 representative RID 기준으로 조회 가능
- `SpotPub` / `SpotSub`가 서로 다른 RID를 가질 수 있음

## 10. Definition of Done

- `Gateway`, `SpotPub`, `SpotSub`, `Discovery` representative RID 계약이 문서에 고정되어 있다.
- first-use 이후 RID 변경 금지 규칙이 명시되어 있다.
- monitor와 registry topology가 같은 RID 의미를 쓴다.
- `receiver`가 public RID subject에서 제거되어 있다.
