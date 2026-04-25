[스펙 목차](../README.ko.md)

# Draft -- Peer Weight

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 상수를
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 `DEALER`가 여러 peer 중 하나를 골라 메시지를 보낼 때, 모든 peer를 같은
비율로 고르는 대신 peer가 광고한 가중치에 따라 라우팅 비율을 조정하는 기능을
정리한다.

핵심 목표는 아래와 같다.

- 처리 능력이 큰 peer가 더 많은 메시지를 받게 한다.
- `DEALER -> ROUTER`와 `DEALER -> DEALER` 관계를 같은 규칙으로 다룬다.
- 사용자가 sender 쪽에서 각 peer를 직접 식별하지 않아도 되게 한다.
- 사용자가 별도 routing policy를 설정하지 않아도 기본 round-robin 성능을 유지한다.

## 2. 범위

이 기능은 **보내는 쪽이 `DEALER`인 outbound 선택**에 적용한다.

직접 영향을 받는 관계는 아래와 같다.

- `DEALER -> ROUTER`
- `DEALER -> DEALER`

직접 바꾸지 않는 동작은 아래와 같다.

- `ROUTER`가 routing id를 지정해 보내는 명시 라우팅
- `ROUTER`의 mandatory, handover 정책
- peer 연결 생성과 종료 정책
- 수신 큐의 처리 순서

즉 이 기능은 `DEALER` 내부의 outbound load balancer가 다음 메시지를 어느 pipe로
보낼지 고르는 규칙만 바꾼다. 명시적인 routing id를 쓰는 send는 대상이 이미
정해져 있으므로 이 기능의 직접 대상이 아니다.

## 3. 기본 의미

peer weight는 **이 소켓이 연결된 peer에게 광고하는 수신 가중치**다.

예를 들어 worker A가 weight `100`, worker B가 weight `50`을 광고하면, 두
worker에 연결된 `DEALER`는 새 메시지를 대략 `2:1` 비율로 A와 B에 보내야 한다.

이 값은 로컬 소켓이 자기 send 비율을 정하는 값이 아니다. 로컬 소켓이 peer에게
"나는 이 정도 비율로 새 메시지를 받아도 된다"라고 알리는 값이다.

기본값은 `100`이다. 사용자는 이를 정상 처리 능력의 기준으로 보고, 부하가 있는
서버는 `70`, `50` 같은 값으로 낮출 수 있다. 값은 상대 비율이므로 모든 peer가
같은 값을 광고하면 실제 분배는 균등하다.

`0`은 새 메시지 후보에서 빠진다는 뜻이다. 이 값은 routing policy와 관계없이 항상
후보 제외로 해석한다.

## 4. 공개 C API 변경 초안

현재 초안은 사용자가 server 쪽 weight만 설정하는 표면을 목표로 한다. sender인
`DEALER`에 별도 routing policy 옵션을 추가하지 않는다.

### 4.1 추가할 공개 옵션

기존 타입별 옵션 표면을 유지한다. 옵션 이름은 타입별 enum 안에 있으므로 별도 타입
접두 의미를 다시 넣지 않고 `WEIGHT`로 둔다.

이 기능을 위해 새로 추가되는 C API 표면은 아래 **옵션 상수**다. 새 함수는
추가하지 않는다.

| 추가 옵션 | enum | 설정 함수 | 대상 타입 | 값 |
|-----------|------|-----------|----------|----|
| `ZLINK_ROUTER_OPT_WEIGHT` | `zlink_router_option_t` | `zlink_set_router_option()` | `ROUTER` | `int`, `0..100` |
| `ZLINK_DEALER_OPT_WEIGHT` | `zlink_dealer_option_t` | `zlink_set_dealer_option()` | `DEALER` | `int`, `0..100` |

값의 의미는 모든 weight 옵션이 같다.

- `100`: 기본 처리 능력
- `1..99`: 기본보다 낮은 처리 비율
- `0`: 새 메시지 후보에서 제외

```c
typedef enum zlink_router_option_t
{
    /* existing values ... */
    ZLINK_ROUTER_OPT_WEIGHT = 0x3106
} zlink_router_option_t;

typedef enum zlink_dealer_option_t
{
    /* existing values ... */
    ZLINK_DEALER_OPT_WEIGHT = 0x3203
} zlink_dealer_option_t;
```

설정 함수는 기존 타입별 옵션 함수를 그대로 사용한다.

```c
zlink_config_result_t zlink_set_router_option (
  void *handle_,
  zlink_router_option_t option_,
  const void *optval_,
  size_t optvallen_);

zlink_config_result_t zlink_set_dealer_option (
  void *handle_,
  zlink_dealer_option_t option_,
  const void *optval_,
  size_t optvallen_);
```

초안 기준 값 형식은 `int`다.

- 기본값: `100`
- 유효 범위: `0..100`
- `0`: 새 메시지 후보에서 제외
- `1..100`: 후보에 포함하며, 값은 weighted routing에서 상대 비율로 사용
- `100`보다 큰 값 또는 음수 값: 실패
- 잘못된 `optvallen_`: 실패
- 지원하지 않는 handle 타입: 실패

`ROUTER`와 `DEALER`는 같은 의미의 weight를 광고하지만, 사용자는 각 타입의 옵션
함수로 설정한다. `SpotNode`와 `Spot`에는 weight 설정 옵션을 추가하지 않는다.
weight는 `DEALER`가 raw socket peer를 고를 때 쓰는 라우팅 입력값이며,
service/Spot 객체 자체의 설정값이 아니기 때문이다. 이미 구현된 Spot/SpotNode
weight 설정 표면의 제거 계획은 `spot-weight-option-removal.ko.md`에서 별도로
다룬다.

```c
int weight = 70;
zlink_set_router_option (router, ZLINK_ROUTER_OPT_WEIGHT,
                         &weight, sizeof (weight));

weight = 0;
zlink_set_dealer_option (dealer, ZLINK_DEALER_OPT_WEIGHT,
                         &weight, sizeof (weight));
```

조회는 기존 get 함수가 있는 타입에서만 제공한다.

- `zlink_get_router_option()`은 `ZLINK_ROUTER_OPT_WEIGHT`를 조회할 수 있어야 한다.
- 현재 `DEALER`에는 공개 get 함수가 없으므로 C core는 `DEALER` weight 조회를 새로
  추가하지 않는다.

바인딩은 타입별 option facade에 로컬 캐시를 둘 수 있다. 다만 C core 공개 계약은
새 get 함수를 추가하지 않는다.

### 4.2 추가하지 않을 공개 API

이 초안은 아래 공개 API를 추가하지 않는다.

```c
/* 추가하지 않음 */
ZLINK_DEALER_OPT_ROUTING_POLICY

/* 추가하지 않음 */
zlink_dealer_set_peer_weight (...);
```

추가하지 않는 이유는 아래와 같다.

- 사용자는 받는 쪽 `ROUTER` 또는 worker `DEALER`의 weight만 설정하면 된다.
- sender `DEALER`는 peer weight 상태를 보고 내부에서 자동으로 round-robin fast
  path와 weighted path를 고른다.
- peer별 setter는 sender가 peer identity를 직접 관리하게 만들어 사용 모델을
  복잡하게 만든다.

### 4.3 삭제할 공개 C API 표면

이 초안의 최종 목표는 "새 메시지 후보 제외"와 "상대 비율 조정"을 weight 하나로
설명하는 것이다. 따라서 이번 변경에서는 admission 관련 공개 C 표면을 삭제한다.

```c
zlink_config_result_t zlink_set_admission_state (
  void *handle_,
  zlink_admission_state_t state_);

zlink_config_result_t zlink_get_admission_state (
  void *handle_,
  zlink_admission_state_t *state_out_);
```

삭제할 enum은 아래와 같다.

```c
typedef enum zlink_admission_state_t
{
    ZLINK_ADMISSION_SERVING = 1,
    ZLINK_ADMISSION_DRAINING = 2
} zlink_admission_state_t;
```

삭제할 monitor event는 아래와 같다.

```c
ZLINK_SOCKET_MONITOR_EVENT_PEER_ADMISSION_CHANGED
ZLINK_EVENT_PEER_ADMISSION_CHANGED
ZLINK_SERVICE_MONITOR_EVENT_PEER_ADMISSION_CHANGED
```

peer weight 변경을 monitor로 알려야 하는 곳은 admission 이름을 쓰지 않는 새
event로 대체한다.

```c
/* draft name */
ZLINK_SOCKET_MONITOR_EVENT_PEER_WEIGHT_CHANGED
ZLINK_EVENT_PEER_WEIGHT_CHANGED
ZLINK_SERVICE_MONITOR_EVENT_PEER_WEIGHT_CHANGED
```

이름을 바꿀 구조체 필드는 아래와 같다.

```c
zlink_spot_node_peer_entry_t.admission_state
zlink_member_peer_entry_t.admission_state
```

새 필드 이름은 admission을 쓰지 않는다.

```c
/* draft field name */
uint32_t weight;
```

삭제 뒤 의미 대응은 아래와 같다.

| 기존 의미 | 새 의미 |
|----------|--------|
| `ZLINK_ADMISSION_SERVING` | `WEIGHT > 0` |
| `ZLINK_ADMISSION_DRAINING` | `WEIGHT == 0` |

기존 `ZLINK_ADMISSION_DRAINING`의 의미는 "새 메시지 후보에서 제외"다. 이미 선택된
multipart 전송이나 이미 queue에 들어간 메시지의 처리 완료를 보장하는 graceful drain
API는 아니었다. 따라서 `WEIGHT == 0`으로 매핑해도 공개 동작 의미는 같다.

이 변경은 호환성 유지 단계를 두지 않는다. core 공개 헤더, 바인딩, spec, guide,
internals 문서는 같은 작업 단위에서 새 weight 모델로 맞춘다.

service/Spot admission도 같은 enum과 함수를 공유하므로, 이번 변경에서는
service/Spot 쪽 admission 공개 표면을 함께 제거한다. 다만 service/Spot 객체에
weight 설정 옵션을 새로 만들지는 않는다. service/Spot 문서와 snapshot에서는 raw
socket peer가 광고한 weight를 보여주는 방향으로만 맞춘다.

## 5. weight 0 의미

이 초안은 `weight=0`을 새 메시지 후보 제외로 정의한다.

사용자 관점에서는 아래 하나의 값으로 세 가지 상태를 표현할 수 있다.

| Weight | 의미 |
|--------|------|
| `100` | 기본 처리 능력 |
| `1..99` | 기본보다 낮은 처리 비율 |
| `0` | 새 메시지 후보에서 제외 |

따라서 사용자는 `ROUTER`나 worker `DEALER`에서 자기 weight만 설정하면 된다. 별도
sender routing policy를 설정하지 않아도 된다.

기존 admission 상태가 있던 구현 지점은 내부적으로 아래처럼 weight 기반 상태로
바꾼다.

- `weight == 0`: effective weight `0`
- `weight > 0`: effective weight는 advertised weight

이 초안은 공개 사용 모델을 단순하게 만들기 위해 `weight=0`을 후보 제외로
정의한다. `DEALER` outbound 후보 선택에서는 최종적으로 effective weight 하나로
판단한다.

## 6. 전파 모델

weight는 연결된 peer에게 내부 ZMP command로 전파한다.

초안 command 이름은 아래와 같다.

```text
WEIGHT
```

command payload는 아래 의미를 갖는다.

- command name: `WEIGHT`
- value: unsigned 32-bit weight

public API가 허용하는 값은 `0..100`이지만 내부 command payload는 unsigned 32-bit로
둔다. 이유는 기존 peer command 인코딩에서 정수 payload를 32-bit 단위로 다루기 쉽고,
나중에 public weight 범위가 넓어져도 command frame 형식을 다시 바꾸지 않기
위해서다. 이번 구현에서는 command 값이 `100`보다 크면 잘못된 command로 보고
무시한다.

정확한 frame layout은 구현 시점에 기존 command 인코딩 규칙과 맞춘다. 공개
사용자는 이 command frame을 직접 만들거나 해석하지 않는다.

전파 규칙은 아래와 같다.

- 새 pipe가 attach되면 로컬 weight가 기본값 `100`이 아닌 경우 peer에게 보낸다.
- 로컬 weight가 runtime에 변경되면 현재 연결된 peer들에게 새 값을 보낸다.
- peer가 weight command를 받으면 해당 pipe의 remote weight를 갱신한다.
- command가 도착하기 전까지 remote weight는 `100`으로 본다.
- 잘못된 command 값은 무시하고 기존 remote weight를 유지한다.

`DEALER`와 `ROUTER`는 모두 weight command를 보낼 수 있다. 다만 받은 weight를
실제 outbound 선택에 사용하는 쪽은 `DEALER` load balancer다.

## 7. DEALER 라우팅 규칙

`DEALER`는 outbound 가능한 pipe 중 effective weight가 `0`보다 큰 pipe만 후보로
본다. 각 후보의 effective weight는 그 pipe의 remote weight다.

기본 규칙은 아래와 같다.

- remote weight를 아직 모르면 `100`
- remote weight가 `0`이면 후보에서 제외
- positive weight가 모두 같으면 기존 round-robin과 같은 비율
- positive weight가 서로 다르면 그 비율만큼 더 자주 선택
- 후보가 없으면 기존 submit 실패 규칙을 따른다

예시는 아래와 같다.

| Peer | Weight | Effective weight | 후보 여부 | 기대 비율 |
|------|--------|------------------|----------|----------|
| A | 100 | 100 | 포함 | 2 |
| B | 50 | 50 | 포함 | 1 |
| C | 0 | 0 | 제외 | 0 |

이 경우 `DEALER`는 A와 B만 대상으로 삼고, 새 메시지를 대략 `2:1` 비율로 보낸다.

모든 후보의 positive weight가 같으면 값이 무엇이든 균등 분배다.

| Peer | Weight | 기대 비율 |
|------|--------|----------|
| A | 50 | 1 |
| B | 50 | 1 |
| C | 50 | 1 |

즉 모두가 같은 부하 상태라고 광고하면 기존 round-robin과 같은 의미가 된다.

## 8. Multipart 규칙

multipart 메시지는 기존 atomicity 규칙을 유지한다.

- 첫 frame을 보낼 때만 대상 pipe를 고른다.
- 이후 frame은 같은 pipe로 보낸다.
- multipart 중간에 weight가 바뀌어도 현재 메시지에는 적용하지 않는다.
- multipart 중간에 대상 pipe가 끊기면 기존 drop 또는 rollback 규칙을 따른다.

즉 weight는 "새 application message를 시작할 때 어느 pipe를 고를지"에만 영향을
준다.

## 9. 라우팅 알고리즘 요구

구현은 정확한 알고리즘 이름보다 아래 요구를 만족해야 한다.

- 장기적으로 weight 비율을 따른다.
- send hot path에서 동적 할당을 하지 않는다.
- active pipe가 하나뿐인 기존 fast path를 유지한다.
- positive weight가 모두 같은 경우 기존 round-robin fast path를 유지한다.
- pipe attach, terminate, weight 변경을 안전하게 반영한다.
- writable 상태가 아닌 pipe는 기존과 같이 active 후보에서 빠진다.
- send마다 전체 peer를 순회하거나 정렬하지 않는다.

이 기능은 peer가 최대 10,000개까지 붙을 수 있는 환경을 고려해야 한다. 따라서
사용자에게는 server 쪽 weight 설정만 노출하더라도, 내부 구현은 자동으로 빠른
경로를 선택해야 한다.

내부 판단은 아래처럼 둔다.

- active 후보가 하나뿐이면 기존 one-pipe fast path
- active 후보의 positive weight가 모두 같으면 기존 round-robin fast path
- active 후보의 positive weight가 서로 다르면 weighted path

이 판단은 send 시점마다 전체 pipe를 훑어서 하면 안 된다. pipe attach, terminate,
weight 변경 같은 상태 변화 시점에 후보 통계와 weighted schedule을 갱신해야 한다.

구현 후보는 아래 둘이다.

| 방식 | 장점 | 단점 |
|------|------|------|
| repeated slot 방식 | 구현이 단순하고 기존 round-robin과 가깝다 | 큰 weight에서 slot 수가 커질 수 있다 |
| smooth weighted round-robin | 큰 weight에도 상태 크기가 작다 | pipe swap, deactivate 처리와 함께 검증해야 한다 |

초안 단계에서는 `0..100` 범위 제한을 전제로 **repeated slot 방식 또는 정규화된
schedule 방식**을 우선 후보로 본다. 이 방식은 weighted path에서도 send 시점에
다음 slot만 고르면 되므로 hot path 비용을 작게 유지할 수 있다.

다만 모든 peer가 weight `100`인 경우에는 schedule을 만들 필요가 없다. 모두 같은
positive weight이므로 기존 round-robin fast path를 그대로 사용한다.

`smooth weighted round-robin`도 후보가 될 수 있지만, 일반 구현은 send마다 active
후보를 훑기 쉽다. 10,000개 peer 환경에서는 per-send `O(N)` 비용을 피해야 하므로,
이 방식을 쓰려면 별도 자료구조로 hot path 비용을 제한해야 한다.

### 9.1 lb_t 내부 상태 요구

`DEALER`의 outbound 선택은 현재 `lb_t`가 담당한다. 구현은 이 구조를 중심으로
아래 상태를 추가하는 방향을 우선 검토한다.

- pipe별 remote weight
- positive active 후보 수
- active 후보의 positive weight가 모두 같은지 나타내는 플래그
- weighted schedule과 현재 schedule index
- schedule이 오래되었는지 나타내는 dirty 플래그

pipe별 remote weight의 기본값은 `100`이다.

상태 갱신이 필요한 시점은 아래와 같다.

- pipe attach
- pipe terminate
- pipe write activation
- pipe write 실패로 active 후보에서 빠질 때
- remote weight command 수신
- local weight 변경으로 command를 보낼 때

send hot path에서는 아래만 수행해야 한다.

- uniform path: 기존 `_current` 기반 round-robin
- weighted path: 미리 만든 schedule에서 다음 pipe 후보를 읽고 index 증가

send hot path에서 아래 작업은 하지 않는다.

- 전체 pipe 정렬
- 전체 pipe weight 합산
- heap allocation
- 10,000개 후보 전체 scan

### 9.2 weighted schedule 구성

weight 범위가 `0..100`이므로 repeated slot schedule은 현실적인 후보가 될 수 있다.
예를 들어 A `100`, B `50`, C `0`이면 schedule은 A와 B만 포함하고 비율은 `2:1`이
된다.

schedule 구성 규칙은 아래와 같다.

- effective weight가 `0`인 pipe는 schedule에 넣지 않는다.
- positive weight가 모두 같으면 schedule을 만들지 않고 round-robin path를 쓴다.
- positive weight가 서로 다를 때만 schedule을 만든다.
- schedule slot 수는 active positive weight의 합을 그대로 쓰거나, 최대공약수로
  나눈 정규화 값을 쓸 수 있다.
- repeated slot schedule의 구현 상한은 10,000 slot으로 둔다.
- raw slot 합이 10,000을 넘으면 먼저 최대공약수로 정규화한다.
- 정규화 뒤에도 10,000 slot을 넘으면 repeated slot schedule을 만들지 않고,
  10,000개 peer 환경에서도 send마다 전체 scan을 하지 않는 bounded 자료구조를
  사용한다.

예시는 아래와 같다.

| Peer | Weight | 정규화 뒤 slot |
|------|--------|----------------|
| A | 100 | 2 |
| B | 50 | 1 |
| C | 0 | 0 |

이 경우 schedule은 개념적으로 아래 비율을 갖는다.

```text
A, A, B
```

실제 slot 순서는 구현이 정할 수 있다. 공개 계약은 정확한 짧은 순서가 아니라 장기
비율이다. 다만 특정 peer가 긴 시간 연속 선택되는 형태는 latency 관점에서 좋지
않으므로, 가능하면 slot을 고르게 섞는다.

### 9.3 active 후보와 schedule 동기화

`lb_t`는 writable 상태가 아닌 pipe를 active 후보에서 빼는 기존 동작을 유지해야
한다. weighted schedule은 active 후보와 어긋나면 안 된다.

규칙은 아래와 같다.

- pipe가 active 후보에서 빠지면 weighted schedule에서도 빠져야 한다.
- pipe가 다시 writable 상태가 되면 remote weight가 `0`이 아닌 경우 후보에 다시
  들어갈 수 있다.
- remote weight가 `0`으로 바뀌면 writable 상태여도 후보에서 빠져야 한다.
- remote weight가 `0`에서 positive 값으로 바뀌면 writable 상태를 확인한 뒤 후보에
  넣는다.
- schedule 재구성이 필요한 상태 변화는 dirty로 표시하고, send 전에 한 번만
  재구성한다.

dirty schedule 재구성은 send hot path에 들어갈 수 있지만, dirty가 아닌 steady
state에서는 재구성 비용이 없어야 한다. 구현은 dirty 상태가 자주 발생하지 않는
control/update path라는 전제에 기대도 된다.

### 9.4 실패 처리

weighted path에서 선택한 pipe에 write가 실패하면 기존 round-robin path와 같은
의미로 처리한다.

- single-part 첫 frame 실패: 해당 pipe를 active 후보에서 빼고 다른 후보를 시도할
  수 있다.
- multipart 중간 실패: 기존 rollback/drop 규칙을 유지한다.
- 모든 후보가 빠지면 기존 submit 실패 규칙을 따른다.

이 절은 구현자가 기존 `lb_t::_more`, `_dropping`, rollback 불변식을 보존해야 함을
뜻한다. weight 기능 때문에 multipart atomicity가 약해지면 안 된다.

## 10. 상태 변경 시점

weight 변경은 runtime에 허용한다.

적용 시점은 아래처럼 둔다.

- sender가 command를 받기 전까지는 이전 weight를 사용한다.
- command를 받은 뒤 시작하는 새 메시지부터 새 weight를 사용할 수 있다.
- 이미 전송 중인 multipart에는 새 weight를 적용하지 않는다.

이 기능은 best-effort 상태 전파다. 따라서 모든 peer가 정확히 같은 순간에 새
비율로 바뀐다는 보장은 하지 않는다.

## 11. Admission 제거와 effective weight

이번 변경에서는 admission 공개 API를 삭제하고, outbound 후보 결정은 effective
weight 하나로 판단한다.

우선순위는 아래와 같다.

1. pipe가 연결되어 있고 writable 후보인지 확인한다.
2. remote weight가 `0`이면 effective weight를 `0`으로 본다.
3. effective weight가 `0`보다 큰 후보만 선택 대상에 넣는다.
4. positive effective weight가 모두 같으면 round-robin, 다르면 weighted 선택을 한다.

즉 기존 `DRAINING`으로 표현하던 후보 제외는 `weight=0`으로 표현한다.

## 12. 오류와 경계 조건

### 12.1 설정 오류

`ZLINK_*_OPT_WEIGHT` 설정은 아래 조건에서 실패해야 한다.

- handle이 해당 타입이 아니다.
- `optval_`이 `NULL`이다.
- `optvallen_`이 `sizeof (int)`가 아니다.
- 값이 `0`보다 작거나 `100`보다 크다.

초안 기준 errno는 기존 option 설정 실패와 맞춰 `EINVAL`을 우선 후보로 본다.

### 12.2 overflow

공개 입력은 `int`로 받는다. 내부 command는 unsigned 32-bit 값으로 전파할 수
있지만, 공개 계약은 `0..100` 범위를 넘는 값을 허용하지 않는다.

weighted 계산에서 누적값 overflow가 생기지 않도록 내부 weight 합산은 최소
64-bit 정수로 처리해야 한다.

### 12.3 프로토콜 버전 전제

이번 변경은 호환성 유지 없이 진행한다. 같은 배포 단위의 peer는 weight command를
지원해야 한다.

- 새 runtime은 weight command를 보내고 받을 수 있어야 한다.
- weight command를 처리하지 않는 구버전 peer와의 의미 호환은 보장하지 않는다.
- command가 아직 도착하지 않은 새 연결 초기 구간에서만 remote weight 기본값
  `100`을 사용한다.

롤링 배포 중 구버전 peer가 weight command를 무시하면 새 runtime은 그 peer를
기본값 `100`으로 보는 방향으로 동작할 수 있다. 이 동작은 장애를 줄이는 graceful
degradation일 뿐이며, 서로 다른 버전 사이의 정확한 weighted routing 의미를
지원한다는 뜻은 아니다. 운영 기준으로는 같은 peer group 안의 runtime을 같은 배포
단위로 맞추는 것을 전제로 한다.

## 13. 구현 체크리스트

구현은 아래 순서로 진행하는 것을 권한다.

### 13.1 공개 헤더와 옵션 매핑

수정 대상:

- `core/include/zlink_enum.h`
- `core/include/zlink.h`
- `core/src/core/internal_defs.hpp`
- `core/src/api/zlink_option.cpp`
- `core/src/api/zlink_option_specialized_api.cpp`
- `core/src/core/options.hpp`
- `core/src/core/options_core_socket.cpp`
- `core/src/core/options_owner.cpp`

작업 내용:

- `ZLINK_ROUTER_OPT_WEIGHT = 0x3106` 추가
- `ZLINK_DEALER_OPT_WEIGHT = 0x3203` 추가
- 내부 옵션 id 추가
- router/dealer typed option mapping 추가
- 값 검증: `int`, `0..100`
- 기본값 `100` 초기화
- admission 함수와 enum 삭제
- admission monitor event는 weight event로 대체
- admission snapshot field는 weight field로 변경

### 13.2 ZMP peer command

수정 대상:

- `core/src/sockets/socket_base.cpp`
- `core/src/sockets/socket_base.hpp`
- `core/src/sockets/router_admission.cpp` 또는 그 후속 weight 파일
- `core/src/sockets/dealer.cpp`
- `core/src/sockets/router.cpp`

작업 내용:

- 기존 `ADMISSION` command를 `WEIGHT` command로 교체
- command payload에 `0..100` weight를 담는다
- 새 pipe attach 시 local weight가 기본값과 다르면 command 전송
- runtime weight 변경 시 연결된 pipe에 command 전송
- `DEALER`와 `ROUTER` 모두 command를 받을 수 있게 처리
- command 수신 시 해당 pipe의 remote weight를 갱신

command decode 실패는 application payload로 넘기지 말고 peer command 처리에서
무시해야 한다. 잘못된 weight 값은 기존 remote weight를 유지한다.

### 13.3 DEALER load balancer

수정 대상:

- `core/src/sockets/lb.hpp`
- `core/src/sockets/lb.cpp`
- `core/src/sockets/dealer.hpp`
- `core/src/sockets/dealer.cpp`

작업 내용:

- `lb_t`에 pipe별 remote weight 상태 추가
- `lb_t::set_weight(pipe, weight)` 추가
- weight `0` pipe는 active 후보에서 제외
- weight `0 -> positive` 변경 시 writable 확인 뒤 후보 복귀
- positive active weight가 모두 같으면 기존 round-robin path 사용
- positive active weight가 다르면 weighted schedule path 사용
- one-pipe fast path 유지
- multipart `_more`, `_dropping`, rollback 동작 유지

### 13.4 ROUTER 쪽 영향

수정 대상:

- `core/src/sockets/router.hpp`
- `core/src/sockets/router.cpp`
- `core/src/sockets/router_data_path.cpp`
- `core/src/sockets/socket_base_routing.cpp`

작업 내용:

- ROUTER 자신이 광고할 local weight 상태를 갖는다
- ROUTER가 peer weight command를 받을 수 있게 한다
- ROUTER의 명시 routing id send는 weight로 대상을 고르지 않는다
- 기존 admission 기반 target 거부 로직은 weight `0` 의미로 재정의한다

### 13.5 Service와 snapshot 표면

수정 대상:

- `core/include/zlink.h`
- `core/src/services/discovery/*`
- `core/src/services/spot/*`
- `core/src/api/service_option_api.cpp`
- `core/src/api/service_option_spot_api.cpp`

작업 내용:

- `zlink_spot_node_peer_entry_t.admission_state`를 `weight`로 변경
- `zlink_member_peer_entry_t.admission_state`를 `weight`로 변경
- service monitor의 peer admission event를 peer weight event로 변경
- service/Spot 쪽 admission setter/getter 제거
- service discovery와 registry projection에서 admission state 대신 weight 값을
  전파한다
- Spot peer cache와 registry member peer query에서 weight 값을 노출한다

raw socket weight만 구현하면서 admission 공개 API를 삭제하면 service/Spot 문서와
바인딩 surface가 깨진다. 따라서 이번 작업에서는 service/Spot의 admission 기반
조회와 monitor 표면을 weight 기반 조회와 monitor 표면으로 바꾼다. 하지만
`SpotNode`나 `Spot`에 weight 설정 옵션을 추가하지는 않는다.

## 14. Monitoring과 조회

이번 변경은 admission 공개 표면을 제거하므로, admission 이름을 쓰는 monitor event와
snapshot 필드는 남기지 않는다.

필수 반영 항목은 아래와 같다.

- `ZLINK_SOCKET_MONITOR_EVENT_PEER_ADMISSION_CHANGED`를
  `ZLINK_SOCKET_MONITOR_EVENT_PEER_WEIGHT_CHANGED`로 대체
- `ZLINK_SERVICE_MONITOR_EVENT_PEER_ADMISSION_CHANGED`를
  `ZLINK_SERVICE_MONITOR_EVENT_PEER_WEIGHT_CHANGED`로 대체
- `zlink_spot_node_peer_entry_t.admission_state`를 `weight`로 변경
- `zlink_member_peer_entry_t.admission_state`를 `weight`로 변경

peer별 remote weight를 raw socket monitor snapshot에 추가할지는 선택 사항이다.
다만 service/Spot snapshot과 registry member query는 기존 admission field를
대체해야 하므로 weight 값을 반드시 노출해야 한다.

debug log는 공개 계약은 아니지만 구현 검증을 위해 권장한다.

- invalid weight command 수신
- weight command decode 실패
- weighted schedule 재구성
- weighted path에서 pipe write 실패로 후보가 빠지는 경우

## 15. 바인딩 반영 방향

정식화 시 바인딩은 타입별 option facade에 같은 이름의 속성을 추가한다.

- `DealerSocketOptions.weight`
- `RouterSocketOptions.weight`

언어별 이름은 각 바인딩의 기존 option naming 규칙을 따른다. 값의 의미와 기본값은
C core 계약과 같아야 한다.

## 16. 테스트 요구

core 테스트는 최소 아래를 포함해야 한다.

### 16.1 기능 테스트

- `DEALER -> ROUTER`에서 weight `100:50` 비율 검증
- `DEALER -> DEALER`에서 weight `100:50` 비율 검증
- weight 기본값 `100`일 때 기존 round-robin과 같은 분배 검증
- 모든 positive weight가 같은 경우 기존 round-robin과 같은 분배 검증
- weight `0` peer가 후보에서 제외되는지 검증
- weight `0 -> 100` 변경 뒤 후보에 다시 포함되는지 검증
- runtime weight 변경 뒤 새 비율 적용 검증
- multipart가 하나의 peer로만 전달되는지 검증
- 잘못된 weight 설정이 실패하는지 검증
- 삭제된 admission API와 enum을 사용하는 코드는 컴파일 또는 contract test에서
  더 이상 공개 표면으로 잡히지 않아야 한다.
- `zlink_spot_node_peer_entry_t.weight`와 `zlink_member_peer_entry_t.weight`가
  registry/Spot query 결과에 채워지는지 검증

비율 테스트는 짧은 구간의 정확한 순서보다 충분한 메시지 수에서의 count를 확인하는
방식이 좋다. weighted round-robin 구현은 순서가 알고리즘에 따라 달라질 수 있으므로,
공개 계약은 장기 비율을 중심으로 검증해야 한다.

### 16.2 회귀 테스트

기존 동작을 깨지 않는지 확인하기 위해 아래 회귀 테스트를 둔다.

- weight를 설정하지 않은 기존 `DEALER -> ROUTER` 테스트가 이전처럼 통과해야 한다.
- weight를 설정하지 않은 기존 `DEALER -> DEALER` 테스트가 이전처럼 통과해야 한다.
- active pipe가 하나뿐인 send 경로는 기존 one-pipe fast path 의미를 유지해야 한다.
- 모든 active peer weight가 `100`이면 기존 round-robin 순서와 분배가 유지되어야 한다.
- 일부 peer가 weight `0`이어도 남은 peer 사이의 round-robin 순서가 깨지지 않아야 한다.
- 기존 admission API 기반 draining 테스트는 weight `0` 기반 테스트로 대체한다.
- `weight=0` 제외 상태에서 pipe terminate, 재connect, 재활성화 경로가 깨지지
  않아야 한다.
- peer disconnect 뒤 재connect하면 remote weight 기본값 또는 새로 광고된 weight가
  올바르게 적용되어야 한다.
- service monitor의 기존 peer admission event 테스트는 peer weight event 테스트로
  대체한다.
- discovery/registry의 기존 admission state projection 테스트는 weight projection
  테스트로 대체한다.
- `ZLINK_DEALER_OPT_ROUTING_POLICY` 같은 sender routing policy 옵션이 공개 enum에
  추가되지 않았는지 contract test로 확인한다.

### 16.3 성능 회귀 테스트

성능 회귀는 기능 회귀와 별도로 확인한다.

- 모든 peer weight가 같은 경우 send hot path가 weighted schedule을 타지 않는지
  확인한다.
- 10,000개 peer를 가정한 경로에서 send마다 전체 후보 순회 또는 정렬이 없는지
  확인한다.
- weight 변경이 없는 steady state에서 send당 동적 할당이 없는지 확인한다.
- `core/build` runtime을 새로 빌드한 뒤 `bindings/c/perf` 기준으로 기존
  `DEALER` 관련 round-robin 성능이 의미 있게 떨어지지 않는지 확인한다.
- weighted path를 켠 경우에도 schedule 재구성 비용이 send마다 반복되지 않는지
  별도 micro benchmark로 확인한다.

## 17. 문서 반영 계획

구현이 끝나면 아래 문서를 같은 작업 단위에서 갱신한다. 이 변경은 호환성 유지
단계를 두지 않으므로, 기존 admission 공개 표면은 문서에서도 제거하고 weight
모델로 바로 바꾼다.

### 17.1 Core Spec

대상 문서:

- `doc/spec/core/socket/dealer.ko.md`
- `doc/spec/core/socket/dealer.md`
- `doc/spec/core/socket/router.ko.md`
- `doc/spec/core/socket/router.md`
- `doc/spec/core/socket/README.ko.md`
- `doc/spec/core/socket/README.md`
- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
- `doc/spec/core/service/discovery.ko.md`
- `doc/spec/core/service/discovery.md`
- `doc/spec/core/service/registry.ko.md`
- `doc/spec/core/service/registry.md`
- `doc/spec/README.ko.md`
- `doc/spec/README.md`

반영 내용:

- `ZLINK_DEALER_OPT_WEIGHT`와 `ZLINK_ROUTER_OPT_WEIGHT`를 타입별 옵션 표에 추가한다.
- 값 형식, 기본값 `100`, 범위 `0..100`, 실패 조건을 명시한다.
- `weight=0`은 새 메시지 후보 제외라고 명시한다.
- `DEALER -> ROUTER`, `DEALER -> DEALER`에만 weighted outbound 선택이 적용된다고
  명시한다.
- `ROUTER`의 명시 routing id send는 weight로 대상을 고르지 않는다고 명시한다.
- `zlink_set_admission_state()`, `zlink_get_admission_state()`,
  `zlink_admission_state_t`가 공개 계약에서 제거되었음을 반영한다.
- `zlink_spot_node_peer_entry_t.admission_state`와
  `zlink_member_peer_entry_t.admission_state`를 `weight` 필드로 바꾼다.
- peer admission monitor event를 peer weight monitor event로 바꾼다.

### 17.2 Guide

대상 문서:

- `doc/guide/03-3-dealer.ko.md`
- `doc/guide/03-3-dealer.md`
- `doc/guide/03-4-router.ko.md`
- `doc/guide/03-4-router.md`
- `doc/guide/03-0-socket-patterns.ko.md`
- `doc/guide/03-0-socket-patterns.md`
- `doc/guide/07-0-services.ko.md`
- `doc/guide/07-0-services.md`
- `doc/guide/07-3-spot.ko.md`
- `doc/guide/07-3-spot.md`
- `doc/guide/12-socket-options.ko.md`
- `doc/guide/12-socket-options.md`

반영 내용:

- 사용자는 server 역할의 `ROUTER` 또는 worker `DEALER`에서 weight만 설정하면
  된다고 설명한다.
- 기본 weight `100`, 부하 감소 예시 `70`, `50`, 후보 제외 `0`을 예제로 넣는다.
- 모든 positive weight가 같으면 기존 round-robin과 같은 분배가 된다고 설명한다.
- sender `DEALER`에 routing policy를 설정하지 않는다고 설명한다.
- admission 기반 draining 예시는 삭제하고 `weight=0` 예시로 바꾼다.
- service/Spot guide에서는 admission 기반 drain 설명을 제거한다. `SpotNode`나
  `Spot`에서 weight를 설정할 수 있다고 설명하지 않는다.
- 내부 pipe, command frame, schedule 같은 구현 세부는 guide 본문에 넣지 않는다.

### 17.3 Internals

대상 문서:

- `doc/internals/socket-option-defaults.ko.md`
- `doc/internals/socket-option-defaults.md`
- `doc/internals/architecture.md`
- `doc/internals/multipart-atomicity.ko.md`
- `doc/internals/services-internals.md`
- `doc/internals/services-internals.ko.md`
- `doc/internals/spot-internals.md`
- `doc/internals/spot-internals.ko.md`

반영 내용:

- DEALER load balancer가 remote weight를 pipe별 상태로 들고 effective weight로
  후보를 고른다는 내부 구조를 설명한다.
- positive weight가 모두 같으면 기존 round-robin fast path를 타야 한다고 명시한다.
- weight가 서로 다를 때만 weighted schedule을 사용한다고 설명한다.
- 10,000개 peer 환경에서 send마다 전체 후보 순회, 정렬, 동적 할당을 피해야 한다고
  명시한다.
- multipart 첫 frame에서만 대상 pipe를 고르고 나머지 frame은 같은 pipe로 보내는
  기존 atomicity 규칙을 유지한다고 설명한다.
- admission command/state 관련 내부 설명은 weight command/state 설명으로 바꾼다.
- discovery/registry/Spot peer cache에서 admission state를 저장하던 위치는 weight
  저장과 전파 설명으로 바꾼다.

### 17.4 Binding Spec

대상 문서:

- `doc/spec/bindings/README.md`
- `doc/spec/bindings/c/README.md`
- `doc/spec/bindings/cpp/README.md`
- `doc/spec/bindings/dotnet/README.md`
- `doc/spec/bindings/go/README.md`
- `doc/spec/bindings/java/README.md`
- `doc/spec/bindings/node/README.md`
- `doc/spec/bindings/python/README.md`
- `doc/spec/bindings/rust/README.md`

반영 내용:

- 각 언어의 `DealerSocketOptions`, `RouterSocketOptions`에 weight 설정을 추가한다.
- 바인딩 이름은 각 언어 관례를 따른다. 예를 들면 `weight`, `setWeight`,
  `SetWeight`, `with_weight`처럼 기존 option facade 스타일에 맞춘다.
- 값 범위 `0..100`, 기본값 `100`, `0` 후보 제외 의미를 모든 언어 문서에 맞춘다.
- admission 관련 public enum, method, exception mapping, sample은 제거한다.
- sender routing policy option이 없다는 점을 surface 표나 contract 항목에서 확인한다.

### 17.5 Site 문서

문서 사이트가 별도 복사본을 들고 있는 경우 아래도 같은 뜻으로 갱신한다.

- `doc/site/docs/api/socket.ko.md`
- `doc/site/docs/api/socket.md`
- `doc/site/docs/guide/03-3-dealer.ko.md`
- `doc/site/docs/guide/03-3-dealer.md`
- `doc/site/docs/guide/03-4-router.ko.md`
- `doc/site/docs/guide/03-4-router.md`
- `doc/site/docs/guide/07-0-services.ko.md`
- `doc/site/docs/guide/07-0-services.md`
- `doc/site/docs/guide/07-3-spot.ko.md`
- `doc/site/docs/guide/07-3-spot.md`
- `doc/site/docs/guide/12-socket-options.ko.md`
- `doc/site/docs/guide/12-socket-options.md`
- `doc/site/docs/internals/socket-option-defaults.ko.md`
- `doc/site/docs/internals/socket-option-defaults.md`

정식 spec에는 구현과 공개 헤더에 실제로 들어간 옵션 이름, 값 범위, 실패 결과만
반영한다.
