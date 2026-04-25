[스펙 목차](../README.ko.md)

# Draft -- Peer Disconnect by Routing ID

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 endpoint 문자열이 아니라 `routing_id`로 연결된 peer를 찾아 끊는
API와 동작 기준을 정리한다.

현재 `zlink_disconnect()`는 endpoint 문자열을 기준으로 연결을 종료한다. 이 방식은
사용자가 연결을 만든 endpoint를 알고 있을 때는 충분하지만, 실제 수신 경로에서는
peer를 `routing_id`로 구분하는 경우가 많다. 특히 callback이나 routed receive
경로에서는 source peer가 `routing_id`로 전달되므로, 같은 식별자로 해당 연결을
종료할 수 있는 API가 필요하다.

이 초안의 핵심 목표는 아래와 같다.

- endpoint 문자열 없이 source peer를 끊을 수 있게 한다.
- `ROUTER`와 `STREAM`뿐 아니라 일반 소켓에서도 같은 개념을 적용한다.
- `routing_id`를 "라우팅 가능한 대상"이라는 좁은 의미가 아니라 "연결된 peer의
  식별자"로 다룬다.
- `SpotNode`가 target node의 연결을 `node_routing_id` 기준으로 끊을 수 있는
  기반을 마련한다.

## 2. 배경

모든 core socket은 기본 `routing_id`를 가진다. 사용자는 이 값을 직접 설정할 수
있고, 설정하지 않으면 core가 기본값을 만든다.

이 `routing_id`는 소켓 종류에 따라 쓰임이 다르다.

| 쓰임 | 설명 |
|------|------|
| source 구분 | 수신한 메시지가 어느 peer에서 왔는지 알려 주는 식별자 |
| 라우팅 대상 선택 | `ROUTER`, `STREAM`처럼 특정 peer로 보낼 때 쓰는 대상 식별자 |

중요한 점은 두 번째 쓰임이 모든 소켓에 있는 것은 아니지만, 첫 번째 쓰임은 일반
소켓에서도 의미가 있다는 것이다. 따라서 rid 기반 disconnect는 "메시지를 이 rid로
보낸다"는 API가 아니라 "이 rid를 가진 peer 연결을 종료한다"는 API로 정의해야
한다.

## 3. 범위

이 초안은 core socket과 `SpotNode`의 연결 종료 제어를 다룬다.

직접 대상은 아래와 같다.

- raw socket
  - `PAIR`
  - `DEALER`
  - `ROUTER`
  - `PUB`
  - `SUB`
  - `XPUB`
  - `XSUB`
  - `STREAM`
- service
  - `SpotNode`

이 초안이 직접 다루지 않는 것은 아래와 같다.

- `routing_id`로 메시지를 보내는 새 send API
- peer admission 상태 전환
- Discovery provider 등록 해제
- peer reconnect 정책 전체

Discovery나 자동 연결 기능과 함께 쓰는 경우에도, 이 API의 직접 의미는 "현재
로컬 handle에 연결된 peer pipe를 종료한다"로 제한한다.

다만 Discovery가 lifecycle을 소유하는 attached socket에서는 사용자가 직접
연결을 바꾸는 public API를 호출하면 안 된다. 따라서 이 초안은 Discovery attached
socket에 대한 `zlink_disconnect_rid()` 호출도 endpoint 기준 `disconnect`와 같은
manual disconnect로 보고 실패시키는 방향을 제안한다. 이때 실패 의미는 기존
attached socket 정책과 맞춰 `EBUSY` 계열로 둔다.

## 4. 기본 개념

### 4.1 Peer identity

이 초안에서 `routing_id`는 peer identity로 본다.

peer identity는 아래 조건을 만족해야 한다.

- 기본 할당된 `routing_id`는 서로 다른 peer를 구분할 수 있어야 한다.
- 사용자가 직접 `routing_id`를 설정하는 경우에도 한 연결 집합 안에서 peer를
  구분할 수 있어야 한다.
- 빈 `routing_id`는 유효한 disconnect 대상이 아니다.

기본 할당 rid는 유니크하다는 전제에 둔다. 따라서 사용자가 기본값을 그대로 쓰는
일반적인 경우에는 rid 하나가 peer 하나를 안정적으로 가리킨다.

커스텀 rid는 사용자가 직접 정하는 값이므로, 같은 local socket에 연결되는 여러
peer가 같은 rid를 쓰지 않도록 사용자가 관리해야 한다. 이 제약은 API가 임의로
해결할 수 없다. 같은 rid가 둘 이상 관찰되는 경우의 정책은 이 문서의 "중복 rid"
절에서 별도로 정한다.

### 4.2 Endpoint disconnect와의 차이

endpoint 기준 disconnect는 연결을 만든 주소를 대상으로 한다.

rid 기준 disconnect는 실제 연결된 peer identity를 대상으로 한다.

두 기능은 서로 대체 관계가 아니다.

- endpoint 기준 disconnect:
  사용자가 `tcp://127.0.0.1:5555` 같은 주소를 알고 있고, 그 주소와 관련된 연결
  또는 connector/listener를 종료하려는 경우에 쓴다.
- rid 기준 disconnect:
  사용자가 수신 callback, dispatch handler, monitor event처럼 source rid를 함께
  제공하는 경로에서 source rid를 알고 있고, 그 peer만 종료하려는 경우에 쓴다.

## 5. 공개 동작 계약

### 5.1 Core socket 동작

core socket용 rid disconnect의 의미는 아래와 같다.

- `s_`가 가리키는 socket에 현재 연결된 peer 중 `peer_rid_`와 같은
  `routing_id`를 가진 peer 연결을 종료한다.
- 성공하면 해당 peer와 연결된 pipe는 비동기 종료 절차에 들어간다.
- 함수가 성공했다고 해서 remote peer가 이미 종료 이벤트를 처리했다는 뜻은
  아니다.
- 대상 pipe가 아직 source rid 조회 구조에 남아 있고 종료 중인 상태라면 성공을
  반환한다. 이미 종료 정리가 끝나 source rid 조회에서 사라졌다면 대상 없음으로
  실패한다.
- Discovery attached socket에서는 실패한다. attached socket의 peer 연결은
  Discovery가 소유하기 때문이다.

반환 타입은 endpoint disconnect와 같은 계열로 맞추기 위해
`zlink_connect_result_t`를 사용한다.

이번 구현에서는 `zlink_connect_result_t`를 확장한다. 별도 result type을 만들지
않고, 결과 코드 확장에 따른 바인딩 변경도 함께 반영한다. 기존 결과 코드만으로는
"대상 rid 없음", "중복 rid", "Discovery attached 상태"를 충분히 구분할 수 없기
때문이다.

결과 의미는 아래와 같다.

| 결과 의미 | errno | 설명 |
|-----------|------------|------|
| 성공 | `0` | 대상 peer 종료 요청을 넣었다 |
| 잘못된 인자 | `EINVAL` | rid가 비어 있거나 입력이 잘못됐다 |
| 잘못된 handle | `EFAULT` | socket 또는 node handle이 아니다 |
| 지원하지 않음 | `ENOTSUP` | 해당 handle 종류가 rid disconnect를 지원하지 않는다 |
| 대상 없음 | `ENOENT` | 일치하는 source rid를 가진 peer가 없다 |
| 중복 rid | `EADDRINUSE` | 같은 source rid를 가진 peer가 둘 이상이라 대상을 확정할 수 없다 |
| lifecycle 소유권 충돌 | `EBUSY` | Discovery attached 상태처럼 사용자가 직접 끊을 수 없다 |

이번 구현에서 추가할 상수 이름은 아래와 같다.

```c
ZLINK_CONNECT_NOT_FOUND = 605
ZLINK_CONNECT_CONFLICT  = 606
ZLINK_CONNECT_BUSY      = 607
```

이름은 `disconnect` 전용이 아니라 `zlink_connect_result_t` 안에 들어가는 값이므로
너무 구체적인 `DISCONNECT_*` 이름을 피한다.

값은 현재 `zlink_connect_result_t`의 마지막 값인 `ZLINK_CONNECT_INTERNAL_ERROR =
604` 뒤에 이어서 배치한다. 기존 상수의 숫자 값은 바꾸지 않는다. 새 값은
connect/disconnect/unbind result 영역인 601번대 안에서 append-only로 추가한다.

`connect_result_internal::from_errno()` 매핑은 아래처럼 확장한다.

| errno | 결과 |
|-------|------|
| `ENOENT` | `ZLINK_CONNECT_NOT_FOUND` |
| `EADDRINUSE` | `ZLINK_CONNECT_CONFLICT` |
| `EBUSY` | `ZLINK_CONNECT_BUSY` |

이 변경은 endpoint 기준 `zlink_disconnect()`에도 영향을 줄 수 있다. 예를 들어 현재
endpoint가 없을 때 내부 errno가 `ENOENT`라면, 구현 뒤에는
`ZLINK_CONNECT_NOT_FOUND`가 반환된다. 이 초안은 이 결과 코드 개선을 허용한다.
기존 결과 코드에 맞추기 위한 호환성 fallback은 두지 않는다.

### 5.2 SpotNode 동작

`SpotNode`용 rid disconnect의 의미는 아래와 같다.

- `node_`가 유지하는 peer node 연결 중 `target_node_rid_`와 같은 node rid를 가진
  peer를 종료한다.
- endpoint 문자열을 모르는 상태에서도 target `SpotNode`와 관련된 내부 mesh
  socket 연결을 종료할 수 있어야 한다.
- 종료 대상은 target node와 직접 연결된 peer socket들이다.
- target node 아래의 개별 spot rid는 이 API의 대상이 아니다.

이 함수는 현재 `zlink_spot_node_disconnect_peer(node, endpoint)`의 rid 기반
버전이다. endpoint 기준 함수는 계속 유지한다.

## 6. 추가되는 C API와 옵션

이번 기능 구현에서 public C surface에 추가되는 항목은 아래와 같다.

### 6.1 함수

```c
ZLINK_EXPORT zlink_connect_result_t zlink_disconnect_rid (
  void *s_,
  const zlink_routing_id_t *peer_rid_);

ZLINK_EXPORT zlink_connect_result_t zlink_spot_node_disconnect_peer_rid (
  void *node_,
  const zlink_routing_id_t *target_node_rid_);
```

`zlink_disconnect_rid()`는 raw socket handle을 대상으로 한다.
`zlink_spot_node_disconnect_peer_rid()`는 `SpotNode` handle을 대상으로 한다.

`Spot` facade handle에는 별도 rid disconnect 함수를 추가하지 않는다. `Spot`은
개별 peer 연결을 직접 소유하지 않고, peer 연결은 node runtime이 관리하기
때문이다. `Spot`에서 peer node를 끊어야 하면 해당 `Spot`이 속한 `SpotNode`에
`zlink_spot_node_disconnect_peer_rid()`를 호출한다.

### 6.2 결과 코드

`zlink_connect_result_t`에 아래 결과 상수를 추가한다.

```c
ZLINK_CONNECT_NOT_FOUND = 605
ZLINK_CONNECT_CONFLICT  = 606
ZLINK_CONNECT_BUSY      = 607
```

추가 결과는 endpoint 기준 connect/disconnect 계열과 rid 기준 disconnect 계열이
함께 사용한다. 따라서 `connect_result_internal::from_errno()`도 같은 PR에서
수정한다.

### 6.3 rid 중복 정책 option

rid disconnect 자체는 runtime 제어 API이지만, 모든 socket이 `routing_id`를 가질 수
있다면 같은 local socket에 동일한 peer rid가 둘 이상 들어오는 상황을 어떻게
처리할지도 공통 정책으로 정해야 한다.

기존 `ZLINK_ROUTER_OPT_PROBE`와 `ZLINK_DEALER_OPT_PROBE`는 이 정책의 이름으로 쓰면
안 된다. `PROBE`는 peer가 자기 rid를 ROUTER에 알려 주도록 빈 메시지를 보내는
handshake 보조 기능이다. 동일 rid가 이미 있을 때 기존 pipe를 유지할지, 새 pipe가
기존 pipe를 인수할지를 정하는 정책은 `PROBE`가 아니라 duplicate rid 정책이다.

이번 초안은 아래 공통 socket option을 추가하는 방향으로 둔다.

```c
typedef enum zlink_rid_duplicate_policy_t
{
    ZLINK_RID_DUPLICATE_REJECT = 0,
    ZLINK_RID_DUPLICATE_HANDOVER = 1
} zlink_rid_duplicate_policy_t;

ZLINK_OPT_RID_DUPLICATE_POLICY = 0x3033
```

`ZLINK_OPT_RID_DUPLICATE_POLICY`는 `zlink_set_option()`으로 설정하는 common socket
option이다. 바인딩은 언어별 socket option facade에 같은 의미를 노출할 수 있다.
대상은 peer가 광고한 socket routing id를 관찰할 수 있는 raw socket이다.

정책 의미는 아래와 같다.

| 정책 | 의미 |
|------|------|
| `ZLINK_RID_DUPLICATE_REJECT` | 같은 peer rid가 이미 있으면 새 pipe를 등록하지 않고 기존 pipe를 유지한다 |
| `ZLINK_RID_DUPLICATE_HANDOVER` | 같은 peer rid가 이미 있으면 기존 pipe를 종료하고 새 pipe를 등록한다 |

기본값은 `ZLINK_RID_DUPLICATE_REJECT`로 둔다. 이유는 기본 할당 rid가 유니크한
정상 운영에서는 정책 차이가 드러나지 않고, 커스텀 rid 충돌이 생겼을 때는 기존
연결을 갑자기 교체하는 것보다 중복을 명확히 거부하는 편이 안전하기 때문이다.

이 옵션은 설정 시점에 특정 rid 값의 중복 여부를 검사하지 않는다. 중복 여부는
connect, accept, handshake, inproc attach, peer command 수신처럼 peer rid가 실제로
확정되는 시점에만 알 수 있다. 따라서 옵션은 "현재 상태 변경"이 아니라 "나중에
동일 peer rid가 관찰되면 어떤 정책을 적용할지"를 정한다.

일반 소켓에서 이 정책을 적용하려면 peer routing id를 실제로 알 수 있어야 한다.
현재 구조에서 `ROUTER`는 routing-id frame을 받고, inproc 연결은 양쪽 `options`의
routing id를 서로 설정할 수 있다. 반면 tcp/ws 같은 transport의 일반 socket은 항상
peer rid를 받는 구조가 아닐 수 있다. 따라서 구현은 일반 socket에도 peer rid 교환을
도입하거나, peer rid를 알 수 없는 transport에서는 이 옵션을 적용하지 못한 상태로
두어야 한다. 후자의 경우 rid disconnect도 해당 pipe를 rid로 찾을 수 없다.

`STREAM`은 이 정책의 직접 대상에서 제외한다. `STREAM`의 4바이트 rid는 remote peer가
광고한 socket routing id가 아니라 local stream socket이 client connection에 부여한
connection id다. 따라서 duplicate policy가 아니라 `STREAM` 내부 route id 할당 규칙과
route shard 정리 규칙으로 중복을 막는다.

Discovery attached socket에서는 이 옵션의 public 설정을 막거나 Discovery가 정한
값으로 고정해야 한다. attached socket의 peer lifecycle은 Discovery가 소유하기
때문이다.

기존 `ZLINK_ROUTER_OPT_HANDOVER`는 이 공통 옵션으로 흡수한다. 구현은 호환성 유지
단계를 두지 않으므로, 정식 공개 계약에서는 router 전용 handover option을 제거하고
`ZLINK_OPT_RID_DUPLICATE_POLICY`만 남기는 방향으로 둔다. router 전용 동작이 필요한
경우도 같은 option을 router handle에 설정해서 표현한다. 이 변경은 기존 ROUTER의
기본 handover 동작을 바꿀 수 있으므로 테스트와 guide에서 명확히 알린다.

### 6.4 추가하지 않는 option

이번 기능은 아래 option은 추가하지 않는다.

추가하지 않는 이유는 아래와 같다.

- Discovery attached 상태 허용 여부는 option으로 바꾸지 않는다. attached lifecycle
  소유권을 지키기 위해 항상 실패한다.
- `PROBE` 이름의 공통 option은 추가하지 않는다. 중복 rid 정책은 probe가 아니라
  duplicate policy이기 때문이다.

### 6.5 내부 helper

public C API는 아래 내부 helper로 들어간다.

```c
/* Draft internal shape only */
int socket_base_t::term_peer_by_rid (const zlink_routing_id_t *peer_rid_);
```

Discovery attached 상태를 막기 위한 내부 lifecycle hook도 추가한다.

```c
/* Draft internal shape only */
int socket_discovery_attachment_t::on_public_disconnect_rid () const;
```

이 hook은 endpoint 기준 `on_public_term_endpoint()`와 같은 정책을 따른다.

## 7. 입력 검증

`peer_rid_` 또는 `target_node_rid_`는 아래 조건을 만족해야 한다.

- 포인터가 `NULL`이면 실패한다.
- `size`가 `0`이면 실패한다.

현재 `zlink_routing_id_t`는 `uint8_t size`와 `uint8_t data[255]` 구조이므로,
`size`가 `data` 배열 크기보다 큰 값은 정상 C API 입력으로 표현되지 않는다.
공개 API에서는 `size > 0`을 핵심 검증 조건으로 둔다.

잘못된 입력은 `EINVAL` 성격의 실패로 본다.

## 8. 소켓별 동작

### 8.1 ROUTER

`ROUTER`는 rid로 peer pipe를 찾는 구조를 이미 가진다.

초안 동작은 아래와 같다.

- `peer_rid_`와 같은 peer를 찾는다.
- 찾은 pipe에 `terminate(false)`를 요청한다.
- 현재 multipart receive 중인 peer라면 현재 처리 중인 메시지 경계와 종료 경합이
  생길 수 있다. 이 경우 기존 pipe 종료 규칙을 따른다.
- 종료 후 같은 rid로 보내는 `zlink_send_part_rid()` 계열 호출은 대상 없음으로
  실패해야 한다.

### 8.2 STREAM

`STREAM`은 client마다 4바이트 rid를 부여하고, 이 rid로 route table을 유지한다.

초안 동작은 아래와 같다.

- `peer_rid_`는 `STREAM`이 노출한 4바이트 rid여야 한다.
- rid가 일치하는 client pipe를 찾아 `terminate(false)`를 요청한다.
- raw callback 또는 packet callback 안에서 현재 callback의 source rid를 넘기면
  그 client 연결을 종료할 수 있어야 한다.
- callback 안에서 같은 stream으로 일반 close를 호출하는 것은 별도 제한이 있을 수
  있지만, rid 기반 disconnect는 "현재 client만 끊기"라는 안전한 제어로 설계한다.

`STREAM`의 rid 크기는 현재 구현 기준으로 4바이트다. 정식 계약에 넣을 때는
`STREAM` source rid의 크기와 바이트 순서를 기존 stream spec과 맞춰야 한다.

### 8.3 DEALER

`DEALER`는 여러 peer로 outbound를 load balance할 수 있지만, public send API에서
특정 rid로 보내는 의미는 없다.

rid 기반 disconnect에서는 이 차이가 문제가 되지 않는다.

- 수신 callback, dispatch handler, monitor event처럼 source rid를 함께 제공하는
  경로에서 source rid를 얻는다.
- 같은 rid를 가진 peer pipe를 찾아 종료한다.
- 종료된 pipe는 이후 load balance 후보에서 제거된다.

즉 `DEALER`에서 rid는 "보낼 대상"이 아니라 "끊을 대상"이다.

현재 일반 `recv()` 또는 단순 part receive 경로가 모든 일반 소켓에서 source rid를
항상 노출하는 것은 아니다. 따라서 이 API를 구현한다고 해서 모든 receive API가
자동으로 source rid를 반환해야 하는 것은 아니다. source rid 노출 범위를 넓히려면
별도 receive surface 변경으로 다룬다.

### 8.4 PAIR

`PAIR`는 보통 하나의 peer만 가진다.

초안 동작은 아래와 같다.

- 현재 peer의 rid가 `peer_rid_`와 같으면 해당 pipe를 종료한다.
- rid가 다르면 대상 없음으로 실패한다.

### 8.5 PUB, SUB, XPUB, XSUB

pub/sub 계열에서도 source 구분용 rid는 의미가 있다. 다만 메시지를 특정 rid로
보내는 라우팅 의미는 없다.

초안 동작은 아래와 같다.

- `PUB` 또는 `XPUB`은 연결된 subscriber 계열 peer를 rid로 끊을 수 있다.
- `SUB` 또는 `XSUB`은 연결된 publisher 계열 peer를 rid로 끊을 수 있다.
- subscription state, topic trie, ready count 같은 내부 상태는 기존 pipe 종료
  경로에서 정리되어야 한다.
- 특정 topic만 끊는 기능은 아니다. rid가 가리키는 peer 연결 전체를 종료한다.

## 9. 대상 없음과 중복 rid

### 9.1 대상 없음

일치하는 peer가 없으면 실패한다.

초안 errno 의미는 `ENOENT`로 둔다.

이유는 rid 기반 disconnect가 send target routing이 아니라 연결된 peer를 찾는
lifecycle 제어이기 때문이다. endpoint 기준 disconnect가 대상 endpoint 없음에
가까운 의미를 쓰는 것처럼, rid 기준 disconnect도 "해당 rid를 가진 peer가 현재
없다"로 해석하는 편이 자연스럽다.

### 9.2 중복 rid

기본 할당 rid는 유니크하므로 일반적인 경우 중복은 발생하지 않는다.

중복 rid는 사용자가 커스텀 rid를 잘못 설정한 경우에만 문제가 된다. 이 초안은
중복 rid를 정상 운영 상태로 보지 않는다.

중복 rid는 기본 정책에서는 실패한다.

이유는 disconnect가 파괴적인 제어이기 때문이다. 같은 rid가 둘 이상이면 API는
"정확히 어느 peer를 끊는가"를 보장할 수 없다. 따라서 `EADDRINUSE` 성격의 실패로
처리하고, 어떤 연결도 끊지 않는 편이 안전하다.

`ZLINK_OPT_RID_DUPLICATE_POLICY`가 `ZLINK_RID_DUPLICATE_REJECT`이면 같은 peer rid를
가진 새 pipe를 등록하지 않는다. 이 경우 rid disconnect 시점에는 중복이 남지 않아야
한다. 그래도 기존 버그, race, 이전 버전 상태, 정책 적용 전 attach 경로 때문에 같은
rid를 가진 pipe가 둘 이상 관찰되면 `ZLINK_CONNECT_CONFLICT`를 반환하고 어떤 pipe도
끊지 않는다.

`ZLINK_OPT_RID_DUPLICATE_POLICY`가 `ZLINK_RID_DUPLICATE_HANDOVER`이면 같은 peer rid를
가진 새 pipe가 기존 pipe를 인수한다. 이 경우 기존 pipe는 종료되고 새 pipe만 해당
rid의 현재 peer가 된다. rid disconnect는 map 또는 source-rid index에 남은 현재
pipe를 대상으로 한다. 일반 socket에서 handover를 구현하려면 fq, lb, dist 같은 pipe
collection에 새 pipe를 붙이기 전에 기존 pipe를 찾아 종료해야 한다. 이미 둘 다
collection에 들어간 뒤에 나중에 정리하면 짧은 시간 동안 중복 peer가 send/recv 후보에
들어갈 수 있다.

`ROUTER`처럼 내부 map이 rid 하나에 pipe 하나만 허용하는 소켓에서는 정상 구현
상태에서 중복 rid가 발생하지 않는다. 이런 소켓은 map lookup 결과를 그대로 사용하며,
`ZLINK_CONNECT_CONFLICT`는 공통 snapshot helper처럼 같은 rid를 가진 pipe를 둘 이상
관찰할 수 있는 방어 경로에서만 발생한다. `STREAM`도 route id 하나에 pipe 하나만
허용하지만, 그 id는 peer가 광고한 socket rid가 아니므로 duplicate policy 대상은
아니다.

## 10. 종료 의미

rid 기반 disconnect는 graceful message drain을 보장하지 않는다.

초안 동작은 아래와 같다.

- 대상 pipe에 `terminate(false)`를 요청한다.
- 아직 remote에 전달되지 않은 local outbound 메시지는 버려질 수 있다.
- 이미 receive queue에 들어온 메시지는 기존 pipe 종료 규칙에 따라 관찰될 수 있다.
- monitor를 사용하는 경우 connection ready 또는 disconnect 계열 이벤트가 뒤따를
  수 있다.

이 동작은 endpoint 기준 `zlink_disconnect()`가 내부 pipe를 종료하는 방식과 같은
철학을 따른다.

## 11. Threading과 callback

이 함수는 public socket API이므로 기존 socket lifecycle guard를 따라야 한다.

초안 기준은 아래와 같다.

- 다른 public API와 동시에 호출될 때 기존 socket public API 동기화 규칙을 따른다.
- socket close 또는 context term과 경합하면 실패하거나 이미 종료 중인 상태로
  처리될 수 있다.
- callback 안에서 호출하는 경우에는 deadlock이 없어야 한다.
- callback 안에서 호출되는 경우에는 오래 걸리는 blocking wait를 하지 않아야 한다.
  함수는 대상 pipe에 종료 요청을 넣고 빠르게 돌아와야 한다.

특히 `STREAM` callback에서 현재 source rid를 끊는 사용 사례가 중요하다.

```c
static void on_stream_packet (
  void *stream,
  const zlink_routing_id_t *source_rid,
  const zlink_msg_t *header,
  const zlink_msg_t *body,
  void *userdata)
{
    if (should_drop_client(header, body)) {
        (void) zlink_disconnect_rid(stream, source_rid);
    }
}
```

위 예시는 초안의 의도만 보여 준다. 정확한 callback 타입과 반환 처리는 구현된
public header를 기준으로 맞춰야 한다.

구현 시 callback 안 호출을 지원하려면 아래 제약을 지켜야 한다.

- public API guard를 잡은 뒤 callback이 끝나기를 기다리는 구조를 만들지 않는다.
- `STREAM` packet callback 경로에서 pipe packet-state lock을 잡은 상태로 다시 같은
  lock을 요구하는 동기 정리 함수를 호출하지 않는다.
- 실제 상태 정리는 기존 pipe termination event 경로에 맡기고, public 함수에서는
  `pipe->terminate(false)` 요청까지만 수행한다.
- callback에서 받은 `source_rid` 포인터는 callback 동안만 유효할 수 있으므로,
  public API 진입 직후 rid 값을 local buffer로 복사한다.

## 12. 내부 구현 방향

### 12.1 공통 helper

core에는 socket 공통 helper를 둔다.

```c
/* Draft internal shape only */
int socket_base_t::term_peer_by_rid (const zlink_routing_id_t *peer_rid_);
```

공통 helper의 기본 구현은 attached pipe snapshot을 만든 뒤 순회한다.

처리 순서는 아래와 같다.

1. 입력 rid를 검증한다.
2. public API 진입 직후 rid 값을 local buffer로 복사한다.
3. 현재 socket에 붙은 pipe 목록의 snapshot을 만든다.
4. 각 pipe에서 이 socket이 source로 관찰하는 peer rid를 계산한다.
5. 정확히 하나가 일치하면 그 pipe에 `terminate(false)`를 요청한다.
6. 일치하는 pipe가 없으면 실패한다.
7. 둘 이상 일치하면 중복 rid 실패로 처리한다.

4번의 "source로 관찰하는 peer rid"는 기존 receive callback이 사용자에게 넘기는
source rid와 같은 의미여야 한다. 구현상 `pipe->get_routing_id()` 또는 peer pipe의
`get_routing_id()` 중 무엇을 쓸지는 소켓 종류와 연결 방향에 따라 달라질 수 있다.
중요한 규칙은 **local socket 자신의 rid를 비교 대상으로 삼지 않는 것**이다.

즉 사용자가 callback이나 monitor event에서 받은 source rid를 그대로
`zlink_disconnect_rid()`에 넘기면 같은 peer가 찾아져야 한다.

snapshot을 먼저 만드는 이유는 두 가지다.

- `pipe->terminate(false)`는 나중에 pipe termination event를 발생시켜 attached pipe
  목록을 바꿀 수 있다.
- 중복 rid를 감지하려면 어느 pipe도 끊기 전에 전체 후보를 확인해야 한다.

따라서 공통 helper는 "찾으면서 바로 terminate" 방식으로 구현하면 안 된다.

snapshot에 들어간 `pipe_t *`는 raw pointer이므로 구현은 수명 안전성을 보장해야
한다. public API guard, socket mailbox 처리 순서, monitor lock 중 어느 것이
`pipe_terminated()`와의 경합을 막는지 코드로 분명해야 한다. 이 보장이 불명확하면
snapshot helper를 그대로 재사용하지 말고, 후보 확인과 종료 요청 사이에 pipe가
해제되지 않는 별도 안전 경로를 만들어야 한다.

대안은 아래 중 하나를 선택한다.

- snapshot 동안 `pipe_t`를 참조로 붙잡을 수 있는 ref count 또는 retain/release
  계층을 추가한다.
- raw pointer 대신 pipe id와 generation을 저장하고, 종료 요청 직전에 socket 소유
  lock 안에서 pipe를 다시 조회한다.
- socket mailbox thread에서만 lookup과 `terminate(false)`를 실행하도록 command를
  보내고, public API는 빠르게 enqueue 결과만 반환한다.
- 기존 route map이나 attached pipe container가 iterator 안정성과 pipe 수명을
  보장하는 경우, 그 보장 조건을 코드 주석과 테스트로 고정한다.

callback 안 호출 경로에서는 오래 걸리는 command drain을 하면 안 된다. 따라서
공통 helper는 callback에서 호출될 수 있는 상황을 고려해 nonblocking 경로로
동작해야 한다. 필요한 command 처리는 public API 진입 전에 이미 안전한 범위에서
끝났거나, 종료 요청 이후 기존 socket event 경로가 처리해야 한다.

### 12.2 ROUTER/STREAM 최적화

`ROUTER`와 `STREAM`은 이미 rid 기반 lookup 구조가 있으므로 override를 둘 수 있다.

- `ROUTER`: `_out_pipes`에서 rid를 찾는다.
- `STREAM`: 4바이트 rid를 decode한 뒤 route shard에서 pipe를 찾거나 `_out_pipes`를
  사용한다.

최적화는 동작을 바꾸면 안 된다. 공통 helper와 같은 입력 검증, 대상 없음, 종료
의미를 유지해야 한다.

다만 최적화 경로도 아래 규칙은 지켜야 한다.

- route map에서 찾은 pipe가 이미 종료 중이면 성공을 반환한다.
- route map에서 rid를 찾지 못하면 대상 없음으로 실패한다.
- route map에서 pipe를 찾은 뒤 map entry를 먼저 지우지 않는다.
- `STREAM`은 rid 크기가 4바이트가 아니면 `EINVAL`로 실패한다.
- `ROUTER`는 rid 크기가 1바이트 이상이면 opaque bytes로 비교한다.

### 12.3 Pipe 종료 후 정리

직접 map에서 먼저 제거하고 pipe를 끊는 방식은 피한다.

권장 순서는 아래와 같다.

1. 대상 pipe를 찾는다.
2. `pipe->terminate(false)`를 호출한다.
3. 실제 map, fq, lb, dist, monitor 상태 정리는 기존 `xpipe_terminated()` 경로가
   처리한다.

이 순서를 지켜야 기존 종료 경로와 이벤트 의미가 갈라지지 않는다.

## 13. SpotNode 적용 방향

`SpotNode`에서 rid 기반 disconnect는 target node rid를 기준으로 한다.

현재 endpoint 기준 peer disconnect는 peer endpoint 문자열을 받아 mesh socket들의
endpoint를 종료한다. rid 기반 함수는 endpoint를 모르는 상태에서도 같은 결과를
내야 하므로, `SpotNode` runtime은 아래 정보를 추적해야 한다.

- peer node rid
- peer pub endpoint
- peer control endpoint
- peer route endpoint가 있다면 해당 endpoint
- 이 peer와 관련된 mesh socket 연결 상태

초안 구현 방향은 아래와 같다.

1. peer handshake나 control snapshot에서 remote node rid를 확인한다.
2. `node_rid -> peer endpoint set` 인덱스를 유지한다.
3. `zlink_spot_node_disconnect_peer_rid()`가 들어오면 target node rid로 endpoint set을
   찾는다.
4. 찾은 endpoint마다 기존 endpoint 기준 disconnect 경로를 호출한다.
5. peer state, ready filter, connected mesh peer state는 기존 endpoint disconnect와
   같은 정리 경로를 사용한다.

target node rid가 여러 endpoint에 매핑될 수 있는 경우가 있다. 예를 들어 같은 peer
node가 여러 transport endpoint로 연결되어 있거나, 재연결 중 이전 endpoint와 새
endpoint가 잠시 공존할 수 있다. 이 경우 `SpotNode` rid 기반 disconnect는 같은
target node rid에 매핑된 모든 peer endpoint를 종료하는 방향이 자연스럽다.

Discovery가 붙은 `SpotNode`에서는 endpoint 기준 peer disconnect와 같은 이유로
rid 기반 peer disconnect도 실패한다. Discovery가 peer 연결 목록과 lifecycle을
소유하기 때문이다. 이 경우 `EBUSY` 계열 결과를 반환한다.

`SpotNode` 구현은 core socket의 `zlink_disconnect_rid()`를 단순히 여러 내부
socket에 호출하는 방식으로 끝나지 않는다. `SpotNode`의 peer state는 endpoint,
control channel, ready filter, mesh 연결 상태를 함께 관리하므로, 먼저
`node_rid -> endpoint set` 인덱스를 통해 기존 endpoint 기준 정리 경로로 들어가는
방향을 기본으로 둔다.

## 14. Monitor 이벤트

rid 기반 disconnect는 새 이벤트 타입을 반드시 요구하지 않는다.

기존 pipe 종료 경로에서 아래 정보가 유지되면 충분하다.

- endpoint pair
- routing id
- ready count 변화
- disconnect reason이 있다면 기존 reason

다만 사용자가 rid 기반 disconnect 호출 결과와 monitor event를 맞춰 보려면,
disconnect event 또는 ready change event에 routing id가 포함되어야 한다. 이미
monitor event가 routing id를 담는 경로가 있다면 그대로 재사용한다.

## 15. 바인딩 반영 방향

정식 구현 후 각 바인딩은 endpoint 기준 `disconnect()`와 구분되는 이름을 제공해야
한다.

언어별 권장 이름은 아래와 같다.

| 바인딩 | 권장 이름 |
|--------|-----------|
| C | `zlink_disconnect_rid` |
| C++ | `socket.disconnect(routing_id)` 또는 `disconnect_rid(routing_id)` |
| Python | `socket.disconnect_rid(routing_id)` |
| Node | `socket.disconnectRid(routingId)` |
| Go | `Socket.DisconnectRID(rid)` |
| Rust | `socket.disconnect_rid(rid)` |

endpoint 문자열과 rid bytes는 타입이 다르므로, 동적 언어에서도 이름을 분리하는
편이 호출 실수를 줄인다.

`SpotNode` 바인딩은 `disconnect_peer_rid()` 계열 이름을 쓴다.

## 16. 테스트 기준

core 테스트는 아래 경우를 포함해야 한다.

- 잘못된 rid 입력이 실패한다.
- 대상 rid가 없으면 실패한다.
- Discovery attached socket에서 `zlink_disconnect_rid()`가 `EBUSY` 계열로 실패한다.
- `PAIR`에서 peer rid로 연결을 끊을 수 있다.
- `DEALER`가 여러 peer 중 source rid 하나만 끊을 수 있다.
- `DEALER`에서 local socket 자신의 rid를 넘겨도 remote peer가 끊기지 않는다.
- `ROUTER`가 target rid peer만 끊고 다른 peer는 유지한다.
- `STREAM` callback에서 source rid peer를 끊을 수 있다.
- `PUB` 또는 `XPUB`이 특정 subscriber peer만 끊을 수 있다.
- `SUB` 또는 `XSUB`이 특정 publisher peer만 끊을 수 있다.
- rid 기반 disconnect 후 monitor ready count가 줄어든다.
- close 또는 endpoint disconnect와 경합해도 crash 없이 종료된다.
- `ZLINK_RID_DUPLICATE_REJECT`에서 같은 peer rid를 가진 새 pipe가 등록되지 않는다.
- `ZLINK_RID_DUPLICATE_HANDOVER`에서 같은 peer rid를 가진 새 pipe가 기존 pipe를
  인수한다.
- tcp/ws 같은 transport의 일반 socket에서 peer rid 교환이 구현되지 않은 경우, 그
  transport에서 duplicate policy와 rid disconnect가 peer rid를 모르는 pipe에 적용되지
  않는다는 점을 테스트나 문서로 고정한다.
- 일반 socket handover에서 기존 pipe와 새 pipe가 동시에 fq, lb, dist 후보에 들어가지
  않는다.
- 방어 경로에서 커스텀 rid 중복이 감지되는 소켓에서는 실패하고 어떤 pipe도 끊지
  않는다.
- 대상 없음, 중복 rid, Discovery attached 상태가 public result code와 바인딩
  예외/에러 값에서 서로 구분된다.
- callback 또는 dispatch handler에서 받은 source rid를 즉시 넘겨도 deadlock 없이
  대상 peer만 종료된다.
- 일반 receive API가 source rid를 노출하지 않는 소켓에서는 문서화된 별도 경로
  없이 rid를 얻을 수 있다고 가정하지 않는다.

회귀 테스트는 아래 경우를 추가로 포함해야 한다.

- endpoint 기준 `zlink_disconnect()`의 대상 없음 결과가 새
  `ZLINK_CONNECT_NOT_FOUND` 매핑으로 관찰된다.
- `zlink_send_part_rid()`와 routed request/reply API의 대상 없음 결과가 rid 기반
  disconnect 결과 확장 때문에 바뀌지 않는다.
- `STREAM`의 4바이트 source rid 크기와 바이트 순서가 기존 stream receive,
  callback, monitor event에서 달라지지 않는다.
- `STREAM`은 `ZLINK_OPT_RID_DUPLICATE_POLICY`의 영향을 받지 않는다.
- `ROUTER` handover 또는 reconnect 이후 새 pipe의 rid map 정리가 깨지지 않는다.
- pub/sub 계열에서 특정 peer를 끊은 뒤 subscription trie, dist/fq 상태, ready
  count가 기존 pipe 종료 경로와 같은 결과를 낸다.
- inproc 연결과 tcp/ws 계열 연결에서 rid 기반 disconnect가 같은 public 의미를
  가진다.
- 아직 route/source-rid 구조에 남아 있는 종료 중 pipe에 같은 rid로 다시 호출하면
  성공한다.
- 종료 정리가 끝나 route/source-rid 구조에서 사라진 rid로 다시 호출하면
  `ZLINK_CONNECT_NOT_FOUND`를 반환한다.

`SpotNode` 테스트는 아래 경우를 포함해야 한다.

- endpoint를 모르는 상태에서 target node rid로 peer를 끊을 수 있다.
- target node rid가 없는 경우 실패한다.
- 같은 target node rid에 여러 endpoint가 있으면 모두 정리된다.
- peer ready filter와 connected mesh peer state가 endpoint 기준 disconnect와 같은
  방식으로 정리된다.
- Discovery가 붙은 상태에서 manual rid disconnect가 `EBUSY` 계열로 막히는지
  검증한다.

## 17. 구현 파일 계획

이번 기능은 public header, C API entry, socket core, service runtime, 바인딩, 테스트가
함께 바뀐다. 구현자는 아래 파일을 먼저 확인하고 같은 변경 단위에서 수정한다.

| 영역 | 파일 또는 위치 | 작업 |
|------|----------------|------|
| public C header | `core/include/zlink.h` | `zlink_disconnect_rid()`, `zlink_spot_node_disconnect_peer_rid()` 선언 추가 |
| public option enum | `core/include/zlink_enum.h` | `ZLINK_OPT_RID_DUPLICATE_POLICY`와 `zlink_rid_duplicate_policy_t` 추가 |
| public result enum | `core/include/zlink_errno.h` | `ZLINK_CONNECT_NOT_FOUND = 605`, `ZLINK_CONNECT_CONFLICT = 606`, `ZLINK_CONNECT_BUSY = 607` 추가 |
| result mapping | `core/src/api/connect_result_internal.hpp` | `ENOENT`, `EADDRINUSE`, `EBUSY` 매핑 추가 |
| option mapping | `core/src/api/zlink_option.cpp`, `core/src/core/options_core_socket.cpp`, `core/src/core/options.hpp` | common socket option으로 rid duplicate policy 저장과 검증 추가 |
| peer rid exchange | `core/src/sockets/socket_base_endpoint.cpp`, transport handshake 경로 | 일반 socket에서 peer rid를 알 수 있는 경로가 충분한지 확인 |
| socket C API | `core/src/api/socket_api.cpp` | `zlink_disconnect_rid()` 구현 추가 |
| SpotNode C API | `core/src/api/service_spot_node_api.cpp` | `zlink_spot_node_disconnect_peer_rid()` 구현 추가 |
| socket base | `core/src/sockets/socket_base.hpp`, `core/src/sockets/socket_base.cpp` 또는 새 분리 파일 | `term_peer_by_rid()` 공통 helper 추가 |
| endpoint runtime | `core/src/sockets/socket_base_endpoint.cpp` | endpoint disconnect와 pipe 종료 정책을 맞춰야 하는지 확인 |
| source rid helper | `core/src/sockets/socket_base_dispatch.cpp` | callback source rid 계산과 rid disconnect 비교 기준 통일 |
| routing sockets | `core/src/sockets/socket_base_routing.cpp` | `ROUTER` 계열 rid lookup과 종료 경로 검토 |
| stream socket | `core/src/sockets/stream.cpp` | 4바이트 rid 검증과 route lookup 기반 종료 경로 추가 |
| Discovery attachment | `core/src/services/discovery/` | attached socket manual disconnect 차단 hook 추가 |
| SpotNode runtime | `core/src/services/spot/` | `node_rid -> endpoint set` 인덱스와 rid 기반 peer disconnect 추가 |
| C tests | `core/tests/` 또는 기존 core test 위치 | raw socket, stream callback, result code 회귀 테스트 추가 |
| service tests | `core/tests/` 또는 기존 service test 위치 | SpotNode rid disconnect와 Discovery attached 차단 테스트 추가 |

파일명은 현재 코드 배치에 따라 조금 달라질 수 있다. 그래도 위 표의 각 책임은
구현 PR에서 빠지면 안 된다.

## 18. 구현 작업 체크리스트

아래 항목을 같은 구현 작업에 포함한다.

- public header에 `zlink_disconnect_rid()`와
  `zlink_spot_node_disconnect_peer_rid()`를 추가한다.
- common socket option에 `ZLINK_OPT_RID_DUPLICATE_POLICY`를 추가하고,
  `ZLINK_RID_DUPLICATE_REJECT`, `ZLINK_RID_DUPLICATE_HANDOVER` 값을 검증한다.
- `zlink_connect_result_t` public enum 정의 위치에 `ZLINK_CONNECT_NOT_FOUND = 605`,
  `ZLINK_CONNECT_CONFLICT = 606`, `ZLINK_CONNECT_BUSY = 607`을 추가한다.
- `connect_result_internal::from_errno()`가 `ENOENT`, `EADDRINUSE`, `EBUSY`를 새
  결과로 매핑하게 수정한다.
- `ZLINK_CONNECT_*` 결과 값의 숫자 배치가 기존 ABI 정책과 충돌하지 않는지 확인한다.
  호환성 fallback은 두지 않지만, enum 값 변경으로 기존 상수의 값이 바뀌면 안 된다.
- core C API 구현 파일에 `zlink_disconnect_rid()` 진입점을 추가하고, SpotNode C API
  구현 파일에 `zlink_spot_node_disconnect_peer_rid()` 진입점을 추가한다.
- C API, C++/Python/Node/Go/Rust 바인딩에서 새 결과 코드를 예외나 에러 값으로
  매핑한다.
- Java와 .NET 바인딩에서도 같은 C API와 결과 코드 매핑을 추가한다.
- socket 공통 helper가 source rid를 계산하는 기준을 기존 callback/monitor source
  rid와 같은 helper로 통일한다.
- peer rid가 확정되는 모든 attach 경로에서 rid duplicate policy를 적용한다. 설정
  시점이 아니라 connect, accept, handshake, inproc attach, peer command 수신처럼
  peer rid를 실제로 알게 되는 시점에 적용해야 한다.
- 일반 socket에 peer rid exchange를 확대할지, peer rid를 알 수 없는 transport에서는
  duplicate policy와 rid disconnect 적용을 제한할지 결정한다. 제한한다면 guide와
  spec에 transport별 제약을 명시한다.
- `STREAM`에는 rid duplicate policy를 적용하지 않는다. `STREAM`은 local connection id
  할당 규칙으로 중복을 막는다.
- 일반 socket handover는 fq, lb, dist에 새 pipe를 붙이기 전에 기존 peer rid pipe를
  찾아 종료하도록 구현한다.
- Discovery attached socket을 막기 위한 내부 hook을 둔다. endpoint 기준
  `on_public_term_endpoint()`와 같은 수준의 `on_public_disconnect_rid()` 성격이면
  충분하다.
- attached pipe snapshot을 만드는 helper를 재사용하거나 새로 추가한다. 순회 도중
  terminate로 목록이 바뀌어도 안전해야 한다.
- snapshot된 `pipe_t *`의 수명 보장을 코드로 확인한다. 보장할 수 없으면 raw pointer
  snapshot을 사용하지 않는다.
- callback 안 호출 경로에서 command drain이나 blocking wait가 발생하지 않도록
  별도 nonblocking 경로를 둔다.
- callback에서 받은 rid 포인터를 내부에 저장하지 않는다. public API 진입 직후 값만
  복사하고, 복사본으로 비교한다.
- `terminate(false)` 호출 전후에 route map, attached pipe list, ready count를 직접
  먼저 지우지 않는다. 기존 pipe 종료 이벤트 경로가 정리하게 둔다.
- `STREAM` callback 안 호출에 대한 deadlock 테스트를 먼저 만든다.
- `SpotNode`는 core socket helper보다 `node_rid -> endpoint set` 인덱스와 기존
  endpoint disconnect 경로를 먼저 설계한다.

구현 순서는 아래처럼 잡는다.

1. common rid duplicate policy option과 결과 enum을 추가한다.
2. `connect_result_internal::from_errno()`를 확장한다.
3. 일반 socket에서 peer rid를 알 수 있는 transport 범위와 제한을 확정한다.
4. peer rid 확정 경로에 duplicate policy 적용 지점을 추가한다.
5. socket 공통 helper와 Discovery attached 차단 hook을 추가한다.
6. `zlink_disconnect_rid()` C API를 추가하고 raw socket 테스트를 붙인다.
7. `ROUTER`와 `STREAM` 최적화 경로를 추가하되, 공통 helper와 같은 테스트를 통과하게
   한다.
8. `SpotNode`의 `node_rid -> endpoint set` 인덱스와
   `zlink_spot_node_disconnect_peer_rid()`를 추가한다.
9. C++/Python/Node/Go/Rust/Java/.NET 바인딩 API와 에러 매핑을 추가한다.
10. guide, internals, core spec, bindings spec, site 문서를 갱신한다.

## 19. 구현 진입 전 최종 리뷰 기준

아래 조건을 만족하면 이 초안은 구현에 들어갈 수 있는 수준으로 본다.

| 기준 | 판정 |
|------|------|
| public C API 이름과 handle 대상이 정해져 있다 | 충족 |
| 새 public result code와 errno 매핑이 정해져 있다 | 충족 |
| 새 public option 추가 여부가 정해져 있다 | 충족. `ZLINK_OPT_RID_DUPLICATE_POLICY`를 추가한다 |
| Discovery attached socket 정책이 정해져 있다 | 충족. `EBUSY` 계열 실패 |
| target 없음 정책이 정해져 있다 | 충족. `ENOENT` 계열 실패 |
| 중복 rid 정책이 정해져 있다 | 충족. 기본 `REJECT`, 선택 `HANDOVER`, 방어 경로 `EADDRINUSE` |
| `STREAM` rid 크기 정책이 정해져 있다 | 충족. 4바이트만 허용 |
| callback 안 호출 정책이 정해져 있다 | 충족. nonblocking 종료 요청만 허용 |
| source rid 비교 기준이 정해져 있다 | 충족. callback과 monitor에서 보이는 peer source rid와 같아야 함 |
| Spot facade 함수 추가 여부가 정해져 있다 | 충족. 추가하지 않는다 |
| SpotNode 종료 기준이 정해져 있다 | 충족. target node rid가 가리키는 endpoint set 전체 |
| 구현 파일 책임이 정해져 있다 | 충족 |
| 회귀 테스트 항목이 정해져 있다 | 충족 |
| 구현 뒤 정식 문서 반영 위치가 정해져 있다 | 충족 |

남은 첫 번째 위험은 일반 socket의 peer rid 관찰 범위다. 현재 구조에서는 모든
transport와 모든 socket type이 항상 peer rid를 받는다고 단정하면 안 된다. 구현은
peer rid exchange를 일반 socket으로 확대할지, 아니면 peer rid를 모르는 transport에
대해 rid duplicate policy와 rid disconnect 적용을 제한할지 먼저 결정해야 한다.

두 번째 위험은 구현 단계에서 코드로 확인해야 하는 수명 문제다. 특히 attached pipe
snapshot의 `pipe_t *`가 `terminate(false)` 호출 시점까지 안전한지 반드시 확인해야
한다. 안전성이 코드 구조로 보장되지 않으면 snapshot 방식은 폐기하고, pipe 수명을
잡아 둘 수 있는 별도 lookup 경로를 만들어야 한다.

세 번째 위험은 callback 재진입 경로다. 구현은 callback 중 public API를 다시 부르는
상황에서 같은 lock을 재획득하지 않는지 테스트로 증명해야 한다. 위 위험들은 설계
결정을 막는 문제는 아니지만, 구현 PR의 필수 검증 항목이다.

네 번째 위험은 기존 ROUTER 기본 동작 변화다. 기존 router 전용 handover 기본값을
공통 duplicate policy 기본값으로 바꾸면 새 연결이 기존 rid를 인수하던 동작이
거부로 바뀔 수 있다. 호환성을 두지 않는 방향은 유지하되, 이 변화는 guide와 migration
note에 명확히 적어야 한다.

## 20. 문서 반영 계획

이 기능은 공개 API, 사용법, 내부 구조, 바인딩 표면이 모두 바뀌므로 구현 PR에 문서
변경을 함께 포함한다. 아래 문서 변경은 선택 사항이 아니라 구현 완료 조건이다.

### 20.1 guide 문서

사용자가 "언제 endpoint disconnect를 쓰고, 언제 rid disconnect를 쓰는가"를 이해할
수 있게 guide를 수정한다.

수정 대상은 아래와 같다.

| 문서 | 반영 내용 |
|------|-----------|
| `doc/guide/03-0-socket-patterns.ko.md` | peer identity로서의 `routing_id`, endpoint disconnect와 rid disconnect의 차이 |
| `doc/guide/03-4-router.ko.md` | `ROUTER`에서 source rid로 특정 peer를 끊는 사용 패턴 |
| `doc/guide/03-5-stream.ko.md` | `STREAM` callback에서 source rid client를 끊는 예시 |
| `doc/guide/07-3-spot.ko.md` | `SpotNode`에서 target node rid 기준 peer disconnect 사용법 |
| `doc/guide/12-socket-options.ko.md` | 기본 rid와 커스텀 rid의 책임, `ZLINK_OPT_RID_DUPLICATE_POLICY`, `PROBE`와 duplicate policy의 차이 |

대응 영문 guide 문서가 있으면 같은 내용을 `.md` 문서에도 반영한다. 한국어
문서만 바꾸고 영문 문서를 남겨 두지 않는다.

### 20.2 internals 문서

구현자가 pipe, route map, monitor event 정리 흐름을 이해할 수 있게 internals를
수정한다.

수정 대상은 아래와 같다.

| 문서 | 반영 내용 |
|------|-----------|
| `doc/internals/stream-socket.ko.md` | 4바이트 source rid, route shard lookup, callback 안 disconnect 안전 조건 |
| `doc/internals/spot-internals.ko.md` | `node_rid -> endpoint set` 인덱스와 endpoint 기준 정리 경로 재사용 |
| 새 문서: `doc/internals/peer-disconnect-rid.ko.md` | socket 공통 helper, attached pipe snapshot, source rid 비교 기준, rid duplicate policy 적용 지점 |

새 internals 문서를 만들면 `doc/README.ko.md`의 internals 목록에도 추가한다.

### 20.3 core spec 문서

공개 계약은 `core/include/zlink.h`를 기준으로 정식 spec에 반영한다.

수정 대상은 아래와 같다.

| 문서 | 반영 내용 |
|------|-----------|
| `doc/spec/core/socket/README.ko.md` | `zlink_disconnect_rid()` 공통 계약, `ZLINK_OPT_RID_DUPLICATE_POLICY`, 결과 코드, Discovery attached 실패 |
| `doc/spec/core/socket/pair.ko.md` | `PAIR`의 peer rid disconnect 동작 |
| `doc/spec/core/socket/dealer.ko.md` | `DEALER`의 source rid disconnect 동작과 send target 아님을 명시 |
| `doc/spec/core/socket/router.ko.md` | `ROUTER` route map 기반 disconnect, 기존 `HANDOVER`와 새 duplicate policy의 정리 방향 |
| `doc/spec/core/socket/pub.ko.md` | publisher 쪽 subscriber peer disconnect 계약 |
| `doc/spec/core/socket/sub.ko.md` | subscriber 쪽 publisher peer disconnect 계약 |
| `doc/spec/core/socket/xpub.ko.md` | subscription state 정리와 peer disconnect 계약 |
| `doc/spec/core/socket/xsub.ko.md` | upstream publisher peer disconnect 계약 |
| `doc/spec/core/socket/stream.ko.md` | 4바이트 rid 검증, callback 안 disconnect 계약 |
| `doc/spec/core/service/spot.ko.md` | `zlink_spot_node_disconnect_peer_rid()` 계약 |

결과 코드가 별도 문서에 정리되어 있으면 `ZLINK_CONNECT_NOT_FOUND`,
`ZLINK_CONNECT_CONFLICT`, `ZLINK_CONNECT_BUSY`를 그 문서에도 추가한다. 별도 문서가
없으면 socket README 또는 공통 API spec에 결과 코드 표를 추가한다.

대응 영문 spec 문서가 있으면 같은 계약을 `.md` 문서에도 반영한다. 정식 spec은
바인딩 구현자가 직접 보는 계약이므로 한국어 문서와 영문 문서의 API 이름, 반환
값, 에러 의미가 달라지면 안 된다.

### 20.4 bindings spec 문서

언어별 바인딩은 같은 기능을 같은 의미로 노출해야 한다. 이름은 언어 관례에 맞추되
endpoint 문자열 disconnect와 혼동되지 않게 분리한다.

수정 대상은 아래와 같다.

| 언어 | 문서 | 반영 내용 |
|------|------|-----------|
| C | `doc/spec/bindings/c/` | `zlink_disconnect_rid`, `zlink_spot_node_disconnect_peer_rid`, `ZLINK_OPT_RID_DUPLICATE_POLICY`, 새 connect result |
| C++ | `doc/spec/bindings/cpp/` | `disconnect_rid`, `spot_node.disconnect_peer_rid`, rid duplicate policy option, 예외/결과 매핑 |
| Python | `doc/spec/bindings/python/` | `Socket.disconnect_rid()`, `SpotNode.disconnect_peer_rid()`, rid duplicate policy option, 예외 매핑 |
| Node | `doc/spec/bindings/node/` | `socket.disconnectRid()`, `spotNode.disconnectPeerRid()`, rid duplicate policy option, error mapping |
| Go | `doc/spec/bindings/go/` | `Socket.DisconnectRID()`, `SpotNode.DisconnectPeerRID()`, rid duplicate policy option, error mapping |
| Rust | `doc/spec/bindings/rust/` | `disconnect_rid`, `disconnect_peer_rid`, rid duplicate policy option, `Result` error mapping |
| Java | `doc/spec/bindings/java/` | `disconnectRid`, `disconnectPeerRid`, rid duplicate policy option, exception mapping |
| .NET | `doc/spec/bindings/dotnet/` | `DisconnectRid`, `DisconnectPeerRid`, rid duplicate policy option, exception/result mapping |

각 언어별 문서는 아래 내용을 공통으로 포함해야 한다.

- rid bytes 입력 타입과 빈 rid 실패
- rid duplicate policy option과 기본값
- 대상 없음, 중복 rid, Discovery attached 상태의 에러 매핑
- callback 안에서 source rid를 넘겨 disconnect할 수 있는지 여부
- endpoint disconnect와 rid disconnect의 차이

### 20.5 site 문서 동기화

`doc/site/docs/` 아래에 guide/spec 내용을 배포용으로 복제해 두는 구조가 유지되는
경우, 구현 PR에서 site 문서도 같이 갱신한다.

최소 반영 대상은 아래와 같다.

- `doc/site/docs/api/socket.ko.md`
- `doc/site/docs/api/spot.ko.md`
- `doc/site/docs/guide/03-0-socket-patterns.ko.md`
- `doc/site/docs/guide/03-4-router.ko.md`
- `doc/site/docs/guide/03-5-stream.ko.md`
- `doc/site/docs/guide/07-3-spot.ko.md`
- `doc/site/docs/guide/12-socket-options.ko.md`
- `doc/site/docs/internals/stream-socket.ko.md`

## 21. 정식 spec 반영 위치

구현이 끝나면 아래 문서에 나누어 반영한다.

- `doc/spec/core/socket/README.ko.md`
  - 공통 rid 기반 disconnect API 계약과 rid duplicate policy option
- 각 socket spec
  - 소켓별 지원 여부와 source rid 의미
- `doc/spec/core/socket/stream.ko.md`
  - `STREAM` rid 크기와 callback 안 disconnect 의미
- `doc/spec/core/service/spot.ko.md`
  - `SpotNode` peer node rid 기반 disconnect 계약
- errno 또는 결과 코드 문서
  - invalid rid, 대상 없음, 중복 rid, unsupported handle 매핑

정식 spec에는 구현된 public header와 테스트로 확인된 내용만 넣는다.
