# Thread-Safe Socket / Service 설계 계획

> 상태 메모
> 이 문서는 thread-safe 설계 초안이며, 본문에 남아 있는 `set_handler()`,
> `zlink_monitor_set_handler()`, `zlink_service_monitor_set_handler()` 등
> 생성 후 handler 교체 서술은 현재 canonical public API가 아니다. 최신
> 기준은 `direct-callback-recv-interface-review.ko.md`와
> `core/include/zlink.h`를 따른다.

## 1. 목적

이 문서는 현재 진행 중인
[`direct-callback-recv-interface-review.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-interface-review.ko.md)
기준 인터페이스를 전제로,
raw socket / `gateway` / `spot` 계열을 동일한 수준의 thread mode 개념으로 정렬하는
상세 설계 계획이다.

핵심 목표:

- raw socket과 service facade의 thread-safety 모델을 하나의 public 개념으로 통일한다.
- 생성 시 `non-thread-safe` / `thread-safe` mode를 선택할 수 있게 한다.
- `gateway`, `spot`, `spot_node`, `spot_pub`, `spot_sub`를
  “특별취급된 service”가 아니라 raw socket과 동일 계층의
  `thread mode` 대상 subject로 정리한다.
- callback-only recv 모델과 충돌하지 않는 범위에서 same-handle concurrent `send`
  와 lifecycle API 동기화 범위를 명확히 한다.
- single-thread 전용 사용자는 no-lock fast path를 선택할 수 있게 하고,
  multi-thread 사용자는 안전한 기본 모드를 선택할 수 있게 한다.
- 기존 ABI 호환성은 고려하지 않는다.
  필요한 인터페이스 변경은 기존 생성자/함수 시그니처를 직접 바꾸는 것으로 전제한다.

쉽게 말해 이 문서는 다음 질문에 한 번에 답하려는 계획이다.

- "이 handle을 여러 thread에서 같이 써도 되나?"
- "같은 handle에서 동시에 send해도 되나?"
- "callback이 도는 중에 handler를 바꾸거나 close해도 되나?"
- "spot/gateway/raw socket마다 규칙이 왜 다른가?"

이 문서의 답은 단순하다.

- 생성할 때 `THREAD_SAFE` 또는 `NON_THREAD_SAFE`를 명시한다.
- `THREAD_SAFE`면 same-handle concurrent `send`와 주요 mutation을 라이브러리가 직렬화한다.
- `NON_THREAD_SAFE`면 호출자가 외부에서 직렬화 책임을 진다.
- 단, `close` / `destroy`는 두 mode 모두 가장 엄격하게 다룬다.

## 한눈에 보는 구조

이 문서의 전체 그림은 아래처럼 이해하면 된다.

### 1) 모든 subject는 생성 시 mode를 고른다

```text
create(...)
   |
   +-- THREAD_SAFE
   |      -> 같은 handle을 여러 thread가 함께 써도 됨
   |      -> send / handler 교체 / 주요 mutation을 라이브러리가 직렬화
   |
   +-- NON_THREAD_SAFE
          -> 같은 handle은 한 thread에서만 쓰는 것이 기본
          -> 여러 thread가 쓰려면 앱이 직접 락으로 감싸야 함
```

### 2) 단, close / destroy는 예외적으로 더 보수적이다

```text
다른 API 실행 중
   |
   +-- send 중
   +-- subscribe 중
   +-- callback 실행 중
   +-- option setter 중
        |
        +-- 이때 다른 thread가 close/destroy 호출
               -> 성공하지 않음
               -> EBUSY
```

즉 `THREAD_SAFE`는 "무조건 아무 때나 다 동시에 호출 가능"이 아니라,
"운영 중 API는 직렬화해 주되, 종료 API는 별도로 엄격하게 막는다"에 가깝다.

### 3) `spot_node`와 unified `spot`의 관계는 이렇게 본다

```text
spot_node
   |
   +-- 내부 data-plane thread
   |      +-- ingress SUB
   |      +-- fanout XPUB
   |      +-- mesh PUB / XSUB
   |
   +-- zlink_spot_new()
          |
          +-- 별도 spot_pub 생성
          +-- 별도 spot_sub 생성
          +-- node 내부 proxy endpoint에 attach
```

중요한 점:

- unified `spot`은 `spot_node`의 public handle을 그대로 재사용하는 구조가 아니다.
- child pub/sub를 따로 만들고 node 내부 data-plane에 붙는다.
- 그래서 `spot`의 publish/subscribe 경로와 `spot_node` direct API 경로를 개념적으로 분리해서 볼 수 있다.

## 2. 기준 문서와 우선순위

이 문서의 직접 기준은 다음이다.

- 인터페이스 기준:
  [`direct-callback-recv-interface-review.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-interface-review.ko.md)
- recv/callback 실행 모델 기준:
  [`direct-callback-recv-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-rewrite-spec.ko.md)

우선순위:

1. 현재 public ABI shape 판단은 `direct-callback-recv-interface-review.ko.md`를 기준으로 한다.
2. callback-only recv, handler replace/no-remove, ownership 규칙은
   `direct-callback-recv-rewrite-spec.ko.md`를 따른다.
3. 이 문서는 thread mode 도입 시 필요한 생성자/계약/락 전략을 추가 정의한다.

즉, 새 컨텍스트에서 이 문서만 보더라도 작업할 수 있어야 하지만,
public 함수 이름과 현재 canonical shape는
`direct-callback-recv-interface-review.ko.md` 기준으로 해석한다.

## 용어 정리

이 문서는 동시성 관련 표현을 많이 사용하므로, 먼저 자주 나오는 용어를 고정한다.

### same-handle

같은 public handle 값 하나를 여러 thread가 공유해서 쓰는 경우를 뜻한다.

예:

- 같은 `gateway` handle로 thread A와 thread B가 동시에 `zlink_gateway_send()` 호출
- 같은 `spot` handle로 callback thread와 worker thread가 동시에 `zlink_spot_publish()` 호출

### callback thread

해당 recv callback이 실제로 실행되는 thread를 뜻한다.
대개 해당 subject의 I/O dispatch를 수행하는 thread다.

### worker thread

callback thread가 아닌, 사용자가 따로 만든 일반 작업 thread를 뜻한다.
문서에서 "다른 thread"라고 하면 특별한 언급이 없는 한 callback thread 외부 thread를 뜻한다.

### in-flight API / in-flight operation

어떤 public API 호출이 "이미 시작됐지만 아직 return하지 않은 상태"를 뜻한다.

예:

- `send()` 함수에 진입했지만 아직 return 전
- `subscribe()`가 내부 socket option 적용 중이라 아직 return 전
- recv callback이 실행 중이라 아직 callback return 전

이 문서에서 `in-flight`는 "이미 실행이 시작된 상태"를 뜻한다.
큐에 들어가서 나중에 처리될 예정이라는 뜻으로 쓰지 않는다.

### dispatch

수신된 메시지 또는 이벤트를 callback으로 넘겨 실행하는 전체 과정을 뜻한다.
"다음 dispatch 진입 시점"은 다음 메시지/event에 대해 callback 호출 경로에 들어가는 순간을 뜻한다.

### match

주로 subscribe/unsubscribe 설명에서 쓰는 용어다.
들어온 메시지가 현재 subscription 집합에 매칭되는지 판단하는 단계를 뜻한다.

### visibility

어떤 변경이 언제부터 관찰되는지를 뜻한다.

예:

- `set_handler()` 후 "다음 dispatch 진입 시점부터 visible"
- `unsubscribe()` 후 "다음 match 진입 시점부터 visible"

### cross-thread close/destroy

현재 callback을 실행 중인 thread가 아닌 다른 thread에서
같은 handle의 `close()` / `destroy()` / monitor close를 호출하는 경우를 뜻한다.

### self-close / self-destroy

callback 안에서 자기 자신이 소유한 handle 종료를 요청하는 경우를 뜻한다.

예:

- raw socket callback 안에서 `zlink_close(socket_handle)`
- service callback 안에서 `zlink_spot_destroy(&spot_handle_slot)`

### child handle

다른 subject에 attach되어 열리는 subordinate handle을 뜻한다.

예:

- socket monitor
- service monitor
- `spot_node`에 attach된 `spot_pub`, `spot_sub`, unified `spot`

### direct API / direct path

어떤 parent subject 자체의 public API를 직접 호출하는 경로를 뜻한다.

예:

- `zlink_spot_node_publish()`, `zlink_spot_node_subscribe()`
- `zlink_gateway_send()`

문서에서 "node direct API"라고 하면 `zlink_spot_node_*` 계열을 뜻한다.

### fail-fast

잘못된 사용을 조용한 data corruption이나 UB로 넘기지 않고,
가능한 빨리 `EINVAL`, `EBUSY`, assert/log 같은 형태로 드러내는 정책을 뜻한다.

## 3. 배경과 문제 정의

지금 public surface를 보면 subject마다 thread-safety 성격이 조금씩 다르다.
그래서 사용자는 "이건 여러 thread에서 써도 되나?"를 API마다 따로 기억해야 한다.

현재 상태를 짧게 정리하면 다음과 같다.

- raw socket은 public 계약상 완전 thread-safe subject가 아니다.
- `gateway`는 내부적으로 `_use_lock` 기반 직렬화를 이미 가지고 있다.
- `spot_pub`는 comment와 구현 모두 thread-safe subject에 가깝다.
- `spot_sub`, unified `spot`, `spot_node`는 API별로 thread-safe 범위가 섞여 있다.
- recv가 callback-only가 되면서, 과거의 큰 recv-side queue/fq 경쟁보다
  same-handle `send`, handler 교체, subscribe/unsubscribe, destroy race가
  더 중요한 동시성 문제가 됐다.

이 상태가 불편한 이유는 다음과 같다.

- public 사용자 입장에서 “무엇이 thread-safe인가”를 API family마다 따로 외워야 한다.
- `gateway`와 `spot_pub`만 특별취급되면 ABI가 일관되지 않다.
- single-thread 앱은 굳이 lock 비용을 부담하고,
  multi-thread 앱은 subject마다 다른 안전 규칙을 알아야 한다.

그래서 이 문서는 public 규칙을 이렇게 단순화하려고 한다.

- handle을 만들 때 mode를 고른다.
- 그 mode가 그 handle의 thread-safety 계약이 된다.
- raw socket, gateway, spot 계열 모두 같은 틀로 설명한다.

## 4. 핵심 결정

### 4.1 thread mode를 생성 시 고정한다

모든 주요 public subject는 생성 시 thread mode를 고정한다.

- raw socket
- `discovery`
- `gateway`
- `spot_node`
- unified `spot`
- standalone `spot_pub`
- standalone `spot_sub`
- monitor handle도 같은 thread mode 개념으로 정렬한다.

런타임에 mode를 바꾸는 API는 두지 않는다.

이유:

- lock on/off 분기와 lifetime 계약이 생성 이후 바뀌면 구현 복잡도가 급증한다.
- handle identity와 마찬가지로 concurrency identity도 생성 시 고정하는 편이 자연스럽다.
- debug assertion, test matrix, 문서화가 쉬워진다.

### 4.2 mode는 항상 명시적으로 받는다

생성자는 모두 `zlink_thread_mode_t`를 명시적으로 받는다.

- implicit default mode를 두지 않는다.
- 호출자가 `THREAD_SAFE` / `NON_THREAD_SAFE`를 명확히 선언해야 한다.
- 문서와 테스트도 explicit mode 기준으로만 정리한다.

### 4.3 non-thread-safe mode는 no-lock contract다

`NON_THREAD_SAFE` mode에서는 라이브러리가 내부 직렬화를 하지 않는다.

- same-handle concurrent `send`는 허용되지 않으며,
  debug에서는 assert/log, release에서는 가능한 범위에서 `EINVAL` 또는 `EBUSY`로 실패시킨다.
- callback thread와 다른 thread에서 같은 handle의 lifecycle API를 동시에 호출하면 안 된다.
- 라이브러리는 fast path를 위해 per-handle mutex를 생략할 수 있다.

### 4.4 thread-safe mode는 per-handle serialization을 기본으로 한다

`THREAD_SAFE` mode는 다음을 기본 보장으로 둔다.

- same-handle concurrent `send` 허용
- `set_handler()`와 dispatch 간 최소한의 safe replace 보장
- option setter / subscribe 같은 state mutation의 직렬화
- destroy/close는 더 강한 drain/reference-count 정책이 필요하므로 별도 규칙을 둔다

이 문서의 목표는 public 계약을 바로 이 형태로 바꾸는 것이다.
단계적 도입이나 compatibility wrapper는 전제하지 않는다.

쉽게 말해:

- `THREAD_SAFE` = "같은 handle을 여러 thread에서 써도 라이브러리가 정리해 준다."
- `NON_THREAD_SAFE` = "같은 handle은 한 thread에서만 쓰거나, 앱이 직접 락을 잡아라."
- 두 mode 모두 `close` / `destroy`는 예외적으로 더 보수적이다.

대표 시나리오로 보면 더 단순하다.

| 시나리오 | `THREAD_SAFE` | `NON_THREAD_SAFE` |
|---|---|---|
| A: worker 두 개가 같은 handle로 send | 허용 | 외부 락 없으면 misuse |
| B: callback 중 다른 thread가 handler 교체 | 허용, 다음 dispatch 반영 | 금지 |
| C: callback 중 다른 thread가 close/destroy | `EBUSY` | caller 책임, 가능하면 fail-fast |

## 5. 새 public enum 제안

```c
typedef enum zlink_thread_mode_t
{
  ZLINK_THREAD_MODE_THREAD_SAFE = 0,
  ZLINK_THREAD_MODE_NON_THREAD_SAFE = 1
} zlink_thread_mode_t;
```

원칙:

- 닫힌 값 집합이므로 enum으로 공개한다.
- 기본값은 `THREAD_SAFE = 0`으로 둔다.
  구조체 zero-init이나 FFI/binding enum 기본값이 유효 상태로 남기 쉽다.
  이것은 enum 값 배치 원칙일 뿐, 생성 API에서 implicit default mode를 둔다는 뜻은 아니다.
- `AUTO`, `EXTERNAL_SERIALIZED` 같은 추가 mode는 현재 범위에 넣지 않는다.

## 6. 공개 생성 API 재설계

### 6.1 API shape 원칙

이 절은 "최종 public 함수 모양을 어떻게 맞출 것인가"를 설명한다.

생성자 이름은 가능하면 그대로 유지하고, 기존 생성자 시그니처에
`zlink_thread_mode_t`를 직접 추가한다.
단, `NULL` 허용으로 자연스럽게 통합되는 경우는 두 생성자를 하나로 병합한다.

이유:

- 호환성 alias/wrapper 없이 곧바로 최종 shape로 정리할 수 있다.
- 문서와 코드가 임시 이행 상태 없이 바로 일치한다.
- 바인딩과 테스트도 최종 생성자만 따르면 된다.

즉 방향은 "새 이름을 잔뜩 만드는 것"이 아니라
"기존 생성자에 mode 인자를 추가해서 최종 형태로 바로 정리하는 것"이다.

### 6.2 raw socket

현재:

```c
void *zlink_socket (void *ctx,
                    zlink_socket_type_t type,
                    const zlink_socket_handler_t *handler);
```

변경안:

```c
void *zlink_socket (void *ctx,
                    zlink_socket_type_t type,
                    const zlink_socket_handler_t *handler,
                    zlink_thread_mode_t thread_mode);
```

비고:

- `zlink_socket_handler_t`는 `kind` + fn union 구조의 descriptor다.
  `ZLINK_SOCKET_HANDLER_MSG` / `ZLINK_SOCKET_HANDLER_SPOT` / `ZLINK_SOCKET_HANDLER_XPUB` 중 하나를
  socket type family에 맞게 선택한다.
- recv-capable 타입은 family에 맞는 non-`NULL` descriptor가 필수이고,
  send-only `PUB`만 `handler == NULL` 경로를 허용한다.
- `zlink_socket_set_handler(void *s, const zlink_socket_handler_t *handler)` 로
  callback 교체를 수행한다. (`zlink_socket_set_msg_handler()` 대체)

읽는 포인트는 단순하다.

- raw socket도 이제 다른 subject와 똑같이 생성 시 mode를 고른다.
- recv-capable socket은 처음부터 handler가 있어야 한다.
- handler 교체는 생성 후 `set_handler()`로 한다.

### 6.3 discovery

`discovery`도 내부적으로 mutable topology cache와 observer list를 가지므로
동일한 mode 대상에 넣는 편이 일관적이다.

즉 `discovery`도 "그냥 보조 객체"로 두지 않고,
thread mode를 가지는 독립 subject로 본다.

현재:

```c
void *zlink_discovery_new (void *ctx,
                           zlink_service_type_t service_type);
```

변경안:

```c
void *zlink_discovery_new (void *ctx,
                           zlink_service_type_t service_type,
                           zlink_thread_mode_t thread_mode);
```

비고:

- `discovery`에서 `THREAD_SAFE`의 의미는 same-handle concurrent `send`보다는
  topology cache update, observer attach/detach, service query 같은 관리 API를
  데이터 race 없이 함께 호출할 수 있다는 뜻에 가깝다.
- 즉 `discovery`는 다른 subject와 같은 enum을 쓰지만,
  실제 보호 대상은 topology/observer/query 경로다.

### 6.4 gateway

현재:

```c
void *zlink_gateway_new (void *ctx,
                         const char *service_name,
                         const char *routing_id,
                         zlink_socket_msg_handler_fn handler);
```

변경안:

```c
void *zlink_gateway_new (void *ctx,
                         const char *service_name,
                         const char *routing_id,
                         zlink_socket_msg_handler_fn handler,
                         zlink_thread_mode_t thread_mode);
```

비고:

- canonical interface review 기준으로 gateway는 `zlink_socket_msg_handler_fn`을 사용한다.
  `zlink_gateway_handler_fn` typedef는 canonical에서 삭제됐다.
- `handler`는 필수 non-`NULL`이다. `NULL`로 생성하면 `EINVAL` / `NULL`을 반환한다.
  `gateway`는 항상 recv-capable이므로 handler 없이 생성할 수 없다.
- 현재 `gateway_t`는 내부 `_use_lock`를 이미 가지고 있으므로
  `thread_mode`는 `_use_lock` 초기값으로 직접 연결할 수 있다.
- 즉 `THREAD_SAFE`면 `_use_lock = true`,
  `NON_THREAD_SAFE`면 `_use_lock = false`가 기본 구현 경로다.
- `gateway`와 `discovery` 간 mode 호환 규칙은 `zlink_gateway_new()` 시점이 아니라
  `zlink_gateway_attach_discovery()` 시점에 체크한다.
  즉 `discovery`가 `NON_THREAD_SAFE`인 상태에서
  `THREAD_SAFE` `gateway`가 `zlink_gateway_attach_discovery()`를 호출하면 `EINVAL`로 막는다.

`gateway`는 이미 `_use_lock` 기반 경로가 있어서 `thread_mode` 연결이 가장 쉬운 대상이다.

### 6.5 spot_node

현재:

```c
void *zlink_spot_node_new (void *ctx,
                           const char *service_name,
                           zlink_spot_handler_fn handler);
```

변경안:

```c
void *zlink_spot_node_new (void *ctx,
                           const char *service_name,
                           zlink_spot_handler_fn handler,
                           zlink_thread_mode_t thread_mode);
```

비고:

- `handler`는 필수 non-`NULL`이다. `NULL`로 생성하면 `EINVAL` / `NULL`을 반환한다.

이유:

- `spot_node`는 bind/connect/register/discovery/TLS/subscription set을
  다루므로 사실상 독립 concurrency subject다.
- `gateway`와 레벨을 맞추려면 `spot_node`도 thread mode를 명시적으로 가져야 한다.

즉 `spot_node`는 단순한 container가 아니라,
그 자체로 별도 concurrency 규칙을 가져야 하는 parent subject다.

### 6.6 unified spot

현재:

```c
void *zlink_spot_new (void *spot_node,
                      zlink_spot_handler_fn handler);
```

변경안:

```c
void *zlink_spot_new (void *spot_node,
                      zlink_spot_handler_fn handler,
                      zlink_thread_mode_t thread_mode);
```

비고:

- `handler`는 필수 non-`NULL`이다. `NULL`로 생성하면 `EINVAL` / `NULL`을 반환한다.
- unified `spot`은 parent `spot_node`의 thread mode와 무관하게 독립적으로 thread mode를 선택한다.
  `spot_node`가 `NON_THREAD_SAFE`이어도 `spot`을 `THREAD_SAFE`로 생성할 수 있다.
- 이 관계는 아래처럼 이해하면 된다.

```text
thread-safe가 아닌 spot_node
        |
        +-- zlink_spot_new(...)
               |
               +-- 새 spot_pub
               +-- 새 spot_sub
               +-- node 내부 data-plane endpoint에 연결
```

구조:

- unified `spot`은 `spot_node`의 public pub/sub handle을 빌려 쓰는 얇은 alias가 아니다.
- 생성 시 별도 child facade를 만들고,
  이 child facade들이 node 내부 proxy/data-plane에 붙는 구조다.
- 그래서 data path 관점에서는 parent와 child가 같은 public socket handle을 공유하지 않는다.

동기화 경계:

- unified `spot`의 publish/subscribe/handler 경로 동기화는
  `spot` 자체의 동기화 책임 범위에서 처리한다.
- 반대로 `spot_node`의 thread mode는 `zlink_spot_node_*` direct API 계약에 적용된다.
- `spot_node`의 `NON_THREAD_SAFE`는
  `zlink_spot_node_*` direct API 호출에 대한 외부 직렬화 책임을 뜻한다.
  node 내부 data-plane thread나 child facade와의 내부 coordination lock까지
  제거한다는 뜻은 아니다.

기타:

- unified `spot`은 생성 시 역할을 선택하지 않는다.
  항상 pub/sub를 함께 가진 facade이므로 `roles` 인자를 받지 않는다.

실제로 사용자가 느끼는 모습은 다음과 같다.

- `spot_node`는 "노드 전체를 관리하는 parent"
- unified `spot`은 "그 parent 위에 붙는 독립 pub/sub facade"
- 둘은 완전히 무관한 객체는 아니지만,
  same-handle thread-safety를 판단할 때는 다른 subject로 보는 편이 더 정확하다

### 6.7 standalone SpotPub / SpotSub

현재:

```c
void *zlink_spot_pub_new (void *node);
void *zlink_spot_sub_new (void *node);
```

변경안:

```c
void *zlink_spot_pub_new (void *node,
                          zlink_thread_mode_t thread_mode);

void *zlink_spot_sub_new (void *node,
                          zlink_thread_mode_t thread_mode);
```

비고:

- `zlink_spot_sub_new()`는 handler를 생성자 인자로 받지 않는다.
  handler는 생성 후 `zlink_spot_sub_set_handler(sub, handler)`로 별도 지정한다.
  (zlink.h 현재 구현 및 spot-node-direct-facade-plan 일치)

추가 규칙:

- parent `spot_node`가 `NON_THREAD_SAFE`인데 standalone `spot_pub` / `spot_sub`를
  `THREAD_SAFE`로 만드는 것은 `EINVAL`로 막는다.
- parent가 `THREAD_SAFE`일 때 child가 `NON_THREAD_SAFE`인 것은 허용 가능하다.

### 6.8 monitor handle

`monitor`도 독립 handle로 열리고 callback/set_handler/close 경쟁을 가지므로
같은 `thread_mode` surface에 포함한다.

즉 monitor도 "부가 기능" 정도로 보지 않고,
handler 교체와 close 경쟁이 있는 별도 child handle로 취급한다.

현재:

```c
void *zlink_socket_monitor_open (void *s,
                                 zlink_socket_monitor_event_mask_t events,
                                 zlink_monitor_handler_fn handler);

void *zlink_discovery_monitor_open (void *discovery,
                                    zlink_discovery_monitor_event_mask_t events,
                                    zlink_service_monitor_handler_fn handler);

void *zlink_gateway_monitor_open (void *gateway,
                                  zlink_gateway_monitor_event_mask_t events,
                                  zlink_service_monitor_handler_fn handler);

void *zlink_spot_monitor_open (void *spot,
                               zlink_spot_role_t role,
                               zlink_spot_monitor_event_mask_t events,
                               zlink_service_monitor_handler_fn handler);

void *zlink_spot_sub_monitor_open (void *sub,
                                   zlink_spot_monitor_event_mask_t events,
                                   zlink_service_monitor_handler_fn handler);

void *zlink_spot_pub_monitor_open (void *pub,
                                   zlink_spot_monitor_event_mask_t events,
                                   zlink_service_monitor_handler_fn handler);
```

변경안:

```c
void *zlink_socket_monitor_open (void *s,
                                 zlink_socket_monitor_event_mask_t events,
                                 zlink_monitor_handler_fn handler,
                                 zlink_thread_mode_t thread_mode);

void *zlink_discovery_monitor_open (void *discovery,
                                    zlink_discovery_monitor_event_mask_t events,
                                    zlink_service_monitor_handler_fn handler,
                                    zlink_thread_mode_t thread_mode);

void *zlink_gateway_monitor_open (void *gateway,
                                  zlink_gateway_monitor_event_mask_t events,
                                  zlink_service_monitor_handler_fn handler,
                                  zlink_thread_mode_t thread_mode);

void *zlink_spot_monitor_open (void *spot,
                               zlink_spot_role_t role,
                               zlink_spot_monitor_event_mask_t events,
                               zlink_service_monitor_handler_fn handler,
                               zlink_thread_mode_t thread_mode);

void *zlink_spot_sub_monitor_open (void *sub,
                                   zlink_spot_monitor_event_mask_t events,
                                   zlink_service_monitor_handler_fn handler,
                                   zlink_thread_mode_t thread_mode);

void *zlink_spot_pub_monitor_open (void *pub,
                                   zlink_spot_monitor_event_mask_t events,
                                   zlink_service_monitor_handler_fn handler,
                                   zlink_thread_mode_t thread_mode);
```

추가 규칙:

- `monitor`는 attached child handle로 본다.
- `handler`는 필수 non-`NULL`이다. `NULL`로 open하면 `EINVAL` / `NULL`을 반환한다.
  monitor는 항상 recv-capable이므로 handler 없이 open할 수 없다.
- parent subject가 `NON_THREAD_SAFE`이면 child monitor를 `THREAD_SAFE`로 여는 것은 `EINVAL`로 막는다.
- parent가 `THREAD_SAFE`일 때 child monitor가 `NON_THREAD_SAFE`인 것은 허용할 수 있다.
- `zlink_monitor_set_handler()` / `zlink_service_monitor_set_handler()`의 교체 visibility는
  parent subject의 direct callback 계약과 동일하게 “다음 dispatch 진입 시점” 기준으로 맞춘다.

### 6.9 공개 인터페이스 변경 요약

아래 표는 이 문서 범위에서 실제로 public 시그니처가 어떻게 바뀌는지를
한 번에 보여준다.

읽을 때는 표 전체를 외우기보다 아래 두 점만 보면 된다.

- 거의 모든 생성/open 함수에 `zlink_thread_mode_t`가 추가된다.
- close/destroy 함수 이름은 유지하고, 계약만 더 엄격하게 정리한다.

| 현재 인터페이스 | 변경안 | 비고 |
|---|---|---|
| `zlink_socket(void *ctx, zlink_socket_type_t type, const zlink_socket_handler_t *handler)` | `zlink_socket(void *ctx, zlink_socket_type_t type, const zlink_socket_handler_t *handler, zlink_thread_mode_t thread_mode)` | raw socket 생성자에 `thread_mode` 추가; handler는 descriptor struct |
| `zlink_discovery_new(void *ctx, zlink_service_type_t service_type)` | `zlink_discovery_new(void *ctx, zlink_service_type_t service_type, zlink_thread_mode_t thread_mode)` | discovery도 thread mode 대상 |
| `zlink_gateway_new(void *ctx, const char *service_name, const char *routing_id, zlink_socket_msg_handler_fn handler)` | `zlink_gateway_new(void *ctx, const char *service_name, const char *routing_id, zlink_socket_msg_handler_fn handler, zlink_thread_mode_t thread_mode)` | `zlink_gateway_handler_fn` 삭제; `zlink_socket_msg_handler_fn` 사용; mode 호환 체크는 `attach_discovery` 시점 |
| `zlink_spot_node_new(void *ctx, const char *service_name, zlink_spot_handler_fn handler)` | `zlink_spot_node_new(void *ctx, const char *service_name, zlink_spot_handler_fn handler, zlink_thread_mode_t thread_mode)` | node가 parent mode를 고정 |
| `zlink_spot_new(void *spot_node, zlink_spot_handler_fn handler)` | `zlink_spot_new(void *spot_node, zlink_spot_handler_fn handler, zlink_thread_mode_t thread_mode)` | roles 없음; parent `spot_node`와 독립적으로 mode 선택 |
| `zlink_spot_pub_new(void *node)` | `zlink_spot_pub_new(void *node, zlink_thread_mode_t thread_mode)` | standalone child 생성자 |
| `zlink_spot_sub_new(void *node)` | `zlink_spot_sub_new(void *node, zlink_thread_mode_t thread_mode)` | standalone child 생성자; handler는 생성 후 `set_handler()`로 지정 |
| `zlink_socket_monitor_open(void *s, zlink_socket_monitor_event_mask_t events, zlink_monitor_handler_fn handler)` | `zlink_socket_monitor_open(void *s, zlink_socket_monitor_event_mask_t events, zlink_monitor_handler_fn handler, zlink_thread_mode_t thread_mode)` | monitor handle도 explicit mode |
| `zlink_discovery_monitor_open(void *discovery, zlink_discovery_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler)` | `zlink_discovery_monitor_open(void *discovery, zlink_discovery_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler, zlink_thread_mode_t thread_mode)` | discovery child monitor |
| `zlink_gateway_monitor_open(void *gateway, zlink_gateway_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler)` | `zlink_gateway_monitor_open(void *gateway, zlink_gateway_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler, zlink_thread_mode_t thread_mode)` | gateway child monitor |
| `zlink_spot_monitor_open(void *spot, zlink_spot_role_t role, zlink_spot_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler)` | `zlink_spot_monitor_open(void *spot, zlink_spot_role_t role, zlink_spot_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler, zlink_thread_mode_t thread_mode)` | unified spot monitor |
| `zlink_spot_sub_monitor_open(void *sub, zlink_spot_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler)` | `zlink_spot_sub_monitor_open(void *sub, zlink_spot_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler, zlink_thread_mode_t thread_mode)` | split sub monitor |
| `zlink_spot_pub_monitor_open(void *pub, zlink_spot_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler)` | `zlink_spot_pub_monitor_open(void *pub, zlink_spot_monitor_event_mask_t events, zlink_service_monitor_handler_fn handler, zlink_thread_mode_t thread_mode)` | split pub monitor |
| `zlink_monitor_set_handler(void *monitor_socket, zlink_monitor_handler_fn handler)` | 시그니처 유지 | 교체 visibility만 “다음 dispatch 진입 시점”으로 명시 |
| `zlink_service_monitor_set_handler(void *monitor, zlink_service_monitor_handler_fn handler)` | 시그니처 유지 | 교체 visibility만 “다음 dispatch 진입 시점”으로 명시 |
| `zlink_close(void *s)` | 시그니처 유지 | cross-thread close: `EBUSY`; callback self-close: deferred teardown |
| `zlink_service_monitor_close(void **monitor_p)` | 시그니처 유지 | 동일 정책 적용 |

## 7. 허용 동시 호출 계약

이 절은 "어디까지를 라이브러리가 책임지고 직렬화해 주는가"를 정리한다.

- `THREAD_SAFE`의 기본 의미는 same-handle concurrent operation 허용이다.
- `NON_THREAD_SAFE`의 기본 의미는 same-handle concurrent operation 금지다.
- 단, `close` / `destroy`는 `THREAD_SAFE`여도 아무 때나 허용하지 않는다.

읽을 때는 아래 두 문장만 먼저 기억하면 된다.

- 운영 API(`send`, `set_handler`, subscribe, option setter 등)는 mode에 따라 허용 범위를 나눈다.
- 종료 API(`close`, `destroy`)는 mode와 별개로 가장 보수적으로 다룬다.

### 7.1 thread-safe mode

허용:

- same-handle concurrent `send`
- callback thread와 다른 thread에서 `set_handler`
- concurrent option setter / query / peer query
- `spot_pub` / `spot_node_publish` / `gateway send`의 same-handle concurrent send

예:

- worker thread A가 `send()` 중일 때 worker thread B가 같은 handle로 `send()` → 허용
- callback thread가 reply/publish 중일 때 worker thread가 같은 handle로 `send()` → 허용
- worker thread가 `set_handler()` 하는 동안 다른 thread가 `send()` → 허용

제한적 허용:

- `spot` / `spot_node`의 subscribe mutation은 thread-safe mode에서도
  “데이터 race 없이 호출 가능”으로 제한한다.
- publish path와 subscribe/unsubscribe가 동시에 호출될 수는 있지만,
  새 subscription이나 unsubscribe가 정확히 어느 메시지부터 관찰되는지는
  별도 visibility 규칙을 명시한다.

조건:

- 각 API는 per-handle serialization 아래에서 defined order를 가져야 한다.
- callback은 inline 실행되더라도 same-handle `send`에서 deadlock 나면 안 된다.
- `set_handler()`는 성공 후 다음 dispatch 진입 시점부터 새 handler가 관찰되어야 한다.
- subscribe/unsubscribe는 `set_handler()`와 같은 수준의 “다음 match/dispatch 진입 시점”
  visibility를 기본 계약으로 둔다.
- 즉 unsubscribe 성공 전에 이미 match/dispatch 단계에 들어간 in-flight 메시지는
  기존 subscription 기준으로 전달될 수 있고,
  unsubscribe 반환 이후 새로 match에 들어가는 메시지부터 새 subscription 상태를 본다.
- subscribe도 동일하게, 호출 반환 이후 새로 match에 들어가는 메시지부터 적용되는 것으로 본다.

`set_handler()`, subscribe/unsubscribe 모두 같은 visibility 패턴을 따른다.

```text
시간 -->
msg A (match 진입) ... set_handler()/unsubscribe() 반환 ... msg B (match 진입)
                                    |
           A는 기존 handler/subscription으로 처리될 수 있음
                                    B부터 새 handler/subscription 적용
```

제한:

- `destroy` / `close`와 다른 API 동시 호출은 가장 엄격한 제한을 둔다.
- 기본 규칙은 “same-handle public API가 하나라도 in-flight면 close/destroy는 성공하지 않는다”이다.
- 즉 cross-thread `close` / `destroy`는 in-flight callback뿐 아니라
  in-flight `send`, option setter, subscribe/unsubscribe, query, monitor API와도 충돌하면
  `EBUSY`를 반환해야 한다.

### 7.2 non-thread-safe mode

허용:

- 단일 thread 또는 외부 직렬화 아래의 모든 기존 사용 패턴
- callback thread 내 same-handle `send`

금지:

- same-handle concurrent `send`
- callback thread와 다른 thread에서 동시에 mutation API 호출
- subscribe/unsubscribe/set_handler/destroy race

권장 디버그 정책:

- debug build에서 owner thread 또는 external serialization 위반을 assert/log 한다.
- release build에서는 UB로 남기지 않고 가능한 범위에서 `EINVAL` 또는 `EBUSY`로 fail-fast 한다.

close/destroy에 대해서는 다음처럼 해석한다.

- `NON_THREAD_SAFE`에서도 close/destroy 관련 최소 상태(`closing_requested`,
  in-flight counter, callback depth 같은 파괴 방지용 상태)는 atomic 또는 경량 guard로 유지할 수 있다.
- 즉 no-lock contract는 "운영 API를 라이브러리가 직렬화하지 않는다"는 뜻이지,
  self-close 안전성이나 best-effort `EBUSY` 판단에 필요한 최소 보호까지 없앤다는 뜻은 아니다.
- 따라서 `NON_THREAD_SAFE`에서도 cross-thread close/destroy가 감지 가능한 경우에는
  best-effort로 `EBUSY`를 반환하는 것을 권장한다.

쉽게 말해:

- debug: 잘못 쓰면 바로 assert/log
- release: 가능한 한 조용히 망가지지 말고 `EINVAL` / `EBUSY`로 빠르게 실패

## 8. 락/동기화 설계

### 8.1 기본 전략

thread-safe mode의 기본 구현은 “global lock”이 아니라 “per-handle lock”이다.

즉 "라이브러리 전체에 큰 락 하나"가 아니라,
"각 handle이 자기 상태를 스스로 지키는 방식"을 기본으로 한다.

- raw socket: socket handle별 mutex
- `gateway`: existing `_sync` 재사용
- `spot_node`: existing `_sync` / `_ctrl_sync` 정리
- `spot_pub`: existing `_sync` 재사용
- `spot_sub`: 새 per-handle lock 또는 parent serialization 재사용
- unified `spot`: facade-level state lock + child pub/sub lock 연계

### 8.2 callback 중 send 허용

현재 구조상 callback 안 send는 중요한 사용 패턴이므로 유지한다.

설계 원칙:

- callback 호출 시 send path와 동일한 non-reentrant lock을 쥔 채 진입하면 안 된다.
- 이미 recursive mutex를 쓰는 곳은 즉시 동작시킬 수 있지만,
  장기적으로는 dispatch 진입 시 어떤 handler를 호출할지 판단하는 단계(classification)의
  lock scope를 최소화하는 것이 더 바람직하다.
- reply/publish 같은 callback 내 send는 deadlock 없이 동작해야 한다.

권장 패턴:

- dispatch 진입 시 handler pointer와 callback에 필요한 최소 상태만 읽는다.
- 필요하면 in-dispatch flag 또는 callback depth를 올린다.
- 그 뒤 subject lock을 풀고 callback을 호출한다.
- callback 안 `send`는 일반 send path와 동일하게 per-handle lock을 다시 잡아 처리한다.

즉 핵심은 "callback 호출 시점에는 send path와 같은 락을 오래 쥐고 있지 않는다"이다.

### 8.3 handler pointer

가능한 경우 handler pointer는 atomic pointer를 사용한다.

이유:

- dispatch fast path에서 lock hold 시간을 줄일 수 있다.
- `set_handler()` visibility semantics를 구현하기 쉽다.

권장 규칙:

- handler 교체는 atomic store-release
- dispatch read는 atomic load-acquire
- 단, handler 외의 연관 상태가 있으면 그 상태는 별도 mutex 보호가 필요하다.

### 8.4 mode별 lock 분기

모든 handle은 내부에 다음과 같은 필드를 가질 수 있다.

```c
zlink_thread_mode_t _thread_mode;
bool _use_lock;
```

규칙:

- `THREAD_SAFE` -> `_use_lock = true`
- `NON_THREAD_SAFE` -> `_use_lock = false`

단, 모든 상태가 완전히 lock-free가 되는 것은 아니다.
다음은 mode와 무관하게 atomic 또는 내부 transport 보호가 필요할 수 있다.

- stop flag
- handler pointer
- monitor close guard
- shared I/O registration state

## 9. subject별 상세 계획

### 9.1 raw socket

구현 목표:

- recv-capable raw socket의 same-handle concurrent `send`를 지원
- `zlink_socket_set_handler()`와 dispatch race를 정의
- `zlink_close()`는 cross-thread close면 `EBUSY`, callback self-close면 deferred teardown으로 정리

비고:

- send-only socket도 동일한 thread mode enum을 사용한다.
- send-only socket에서 `THREAD_SAFE`의 의미는 concurrent `send` 허용이다.

raw socket 쪽에서 핵심은 두 가지다.

- recv-capable socket도 same-handle concurrent `send`를 명시적으로 지원할 것
- close와 handler 교체 race를 더 이상 암묵 규칙으로 두지 않을 것

### 9.2 discovery

`discovery`는 raw socket이나 `gateway`와 성격이 조금 다르다.
핵심은 메시지 send 경쟁이 아니라 topology/observer/query 경쟁이다.

구현 목표:

- `zlink_discovery_new(..., thread_mode)`로 생성자 시그니처 변경
- topology cache update와 service query가 동시에 돌아도 data race가 없도록 정리
- observer attach/detach와 monitor open/set_handler/close 경쟁을 정의

쉽게 말해 `discovery`의 `THREAD_SAFE`는
"여러 thread가 같은 discovery를 동시에 조회/관찰/갱신해도 계약이 깨지지 않는다"는 뜻이다.

### 9.3 gateway

현재 구현은 이미 thread-safe 지향이므로 가장 먼저 정리하기 좋다.

구현 목표:

- `_use_lock`를 public `thread_mode`와 직접 연결
- `zlink_gateway_new(..., thread_mode)`로 생성자 시그니처 변경
- send/send_rid/set_handler/monitor_open에
  동일한 mode 계약 적용
- callback 중 `send_rid()` reply를 공식 허용 패턴으로 문서화

추가 검토:

- 현재 dispatch에서 `_sync`를 잡고 handler를 호출하는 구조는 동작은 되지만
  lock hold가 길 수 있다.
- classification(어떤 handler를 호출할지 판단하는 단계)에 필요한 최소 state만
  복사한 뒤 lock을 풀고 callback을 호출하는 방향을 권장한다.

즉 `gateway`는 이미 lock 기반 구조가 있으니,
"락을 잡고 오래 버티는 구현"을 줄이고
"필요한 상태만 복사하고 callback은 락 밖에서 실행"하는 쪽으로 다듬는 것이 좋다.

### 9.4 spot_node

`spot_node`는 publish, subscribe, discovery attach, register/unregister, TLS/bind/peer connect가
모두 한 subject에 걸쳐 있어서 가장 까다롭다.
data path만이 아니라 discovery, TLS, peer 관리, child facade 관리까지 함께 보는 대상이다.

구현 목표:

- `zlink_spot_node_new(..., thread_mode)`로 생성자 시그니처 변경
- publish vs subscribe mutation vs handler 교체를 defined order로 정리

### 9.5 unified spot

구현 목표:

- `zlink_spot_new(..., thread_mode)`로 생성자 시그니처 변경
- `spot->handler`, `spot->pub`, `spot->sub`, role mask state 보호
- callback 중 `zlink_spot_publish()` 허용
- `spot` facade-level API와 child pub/sub API의 thread mode 상호작용 정의
- subscribe/unsubscribe의 visibility 규칙을 `set_handler()`와 같은 수준으로 명시
- unified `spot`은 node-owned default pub/sub를 재사용하지 않고
  생성 시 별도 child pub/sub를 attach하는 구조를 유지한다.
  즉 `spot`의 `THREAD_SAFE` 보장은 자기 child facade lock과 child socket 기준으로 성립해야 한다.
- `spot_node`와 `spot` 사이에 남는 공유 영역은
  health/fault 상태, child registry, topology summary 같은 관리 상태다.
  이 부분은 node 내부 coordination으로 보호되고,
  `spot_node` direct API의 `NON_THREAD_SAFE` 계약과 분리해서 정의해야 한다.

unified `spot`의 thread-safety는 두 축으로 나눠서 본다:
data path(publish/subscribe)는 child facade 기준,
node와의 공유 영역(health/fault, child registry 등)은 관리 상태로 구분한다.

### 9.6 SpotPub / SpotSub

`SpotPub`는 이미 thread-safe subject에 가깝다.
이번 계획에서는 “특별취급된 예외”가 아니라 전체 모델의 일부로 재정의한다.

구현 목표:

- `zlink_spot_pub_new(..., thread_mode)` / `zlink_spot_sub_new(..., thread_mode)`로
  생성자 시그니처 변경
- `SpotPub`은 기존 thread-safe publish 의미를 유지하되 public mode enum으로 정렬
- `SpotSub`도 subscribe/unsubscribe/set_handler/peer query에 동일한 mode 규칙 적용

## 10. parent-child mode 제약

이 절은 "parent가 어떤 mode일 때 child를 어떤 mode로 만들 수 있나"를 정리한다.
핵심은 stronger child on weaker parent를 어디까지 허용할지다.

먼저 사용자 관점에서 가장 헷갈릴 수 있는 차이를 짚으면 다음과 같다.

- unified `spot`은 생성 시 별도 child pub/sub를 만들고,
  node 내부 inproc proxy/data-plane endpoint에 연결된다.
  그래서 parent `spot_node` handle과 같은 public socket handle을 직접 공유하지 않는다.
- standalone `spot_pub` / `spot_sub`는 parent `spot_node`와 더 직접적인 조합으로 본다.
  따라서 parent보다 stronger한 mode를 가지게 하지 않는다.

즉 둘 다 `spot_node`에 attach되지만,
문서에서는 unified `spot`을 "독립 facade 조합",
standalone `spot_pub` / `spot_sub`를 "parent와 더 직접적인 child 조합"으로 구분해서 본다.

### 10.1 unified spot (`zlink_spot_new`)

`zlink_spot_new()`는 parent `spot_node`의 thread mode와 무관하게 독립적으로 mode를 선택할 수 있다.
mode 조합 제약 없이 모든 조합이 허용된다.

근거:

- unified `spot`은 parent `spot_node`의 default pub/sub handle을 공유하지 않는다.
- 생성 시 별도 `spot_pub` / `spot_sub`를 만들고,
  이 child facade들은 node 내부 data-plane의 inproc proxy endpoint에 연결된다.
- 따라서 `spot`의 publish/subscribe/handler 경로는
  `spot_node` direct API와 동일 public socket handle을 놓고 경쟁하지 않는다.
- parent `spot_node`의 `NON_THREAD_SAFE`는 node direct API 사용 규칙을 뜻하며,
  node 내부 data-plane 및 child facade coordination까지 no-lock으로 바꾼다는 뜻은 아니다.

즉 unified `spot`은 예외가 아니라,
"별도 child facade가 node 내부 proxy에 연결되는 구조"라는 점 때문에
독립 mode 선택이 가능한 케이스라고 이해하면 된다.

### 10.2 standalone SpotPub / SpotSub

`zlink_spot_pub_new()` / `zlink_spot_sub_new()`는 parent `spot_node`의 mode와 다음 제약을 가진다.

| parent `spot_node` | standalone pub/sub | 허용 여부 | 이유 |
|---|---|---|---|
| `THREAD_SAFE` | `THREAD_SAFE` | 허용 | 가장 자연스러운 조합 |
| `THREAD_SAFE` | `NON_THREAD_SAFE` | 허용 | child만 no-lock fast path 선택 가능 |
| `NON_THREAD_SAFE` | `NON_THREAD_SAFE` | 허용 | 전체를 외부 직렬화로 운용 |
| `NON_THREAD_SAFE` | `THREAD_SAFE` | 금지 | weaker parent를 stronger child가 보정할 수 없음 |

권장 에러:

- invalid 조합은 `EINVAL`

standalone pub/sub는 unified `spot`과 다르게 parent mode 제약을 둔다.
이 문서는 그 차이를 일부러 남긴다.

- unified `spot`은 별도 facade 조합으로 본다.
- standalone pub/sub는 parent `spot_node`와 더 직접적인 조합으로 본다.

### 10.3 discovery-gateway / discovery-spot_node mode 제약

`gateway`와 `discovery`, `spot_node`와 `discovery`의 mode 관계는 standalone pub/sub와 같은 원칙을 따른다.

단, `gateway` / `spot_node`는 생성 시 `discovery`를 받지 않고 `attach_discovery()`로 나중에 연결하므로,
mode 호환 체크는 생성 시점이 아니라 `attach_discovery()` 호출 시점에 수행한다.

| `discovery` mode | `gateway` mode | 허용 여부 | 이유 |
|---|---|---|---|
| `THREAD_SAFE` | `THREAD_SAFE` | 허용 | 가장 자연스러운 조합 |
| `THREAD_SAFE` | `NON_THREAD_SAFE` | 허용 | child만 no-lock fast path 선택 가능 |
| `NON_THREAD_SAFE` | `NON_THREAD_SAFE` | 허용 | 전체를 외부 직렬화로 운용 |
| `NON_THREAD_SAFE` | `THREAD_SAFE` | 금지 | weaker parent를 stronger child가 보정할 수 없음 |

## 11. destroy / close 정책

이 항목은 thread-safe 설계에서 가장 중요한 리스크다.

먼저 핵심 규칙을 짧게 적으면 다음과 같다.

- `close` / `destroy`는 모든 public API 중 가장 강한 제약을 가진다.
- cross-thread close/destroy는 same-handle in-flight public API가 하나라도 있으면 `EBUSY`다.
- self-close는 허용하지만 "즉시 free"가 아니라 "close 요청만 기록하고 callback return 뒤 teardown"이다.
- self-close 성공 후에는 그 handle을 다시 쓰면 안 된다.

여기서 in-flight public API에는 다음이 포함된다.

- recv callback dispatch
- `send` / `publish`
- `set_handler`
- subscribe / unsubscribe
- option setter / query
- monitor open / close / set_handler
- bind / connect / attach_discovery 같은 lifecycle-adjacent API

머릿속에서는 아래 순서로 그리면 된다.

```text
1. handle이 평소처럼 동작 중
2. 어떤 API가 들어와서 아직 return하지 않음   <- in-flight
3. 이때 다른 thread가 close/destroy 호출
4. 라이브러리는 "지금은 종료하면 위험하다"고 판단
5. EBUSY 반환
6. 호출자가 작업이 끝난 뒤 다시 close/destroy 시도
```

### 11.1 cross-thread close/destroy (callback thread 외부에서 호출)

위 흐름을 그대로 적용한다.
`THREAD_SAFE` mode라도 close/destroy는 "아무 concurrent call과도 안전하게 섞이는 API"가 아니다.

- 다른 thread에서 같은 handle의 public API가 하나라도 in-flight면
  `close()` / `*_destroy()` / `zlink_service_monitor_close()`는 `EBUSY`를 반환한다.
- `EBUSY`를 받은 caller는 in-flight 작업이 끝난 뒤 다시 시도해야 한다.
- 완료 시점 판단은 caller 책임이며, 라이브러리는 별도 wait API를 제공하지 않는다.

예:

| 상황 | 결과 |
|---|---|
| thread A가 `send()` 중일 때 thread B가 `zlink_close()` | `EBUSY` |
| thread A가 `subscribe()` 중일 때 thread B가 `zlink_spot_destroy()` | `EBUSY` |
| monitor callback 실행 중일 때 다른 thread가 `zlink_service_monitor_close()` | `EBUSY` |

`NON_THREAD_SAFE` mode에서도 해석은 크게 다르지 않다.

- 라이브러리는 운영 API를 내부 lock으로 직렬화하지 않는다.
- 하지만 self-close 안전성과 파괴 방지용 최소 상태는 유지할 수 있다.
- 따라서 cross-thread close/destroy가 명확히 감지되면 best-effort `EBUSY`를 반환할 수 있다.
- 다만 `THREAD_SAFE`처럼 강한 직렬화 보장을 제공하는 것은 아니며,
  외부 직렬화 책임은 여전히 caller에 있다.

### 11.2 self-close (callback 안에서 자기 handle close/destroy 호출)

- callback 안에서 자기 자신의 handle 종료를 요청하는 것은 허용한다.
- 단 성공의 의미는 "지금 즉시 메모리를 해제했다"가 아니라
  "close 요청을 accepted 했고 callback return 뒤 teardown 하겠다"이다.
- 구현은 `closing_requested` 같은 내부 플래그만 세우고,
  실제 shutdown / free는 dispatcher epilogue에서 수행한다.
- 이 정책은 `direct-callback-recv-rewrite-spec.ko.md`의 deferred teardown 계약을 그대로 따른다.

흐름으로 보면 다음과 같다.

```text
callback 진입
   |
   +-- callback 안에서 self-close 요청
   |      -> 성공 반환
   |      -> 하지만 아직 free는 안 함
   |
   +-- callback return
          |
          +-- dispatcher epilogue
                 -> closing_requested 확인
                 -> 실제 teardown / free
```

왜 이렇게 하느냐:

- callback stack 위에서 즉시 free하면 현재 실행 중인 frame이 dangling reference가 되기 쉽다.
- 따라서 self-close는 "지금 닫아 달라"는 요청만 받고,
  실제 객체 해제는 callback이 완전히 빠져나간 뒤로 미룬다.

raw socket의 self-close:

- raw socket과 raw monitor handle은 `zlink_close(handle)`로 self-close를 요청한다.

service object의 self-close:

- `gateway`, `discovery`, `spot_node`, `spot`, `spot_pub`, `spot_sub`,
  service monitor는 기존 시그니처를 유지하므로 `*_destroy(void **handle_p)` 또는
  `zlink_service_monitor_close(void **handle_p)`로 self-close를 요청한다.
- 즉 callback 안에서 service handle을 닫고 싶다면,
  caller는 자신이 소유한 handle slot의 주소를 넘겨야 한다.

예:

```c
static void *g_spot = NULL;

static void on_spot_msg (const zlink_routing_id_t *rid,
                         const char *topic,
                         size_t topic_len,
                         zlink_msg_t *parts,
                         size_t part_count)
{
  zlink_spot_destroy (&g_spot);
  return;
}
```

service self-close의 추가 규칙:

- `handle_p`는 실제 현재 handle을 가리키는 유효한 storage slot이어야 한다.
- self-close가 성공하면 라이브러리는 그 slot을 더 이상 재사용 대상으로 보지 않는다.
  실제 메모리 해제는 epilogue로 미루더라도, 호출자 관점에서는 "이 handle은 끝났다"로 본다.
- self-close 이후 callback 안에서 같은 handle로 추가 API를 호출하는 것은 정의하지 않는다.
  self-close는 callback의 마지막 동작이어야 한다.

실무적으로는 아래처럼 이해하면 된다.

- raw socket: handle 값 자체를 넘겨서 닫는다.
- service object: handle 변수가 들어 있는 slot 주소를 넘겨서 닫는다.
- 둘 다 "즉시 해제"가 아니라 "종료 예약"에 가깝다.

### 11.3 권장 공개 계약 문구

문서와 header comment에는 아래 수준으로 직접 적는 것을 권장한다.

- "`THREAD_SAFE` handle도 close/destroy는 다른 same-handle public API와 자유롭게 병행할 수 없다."
- "cross-thread close/destroy는 same-handle in-flight public API가 있으면 `EBUSY`를 반환한다."
- "callback 안 self-close는 허용하지만 실제 teardown은 callback return 뒤 수행된다."
- "service object self-close는 `void **handle_p`를 받는 기존 destroy/close API로 요청한다."

## 12. 성능 관점

이 절의 요점은 "thread-safe mode를 넣어도 성능이 무조건 나빠진다"는 뜻이 아니라,
"비용이 어디서 생기는지 알고 선택하자"에 가깝다.

### 12.1 예상 효과

- recv callback-only 구조에서는 recv-side queue lock 경쟁이 줄어든다.
- 따라서 thread-safe mode의 추가 비용은 대부분 send/mutation/lifecycle 직렬화에 집중된다.
- 일반 네트워크/TLS/syscall 지배 workload에서는 uncontended per-handle lock 비용이
  큰 문제일 가능성은 낮다.

### 12.2 주의 workload

다음은 lock 비용이 눈에 띌 수 있다.

- inproc 중심
- 매우 작은 메시지
- 매우 높은 same-handle fan-in send
- callback 안에서 즉시 same-handle send 반복

따라서 single-thread 최적화가 필요한 사용자를 위해
`NON_THREAD_SAFE` mode를 계속 제공해야 한다.

### 12.3 앱 레벨 권장 패턴

thread-safe mode가 있더라도 최고 성능 패턴은 여전히 다음이다.

- callback에서는 최소 작업만 수행
- ownership move 또는 shallow copy 후 worker queue에 enqueue
- 실제 비즈니스 처리와 fanout send는 worker thread에서 수행
- callback 안에서 종료가 필요하면 가능하면 self-close를 사용한다.
- 외부 thread에서 종료가 필요하면 stop flag, worker join, callback quiesce 같은 방식으로
  먼저 in-flight 작업을 정리한 뒤 close/destroy를 호출한다.
- `while (close() == EBUSY) sleep(...)` 같은 busy-wait retry는 권장하지 않는다.

즉, thread-safe mode는 correctness와 usability를 높이는 기본 장치이지,
모든 workload에서 최저 latency를 보장하는 마법은 아니다.

## 13. 구현 항목

### 13.1 enum / 생성자 정리

- `zlink_thread_mode_t` 추가
- 기존 생성자 시그니처에 `thread_mode` 추가
- `zlink_socket`: `handler` / `thread_mode` 추가 (단일 함수로 통합 완료)

구현 항목은 순서를 이렇게 보면 된다.

1. public enum과 생성자 시그니처를 먼저 고친다.
2. 각 subject 내부에 mode-aware synchronization을 넣는다.
3. 마지막으로 close/destroy race와 테스트를 닫는다.

### 13.2 raw socket 내부 반영

- socket base 또는 handle wrapper에 `_thread_mode`, `_use_lock` 추가
- recv-capable raw socket의 send/set_handler/close 경로에 mode-aware serialization 도입

### 13.3 gateway 반영

- 기존 `_use_lock`와 public `thread_mode` 연결
- `gateway` 회귀 테스트를 mode별로 분리

### 13.4 monitor 계열 반영

- `zlink_socket_monitor_open()`, `zlink_discovery_monitor_open()`,
  `zlink_gateway_monitor_open()`, `zlink_spot_monitor_open()`,
  `zlink_spot_sub_monitor_open()`, `zlink_spot_pub_monitor_open()`에
  `thread_mode` 시그니처 변경 반영
- parent subject와 child monitor 간 stronger-on-weaker 금지 규칙 구현
- `zlink_monitor_set_handler()` / `zlink_service_monitor_set_handler()`의
  replace visibility를 “다음 dispatch 진입 시점” 계약으로 고정
- monitor close guard와 in-flight callback 경쟁을 mode-aware하게 정리

### 13.5 spot 계열 정리

- `spot_node`, unified `spot`, `spot_pub`, `spot_sub` 생성자 시그니처 변경
- standalone `spot_pub` / `spot_sub` 에 대한 parent-child mode 제약 구현

### 13.6 destroy race 정리

- cross-thread close: same-handle in-flight public API 감지 후 `EBUSY` 반환 구현
- self-close: `closing_requested` 플래그 설정 및 dispatcher epilogue에서 실제 teardown 수행 구현
- 두 케이스를 구분하는 내부 상태(callback thread id, in-dispatch flag,
  public API in-flight counter 등) 결정 및 구현
- 위 정책에 맞는 cross-thread close 테스트 및 self-close deferred teardown 테스트 추가

## 14. 테스트 계획

이 절은 "새 API가 생겼는가"보다 "새 계약이 실제로 지켜지는가"를 확인하는 데 초점을 둔다.

### 14.1 공통 생성 mode 테스트

- 생성자는 requested mode를 내부에 반영한다.
- invalid mode 값은 `EINVAL`
- `zlink_socket(ctx, ZLINK_SOCKET_PUB, NULL, thread_mode)`: send-only `PUB`은 handler 없이 생성 성공
- `zlink_socket(ctx, recv_capable_type, NULL, thread_mode)`: recv-capable 타입은 `NULL` handler로 생성 실패
- `zlink_socket(..., handler, thread_mode)`: recv-capable 타입은 handler와 함께 생성 성공, 첫 메시지부터 handler 호출
- `zlink_spot_node_new(ctx, service_name, handler, thread_mode)`: service-bound 생성 성공
- `zlink_spot_sub_new(node, THREAD_SAFE)`: 생성 성공; handler는 이후 `set_handler()`로 지정
- `zlink_spot_sub_new(node, invalid_mode)`: 유효하지 않은 mode → `NULL` / `EINVAL`

### 14.2 thread-safe mode 테스트

- same-handle concurrent send 성공
- callback thread와 worker thread의 동시 send 성공
- `set_handler()` 후 다음 dispatch부터 새 handler 적용
- handler 교체 실패 시 기존 handler 유지

### 14.3 non-thread-safe mode 테스트

- single-thread 사용은 정상 동작
- debug build에서 cross-thread misuse assert 또는 로그
- release build에서는 `EINVAL` 또는 `EBUSY` fail-fast 확인

### 14.4 gateway 테스트

- callback 안 `zlink_gateway_send_rid()` reply 성공
- multi-thread `send` / `set_handler` 공존
- `NON_THREAD_SAFE` gateway에서 concurrent mutation misuse 검출

### 14.5 monitor 테스트

- `*_monitor_open(..., thread_mode)`가 requested mode를 monitor handle에 반영
- parent `NON_THREAD_SAFE` + child `THREAD_SAFE monitor` 조합은 `EINVAL`
- parent `THREAD_SAFE` + child `NON_THREAD_SAFE monitor` 조합은 허용 여부대로 동작
- `zlink_monitor_set_handler()` / `zlink_service_monitor_set_handler()` 후
  다음 dispatch부터 새 handler 적용
- monitor callback in-flight 중 다른 thread에서 close → `EBUSY`; callback self-close → deferred teardown

### 14.6 spot 테스트

- callback 안 `zlink_spot_publish()` 성공
- standalone `spot_pub` / `spot_sub` parent `NON_THREAD_SAFE` + child `THREAD_SAFE` → `EINVAL`
- `spot_sub` concurrent subscribe/unsubscribe/set_handler ordering

### 14.7 destroy 테스트

- cross-thread close: in-flight callback 실행 중 다른 thread에서 close/destroy → `EBUSY` 반환 확인
- cross-thread close: in-flight `send` / subscribe / option setter 중 다른 thread에서 close/destroy → `EBUSY` 반환 확인
- cross-thread close: `EBUSY` 후 in-flight 완료 보장 뒤 재시도 → 성공 확인
- raw self-close: callback 안에서 자기 handle `zlink_close()` → 즉시 성공, epilogue에서 teardown 확인
- service self-close: callback 안에서 `*_destroy(&handle_slot)` 또는
  `zlink_service_monitor_close(&handle_slot)` → 즉시 성공, epilogue에서 teardown 확인
- concurrent send + cross-thread close race → `EBUSY` 또는 send 정상 처리 확인
- monitor cross-thread close race → 동일 policy 적용 확인

## 15. 정책 회귀 테스트

이 절은 thread mode 도입으로 새로 생기는 공개 정책이 구현 이후에도 다시 무너지지 않도록
막는 회귀 테스트 항목을 정리한다.
시그니처 값 대조 테스트보다 "정책 계약이 지켜지는가"를 중점적으로 확인한다.

쉽게 말해 이 절은 "헤더 모양이 맞는가"와
"실행해 봤을 때 정말 그 규칙대로 동작하는가"를 분리해서 보는 체크리스트다.

각 항목은 아래 기준으로 분류한다.

- **[런타임]**: 실행 중 결과 또는 에러코드를 직접 확인하는 테스트
- **[ABI]**: API/타입 존재 여부 검증. 컴파일 타임 또는 헤더 정적 검사로 수행

### 15.1 thread mode 고정 정책

**[ABI]** 모든 주요 생성자(`zlink_socket`, `zlink_discovery_new`, `zlink_gateway_new`,
`zlink_spot_node_new`, `zlink_spot_new`, `zlink_spot_pub_new`, `zlink_spot_sub_new`,
`*_monitor_open`)는 `zlink_thread_mode_t` 인자를 명시적으로 받는다.
implicit default mode를 가진 overload가 추가되면 헤더 검사로 검출한다.

**[ABI]** 생성 후 thread mode를 바꾸는 setter가 public header에 존재하지 않아야 한다.
(예: `zlink_socket_set_thread_mode()` 같은 이름의 API가 생기면 즉시 회귀)

**[런타임]** 유효하지 않은 `thread_mode` 값으로 생성하면 `NULL` / `EINVAL`을 반환해야 한다.

**[런타임]** `THREAD_SAFE`로 생성한 handle은 concurrent send가 data corruption 없이 처리된다.
`NON_THREAD_SAFE`로 생성한 handle은 단일 thread에서 동일한 기능 결과를 낸다.
(내부 `_use_lock` 상태를 직접 조회하는 API는 없으므로 동작 기반 간접 검증으로 대체한다.)

### 15.2 parent-child mode 제약 정책

standalone `spot_pub` / `spot_sub` 조합 (금지 케이스):

**[런타임]** `NON_THREAD_SAFE` `spot_node`에서 `THREAD_SAFE` `spot_pub` 생성 → `EINVAL` / `NULL`

**[런타임]** `NON_THREAD_SAFE` `spot_node`에서 `THREAD_SAFE` `spot_sub` 생성 → `EINVAL` / `NULL`

standalone `spot_pub` / `spot_sub` 조합 (허용 케이스):

**[런타임]** `THREAD_SAFE` `spot_node`에서 `NON_THREAD_SAFE` `spot_pub` 생성 → 성공

**[런타임]** `THREAD_SAFE` `spot_node`에서 `NON_THREAD_SAFE` `spot_sub` 생성 → 성공

**[런타임]** `NON_THREAD_SAFE` `spot_node`에서 `NON_THREAD_SAFE` `spot_pub` / `spot_sub` 생성 → 성공

unified `spot` 조합:

**[런타임]** `NON_THREAD_SAFE` `spot_node`에서 `THREAD_SAFE` `spot` 생성 → 성공
(unified `spot`은 parent mode 제약 없이 독립 mode 선택 가능)

**[런타임]** `THREAD_SAFE` `spot_node`에서 `NON_THREAD_SAFE` `spot` 생성 → 성공

spot mode 독립성 검증 (핵심 케이스):

**[런타임]** `NON_THREAD_SAFE` `spot_node` + `THREAD_SAFE` `spot` 조합에서
`spot`에 대한 concurrent publish / set_handler 호출이 data corruption 없이 처리되어야 한다.
`spot_node`가 `NON_THREAD_SAFE`이더라도 `spot` 자체의 thread-safe 보장은 유지되어야 한다.

**[런타임]** 위 조합에서 callback thread와 worker thread가 동시에 `zlink_spot_publish()`를 호출해도
deadlock이나 data corruption이 발생하지 않아야 한다.

비고:
이 두 항목은 `spot`의 thread mode가 `spot_node`로부터 완전히 독립적임을 실제 concurrent 동작으로
검증하는 핵심 회귀 테스트다. unified `spot`은 child pub/sub를 별도로 만들고
node data-plane proxy에 attach되므로, `spot_node`의 `NON_THREAD_SAFE`가
`spot`의 publish/subscribe/handler 경로 thread-safe 보장을 깨지 않는지 확인한다.

monitor 조합:

**[런타임]** `NON_THREAD_SAFE` subject에서 `THREAD_SAFE` monitor open → `EINVAL` / `NULL`

**[런타임]** `THREAD_SAFE` subject에서 `NON_THREAD_SAFE` monitor open → 성공

위 규칙은 `zlink_socket_monitor_open`, `zlink_discovery_monitor_open`,
`zlink_gateway_monitor_open`, `zlink_spot_monitor_open`,
`zlink_spot_pub_monitor_open`, `zlink_spot_sub_monitor_open` 전체에 동일하게 적용된다.

### 15.3 discovery attach 시점 mode 호환 정책

`gateway` attach 케이스:

**[런타임]** `THREAD_SAFE` `gateway`에 `NON_THREAD_SAFE` `discovery` attach → `EINVAL`

**[런타임]** `NON_THREAD_SAFE` `gateway`에 `NON_THREAD_SAFE` `discovery` attach → 성공

**[런타임]** `THREAD_SAFE` `gateway`에 `THREAD_SAFE` `discovery` attach → 성공

**[런타임]** `NON_THREAD_SAFE` `gateway`에 `THREAD_SAFE` `discovery` attach → 성공

체크 시점 검증:

**[런타임]** `THREAD_SAFE` `gateway`를 `zlink_gateway_new()`로 생성하는 것 자체는 항상 성공해야 한다.
`NON_THREAD_SAFE` discovery가 이후에 attach 시도될 때 비로소 `EINVAL`이 발생해야 한다.

`spot_node` attach 케이스:

**[런타임]** `THREAD_SAFE` `spot_node`에 `NON_THREAD_SAFE` `discovery` attach → `EINVAL`

**[런타임]** `NON_THREAD_SAFE` `spot_node`에 `NON_THREAD_SAFE` `discovery` attach → 성공

**[런타임]** `THREAD_SAFE` `spot_node`에 `THREAD_SAFE` `discovery` attach → 성공

**[런타임]** `NON_THREAD_SAFE` `spot_node`에 `THREAD_SAFE` `discovery` attach → 성공

**[런타임]** 체크는 `zlink_spot_node_new()` 시점이 아니라 `attach_discovery()` 호출 시점에 발생해야 한다.

### 15.4 직렬화 및 visibility 정책

send 직렬화:

**[런타임]** `THREAD_SAFE` handle에서 N개 thread로 same-handle concurrent send를 동시에 호출하면
data corruption 없이 처리되거나 명시적 에러(`EAGAIN` 등)를 반환해야 한다.
조용한 data loss나 UB는 없어야 한다.

**[런타임]** `THREAD_SAFE` gateway에서 callback thread와 worker thread가 동시에 `send_rid()`를 호출해도
deadlock이 발생하지 않아야 한다.

**[런타임]** `THREAD_SAFE` `spot_pub`에서 concurrent `publish()`가 data corruption 없이 처리된다.

**[런타임]** `THREAD_SAFE` handle에서 concurrent send 중 `set_handler()` 호출이 동시에 발생해도
data corruption이나 handler 중간 상태 없이 처리되어야 한다.
send는 정상 처리되거나 에러를 반환하고, handler는 기존 또는 새 handler 중 하나로 완전히 교체된 상태여야 한다.

이 절에서 중요한 포인트는 "중간 상태가 보이면 안 된다"는 점이다.
즉 호출 결과가 성공이든 실패든,
사용자는 반쯤 바뀐 handler나 반쯤 적용된 subscription 상태를 보면 안 된다.

handler replace visibility (두 mode 공통):

**[런타임]** `set_handler()` 반환 이후에 dispatch 경로에 새로 진입한 메시지는
새 handler를 호출해야 한다. (THREAD_SAFE / NON_THREAD_SAFE 두 mode 모두 해당)

**[런타임]** `set_handler()` 호출 시점에 이미 in-flight인 callback은 기존 handler로 완료된다.

**[런타임]** `set_handler(NULL)` 호출은 `EINVAL`을 반환하고 기존 handler를 유지해야 한다.

**[런타임]** `set_handler()` 실패는 기존 handler를 변경하지 않아야 한다.

subscribe/unsubscribe visibility (두 mode 공통):

**[런타임]** `unsubscribe()` 반환 이후에 새로 match에 진입하는 메시지는
해당 subscription을 더 이상 관찰하지 않아야 한다. (THREAD_SAFE / NON_THREAD_SAFE 두 mode 모두)

**[런타임]** `unsubscribe()` 호출 시점에 이미 match/dispatch 단계에 진입한 in-flight 메시지는
기존 subscription 기준으로 전달될 수 있다.

**[런타임]** `subscribe()` 반환 이후 새로 match에 진입하는 메시지부터 적용된다.

### 15.5 callback 중 send 허용 정책 (deadlock 없음)

**[런타임]** `THREAD_SAFE` gateway recv callback 안에서 `zlink_gateway_send_rid()` 호출 → deadlock 없음

**[런타임]** `THREAD_SAFE` `spot` recv callback 안에서 `zlink_spot_publish()` 호출 → deadlock 없음

**[런타임]** `THREAD_SAFE` raw socket recv callback 안에서 같은 handle send 계열 API 호출 → deadlock 없음

**[런타임]** gateway monitor callback 안에서 parent gateway의 send 계열 API 호출 → deadlock 없음

**[런타임]** 위 패턴은 `NON_THREAD_SAFE` mode에서도 동일하게 동작해야 한다.
(callback thread에서의 send는 두 mode 모두 공식 허용 패턴이다.)

### 15.6 NON_THREAD_SAFE no-lock 정책

**[런타임]** `NON_THREAD_SAFE` mode handle은 단일 thread 또는 외부 직렬화 아래에서
`THREAD_SAFE` mode와 동일한 기능 결과를 내야 한다.

**[런타임/death test]** debug build에서는 `NON_THREAD_SAFE` handle의 cross-thread 동시 호출이
assert / log 등으로 검출되어야 한다.
이 테스트는 의도적으로 violation을 유발하는 death test 패턴(callback 안에서 다른 thread로 mutation)으로
구현하고, assert 또는 abort로 종료됨을 검증한다.

**[런타임]** release build에서는 cross-thread misuse가 documented UB가 아니라
가능한 범위에서 `EINVAL` 또는 `EBUSY` fail-fast로 관찰되어야 한다.

### 15.7 destroy/close race 정책

**cross-thread close/destroy** (다른 thread에서 close 호출):

테스트 구현 원칙:
in-flight 상태를 강제하기 위해 callback 내부에서 semaphore 또는 condition variable로
close 호출 thread에 신호를 보내고, 그 신호 이후 close를 호출하는 blocking callback 패턴을 사용한다.

또한 callback뿐 아니라 다른 public API도 in-flight 상태로 만들어 같은 정책을 확인해야 한다.

이 절은 "종료 API는 언제나 마지막에만 안전하다"는 사실을 테스트로 고정하는 부분이다.

**[런타임]** `THREAD_SAFE` mode에서 same-handle in-flight public API가 실행 중인 동안
다른 thread가 `zlink_close()` / `*_destroy()` / `zlink_service_monitor_close()`를 호출하면
`EBUSY`를 반환해야 한다.

**[런타임]** `EBUSY` 수신 후 caller가 in-flight 작업 완료를 보장한 뒤 재시도하면 성공해야 한다.

**[런타임]** in-flight send / subscribe / option setter와 cross-thread close/destroy 충돌 시에도
동일한 `EBUSY` policy가 적용되어야 한다.

**[런타임]** monitor handle의 in-flight callback 중 다른 thread에서
`zlink_service_monitor_close()` 호출 시 동일 `EBUSY` policy가 적용되어야 한다.

**self-close** (callback thread 안에서 자기 handle close 호출):

**[런타임]** `THREAD_SAFE` mode에서 callback 안에서 자기 자신의 handle에 `zlink_close()`를 호출하면
즉시 `EBUSY`를 반환하지 않고 성공해야 한다. (deferred teardown — `closing_requested` 설정)

**[런타임]** `THREAD_SAFE` mode에서 service callback 안에서
`*_destroy(&handle_slot)` 또는 `zlink_service_monitor_close(&handle_slot)`를 호출해도
즉시 `EBUSY`를 반환하지 않고 성공해야 한다.

**[런타임]** self-close 이후 callback이 반환되면 dispatcher epilogue가 실제 teardown을 수행해야 한다.
teardown 완료 이후 handle은 더 이상 유효하지 않아야 한다.

**[런타임]** self-close 이후 callback 내에서 같은 handle로 추가 API 호출은 정의되지 않은 동작이다.
(self-close는 callback의 마지막 동작으로 수행하는 것이 올바른 사용 패턴임)

**[런타임]** `NON_THREAD_SAFE` mode에서 cross-thread destroy/close 경쟁은 caller 책임이며,
라이브러리가 내부 직렬화로 이를 막지 않아야 한다.

**[런타임]** `NON_THREAD_SAFE` mode에서 callback self-close도 동일하게 deferred teardown이 적용되어야 한다.
(self-close 정책은 mode에 무관하게 공통이다. callback stack 위에서 즉시 free를 하면 안 된다.)

### 15.8 mode 전이 금지 정책 (ABI 검증)

이 섹션은 런타임 테스트가 아니라 public header에 대한 정적/ABI 검증 항목이다.
구현 완료 후 header를 직접 검사하거나 빌드 단계에서 확인한다.

**[ABI]** 생성 이후 `THREAD_SAFE` ↔ `NON_THREAD_SAFE` 전환 setter가 public header에 존재하지 않아야 한다.
이를 시도하는 API가 생기면 즉시 회귀로 간주한다.

**[ABI]** `_use_lock` 같은 내부 상태를 public API로 직접 노출하는 setter가 생겨서는 안 된다.

## 16. 공개 문서 반영 대상

이 설계가 구현되면 다음 문서도 같이 업데이트해야 한다.

- `direct-callback-recv-interface-review.ko.md`
  - 생성자 시그니처에 `thread_mode` 확장안 반영
  - callback setter 계약에 mode별 제약 추가
- `direct-callback-recv-rewrite-spec.ko.md`
  - thread safety 섹션을 “선행 조건 아님”에서
    “public selectable mode” 기준으로 갱신
- `core/include/zlink.h`
  - 새 enum / 변경된 생성자 시그니처 문서화

## 17. 최종 권장안

요약하면 다음이 권장안이다.

1. raw socket과 service facade를 동일한 `zlink_thread_mode_t` 개념으로 정렬한다.
2. `discovery`, `gateway`, `spot` 계열도 raw socket과 같은 수준의 thread mode 대상에 넣는다.
3. 생성 시 `THREAD_SAFE` / `NON_THREAD_SAFE`를 고정한다.
4. 기존 생성자 이름은 유지하고 시그니처를 직접 바꾼다.
5. `gateway`와 `spot`을 다른 socket보다 특별취급하지 않고 같은 계층의 concurrency subject로 다룬다.
6. 구현은 per-handle serialization 중심으로 가고, destroy race는 보수적으로 제한한다.
7. single-thread 최적화가 필요한 사용자는 명시적으로 `NON_THREAD_SAFE`를 선택하게 한다.

한 줄로 줄이면 다음과 같다.

"모든 subject가 생성 시 thread mode를 가지게 하고,
운영 중 API는 mode에 따라 직렬화하되,
종료 API는 항상 가장 보수적으로 다룬다."
