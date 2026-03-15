# Thread-Safe Socket / Service 설계 계획

> 상태 메모
> 이 문서는 raw socket, `gateway`, `spot`, `discovery`, monitor handle을
> 모두 포함하는 thread-safe 공개 계약 재정렬안이다.
> 목표는 "모든 operational API를 같은 강도의 thread-safe contract로 묶는 것"이
> 아니라, 성능 우선 `data-plane`을 중심에 둔 3계층 계약을 canonical contract로
> 고정하는 것이다.
> public selectable thread mode는 두지 않는다.
> recv/direct/send-ready 관련 public surface는 유지하며, 이번 재작성은 API 삭제나
> 생성 시 고정 정책 도입이 아니라 보장 강도의 재배치다.

## 1. 목적

이 문서는 현재 진행 중인
[`direct-callback-recv-interface-review.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-interface-review.ko.md)
와
[`direct-callback-recv-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-rewrite-spec.ko.md)
를 전제로, raw socket / `gateway` / `spot` / `discovery` / monitor handle의
공개 동시성 계약을 성능 우선 관점으로 다시 고정한다.

이 문서의 목표는 세 가지다.

- `send` / `publish` / `send_rid` hot path에 대해 same-handle concurrent 사용을
  외부 직렬화 없이 허용한다.
- `bind/connect/disconnect`, option 변경, `attach_discovery`, monitor open/close,
  heavy `query/snapshot` 계열은 문서 범위에 남기되 `low-frequency serialized
  contract`로 내린다. lightweight runtime read는 이 계층의 비용 모델을 강제하지
  않는다.
- `close` / `destroy`는 별도 `lifecycle strict` 규칙으로 fail-fast errno와 진입
  경계를 고정한다.

추가 설계 원칙:

- hot path를 오염시키지 않고 큰 비용 없이 thread-safe하게 만들 수 있는
  low-frequency handle / query / monitor / utility API는 가능한 한 기본
  thread-safe 범위로 끌어올린다.
- 최종적으로 남기는 비-thread-safe 예외는 비용 대비 이득이 낮거나 실행 모델이
  본질적으로 다른 surface로 제한한다.

이 문서가 만들고자 하는 결과는 하나로 요약된다.

- 사용자가 같은 handle을 여러 thread에서 써도 되는지, 된다면 어떤 API가 어떤
  비용 모델과 errno 규칙을 가지는지, 문서와 테스트만 보고 바로 판단할 수 있는
  canonical public contract를 만든다.

이 문서가 해결하려는 문제는 다음과 같다.

- "모든 operational API를 같은 강도의 thread-safe로 보장한다"는 목표는
  성능 요구와 구현 복잡도를 동시에 악화시킨다.
- 반대로 계약을 너무 줄이면 runtime control-plane API와 기존 public surface의
  의미가 흔들린다.
- 따라서 hot path, control path, lifecycle을 같은 층위로 다루지 않고, 서로 다른
  강도와 비용 모델을 가진 계약으로 명시해야 한다.

이번 문서의 1차 성공 기준은 아래와 같다.

- same-handle concurrent `send` / `publish` / `send_rid`가 문서 중심 목표로
  승격되어 있을 것
- control-plane API가 범위 밖으로 밀리지 않고 serialized contract로 남아 있을 것
- `close` / `destroy`의 fail-fast 규칙과 `ESHUTDOWN` 진입 경계가 분명할 것
- 성능 acceptance가 문서 전반의 기준으로 연결되어 있을 것

operational 수준 검토 체크리스트는 14절에서 별도로 정의한다.

이번 문서가 하지 않는 일도 초반에 고정한다.

- public API를 삭제하거나, runtime API를 초기화 단계 전용으로 재정의하지 않는다.
- handler 정책을 생성 시 고정으로 바꾸지 않는다.
- 구현 세부 메커니즘 자체를 canonical contract로 승격하지 않는다.

이 문서는 구현 설명서가 아니라 목표 공개 계약 설계안이다. 구현은 이 문서가
정한 계약을 만족해야 하고, 현재 구현의 달성 여부는 후속 구현/검증 단계에서
측정한다.

### 1.1 사용자 관점 한 줄 계약

이 문서가 최종적으로 만들고자 하는 사용자 관점 계약은 복잡한 예외표가 아니라
아래 세 문장이다.

- 공개 `socket` / `service` handle API는 기본적으로 thread-safe다.
- 사용자는 같은 handle을 여러 thread에서 공유해도 되며, `send` / `publish`는
  same-handle concurrent 사용을 허용한다.
- 예외는 소수만 남긴다. 초기 설정 전용 API, callback context에서 금지된 일부
  API, same-message object 공유, proxy 실행 모델은 기본 허용 범위 밖이다.

즉 외부에 전달할 기본 메시지는 "thread-safe인가 아닌가를 API마다 새로 추측하게
하지 않는다"다. 문서는 기본 허용을 먼저 말하고, 예외만 명시해야 한다.

## 2. 기준 문서와 용어

직접 기준은 다음이다.

- 인터페이스 기준:
  [`direct-callback-recv-interface-review.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-interface-review.ko.md)
- recv/callback 실행 모델 기준:
  [`direct-callback-recv-rewrite-spec.ko.md`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/doc/plan/direct-callback-recv/direct-callback-recv-rewrite-spec.ko.md)
- 현재 공개 헤더 기준:
  [`zlink.h`](/home/hep7/project/kairos/zlink-direct-callback-rewrite/core/include/zlink.h)

우선순위는 다음과 같다.

1. canonical public 함수 이름과 시그니처는 interface review와 `zlink.h`를 따른다.
2. callback-only recv, ownership, deferred teardown 규칙은 rewrite spec을 따른다.
3. 이 문서는 위 인터페이스 위에서 thread-safe contract의 강도와 적용 범위를
   추가 정의한다.

자주 쓰는 용어:

- same-handle:
  같은 public handle 값 하나를 여러 thread가 공유해서 사용하는 경우.
- hot path:
  steady-state `send` / `publish` / `send_rid`와 허용된 callback 중 same-handle
  `send` / `publish` 경로.
- control path:
  option 변경, `bind/connect/disconnect`, `attach_discovery`, heavy
  `query/snapshot`, `monitor_open/close`, `set_send_ready_handler` 같은 저빈도
  운영 경로. lightweight runtime read(`RCVMORE`, `EVENTS`, `LAST_ENDPOINT` 등)는
  이 경로의 lightweight subset이다(6.3절 참조).
- lifecycle:
  `close` / `destroy`와 그 admission/종료 의미를 정의하는 경로.
- admitted call:
  lifecycle gate를 통과해 실행이 시작된 호출.
- no-latch:
  failed close(`EBUSY`)가 handle 상태를 closing으로 영구 전이시키지 않는 속성.
  즉 `EBUSY` 반환 후 handle은 이전 operational state로 완전히 복귀한다.
- in-flight API:
  admission gate를 통과하여 실행이 시작됐고 아직 return하지 않은 호출.
  gate 진입 직전 대기 중인 호출이나 send queue에 enqueue만 된 메시지는
  in-flight에 해당하지 않는다.
- callback thread:
  recv callback, send-ready callback, monitor callback이 실제로 실행되는 thread.
- worker thread:
  callback thread가 아닌 일반 작업 thread.
- internal child:
  public contract의 직접 대상은 아니지만 parent/facade 구현에 사용되는 내부
  handle. 예: unified `spot` 내부 pub/sub child.

## 3. 설계 요약

### 3.1 public selectable thread mode는 두지 않는다

이 계획에서는 thread mode를 public surface에서 제거한다.

- `zlink_thread_mode_t` 같은 enum을 도입하지 않는다.
- 생성자나 `*_monitor_open()`에 `thread_mode` 인자를 추가하지 않는다.
- stronger/weaker mode 조합 매트릭스를 두지 않는다.
- thread-safety는 선택 옵션이 아니라 기본 공개 계약이다.

즉 내부 설계는 계약 계층으로 정리하되, 사용자에게는 "기본적으로
thread-safe, 예외만 명시" 형태로 보여야 한다. 계층 분류는 구현/문서 작성자가
일관성을 유지하기 위한 내부 정리 수단이지, 사용자가 API마다 별도 분류를
외우게 만드는 목적이 아니다.

### 3.2 canonical contract는 3계층이다

| 계층 | 대상 API | ordering semantics | 성능 목표 | 핵심 errno / lifecycle |
| --- | --- | --- | --- | --- |
| `Hot path guaranteed` | `send`, `publish`, `send_rid`, 허용된 callback 중 same-handle `send/publish` | 외부 직렬화 없이 concurrent 허용. 단일 thread 내 sequential send는 호출 순서가 보장된다. 서로 다른 thread 간 concurrent send의 message ordering은 내부 직렬화 순서를 따르며, thread 간 호출 시작 순서를 보장하지 않는다 | 최우선 | close accepted 이후 새 진입은 `ESHUTDOWN` |
| `Control path serialized / low-frequency` | `bind/connect/disconnect`, option 변경, `attach_discovery`, heavy `query/snapshot`, `monitor_open/close`, `set_send_ready_handler` | same-handle concurrent control-path 호출은 correctness를 보장하되, 실행 순서는 내부 직렬화에 따라 결정될 수 있다 | correctness 우선, hot path 저해 금지 | close accepted 이후 새 진입은 `ESHUTDOWN` |
| `Lifecycle strict` | `close`, `destroy` | admission gate 중심으로 선형화 | steady-state hot path 오염 금지 | in-flight external close/destroy는 `EBUSY`, accepted close 이후 새 진입은 `ESHUTDOWN`, failed close는 no-latch, double close는 `EALREADY` |

이 문서에서 `thread-safe`는 단일 강도 용어가 아니다. 각 public API는 반드시
위 세 계층 중 하나에 속해야 하며, 문서와 테스트는 그 계층을 기준으로 계약을
검증한다. lightweight runtime read는 별도 계층이 아니라 control path의
lightweight subset으로 취급하되, heavy query/snapshot과 같은 직렬화 비용
모델을 강제하지 않는다(6.3절 참조).

사용자에게는 이 3계층을 다음처럼 축약해서 설명한다.

- 기본값:
  공개 handle API는 thread-safe다.
- 고빈도 운영 경로:
  `send` / `publish` / `send_rid`는 same-handle concurrent 사용을 허용한다.
- 저빈도 운영 경로:
  connect, subscribe, monitor, heavy query, option 변경은 thread-safe하되
  correctness 우선 직렬화 계층으로 다룬다.
- 종료 경로:
  `close` / `destroy`는 fail-fast lifecycle gate를 가진다.

추가 메모:

- `RCVMORE`, `EVENTS`, `LAST_ENDPOINT`, 일부 `routing_id`처럼 자주 읽히는
  lightweight runtime read는 heavy `query/snapshot`과 같은 비용 모델로 묶지
  않는다.
- 즉 사용자에게는 "runtime read도 기본 허용 범위 안"으로 설명하고, 내부적으로만
  lightweight read와 heavy query를 구분한다.

즉 3계층은 내부 정리 방식이지, 사용자에게 API별 금지 목록을 길게 외우게 만드는
도구가 아니다.

### 3.3 범위는 유지하고 강도만 재배치한다

범위에 포함되는 subject:

- raw socket
- `discovery`
- `registry`
- `gateway`
- `spot_node`
- unified `spot`
- socket/service monitor handle

`spot_pub` / `spot_sub` 같은 internal child는 public contract의 직접 대상이
아니며, parent/facade contract를 만족시키기 위한 구현 단위로 취급한다.
`registry`는 hot path subject가 아니라 `discovery`와 함께 움직이는
control-plane 중심 subject로 취급한다.

유지하는 원칙:

- public API surface는 흔들지 않는다.
- runtime control-plane API는 범위에 남긴다.
- `set_send_ready_handler()` 계열은 유지한다.
- `discovery`와 `monitor`는 문서 범위에 남기되 `control-plane 중심 subject`로
  적극 정의한다.

이번 재작성에서 하지 않는 일:

- control-plane API를 "초기화 단계 전용"으로 내리지 않는다.
- `send-ready setter`를 제거하지 않는다.
- 모든 handler를 생성 시 고정으로 바꾸지 않는다.

## 4. 계약 계층별 정의

### 4.1 Hot path guaranteed

`Hot path guaranteed`는 다음을 뜻한다.

- same-handle concurrent `send` / `publish` / `send_rid`를 외부 직렬화 없이
  허용한다.
- 단일 thread 내 sequential send는 호출 순서가 보장된다. 서로 다른 thread 간
  concurrent send의 message ordering은 내부 직렬화 순서를 따르며, thread 간
  호출 시작 순서를 보장하지 않는다.
- 허용된 callback 안 same-handle `send` / `publish`를 공식 패턴으로 유지한다.
- callback thread에서의 same-handle send와 worker thread에서의 concurrent
  send가 동시에 발생하는 경우도 same-handle concurrent send로 취급하며,
  동일한 hot path 계약이 적용된다.
- 가능한 경우 hot path thread-safety는 shared broad lock이 아니라 기존
  send queue를 재사용하거나 동등한 low-overhead publication 경로를 통해
  달성한다.
- steady-state 경로는 최소 원자 연산, 짧은 critical section, 혹은 동등한
  저비용 메커니즘만 허용한다.
- hot path에 broad lock, retry wait, per-call allocation, 불필요한 wakeup을
  끌어들이지 않는다.
- concurrent send가 내부 backpressure에 도달한 경우의 errno와 blocking 의미는
  각 subject의 기존 send 계약(rewrite spec)을 따른다. 이 문서는 backpressure
  정책 자체를 재정의하지 않는다.

이 계층은 성능 우선 subject다. correctness만 맞추는 수준이 아니라,
single-thread small message 비용과 multi-thread contention 비용을 함께
통제해야 한다.

### 4.2 Control path serialized / low-frequency

이 계층에 속하는 API는 다음 성격을 가진다.

- runtime 사용 가능하다.
- same-handle concurrent control-path 호출은 correctness를 보장하되, 실행
  순서는 내부 직렬화에 따라 결정될 수 있다.
- heavy query/snapshot과 mutation성 setter는 이 계층의 대표 대상이다.
- lightweight runtime read는 기본 허용 범위 안에 남기되, 반드시 이 계층의
  가장 무거운 직렬화 비용 모델을 따를 필요는 없다.
- 성공 반환한 control-path 호출의 효과는 그 이후 admission된 호출에서 관측
  가능해야 한다.
- 동시에 들어온 control-path 호출의 linearization order는 내부 직렬화 순서로
  정해지며, 호출 시작 순서나 thread scheduling 순서를 보장하지 않는다.
- data-plane과 동시 호출될 수 있지만, 비용 목표는 hot path와 다르다.
- 내부 직렬화, 짧은 serialization lane, control-plane 전용 lock은 허용된다.
- 다만 그 직렬화는 hot path state/cacheline/lock과 분리되어야 한다.

이 계층은 `thread-safe 보장은 유지하되 hot path와 같은 비용 모델을 요구하지
않는` 계층이다. 문서에서 제외하거나 약속을 없애는 계층이 아니다.

### 4.3 Lifecycle strict

`close` / `destroy`는 hot path나 control path와 다른 규칙을 가진다.

- in-flight external `close` / `destroy`는 fail-fast `EBUSY`다.
- close가 accepted된 이후 새 API 진입은 data-plane/control-plane 구분 없이
  `ESHUTDOWN`이다.
- close accepted 전에 이미 admission된 호출은 각 API 계약에 따라 정상 완료
  또는 canonical errno로 종료될 수 있다.
- failed close는 no-latch다. 즉 `EBUSY`만으로 closing state를 영구 latch하면
  안 된다.
- double close / destroy는 `EALREADY`다.

이 계층의 목적은 "종료도 thread-safe이므로 아무 때나 섞어도 된다"가 아니라,
"종료는 별도 strict gate로 선형화하고 실패 시 즉시 드러낸다"를 공개 계약으로
고정하는 것이다.

## 5. 공통 공개 계약

### 5.1 문서가 보장하는 것

- 공개 `socket` / `service` handle API는 기본적으로 thread-safe다.
- same-handle concurrent `send` / `publish` / `send_rid`
- 허용된 callback 안 same-handle `send` / `publish`
- same-handle concurrent control-path 호출에 대한 correctness
- lightweight runtime read (`RCVMORE`, `EVENTS`, `LAST_ENDPOINT`, 일부
  `routing_id`)의 기본 허용
- data-plane과 control-path가 섞인 경우의 canonical lifecycle/ordering 의미
- external `close` / `destroy`의 fail-fast 규칙

사용자 안내 문구는 위 항목을 다시 다음 네 줄로 압축한다.

1. handle은 여러 thread에서 공유해도 된다.
2. `send` / `publish`는 same-handle concurrent 사용이 가능하다.
3. connect, subscribe, option 변경, monitor 같은 운영 API도 runtime에 호출할 수 있다.
4. 종료는 fail-fast이며 accepted 이후 새 진입은 `ESHUTDOWN`이다.

### 5.2 문서가 보장하지 않는 것

- 모든 public API를 같은 비용 모델로 처리하는 것
- same-handle concurrent control-path 호출의 사용자 의도 순서를 그대로 보장하는 것
- 서로 다른 handle 사이의 전역 fairness 또는 ordering
- 사용자 callback 내부 상태까지 라이브러리가 대신 동기화하는 것
- lock-free 자체를 공개 계약으로 보장하는 것

아래 항목은 기본 thread-safe 허용 범위 밖의 소수 예외로 명시한다.

- 초기 설정 시점 제약이 있는 API
  예: first bind/connect/query 전에만 의미가 있는 routing id 설정, registry
  초기 설정 계열
- callback context에서만 금지되는 reentrant API
  일반적인 handle thread-safe 예외라기보다, 특정 callback 실행 맥락 안에서만
  추가 제약이 붙는 규칙이다. 예: raw STREAM callback 안 `close`, send-ready
  callback 안 handler 교체
- same-message object 공유
  handle thread-safety와 별도로, 동일한 `zlink_msg_t` 인스턴스를 여러 thread가
  동시에 조작하는 사용은 기본 계약이 아니다
- proxy 실행 모델
  `zlink_proxy` / `zlink_proxy_steerable`는 일반 handle API처럼 "같은 소켓에 여러
  스레드가 operational API를 섞어 호출"하는 모델이 아니다. proxy가 실행되는
  동안 `frontend` / `backend` / `capture` / `control` 소켓은 proxy forwarding
  loop가 사실상 전용으로 점유하며, 다른 스레드가 그 소켓들에 일반 operational
  API를 섞어 호출하는 사용은 기본 계약으로 보지 않는다

다만 위 예외 목록은 가능한 한 작게 유지한다.

- `registry query client`, monitor handle처럼 hot path가 아니고 큰 비용 없이
  thread-safe하게 만들 수 있는 저빈도 surface는 최종 예외 목록에 남기지 않는
  방향을 기본 원칙으로 둔다.
- 즉 "예외를 상세 분류로 오래 유지"하는 것보다, "싸게 올릴 수 있는 것은 기본
  thread-safe 범위로 편입하고 진짜 예외만 남긴다"를 우선한다.

### 5.3 recv/direct/send-ready handler 정책

이 재작성은 handler 관련 public surface를 바꾸지 않는다.

- recv/direct handler는 기존 public surface와 rewrite spec을 따른다.
- recv/direct handler는 hot path 계약의 핵심 subject가 아니다.
- `set_send_ready_handler()`는 유지하되 `Control path serialized / low-frequency`
  계층으로 분류한다.

최소 유지 계약:

- `(handler, subject)` pair는 항상 일관된 쌍으로 관측되어야 한다.
- setter와 dispatch가 경합하면 dispatch는 기존 pair 또는 새 pair 중 하나를
  관측할 수 있다.
- 문서와 header에 이미 존재하는 reentrant setter 정책이 있다면 그대로 보존한다.
- send-ready visibility와 setter-dispatch 세부 ordering은 후속 확장 규칙으로
  분리할 수 있지만, setter 자체를 없애거나 생성 시 고정 정책으로 바꾸지 않는다.

## 6. 계층별 API 분류

### 6.1 Hot path guaranteed

- raw socket `send`
- `gateway_send`
- `gateway_send_rid`
- `spot_publish`
- `spot_node`를 통해 노출되는 steady-state publish/send 경로
- 허용된 recv/direct/send-ready callback 중 same-handle `send` / `publish`

### 6.1.1 Recv/callback delivery

recv/direct/send-ready callback의 delivery 경로 자체는 hot path guaranteed의
핵심 subject가 아니다. callback delivery는 라이브러리 내부 thread 모델에 의해
구동되며, public contract 관점에서의 분류는 다음과 같다.

- callback 안에서의 same-handle `send` / `publish`는 hot path guaranteed(6.1절)
- `set_send_ready_handler()` 같은 handler 교체는 control path serialized(6.2절)
- recv/direct handler의 등록·교체 정책은 rewrite spec을 따르며, 이 문서는
  handler 정책 자체를 재정의하지 않는다(5.3절 참조)
- callback thread에서의 control-path API 호출(`bind/disconnect`, option 변경,
  `attach_discovery` 등)은 기본적으로 허용하되, control path serialized 계층의
  직렬화 규칙을 그대로 따른다. 단 callback context에서 금지되는 소수 API(5.2절
  예외 목록: raw STREAM callback 안 `close`, send-ready callback 안 handler
  교체 등)는 callback thread에서도 동일하게 금지된다

### 6.2 Control path serialized / low-frequency

- raw socket `bind/connect/disconnect`
- raw socket option 변경 / heavy query
- `gateway` route mutation, option 변경, monitor open/close, attach 계열
- `spot` / `spot_node` option 변경, peer mutation, attach/heavy query 계열
- `attach_discovery`
- `registry` query/update 계열
- `registry query client`, monitor handle 같은 저빈도 operational surface도
  가능하면 이 계층의 thread-safe 기본 범위 안으로 정리한다
- heavy `query/snapshot`
- `monitor_open/close`
- `set_send_ready_handler`

### 6.3 Runtime read (control path lightweight subset)

- raw socket `RCVMORE`
- raw socket `EVENTS`
- raw socket `LAST_ENDPOINT`
- raw/service `routing_id` 계열 조회
- lightweight readiness/state read

이 항목들은 별도 4번째 계층이 아니라, `Control path serialized / low-frequency`
계층의 lightweight subset이다. 기본 thread-safe 허용 범위에 남기되, heavy
query/snapshot과 같은 직렬화 비용 모델을 강제하지 않는다.

### 6.4 Lifecycle strict

- raw socket `close`
- service facade `destroy`
- monitor handle `close` / `destroy`
- parent handle의 close/destroy가 child 관찰 상태와 만나는 경계

## 7. 설계 원칙

### 7.1 hot path와 control-plane을 분리한다

canonical 원칙:

- hot path state와 control-plane state를 분리한다.
- hot path cacheline과 control-plane cacheline을 분리한다.
- hot path admission과 lifecycle admission은 최소 공유 상태만 사용한다.
- control-plane 직렬화가 hot path에 broad lock으로 번지지 않게 한다.

### 7.2 hot path는 최소 비용만 허용한다

hot path에서 허용되는 비용 모델:

- 최소 atomic/CAS
- 짧은 critical section
- 기존 internal send queue publication 또는 동등한 low-overhead publication
  path
- batch 친화적 enqueue/dequeue

hot path에서 금지하는 것:

- steady-state마다 추가 allocation
- retry wait / backoff loop
- broad lock
- control-plane mutex 재사용으로 인한 공용 병목

### 7.3 control-plane은 serialized lane을 허용한다

control-plane은 correctness 우선 계층이다.

- 내부 serialization lane은 허용된다.
- monitor/discovery/query/option setter는 short critical section을 가질 수 있다.
- 다만 serialized lane이 hot path admission 경로와 같은 lock/state를 공유하면
  안 된다.

### 7.4 lifecycle은 별도 strict gate로 선형화한다

- close/destroy는 hot path queue나 control-plane 직렬화와 별개 admission gate를
  둔다.
- no-latch를 유지하려면 close/destroy admission은 partial teardown을 시작하기
  전에 단일 gate 단계에서 판정되어야 한다.
- accepted close 이후 새 진입은 빠르게 `ESHUTDOWN`으로 종료한다.
- `EBUSY` close 실패는 상태를 오염시키지 않아야 한다.
- 이미 admission된 호출의 완료/종료 의미는 API별 canonical errno로 수렴해야
  한다.

## 8. Subject별 계획

### 8.1 raw socket

raw socket은 최우선 subject다.

- `send`는 `Hot path guaranteed`의 대표 경로다.
- same-handle concurrent `send`를 외부 직렬화 없이 허용해야 한다.
- `bind/connect/disconnect`, option 변경, monitor open은
  `Control path serialized / low-frequency`로 분류한다.
- `close`는 `Lifecycle strict` 규칙을 따른다.

raw socket에서 문서 중심은 `send` hot path와 fail-fast close다. runtime
control-plane API도 범위에 남지만, 비용 목표는 hot path와 분리한다.
다만 raw socket send의 실제 달성 난이도는 하위 socket family 구현이 제공하는
send thread-safety 수준에 영향을 받는다. 이 문서는 공개 계약을 고정하지만,
subject별 구현 난이도 평가는 하위 socket 구현 상태를 함께 본다.

### 8.2 discovery

`discovery`는 범위 밖이 아니라 `control-plane 중심 subject`다.

- topology attach/query/observer 관리가 중심이다.
- correctness와 직렬화 의미가 핵심이다.
- hot path 비용 모델을 discovery에 그대로 요구하지 않는다.
- 다만 discovery 내부 직렬화가 parent data-plane 성능을 오염시키면 안 된다.

`discovery`는 성능 우선 thread-safe 문서에서 주변부가 아니라, control-plane
계층을 대표하는 subject로 다룬다.

### 8.3 registry

`registry`는 `discovery`와 함께 움직이는 `control-plane 중심 subject`다.

- `discovery`와 함께 topology/query/update 의미를 형성하는 control-plane
  구현 단위다.
- hot path 비용 모델을 직접 요구하지 않는다.
- `registry` 직렬화는 correctness와 visibility를 우선하되, parent data-plane
  성능을 오염시키면 안 된다.

### 8.4 gateway

`gateway`는 data-plane과 control-plane이 함께 존재하는 mixed subject다.

- `send` / `send_rid`는 `Hot path guaranteed`
- route mutation, attach, option 변경, monitor open은 `Control path serialized`
- `destroy`는 `Lifecycle strict`

핵심 원칙은 `gateway`의 steady-state send path에 control-plane lock을 섞지 않는
것이다.

### 8.5 spot_node

`spot_node`는 service 계층의 data-plane subject다.

- callback 중 send/publish를 포함한 steady-state 경로는 hot path다.
- peer mutation, attach/query, option 변경은 control path다.
- destroy는 lifecycle strict를 따른다.

### 8.6 unified spot

unified `spot` 역시 mixed subject다.

- `publish`와 허용된 callback 중 same-handle `publish/send`는 hot path다.
- discovery attach, peer mutation, option/query는 control path다.
- close/destroy는 strict gate로 다룬다.

### 8.7 internal child

internal child는 public contract의 직접 대상이 아니다.

- `spot_pub` / `spot_sub` 등은 parent/facade contract를 만족시키기 위한 구현
  단위다.
- child ordering이나 open/destroy 세부 선형화는 확장 규칙으로 다룬다.
- public 문서의 중심은 parent/facade가 어떤 계약을 제공하느냐다.

### 8.8 monitor

monitor는 범위 밖이 아니라 `control-plane 중심 subject`다.

- open/close 자체는 `Control path serialized / low-frequency`
- delivery 경로의 내부 thread-safety는 parent 관찰 정확성 확보가 목적이다.
- monitor open/close 경쟁 계약은 hot path와 다른 비용 모델로 다룬다.
- parent data-plane을 관찰하되, parent hot path를 broad lock으로 막아서는 안 된다.

## 9. Lifecycle strict 상세 규칙

### 9.1 close/destroy admission

- external `close` / `destroy`는 in-flight API(admission gate를 통과하여
  return하지 않은 호출)가 있으면 `EBUSY`
- gate 진입 직전 대기 중인 호출이나 send queue에 enqueue만 된 메시지는
  in-flight에 해당하지 않는다
- `EBUSY`는 fail-fast다
- `EBUSY` 반환만으로 closing state를 latch하지 않는다(no-latch: handle은
  이전 operational state로 완전히 복귀한다)
- accepted close는 partial teardown 이전의 단일 lifecycle gate 또는 동등한 최소
  상태로 표시한다

### 9.2 accepted close 이후의 의미

- 새 진입:
  data-plane/control-plane 구분 없이 `ESHUTDOWN`
- 이미 admission된 호출:
  각 API 계약에 따라 정상 완료 또는 canonical errno로 종료한다.
  계층별 대표 종료 errno는 다음과 같다.
  - hot path: 이미 enqueue된 send는 정상 완료(drain-then-close), admission
    직후이나 아직 enqueue되지 않은 호출은 `ESHUTDOWN`. 단 drain은 best-effort가
    아니라, accepted close 시점의 enqueued message를 모두 소진한 뒤 teardown을
    진행함을 뜻한다
  - control path: 진행 중인 mutation은 정상 완료 또는 `ESHUTDOWN`으로 수렴
  - lifecycle: double close는 `EALREADY`
  이 문서의 canonical errno 집합은 `EBUSY`, `ESHUTDOWN`, `EALREADY`다.
  이 밖의 errno가 필요한 경우 후속 구현/검증 단계에서 명시적으로 추가한다.

이 구분은 문서 초반 표, 공통 계약, 테스트 계획에 모두 동일하게 반영한다.

### 9.3 double close / self-close

- double close / destroy는 `EALREADY`
- self-close는 callback 종류별 확장 규칙으로 정리한다
- raw recv callback에서의 close 금지 같은 민감한 금지 규칙은 핵심 규칙으로
  유지한다

### 9.4 parent-child 확장 규칙

다음 항목은 optional 확장이 아니라, 별도 상세 규칙 문서로 분리해 계속
명세해야 하는 후속 상세 규칙이다.

- child open vs parent destroy 선형화
- callback 종류별 self-close 세부 matrix
- observer ordering 세부 canonical map

핵심 계약은 어디까지나 `external close/destroy fail-fast + accepted close 이후
새 진입 ESHUTDOWN + failed close no-latch`다.

## 10. 성능 수용 기준

### 10.1 문서 핵심 acceptance

성능 수용 기준은 10절 말미의 부록이 아니라 문서 핵심 acceptance다. 새 계약은
성능 관점에서 다음을 만족해야 한다.

- single-thread small message hot path 비용 최소화
- same-handle multi-thread `send/publish` 확장성 유지
- different-handle scaling 저해 금지
- steady-state 경로에 extra allocation, wakeup, retry loop, broad lock 금지

### 10.2 계층별 성능 기준

- hot path:
  추가 비용 최소화, same-handle contention과 different-handle scaling 보호
- control path:
  correctness 우선, 다만 hot path 저해 금지
- lifecycle:
  steady-state 경로에 retry wait/allocation/broad lock 유입 금지

### 10.3 구현 원칙과 측정 관점

- send hot path는 기존 send queue publication을 재사용하거나 동등한 저비용
  publication 경로로 구성한다
- 가능하면 producer 측 동시성만 정리하고 consumer/I/O thread 모델은 유지한다
- 핵심 비용은 publication/admission 경계에 몰아넣고, steady-state 송신 경로에
  broad lock을 섞지 않는다
- control-plane 직렬화는 별도 lane으로 보낸다
- lifecycle gate는 single-word admission 또는 동등한 최소 공유 상태를 우선한다
- 성능 측정의 1차 대상은 raw/gateway/spot send path다
- control-plane 혼합 workload는 2차 기준으로 둔다

send queue / publication path 후보는 문서에서 구현 선택지로만 남기고, 특정
자료구조를 canonical contract로 고정하지 않는다.

- 후보 1:
  기존 internal send queue를 유지하면서 producer admission만 thread-safe하게
  확장
- 후보 2:
  검증된 concurrent queue를 도입해 publication 경로를 대체
- 후보 3:
  bounded ring 또는 동등한 고정 비용 publication path를 subject별 요구에 맞게
  적용
- 후보 4:
  per-producer queue fan-in 같은 고성능 구조를 필요 시 후속 검토

선택 기준은 다음을 우선한다.

- hot path 비용 최소화
- same-handle multi-producer 확장성
- different-handle scaling 비저해
- close/admission/visibility 계약과의 결합 용이성
- ownership, backpressure, memory usage 정책과의 적합성

## 11. 구현 항목과 적용 순서

구현 순서는 hot path 우선으로 다시 쓴다.

1. raw socket `send` hot path + fail-fast close
2. `gateway` `send` / `send_rid`
3. unified `spot` / `spot_node` `publish`
4. service/facade의 허용된 callback 중 same-handle `send` / `publish`
5. 필요한 범위의 monitor delivery 정리
6. `discovery` / registry / 기타 control-plane 정리

공통 원칙은 "모든 subject에 동일한 runtime 계층을 먼저 강제"가 아니라,
"hot path별 최소 공통 원칙을 먼저 고정"이다.

## 12. 테스트 계획

### 12.1 Hot path guaranteed 필수 회귀

- same-handle concurrent `send`
- same-handle concurrent `publish`
- same-handle concurrent `send_rid`
- 허용된 callback 중 same-handle `send/publish`
- different-handle scaling:
  handle 수를 단계적으로 증가시키며(예: 1, 4, 16, 64) 각 handle에 동일한
  send workload를 부여한 뒤, handle 수 증가에 따른 per-handle throughput이
  유의미하게 저하되지 않음을 확인한다. 측정 대상은 per-handle throughput과
  총 aggregate throughput이다. single-handle baseline 대비 per-handle
  throughput 저하율이 20% 이내여야 한다(64 handle 기준). 이 수치는 초기
  acceptance 하한이며, subject별 세부 계획 문서에서 더 엄격하게 조정할 수 있다
- 합격 기준:
  corruption 없음, hang/abort 없음, 문서가 허용한 errno 집합 밖의 오류 없음
- stress 기준:
  반복 실행과 경쟁 창 확대를 포함한 다회 반복으로 검증한다. 최소 반복 하한은
  10^3회 이상으로 하되, subject별 세부 계획 문서에서 상향할 수 있다
- timeout 기준:
  hard timeout을 두고 timeout은 즉시 실패로 처리한다
- sanitizer 기준:
  가능한 lane에서는 TSan 경고 0건을 목표 acceptance로 둔다

### 12.2 Control path serialized 필수 회귀

- `bind/connect/disconnect`, option 변경, attach/heavy query가 직렬화된 의미를
  유지하는지
- `monitor_open/close`와 `set_send_ready_handler()`가 low-frequency serialized
  contract를 만족하는지
- same-handle concurrent control-path 호출의 ordering semantics가 문서와 어긋나지
  않는지
- control-path와 data-plane이 경합할 때 correctness가 유지되는지
- 합격 기준:
  성공 반환한 control-path 호출의 효과가 이후 admission된 호출에서 관측되고,
  linearization order가 내부 직렬화 의미와 어긋나지 않을 것
- timeout 기준:
  hard timeout을 두고 hang은 즉시 실패로 처리한다
- sanitizer 기준:
  가능한 lane에서는 data race warning 0건을 목표 acceptance로 둔다

### 12.2.1 Runtime read 필수 회귀

- `RCVMORE`, `EVENTS`, `LAST_ENDPOINT`, `routing_id` 계열 조회가 문서가 의도한
  기본 thread-safe read 계약을 만족하는지
- lightweight runtime read가 heavy query/snapshot과 같은 직렬화 비용 모델로
  강제되지 않는지
- read와 data-plane/control-path가 경합할 때 문서가 허용한 범위 밖의 errno나
  hang이 없는지
- 합격 기준:
  correctness 유지, hang 없음, hot path를 불필요하게 직렬화하지 않을 것
- timeout 기준:
  hard timeout을 두고 hang은 즉시 실패로 처리한다

### 12.3 Lifecycle strict 필수 회귀

- external `close` / `destroy` -> `EBUSY`
- accepted close 이후 새 진입 -> `ESHUTDOWN`
- 이미 admission된 호출의 완료/종료 의미 유지
- failed close 후 handle live 유지
- double close / destroy -> `EALREADY`
- 합격 기준:
  no-latch, canonical errno, hang 없는 종료가 모두 유지될 것
- timeout 기준:
  hard timeout을 두고 wait-to-drain regressions를 즉시 검출한다

### 12.4 Cross-tier 경합 필수 회귀

계층 간 경합은 단일 계층 테스트로 검증할 수 없는 버그의 주요 원인이다.

- hot path send + lifecycle close 경합:
  close가 accepted되기 직전/직후에 send가 진입하는 경합 창 검증
- hot path send + control path bind/disconnect 경합:
  steady-state send 중 connect/disconnect가 동시에 일어나는 경우의
  correctness 유지
- control path option 변경 + lifecycle close 경합:
  option setter 진행 중 close가 들어오는 경우 `EBUSY` 또는 setter 완료 후
  close accepted 순서가 유지되는지
- callback thread send + worker thread close 경합:
  callback 안 send가 진행 중일 때 외부 thread에서 close를 시도하는 경우.
  callback 경로의 send도 inflight/admission 보호 범위 안에 남아 있는지 검증
- 합격 기준:
  canonical errno 이외 오류 없음, hang/abort 없음, no-latch 위반 없음
- timeout 기준:
  hard timeout을 두고 hang은 즉시 실패로 처리한다
- sanitizer 기준:
  가능한 lane에서는 TSan 경고 0건을 목표 acceptance로 둔다

### 12.5 최소 테스트 acceptance 공통 규칙

- 모든 회귀는 hard timeout을 사용한다.
- retry 기반 통과를 금지한다.
- corruption, abort, hang은 단일 실패로 즉시 처리한다.
- 문서가 허용하지 않은 errno는 실패다.
- stress 반복 수와 sanitizer lane은 subject별 세부 계획 문서에서 더 강화할 수
  있지만, 이 문서에서는 최소 acceptance로 유지한다.

### 12.6 후속 확장 회귀

- send-ready visibility 세부 규칙
- parent-child open/destroy 선형화
- discovery/registry full race matrix
- callback 종류별 self-close 세부 matrix

기존 public API를 흔드는 테스트 삭제는 하지 않는다. 테스트는 삭제가 아니라
계층 기준 재분류를 원칙으로 한다.

## 13. ABI / header / 문서 정합성

- public header에는 thread mode 선택 API를 추가하지 않는다.
- recv/direct/send-ready 관련 existing public surface는 유지한다.
- `set_send_ready_handler()`는 남기되 hot path 핵심 요구에서 제외한다.
- `discovery`와 `monitor`는 문서상 `control-plane 중심 subject`로 명시한다.
- "모든 operational API 동일 강도 thread-safe" 같은 문구는 header comment와
  문서 어디에도 남기지 않는다.
- "기본적으로 thread-safe, 예외만 명시" 문구가 문서 초반과 요약에 존재해야
  한다.
- 예외 목록은 init-only, callback-context 제한, same-message object 공유,
  proxy 실행 모델로 압축되어 있어야 한다.
- `registry query client` 같은 저빈도 handle은 큰 비용 없이 올릴 수 있다면
  예외 목록보다 기본 thread-safe 범위로 편입하는 방향이 문서에 명시돼 있어야
  한다.

## 14. 문서 재작성 검토 체크리스트

문서 수정 후 아래 항목을 반드시 확인한다.

- `all operational API 동일 강도 thread-safe` 문구가 남아 있지 않을 것
- 문서 초반에 3계층 계약 표가 있을 것
- runtime read가 control-path heavy query와 구분되어 있을 것
- control-path 정의에 ordering semantics가 포함될 것
- `control-plane = 초기화 단계 전용` 같은 과도한 축소 문구가 없을 것
- `send-ready setter 제거`, `handler 생성 시 고정화` 같은 API 철학 변경이
  없을 것
- `discovery`와 `monitor`가 `control-plane 중심 subject`로 명시될 것
- `accepted close 이후 새 진입`과 `이미 admission된 호출`이 구분되어 있을 것
- `ESHUTDOWN` 적용 범위가 data/control 모두로 명시될 것
- 테스트 계획이 `hot path / control path / runtime read / lifecycle / cross-tier`
  기준으로 재분류될 것
- 성능 acceptance가 hot path 중심 목표와 직접 연결될 것

## 15. 기본 가정

- 문서 범위는 유지한다. raw socket, `gateway`, `spot`, `discovery`, `monitor`를
  계속 포함한다.
- public API surface는 흔들지 않는다. runtime control-plane API와
  `set_send_ready_handler()`는 유지한다.
- 변경하는 것은 포함 여부가 아니라 보장 강도다.
- 새 canonical contract는 `data-plane 최우선 / control-plane serialized /
  lifecycle strict`의 3계층이다.
- 이번 재작성은 구현 단순화를 위한 요구사항 재정렬이지, API 삭제나 초기화 전용
  강제 같은 철학 변경이 아니다.
- low-frequency handle 중 큰 비용 없이 thread-safe하게 만들 수 있는 것은
  가능한 한 예외 목록에 남기지 않는 방향을 기본 가정으로 둔다.

## 부록 A. 현재 구현 대비 상태 메모

이 문서는 목표 계약 설계안이지만, 독자가 현재 구현과의 거리를 잃지 않도록 최소
상태 메모를 남긴다.

- raw socket admission gate, send-ready pair 관측 일관성, 일부 deferred close
  계열은 이미 구현 기반이 존재한다.
- `gateway` / `spot_node` / unified `spot`의 class-level admission, lifecycle
  gate, hot path 정렬은 이미 반영되었고, 현재 문서의 중심은 그 공개 계약을
  유지·정리하는 것이다.
- `discovery` / `registry` / `monitor`는 control-plane subject로서 correctness와
  visibility 규칙이 문서와 테스트에 계속 일치하는지 추적하는 대상이다.

현재 구현의 service public API guard(`service_public_api.hpp`)는 이 문서의
lifecycle strict 계층(admission gate, `ESHUTDOWN` 진입 차단)과 control path
serialized 계층(API 진입 직렬화)을 실현하는 핵심 메커니즘이다. guard가 어떤
계층의 어떤 계약을 담당하는지는 아래와 같다.

- admission gate → lifecycle strict의 in-flight 판정과 `EBUSY` / `ESHUTDOWN`
  반환
- API 진입 직렬화 → control path serialized의 same-handle concurrent
  control-path 호출 correctness 보장
- hot path send는 guard의 broad lock 경로를 우회하여 최소 비용 경로를 사용한다

세부 달성도 표나 구현별 갭 분석은 별도 상태 문서 또는 구현 리뷰 문서에서
갱신한다.
