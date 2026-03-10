# SpotNode Direct Facade 확장 계획

## 1. 목적

현재 `SPOT` 문서는 `SpotNode`를 bind/connect/discovery/TLS wiring owner로만
설명하고, 실제 송수신은 `SpotPub` / `SpotSub`를 통해서만 하도록 고정하고 있다.

이 계획은 그 모델을 다음처럼 확장한다.

- `SpotNode`는 계속 wiring owner다.
- 동시에 `SpotNode`는 "위치 투명 topic pub/sub"를 직접 사용하는
  상위 facade로도 사용할 수 있다.
- 기존 `SpotPub` / `SpotSub` API는 유지한다.
- 새 `SpotNode` direct API는 내부적으로 node가 보유한 기본
  `SpotPub` / `SpotSub` facade를 사용한다.

즉 최종 사용자 모델은 아래 두 가지를 모두 허용한다.

1. 명시적 handle 모델
   - `zlink_spot_pub_new(node)`
   - `zlink_spot_sub_new(node)`
2. direct node 모델
   - `zlink_spot_node_publish*()`
   - `zlink_spot_node_recv()`
   - `zlink_spot_node_subscribe*()`

이 확장은 "SpotNode를 topology subject나 raw socket owner로 승격"하는 것이
아니다. direct API가 node 안의 기본 `pub/sub` facade를 감싼다는 점이 핵심이다.

## 2. 핵심 판단

### 2.1 SpotNode의 새 역할

`SpotNode`의 역할을 아래처럼 재정의한다.

- 기본 역할:
  - bind/connect/discovery/register/TLS owner
  - local facade와 remote mesh를 잇는 routing/bridge owner
- 추가 역할:
  - 기본 내장 `SpotPub` / `SpotSub`를 보유하는 direct-use facade owner

중요한 제한:

- `SpotNode` 자체는 topology subject가 아니다.
- `SpotNode` 자체는 public monitor target이 아니다.
- `SpotNode` 자체를 poller subject로 직접 취급하지 않는다.
- topology, monitor, peer 통계는 내장 `pub/sub` subject 기준으로 유지한다.

즉 direct node API는 "node 자체를 새 socket/service kind로 만드는 것"이 아니라,
"node가 기본 `pub/sub`를 대신 소유하고 노출하는 것"이다.

### 2.2 direct node 사용 모델

direct node API를 호출하면 `SpotNode`는 lazy-init 방식으로 기본 facade를 만든다.

- 첫 publish 계열 호출 시 기본 `SpotPub` 생성
- 첫 recv/subscribe/handler 계열 호출 시 기본 `SpotSub` 생성

이 lazy-init은 기존 `SpotPub` / `SpotSub` 생성과 같은 ready barrier를 따른다.

- 첫 direct publish 계열 호출은 기본 embedded `SpotPub` 생성 비용을 포함할 수 있다.
- 첫 direct subscribe/recv/handler 계열 호출은 기본 embedded `SpotSub` 생성 비용을
  포함할 수 있다.
- one-time activation wait는 proxy 재작성 스펙의 bounded wait 계약을 그대로 따른다.
- publish 경로의 lazy-init은 thread-safe하게 보호한다.
- 즉 여러 스레드가 동시에 첫 `zlink_spot_node_publish*()`를 호출해도
  embedded `SpotPub` 생성은 한 번만 수행되어야 한다.

즉 direct API의 첫 호출은 setup 성격의 one-time activation 비용을 가질 수 있다.

첫 publish/recv 지연을 setup 단계에서 제거하고 싶은 사용자를 위해 아래 getter를 둔다.

```c
void *zlink_spot_node_default_pub(void *node);
void *zlink_spot_node_default_sub(void *node);
```

이 getter는 기본 embedded facade를 명시적으로 생성/획득하는 advanced API다.

- 반환 대상은 node-owned 기본 embedded handle이다.
- 호출자가 destroy 하지 않는다.
- setup 단계에서 미리 호출해 first-send / first-recv activation 비용을 앞당길 수 있다.
- 호출자가 `zlink_spot_pub_destroy()` / `zlink_spot_sub_destroy()`를 이 handle에 직접
  적용하면 안 된다.
- 구현은 이 오용을 방어해야 하며, node-owned embedded handle에 대한 직접 destroy는
  `EINVAL`로 실패하고 실제 해제를 수행하지 않는다.

기본 facade는 node 수명에 종속된다.

- 사용자가 별도 destroy 하지 않는다.
- `zlink_spot_node_destroy()`가 내부 기본 facade도 함께 정리한다.

별도 `zlink_spot_pub_new()` / `zlink_spot_sub_new()`로 만든 child handle은
기존처럼 개별 destroy가 필요하다.

### 2.3 direct node와 child handle의 공존

한 node 아래에서 아래 사용 패턴을 모두 허용한다.

- node direct publish + child `SpotSub`
- child `SpotPub` + node direct recv
- node direct pub/sub + child pub/sub 혼합

즉 direct node facade는 "exclusive mode"가 아니라 node가 제공하는
기본 handle 세트다.

추가로 아래 동작을 정상 계약으로 고정한다.

- 같은 node의 direct publish는 같은 node의 child `SpotSub`로 fanout 될 수 있다.
- 같은 node의 child `SpotPub` publish는 node direct recv로 수신될 수 있다.
- node direct pub/sub와 child pub/sub는 서로 같은 topic 공간을 공유한다.
- local fanout / remote mesh / discovery 동작은 direct handle이냐 child handle이냐에
  따라 달라지지 않는다.

즉 direct node facade와 child handle은 서로 "격리된 두 모드"가 아니라,
같은 `SpotNode` bridge에 붙은 subject들의 한 조합이다.

### 2.4 왜 상호 배타로 두지 않는가

이번 확장에서는 아래와 같은 상호 배타 제약을 두지 않는다.

- direct API를 한 번 쓴 node에서는 `zlink_spot_pub_new()` / `zlink_spot_sub_new()` 금지
- child handle이 하나라도 있으면 direct API 금지

이유는 다음과 같다.

- 구현상 추가 상태 머신이 생기면 `EFSM` 경계가 불필요하게 복잡해진다.
- direct node는 결국 node-owned default `SpotPub/Sub`일 뿐이므로,
  child handle과 같은 계층에서 공존시키는 편이 더 자연스럽다.
- 사용자는 간단한 direct 모델로 시작한 뒤 필요할 때 child handle만 추가로
  분리할 수 있어야 한다.
- 성능상으로도 "기본 embedded pub/sub 한 쌍"이 더 생기는 정도이며,
  기존 multi-handle 모델과 충돌하지 않는다.

즉 direct node API는 편의 facade를 추가하는 것이지, 기존 `SpotPub/Sub`
모델을 대체하거나 봉쇄하는 모드가 아니다.

## 3. 공개 API 추가 계약

### 3.1 direct publish / subscribe / recv API

다음 public API를 추가한다.

```c
int zlink_spot_node_publish(void *node,
                            const char *topic_id,
                            zlink_msg_t *parts,
                            size_t part_count,
                            int flags);

int zlink_spot_node_publish_bytes(void *node,
                                  const char *topic_id,
                                  const void *data,
                                  size_t size,
                                  int flags);

int zlink_spot_node_subscribe(void *node, const char *topic_id);
int zlink_spot_node_subscribe_pattern(void *node, const char *pattern);
int zlink_spot_node_unsubscribe_filter(void *node,
                                       const char *topic_id_or_pattern);

int zlink_spot_node_set_handler(void *node,
                                zlink_spot_sub_handler_fn handler,
                                void *userdata);

int zlink_spot_node_recv(void *node,
                         zlink_msg_t **parts,
                         size_t *part_count,
                         int flags,
                         char *topic_id_out,
                         size_t *topic_id_len);
```

이 함수들의 의미는 각각 기본 내장 `SpotPub` / `SpotSub`에 대한 아래 위임으로
고정한다.

- `zlink_spot_node_publish*` -> 기본 `SpotPub`
- `zlink_spot_node_subscribe*` / `unsubscribe_filter` -> 기본 `SpotSub`
- `zlink_spot_node_set_handler` / `recv` -> 기본 `SpotSub`

threading 계약도 같은 의미를 따른다.

- `zlink_spot_node_publish*`는 thread-safe
- `zlink_spot_node_recv`, `set_handler`, `subscribe`, `unsubscribe`는
  thread-safe가 아니며 외부 직렬화가 필요
- handler 활성 중 `zlink_spot_node_recv()`는 `EBUSY`

destroy 규칙도 기존 `SpotPub/Sub` 계약을 그대로 따른다.

- `zlink_spot_node_destroy()`와 direct publish/recv/subscribe/handler 호출을
  동시에 수행하면 안 된다.
- 호출자는 `zlink_spot_node_destroy()` 전에 direct API 사용을 모두 중단해야 한다.
- direct API는 node-owned embedded facade를 통해 기존 `SpotPub/Sub`와 같은
  lifetime/threading 계약을 따른다.

### 3.2 node-level pub/sub option API

다음 public API를 추가한다.

```c
int zlink_spot_node_set_pub_option(void *node,
                                   int option,
                                   const void *optval,
                                   size_t optvallen);

int zlink_spot_node_set_sub_option(void *node,
                                   int option,
                                   const void *optval,
                                   size_t optvallen);
```

이 API의 계약은 다음으로 고정한다.

- `set_pub_option`
  - 기본 내장 `SpotPub`가 이미 생성되어 있으면 즉시 적용
  - 아직 생성되지 않았으면 node의 pub default config에 저장
  - 이후 `zlink_spot_pub_new(node)`로 생성되는 child `SpotPub`의 기본값에도 적용
- `set_sub_option`
  - 기본 내장 `SpotSub`가 이미 생성되어 있으면 즉시 적용
  - 아직 생성되지 않았으면 node의 sub default config에 저장
  - 이후 `zlink_spot_sub_new(node)`로 생성되는 child `SpotSub`의 기본값에도 적용

즉 node-level option은 "default template + 기본 embedded facade" 의미다.

중요:

- 이미 생성된 child `SpotPub/Sub`에는 소급 적용하지 않는다.
- 즉 node-level option은 "future child default"와 "현재 embedded default handle"만
  바꾼다.
- 이미 만들어진 child handle을 바꾸려면 기존
  `zlink_spot_pub_set_option()` / `zlink_spot_sub_set_option()`를 사용해야 한다.
- `zlink_spot_node_set_pub_option()`과 `zlink_spot_node_publish*()`를 동시에 호출하면
  안 된다.
- 이 제약은 기존 `SpotPub`의 `set_option()` / `publish()` 관계와 동일하다.
- `zlink_spot_node_set_sub_option()`도 embedded `SpotSub`의 일반 조작과 동시에
  호출하지 않는 것을 전제로 한다.

#### 3.2.1 옵션 리스트 정리

node-level option은 새 enum을 만들지 않고, 기존 `SpotPub` / `SpotSub` option enum을
그대로 재사용한다.

| 대상 | 설정 API | 사용 enum | 적용 대상 |
|------|----------|-----------|-----------|
| `SpotNode` publish default | `zlink_spot_node_set_pub_option()` | `ZLINK_SPOT_PUB_OPT_*` | 현재 embedded default `SpotPub` + 이후 생성될 child `SpotPub`의 초기 기본값 |
| `SpotNode` subscribe default | `zlink_spot_node_set_sub_option()` | `ZLINK_SPOT_SUB_OPT_*` | 현재 embedded default `SpotSub` + 이후 생성될 child `SpotSub`의 초기 기본값 |
| child `SpotPub` override | `zlink_spot_pub_set_option()` | `ZLINK_SPOT_PUB_OPT_*` | 해당 child `SpotPub` 인스턴스 하나 |
| child `SpotSub` override | `zlink_spot_sub_set_option()` | `ZLINK_SPOT_SUB_OPT_*` | 해당 child `SpotSub` 인스턴스 하나 |

지원 옵션 리스트는 아래로 고정한다.

| enum | 의미 | `SpotNode set_pub_option` | `SpotPub set_option` |
|------|------|---------------------------|----------------------|
| `ZLINK_SPOT_PUB_OPT_SNDHWM` | publish send HWM | 지원 | 지원 |
| `ZLINK_SPOT_PUB_OPT_SNDTIMEO` | publish send timeout | 지원 | 지원 |
| `ZLINK_SPOT_PUB_OPT_LINGER` | linger | 지원 | 지원 |
| `ZLINK_SPOT_PUB_OPT_NODROP` | publish no-drop / strict backpressure | 지원 | 지원 |
| `ZLINK_SPOT_PUB_OPT_SNDBUF` | kernel send buffer | 지원 | 지원 |
| `ZLINK_SPOT_PUB_OPT_RCVBUF` | kernel recv buffer | 지원 | 지원 |
| `ZLINK_SPOT_PUB_OPT_MODE` | legacy async publish mode | `ENOTSUP` | `ENOTSUP` |
| `ZLINK_SPOT_PUB_OPT_QUEUE_HWM` | legacy async queue hwm | `ENOTSUP` | `ENOTSUP` |
| `ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY` | legacy async queue full policy | `ENOTSUP` | `ENOTSUP` |

| enum | 의미 | `SpotNode set_sub_option` | `SpotSub set_option` |
|------|------|---------------------------|----------------------|
| `ZLINK_SPOT_SUB_OPT_RCVHWM` | subscribe receive HWM | 지원 | 지원 |
| `ZLINK_SPOT_SUB_OPT_RCVTIMEO` | subscribe receive timeout | 지원 | 지원 |
| `ZLINK_SPOT_SUB_OPT_LINGER` | linger | 지원 | 지원 |
| `ZLINK_SPOT_SUB_OPT_QUEUE_NODROP` | legacy queue no-drop | `ENOTSUP` | `ENOTSUP` |
| `ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY` | legacy queue full policy | `ENOTSUP` | `ENOTSUP` |
| `ZLINK_SPOT_SUB_OPT_SNDBUF` | kernel send buffer | 지원 | 지원 |
| `ZLINK_SPOT_SUB_OPT_RCVBUF` | kernel recv buffer | 지원 | 지원 |

### 3.3 옵션 precedence

우선순위는 아래로 고정한다.

1. child handle에 대한 `zlink_spot_pub_set_option()` /
   `zlink_spot_sub_set_option()`의 per-handle 설정
2. node-level default (`zlink_spot_node_set_pub_option` /
   `zlink_spot_node_set_sub_option`)
3. 구현 기본값

즉 node-level option은 child handle의 초기 기본값을 정할 뿐,
이미 per-handle override가 적용된 child를 다시 덮어쓰는 강제 정책이 아니다.

### 3.4 advanced interop는 getter 두 개로 제한

direct node 모델은 data-plane 편의 API를 제공하지만, introspection/monitor/poller/RID
surface까지 node wrapper로 다시 복제하지는 않는다.

advanced interop는 아래 getter 두 개로 제한한다.

```c
void *zlink_spot_node_default_pub(void *node);
void *zlink_spot_node_default_sub(void *node);
```

이 getter로 얻은 embedded default handle에 대해 기존 API를 그대로 사용한다.

- peer 통계: `zlink_spot_pub_peers()` / `zlink_spot_sub_peers()`
- monitor: `zlink_spot_pub_monitor_open()` / `zlink_spot_sub_monitor_open()`
- poller: `zlink_poller_add_spot_pub()` / `zlink_poller_add_spot_sub()`
- routing id: `zlink_spot_pub_*routing_id()` / `zlink_spot_sub_*routing_id()`

즉 1차 direct facade 확장은 "송수신 편의와 node-level default option"에 집중하고,
기존 고급 surface는 embedded default handle getter를 통해 재사용한다.

## 4. 내부 구현 계약

### 4.1 spot_node_t에 추가할 상태

`spot_node_t`에 아래 내부 상태를 추가한다.

- 기본 embedded `spot_pub_t *` (`_default_pub`)
- 기본 embedded `spot_sub_t *` (`_default_sub`)
- node-level pub option defaults
- node-level sub option defaults

기본 embedded facade는 `spot_node_t`가 소유하고 `destroy()`에서 해제한다.

추가 규칙:

- `_default_pub` / `_default_sub`는 lazy-init 후 재사용되는 stable pointer다.
- 같은 node에서 `zlink_spot_node_default_pub()`를 여러 번 호출하면 같은 handle을
  반환해야 한다.
- `_default_pub` / `_default_sub`도 기존 `_pubs` / `_subs` 집합에 포함시켜
  fault propagation, topology summary, destroy 순서를 기존 handle 경로와 통일한다.
- `remove_spot_pub()` / `remove_spot_sub()`는 제거 대상이 embedded default handle이면
  `_default_pub` / `_default_sub` 포인터도 함께 `NULL`로 정리해야 한다.

### 4.1.1 내부 helper 고정

구현자는 아래 helper를 추가하고, direct API는 반드시 이를 통해 embedded facade를
획득한다.

```c++
spot_pub_t *spot_node_t::ensure_default_pub ();
spot_sub_t *spot_node_t::ensure_default_sub ();
```

계약:

- `ensure_default_pub()`는 thread-safe해야 한다.
- `ensure_default_pub()`는 double-check + node lock 또는 동등한 once 패턴으로
  embedded `SpotPub`를 정확히 한 번만 만든다.
- `ensure_default_sub()`는 direct sub API의 기존 non-thread-safe 계약을 따른다.
- helper 내부에서 실제 facade 생성은 기존 `create_spot_pub()` /
  `create_spot_sub()` 경로를 재사용한다.
- helper는 생성 후 node default option을 다시 적용하지 않고,
  생성 시점의 node default snapshot을 child와 같은 방식으로 복사한다.

### 4.2 child handle 생성 규칙

`create_spot_pub()` / `create_spot_sub()`는 node default option을 새 child handle에
적용한 뒤 반환해야 한다.

즉 node-level option API는 direct embedded handle뿐 아니라 future child handle
생성 기본값도 제어해야 한다.

반대로 node-level option이 internal data-plane socket이나 mesh socket까지
직접 바꾸는 계약은 두지 않는다.

이 문서에서 node pub/sub option이 제어하는 대상은 오직 facade service option이다.

기본 embedded facade와 child handle 사이에 별도 격리 라우팅은 두지 않는다.

- 기본 embedded `SpotPub`도 일반 child `SpotPub`와 같은 local ingress 경로를 사용한다.
- 기본 embedded `SpotSub`도 일반 child `SpotSub`와 같은 local fanout 경로를 사용한다.
- 따라서 direct node와 child handle 사이의 local 메시지 교환은 별도 예외가 아니라
  기존 SPOT fanout 규칙의 일부다.

구현 순서도 아래처럼 고정한다.

1. node default option snapshot 준비
2. facade socket 생성
3. default snapshot 적용
4. node set (`_pubs` / `_subs`) 등록
5. ready barrier wait
6. topology summary 초기 state 반영

즉 ready barrier 전에 `_pubs` / `_subs`에 넣고, barrier 실패 시 즉시 rollback 한다.

### 4.3 direct API와 representative RID

기본 embedded `SpotPub` / `SpotSub`도 각각 대표 RID를 가진다.

- `SpotNode` 자체의 RID 개념은 추가하지 않는다.
- topology / monitor / summary subject는 계속 pub RID, sub RID 기준이다.
- direct node에서 RID가 필요하면 `zlink_spot_node_default_pub()` /
  `zlink_spot_node_default_sub()`로 embedded handle을 얻은 뒤 기존
  `SpotPub/Sub` RID API를 사용한다.

### 4.4 monitor / topology / readiness 문서와의 정합성

기존 문서의 아래 문구는 직접 사용 모델에 맞게 수정해야 한다.

- "SpotNode는 wiring owner only"
- "SpotNode는 poller 대상이 아니다"
- "SpotNode는 public monitor 대상이 아니다"

수정 원칙:

- `SpotNode` 자체는 여전히 poller/monitor subject가 아니다.
- direct node helper는 송수신 convenience만 제공한다.
- poller/monitor 진입은 embedded default handle getter를 통해 기존
  `SpotPub/Sub` surface를 재사용한다.
- topology subject는 계속 embedded `SpotPub/Sub`다.

즉 금지되는 것은 `zlink_spot_node_monitor_open()` 같은 "node 자체 monitor"이며,
허용되는 것은 `zlink_spot_node_default_pub()` /
`zlink_spot_node_default_sub()`로 얻은 embedded subject에 기존 monitor API를
붙이는 방식이다.

### 4.5 topology summary subject 규칙

embedded facade는 topology에서 예외 취급하지 않는다.

- 기본 embedded `SpotPub`가 생성되면 일반 child `SpotPub`와 같은 독립 subject다.
- 기본 embedded `SpotSub`가 생성되면 일반 child `SpotSub`와 같은 독립 subject다.
- child handle도 계속 각각 독립 topology subject다.

즉 한 node 아래에 아래 summary가 함께 존재할 수 있다.

- embedded pub 1개
- embedded sub 1개
- child pub N개
- child sub M개

registry/topology는 이들을 "같은 node의 여러 subject"로 관찰한다.
embedded facade라고 해서 child handle보다 축약되거나 합쳐지지 않는다.

### 4.6 direct API 구현 entrypoint

직접 추가할 C API는 아래 경로로 연결한다.

- public 선언: `core/include/zlink.h`
- C shim: `core/src/api/zlink.cpp`
- 실제 구현: `core/src/services/spot/spot_node.hpp/cpp`

API별 내부 위임은 아래처럼 고정한다.

- `zlink_spot_node_publish*` -> `ensure_default_pub()` -> `spot_pub_t::publish()`
- `zlink_spot_node_subscribe*` / `unsubscribe_filter` -> `ensure_default_sub()` ->
  `spot_sub_t::{subscribe, subscribe_pattern, unsubscribe}()`
- `zlink_spot_node_set_handler` / `recv` -> `ensure_default_sub()` ->
  `spot_sub_t::{set_handler, recv}()`
- `zlink_spot_node_default_pub/sub` -> `ensure_default_pub/sub()`
- `zlink_spot_node_set_pub_option` / `set_sub_option` ->
  node default store 갱신 + embedded default handle 존재 시 즉시 적용

### 4.7 초기 기본값 저장 형식

node-level default option은 "raw optval blob map"이 아니라 명시적 필드 집합으로
보관한다.

예:

- pub defaults:
  - `sndhwm`
  - `sndtimeo`
  - `linger`
  - `nodrop`
  - `sndbuf`
  - `rcvbuf`
- sub defaults:
  - `rcvhwm`
  - `rcvtimeo`
  - `linger`
  - `sndbuf`
  - `rcvbuf`

legacy queue/async 계열은 node default store에도 저장하지 않고 즉시 `ENOTSUP`로
실패한다.

### 4.8 destroy 경로의 구현 규칙

destroy 경로는 기존 `_pubs` / `_subs` 정리 순서와 충돌하지 않게 아래로 고정한다.

1. 외부 direct API 진입 차단
2. `_default_pub` / `_default_sub` 포함 전체 `_pubs` / `_subs` snapshot 확보
3. `_default_pub` / `_default_sub` 포인터를 먼저 `NULL`로 정리
4. 기존 handle destroy loop 재사용
5. data plane / discovery / topology cleanup 진행

즉 embedded default handle을 위한 별도 destroy 분기를 만들지 않고,
기존 handle destroy 메커니즘 안에 포함시켜 정리한다.

## 5. 문서 갱신 방향

아래 문서들을 함께 갱신한다.

- `doc/api/spot.md`
- `doc/api/spot.ko.md`
- `doc/guide/07-3-spot.md`
- `doc/guide/07-3-spot.ko.md`
- `doc/plan/service-monitor-readiness-plan.ko.md`
- `doc/plan/service-option-surface-plan.ko.md`
- `doc/plan/spot-proxy-rewrite-spec.ko.md`

핵심 수정 방향:

- `SpotNode = wiring owner only` 문구를
  `SpotNode = wiring owner + optional direct pub/sub facade owner`로 수정
- topology subject가 node 자체가 아니라 embedded pub/sub임을 명시
- direct node API 예제를 추가
- explicit handle 모델과 direct node 모델 두 가지 예제를 나란히 제공
- first direct API call의 activation 비용과 `default_pub/sub` getter 용도를 명시
- embedded default handle이 기존 `_pubs` / `_subs`와 같은 subject 경로를 쓴다는 점을
  문서와 guide에 명시

## 6. 테스트 계획

### 6.1 기능 테스트

- direct node publish -> direct node recv
- direct node publish -> child `SpotSub`
- child `SpotPub` -> direct node recv
- direct node publish -> 같은 node의 direct recv + child `SpotSub` 동시 fanout
- child `SpotPub` publish -> 같은 node의 direct recv + 다른 child `SpotSub` 동시 fanout
- discovery 기반 remote node direct publish/recv
- 수동 peer mesh 기반 remote node direct publish/recv

### 6.2 옵션 테스트

- `zlink_spot_node_set_pub_option` 후 direct publish path의 option 반영
- `zlink_spot_node_set_sub_option` 후 direct recv path의 option 반영
- node default 설정 후 새 child `SpotPub/Sub` 생성 시 기본값 상속
- child `set_option`이 node default보다 우선
- 이미 생성된 child에는 node-level option이 소급 적용되지 않음
- unsupported option은 기존 `SpotPub/Sub`와 같은 `ENOTSUP`

### 6.3 recv/handler/poller/monitor 테스트

- `zlink_spot_node_set_handler` 활성 중 `zlink_spot_node_recv`는 `EBUSY`
- `zlink_spot_node_default_sub()` + `zlink_poller_add_spot_sub()` readiness 후
  `zlink_spot_node_recv` 성공
- `zlink_spot_node_default_pub()` + `zlink_poller_add_spot_pub()` readiness 후
  `zlink_spot_node_publish*` 성공
- `zlink_spot_node_default_pub/sub()`로 얻은 embedded facade에 monitor를 붙였을 때
  기존 `SpotPub/Sub` subject 이벤트를 수신
- direct API 첫 호출의 one-time activation bounded wait를 별도 테스트

### 6.4 회귀 테스트

- 기존 `SpotPub/Sub` 직접 사용 테스트는 그대로 유지
- 기존 SPOT scenario/perf smoke 통과
- node direct API 추가 후 기존 public API 동작 의미가 변하지 않음

### 6.5 구현 완료 체크리스트

- `SpotNode` direct API만으로 local/remote publish-subscribe 예제가 동작한다.
- `default_pub/sub` getter가 stable pointer를 반환한다.
- embedded default handle 직접 destroy는 `EINVAL`로 방어된다.
- first publish race에서 embedded `SpotPub`가 1개만 생성된다.
- node default option이 future child 생성에만 적용되고 기존 child에는 소급되지 않는다.
- embedded default handle도 topology summary에서 일반 child와 같은 독립 subject로 보인다.

## 7. 비범위

이번 계획에서 하지 않는다.

- `SpotNode` 자체를 새 service kind로 추가
- `zlink_spot_node_monitor_open()` 같은 node 자체 monitor
- `SpotNode` topology subject 추가
- node-level option이 internal mesh/data-plane socket까지 직접 조정하는 계약
- 기존 `SpotPub/Sub` API 제거
- node wrapper 형태의 peer/monitor/poller/RID API 대량 추가

## 8. 최종 사용자 모델

최종적으로 사용자는 아래 둘 중 하나를 선택할 수 있어야 한다.

### 8.1 간단한 경우

```c
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://*:9500");
zlink_spot_node_subscribe(node, "chat");
zlink_spot_node_publish_bytes(node, "chat", data, size, 0);
zlink_spot_node_recv(node, &parts, &part_count, 0, topic, &topic_len);
```

### 8.2 세밀한 경우

```c
void *node = zlink_spot_node_new(ctx);
zlink_spot_node_set_pub_option(node, ZLINK_SPOT_PUB_OPT_SNDHWM, &hwm, sizeof(hwm));
zlink_spot_node_set_sub_option(node, ZLINK_SPOT_SUB_OPT_RCVHWM, &hwm, sizeof(hwm));

void *pub = zlink_spot_pub_new(node);
void *sub = zlink_spot_sub_new(node);
```

즉 `SpotNode`는 더 이상 "반드시 pub/sub를 별도로 꺼내야만 쓸 수 있는 wiring
owner"가 아니라, 직접 사용과 세밀한 분리 사용을 모두 수용하는 상위 facade다.

중요한 점은 이 둘이 상호 배타 모드가 아니라는 것이다.

- 간단한 흐름은 `SpotNode` direct API만 써도 된다.
- 같은 node에서 특정 publisher/subscriber만 분리하고 싶으면 child handle을 추가하면 된다.
- direct node와 child handle은 서로 메시징할 수 있으며, 이것은 위치 투명성 계약에
  어긋나지 않는 정상 동작이다.
- monitor/poller/RID 같은 고급 제어가 필요하면 `default_pub/sub` getter로 embedded
  handle을 얻어 기존 `SpotPub/Sub` API를 그대로 사용한다.

## 9. 구현 메모

구현자는 아래를 먼저 처리하면 된다.

1. `spot_node_t`에 `_default_pub` / `_default_sub`와 node default store 추가
2. `ensure_default_pub()` / `ensure_default_sub()` 구현
3. `zlink.h` / `zlink.cpp`에 direct API와 getter 추가
4. `create_spot_pub()` / `create_spot_sub()`에 node default snapshot 적용
5. direct API 테스트와 기존 SPOT 회귀 테스트 갱신

즉 이 문서 구현의 핵심은 "새로운 data plane을 만드는 것"이 아니라,
"기존 SpotPub/Sub facade를 node-owned default handle로 재사용하는 thin expansion"
이다.
