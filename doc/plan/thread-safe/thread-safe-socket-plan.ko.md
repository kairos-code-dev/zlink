# Thread-Safe Socket / Service 설계 계획

> 상태 메모
> 이 문서는 raw socket, `discovery`, `gateway`, `spot` 계열, monitor handle을
> 모두 `thread-safe only` public contract로 정리한 설계안이다.
> public selectable thread mode는 두지 않는다.
> recv handler는 생성 시 고정이며 런타임 교체 API는 존재하지 않는다.
> 런타임에 교체 가능한 유일한 callback setter는 `*_set_send_ready_handler()`
> 계열이다.
> 실제 함수 이름과 canonical public shape는
> [`direct-callback-recv-interface-review.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-interface-review.ko.md)
> 와 [`zlink.h`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/include/zlink.h)를
> 따른다.

## 1. 목적

이 문서는 현재 진행 중인
[`direct-callback-recv-interface-review.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-interface-review.ko.md)
기준 인터페이스를 전제로,
raw socket / `gateway` / `spot` / `discovery` / monitor handle의 공개 동시성
계약을 하나의 규칙으로 정리한다.

핵심 목표:

- 모든 주요 public handle을 thread-safe contract로 통일한다.
- 생성자에 동시성 관련 추가 인자를 받지 않는다.
- raw socket, `gateway`, `spot_node`, unified `spot`, `spot_pub`,
  `spot_sub`, `discovery`, monitor handle을 같은 계층의 concurrency
  subject로 본다.
- same-handle concurrent `send` / `publish`, send-ready handler 교체,
  subscription mutation, query, lifecycle API의 경계를 명확히 한다.
- callback-only recv 모델과 충돌하지 않는 범위에서 callback 중 send를
  공식 허용 패턴으로 유지한다.
- 종료 API는 운영 API보다 더 보수적으로 다뤄서 `close` / `destroy` race를
  명시 계약으로 고정한다.

한 줄로 줄이면 방향은 단순하다.

- 모든 public handle은 생성 즉시 thread-safe다.
- 운영 API는 라이브러리가 per-handle 기준으로 직렬화한다.
- `close` / `destroy`는 예외적으로 더 엄격하게 제한한다.

## 2. 기준 문서와 용어

직접 기준은 다음이다.

- 인터페이스 기준:
  [`direct-callback-recv-interface-review.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-interface-review.ko.md)
- recv/callback 실행 모델 기준:
  [`direct-callback-recv-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-rewrite-spec.ko.md)
- 현재 공개 헤더 기준:
  [`zlink.h`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/include/zlink.h)

우선순위:

1. canonical public 함수 이름과 시그니처는 interface review와 `zlink.h`를 따른다.
2. callback-only recv, ownership, deferred teardown 규칙은 rewrite spec을 따른다.
3. 이 문서는 위 인터페이스 위에서 thread-safe public contract를 추가 정의한다.

recv scope 고정:

- public sync recv API는 이미 제거되었다 (proxy 등 내부 경로 제외).
- 이 문서의 thread-safe 설계 scope에 public recv()는 존재하지 않는다.
- callback-only recv 모델만 전제하며, blocking recv와 `EBUSY` close 정책의
  충돌, blocked thread cancel story는 이 문서의 범위 밖이다.
- 이 문서에서 data-plane이란 `send` / `publish` / callback dispatch를 뜻하며,
  public `recv()`는 여기에 포함되지 않는다.

자주 쓰는 용어:

- same-handle:
  같은 public handle 값 하나를 여러 thread가 공유해서 사용하는 경우.
- callback thread:
  recv callback, send-ready callback, 또는 monitor callback이 실제로 실행되는 thread.
- worker thread:
  callback thread가 아닌 일반 작업 thread.
- in-flight API:
  public API 호출이 시작됐고 아직 return하지 않은 상태.
- self-close:
  callback 안에서 자기 자신이 소유한 handle의 종료를 요청하는 경우.
- child handle:
  parent subject에 attach되어 열린 subordinate handle. monitor,
  standalone `spot_pub`, standalone `spot_sub`, unified `spot`이 여기에
  해당한다.

## 3. 핵심 결정

### 3.1 public selectable thread mode는 두지 않는다

이 계획에서는 thread mode 개념 자체를 public surface에서 제거한다.

- `zlink_thread_mode_t` 같은 enum을 도입하지 않는다.
- 생성자나 `*_monitor_open()`에 `thread_mode` 인자를 추가하지 않는다.
- parent-child 사이의 stronger/weaker 조합 매트릭스를 두지 않는다.
- thread-safety는 선택 옵션이 아니라 기본 공개 계약이다.

즉 사용자는 더 이상 "이 handle을 어떤 mode로 만들었나"를 기억할 필요가 없다.
질문은 하나로 줄어든다.

- "이 public handle은 same-handle concurrent 사용이 허용되나?"

이 문서의 답은 일관되게 "허용된다. 단, 종료 API는 예외다."이다.

### 3.2 모든 주요 subject는 thread-safe contract를 가진다

적용 대상:

- raw socket
- `discovery`
- `gateway`
- `spot_node`
- unified `spot`
- standalone `spot_pub`
- standalone `spot_sub`
- socket/service monitor handle

이 대상들은 모두 same-handle concurrent `send`, `publish`, send-ready handler
교체, subscription mutation, query, option setter, attach 계열 호출에 대해
data race가 없도록 정의한다.

recv handler는 생성 시 고정이며 런타임 교체 API가 없으므로 동시성 보호 대상이
아니다.

### 3.3 thread-safe 범위는 문서 계약으로 고정한다

이 문서에서 말하는 thread-safe 범위는 구현 내부 선택사항이 아니라
public contract다.

- 어떤 API를 same-handle concurrent 호출할 수 있는지는 문서, header comment,
  테스트에서 먼저 고정한다.
- 구현은 그 계약을 만족하는 방법을 선택할 수 있지만, 계약 자체를 구현마다
  다르게 해석하면 안 된다.
- 즉 범위는 문서가 정하고, mutex/atomic/TLS/batching 같은 메커니즘은 구현이
  선택한다.

포함할 범위:

- same-handle concurrent `send` / `publish` / `send_rid`
- same-handle `*_set_send_ready_handler()` 교체
- same-handle subscribe / unsubscribe / option setter / query / peer query
- same-handle `attach_discovery()`, bind/connect, monitor open/close 같은 운영 중
  lifecycle-adjacent API
- callback thread와 worker thread가 같은 handle을 함께 사용하는 경우

제외할 범위:

- `close` / `destroy`를 다른 운영 API와 자유롭게 병행하는 것
- parent와 child handle을 하나의 원자적 종료 단위로 암묵 처리하는 것
- 서로 다른 handle 사이의 전역 ordering 또는 fairness 보장
- 사용자 callback 내부 상태까지 라이브러리가 대신 동기화하는 것
- lock-free 보장, 최고 성능 보장, starvation 부재 보장

즉 이 문서의 thread-safe는 "same-handle 운영 API의 정합성을 라이브러리가
보장한다"는 뜻이며, "모든 API를 아무 때나 병행 호출해도 된다"는 뜻은 아니다.

### 3.4 기본 구현 전략은 per-handle serialization이다

기본 전략은 global lock이 아니라 handle 단위 직렬화다.

- 각 handle은 자기 상태를 자체적으로 보호한다.
- 서로 다른 handle 사이에는 불필요한 전역 락을 두지 않는다.
- parent-child coordination은 공유 상태 경계에서만 명시적으로 잡는다.

사용자 관점의 의미는 단순하다.

- worker thread 두 개가 같은 handle로 `send()`를 호출할 수 있다.
- callback thread와 worker thread가 같은 handle로 동시에 `publish()`할 수 있다.
- mutation/query가 동시에 들어와도 반쯤 적용된 중간 상태가 보이면 안 된다.

### 3.5 `close` / `destroy`는 운영 API보다 더 엄격하다

thread-safe contract가 있다고 해서 종료 API까지 아무 때나 병행 허용하는 것은 아니다.

- same-handle public API가 하나라도 in-flight면 cross-thread
  `close` / `destroy`는 성공하지 않는다.
- 이 경우 라이브러리는 `EBUSY`를 반환한다.
- 이 `EBUSY`는 정상 종료 절차의 일부가 아니라,
  호출자가 사전에 quiesce/join을 끝내지 못했다는 fail-fast 신호다.
- cross-thread `close` / `destroy`가 `EBUSY`를 반환한 경우 handle은 계속 live 상태로
  남아야 하며, 그 실패만으로 `closing_requested`가 latch되면 안 된다.
- callback 안의 self-close는 허용하지만 실제 teardown은 callback return 뒤로
  미룬다.

### 3.6 callback 중 send와 visibility 규칙을 유지한다

callback 안 send는 중요한 사용 패턴이므로 유지한다.

- recv callback 안 same-handle `send` / `publish` 허용
- monitor callback 안 parent subject send 계열 API 허용
- send-ready handler 교체는 "다음 writable transition"부터 visible
- subscribe / unsubscribe는 "다음 match 또는 dispatch 진입 시점"부터 visible

핵심 시나리오를 표로 요약하면 다음과 같다.

| 시나리오 | 결과 |
|---|---|
| worker 두 개가 같은 handle로 `send` | 허용, per-handle 기준 직렬화 |
| callback thread와 worker thread가 동시에 `publish` | 허용 |
| `set_send_ready_handler()` 도중 writable transition 발생 | 기존 또는 새 handler 중 하나로 완결 처리 |
| `unsubscribe()` 반환 후 새 메시지 유입 | 새 subscription 상태 적용 |
| 다른 thread가 in-flight API 중 `close` 호출 | `EBUSY` |
| callback 안 self-close | 성공 처리 후 epilogue teardown |

## 4. 한눈에 보는 구조

전체 그림은 아래처럼 이해하면 된다.

```text
create/open(...)
   |
   +-- thread-safe handle
          |
          +-- recv handler는 생성 시 고정 (런타임 교체 없음)
          +-- same-handle concurrent send / mutation 허용
          +-- callback 중 send 허용
          +-- send-ready handler / subscription visibility는 다음 transition/match 기준
          +-- cross-thread close/destroy는 in-flight가 있으면 EBUSY
          +-- self-close는 deferred teardown
```

`spot_node`와 child facade의 관계는 아래처럼 본다.

```text
spot_node
   |
   +-- zlink_spot_new()
   |      -> child pub/sub를 별도 생성
   |      -> node 내부 data-plane endpoint에 attach
   |
   +-- zlink_spot_pub_new()
   +-- zlink_spot_sub_new()
   |
   +-- child와 parent는 모두 thread-safe contract를 가짐
```

즉 unified `spot`, standalone `spot_pub`, standalone `spot_sub`, monitor handle은
parent와 연결된 child지만, thread-safety를 더 약하게 해석하는 별도 등급으로
보지 않는다.

## 5. 공개 API 방향

이 문서의 방향은 "생성자 시그니처에 동시성 옵션을 추가하는 것"이 아니다.
canonical public shape는 그대로 유지하고, 그 위에 thread-safe contract를
명시적으로 올린다.

| Subject | Canonical 생성/open API | 계획 방향 |
|---|---|---|
| raw socket | `zlink_socket(ctx, type, handler)` | recv-capable socket은 생성 즉시 thread-safe send subject로 본다. public recv()는 제거 완료. raw `PUB`만 `handler == NULL` 경로를 유지한다. |
| `discovery` | `zlink_discovery_new(ctx, service_type)` | topology cache, observer, query, monitor 경로를 thread-safe subject로 정리한다. |
| `gateway` | `zlink_gateway_new(ctx, service_name, routing_id, handler)` | existing `_sync` 기반 구조를 재사용하되 공개 계약은 unconditional thread-safe로 올린다. |
| `spot_node` | `zlink_spot_node_new(ctx, service_name, handler)` | publish, subscribe, discovery attach, TLS, peer connect, child registry를 thread-safe subject로 본다. |
| unified `spot` | `zlink_spot_new(spot_node, handler)` | facade-level publish/subscribe/peer query를 thread-safe contract로 유지한다. |
| standalone `spot_pub` | `zlink_spot_pub_new(node)` | 이미 thread-safe로 설명되던 성격을 공통 계약 일부로 흡수한다. |
| standalone `spot_sub` | `zlink_spot_sub_new(node, handler)` | subscription mutation, peer query를 thread-safe subject로 정리한다. |
| monitor handle | `*_monitor_open(..., handler)` | 모든 monitor를 독립 thread-safe child handle로 본다. |

추가 방향:

- `zlink_gateway_attach_discovery()` / `zlink_spot_node_attach_discovery()`는
  동시성 모드 호환 체크를 하지 않는다. 단, topology ownership 규칙은 유지한다:
  - manual peer/route가 하나라도 존재하는 상태에서 attach를 호출하면 `EBUSY`
  - attach 이후에는 manual `connect` / `disconnect`가 금지된다
  - topology 변화는 discovery-driven convergence로만 반영된다
  - attach 시점에는 위 ownership 전제와 lifetime / in-flight state를 함께 검증한다.
- `zlink_spot_pub_new()`의 "default thread-safe" 같은 문구는 공통 규칙에 맞춰
  일반화한다. 이제 특별취급된 예외가 아니라 전체 모델의 일부다.
- public header에는 `_use_lock`나 internal sync field를 노출하지 않는다.

## 6. 허용 동시 호출 계약

이 절은 "어디까지를 라이브러리가 책임지고 직렬화해 주는가"를 정리한다.

### 6.1 send / publish / reply

허용:

- same-handle concurrent `zlink_send()`
- same-handle concurrent `zlink_gateway_send()`
- same-handle concurrent `zlink_gateway_send_rid()`
- same-handle concurrent `zlink_spot_node_publish()`
- same-handle concurrent `zlink_spot_publish()`
- same-handle concurrent `zlink_spot_pub_publish()`
- callback thread와 worker thread의 동시 send/publish/reply

보장:

- data corruption이 없어야 한다.
- deadlock이 없어야 한다.
- 성공 또는 명시적 에러(`EAGAIN` 등) 중 하나로 완결되어야 한다.
- 조용한 data loss나 중간 상태 노출은 허용하지 않는다.

### 6.2 send-ready handler

`*_set_send_ready_handler()`는 런타임에 교체 가능한 유일한 callback setter다.

대상 API:

- `zlink_socket_set_send_ready_handler()`
- `zlink_gateway_set_send_ready_handler()`
- `zlink_spot_node_set_send_ready_handler()`
- `zlink_spot_set_send_ready_handler()`
- `zlink_spot_pub_set_send_ready_handler()`

동시성 계약:

- send-ready handler 교체와 send path / writable transition dispatch는 동시에
  발생할 수 있다. 라이브러리가 data race 없이 직렬화한다.
- 교체가 성공하면 다음 writable transition부터 새 handler가 관찰된다.
- 이미 in-flight인 send-ready callback은 기존 handler로 완료될 수 있다.
- canonical interface review에 따라 replace-only surface다. `NULL` 제거는
  허용하지 않으며, `NULL`을 전달하면 `EINVAL`을 반환한다.

callback 중 재진입:

- send-ready callback 안에서 same-handle `send` / `publish`를 호출할 수 있다.
- send-ready callback 안에서 `*_set_send_ready_handler()`를 다시 호출하면
  `EDEADLK`를 반환한다 (재진입 금지).

close race:

- send-ready callback 실행 중 다른 thread에서 `close` / `destroy` 호출 시
  `EBUSY`를 반환한다 (9절 공통 정책).
- send-ready callback 안 self-close는 recv callback과 동일한 deferred teardown
  규칙을 따른다.

### 6.3 mutation / query / attach

허용:

- send-ready handler 교체와 send path / writable transition의 공존
- subscribe / unsubscribe와 publish의 공존
- option setter / query / peer query의 공존
- `attach_discovery()`, bind/connect, monitor open/close 같은 lifecycle-adjacent
  API의 thread-safe 직렬화

정의:

- 각 API는 per-handle 기준의 defined order를 가져야 한다.
- 호출이 겹치더라도 반쯤 적용된 subscription, 손상된
  topology cache가 보이면 안 된다.

참고: recv handler는 생성 시 고정이므로 교체 동시성을 고려할 필요가 없다.

### 6.4 visibility

send-ready handler 교체:

- `*_set_send_ready_handler()`가 성공하면 다음 writable transition부터
  새 handler가 관찰되어야 한다.
- 이미 in-flight인 send-ready callback은 기존 handler로 완료될 수 있다.

subscription mutation:

- `unsubscribe()` 반환 전에 이미 match/dispatch에 진입한 메시지는 기존 구독
  상태로 전달될 수 있다.
- `unsubscribe()` 반환 이후 새로 match에 진입한 메시지부터 새 상태를 본다.
- `subscribe()`도 동일하게 반환 이후 새로 match에 진입한 메시지부터 적용된다.

## 7. 락/동기화 설계

### 7.1 기본 전략

권장 구현은 아래 조합이다.

- per-handle mutex 또는 동등한 직렬화 primitive
- send-ready `(handler, subject)` 쌍의 원자적 publication (seqlock 또는 동등 메커니즘)
- admission state (closing bit + inflight count를 단일 원자적 단위로 관리)
- callback scope metadata (thread id + depth)
- parent-child reference guard

핵심은 "모든 것을 lock-free로 만들자"가 아니라
"public contract를 지키면서 lock hold 시간을 줄이자"다.

### 7.2 callback 중 send 허용

callback 안 send를 허용하려면 callback 호출 시 send path와 동일한 non-reentrant
락을 오래 쥔 채 진입하면 안 된다.

권장 패턴:

1. dispatch 진입 시 recv handler(고정)와 최소 분기 상태만 읽는다.
2. 필요하면 callback depth / in-flight를 올린다.
3. subject lock을 풀고 callback을 호출한다.
4. callback 안 send는 일반 send path가 다시 lock을 잡아 처리한다.

즉 "classification은 락 안에서 짧게, callback 실행은 가능한 한 락 밖에서"가
원칙이다.

### 7.3 send-ready handler publication

recv handler는 생성 시 고정이므로 atomic 보호가 필요 없다.

send-ready handler는 런타임에 교체 가능하며, 실제 callback ABI는
`(handler_fn, subject)` 쌍이다. 이 두 값을 분리된 atomic store/load로
다루면 교체 중 "새 handler + 옛 subject" 조합이 보일 수 있다.

publication 단위는 `(handler, subject)` 쌍 전체이며, 권장 메커니즘은
seqlock이다.

- setter (rare path):
  1. sequence counter를 홀수로 올린다 (writing 진입)
  2. handler와 subject를 store한다
  3. sequence counter를 짝수로 올린다 (writing 완료)
- dispatcher (hot path, writable transition):
  1. sequence를 load한다 (s1)
  2. handler와 subject를 load한다
  3. sequence를 다시 load한다 (s2)
  4. s1 != s2이거나 s1이 홀수면 retry한다 (setter와 정확히 겹칠 때만 발생)
- sequence counter는 `atomic<uint32_t>`, acquire/release ordering

이 방식의 성능 특성:
- dispatch hot path는 lock-free이며 retry 확률은 극히 낮다 (setter는 rare)
- setter에 mutex가 필요 없다
- 추가 heap allocation이 없다
- `op_state_mutex`와 독립적으로 동작하므로 send path lock과 간섭하지 않는다

대안으로 128-bit atomic(CMPXCHG16B), RCU-style slot swap도 가능하지만,
seqlock이 이 용도에서 복잡도 대비 성능이 가장 좋다.

### 7.4 내부 구현 세부사항은 공개 계약과 분리한다

현재 일부 subject가 `_use_lock` 또는 개별 `_sync`를 이미 가지고 있더라도,
그것은 구현 세부사항일 뿐이다.

- `_use_lock`를 public 옵션으로 승격하지 않는다.
- 기존 lock이 있으면 재사용하되, 공개 계약은 unconditional thread-safe다.
- 기존 lock이 없다면 raw socket / discovery / monitor 쪽에 공통 수명 보호와
  직렬화 계층을 추가한다.

### 7.5 현재 구현 기준 현실 점검

현재 구현은 "일부 subject는 operational path가 lock으로 보호되지만, 전체 public
surface가 같은 계약으로 정리된 상태는 아니다"라고 보는 것이 정확하다.

| Subject | 현재 상태 | 핵심 갭 |
|---|---|---|
| raw socket (`PAIR`/`DEALER`/`ROUTER`/`PUB`/`SUB`/`XSUB`/`XPUB`) | `socket_base_t` 공통 API 직렬화 계층이 없다 | same-handle concurrent `send` / option setter / close를 전면 보장하지 못한다 |
| raw `STREAM` | `_api_mutex`가 있어 일부 API는 보호된다 | 전체 raw socket 모델로 일반화되지 않았다 |
| `XSUB` / `XPUB` direct-dispatch | dispatch inflight / stop 대기는 있다 | public API 전체를 덮는 lifetime guard가 아니다 |
| `gateway` | `_sync` 기반으로 send path는 상당 부분 직렬화돼 있다 | close/destroy/in-flight 정책이 문서 수준의 공통 프로토콜로 정리되지 않았다 |
| `spot_pub` / `spot_sub` / `spot_node` | `_sync`와 callback inflight가 일부 있다 | parent-child close ordering, 공통 lifetime gate, raw socket 기반 API와의 일관성이 부족하다 |
| `discovery` / `registry` | `_sync`가 있고 observer/task state를 일부 보호한다 | bootstrap, bind, destroy, observer callback과 운영 API를 하나의 thread-safe contract로 고정하지 못했다 |
| monitor handle | callback depth / self-close defer가 구현돼 있다 | parent close 정책과 raw/service 공통 정책이 완전히 일치하지 않는다 |

구현 기준 주요 관찰:

- raw socket의 핵심 hot path인
  [`socket_base_t::setsockopt()`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp),
  [`socket_base_t::send()`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp)
  에는 공통 per-handle serialization 계층이 없다.
  (public `recv()`는 이미 제거되었으므로 thread-safe 설계 대상이 아니다.)
- `STREAM`만
  [`stream_t::_api_mutex`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/stream.cpp)
  를 통해 별도 보호를 제공한다. 즉 raw socket 전반의 완성된 모델이 아니라
  예외 구현이다.
- `gateway`, `spot_pub`, `spot_sub`, `spot_node`는 각각 `_sync`를 이용한
  subject-local serialization을 이미 사용 중이므로 raw socket보다 출발점이 좋다.
- 최근 SPOT shutdown race는
  [`core/src/core/object.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/object.cpp),
  [`core/src/core/pipe.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/pipe.cpp),
  [`core/src/core/mailbox.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/mailbox.cpp),
  [`core/src/core/yqueue.hpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/core/yqueue.hpp),
  [`core/src/engine/asio/asio_engine.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/engine/asio/asio_engine.cpp)
  축에서 터졌다. 따라서 thread-safe 확장은 "API lock 추가"만으로 끝나지 않고
  lifetime ordering까지 함께 닫아야 한다.

이 절의 결론은 단순하다.

- 목표 계약은 유지한다.
- 다만 "문서 선언 -> 구현 추정" 순서가 아니라 "공통 lifetime gate 구현 ->
  subject별 이식 -> 문서/테스트 고정" 순서로 진행해야 한다.

### 7.6 공통 thread-safe runtime 계층

thread-safe only 계약을 타협 없이 밀려면 subject별 lock 추가가 아니라 공통
runtime 계층을 먼저 정의해야 한다.

권장 구조:

1. admission state word (CAS 기반 단일 atomic)
   - `closing_requested`(1 bit)와 `inflight count`(나머지 bits)를 하나의
     `atomic<uint32_t>` 또는 동등한 단일 word로 합친다.
   - API 진입은 CAS로 inflight를 1 증가시키되, closing bit이 켜져 있으면
     즉시 실패한다. 이 두 판정이 같은 CAS 연산에서 원자적으로 처리된다.
   - API 이탈은 `fetch_sub(1, release)`로 inflight를 감소시킨다.
   - close는 CAS로 closing bit을 설정하되, inflight > 0이면 실패(`EBUSY`)한다.
   - 이 방식은 별도 `close_admission_mutex` 없이 admission과 close의 선형화를
     하드웨어 수준에서 보장한다.
   - 이미 in-flight인 callback thread의 same-handle send/publish는 callback scope
     metadata로 별도 판정하여 허용한다.
   - 구현은 packed state word 외에 cacheline 분리, 필요 시 sharded counter 등
     alternative implementation을 선택할 수 있다. 문서는 admission state의
     의미를 고정하되 자료구조를 강제하지 않는다.

   참고 pseudocode:
   ```
   // enter_public_api
   uint32_t old = state.load(acquire);
   do {
       if (old & CLOSING_BIT) return ESHUTDOWN;
   } while (!state.compare_exchange_weak(old, old + 1, acq_rel));

   // leave_public_api
   state.fetch_sub(1, release);

   // begin_close_or_fail_busy (external)
   uint32_t old = state.load(acquire);
   do {
       if (old & ~CLOSING_BIT) return EBUSY;  // inflight > 0
   } while (!state.compare_exchange_weak(old, old | CLOSING_BIT, acq_rel));
   // 성공: closing accepted, 새 API 진입 차단됨

   // self-close (callback 내)
   // closing bit + close_deferred 설정, inflight는 callback epilogue에서 감소
   ```

2. operation/state lock (`op_state_mutex`)
   - 상태 mutation과 snapshot 생성, 짧은 operation fast path 보호는 이 lock으로
     직렬화한다.
   - 단, user callback 실행 동안에는 이 lock을 들고 있으면 안 된다.
   - public `recv()`는 이미 제거되었으므로 이 lock 아래에서의 blocking wait 시나리오는
     존재하지 않는다. send의 `EAGAIN` 후 wait도 반드시 lock 밖에서 수행한다.
3. callback scope metadata
   - callback depth만으로는 부족하다.
   - callback thread 식별 정보와 self-close defer 플래그를 같이 기록해야 한다.
   - close가 수락된 뒤에는 새 callback scope 진입도 같은 gate 아래에서 막아야 한다.
   - 중요 invariant: **한 handle에는 동시에 최대 하나의 callback thread만 활성화된다.**
     recv callback 실행 중 같은 handle의 send-ready callback은 발생하지 않으며,
     그 역도 마찬가지다. monitor는 별도 handle이므로 이 invariant에 해당하지 않는다.
   - 이 invariant가 성립하므로 per-handle `callback_thread_id + callback_depth`는
     단일 슬롯으로 관리할 수 있으며, self-close 판정과 callback-중-send 허용 판정이
     신뢰할 수 있다.
   - 이 invariant가 architecture 변경으로 깨질 경우 per-callback token/guard 방식으로
     전환해야 한다.

중요한 구현 원칙:

- "큰 recursive mutex 하나로 send 전체를 감싼다"는 방식은 raw socket에 그대로
  적용하면 안 된다.
- 따라서 raw socket 계층은
  "CAS 기반 admission gate"와 "짧은 state serialization"을 분리해야 한다.
- thread-safe는 타협하지 않되, operational hot path는 가능한 한
  "atomic admission + 짧은 lane-local critical section"으로 끝나야 한다.
- close/destroy/config/handler 교체 같은 control-plane과,
  `send` / `publish` / callback dispatch 같은 data-plane은 같은 큰 mutex 하나로
  묶지 않는다.
- **`process_commands()`를 `op_state_mutex` 아래에서 full drain으로 수행하는 것은
  금지한다.** 현재 `process_commands()`는 mailbox를 전부 drain하는 가변 비용
  작업이며, heavy command(pipe_term 등) 하나가 lock hold time을 spike시켜
  same-handle concurrent send 전체의 tail latency를 찌를 수 있다.
  허용되는 구현:
  - `process_commands`를 `op_state_mutex` 밖에서 수행하고, 결과 상태만
    짧게 lock 안에서 반영하는 방식
  - `op_state_mutex` 안에서 수행하더라도 one-pass bounded budget으로 제한하되,
    heavy command를 만나면 lock을 풀고 defer하는 방식
  - 금지되는 구현: unbounded mailbox drain을 `op_state_mutex` hold 중 수행
- thread-safety 구현 때문에 새 heap allocation, message copy, condition wait를
  hot path에 추가해서는 안 된다.
- lock은 상태 snapshot과 transition 보호에만 쓰고, callback 실행은
  항상 lock 밖에서 수행한다.
- 즉 "thread-safe니까 느려도 된다"는 접근은 금지한다.

raw socket에 필요한 최소 helper는 다음과 같다.

- `socket_public_api_state_t` 또는 동등한 구조를
  [`socket_base_t`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.hpp)
  에 둔다.
- 필드:
  - `admission_state` — 단일 atomic word. closing bit + inflight count를 합친다.
    구현은 packed `uint32_t`, cacheline-isolated counter, 또는 sharded counter 중
    적합한 방식을 선택할 수 있다. 문서는 의미만 고정한다.
  - `callback_thread_id` — 현재 활성 callback의 thread ID (단일 슬롯,
    "한 handle 당 동시 callback thread 최대 1개" invariant 전제)
  - `callback_depth` — 중첩 callback 깊이
  - `op_state_mutex` — 상태 mutation / snapshot 보호용 짧은 lock
  - `close_deferred` — self-close 시 epilogue teardown 예약 flag
- helper:
  - `enter_public_api(kind, allow_from_callback)` — admission state CAS
  - `leave_public_api()` — admission state fetch_sub
  - `try_enter_callback_scope()` — callback_thread_id 설정 + depth 증가
  - `leave_callback_scope()` — depth 감소 + deferred close 확인
  - `begin_close_or_fail_busy()` — admission state CAS로 closing bit 설정 시도
  - `finish_close()` — teardown 진행

서비스 계열은 다음 방향이 적합하다.

- `gateway`, `spot_node`, `spot_pub`, `spot_sub`, `discovery`, `registry`는
  각자 `_sync`를 유지하되, public contract 경계는
  [`services/common`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/common)
  에 공통 helper를 만들어 맞춘다.
- 즉 `_sync`는 상태 보호용, 공통 helper는 API admission / close policy용으로
  역할을 분리한다.
- callback self-close 판별은 `thread-local depth only`가 아니라,
  per-handle `callback_thread_id + callback_depth`를 canonical source of truth로
  삼는다.
- thread-local depth는 진단/assert 또는 fast reject 보조 신호로만 사용할 수 있고,
  correctness 판정에 단독으로 쓰면 안 된다.

### 7.7 실제 적용 순서

이 문서를 구현으로 옮길 때의 권장 순서는 다음이다.

1. raw socket 공통 lifetime gate를 먼저 넣는다.
   - 이유: 모든 서비스가 결국 raw socket 위에 올라가기 때문이다.
   - 대상 파일:
     [`socket_base.hpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.hpp),
     [`socket_base.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp),
     [`zlink.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/api/zlink.cpp)
2. monitor path를 raw socket close policy와 맞춘다.
   - 현재 monitor는 별도 callback depth를 갖고 있으므로 공통 gate에 가장 먼저
     이식하기 좋다.
3. `gateway`를 공통 service API guard 위로 옮긴다.
   - send path가 이미 `_sync`를 쓰므로 적용 난이도가 가장 낮다.
4. `spot_pub` / `spot_sub` / `spot_node`를 공통 guard 위로 옮긴다.
   - parent-child ordering, callback inflight, direct handler path를 함께 정리한다.
5. `discovery` / `registry`를 마지막에 올린다.
   - observer callback, bootstrap/bind, background task와의 ordering을 같이
     정리해야 하므로 가장 어렵다.
6. 마지막에 문서와 테스트를 계약으로 고정한다.

이 순서를 뒤집지 않는 이유:

- raw socket이 먼저 안정되지 않으면 서비스 layer lock이 raw core race를 가릴 뿐
  해결하지 못한다.
- `discovery` / `registry`를 먼저 건드리면 observer/task ordering까지 한 번에
  터져 디버깅 난이도가 급격히 오른다.

## 8. subject별 상세 계획

### 8.0 공통 기반 작업

subject별 구현 전에 아래 공통 작업을 먼저 끝내야 한다.

- raw/socket 계층용 공통 API admission helper 추가
- service 계층용 공통 API guard helper 추가
- close/destroy 공통 `EBUSY` 프로토콜 추가
- callback scope metadata의 공통 표현 추가
- self-close deferred teardown의 공통 helper 추가

후보 위치:

- raw socket 공통 계층:
  [`core/src/sockets/socket_base.hpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.hpp),
  [`core/src/sockets/socket_base.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.cpp)
- service 공통 계층:
  [`core/src/services/common`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/services/common)
- public API 진입점:
  [`core/src/api/zlink.cpp`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/api/zlink.cpp)

### 8.1 raw socket

구현 목표:

- raw socket의 same-handle concurrent `send` 지원 (public `recv()`는 이미 제거됨)
- `zlink_socket_set_send_ready_handler()` 교체와 writable transition dispatch의
  동시성 보호 (`(handler, subject)` 쌍 seqlock)
- `zlink_stream_attach_raw()` / `zlink_stream_attach_len32be()` 같은 attach 교체와
  dispatch race 정의
- `zlink_close()`에 공통 `EBUSY` / self-close deferred teardown 정책 적용

현재 구현 관찰:

- `socket_base_t::send/setsockopt/getsockopt/close`는 같은 subject에서
  공통 API guard를 공유하지 않는다.
- `STREAM`만 `_api_mutex`가 있고 나머지 raw socket은 없다.
- `XSUB` / `XPUB`의 dispatch inflight는 callback stop 용도이지,
  same-handle public API 직렬화 계층이 아니다.

실제 작업 항목:

- [`socket_base_t`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/src/sockets/socket_base.hpp)
  에 공통 admission state (CAS state word)를 추가한다.
- `send()`는 아래 순서로 재구성한다.
  1. admission state CAS로 진입 허용 여부 검사
  2. `process_commands`는 `op_state_mutex` 밖에서 수행하거나,
     `op_state_mutex` 안이라면 one-pass bounded budget으로 제한한다.
     **unbounded mailbox drain을 `op_state_mutex` hold 중 수행하는 것은 금지한다.**
     heavy command(pipe_term 등)를 만나면 lock을 풀고 defer해야 한다.
  3. 짧은 `op_state_mutex` 안에서 `xsend` 한 번 수행
  4. `EAGAIN`이면 state lock을 풀고 wait (lock 밖)
  5. 재진입 시 다시 2로 복귀
- admission state CAS와 `op_state_mutex`는 역할이 분리된다.
  admission state는 closing/inflight 원자성을 보장하고,
  `op_state_mutex`는 짧은 state mutation만 보호한다.
- `setsockopt/getsockopt/bind/connect/monitor_open/send_ready_handler_setter`도
  같은 admission state CAS 아래 넣는다.
- `close()`는 admission state CAS로 closing bit 설정을 시도한다.
  inflight > 0이면 closing bit을 건드리지 않고 `EBUSY`를 반환한다.
  handle은 계속 live 상태로 남는다.
- `STREAM`의 `_api_mutex`는 raw 공통 gate가 올라온 뒤 `STREAM` 특수화가 정말
  필요한 부분만 남기고 정리한다.

### 8.2 discovery

구현 목표:

- topology cache update, subscribe/unsubscribe, service query, monitor open/close를
  thread-safe subject로 정리
- observer list와 representative routing id 변경을 data race 없이 처리
- topology ownership 규칙과 lifetime ordering을 검증
  (manual peer가 있으면 attach `EBUSY`, attach 후 manual connect/disconnect 금지)

현재 구현 관찰:

- `_sync`는 이미 넓게 쓰고 있다.
- 그러나 bootstrap/connect, observer callback, task wakeup, destroy가 공통
  close protocol 아래 있지는 않다.
- observer callback은 inflight count를 갖지만 raw/service 공통 표현과는 분리돼
  있다.

실제 작업 항목:

- `_sync`는 유지하되 public API admission helper를 추가한다.
- observer callback 실행은 `_sync` 안에서 대상 목록 snapshot만 만들고,
  실제 callback 호출은 락 밖에서 수행한다.
- destroy는 observer inflight와 public API inflight를 함께 본다.
- `connect_registry()` / bootstrap path는 "초기화 단계 API"로 분리하지 말고,
  문서 계약대로 thread-safe operational API로 승격한다. 단, raw socket lock을
  오래 쥔 채 bootstrap reply를 기다리면 안 된다.

### 8.3 gateway

구현 목표:

- existing `_sync` 재사용
- `send`, `send_rid`, send-ready handler 교체, monitor 경로를 동일 계약 아래 정리
- recv callback 안 `zlink_gateway_send_rid()` reply를 공식 허용 패턴으로 문서화

현재 구현 관찰:

- `_sync`를 통한 직렬화가 이미 넓게 들어가 있다.
- `_use_lock` 분기가 있지만 공개 계약은 unconditional thread-safe이므로
  이 분기를 contract 해석에 사용하면 안 된다.
- `destroy()`는 문서가 요구한 공통 close protocol보다 더 ad-hoc한 teardown
  순서를 가진다.

실제 작업 항목:

- `_use_lock`는 internal migration switch로만 두고, public contract 측면에서는
  항상 lock-enabled subject로 간주한다.
- send/send_rid/send-ready setter/monitor open을 공통 service API guard 아래
  묶는다.
- `destroy()`는 external 호출이면 먼저 inflight를 검사하고, in-flight > 0이면
  `EBUSY`로 거절한다. in-flight == 0일 때만 `closing_requested`를 설정하고
  teardown으로 진행한다.
- callback 안 `send_rid()`는 허용하되 `_sync`를 callback 호출 중 오래 쥐지
  않도록 dispatch path를 점검한다.

### 8.4 spot_node

구현 목표:

- publish, subscribe, unsubscribe, discovery attach, TLS 설정, peer connect/disconnect,
  child registry를 thread-safe subject로 정리
- node direct API와 child facade coordination을 같은 수명 보호 체계 아래 둔다

현재 구현 관찰:

- `_sync`가 매우 넓게 쓰이고 있어 상태 정합성 자체는 비교적 좋다.
- 반면 shutdown은 data-plane, control socket, discovery observer, child handle,
  runtime stop/join이 순차적으로 얽혀 있다.
- 최근 SPOT race는 child/runtime/core lifetime 경계가 아직 완전히 닫히지
  않았다는 뜻이다.

실제 작업 항목:

- `spot_node` 자체에 public API admission / close gate를 둔다.
- child registry(`spot`, `spot_pub`, `spot_sub`)와 parent destroy ordering을
  문서 정책대로 `EBUSY`로 강제한다.
- direct publish/subscribe path는 `_sync` snapshot 후 락 밖에서 callback 또는
  blocking 작업을 수행하도록 정리한다.
- data-plane stop/join 전후에 raw socket close gate가 이미 적용돼 있어야 한다.

### 8.5 unified `spot`

구현 목표:

- facade-level `publish`, `subscribe`, `unsubscribe`, peer query, send-ready handler 교체 보호
- callback 안 `zlink_spot_publish()` 허용
- child pub/sub attach 구조는 유지하되, thread-safety는 facade-level contract로
  설명

구조상 중요한 점:

- unified `spot`은 `spot_node`의 public handle alias가 아니다.
- 생성 시 child pub/sub를 별도로 만들고 node 내부 data-plane endpoint에 붙는다.
- 따라서 `spot`과 `spot_node`는 연결되어 있지만 같은 public handle 경쟁으로
  설명하지 않는 편이 정확하다.

현재 구현 관찰:

- unified facade는 child pub/sub를 합성한 형태라서 "한 handle의 thread-safe"와
  "child 두 개의 lifetime coordination"이 동시에 필요하다.
- facade-level contract를 지키려면 내부 child 두 개가 separate object라는
  사실을 외부에서 느끼지 않게 해야 한다.

실제 작업 항목:

- unified `spot` handle에도 별도 public API admission state를 둔다.
- facade API가 child pub/sub를 호출할 때 facade lock 안에서는 child ref snapshot과
  liveness 확인만 수행하고, 실제 child API 호출은 facade lock을 푼 뒤 시작한다.
- steady-state facade path에서는 facade lock과 child lock을 동시에 들지 않는다.
- lifecycle wiring이 필요한 경로의 lock ordering은
  `parent/facade registry -> child registry -> child local state -> raw socket op state`
  순서로 고정하고, 역순 획득은 금지한다.
- child destroy race는 parent-child registry로 막되, child callback/user callback은
  어떤 parent/child lock도 들지 않은 상태에서 실행한다.
- callback 안 publish는 facade guard가 허용하고, 실제 child pub send는 child의
  state lock으로 처리한다.

fast path 최적화 방향:

- 현재 코드는 child 생성이 lazy다. 이 상태에서는 facade lock으로 child ref를
  보호해야 하므로 steady-state publish에서도 facade lock + child lock = lock 2회가 필요하다.
- child를 eager 생성하고 ref를 불변으로 만들면 steady-state publish에서
  facade lock을 제거할 수 있다. child의 validity는 child 자체의 admission state
  CAS가 보장한다.
- 따라서 facade admission state CAS → child admission state CAS → child op_state_mutex
  → xsend의 경로에서 facade lock을 뺄 수 있다.
- 단, facade-level admission/liveness (facade 자체의 closing 판정)는 여전히 필요하며,
  이것은 facade의 admission state CAS로 처리한다 (lock이 아닌 atomic).
- eager child creation으로 전환 여부는 구현 시 확정하되, 문서는 이 방향을 열어둔다.

### 8.6 standalone `spot_pub` / `spot_sub`

구현 목표:

- 두 handle 모두 공통 thread-safe contract 대상에 포함
- `SpotPub`의 sync/async publish 실행 방식은 thread-safety 등급이 아니라
  내부 send semantics로 분리
- `SpotSub`의 subscription mutation visibility를 unified `spot`과 같은
  수준으로 고정

현재 구현 관찰:

- `spot_pub`는 publish path가 `_sync`로 감싸져 있어 출발점이 좋다.
- `spot_sub`는 `_sync`, direct-handler state, callback inflight가 이미 있다.
- 하지만 `destroy()`와 public API 공통 admission은 아직 한 체계로 묶이지 않았다.

실제 작업 항목:

- `spot_pub`는 publish/set_option/send-ready setter를 공통 service API guard
  아래 둔다.
- `spot_sub`는 subscribe/unsubscribe/set_option을
  공통 service API guard 아래 둔다. (public `recv()`는 이미 제거됨)
- `spot_sub`의 callback inflight는 service 공통 callback scope 표현으로
  승격한다.
- direct callback path는 공통 visibility 규칙을 따른다.

### 8.7 monitor handle

구현 목표:

- `zlink_socket_monitor_open()`
- `zlink_discovery_monitor_open()`
- `zlink_gateway_monitor_open()`
- `zlink_spot_monitor_open()`
- `zlink_spot_sub_monitor_open()`
- `zlink_spot_pub_monitor_open()`

위 handle들을 모두 독립 thread-safe child handle로 정리한다.

- monitor handle의 recv handler도 생성 시(`*_monitor_open()`) 고정이며 교체
  API가 없다.
- monitor callback과 parent subject API 사이에도 동일한 close/teardown 정책을
  적용한다.

현재 구현 관찰:

- monitor registry, worker thread, callback depth, self-close defer는 이미 상당 부분
  구현돼 있다.
- 그러나 raw socket/service parent의 close gate와 같은 구조는 아니다.

실제 작업 항목:

- monitor handle의 close policy를 raw/service 공통 close gate와 동일한 규칙으로
  맞춘다.
- parent가 살아 있는 동안 child monitor가 parent snapshot subject를 안전하게
  참조하도록 reference guard를 추가한다.
- `monitor_open()` 시점의 thread-safe contract와 parent `destroy`의 `EBUSY`
  정책을 테스트로 고정한다.

## 9. `close` / `destroy` 정책

이 항목은 thread-safe only 설계에서 가장 중요한 리스크다.

### 9.1 cross-thread close / destroy

기본 규칙:

- same-handle public API가 하나라도 in-flight면 cross-thread
  `close` / `destroy`는 `EBUSY`
- 대상에는 callback dispatch (recv / send-ready / monitor), `send`, `publish`,
  `*_set_send_ready_handler()`, subscribe/unsubscribe, option setter/query,
  monitor open/close, `attach_discovery`, bind/connect 계열이 포함된다

예:

- thread A가 `send()` 중일 때 thread B가 `zlink_close()` 호출 -> `EBUSY`
- callback 실행 중 다른 thread가 `zlink_spot_destroy()` 호출 -> `EBUSY`
- monitor callback 실행 중 다른 thread가 `zlink_service_monitor_close()` 호출 -> `EBUSY`

중요한 의미:

- 이 `EBUSY`는 "잠시 후 다시 자동 재시도하라"는 신호가 아니다.
- 정상 종료 경로는 caller가 ingress를 끊고 worker/callback source를 quiesce한 뒤
  단일 `close` / `destroy` 호출로 끝나야 한다.
- `EBUSY`가 반환된 경우 handle은 poison되지 않으며, in-flight 작업이 끝난 뒤에도
  계속 live subject로 남아 있어야 한다.

### 9.2 self-close

허용:

- callback 안에서 자기 자신의 handle 종료 요청

의미:

- 즉시 free가 아니다.
- `closing_requested`를 기록하고 callback return 뒤 dispatcher epilogue에서
  실제 teardown을 수행한다.

호출 형태:

- raw socket / raw monitor: `zlink_close(handle)`
- service object: `*_destroy(&handle_slot)`
- service monitor: `zlink_service_monitor_close(&handle_slot)`

추가 규칙:

- self-close 이후 callback 안에서 같은 handle을 다시 쓰는 것은 정의하지 않는다.
- self-close는 callback의 마지막 동작으로 보는 것이 맞다.

### 9.3 close 진입 원자성 프로토콜

`close` / `destroy`와 운영 API 사이의 race를 방지하기 위해 **CAS 기반 단일
admission state word**를 사용한다. 이 프로토콜은 별도 `close_admission_mutex`
없이 admission과 close의 선형화를 하드웨어 수준에서 보장한다.

admission state word 구조: `[closing bit | inflight count]`

1. external `close` / `destroy`:
   - admission state word를 CAS로 읽는다.
   - inflight count > 0이면 closing bit을 건드리지 않고 `EBUSY`를 반환한다.
     handle은 live 상태로 남으며, 새 운영 API 진입도 계속 허용된다.
   - inflight count == 0이면 CAS로 closing bit을 설정한다.
     CAS가 성공하면 close 수락이며, 이 시점부터 새 API 진입의 CAS가 실패한다.
     CAS가 실패하면 (다른 thread가 사이에 끼어들었으면) retry한다.
   - closing bit은 한 번 설정되면 취소되지 않는다.
2. self-close (callback 내):
   - callback epilogue가 teardown completion을 책임지므로
     closing bit과 `close_deferred`를 설정하고 즉시 성공 처리한다.
   - inflight는 callback epilogue에서 감소한다.
3. close가 수락된 뒤에는 teardown을 진행한다.

이 프로토콜이 해결하는 race:

- 별도 mutex + atomic counter 조합에서 발생하는 "closing 체크와 inflight 증가
  사이의 gap" 문제가 단일 CAS로 원자적으로 해결된다.
- 구현자가 hot-path mutex로 회귀하거나 shutdown race를 남길 여지가 없다.

핵심 정책:

- `EBUSY` 반환은 "close 요청 거절"이며, close intent를 latch하지 않는다.
- 따라서 외부 caller가 retry loop를 돌 필요가 없도록 설계한다.
- 정상 경로는 "quiesce -> 단일 close 성공"이다.
- `EBUSY`는 호출자가 shutdown precondition을 어겼다는 사실을 빠르게 드러내는
  용도다.

호출자 동기화 책임:

- 이 설계는 close-ready callback이나 별도 completion notification API를
  전제하지 않는다.
- in-flight 완료 확인은 caller가 소유한 worker join, callback source stop,
  event/semaphore/condition variable 같은 상위 구조에서 보장해야 한다.
- 향후 waitable asynchronous shutdown이 필요하면 별도
  `*_shutdown_begin()` / `*_wait_closed()` 류 API로 분리하는 것이 맞고,
  현재 `close` / `destroy` semantics에 retry를 내장하지 않는다.

callback 중 send 보호:

- `closing_requested` 상태에서도 이미 in-flight인 callback 안에서의
  same-handle `send` / `publish`는 허용한다.
- 판별 기준은 per-handle `callback_thread_id + callback_depth`다.
- thread-local callback depth는 다른 handle의 callback과 구분할 수 없으므로
  correctness 판정의 단독 근거로 사용하면 안 된다.
- 필요하다면 thread-local depth는 debug assert나 fast-path 힌트로만 사용한다.
- 이 규칙이 없으면 3.6절의 "callback 중 send 허용" 패턴이 close race에서
  깨진다.

이 프로토콜의 핵심은 `close 수락`과 `운영 API 진입 차단`을 같은 CAS 연산에서
원자적으로 처리하되, `거절된 close`가 handle을 좀비 상태로 만들지 않게 하는 것이다.

### 9.4 parent-child 종료 순서

parent subject(`spot_node`, 또는 monitor의 parent)를 종료할 때
살아있는 child handle이 있는 경우의 정책:

- child handle(unified `spot`, standalone `spot_pub`, standalone `spot_sub`,
  monitor handle)이 하나라도 열려 있으면 parent `destroy`는 `EBUSY`를 반환한다.
- 호출자는 child를 먼저 종료한 뒤 parent를 종료해야 한다.
- child callback 안에서 parent를 종료하는 것은 허용하지 않는다.

이 정책은 parent가 child를 암묵적으로 강제 teardown하는 것을 방지한다.
child의 수명은 호출자가 명시적으로 관리한다.

### 9.5 double close / double destroy

- `closing_requested`가 이미 설정된 handle에 대해 다시 `close` / `destroy`를
  호출하면:
  - in-flight > 0이면 `EBUSY`를 반환한다 (첫 번째 시도와 동일).
  - in-flight == 0이면 teardown을 진행한다.
- external `close`가 `EBUSY`로 거절된 경우에는 `closing_requested`가 설정되지
  않았으므로, 이후 호출은 "double close"가 아니라 새로운 종료 시도다.
- 이미 teardown이 완료된 handle에 대한 `close` / `destroy`는 정의하지 않는다
  (dangling handle 사용과 동일).
- self-close 후 같은 callback 내에서 다시 close를 호출하는 것은 정의하지 않는다.

### 9.6 호출자 권장 패턴

- 외부 thread에서 종료가 필요하면 먼저 worker와 callback source를 quiesce한다.
- 그 다음 `close` / `destroy`를 한 번 호출한다.
- 정상 설계에서는 application retry loop가 필요 없어야 한다.
- `EBUSY`를 받았다면 종료 절차를 너무 일찍 시작한 것이므로, busy-wait나
  blind retry 대신 caller 쪽 quiesce/join 순서를 바로잡아야 한다.
- 정말 예외적으로 다시 종료를 시도해야 한다면, 그것은 "looping retry"가 아니라
  in-flight source 정리가 끝난 뒤의 새로운 단일 종료 시도여야 한다.
- `while (close() == EBUSY) sleep(...)` 같은 busy-wait retry는 금지에 가깝게
  본다.
- child가 있는 parent를 종료할 때는 child를 먼저 종료한다.

## 10. 성능 관점

이 설계는 unconditional thread-safe를 전제로 하지만,
"정합성을 위해 성능 저하를 기본값으로 받아들인다"는 뜻은 아니다.

의미는 다음과 같다.

- thread-safe는 타협하지 않는다.
- 동시에 hot path에서는 불필요한 lock, allocation, copy, wakeup을 넣지 않는다.
- 최적화 포인트는 "lock을 끄는 선택지"가 아니라
  "admission을 싸게 만들고, lock scope를 짧게 만들고, control-plane과
  data-plane을 분리하는 구현"이다.

### 10.1 성능 비타협 원칙

다음 항목은 설계 원칙이 아니라 사실상 구현 제약이다.

- global lock 금지
- handle 간 공유 mutex 금지
- blocking `send` / poll wait 동안 mutex 보유 금지 (public `recv()`는 이미 제거됨)
- thread-safety를 위해 per-call heap allocation 추가 금지
- thread-safety를 위해 per-message memcpy 추가 금지
- control-plane mutation과 steady-state send/publish를 같은 mutex로 장시간 직렬화 금지
- callback dispatch 직전/직후의 snapshot 외에는 callback 동안 lock 보유 금지

즉 운영 중 steady-state throughput은 가능한 한 기존 hot path 구조를 유지하고,
thread-safe 구현 비용은 admission / close / mutation 경계로 밀어내야 한다.

### 10.2 raw socket fast path 원칙

raw socket은 성능 민감도가 가장 높으므로 다음 순서를 기준으로 설계한다.

1. API admission은 CAS 기반 state word로 처리한다. uncontended CAS 1회로 끝나야 한다.
2. `send`의 steady-state fast path에는 heap allocation이 없어야 한다.
3. `process_commands`는 `op_state_mutex` 밖에서 수행하거나, 안에서 수행하더라도
   bounded budget + heavy command defer로 lock hold time을 제한해야 한다.
   **unbounded mailbox drain을 send lock 아래에서 수행하는 것은 금지한다.**
4. state mutex가 필요하더라도 `xsend`의 짧은 구간만 보호해야 한다.
5. `EAGAIN` 후 wait는 반드시 lock 밖에서 수행한다.
6. option setter, bind/connect, monitor open/close는 hot path와 분리된
   control-plane 경로로 취급한다.

핵심은 same-handle concurrent 사용을 허용하되,
steady-state send가 lifecycle/config 변경이나 command backlog 때문에
장시간 막히지 않게 하는 것이다.

### 10.3 service hot path 원칙

`gateway`, `spot_pub`, unified `spot`, `spot_node` direct publish 경로는
기존 `_sync`가 있더라도 그대로 "큰 mutex 하나"가 되면 안 된다.

- `_sync`는 ownership, lifecycle, registry 보호에 사용한다.
- callback handler publication은 seqlock(7.3절)으로 `_sync`와 분리한다.
- steady-state send/publish는 가능하면 snapshot 이후 락 밖에서 진행한다.
- fanout 목록, peer 목록은 락 안에서 snapshot을 만들고,
  실제 전송/dispatch는 락 밖에서 수행한다.
- destroy/config mutation은 느려도 되지만 send/publish는 느려지면 안 된다.

snapshot 메커니즘 분류:

"snapshot 후 lock 밖 dispatch"와 "per-call heap allocation 금지"를 동시에
만족하려면 snapshot의 lifetime/cost model을 subject별로 명시해야 한다.

| Subject | snapshot 대상 | 예상 크기 | 권장 메커니즘 |
|---|---|---|---|
| raw socket | send target pipe(s) | 1-2개 | stack buffer (pointer 복사) |
| `gateway` | routing table lookup 결과 | 1개 | stack buffer |
| `spot_pub` | subscriber pipe list | 가변 (수십~수백) | epoch-based reclamation 또는 RCU |
| `spot_node` | child registry | 수 개~수십 | stack buffer (cap) 또는 epoch |
| `discovery` | observer list | 수 개 | stack buffer |
| unified `spot` | child pub/sub ref | 2개 (고정) | eager creation + immutable ref |

- **stack buffer**: 고정 크기 소수 대상. lock 안에서 pointer를 stack array에 복사하고
  lock을 풀고 사용한다. alloc 없음, copy 비용 = pointer 복사 수 개.
  fanout 상한이 필요하다.
- **epoch-based reclamation / RCU**: 가변 크기 다수 대상. writer가 새 목록을
  할당하고 atomic swap한 뒤 이전 목록을 epoch retire queue에 넣는다.
  reader는 epoch enter → atomic load(list_ptr) → 순회 → epoch exit.
  read path에 alloc 없음. write는 rare(control-plane mutation)이므로
  write 시 alloc은 허용된다.
- **immutable ref**: 생성 후 불변인 대상. lock 없이 직접 접근 가능.
  unified `spot`의 child ref가 eager creation으로 전환되면 이 범주에 해당한다.

이 메커니즘이 비어 있으면 구현은 hot-path heap copy 또는 long lock hold로
미끄러진다. 따라서 subject별 snapshot 방식을 구현 전에 확정해야 한다.

즉 서비스 계층의 목표는 "thread-safe한 관리 plane"과
"빠른 data plane"을 구조적으로 분리하는 것이다.

### 10.4 현재 계획에 대한 성능 리뷰

현재 문서 기준으로 특히 주의할 부분은 다음이다.

- raw socket에 공통 gate를 넣을 때 `send` 전체를 mutex로 감싸면 안 된다.
- `gateway` / `spot_*`에 기존 `_sync`만 재사용하면 control-plane lock이
  hot path lock으로 비대해질 수 있다.
- callback inflight 보호를 이유로 dispatch 전체를 락 아래 넣으면 tail latency가
  급격히 악화될 수 있다.
- close/destroy 정책은 보수적으로 가되, 그 비용을 steady-state send/publish에
  전파하면 안 된다.

추가 주의 사항:

- `process_commands()`의 full drain을 `op_state_mutex` 안에서 수행하면
  heavy command 하나가 same-handle concurrent send 전체의 tail latency를
  spike시킨다. 이것은 이 문서에서 **금지 수준**으로 다룬다. (7.6절, 10.2절 참조)
- query-heavy 경로(getsockopt, peer_query)를 write 경로와 분리하고 싶으면
  rwlock보다 **read-mostly immutable snapshot 또는 COW publication**을 우선
  후보로 둔다. `pthread_rwlock_t`는 writer starvation, 플랫폼 차이,
  uncontended cost 때문에 범용 기본값으로 두기엔 거칠다.
  rwlock은 profiling으로 read contention이 확인된 후 마지막 선택지로 남긴다.

그래서 구현 우선순위도 "락 추가"가 아니라 다음 순서가 맞다.

1. CAS 기반 admission state word (7.6절 프로토콜)
2. 짧은 상태 snapshot lock + subject별 snapshot 메커니즘 (10.3절 분류)
3. 락 밖 dispatch / callback
4. 마지막에 close/destroy slow path 정리

### 10.5 성능 수용 기준

thread-safe 구현이 끝났다고 해서 성능 검증이 끝난 것은 아니다.
아래 항목을 perf acceptance로 같이 본다.

same-handle 기준 (정확성 + low fixed overhead 목표):

- single-thread uncontended `send` / `publish` throughput이
  thread-safe 적용 전 대비 측정 가능한 정량 기준 이내여야 한다.
  reference host 기준 uncontended hot-loop send의 추가 비용이
  **절대값 50 ns 이하 또는 상대값 5% 이하** 중 하나를 만족해야 한다.
  이 수치는 초기 구현 후 baseline 측정으로 확정하되, 구조적 무너짐을 방지하는
  hard limit으로 사용한다.
- same-handle concurrent send는 scaling 목표가 아니라 정확성 + low overhead 목표다.
  `op_state_mutex`가 `xsend`를 직렬화하므로 sender 수를 올려도 same-handle
  throughput은 증가하지 않는다. 이것은 의도된 동작이다.
- same-handle concurrent send에서 sender 수 증가 시 throughput의 단조 감소는
  허용한다. 단, uncontended 대비 contended overhead가 과도하면 (예: lock 구현
  문제, false sharing) lock 설계를 다시 본다.

different-handle 기준 (scaling 목표):

- 서로 다른 handle의 concurrent send는 per-handle 직렬화이므로
  sender 수에 비례하여 total throughput이 증가해야 한다.
- global lock, handle 간 공유 mutex, 공유 atomic counter가 이 scaling을
  방해하면 실패로 본다.

공통 기준:

- `gateway` / `spot_pub` / unified `spot` / raw socket의 small message 경로에서
  lock contention으로 tail latency가 급증하면 실패로 본다
- close/destroy 보호를 넣은 뒤 steady-state benchmark에 추가 wakeup이나
  retry-like wait가 생기면 실패로 본다
- thread-safety 때문에 새 copy/alloc이 늘어난 경우, 그 이유가 lifecycle safety가
  아니라면 롤백 대상이다

권장 앱 패턴:

- callback에서는 최소 작업만 수행
- ownership move 또는 shallow copy 후 worker queue로 넘김
- 비즈니스 처리와 fanout send는 worker thread에서 수행
- 종료는 deterministic quiesce 후 수행

성능 측정 계획:

- thread-safe 적용 전후의 single-thread send/publish throughput을 perf benchmark로
  비교한다. admission state CAS의 uncontended 비용을 정량화한다.
  (reference host 기준 절대값/상대값 기록)
- same-handle concurrent send: thread 수를 1, 2, 4, 8로 변경하며 throughput과
  tail latency를 측정한다. `zlink_spot_pub_publish()`, `zlink_gateway_send()`,
  raw `zlink_send()` 경로를 각각 대상으로 한다. scaling은 기대하지 않되,
  uncontended 대비 contended overhead를 정량화한다.
- different-handle concurrent send: handle 수 × sender 수를 변경하며
  total throughput scaling을 측정한다. 선형에 가까운 scaling을 기대한다.
- recv callback 안 send 경로의 admission CAS + lock 재진입 오버헤드를 측정한다.
- `close` / `destroy` 가드가 들어간 뒤에도 steady-state run에서 extra allocation,
  extra copy, extra wait가 없는지 profiler/trace로 확인한다.
- service 계열은 control-plane mutation이 없는 steady-state 구간과,
  handler 교체/monitor open 같은 mutation이 섞인 구간을 분리 측정한다.
- `process_commands`가 `op_state_mutex` 아래에서 수행되는 경우
  command backlog 크기별 lock hold time을 측정한다.
- 기존 perf benchmark 인프라를 재사용하되, same-handle concurrent sender와
  different-handle scaling 시나리오를 추가한다.

## 11. 구현 항목

1. raw socket, `discovery`, `gateway`, `spot`, monitor에 공통 lifetime guard와
   per-handle serialization을 넣는다.
2. 기존 `_sync`가 있는 subject는 재사용하고, 없는 subject는 공통 보호 계층을
   추가한다.
3. send-ready handler 교체 visibility를 전 subject에 같은 규칙으로 맞춘다.
4. subscribe / unsubscribe visibility를 unified `spot`, `spot_sub`,
   `spot_node` direct API에 일관되게 맞춘다.
5. cross-thread `close` / `destroy`의 `EBUSY` 판단과 self-close deferred teardown을
   공통 정책으로 구현한다.
6. 선택적 동시성 모드를 전제한 기존 설계 문구와 테스트 항목을 걷어낸다.
7. 공개 문서와 header comment를 "thread-safe only contract" 기준으로 다시 쓴다.

실행 순서 고정:

1. `socket_base` 공통 gate 추가
2. `zlink.cpp` raw API 진입점과 monitor close path 이식
3. `gateway`
4. `spot_pub` / `spot_sub` / `spot_node` / unified `spot`
5. `discovery` / `registry`
6. 문서 / 테스트 / perf 회귀 검증

추가 원칙:

- mailbox를 다른 queue로 교체하는 것은 1차 해법이 아니다.
- 먼저 public contract에 필요한 lifetime / ordering / inflight semantics를
  구현해야 한다.
- queue 구현 교체는 그 이후 병목이 명확할 때만 별도 작업으로 분리한다.
- 공통 helper를 넣을 때도 hot path가 heap allocation이나 global contention을
  새로 만들면 안 된다.
- `_sync`가 이미 있는 subject는 무조건 재사용하는 것이 아니라,
  hot path와 control-plane을 분리할 수 있는지 먼저 검토한다.
- raw socket은 성능 기준선이므로, 첫 구현부터 uncontended fast path를 atomic
  중심으로 설계한다.

## 12. 테스트 계획

테스트는 "새 enum이 생겼는가"보다 "새 계약이 실제로 지켜지는가"를 확인하는 데
초점을 둔다.

### 12.1 생성과 기본 검증

- `zlink_socket(ctx, ZLINK_SOCKET_PUB, NULL)`은 send-only `PUB` 경로로 성공
- recv-capable raw socket + `NULL` handler는 실패
- `zlink_gateway_new(..., NULL)` 실패
- `zlink_spot_node_new(..., NULL)` 실패
- `zlink_spot_new(..., NULL)` 실패
- `zlink_spot_sub_new(node, NULL)` 실패
- `*_monitor_open(..., NULL)` 실패

### 12.2 concurrent send / publish

- same-handle concurrent `zlink_send()` 성공 또는 명시적 에러만 반환
- callback thread와 worker thread의 동시 send 성공
- `zlink_gateway_send_rid()` concurrent reply에 deadlock 없음
- `zlink_spot_node_publish()`, `zlink_spot_publish()`,
  `zlink_spot_pub_publish()` concurrent 호출에 data corruption 없음

### 12.3 send-ready handler

- `*_set_send_ready_handler()` 교체와 동시 send 호출에 data race 없음
- send-ready callback 안에서 same-handle `send` / `publish`에 deadlock 없음
- send-ready callback 안에서 `*_set_send_ready_handler()` 재진입 시 에러 반환
- send-ready callback 실행 중 다른 thread에서 `close` / `destroy` 호출 시 `EBUSY`
- send-ready callback 안 self-close는 deferred teardown으로 처리
- `NULL`을 전달하면 `EINVAL` 반환 (replace-only, no-remove)
- raw `SUB` / `XSUB`에 대한 `zlink_socket_set_send_ready_handler()` 호출은 `EINVAL`

### 12.4 visibility

- `*_set_send_ready_handler()` 교체 후 다음 writable transition부터 새 handler 적용
- 교체 시점에 이미 in-flight인 send-ready callback은 기존 handler로 완료 가능
- `unsubscribe()` 반환 이후 새 메시지부터 새 subscription 상태 적용
- `subscribe()` 반환 이후 새 메시지부터 새 구독 상태 적용
- 실패한 send-ready handler 교체는 기존 handler 유지

### 12.5 attach / query / monitor

- `attach_discovery()`와 query / option setter / peer query 동시 호출 시 data race 없음
- manual peer/route가 존재하는 상태에서 `attach_discovery()` 호출 시 `EBUSY`
- `attach_discovery()` 이후 manual `connect` / `disconnect` 호출 시 에러 반환
- monitor callback 중 parent API 호출에 deadlock 없음
- monitor open/close와 parent lifecycle API가 공통 `EBUSY` 정책을 따름

### 12.6 close / destroy

- in-flight callback 중 다른 thread에서 `close` / `destroy` -> `EBUSY`
- in-flight `send`, subscribe, option setter 중 다른 thread에서 종료 -> `EBUSY`
- external `EBUSY` 반환 후 `closing_requested`가 latch되지 않고 handle이 계속 live 상태로 유지됨
- external `EBUSY` 반환 뒤 in-flight 완료 후에도 새 운영 API 진입이 계속 가능함
- self-close 수락 후에는 `closing_requested` 상태에서 외부 thread의 새 운영 API 진입이 에러로 차단됨
- self-close 수락 후 in-flight callback 안 same-handle `send` / `publish`는 정상 동작
- self-close 요청은 즉시 성공하고 callback return 뒤 teardown 수행
- teardown 뒤 handle 재사용이 실패함을 확인
- child handle이 열려 있는 상태에서 parent `destroy` -> `EBUSY`
- child를 먼저 종료한 뒤 parent `destroy` 성공
- `closing_requested` 상태에서 double close 시 in-flight에 따라 `EBUSY` 또는 teardown

### 12.7 stress / sanitizer / fuzz

- ThreadSanitizer 빌드에서 unittest/integration/e2e와 thread-safe 관련 시나리오를
  전체 실행한다.
- same-handle concurrent `send` + cross-thread `close` 거절 경로를 수천~수만 회
  반복하는 stress test를 추가한다.
- send-ready handler 교체, subscribe/unsubscribe, monitor open/close,
  attach/query/destroy를 랜덤 순서로 섞는 deterministic-seed fuzz test를 추가한다.
- raw socket, `gateway`, `spot_pub`, `spot_sub`, unified `spot`, `discovery` 각각에
  대해 "callback 중 send + 외부 destroy 시도" race를 반복 실행한다.
- sanitizer/stress/fuzz는 hard timeout과 fail-fast 원칙을 유지한다.

테스트 구현 원칙:

- retry loop를 넣지 않는다.
- `sleep()` 기반 동기화를 쓰지 않는다.
- semaphore, condition variable, event flag와 hard timeout으로 in-flight 상태를
  강제한다.
- 단일 실패가 즉시 test executable 실패로 이어져야 한다.

## 13. 정책 회귀 테스트

### 13.1 ABI / header 검증

- public header에 `zlink_thread_mode_t` 같은 selectable thread mode type이 없어야 한다
- 생성자와 `*_monitor_open()`에 `thread_mode` 인자가 추가되지 않아야 한다
- `*_set_thread_mode()` 같은 API가 존재하지 않아야 한다
- recv handler 교체 API(`*_set_handler()`, `*_set_recv_handler()` 등)가 존재하지
  않아야 한다 — recv handler는 생성 시 고정이다
- `_use_lock` 등 내부 직렬화 상태를 public API로 노출하지 않아야 한다

### 13.2 런타임 계약 검증

- 모든 주요 handle이 same-handle concurrent send / mutation에서 data race 없이 동작
- close / destroy는 공통 `EBUSY` 정책을 유지
- self-close는 공통 deferred teardown 정책을 유지
- parent-child 생성과 attach에 동시성 모드 호환성 에러 케이스가 존재하지 않아야 한다

## 14. 공개 문서 반영 대상

이 설계가 구현되면 다음 문서도 같이 업데이트해야 한다.

- [`direct-callback-recv-interface-review.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-interface-review.ko.md)
  - 생성자 시그니처에 선택적 동시성 모드 확장 전제가 남아 있지 않도록 정리
  - thread-safe contract를 생성 옵션이 아니라 공통 공개 계약으로 명시
- [`direct-callback-recv-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-rewrite-spec.ko.md)
  - thread safety 섹션을 unconditional public contract 기준으로 정리
  - deferred teardown과 visibility 규칙을 본 문서와 맞춤
- [`zlink.h`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/include/zlink.h)
  - `spot_pub`의 "default thread-safe" 문구를 공통 규칙에 맞게 일반화
  - raw socket / gateway / spot / monitor 계열의 동시성 계약을 header comment에 반영

## 15. 최종 권장안

요약하면 권장안은 다음과 같다.

1. 선택적 동시성 모드 옵션을 도입하지 않는다. 모든 handle은 unconditional
   thread-safe다.
2. 현재 canonical 생성자와 monitor open 시그니처를 유지한다.
3. thread-safe 범위는 public contract로 문서에 먼저 고정하고, 구현은 그
   계약을 만족하는 메커니즘만 선택한다.
4. recv handler는 생성 시 고정이며 런타임 교체 API를 두지 않는다.
   public sync recv API는 이미 제거되었으며 이 문서의 scope에 포함하지 않는다.
5. `*_set_send_ready_handler()`는 유일한 런타임 교체 가능 callback setter이며,
   `(handler, subject)` 쌍을 seqlock 또는 동등한 메커니즘으로 원자적으로
   publish한다.
6. raw socket, `discovery`, `gateway`, `spot` 계열, monitor handle을 모두
   thread-safe contract로 통일한다.
7. 구현은 CAS 기반 admission state word와 per-handle operation lock을 중심으로
   정리한다. admission과 close의 선형화는 단일 atomic word CAS로 처리하며,
   별도 close_admission_mutex는 불필요하다.
8. `close` / `destroy`는 CAS로 closing bit 설정을 시도하되 inflight > 0이면
   `EBUSY`로 거절(closing bit 미설정, handle live 유지), accepted close에서만
   closing bit 설정, self-close deferred teardown, parent-child 순서,
   double close 규칙으로 보수적으로 묶는다.
9. `attach_discovery()`는 topology ownership 규칙(manual peer 존재 시 `EBUSY`,
   attach 후 manual connect/disconnect 금지)을 유지한다.
10. `process_commands()` full drain을 `op_state_mutex` 아래에서 수행하는 것은
    금지한다. command processing은 lock 밖에서 수행하거나, bounded budget +
    heavy command defer로 lock hold time을 제한한다.
11. same-handle concurrent send는 scaling 목표가 아니라 정확성 + low overhead
    목표다. 성능 수용 기준은 uncontended overhead budget(절대값/상대값)과
    different-handle scaling으로 정의한다.
12. snapshot 메커니즘은 subject별로 명시한다 (stack buffer / epoch-RCU /
    immutable ref). "snapshot 후 lock 밖 dispatch"와 "per-call alloc 금지"를
    동시에 만족하는 구체적 방식을 구현 전에 확정해야 한다.
13. 한 handle에는 동시에 최대 하나의 callback thread만 활성화된다는 invariant를
    유지한다. 이 invariant 위에서 per-handle `callback_thread_id + callback_depth`가
    self-close 판정과 callback-중-send 허용의 canonical source of truth로 동작한다.

한 줄 요약:

"모든 public handle은 thread-safe이고, 운영 API는 직렬화되며,
종료 API는 항상 가장 보수적으로 다룬다."
