# SpotNode Direct Facade 확장 계획

## 1. 목적

이 문서는 `SpotNode`를 wiring owner로 유지하면서도,
node-owned default `SpotPub` / `SpotSub` facade를 통해
직접 publish / subscribe / callback 수신을 제공하는 계획을 정리한다.

이 문서의 canonical 전제는
[`direct-callback-recv-rewrite-spec.ko.md`](./direct-callback-recv-rewrite-spec.ko.md)
다.

핵심 목표:

- `SpotNode` direct API를 유지하되 recv API는 callback-only로 정렬한다.
- explicit child handle 모델과 node direct facade 모델을 함께 허용한다.
- `spot_node` 자체를 새 monitor/topology subject로 승격하지 않는다.

## 2. 핵심 판단

### 2.1 `SpotNode`의 역할

`SpotNode`의 역할은 다음으로 고정한다.

- bind/connect/discovery/register/TLS owner
- local facade와 remote mesh를 잇는 bridge owner
- node-owned default `SpotPub` / `SpotSub` facade owner

중요한 제한:

- `SpotNode` 자체는 topology subject가 아니다.
- `SpotNode` 자체는 public monitor target이 아니다.
- direct API는 node 안의 기본 facade를 감싼 thin expansion이다.

### 2.2 두 가지 사용 모델

사용자는 아래 둘 다 쓸 수 있어야 한다.

1. explicit handle 모델
- `zlink_spot_pub_new(node)`
- `zlink_spot_sub_new(node)`

2. direct node 모델
- `zlink_spot_node_publish*()`
- `zlink_spot_node_subscribe*()`
- `zlink_spot_node_set_handler()`

두 모델은 상호 배타가 아니다.

## 3. direct facade 생성 규칙

direct API는 lazy-init 방식으로 node-owned default facade를 만든다.

- 첫 publish 계열 호출 시 기본 `SpotPub` 생성
- 첫 subscribe/handler 계열 호출 시 기본 `SpotSub` 생성

advanced setup API:

```c
void *zlink_spot_node_default_pub(void *node);
void *zlink_spot_node_default_sub(void *node);
```

의미:

- node-owned 기본 embedded handle을 명시적으로 생성/획득한다.
- 호출자가 destroy 하지 않는다.
- first-send / first-callback activation 비용을 setup 단계로 당길 수 있다.

직접 destroy는 허용하지 않는다.

- embedded handle에 대한 `zlink_spot_pub_destroy()` /
  `zlink_spot_sub_destroy()`는 `EINVAL`

## 4. 공개 API 계약

### 4.1 direct publish / subscribe / callback API

public API는 다음으로 정렬한다.

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
                                zlink_spot_sub_handler_fn handler);
```

두지 않는다.

```c
int zlink_spot_node_recv(...);
```

즉 node-owned default sub의 canonical recv path는
`zlink_spot_node_set_handler()`다.

### 4.2 내부 위임

API별 내부 위임은 아래로 고정한다.

- `zlink_spot_node_publish*` -> `ensure_default_pub()` -> `spot_pub_t::publish()`
- `zlink_spot_node_subscribe*` -> `ensure_default_sub()` -> `spot_sub_t::subscribe*()`
- `zlink_spot_node_set_handler` -> `ensure_default_sub()` -> `spot_sub_t::set_handler()`
- `zlink_spot_node_default_pub/sub` -> `ensure_default_pub/sub()`

### 4.3 threading / lifetime

- `zlink_spot_node_publish*`는 thread-safe
- `zlink_spot_node_subscribe*`, `unsubscribe_filter`, `set_handler`는
  thread-safe가 아니며 외부 직렬화가 필요
- `handler == NULL`은 허용하지 않는다
- callback 제거 API는 제공하지 않는다
- `zlink_spot_node_destroy()`와 direct API를 동시에 호출하면 안 된다

## 5. node-level option API

### 5.1 API shape

```c
int zlink_spot_node_set_pub_option(void *node,
                                   zlink_spot_pub_option_t option,
                                   const void *optval,
                                   size_t optvallen);

int zlink_spot_node_set_sub_option(void *node,
                                   zlink_spot_sub_option_t option,
                                   const void *optval,
                                   size_t optvallen);
```

계약:

- embedded default facade가 이미 있으면 즉시 적용
- 아직 없으면 node default store에 저장
- 이후 생성되는 child `SpotPub/Sub`의 기본값에도 적용
- 이미 생성된 child에는 소급 적용하지 않음

### 5.2 저장 대상

node default store는 현재 canonical option surface와 정렬한다.

pub defaults:

- `sndhwm`
- `sndtimeo`
- `linger`
- `nodrop`
- `sndbuf`
- `rcvbuf`

sub defaults:

- `rcvhwm`
- `linger`
- `sndbuf`
- `rcvbuf`

저장하지 않는 항목:

- `ZLINK_SPOT_PUB_OPT_MODE`
- `ZLINK_SPOT_PUB_OPT_QUEUE_HWM`
- `ZLINK_SPOT_PUB_OPT_QUEUE_FULL_POLICY`
- `ZLINK_SPOT_SUB_OPT_RCVTIMEO`
- `ZLINK_SPOT_SUB_OPT_QUEUE_NODROP`
- `ZLINK_SPOT_SUB_OPT_QUEUE_FULL_POLICY`

이 항목들은 현재 canonical spec에서 `ENOTSUP` 또는 삭제 대상이다.

## 6. topology / monitor / peer 관찰 규칙

- embedded default `SpotPub` / `SpotSub`는 일반 child handle과 같은 독립 subject다.
- registry/topology는 이들을 "같은 node의 여러 subject"로 관찰한다.
- monitor와 poller는 embedded facade에 붙는다.
- `SpotNode` 자체 monitor는 추가하지 않는다.

즉 한 node 아래에 다음이 함께 존재할 수 있다.

- embedded pub 1개
- embedded sub 1개
- child pub N개
- child sub M개

## 7. direct node와 child handle의 공존

허용 패턴:

- node direct publish + child `SpotSub`
- child `SpotPub` + node direct callback 수신
- node direct pub/sub + child pub/sub 혼합

추가 계약:

- direct node와 child handle은 같은 topic 공간을 공유한다.
- local fanout / remote mesh / discovery 동작은 handle 종류에 따라 달라지지 않는다.
- `zlink_spot_node_set_handler()`는 node-owned default sub의 구독 집합만 소비한다.
- 각 `zlink_spot_set_handler()`는 해당 facade의 구독 집합만 소비한다.

## 8. 예시

### 8.1 direct node 모델

```c
static void on_topic(const char *topic_id,
                     zlink_msg_t *parts,
                     size_t part_count)
{
    /* consume and close parts */
}

void *node = zlink_spot_node_new(ctx);
zlink_spot_node_bind(node, "tcp://*:9500");
zlink_spot_node_subscribe(node, "chat");
zlink_spot_node_set_handler(node, on_topic);
zlink_spot_node_publish_bytes(node, "chat", data, size, 0);
```

### 8.2 explicit handle 모델과 혼합

```c
void *node = zlink_spot_node_new(ctx);
void *pub = zlink_spot_pub_new(node);
void *sub = zlink_spot_sub_new(node);

zlink_spot_node_set_handler(node, on_default_sub);
zlink_spot_sub_set_handler(sub, on_child_sub);
zlink_spot_pub_publish_bytes(pub, "chat", data, size, 0);
```

## 9. 테스트 계획

기능 테스트:

- direct node publish -> direct node callback
- direct node publish -> child `SpotSub`
- child `SpotPub` -> direct node callback
- direct node publish -> direct node callback + child `SpotSub` 동시 fanout
- discovery 기반 remote node direct publish/callback

옵션 테스트:

- `zlink_spot_node_set_pub_option` 후 direct publish path 반영
- `zlink_spot_node_set_sub_option` 후 direct callback path 반영
- node default 설정 후 새 child `SpotPub/Sub` 생성 시 기본값 상속
- unsupported option은 `ENOTSUP`

회귀 테스트:

- 기존 `SpotPub/Sub` 직접 사용 테스트 유지
- `zlink_spot_node_recv()` 의존 테스트 제거 후 callback 기준으로 갱신
- 기존 SPOT scenario/perf smoke 통과

## 10. 연관 문서

- 메인 스펙:
  [`direct-callback-recv-rewrite-spec.ko.md`](./direct-callback-recv-rewrite-spec.ko.md)
- option surface:
  [`service-option-surface-plan.ko.md`](./service-option-surface-plan.ko.md)
- monitor/readiness:
  [`service-monitor-readiness-plan.ko.md`](./service-monitor-readiness-plan.ko.md)
- proxy 재작성:
  [`spot-proxy-rewrite-spec.ko.md`](./spot-proxy-rewrite-spec.ko.md)

## 11. Definition of Done

- `SpotNode` direct API에서 `recv`가 제거되고 callback-only로 정렬되어 있다.
- node-owned default `SpotPub/Sub` lazy-init이 동작한다.
- embedded default handle 직접 destroy가 `EINVAL`로 방어된다.
- node default option이 future child 생성에만 적용되고 기존 child에는 소급되지 않는다.
- embedded default handle도 topology/monitor 관점에서 일반 child와 같은 독립 subject로 보인다.
