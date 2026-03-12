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

- 각 handle은 자기 상태를 스스로 보호한다.
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
| raw socket | `zlink_socket(ctx, type, handler)` | recv-capable socket은 생성 즉시 thread-safe recv/send subject로 본다. raw `PUB`만 `handler == NULL` 경로를 유지한다. |
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
- atomic send-ready handler pointer 또는 동일 효과의 lock-minimized snapshot
- public API in-flight counter
- callback depth
- `closing_requested` / `closed` 상태 플래그
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

### 7.3 send-ready handler pointer

recv handler는 생성 시 고정이므로 atomic 보호가 필요 없다.

send-ready handler는 런타임에 교체 가능하므로 가능한 경우 atomic으로 다룬다.

- 교체는 store-release
- writable transition read는 load-acquire

이렇게 하면 writable transition fast path의 lock hold 시간을 줄이고
visibility semantics를 구현하기 쉬워진다.

### 7.4 내부 구현 세부사항은 공개 계약과 분리한다

현재 일부 subject가 `_use_lock` 또는 개별 `_sync`를 이미 가지고 있더라도,
그것은 구현 세부사항일 뿐이다.

- `_use_lock`를 public 옵션으로 승격하지 않는다.
- 기존 lock이 있으면 재사용하되, 공개 계약은 unconditional thread-safe다.
- 기존 lock이 없다면 raw socket / discovery / monitor 쪽에 공통 수명 보호와
  직렬화 계층을 추가한다.

## 8. subject별 상세 계획

### 8.1 raw socket

구현 목표:

- recv-capable raw socket의 same-handle concurrent `send` 지원
- `zlink_socket_set_send_ready_handler()` 교체와 writable transition dispatch의
  동시성 보호
- `zlink_stream_attach_raw()` / `zlink_stream_attach_len32be()` 같은 attach 교체와
  dispatch race 정의
- `zlink_close()`에 공통 `EBUSY` / self-close deferred teardown 정책 적용

### 8.2 discovery

구현 목표:

- topology cache update, subscribe/unsubscribe, service query, monitor open/close를
  thread-safe subject로 정리
- observer list와 representative routing id 변경을 data race 없이 처리
- topology ownership 규칙과 lifetime ordering을 검증
  (manual peer가 있으면 attach `EBUSY`, attach 후 manual connect/disconnect 금지)

### 8.3 gateway

구현 목표:

- existing `_sync` 재사용
- `send`, `send_rid`, send-ready handler 교체, monitor 경로를 동일 계약 아래 정리
- recv callback 안 `zlink_gateway_send_rid()` reply를 공식 허용 패턴으로 문서화

### 8.4 spot_node

구현 목표:

- publish, subscribe, unsubscribe, discovery attach, TLS 설정, peer connect/disconnect,
  child registry를 thread-safe subject로 정리
- node direct API와 child facade coordination을 같은 수명 보호 체계 아래 둔다

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

### 8.6 standalone `spot_pub` / `spot_sub`

구현 목표:

- 두 handle 모두 공통 thread-safe contract 대상에 포함
- `SpotPub`의 sync/async publish 실행 방식은 thread-safety 등급이 아니라
  내부 send semantics로 분리
- `SpotSub`의 subscription mutation visibility를 unified `spot`과 같은
  수준으로 고정

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

`close` / `destroy`와 운영 API 사이의 race를 방지하기 위해 다음 순서를 따른다.

1. `close` / `destroy` 진입 시 per-handle lock을 잡고 `closing_requested`를
   atomic하게 설정한다.
2. `closing_requested`가 설정되면 이후 **외부 thread에서의** 새 운영 API 진입은
   즉시 에러로 반환한다 (`ETERM` 또는 동등한 에러).
3. in-flight counter를 확인한다.
   - in-flight > 0이면 lock을 풀고 `EBUSY`를 반환한다. `closing_requested`는
     유지되므로 새 외부 API 진입은 이미 차단된 상태다.
   - in-flight == 0이면 teardown을 진행한다.
4. `EBUSY`를 받은 호출자는 in-flight 작업의 완료를 보장한 뒤 재시도한다.
   두 번째 시도에서는 in-flight == 0이 보장되므로 teardown이 진행된다.

callback 중 send 보호:

- `closing_requested` 상태에서도 이미 in-flight인 callback 안에서의
  same-handle `send` / `publish`는 허용한다.
- 판별 기준은 현재 thread가 callback thread인지 여부다. per-handle에 기록된
  callback thread ID와 현재 thread ID를 비교하거나, thread-local callback
  depth를 사용한다. per-handle callback depth 카운터만으로는 외부 thread가
  잘못 우회할 수 있으므로 thread 식별이 반드시 필요하다.
- 이 규칙이 없으면 3.6절의 "callback 중 send 허용" 패턴이 close race에서
  깨진다.

이 프로토콜의 핵심은 `closing_requested` 설정과 in-flight 확인 사이에
외부 thread의 새 운영 API가 진입할 수 없도록 하되,
이미 in-flight인 callback의 정상 동작은 보장하는 것이다.

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
- 이미 teardown이 완료된 handle에 대한 `close` / `destroy`는 정의하지 않는다
  (dangling handle 사용과 동일).
- self-close 후 같은 callback 내에서 다시 close를 호출하는 것은 정의하지 않는다.

### 9.6 호출자 권장 패턴

- 외부 thread에서 종료가 필요하면 먼저 worker와 callback source를 quiesce한다.
- 그 다음 `close` / `destroy`를 호출한다.
- `EBUSY`를 받으면 in-flight 작업이 끝났음을 보장한 뒤 재시도한다.
  `closing_requested`는 이미 설정되어 있으므로 외부 thread의 새 운영 API
  진입은 차단된 상태이며, 재시도 시 teardown이 진행된다.
  단, in-flight callback 안에서의 same-handle send/publish는 callback이
  끝날 때까지 계속 허용된다.
- `while (close() == EBUSY) sleep(...)` 같은 busy-wait retry는 권장하지 않는다.
- child가 있는 parent를 종료할 때는 child를 먼저 종료한다.

## 10. 성능 관점

이 설계는 공개 계약을 단순하게 만드는 대신,
unconditional thread-safe이므로 lock 비활성화 옵션을 public API로 드러내지 않는다.

의미는 다음과 같다.

- per-handle serialization 비용은 받아들인다.
- 대신 recv callback-only 구조, lock hold 최소화, snapshot 기반 dispatch로
  실효 비용을 낮춘다.
- 최적화 포인트는 "lock을 끄는 선택지"가 아니라 "lock을 짧게 잡는 구현"이다.

권장 앱 패턴:

- callback에서는 최소 작업만 수행
- ownership move 또는 shallow copy 후 worker queue로 넘김
- 비즈니스 처리와 fanout send는 worker thread에서 수행
- 종료는 deterministic quiesce 후 수행

성능 측정 계획:

- thread-safe 적용 전후의 single-thread send/recv throughput을 perf benchmark로
  비교한다. per-handle lock의 uncontended 비용을 정량화한다.
- concurrent send thread 수를 1, 2, 4, 8로 변경하며 throughput 변화를 측정한다.
  `zlink_spot_pub_publish()`, `zlink_gateway_send()`, raw `zlink_send()` 경로를
  각각 대상으로 한다.
- recv callback 안 send 경로의 lock 재진입 오버헤드를 측정한다.
- 기존 perf benchmark 인프라를 재사용하되, concurrent sender 시나리오를 추가한다.

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
- `EBUSY` 반환 후 `closing_requested` 상태에서 외부 thread의 새 운영 API 진입이 에러로 차단됨
- `EBUSY` 반환 후 in-flight callback 안 same-handle `send` / `publish`는 정상 동작
- `EBUSY` 반환 후 in-flight 완료 뒤 재시도하면 teardown 성공
- self-close 요청은 즉시 성공하고 callback return 뒤 teardown 수행
- teardown 뒤 handle 재사용이 실패함을 확인
- child handle이 열려 있는 상태에서 parent `destroy` -> `EBUSY`
- child를 먼저 종료한 뒤 parent `destroy` 성공
- `closing_requested` 상태에서 double close 시 in-flight에 따라 `EBUSY` 또는 teardown

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
5. `*_set_send_ready_handler()`는 유일한 런타임 교체 가능 callback setter이며,
   동시성 계약 범위에 포함한다.
6. raw socket, `discovery`, `gateway`, `spot` 계열, monitor handle을 모두
   thread-safe contract로 통일한다.
7. 구현은 per-handle serialization과 공통 lifetime guard를 중심으로 정리한다.
8. `close` / `destroy`는 `closing_requested` 원자 설정, `EBUSY`,
   deferred teardown, parent-child 순서, double close 규칙으로 보수적으로 묶는다.
9. `attach_discovery()`는 topology ownership 규칙(manual peer 존재 시 `EBUSY`,
   attach 후 manual connect/disconnect 금지)을 유지한다.
10. 성능 최적화는 no-lock 분기가 아니라 lock scope 축소와 dispatch snapshot으로
   해결하며, 적용 전후 throughput을 perf benchmark로 검증한다.

한 줄 요약:

"모든 public handle은 thread-safe이고, 운영 API는 직렬화되며,
종료 API는 항상 가장 보수적으로 다룬다."
