# Service Routing ID 정책 계획

## 1. 목적

이 문서는 service-facing socket의 `routing_id` 정책을
공통 규칙으로 고정하기 위한 계획을 정의한다.

핵심 목표는 다음과 같다.

- 모든 public service-facing socket이 대표 `routing_id`를 갖게 한다.
- `routing_id`를 monitor와 registry topology에서 공통 식별자로 사용한다.
- 사용자가 필요하면 `routing_id`를 명시적으로 override할 수 있게 한다.
- collision 정책을 명확히 정의해 구현과 문서의 해석 차이를 줄인다.

즉 목표 상태는 다음 한 줄이다.

```text
representative routing_id = service socket identity
```

## 2. 기본 결론

이 문서는 다음 결론을 전제로 한다.

- 모든 service-facing socket은 representative `routing_id`를 가진다.
- `routing_id`는 기본적으로 자동 생성된다.
- 사용자는 원하면 `routing_id`를 직접 지정할 수 있다.
- override에 따른 충돌 책임은 사용자에게 있다.
- 충돌이 감지되면 구현은 반드시 오류를 반환해야 한다.

즉 `routing_id`는 단순한 raw socket option이 아니라
service identity 정책의 일부다.

## 3. 범위와 비범위

### 3.1 범위

이 문서의 범위에 들어가는 service-facing socket:

- `Gateway`의 `ROUTER`
- `Receiver`의 `ROUTER`
- `SpotPub`의 `PUB`
- `SpotSub`의 `SUB`
- `Discovery`의 `SUB`

이 socket들은 모두 representative `routing_id`를 가져야 한다.

### 3.2 비범위

다음 socket은 public representative identity 대상으로 보지 않는다.

- `SpotNode` internal `DEALER`
- `Receiver` internal `DEALER`
- internal monitor socket
- registry cluster sync socket
- raw benchmark/test 전용 socket

즉 `routing_id` 정책은 public service surface 기준이다.

## 4. service별 representative RID

service별 대표 RID는 다음과 같이 고정한다.

| 서비스 | representative socket | representative RID |
|---|---|---|
| `Gateway` | `ROUTER` | `router rid` |
| `Receiver` | `ROUTER` | `router rid` |
| `SpotPub` | `PUB` | `pub rid` |
| `SpotSub` | `SUB` | `sub rid` |
| `Discovery` | `SUB` | `sub rid` |

중요한 점:

- `SpotPub`와 `SpotSub`는 서로 다른 entry이므로 각각 다른 RID를 가진다.
- `SpotNode`는 monitor/registry의 identity 주체가 아니다.
- `SpotNode`는 연결, discovery, registry, TLS 같은 공통 wiring owner로만 남는다.

## 5. 생성과 override 정책

### 5.1 기본 정책

- representative socket이 생성될 때 RID가 없으면 자동 생성한다.
- 사용자가 override를 지정하면 자동 생성 대신 그 값을 사용한다.
- 생성된 RID는 이후 monitor와 registry reporting의 대표 식별자로 재사용한다.

### 5.2 override 정책

사용자 override는 허용한다.

허용 방식 예:

- constructor parameter
- dedicated `set_routing_id` API
- service option 중 identity 전용 항목

권장 방향:

- generic `set_option(..., ROUTING_ID, ...)`보다는
  dedicated identity API가 더 명확하다.
- override는 representative socket의 첫 bind/connect/open/use 이전에만 허용한다.
- socket이 실제로 실현되었거나 monitor/reporting identity로 사용되기 시작한 뒤에는
  `EFSM` 또는 equivalent error를 반환하는 편이 낫다.

예:

```c
int zlink_gateway_set_routing_id(void *gateway,
                                 const void *data,
                                 size_t size);

int zlink_receiver_set_routing_id(void *receiver,
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

### 5.3 getter 정책

monitor/registry/diagnostic 용도로 representative RID getter가 필요하다.

```c
int zlink_gateway_routing_id(void *gateway, zlink_routing_id_t *out);
int zlink_receiver_routing_id(void *receiver, zlink_routing_id_t *out);
int zlink_spot_pub_routing_id(void *pub, zlink_routing_id_t *out);
int zlink_spot_sub_routing_id(void *sub, zlink_routing_id_t *out);
int zlink_discovery_routing_id(void *discovery, zlink_routing_id_t *out);
```

이 getter는 local monitor correlation과 registry reporting에서 공통으로 쓸 수 있다.

## 6. 충돌 정책

### 6.1 기본 정책

- RID 충돌 책임은 사용자에게 있다.
- 구현은 충돌을 조용히 무시하면 안 된다.
- 충돌이 감지되면 반드시 오류를 반환해야 한다.

### 6.2 충돌 판단 범위

registry domain 안에서는 representative RID가 충돌하지 않는 것을 기본으로 본다.

실제 entry key는 다음으로 잡는 것이 안전하다.

```text
service_kind + representative routing_id + service_name
```

이유:

- 하나의 `Gateway`가 여러 `service_name`을 소비할 수 있다.
- 하나의 representative RID가 여러 service subject와 연결될 수 있다.
- `routing_id`만 단독 key로 쓰면 gateway multi-service를 표현하기 어렵다.

### 6.3 충돌 시 동작

충돌이 감지되면:

- API는 오류를 반환한다.
- monitor에는 오류 event가 올라갈 수 있다.
- registry는 conflicting topology entry를 reject하거나 `ERROR` 상태로 남긴다.

권장 errno:

- local identity conflict:
  `EADDRINUSE` 또는 `EINVAL`
- registry-reported conflict:
  service-level status code + `error_code`

## 7. monitor와의 관계

monitor는 representative RID를 기준으로 service identity를 설명할 수 있어야 한다.

기본 원칙:

- monitor event는 event subject의 representative RID를 담을 수 있어야 한다.
- 사용자는 monitor handle의 service와 RID를 대응시켜 event를 구분할 수 있어야 한다.
- peer-specific event가 아니라 service-level event인 경우에도
  representative RID는 그 monitor 주체의 identity로 유지된다.

즉 monitor에서 RID는 다음 의미를 갖는다.

```text
이 event가 어떤 service socket에서 나온 것인가
```

## 8. registry와의 관계

registry topology entry의 대표 식별자도 representative RID를 사용한다.

즉 registry는 다음 전제를 따른다.

- `Gateway` entry는 `router rid`
- `Receiver` entry는 `router rid`
- `SpotPub` entry는 `pub rid`
- `SpotSub` entry는 `sub rid`
- `Discovery` entry는 `sub rid`

따라서 `instance_id`는 필수 전제가 아니다.

registry entry는 다음 필드 조합으로 식별하면 된다.

- `service_kind`
- `routing_id`
- `service_name`

## 9. 현재 구현과의 차이

현재 구현은 부분적으로만 이 정책과 맞아 있다.

이미 비교적 맞는 부분:

- `Gateway` / `Receiver`는 router RID를 생성/override하는 경로가 있다.
- `SpotNode`는 internal control-plane RID를 갖고 registry dealer에 사용한다.

보강이 필요한 부분:

- `SpotPub`의 `PUB rid`
- `SpotSub`의 `SUB rid`
- `Discovery`의 `SUB rid`
- representative RID getter API
- representative RID를 monitor/registry에 직접 연결하는 public surface

즉 현재 구현은 “RID capability는 있음” 단계이고,
이 문서는 이를 “service identity policy”로 끌어올리는 계획이다.

## 10. 권장 구현 순서

### Phase 0: 공통 기반

- representative RID 규칙 문서화
- service-level RID getter/setter API shape 확정

### Phase 1: Gateway / Receiver

- 기존 router RID 경로를 public identity contract로 확정
- getter 추가
- monitor payload와 연결

### Phase 2: SpotPub / SpotSub

- `PUB` / `SUB`에 representative RID 생성/override 추가
- registry topology와 monitor에서 이 RID를 직접 사용

### Phase 3: Discovery

- discovery `SUB` representative RID 추가
- registry topology와의 연결 완성

## 11. 테스트 계획

- auto-generated RID가 비어 있지 않음
- user override가 적용됨
- override 충돌 시 오류 발생
- monitor event에서 representative RID로 service 구분 가능
- registry topology entry가 representative RID를 기준으로 조회 가능
- `SpotPub` / `SpotSub`가 서로 다른 RID를 갖는지 검증
- `Gateway` multi-service entry가 `service_kind + rid + service_name`으로 구분 가능한지 검증

## 12. 리스크와 완화

| 리스크 | 설명 | 완화 |
|---|---|---|
| 서비스별 구현 불균일 | 일부 service만 RID contract를 제대로 반영할 수 있음 | Phase별로 service별 계약을 명시적으로 맞춤 |
| 충돌 해석 차이 | local conflict와 registry conflict가 다르게 처리될 수 있음 | 오류 반환 규칙을 문서에 고정 |
| generic option과 identity 혼동 | `ROUTING_ID`를 단순 socket option으로 볼 수 있음 | identity는 별도 정책 문서로 분리 |
| Spot owner 모델 혼란 | `SpotNode`와 `SpotPub/Sub` 역할이 섞일 수 있음 | `SpotNode = wiring owner`, `SpotPub/Sub = identity subject`로 고정 |

## 13. Definition of Done

- 모든 service-facing socket의 representative RID 정책이 문서에 고정됨
- representative RID getter/setter public shape가 정리됨
- monitor 문서와 registry 문서가 representative RID를 기준으로 정렬됨
- `SpotPub` / `SpotSub`가 별도 identity subject로 정의됨
- `SpotNode`는 wiring owner로만 남는다는 점이 문서에 명시됨

## 14. 자기 리뷰

- representative RID를 socket별이 아니라 service identity로 정의했다.
- `instance_id` 없이도 registry와 monitor를 연결할 수 있는 방향을 제시했다.
- `SpotPub` / `SpotSub`를 별도 RID로 나누는 요구를 명시적으로 반영했다.
- generic option 문서와 identity 정책을 분리해 해석 충돌을 줄이도록 했다.
