[Spec Index](../README.md)

# Bindings API Policy

> request-reply 와 SPOT routed 구현 기준은
> [`doc/plan/spot-refactor`](../../plan/spot-refactor) 아래 문서를 따른다.
> 언어별 인터페이스 시그니처와 사용 예는
> `c/`, `cpp/`, `java/`, `dotnet/`, `node/`, `python/`, `go/`, `rust/` 를 참조한다.

## 목적
이 문서는 `bindings/` 전체의 public API 정책을 정의한다.

이 문서의 목적은 각 언어 바인딩이 제각각 다른 표면과 예외 규칙을 갖는 것을
막고, `core/include/zlink.h`를 기준으로 설명 가능하고 일관된 공통 계약을
강제하는 데 있다.

`c/`, `cpp/`, `java/`, `dotnet/`, `node/`, `python/`, `go/`, `rust/` 아래 문서는
각 바인딩 구현이 실제로 외부에 제공해야 하는 public API contract를 정의한다.
이 문서들이 규정하는 것은 공개 타입, 메서드, 시그니처, 반환값, 오류 의미이며,
바인딩 구현이 노출하는 public 인터페이스는 이 계약과 달라지면 안 된다.
다만 이 문서는 실제 구현 클래스 계층이나 내부 파일 구조까지 규정하지는 않는다.
각 바인딩은 같은 공개 계약을 유지하는 범위에서 내부 구조를 언어 관례에 맞게
자유롭게 설계할 수 있다.

이 문서는 단순 스타일 가이드가 아니다. 다음을 위한 설계 기준 문서다.
- public API 설계 기준
- 리뷰 기준
- 리팩터링 기준
- 샘플과 테스트 기준

이 문서의 의도는 다음과 같다.
- 언어별로 이름만 비슷하고 의미가 다른 API를 없앤다.
- 같은 능력을 여러 방식으로 중복 노출하는 얕은 표면을 없앤다.
- raw option bag, 불필요한 편의 래퍼, 암묵적 ownership, 숨은 오류 경로를
  줄인다.
- binding 사용자가 internal sequencing, native 세부사항, hidden transport
  switch를 알지 않아도 되게 만든다.
- POSD 원칙에 맞는 깊은 모듈과 낮은 변경 파급(change amplification) 구조를 유도한다.
- correctness뿐 아니라 비용 모델, 샘플 품질, 테스트 가능성까지 공통 기준으로
  묶는다.

기준은 항상 `core/include/zlink.h` 이다. 각 바인딩은 코어 계약을 따르되,
표현 방식은 언어 관례에 맞게 선택할 수 있다. 다만 의미 계약은 바뀌면 안 된다.

이 문서는 “각 언어가 어떻게 보일 수 있는가”보다 “각 언어가 무엇을 보장해야
하는가”를 정의한다.

## Substrate vs Public Binding Surface

bindings 구현은 core가 제공하는 helper substrate C API(`*_part` 계열) 위에 올라간다.
bindings 사용자에게 노출되는 public API는 그 helper 시그니처를 그대로 따라야 한다는
뜻이 아니다. 다만 내부 구현이 어떤 core 함수를 호출하는가는 아래 규칙으로 고정한다.

이 문서는 아래 경계를 기준으로 해석한다.

- `core/include/zlink.h` 의 `*_part` helper substrate 계약은
  bindings 구현이 반드시 사용해야 하는 native substrate다.
- `doc/spec/bindings/` 아래 문서는 각 언어 binding이 외부에 제공하는
  **public convenience contract**만 정의한다.

즉 binding public API는 helper substrate와 모양이 달라도 된다. 그러나 내부에서
core를 호출하는 방식은 달라서는 안 된다.

예를 들면 아래 구조가 요구된다.

- core substrate는 `*_part`, `has_more`, caller-provided `zlink_msg_t`
  같은 primitive한 표면을 가진다.
- Java, `.NET`, `Go`, `Rust`, `Python`, `Node`, `C++`, C binding은 그 위에
  `Received`, `Message`, collection, request/reply convenience 같은
  언어 친화적 public API를 올린다.
- public API 내부에서 core를 직접 호출하는 경로는 반드시 `*_part` substrate를 사용한다.
  aggregate 형태의 core 함수(`zlink_send`, `zlink_recv`, `zlink_publish` 등)를
  binding 내부에서 호출하면 안 된다.

아래 조건은 반드시 지켜야 한다.

- binding public API의 의미 계약은 core 계약으로 설명 가능해야 한다.
- helper substrate에만 있는 low-level 세부사항을 binding 사용자에게 직접 노출하지
  않는다.
- `doc/spec/bindings/` 문서는 helper substrate 시그니처 자체를 public contract로
  문서화하지 않는다.
- helper substrate는 bindings 구현과 성능 최적화를 위한 기반 계층으로만 취급한다.

즉 bindings 정책 문서의 기준은 "helper가 어떻게 생겼는가"가 아니라,
"binding 사용자가 최종적으로 어떤 public contract를 보게 되는가"이다.

## `*_part` Substrate 사용 의무 (Required)

send, request, reply, publish, subscribe 계열 함수의 내부 구현은 반드시
core의 `*_part` helper substrate를 사용해야 한다. 이는 `Required` 규칙이다.

### 적용 대상

아래 계열에 해당하는 모든 binding 내부 구현 경로에 적용한다.

- send / trySend (단일 part, 복수 part, routed 포함)
- recv / tryRecv (단일 part, 복수 part, routed 포함)
- request / tryRequest (dealer, router, SPOT 계열 포함)
- reply (router, SPOT 계열 포함)
- publish / tryPublish
- subscribe / trySubscribe (SPOT subscribe 포함)

### 이유

core가 aggregate 함수와 `*_part` substrate를 모두 제공하던 시기에는 aggregate 함수를
직접 호출하는 것이 허용됐다. 그러나 이 구조는 아래 비용을 만든다.

- core가 먼저 native aggregate (parts 배열) 를 구성한다.
- binding이 그 aggregate를 다시 언어별 객체(`Message[]`, `Received`, value object)로
  변환한다.
- 결과적으로 "native aggregate 생성 → 언어 객체 aggregate 생성"이 연속으로 일어나며,
  이 이중 변환 비용이 hot path의 실질적인 병목이 된다.

`*_part` substrate를 직접 사용하면 binding이 part 하나씩 언어 객체로 직접 변환할 수
있고, native aggregate 생성 단계를 완전히 제거할 수 있다. 이는 특히 Java, .NET처럼
객체 materialization 비용이 큰 언어에서 측정 가능한 성능 차이를 만든다.

이 규칙은 구조 정리 목적이 아니라 **런타임 성능 비용을 실질적으로 줄이기 위한** 요구사항이다.

### public API 형태는 유지

이 규칙은 내부 구현 기반에 관한 것이다. binding 사용자가 보는 public API 형태는
이 규칙과 무관하게 각 언어 spec이 정한 대로 유지한다.

- 사용자는 `send(List<Message>)`, `recv()`, `request(...)` 같은 언어 친화적 API를 그대로 쓴다.
- `*_part` 호출 시퀀스는 binding 내부 구현 세부사항이며, 사용자에게 노출하지 않는다.

### 준수 확인

구현 리뷰와 검증 단계에서 아래를 확인한다.

- binding 소스에서 aggregate 심볼(`zlink_send`, `zlink_recv`, `zlink_send_rid`,
  `zlink_publish`, `zlink_subscribe`, `zlink_router_recv`, `zlink_dealer_request`,
  `zlink_router_request`, `zlink_router_reply`, `zlink_spot_send_*`,
  `zlink_spot_request_*`, `zlink_spot_reply_*`, `zlink_spot_subscribe` 등)을
  직접 호출하는 경로가 없어야 한다.
- 대신 대응하는 `*_part` 심볼(`zlink_send_part`, `zlink_recv_part`,
  `zlink_send_part_rid`, `zlink_publish_part`, `zlink_subscribe_part`,
  `zlink_router_recv_part`, `zlink_dealer_request_part`, `zlink_router_request_part`,
  `zlink_router_reply_part`, `zlink_spot_*_part` 등)을 사용해야 한다.
- 미준수 시 리뷰에서 차단된다.

## Public vs Internal API Boundary

각 binding은 public contract와 internal implementation surface를 분리해야 한다.
이 문서와 각 언어별 spec에 적힌 것만 public API다.

아래 원칙은 모든 binding에 공통으로 적용한다.

- public으로 문서화되지 않은 타입, 함수, 메서드, 모듈, 패키지, 네임스페이스는
  모두 internal implementation detail로 본다.
- internal API는 이름만 internal처럼 보이게 두면 충분하지 않다. 가능한 언어는
  패키지 export, module export, assembly visibility, crate re-export,
  package `exports`, `internal/` directory 같은 언어 고유 경계를 사용해
  실제로 접근을 제한해야 한다.
- perf, sample, test도 원칙적으로 public binding entrypoint만 사용해야 한다.
  같은 저장소 안에 있다고 해서 internal helper를 직접 import하거나 참조하면
  안 된다.
- public contract 검증은 배포된 binding consumer가 보게 되는 entrypoint를
  기준으로 한다. 소스 트리 내부에 internal symbol이 존재한다는 이유로 public으로
  간주하지 않는다.
- internal 구조를 리팩터링할 자유는 보장하되, 그 자유는 public contract를
  유지하는 범위 안에서만 허용된다.

즉 이 문서의 목적은 "public API를 문서화하는 것"일 뿐 아니라,
"public이 아닌 API를 public처럼 사용하지 못하게 경계를 강제하는 것"까지
포함한다.

### Send/Recv Public Shape Is Fixed

bindings의 `send/recv` 공개 형태는 substrate helper가 어떻게 생기느냐에 따라
매번 다시 정하는 대상이 아니다. 이 문서와 각 언어별 binding spec이 정한
public shape를 기준으로 고정한다.

즉 helper substrate가 `*_part`, `has_more`, caller-provided message storage
형태로 바뀌더라도, binding public API는 아래 원칙을 유지해야 한다.

- binding 사용자는 언어 문서에 정의된 `send`, `trySend`, `recv`, `tryRecv`,
  request/reply, callback 형태를 본다.
- multipart는 각 언어 문서가 정한 aggregate convenience 모델로 계속 제공할 수
  있다.
- helper substrate 변경만을 이유로 binding public `send/recv` shape를 함께
  흔들면 안 된다.
- public shape를 바꾸려면 helper 도입과는 별도의 public API 변경으로 다뤄야
  하며, `doc/spec/bindings/` 문서부터 먼저 갱신해야 한다.

즉 앞으로 helper C API를 도입하더라도, bindings 쪽 `send/recv`는
"구현 기반이 바뀌는 것"이지 "사용자에게 보이는 형태가 자동으로 바뀌는 것"이
아니다.

## Core Alignment Overrides

이 절은 언어별 문서의 세부 예제보다 우선 적용되는 core 계약 요약이다.
`core/include/zlink.h` 와 언어별 문서 간 불일치가 있으면 이 절을 기준으로 한다.

- direct receive callback install surface 는 raw `STREAM` 과 SPOT routed
  receive 에만 존재한다.
- 바인딩은 raw `PAIR`, `DEALER`, `ROUTER` 에 대해 `onReceive` 류 direct data
  callback 을 public 으로 노출하면 안 된다.
- 바인딩은 raw `SUB`, `XSUB`, SPOT subscribe receive 에 대해
  `onSubscribe` 류 direct topic callback 을 public 으로 노출하면 안 된다.
- `ROUTER` inbound routed traffic 은 data-plane recv-only 이다. 바인딩은
  `zlink_router_recv()` 대응 수신 표면과 request completion callback 만
  노출한다.
- core raw `STREAM` 은 `recv`, raw callback (`zlink_recv_handler()`),
  packet callback (`zlink_stream_packet_handler()`) 의 세 모드 중 하나를
  선택하는 예외 타입이다. 고수준 바인딩은 `recv` 와 packet callback
  surface 를 기본 계약으로 노출해야 하고, raw direct callback 은 해당
  언어 문서가 명시할 때만 추가로 public 으로 노출한다.
- SPOT 은 channel-aware 모델이다. 바인딩은
  `attach_discovery(...)`,
  `attach_channel_dealer(...)`,
  `attach_channel_dealer_manual(...)`,
  `attach_pub_ingress(...)`,
  `send_channel`, `request_channel`,
  channel-aware publish / subscribe 표면을 제공해야 한다.
- service-aware SPOT subscribe 결과는 topic / parts 와 함께 반드시
  `service_name` 을 노출해야 한다.
- `zlink_spot_dispatch_event_handler()` 가 SPOT topic/routed/timer plane 의
  canonical readable notification surface 이다.
  `zlink_spot_handler()` 와 routed 축에서 mutually exclusive 다.
- `zlink_send_ready_handler()` 와 poller `ZLINK_POLLOUT` 은 같은
  send-recovery readiness 축을 가리킨다. 바인딩 문서도 같은 의미로 설명해야
  한다. `ZLINK_POLLOUT` 은 "transport writable" 이 아니라
  "send recovery readiness / backpressure recovery notification" 으로 설명한다.
- 바인딩은 `zlink_set_admission_state()` / `zlink_get_admission_state()` 와
  `ZLINK_ADMISSION_SERVING` / `ZLINK_ADMISSION_DRAINING` enum 을 언어별
  typed surface 로 노출해야 한다. 대응하는 제출 실패 코드는
  `ZLINK_SUBMIT_NOT_ADMITTED` (값 13) 이며, 모든 바인딩의 `SubmitError`
  매핑에 포함되어야 한다.
- core raw `STREAM` 은 다음 세 수신 모드 중 하나만 선택할 수 있다:
  (a) `zlink_recv()` blocking/non-blocking recv, (b) `zlink_recv_handler()`
  raw direct callback, (c) `zlink_stream_packet_handler()` 빅엔디언
  `u16 header_size + u32 body_size + header + body` 프레이밍 packet callback.
  두 번째 attach 시 `EBUSY` 가 반환된다. 고수준 바인딩이 raw direct
  callback 을 공개하지 않더라도, public 으로 노출한 `STREAM` receive
  surface 들 사이에는 같은 배타 규칙을 유지해야 한다.
- `zlink_recv_handler()` 는 raw `STREAM` 전용이다. `PAIR`/`DEALER`/`SUB`/
  `XSUB`/`ROUTER` 에 attach 하면 `ZLINK_HANDLER_NOT_SUPPORTED` 로 실패한다.
- Discovery auto-connect 으로 같은 서비스의 두 ROUTER 가 쌍을 이룰 때,
  내부 정책이 `(routing_id, advertise endpoint)` 전순서로 initiator 를
  선정한다. 사용자가 설정하는 옵션이 아니다.
- socket 기본값: `ZLINK_ROUTER_OPT_MANDATORY` = `1`,
  `ZLINK_ROUTER_OPT_HANDOVER` = `1`, `ZLINK_PUB_OPT_NODROP` = `1`.
  바인딩 예제는 이 기본값을 기준으로 작성한다.

## 문서 해석 규칙
- 이 문서의 정책 본문은 기본적으로 규범 문서다.
- 아래 용어는 다음 의미로 해석한다.
  - `Required`: 현재 리뷰와 구현에서 반드시 지켜야 하는 항목.
    미준수 시 리뷰에서 차단된다.
  - `Recommended`: 강하게 권장하지만, 바인딩 특성에 따라 단계적으로 적용할
    수 있는 항목. 미준수 시 리뷰에서 사유를 요구하지만 차단하지 않는다.
  - `Target`: 장기적으로 맞춰가야 하는 목표 항목. 해당 바인딩이 이
    컴포넌트를 구현하기로 결정한 경우에만 적용된다. 구현하지 않기로
    결정한 경우 리뷰에서 요구하지 않는다.
- 별도 표시가 없으면 정책 본문은 `Required`로 본다.
- 섹션 제목에 `(Target)` 또는 `(Recommended)`가 표시된 경우, 해당 섹션
  전체는 표시된 수준으로 해석한다. 무표시 기본값(`Required`)보다 우선한다.
- `Non-Normative Backlog: Implementation Follow-Ups` 섹션은 규범 본문이 아니라
  비규범 backlog다.
- backlog 항목은 현재 미준수 가능성을 추적하기 위한 것이며, 문서 본문의 의미 계약을 대체하지 않는다.

## 핵심 원칙
- 코어 계약은 `zlink.h`의 `*_part` substrate가 단일 기준이다.
- send/recv/request/reply/publish/subscribe 계열의 내부 구현은 반드시 core `*_part`
  substrate를 사용한다. aggregate 형태의 core 함수를 binding 내부에서 직접 호출하지 않는다.
- public API는 multipart 모델을 기준으로 설계한다.
- blocking과 non-blocking은 이름으로 구분할 수 있다.
- 동일한 능력을 여러 방식으로 중복 노출하지 않는다.
- 값의 의미는 `int`가 아니라 enum, boolean, value object로 올린다.
- raw option bag은 public에 노출하지 않는다.
- 바인딩은 코어의 상태 오류를 추론하지 않는다.
- 입력 값의 형식, 범위, overflow, truncation 위험은 바인딩이 먼저 막는다.
- 구조는 POSD 원칙에 따라 깊은 모듈, 정보 은닉, 낮은 변경 파급을
  우선한다.
- 이 문서는 의미 계약을 우선 정의한다.
- 언어별 표면은 각 언어 관례에 맞게 달라질 수 있지만, 의미 계약은 같아야
  한다.

## Monitor Ready Contract
- `*_READY_CHANGED` monitor event 의 `value` 는 aggregate ready count 계약이 아니다.
- binding public API는 monitor snapshot 에 ready-count surface 가 있다고
  가정하면 안 된다.
- readiness gate 가 필요하면 low-cost event edge 를 직접 사용해야 한다.
- raw perf/샘플은 `CONNECTION_READY` event counting 을 사용한다.
- SPOT perf/샘플은 service monitor gate 를 사용하지 않는다.
- SPOT perf 는 explicit `READY/START` barrier protocol 을 사용한다.
- delivery-ready/count 계열 monitor event 를 새 gate contract 로 만들면 안 된다.

## POSD Structure Policy
- 바인딩 설계는 John Ousterhout의 POSD 원칙을 따른다.
- public API는 사용자가 알아야 할 개념 수를 줄여야 한다.
- 내부 구현 복잡도는 facade, value object, domain object 뒤로 숨겨야 한다.
- 얕은 래퍼(shallow wrapper)는 지양한다.
  - 단순히 native 함수 이름만 바꾸고 새 의미를 추가하지 못하는 public
    wrapper는 늘리지 않는다.
- 같은 능력을 여러 타입과 여러 이름으로 반복 노출하지 않는다.
- 변화가 한 곳에서 끝나야 할 규칙은 한 모듈에 모은다.
  - 예: routing id 길이 제한
  - 예: send failure contract
  - 예: typed option ownership
- 시간 순서에 의존하는 분해(temporal decomposition)를 줄인다.
  - 예: 사용자가 `setOption` 조합 순서를 기억해야 하는 API 금지
- public API는 “무엇을 할 수 있는지”를 드러내고, “내부에서 어떻게 배선되는지”를
  드러내지 않아야 한다.
- 값 객체와 결과 객체는 깊은 모듈로 취급한다.
  - 호출자에게는 작은 인터페이스를 주고, 내부에서는 검증, ownership, shape
    규칙을 함께 캡슐화해야 한다

## Public Surface Rules

### Base Type Exposure
- 가능하면 컴파일 단계에서 사용자가 concrete socket type만 직접 쓰게 해야 한다.
- 사용자가 generic root base, raw compat base, shared base를 concrete socket
  type 대신 직접 쓰는 구조는 피한다.
- static typed binding은 public type/export/visibility를 이용해 이 규칙을
  강제해야 한다.
- dynamic binding은 export 제한과 surface test로 같은 규칙을 강제해야 한다.
- generic root base 또는 raw compat base는 공통 lifecycle과 공통 관리 기능만
  외부에 노출한다.
- capability-specific shared base는 모든 descendant가 공통으로 가지는 능력만
  외부에 노출할 수 있다.
- socket-type-specific capability를 generic root base나 raw compat base로
  올리면 안 된다.
- public base에서 외부 접근을 허용해도 되는 공통 기능 예:
  - `bind`, `unbind`
  - `connect`, `disconnect` on connectable base only
  - `close` / `dispose`
  - common typed options
  - `monitorOpen` 또는 동등한 monitor 진입점
- generic root base 또는 raw compat base에서 외부 접근을 허용하면 안 되는 기능:
  - `send(...)`
  - `send(routingId, ...)`
  - `sendParts(...)`
  - `sendFrom(...)`
  - `recv()`
  - `recv(flags)` / `recv(size, flags)`
  - `recvInto(...)`
  - `recvMsgInto(...)`
  - routed receive alias (`receiveRouted` 등)
  - `publish(...)`
  - `setSubscription(...)`
  - `unsetSubscription(...)`
  - `subscribe()`
  - `receiveSubscriptionEvent()`
  - `onReceive(...)`
  - `onSubscribe(...)`
  - `onSendReady(...)`
  - `setRoutingId(...)`, `getRoutingId()`
  - `attachDiscovery(...)`
  - `attachStreamRaw(...)`, `detachStream()`
  - `streamAttach(...)`, `streamAttachRaw(...)`, `streamDetach()`
  - `streamPeerRoutingId(...)`, `streamSend(...)`
  - raw option bag (`setOption`, `getOption`, `setSockOpt`, `getSockOpt` 등)
  - topic/socket-type-specific option facade
  - canonical 이름을 우회하는 legacy alias
    - 예: `recvHandler(...)`, `subscribeHandler(...)`
- capability-specific shared base는 descendant 전부에 공통인 capability에 한해
  허용할 수 있다.
  - 예: subscriber-only base의 `setSubscription`, `unsetSubscription`,
    `subscribe`
  - 예: publisher-only base의 `publish`, `onSendReady`
  - 예: discovery-capable socket base의 `attachDiscovery`
- 위 capability는 capability matrix에서 `Y`인 concrete socket type에만
  public으로 존재해야 한다.
- capability matrix에서 `—`인 socket type에 대해 base 경유 우회 호출이 가능하면
  안 된다.
- perf, sample, helper, compat layer도 canonical public surface 규칙을
  우회하는 base entry를 새 기준처럼 사용하면 안 된다.
- deprecated compat API가 필요하더라도 canonical public API와 분리된 compat
  namespace 또는 internal surface로 격리한다.
- 사용자가 `SocketType`과 raw flag 조합을 기억해서 올바른 send/recv 계열을
  선택해야 하는 구조는 POSD 위반으로 본다.

### Multipart Only
- send/receive public surface는 multipart 기준으로 통일한다.
- 단일 메시지 수신 편의 오버로드는 public에 두지 않는다.
- 단일 part 전송 편의 메서드는 허용할 수 있다.
  - 예: `send(Message part)`는 `send(List<Message> parts)`의 간편 오버로드
- 수신 결과는 언어에 맞는 도메인 객체 또는 동등한 multipart 표현으로
  반환한다.

### Error Handling Policy

모든 데이터 경로 함수 (`send`, `recv`, `request`, `reply`, `subscribe`,
`publish`) 는 동일한 에러 처리 원칙을 따른다.

#### 원칙

1. **Exception 언어는 반환값으로 에러를 전달하지 않는다.**
   - 대상: C++, Java, .NET, Node, Python.
   - 성공 시 결과를 반환하거나 void 반환한다.
   - 실패 시 예외를 던진다.
   - 예외에는 `int code` (0–703 범위) 를 포함하여 호출자가 실패 원인을
     구분할 수 있게 한다.
   - `BACKPRESSURED`, `NOT_CONNECTED`, `NOT_FOUND` 를 포함한 모든 실패는
     예외로 전달한다. 이들은 반환값이 아니다.
2. **C / Go / Rust 는 exception 이 없으므로 return-based 계약을 따른다.**
   바인딩은 각 언어 관용구에 맞는 스타일로 처리한다.
   - C: 함수별 typed result enum 반환
     (`zlink_submit_result_t`, `zlink_recv_result_t`,
      `zlink_handler_result_t`, `zlink_close_result_t`,
      `zlink_bind_result_t`, `zlink_connect_result_t`,
      `zlink_config_result_t`).
   - Go: `(T, error)` 반환. error 객체에 `int` 코드를 포함한다.
   - Rust: `Result<T, E>` 반환. `E` 는 가능한 한 함수군별 구체 에러
     (`BindError`, `SubmitError` 등)를 쓰고, 여러 함수군이 섞이는 경계에서만
     `ZlinkError` 로 승격한다. 에러 값에는 `int` 코드가 포함된다.
     `?` 연산자로 호출측 전파를 쓴다.
3. **데이터 plane primitive 와 callback request submit 은 canonical `Try*` 쌍을 둘 수 있다.**
   - 대상은 `send` / `recv` 계열 primitive operation 이다.
   - 공개 surface 는 `send` / `trySend`, `recv` / `tryRecv` 쌍으로 노출할 수 있다.
   - callback completion request 는 submit 단계가 따로 있으므로
     `request(..., callback, ...)` / `tryRequest(..., callback, ...)` 쌍을 둘 수 있다.
   - coroutine / await request 에는 `tryRequest` 를 두지 않는다.
   - `reply`, `publish`, `subscribe` 는 composite operation 이므로
     `tryReply`, `tryPublish`, `trySubscribe` 를 추가하지 않는다.
   - `sendNoWait`, `recvNoWait`, `publishNoWait` 같은 transport-style 이름은
     공개 surface 에 두지 않는다.
   - `trySend` 는 temporary backpressure 만 `false` 로 돌려주고,
     route-not-ready 를 포함한 다른 submit 오류는 예외 또는 반환 에러로
     전달한다.
   - `tryRecv` 는 현재 읽을 데이터가 없으면 `false` 또는 `null` 로 돌려주고,
     진짜 오류만 예외 또는 반환 에러로 전달한다.
4. **`INTERNAL_ERROR` 상세 조회.**
   - result code 가 `INTERNAL_ERROR` 계열 (12, 104 등) 이면
     `zlink_errno()` 로 내부 raw errno 를 조회할 수 있다.
   - 바인딩의 에러 타입(exception 언어는 예외 객체, return-based 언어는
     에러 값)은 `internalErrno` / `internal_errno` 필드로 이를 노출한다
     (디버깅 전용).
   - 그 외 result code 에서는 `zlink_errno()` 호출이 불필요하다.

#### 언어별 에러 표현

| 언어 | 처리 방식 | 에러 타입 | 코드 접근 | 내부 errno |
|---|---|---|---|---|
| C | return | 함수별 result enum 반환 | enum 값 자체 | `zlink_errno()` |
| C++ | throw | `zlink_error_t` | `.code()` | `.internal_errno()` |
| Java | throw | `ZlinkException` | `.getCode()` | `.getInternalErrno()` |
| .NET | throw | `ZlinkException` | `.Code` | `.InternalErrno` |
| Go | return | `error` | `.Code()` | `.InternalErrno()` |
| Rust | return (`Result`) | `ZlinkError` | `.code()` | `.internal_errno()` |
| Node | throw | `ZlinkError` | `.code` | `.internalErrno` |
| Python | throw | `ZlinkError` | `.code` | `.internal_errno` |

- `return` 그룹(C / Go / Rust) 은 호출자가 반환값을 명시적으로 검사한다.
  Go 는 `if err != nil`, Rust 는 `match` / `?` 연산자 관용구를 쓴다.
- `throw` 그룹(C++ / Java / .NET / Node / Python) 은 예외를 전파한다. caller
  는 언어별 `try`/`catch` 또는 상위 propagation 에서 처리한다.

#### Error Codes

- C API 는 함수별 typed result enum 을 반환한다.
- 모든 enum 값은 0–703 범위에서 겹치지 않는다.
- 바인딩은 이 코드를 언어별 에러 타입의 `int code` 에 포함시킨다
  (exception 언어는 예외 객체, return-based 언어는 반환 에러 값).
- 전체 enum 정의는
  [errno-map.md](../core/errno-map.md) 를 참조한다.

#### Per-Function Error Type Hierarchy

C API 의 **함수별 typed result enum 구조를 모든 바인딩이 그대로 계승**한다.
단일 `ZlinkException` / `ZlinkError` 만 두면 시그니처만으로 발생 가능한 에러
집합을 알 수 없기 때문이다.

각 바인딩은 8 개의 함수군 에러 타입을 `ZlinkException` / `ZlinkError` 의
하위 타입으로 제공한다. 메서드 시그니처는 해당 함수군의 구체 에러 타입을
노출해야 한다.

| C result enum | 함수군 | 하위 에러 타입 (의미 계약) |
|--------------|--------|--------------------------|
| `zlink_submit_result_t` | send / publish / request submit / reply submit | `SubmitError` |
| `zlink_request_result_t` | request completion (callback) | `RequestError` |
| `zlink_recv_result_t` | recv / subscribe / subscription event / monitor recv / timer recv | `RecvError` |
| `zlink_handler_result_t` | handler 등록 | `HandlerError` |
| `zlink_close_result_t` | close / destroy | `CloseError` |
| `zlink_bind_result_t` | bind | `BindError` |
| `zlink_connect_result_t` | connect / disconnect / unbind | `ConnectError` |
| `zlink_config_result_t` | option set/get, snapshot, poller mutation, proxy, timer config | `ConfigError` |

##### 언어별 네이밍

| 언어 | 최상위 타입 | 하위 타입 네이밍 | 기반 타입 | 예시 시그니처 |
|------|-----------|----------------|----------|-------------|
| C | — | 함수별 typed enum 그대로 | — | `zlink_bind_result_t zlink_bind(...)` |
| C++ | `zlink_error_t` | `zlink::<category>_error_t` (snake_case + `_t`) | `std::runtime_error` 계열 | `void bind(...) /* @throws bind_error_t */` |
| Java | `ZlinkException` | `<Category>Exception` | **unchecked** (`RuntimeException`) | `void bind(...) /* @throws BindException */` |
| .NET | `ZlinkException` | `Zlink<Category>Exception` | `System.Exception` (unchecked; .NET 의 모든 exception 은 unchecked) | `void Bind(...) /* throws ZlinkBindException */` |
| Node | `ZlinkError` | `<Category>Error` | `Error` | `bind(ep): void /* @throws BindError */` |
| Python | `ZlinkError` | `<Category>Error` | `Exception` | `def bind(ep): ...  # raises BindError` |
| Go | `error` (interface) | `*<Category>Error` (typed error struct) | `error` 인터페이스 구현 | `func (s) Bind(ep) error  // returns *BindError` |
| Rust | `ZlinkError` (enum) | `<Category>Error` (variant 또는 별도 타입) | `std::error::Error` 구현 | `fn bind(ep) -> Result<(), BindError>` |

- `Category` 는 `Submit`/`Request`/`Recv`/`Handler`/`Close`/`Bind`/`Connect`/
  `Config` 의 8 개.
- `ZlinkException` / `ZlinkError` 는 모든 하위 타입의 부모로서 "모두 잡기"
  관용구를 유지한다. caller 는 세분화가 필요하면 하위 타입으로, 아니면
  부모로 캐치한다.
- 각 하위 에러 타입은 해당 함수군의 `ErrorCode` 중첩 enum 을 전용으로
  가진다. 다른 함수군 코드는 그 타입에서 표현되지 않는다.
- **Java / .NET 은 unchecked exception 체계를 따른다.** 메서드 시그니처에
  `throws` 절을 강제하지 않는다. 발생 가능 exception 은 Javadoc `@throws`
  / XML doc `/// <exception cref="...">` 로 명시한다.
- Rust / Go 는 반환 타입으로 구체 하위 에러를 선언한다. 동적 언어
  (Node/Python) 는 TSDoc `@throws` / Python docstring `Raises:` 로 동일
  정보를 제공한다.

##### 시그니처 선언 규칙

- 메서드가 단일 함수군 에러만 던질/반환할 수 있으면 구체 하위 타입만
  명시한다.
  - Java: `@throws BindException` (Javadoc, 시그니처에 `throws` 절 불필요)
  - .NET: `/// <exception cref="ZlinkBindException">`
  - C++: `/// @throws bind_error_t` (noexcept 로 표시하지 않음)
  - Node: TSDoc `@throws {BindError}`
  - Python: docstring `Raises: BindError`
  - Go: 반환 타입 문서 `returns *BindError`
  - Rust: 반환 타입 `Result<T, BindError>`
- 메서드가 여러 함수군에 걸칠 경우 (예: service 계층 조합 호출) 공통 부모
  `ZlinkException` / `ZlinkError` 를 선언하고 doc 에 실제 발생 가능한
  하위 타입을 나열한다.
- validation 예외 (language-native `IllegalArgumentException` 등) 는 위 체계
  와 별도이며, `ZlinkException` / `ZlinkError` 계층에 들어가지 않는다.

### Flags Policy

모든 데이터 경로 함수는 `flags` 파라미터를 갖는다.

| 함수 계열 | flags 용도 |
|---|---|
| `send`, `publish`, `reply` | `DONTWAIT` — non-blocking submit |
| `recv`, `subscribe`, `receiveSubscriptionEvent` | `DONTWAIT` — non-blocking receive |
| `request` (callback) | `DONTWAIT` — non-blocking submit |
| `request` (coroutine/async) | flags 없음 — 항상 blocking submit |

- flags 기본값은 `0` (blocking).
- non-blocking 호출에서 데이터 없음 / backpressure 시 언어 관용구에 맞춰
  전달한다 (submit result 코드로 구분 가능).
  - exception 언어 (C++/Java/.NET/Node/Python): 예외를 던진다.
  - return-based 언어 (C/Go/Rust): 에러 반환 (C=result enum,
    Go=`error`, Rust=`Err(E)`).
- 언어별 flags 표현:
  - C / C++: `int flags = 0`
  - Java: `SendFlags flags` overload (기본 blocking 오버로드 유지)
  - .NET: `SendFlags flags = SendFlags.None`
  - Go: `flags SendFlags`
  - Rust: base 함수 (blocking) + `_with_flags` 변형
  - Node: `flags?: SendFlags`
  - Python: `*, flags: int = 0`

### Naming Policy

#### 오버로드 우선, 이름 분화 금지

파라미터로 구분 가능한 변형은 동일한 이름을 사용한다.
별도 이름(`request_callback`, `send_nonblocking` 등)을 만들지 않는다.

```
// GOOD — 같은 이름, 파라미터로 구분
request(parts, timeout)                    // coroutine
request(parts, callback, flags, timeout)   // callback

// BAD — 이름 분화
request(parts, timeout)
request_callback(parts, callback, flags, timeout)
```

#### SPOT 대상 네이밍

SPOT routed 네이밍은 두 축을 함께 가져간다.

- **channel-aware 경로**
  - `send_channel(channel_name, parts, flags)`
  - `request_channel(channel_name, parts, callback, flags, timeout)`
  - `publish(service_name, topic, parts, flags)`
  - `reply_to_spot(dest_node_rid, dest_spot_rid, request_seq, parts, flags)`
  - `reply_to_router(peer_rid, request_seq, parts, flags)`

새 SPOT 바인딩 표면에서는 예전 `send_service` / `request_service` 대신
`send_channel` / `request_channel` / `publish(service_name, ...)` 를 기본 경로로
본다. 직접 주소 지정 경로는 코어가 제공하는 typed routed surface 로서 별도
지원할 수 있다.

언어별 관례에 따라 camelCase / PascalCase / snake_case 로 변환한다.

### Request Policy

request 는 coroutine 변형과 callback 변형 두 가지를 제공한다.
**함수 이름은 둘 다 `request`** 이고 callback 파라미터 유무로 구분한다.

#### Coroutine / Async request

```
async request(parts, timeout = 0) → List<Message>    // throws on any failure
```

- flags 파라미터 없음. submit 은 항상 blocking (코루틴 대기).
- `timeout = 0` 이면 소켓 기본 timeout 을 사용한다. 호출 시 생략 가능.
- submit 실패 시 예외. reply 실패 시 예외 (ETIMEDOUT 등).
- **성공 시 reply payload 의 `List<Message>` 만 반환한다.** caller 는 이미
  자기가 보낸 request 의 routing_id 와 request_seq 를 알고 있으므로
  `Received` 를 되돌려 받을 필요가 없다. 별도 `Reply` 타입은 만들지 않는다.
- multipart reply 가 가능하므로 단일 `Message` 가 아닌 `List<Message>` 를
  반환한다. 단일 part reply 는 `list[0]` 으로 꺼낸다.

#### Callback request

```
request(parts, callback, flags = 0, timeout = 0)    // throws on submit failure
```

- flags 파라미터 있음. `DONTWAIT` 으로 non-blocking submit 가능.
- `timeout = 0` 이면 소켓 기본 timeout 을 사용한다. 호출 시 생략 가능.
- submit 실패 시 언어 관용구로 전달 (exception 언어=예외, return-based
  언어=에러 반환). 실패 시 callback 은 등록되지 않는다.
- submit 성공 시 callback 이 정확히 한 번 호출된다.
  - 성공: `result = OK`, reply parts 포함
  - 실패: `result != OK` (TIMED_OUT 등), parts 는 empty / null / None /
    `Option::None`
- callback 시그니처는 언어 관용구를 따르며 **reply payload 는 `List<Message>`**
  로 전달한다 (`Received` 가 아니다):
  - 공통 패턴 (C++/Java/.NET/Node/Python/Go):
    `(RequestResult result, List<Message> parts)` — 결과 enum 과 parts 리스트
  - Rust 관용구: `FnOnce(Result<Vec<Message>, RequestError>)` — `Result` 타입
    이 Rust 에서 에러 + 값을 표현하는 표준 방식이므로 이 패턴을 허용한다.
    `RequestError::code` 는 `RequestResult` enum 값과 1:1 대응한다.

#### 공통

- `zlink_request_result_t` 전체 정의는
  [errno-map.md](../core/errno-map.md) 를 참조한다.
- Go / Rust 는 exception 이 없으므로 callback request 의 submit 실패도
  return-based 로 처리한다 (Go: `*SubmitError` 반환, Rust:
  `Result<_, SubmitError>` 반환).

## Domain Object Policy
- Java, C#, Go, Rust, Node, Python은 가능하면 `out` 파라미터나 raw tuple보다
  도메인 객체를 우선한다.
- 최소 핵심 도메인 모델:
  - `Message`
  - `RoutingId`
  - `Received`
  - `TopicMessage`
  - `SubscriptionEvent`
  - `SubmitResult` (C / Go / Rust — return-based 언어에서 반환 객체/에러에
    포함. exception 언어에서는 예외 객체 `.code` 로 노출)
- 결과 객체는 payload shape, ownership, optional routing metadata를 함께
  설명해야 한다.
- 편의 기능은 결과 객체 메서드로 둔다.
  - 예: `singlePartOrThrow()`

### 도메인 객체 Canonical Shape (모든 바인딩 공통)

각 도메인 객체는 아래 canonical field/method 집합을 **그대로** 노출한다.
언어별로 명명법(camelCase / snake_case / PascalCase) 만 변환하고,
**필드 타입과 메서드 의미는 바꾸지 않는다.** 언어별 "편의" 라는 이유로
canonical 에 없는 메서드를 추가하거나(`__iter__`, `to_bytes_list` 등) 일부
메서드만 생략하면 안 된다.

#### `TopicMessage`

raw `SUB` / `XSUB` 와 service-aware `Spot subscribe` 의 recv 결과다.
raw pub/sub 는 C API `zlink_subscribe()` 를, Spot subscribe 는
`zlink_spot_subscribe()` 를 바인딩 도메인 객체 하나로 감싼다.

| 구성 | 타입 | 의미 |
|------|------|------|
| `routing_id` | `RoutingId?` (optional) | 송신자 routing id. transport 가 carry 안 하면 null/None/empty |
| `service_name` | `string?` (optional) | Spot subscribe 에서만 설정되는 서비스명. raw `SUB` / `XSUB` 는 null/None/empty |
| `topic` | **`string` (UTF-8)** | 매칭된 topic. **bytes 가 아니다.** |
| `parts` | `List<Message>` / `Vec<Message>` | multipart payload |
| `is_single_part()` | `bool` | `parts.size() == 1` |
| `first_part()` | `Message` | `parts[0]`; 비어있으면 에러/예외 |
| `single_part_or_throw()` | `Message` | `is_single_part()` 면 part 반환, 아니면 에러/예외 |
| `close()` / `Dispose()` / `Drop` | — | 보유 parts 정리. 언어별 lifecycle 관용구 적용 |

규칙:
- `Subscribed` 나 그와 유사한 subclass 를 만들지 않는다. `TopicMessage`
  하나만 노출한다.
- Spot subscribe 결과는 `service_name + topic + parts` 를 함께 노출해야 한다.
  `service_name` 을 버리거나 별도 부가 객체로 분리하면 안 된다.
- `topic` 은 UTF-8 `string` 이다. `bytes` / `byte[]` / `Vec<u8>` 으로
  노출하지 않는다 (내부적으로 raw bytes 로 왔더라도 공개 API 는 decode).
- `RoutingId` 필드는 typed `RoutingId` 하나만 둔다. `RoutingId: string` +
  `RoutingIdValue: RoutingId?` 같은 이중 property 금지.

#### `Received`

PAIR / DEALER / ROUTER / SPOT 의 recv 결과. topic 필드가 없는 점 외에는
`TopicMessage` 와 동일한 편의 메서드 집합 + request-reply 용 `reply()` 를
가진다.

| 구성 | 타입 | 의미 |
|------|------|------|
| `routing_id` | `RoutingId?` | 송신자 routing id (router=peer_rid, spot=source_node_rid) |
| `spot_rid` | `RoutingId?` | SPOT routed recv 에서만 설정 (source_spot_rid) |
| `request_seq` | `uint64?` | request-reply 모드일 때 설정, 아니면 null |
| `parts` | `List<Message>` | multipart payload |
| `is_single_part()` | `bool` | 동일 |
| `first_part()` | `Message` | 동일 |
| `single_part_or_throw()` | `Message` | 동일 |
| `reply(parts, flags?)` | — | request 였을 때만 유효. `request_seq` 없거나 reply context 가 invalid 하면 `SubmitError` |
| `close()` / 동등 | — | 동일 |

`reply()` 규칙:
- **`request_seq` 가 `null` 이면 호출 금지**. 호출 시 `SubmitError`
  계열로 처리한다. `request_seq == 0`, 잘못된 `(routing_id, request_seq)`
  조합 등 invalid reply context 도 같은 submit domain 으로 본다.
- `Received` 가 내부적으로 source socket 참조를 보유한다 (binding 이 recv /
  handler 에서 Received 를 만들 때 주입).
- socket 이 close 된 후 `reply()` 호출하면 `SubmitError(TERMINATED)`.
- 서버 측 사용자가 `(peerRid, requestSeq)` 를 따로 보관할 필요 없음 —
  `Received` 하나로 완결.
- 별도 `router.reply(peerRid, seq, parts)` 저수준 호출도 pull-mode 호환성
  위해 남겨두되, **권장 경로는 `received.reply(...)`**.

#### `SubscriptionEvent`

XPub 이 받는 subscribe/unsubscribe 이벤트와 Spot subscription event recv 결과다.

| 구성 | 타입 | 의미 |
|------|------|------|
| `routing_id` | `RoutingId?` | 구독자 routing id |
| `service_name` | `string?` | Spot subscription event 에서만 설정되는 서비스명. XPub 는 null/None/empty |
| `topic` | `string` (UTF-8) | 구독/해제 topic |
| `subscribed` | `bool` | true=subscribe, false=unsubscribe |

규칙:
- value object 로만 노출한다 (메서드 없음, 필드만).
- `close()` 등 lifecycle 없음 (값 타입).
- Spot subscription event 결과는 `service_name + topic + subscribed` 를
  함께 노출해야 한다.

#### `RoutingId`

Routing id value object. Binary-safe (1-255 bytes).

| 구성 | 타입 | 의미 |
|------|------|------|
| `bytes` / `data` | `bytes` / `byte[]` / `Vec<u8>` / `Buffer` | raw bytes (immutable view) |
| `size` | `int` (1-255) | byte length |
| `from_bytes(bytes)` | static/ctor | 생성자 |
| `to_bytes()` | `bytes` | 원본 바이트 반환 |
| equality / hash | — | 언어별 관용구 (`equals`/`hashCode`, `__eq__`/`__hash__`, `PartialEq+Eq+Hash`) |

규칙:
- **binary-safe value type**. `RoutingId(string value)` 같은 string 전용
  생성자를 기본으로 두지 않는다. string 변환은 편의 메서드 (`to_hex()`,
  `to_string()`) 로만 제공.
- 불변 (immutable). 한 번 생성하면 내용 변경 불가.
- Node 에서는 raw `Buffer` 대신 `RoutingId` 래퍼 타입을 그대로 노출한다.

#### `MonitorEvent`

socket monitor 가 방출하는 이벤트. 모든 바인딩이 **필수 노출**.

| 구성 | 타입 | 의미 |
|------|------|------|
| `event` | `MonitorEventType` (enum) | 이벤트 종류 (CONNECTION_READY, CONNECTED, DISCONNECTED 등) |
| `value` | `uint32` | 이벤트 별 상세 값 (예: DISCONNECTED 시 reason code) |
| `routing_id` | `RoutingId?` | 해당 peer routing id (없는 이벤트는 null) |
| `local_addr` | `string` | 로컬 endpoint |
| `remote_addr` | `string` | 원격 endpoint |

#### `MonitorSnapshot`

socket monitor 가 제공하는 런타임 상태 스냅샷. 모든 바인딩이 **필수 노출**.

| 구성 | 타입 | 의미 |
|------|------|------|
| `source_kind` | enum | 모니터 대상 종류 |
| `state_flags` | `uint32` | 상태 비트마스크 |
| `detail_flags` | `uint32` | 세부 비트마스크 |
| `snd_pending_msgs` | `uint64` | 송신 큐 대기 메시지 수 |
| `rcv_pending_msgs` | `uint64` | 수신 큐 대기 메시지 수 |
| `is_ready()` | `bool` | raw socket monitor source에서만 `state_flags` 의 ready 비트 확인 편의 메서드 |

#### `ServiceEvent`

Discovery `ServiceMonitor` 가 방출하는 이벤트.
모든 바인딩이 **필수 노출**.

| 구성 | 타입 | 의미 |
|------|------|------|
| `service_kind` | enum | `ZLINK_SERVICE_TYPE_SPOT`, `SOCKET` 등 |
| `event_type` | enum | `UP`, `DOWN`, `PROVIDERS_CHANGED`, `ERROR` 등 |
| `status` | `uint32` | status code |
| `error_code` | `uint32` | 에러 시 errno |
| `value` | `uint64` | 이벤트별 값 |
| `detail_flags` | `uint32` | 세부 플래그 |
| `service_name` | `string` | 서비스명 |
| `endpoint` | `string` | 엔드포인트 |
| `routing_id` | `RoutingId?` | peer routing id |
| `subject` | `string` | subscribe subject (topic) |
| `subject_kind` | enum | subject 종류 |

#### 서비스 계층 엔트리 객체

아래는 service-layer snapshot/query 에서 반환되는 value object 들.
모든 바인딩이 **필드 목록을 spec 에 명시**해야 한다 (C 구조체를 그대로
노출하면 안 되며 언어별 named field 로 래핑).

- `MemberPeerEntry` — discovery 가 제공하는 멤버 peer 정보.
  `admissionState` (또는 언어 관례의 동등 필드) 를 포함해야 한다.
- `RegistryTopologyEntry` — registry 의 topology 엔트리
- `RegistryServiceSummaryEntry` — registry service summary 엔트리
- `SpotNodeStatus` — spot node 상태 스냅샷
- `SpotNodePeerEntry` — spot node peer 엔트리.
  `admissionState` 를 포함해야 한다.
- `SpotNodeSubjectEntry` — spot node subject 엔트리

각 spec 은 이들 타입의 필드를 표 또는 코드 블록으로 명시한다. `Cpp` 는
raw `zlink_*_t` 구조체를 바인딩 API 표면으로 노출하지 않고 `class
<name>_t { ... }` 형식으로 래핑한다.

위 canonical 을 벗어난 추가 메서드/필드는 정책 위반이다. 언어별 spec 에서
누락이 발견되면 canonical 기준으로 채워 넣고, 추가된 비표준 메서드는 삭제한다.

## Socket Type Capability Policy
- 소켓 타입별 능력은 타입 자체에만 노출한다.
- 관련 없는 소켓은 관련 없는 함수에 접근할 수 없어야 한다.
  - 예: `PairSocket`에 publish/subscribe/xpub control surface 금지
  - 예: `StreamSocket`에 일반 connect surface 금지
- 소켓 타입별 option도 타입별 capability facade로만 노출한다.

### 소켓 클래스 네이밍/구조 규칙 (중요)
- **소켓 클래스 이름은 core C API 의 socket 타입 이름을 그대로 따른다**:
  `PairSocket`, `PubSocket`, `SubSocket`, `XPubSocket`, `XSubSocket`,
  `DealerSocket`, `RouterSocket`, `StreamSocket`. 바인딩이 임의로 이름을
  바꾸거나 동의어(`ClientSocket`, `BrokerSocket` 등)를 추가하면 안 된다.
- **소켓의 기능 함수(`send`, `recv`, `request`, `reply`, `publish`,
  `subscribe`, `on*` 핸들러 등)는 소켓 클래스의 메서드로 직접 노출한다**.
  단일 함수 또는 좁은 역할(예: request-reply) 만을 위한 별도 wrapper/
  "helper" 클래스(`RequestDealer`, `RequestRouter`, `DealerClient`,
  `RouterRequester` 등) 를 만들지 않는다.
  - 이유 1: C API 는 `zlink_dealer_request()` / `zlink_router_request()` /
    `zlink_router_reply()` 를 raw socket handle 위에 직접 두는 계약이다.
    바인딩 표면이 이 구조를 유지해야 core ↔ 바인딩 대응이 1:1 로 유지된다.
  - 이유 2: wrapper 클래스는 "래핑된 소켓을 또 하나 들고 다녀야 하는"
    중복 lifecycle 을 만든다.
  - 이유 3: 이름에서 역할이 반전돼 읽히기 쉬움 (`RequestDealer` →
    "requests 를 dealing" 으로 오독).
- Future/coroutine 브릿지 같은 구현 상태(pending map 등)는 소켓 클래스
  내부에 두고, 외부로는 메서드만 노출한다.
- 예외는 **서로 다른 소켓 타입을 조합**하는 service-layer surface 뿐이다
  (예: `Spot`, `SpotNode`, `Registry`, `Discovery`, `RegistryQueryClient`).
  이들은 단일 소켓 함수 wrapper 가 아니라 독립된 service 계약이다.
- 이 규칙은 전 바인딩(C++/Java/.NET/Node/Python/Go/Rust) 에 동일하게
  적용되며, spec 파일에서 위반이 발견되면 **즉시 수정 대상**이다.

### Socket Capability Matrix
- 이 표는 `core/include/zlink.h` C API를 기준으로 각 소켓 타입이 가져야 할
  능력을 정의한다.
- 각 바인딩은 이 표를 정답으로 삼아 surface test를 작성한다.
- `Y`는 해당 능력을 반드시 public API로 노출해야 함을 의미한다.
- `—`는 해당 능력을 public API로 노출하면 안 됨을 의미한다.

#### Connection Capabilities

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `bind` | Y | Y | Y | Y | Y | Y | Y | Y |
| `unbind` | Y | Y | Y | Y | Y | Y | Y | Y |
| `connect` | Y | Y | Y | Y | Y | Y | Y | — |
| `disconnect` | Y | Y | Y | Y | Y | Y | Y | — |

#### Send Capabilities

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `send` | Y | Y | — | — | — | — | — | — |
| `send(routingId)` | — | — | Y | — | — | — | — | Y |
| `publish` | — | — | — | Y | — | Y | — | — |

#### Receive Capabilities

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `recv` | Y | Y | Y | — | — | — | — | Y |
| `subscribe` | — | — | — | — | Y | — | Y | — |
| `receiveSubscriptionEvent` | — | — | — | — | — | Y | — | — |

#### Subscription Management

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `setSubscription` | — | — | — | — | Y | — | Y | — |
| `unsetSubscription` | — | — | — | — | Y | — | Y | — |

#### Callback Capabilities

| Capability | Pair | Dealer | Router | Pub | Sub | XPub | XSub | Stream |
|---|---|---|---|---|---|---|---|---|
| `onPacket` | — | — | — | — | — | — | — | Y |
| `onSubscribe` | — | — | — | — | — | — | — | — |
| `onSendReady` | Y | Y | Y | Y | — | Y | — | Y |

`STREAM` public surface 는 최소 `recv` 와 `onPacket` 을 제공해야 한다.
일부 바인딩은 raw direct callback `onReceive` 를 추가로 노출할 수 있다.
이미 한 수신 모드가 걸려 있는 상태에서 다른 모드 attach 를 시도하면
`HandlerResult::BUSY` (또는 동등한 `EBUSY`) 를 반환한다.

#### Typed Option Capabilities

| Option Facade | 적용 소켓 |
|---|---|
| Common options (linger, HWM, timeout 등) | 전체 |
| Router options (mandatory, handover, probe, connectRoutingId) | Router |
| Dealer options (probe) | Dealer |
| Stream options (notify) | Stream |
| Pub options (verbose, verboser, noDrop, manual 등) | Pub, XPub |
| Sub options (topicsCount) | Sub, XSub |
| RoutingId (set/get) | Dealer, Router, Stream |
| `attachDiscovery` | Dealer, Router, Pub, Sub |

- `attachDiscovery` 후 해당 소켓에서 `connect`, `disconnect`, `unbind`,
  `close`는 차단된다. Discovery `close`가 소켓 lifecycle을 관리한다.

## Language Spec File Compliance Rules

각 언어별 스펙 파일(`doc/spec/bindings/{lang}/README.md`)은 아래 규칙을
반드시 준수해야 한다. 스펙 파일 작성이나 리뷰 시 이 체크리스트를 적용한다.

### Capability Matrix 정합성
- 각 소켓 타입 클래스는 위 Socket Capability Matrix에서 `Y`인 능력만
  public 메서드로 노출해야 한다.
- `—`인 능력은 해당 소켓 타입 클래스에 존재하면 안 된다.
- 특히 다음 위반이 자주 발생하므로 주의한다:
  - `RouterSocket` / `StreamSocket`에 plain `send` (routingId 없는 send) 금지 —
    반드시 `send(routingId, ...)` 형태여야 한다.
  - `PairSocket` / `XPubSocket` / `StreamSocket` / `XSubSocket`에 `attachDiscovery` 금지 —
    Dealer, Router, Pub, Sub에만 허용된다.
  - `XPubSocket`에 `onSubscribe` 콜백 금지 —
    XPub는 `receiveSubscriptionEvent`만 허용된다.

### Routed Send 필수 인자
- `RouterSocket`과 `StreamSocket`의 send는 routingId를 **필수** 인자로 받아야 한다.
- routingId를 optional/default 파라미터로 만들면 plain send가 가능해지므로 금지한다.

### Blocking Send 반환값
- blocking `send` / `publish`는 성공 시 반환값 없이 정상 반환하고,
  실패 시 예외 또는 언어별 오류 경로로 전달해야 한다.
- 상태 코드(int, number 등)를 반환하는 방식은 금지한다.

### 언어별 네이밍 일관성
- 한 바인딩 내에서 네이밍 컨벤션이 혼재되면 안 된다.
  - Python: 모든 public API는 `snake_case`. (프로퍼티 포함)
  - Java: `camelCase` 메서드, `PascalCase` 클래스.
  - C#: `PascalCase` 전체.
  - Go: `PascalCase` exported.
  - Rust: `snake_case` 메서드, `PascalCase` 타입.
  - C++: `snake_case` 메서드, `_t` 접미사 타입.
  - Node/TypeScript: `camelCase` 메서드, `PascalCase` 클래스.

### C API 전수 커버리지
- 각 언어별 스펙 파일은 `core/include/zlink.h`의 모든 ZLINK_EXPORT 함수에
  대응하는 바인딩 인터페이스를 빠짐없이 기술해야 한다.
- 대응은 1:1이 아닐 수 있다 (옵션 함수 그룹이 하나의 typed facade로 통합되는 등).
- 그러나 C API의 어떤 기능도 바인딩 스펙에서 누락되면 안 된다.
- 새로운 C API가 `zlink.h`에 추가되면 모든 언어 스펙 파일도 함께 갱신해야 한다.

## Service Layer Policy
- 이 섹션은 소켓 레이어 위에 올라가는 서비스 계층(Spot, Discovery, Registry)의
  public API 정책을 정의한다.
- 서비스 계층도 소켓 계층과 동일한 POSD 원칙, naming policy, error policy,
  ownership policy, testing policy를 따른다.
- 서비스 계층의 기준은 `core/include/zlink.h`의 Spot/Discovery/Registry C API다.

### Spot / SpotNode Lifecycle (POSD 원칙)

- **`SpotNode` 가 lifecycle 소유자**다. `Spot` 은 그 위의 pub/sub facade 로,
  `SpotNode` 가 살아 있는 동안만 유효하다.
- `Spot` 은 독립 생성자로 만들지 않는다. **`SpotNode.createSpot(...)` 등
  factory 메서드로 생성**한다. 이름은 언어 관용구대로 (`spot_node.new_spot`,
  `spotNode.createSpot`, 등).
- `Spot` 생명은 부모 `SpotNode` 에 바인드된다.
  - `spot.close()` — Spot 만 끝내고 node 는 유지
  - `spotNode.close()` — node 와 그 아래 모든 live Spot 을 함께 정리
    (cascading close)
- 사용자가 `Spot` 과 `SpotNode` 의 close 순서를 수동으로 조합할 필요를
  제거한다. 바인딩이 `SpotNode.close()` 에서 child spots 를 선처리한 후
  node 를 내린다.
- C API 의 raw `zlink_spot_new(...)` + `zlink_spot_node_new(...)` 조합을
  바인딩 public 생성자로 그대로 노출하지 않는다. 반드시 `SpotNode` 중심의
  factory 패턴으로 싼다.

### Service Layer Introspection Surface Tiers

서비스 계층의 introspection / snapshot / entry 타입은 **사용 빈도에 따라
두 계층으로 구분**한다. 바인딩 spec 은 이 구분을 반영한다.

- **Primary (핵심)**: 일반 사용자가 자주 쓰는 snapshot/query surface.
  `bindings/<lang>/README.md` 의 상위 섹션에 기술한다.
  - `MemberPeerEntry` (discovery.memberPeers 결과)
  - `SpotNodeStatus` (spot node 상태)
  - `RegistryTopologyEntry` (registry.topologySnapshot 결과)

- **Advanced / Diagnostic (진단용)**: 디버깅 / 운영 모니터링 등 특수 용도.
  spec 에서 "Advanced" 또는 "Diagnostic" 하위 섹션으로 분리 기술한다.
  - `RegistryServiceSummaryEntry`, `RegistryStatus`
  - `SpotNodePeerEntry`, `SpotNodeSubjectEntry`
  - 각종 filter 타입 (`RegistryTopologyFilter`,
    `RegistryServiceSummaryFilter`, `SpotNodePeerFilter`,
    `SpotNodeSubjectFilter`)

Primary 타입만으로 기본 사용 시나리오가 성립해야 한다. Advanced 타입을
배우지 않고도 "서비스 등록 / 검색 / 연결" 흐름이 완결돼야 한다.

### `zlink_errno()` Public Exposure

- 바인딩은 **raw `zlink_errno()` / `zlinkErrno()` 함수를 public 으로 노출하지
  않는다**. 에러 상세는 **언제나 에러 타입의 `internalErrno` /
  `internal_errno` 필드**로만 접근한다.
- 사용자가 에러 조사 시 "가끔 `ZlinkException.getCode()` 쓰고 가끔 `Zlink.
  errno()` 쓰는" 이중 경로를 만들지 않는다 — 한 진입점으로 통일.
- 바인딩 내부 구현이 `zlink_errno()` 를 호출해 예외 객체에 채워 넣는 건
  허용 (내부 해석용). public surface 에만 금지 적용.
- `Zlink.strerror(errno)` 같은 message lookup 유틸은 convenience 로 남겨두되,
  raw `errno()` accessor 는 private 또는 삭제.

### Service Layer Architecture
- 서비스 계층은 다섯 개의 컴포넌트로 구성된다.

```
Registry (서버)
  ├── bind (PUB + ROUTER endpoint)
  ├── cluster: addPeer (다른 Registry와 동기화)
  ├── config: setId, setHeartbeat, setBroadcastInterval
  └── introspection: statusSnapshot, serviceSummarySnapshot,
      memberPeers, topologySnapshot, topologyQuery

Discovery (클라이언트 — 서비스 뷰)
  ├── connectRegistry (Registry에 연결)
  ├── metadata: setValue/getValue, setMetadata/getMetadata
  ├── introspection: memberPeers, memberPeerMetadata
  └── lifecycle: destroy → 연결된 모든 participant 종료

SpotNode (channel-aware topology runtime)
  ├── bind (endpoint)
  ├── raw mesh: connectPeer / disconnectPeer
  ├── channel attach: attachDiscovery / attachChannelDealer /
  │   attachChannelDealerManual / attachPubIngress
  ├── introspection: statusSnapshot, peersSnapshot, peersQuery,
  │   subjectsSnapshot
  └── TLS: setTlsServer, setTlsClient

Spot (channel-aware facade — SpotNode 위에 올라감)
  ├── publish(service_name, topic, ...)
  ├── subscribe
  ├── sendChannel / requestChannel
  ├── setSubscription / unsetSubscription
  ├── onDispatchEvent / onRoutedReceive / onSendReady
  └── close (node는 살아 있음)

RegistryQueryClient (원격 토폴로지 조회)
  ├── connect (Registry endpoint)
  ├── snapshot (필터 기반 조회)
  └── close
```

### SpotNode Capability Matrix

| Capability | SpotNode |
|---|---|
| `bind` | Y |
| `connectPeer` | Raw mesh only |
| `disconnectPeer` | Raw mesh only |
| `attachChannelDealer` | Y |
| `attachChannelDealerManual` | Y |
| `attachPubIngress` | Y |
| `attachDiscovery` | Y |
| `setTlsServer` | Y |
| `setTlsClient` | Y |
| `statusSnapshot` | Y |
| `peersSnapshot` | Y |
| `peersQuery` | Y |
| `subjectsSnapshot` | Y |
| `close` | Y |

- SpotNode는 data plane API(`send`/`recv`/`publish`/`subscribe`)를 직접
  노출하지 않는다.
- data plane은 `Spot` facade를 통해서만 접근한다.
- `connectPeer`/`disconnectPeer`는 raw peer topology 전용 control path 다.
- channel-aware public 설명에서는 `attachDiscovery` /
  `attachChannelDealer` / `attachChannelDealerManual` /
  `attachPubIngress` 를 중심으로 다룬다.

### Spot Capability Matrix

| Capability | Spot |
|---|---|
| `publish(service_name, topic, ...)` | Y |
| `subscribe` | Y |
| `receiveSubscriptionEvent` | Y |
| `setSubscription` / `unsetSubscription` | Y |
| `sendChannel` / `requestChannel` | Y |
| `replyToSpot` | Optional typed routed reply surface |
| `replyToRouter` | Optional typed routed reply surface |
| `onDispatchEvent` | Y |
| `onRoutedReceive` | Y |
| `onSendReady` | Y |
| `close` | Y |

- Spot은 소켓 타입이 아니라 SpotNode 위에 올라가는 service-aware facade다.
- Spot routed receive 는 `recv_routed` 또는 동등한 typed recv surface 로
  노출할 수 있다.
- Spot은 `bind`/`connect`를 갖지 않는다 (SpotNode가 담당).
- Spot `close`는 facade만 해제하고 SpotNode는 살아 있다.

### Discovery Capability Matrix

| Capability | Discovery |
|---|---|
| `connectRegistry` | Y |
| `setValue` / `getValue` | Y |
| `setMetadata` / `getMetadata` | Y |
| `memberPeers` | Y |
| `memberPeerMetadata` | Y |
| `close` | Y |

- Discovery는 생성 시 `serviceType`과 `serviceName`을 고정한다.
- 이후 변경할 수 없다.
- `close` 시 연결된 모든 participant(SpotNode 등)가 종료된다.
- Discovery는 data plane이 아니라 서비스 등록/발견 plane이다.

### Registry Capability Matrix (`Target`)
- 이 matrix는 `Target`이다. 전체 바인딩 필수가 아니며, 구현하는 바인딩만
  아래 표를 따른다.

| Capability | Registry |
|---|---|
| `bind` (pubEndpoint, routerEndpoint) | Y |
| `setId` | Y |
| `addPeer` | Y (클러스터 동기화) |
| `setHeartbeat` (interval, timeout) | Y |
| `setBroadcastInterval` | Y |
| `statusSnapshot` | Y |
| `serviceSummarySnapshot` | Y |
| `memberPeers` | Y |
| `memberPeerMetadata` | Y |
| `topologySnapshot` | Y |
| `topologyQuery` | Y |
| `close` | Y |

- Registry는 서버 측 컴포넌트다.
- PUB endpoint(서비스 목록 브로드캐스트)와 ROUTER endpoint(등록 수신)를
  동시에 바인드한다.
- cluster 모드에서는 `addPeer`로 다른 Registry와 동기화한다.

### RegistryQueryClient Capability Matrix (`Target`)
- 이 matrix는 `Target`이다. 전체 바인딩 필수가 아니며, 구현하는 바인딩만
  아래 표를 따른다.

| Capability | RegistryQueryClient |
|---|---|
| `connect` | Y |
| `snapshot` (필터 기반) | Y |
| `close` | Y |

- 원격에서 Registry 토폴로지를 조회하기 위한 클라이언트다.
- Discovery와 별개로 사용할 수 있다.

### Service Monitor Policy
- Discovery만 ServiceMonitor를 열 수 있다.
- SPOT(SpotNode, Spot)은 ServiceMonitor를 노출하지 않는다.
  SPOT 관찰은 `statusSnapshot`, `peersSnapshot`, `subjectsSnapshot` API를 사용한다.
- ServiceMonitor는 소켓의 SocketMonitor와 별도 타입이다.
- SocketMonitor와 ServiceMonitor는 둘 다 기본이 recv model 이다.
- `onEvent(handler)`를 호출하면 callback-only model로 일방 전환된다.
  이후 `recv()`는 `BUSY` / `EBUSY` 계열 오류를 반환해야 한다.
- `snapshot()`은 recv model과 callback-only model 양쪽에서 모두 동작해야 한다.
- ServiceMonitor API:
  - `recv()`: blocking/non-blocking event 수신 (flags로 제어)
  - `onEvent(handler)`: callback 등록
  - `snapshot()`: 현재 상태 스냅샷
  - `close()`
- ServiceMonitor event는 typed event surface로 노출해야 한다.
- raw int event mask만 노출하면 안 된다.
- ServiceMonitor `onEvent` callback 해제 정책:
  - 소켓 callback과 동일 — `null`/`None` 설정 해제 금지
  - callback 해제는 `close()`로만 이루어진다
- SocketMonitor callback 해제 정책도 동일하다.
  - SocketMonitor는 callback 등록 API가 없으면 해당 없음
  - callback 등록 API가 있는 경우 `close()`로만 해제한다
- ServiceMonitor event 종류 (Discovery 전용):
  - `error`, `serviceUp`, `serviceDown`, `providersChanged`, `closed`

### Service Layer Domain Objects
- 서비스 계층도 domain object를 사용해야 한다.
- 최소 핵심 domain object:
  - `ServiceEvent`: ServiceMonitor에서 수신하는 이벤트
  - `MonitorSnapshot`: monitor 상태 스냅샷
  - `SpotNodeStatus`: SpotNode 상태 (state, peer count 등)
  - `MemberPeerEntry`: 서비스 멤버 peer 정보
  - `RegistryTopologyEntry`: 토폴로지 엔트리
  - `RegistryStatus`: Registry 상태
  - `RegistryServiceSummaryEntry`: 서비스 요약 엔트리
- Advanced / Diagnostic domain object:
  - `SpotNodePeerEntry`: peer 정보
  - `SpotNodeSubjectEntry`: subject 정보
- 필터 객체:
  - `SpotNodePeerFilter`: peer 조회 필터
  - `SpotNodeSubjectFilter`: subject 조회 필터
  - `RegistryServiceSummaryFilter`: 서비스 요약 조회 필터
  - `RegistryTopologyFilter`: 토폴로지 조회 필터
- enum/value object:
  - `ServiceType`: `SPOT`, `SOCKET`
  - `ServiceRole`: `SPOT`, `ROUTER`, `DEALER`, `PUB`, `SUB`
  - `SpotNodeState`: `IDLE`, `CONNECTING`, `PARTIAL_READY`, `READY`, `ERROR`
  - `MonitorSourceKind`: `SOCKET`, `SPOT_PUB`, `SPOT_SUB`, `SPOT_NODE`
  - `SpotPeerSource`: `MANUAL`, `DISCOVERY`, `MIXED`
  - `SpotPeerState`: `CONFIGURED`, `CONNECTING`, `CONNECTED`
  - `RegistryState`: `IDLE`, `ACTIVE`, `DEGRADED`, `ERROR`
  - `TopologySource`: `MANUAL`, `DISCOVERY`, `REGISTRY`
  - `TopologyState`: `DISCOVERED`, `CONNECTING`, `READY`, `LOST`,
    `ERROR`, `STOPPED`
- `MonitorSnapshot.isReady()` 또는 동등한 편의 accessor는 raw socket
  monitor source에서만 ready 의미를 해석한다. `SPOT_PUB`, `SPOT_SUB`
  source에서는 ready bit를 SPOT readiness로 확장 해석하면 안 된다.

### Service Layer Naming Policy
- 서비스 계층도 Naming Policy를 따른다.
- 허용되는 변형: 케이싱 변형, overload 불가 언어의 최소 접미사.
- 단어 교체, 생략, 대체는 금지한다.
- 규칙 상세는 Naming Policy 본문과 동일하다.

#### Service Layer Canonical Name Table

| Component | Canonical Name | 설명 |
|---|---|---|
| SpotNode | `bind` | endpoint 바인드 |
| SpotNode | `connectPeer` | raw peer 연결 |
| SpotNode | `disconnectPeer` | raw peer 연결 해제 |
| SpotNode | `attachChannelDealer` | Discovery 기반 channel DEALER attach |
| SpotNode | `attachChannelDealerManual` | 수동 channel DEALER attach |
| SpotNode | `attachPubIngress` | 외부 PUB ingress attach |
| SpotNode | `attachDiscovery` | Discovery 연결 |
| SpotNode | `subjectsSnapshot` | subject 목록 스냅샷 |
| SpotNode | `setTlsServer` | TLS 서버 설정 |
| SpotNode | `setTlsClient` | TLS 클라이언트 설정 |
| SpotNode | `statusSnapshot` | 노드 상태 스냅샷 |
| SpotNode | `peersSnapshot` | peer 목록 스냅샷 |
| SpotNode | `peersQuery` | peer 필터 조회 |
| SpotNode | `subjectsSnapshot` | subject 목록 스냅샷 |
| SpotNode | `close` | 노드 종료 |
| Spot | `publish(service_name, topic, ...)` | 서비스 단위 토픽 발행 |
| Spot | `subscribe` | 토픽 구독 수신 |
| Spot | `receiveSubscriptionEvent` | service-aware 구독 이벤트 수신 |
| Spot | `setSubscription` / `unsetSubscription` | 구독 필터 관리 |
| Spot | `sendChannel` / `requestChannel` | channel 단위 routed 송신 / 요청 |
| Spot | `onDispatchEvent` | topic/routed/timer readable 알림 |
| Spot | `onRoutedReceive` | direct routed callback |
| Spot | `onSendReady` | send ready callback |
| Spot | `close` | facade 종료 |
| Discovery | `connectRegistry` | Registry에 연결 |
| Discovery | `setValue` / `getValue` | 서비스 값 설정/조회 |
| Discovery | `setMetadata` / `getMetadata` | 서비스 메타데이터 설정/조회 |
| Discovery | `memberPeers` | 멤버 peer 목록 조회 |
| Discovery | `memberPeerMetadata` | 멤버 peer 메타데이터 조회 |
| Discovery | `close` | Discovery 종료 (participant 포함) |
| Registry | `bind` | PUB + ROUTER endpoint 바인드 |
| Registry | `setId` | Registry ID 설정 |
| Registry | `addPeer` | 클러스터 peer 추가 |
| Registry | `setHeartbeat` | heartbeat interval/timeout 설정 |
| Registry | `setBroadcastInterval` | 브로드캐스트 주기 설정 |
| Registry | `statusSnapshot` | Registry 상태 스냅샷 |
| Registry | `serviceSummarySnapshot` | 서비스 요약 스냅샷 |
| Registry | `memberPeers` | 멤버 peer 목록 조회 |
| Registry | `memberPeerMetadata` | 멤버 peer 메타데이터 조회 |
| Registry | `topologySnapshot` | 토폴로지 스냅샷 |
| Registry | `topologyQuery` | 토폴로지 필터 조회 |
| Registry | `close` | Registry 종료 |
| RegistryQueryClient | `connect` | Registry에 연결 |
| RegistryQueryClient | `snapshot` | 토폴로지 스냅샷 (필터 선택) |
| RegistryQueryClient | `close` | 클라이언트 종료 |
| ServiceMonitor | `recv` | event 수신 |
| ServiceMonitor | `onEvent` | event callback |
| ServiceMonitor | `snapshot` | 상태 스냅샷 |
| ServiceMonitor | `close` | monitor 종료 |

### Service Layer Testing Policy
- 서비스 계층은 sample이나 perf에서 직접 검증되지 않는 컴포넌트를 포함한다.
- 특히 Discovery와 Registry는 테스트가 유일한 검증 경로다.
- 래핑 코드라도 FFI 매핑, lifecycle, 타입 변환이 올바른지 반드시 테스트해야 한다.
- 서비스 계층도 Test Matrix와 동일한 카테고리로 테스트한다.

#### Service Layer Surface Tests
- SpotNode capability matrix 정렬 확인
- Spot capability matrix 정렬 확인
- Discovery capability matrix 정렬 확인
- Registry capability matrix 정렬 확인 (구현된 경우)
- RegistryQueryClient capability matrix 정렬 확인 (구현된 경우)
- ServiceMonitor canonical surface 존재 확인 — Discovery 전용 (`recv`,
  `onEvent`, `snapshot`)
- typed domain object 존재 확인 (ServiceEvent, SpotNodeStatus,
  MemberPeerEntry 등)
- typed enum 존재 확인 (ServiceType, ServiceRole, SpotNodeState 등)

#### Service Layer Contract Tests
- SpotNode: create/bind/close lifecycle 누수 없음
- Spot: create/close lifecycle (SpotNode는 살아 있어야 함)
- Discovery: create/connectRegistry/close lifecycle 누수 없음
- Discovery close 시 연결된 participant(SpotNode 등) 종료 확인
- Registry: create/bind/close lifecycle 누수 없음 (구현된 경우)
- RegistryQueryClient: create/connect/close lifecycle (구현된 경우)
- ServiceMonitor: open/close lifecycle 누수 없음 (Discovery 전용)
- 예외/오류 경로에서도 native 리소스가 정리되는지 확인

#### Service Layer Behavior Tests
- SpotNode bind → Spot publish → Spot subscribe 경로 성공
- Spot subscribe → 데이터 없음 시 empty 반환 (non-blocking)
- Spot publish 실패 시 예외 확인
- Spot dispatch event callback 호출 확인
- Spot onSendReady callback 호출 확인
- Spot receiveSubscriptionEvent 경로 확인
- SpotNode attachChannelDealer / attachChannelDealerManual 경로 동작 확인
- SpotNode attachPubIngress 경로 동작 확인
- Discovery connectRegistry → 서비스 등록 경로 성공
- Discovery setValue/getValue round-trip 확인
- Discovery setMetadata/getMetadata round-trip 확인
- Discovery memberPeers 조회 확인
- Registry bind → Discovery connectRegistry → 서비스 발견 경로 성공
  (Registry 구현된 경우)
- Registry statusSnapshot 결과 확인 (구현된 경우)
- Registry topologySnapshot/topologyQuery 결과 확인 (구현된 경우)
- RegistryQueryClient snapshot 결과 확인 (구현된 경우)
- Socket attachDiscovery → connect/disconnect/unbind/close 차단 확인
  (Discovery 지원 시)

#### Service Layer Monitor Tests
- ServiceMonitor blocking recv 성공 경로 (Discovery 전용)
- ServiceMonitor non-blocking recv empty 경로 (Discovery 전용)
- ServiceMonitor onEvent callback 호출 확인 (Discovery 전용)
- ServiceMonitor snapshot 상태 반환 확인 (Discovery 전용)
- Discovery monitor: serviceUp/serviceDown event 수신 확인 (Discovery
  지원 시)

#### Service Layer Introspection Tests
- SpotNode statusSnapshot → SpotNodeStatus 필드 검증
  (state, peerCount, subjectCount 등)
- SpotNode peersSnapshot → SpotNodePeerEntry 목록 검증
- SpotNode peersQuery → 필터 적용 결과 검증
- SpotNode subjectsSnapshot → SpotNodeSubjectEntry 목록 검증
- Registry statusSnapshot → RegistryStatus 필드 검증 (구현된 경우)
- Registry serviceSummarySnapshot → 필터 적용 결과 검증 (구현된 경우)
- Registry memberPeers → MemberPeerEntry 목록 검증 (구현된 경우)
- Registry topologySnapshot → RegistryTopologyEntry 목록 검증 (구현된 경우)
- Discovery memberPeers → MemberPeerEntry 목록 검증
- Discovery memberPeerMetadata → metadata 반환 검증

#### Service Layer Scope for Tests

| Test Category | SpotNode+Spot | Discovery | Registry | QueryClient |
|---|---|---|---|---|
| Surface | Required | Required | 구현 시 Required | 구현 시 Required |
| Contract | Required | Required | 구현 시 Required | 구현 시 Required |
| Behavior | Required | Required | 구현 시 Required | 구현 시 Required |
| Monitor | Required | Required | — | — |
| Introspection | Required | Required | 구현 시 Required | 구현 시 Required |

- service/spot 계열이 없는 바인딩은 이 테스트를 제외할 수 있다.
- 여기서 `SpotNode+Spot` 의 monitor 설명은 socket monitor 기준으로만
  뜻한다. `ServiceMonitor` open surface 를 뜻하지 않는다.

### Service Layer Sample Policy
- Canonical Sample Set에 정의된 서비스 계열 샘플:
  - `spot_recv_sample`: Spot service-aware subscribe / routed recv
  - `spot_callback_sample`: Spot dispatch event callback
  - `monitor_recv_sample`: monitor event 수신 (socket monitor 포함)
- service/spot 계열이 없는 바인딩은 `spot_*` 샘플을 제외할 수 있다.

### Service Layer Scope per Binding
- 모든 바인딩이 서비스 계층 전체를 구현해야 하는 것은 아니다.
- 최소 요구 사항:

| Component | 요구 수준 |
|---|---|
| SpotNode + Spot | 해당 바인딩에 spot 지원이 있으면 Required |
| Discovery | 해당 바인딩에 discovery 지원이 있으면 Required |
| Registry | Target (서버 측 컴포넌트, 전체 바인딩 필수 아님) |
| RegistryQueryClient | Target (조회 전용 클라이언트) |
| ServiceMonitor | Discovery 지원 시 Required (SPOT은 ServiceMonitor 미사용) |

### Callback API Policy
- callback 등록 API는 각 소켓 타입의 capability에 따라 노출한다.
- 위 Callback Capabilities 표가 기준이다.
- canonical callback 이름:
  - `onReceive`: raw `STREAM` direct fragment callback
    (해당 바인딩이 이 optional extension 을 public 으로 노출할 때만 사용)
  - `onDispatchEvent`: SPOT unified readable notification callback
  - `onRoutedReceive`: SPOT direct routed callback
  - `onSendReady`: send ready 상태 callback
- callback payload shape는 direct receive와 동일해야 한다.
  - `onReceive` callback payload = raw `STREAM` recv shape
    (binding-specific optional extension)
  - `onRoutedReceive` callback payload = SPOT routed recv shape
- callback 등록 후 동일 subject에 대한 direct recv/subscribe는 native 계약에
  따라 차단된다 (EBUSY).
- callback을 `null`/`None`으로 설정하여 해제하는 것은 허용하지 않는다.
  callback 해제는 socket close로만 이루어진다.

## Core API Additions

이 섹션은 `core/include/zlink.h`에 추가된 core API를 정리한다.
각 바인딩은 이 API를 언어별 typed surface로 노출해야 한다.

### Request-Reply Policy

> 언어별 인터페이스 시그니처와 사용 예는
> `cpp/`, `java/`, `dotnet/`, `node/`, `python/`, `go/`, `rust/` 를 참조한다.
> 구현 기준 상세는
> [`doc/plan/spot-refactor/SOCKET_REQUEST_REPLY_API_SPEC.md`](../../plan/spot-refactor/SOCKET_REQUEST_REPLY_API_SPEC.md),
> [`doc/plan/spot-refactor/ZMP_REQUEST_REPLY_PROTOCOL.md`](../../plan/spot-refactor/ZMP_REQUEST_REPLY_PROTOCOL.md),
> [`doc/plan/spot-refactor/SPOT_ROUTED_MESSAGE_SPEC.md`](../../plan/spot-refactor/SPOT_ROUTED_MESSAGE_SPEC.md)
> 를 따른다.

#### 설계 원칙

- request-reply 는 ZMP protocol envelope 로 처리한다.
  `zlink_msg_t` 에 request 표시를 붙이는 방식은 사용하지 않는다.
- dispatch, pending map, timeout, reply 매칭은 core C API 에서 처리한다.
  바인딩은 이 로직을 다시 구현하지 않는다.
- core 는 callback 기반 비동기 모델을 제공한다.
  바인딩은 callback 위에 coroutine/future/promise 표면을 얹는다.
- `request()` 는 thread blocking API 가 아니다.
- request-reply 는 Router/Dealer 소켓과 SPOT 의 기능 확장이다.
  별도 추상 레이어가 아니라 기존 표면에 capability 를 얹는다.

#### Public surface 에 두지 않는 API

message-level request-reply marker API 와 per-message metadata API 는
public surface 의 일부가 아니다. 바인딩은 다음 함수나 상수를 public 으로
노출하지 않고, `Message` 객체 안에 request marker 상태를 두지 않는다.

- `zlink_msg_set_request`, `zlink_msg_set_reply`, `zlink_msg_get_request_info`
- `zlink_msg_set_metadata`, `zlink_msg_get_metadata`, `zlink_msg_clear_metadata`

#### 유효한 Request-Reply 조합

**Socket 경로:**

| 요청자 | 응답자 | 가능 | reply 경로 |
|--------|--------|------|-----------|
| Dealer | Router | Y | Router 가 Dealer 의 routing_id 로 회신 |
| Router | Router | Y | 서로 routing_id 로 회신 |
| Dealer | Dealer | **N** | 양쪽 다 routing_id 없음 |
| Router | Dealer | **N** | Dealer 가 특정 peer 에 회신 불가 |

**SPOT 경로:**

| 요청자 | 응답자 | 가능 | reply 경로 |
|--------|--------|------|-----------|
| Spot | Spot | Y | 상대 주소 + request_seq 로 회신 |
| Spot | Router | Y | Spot 이 Router 에 request, Router 가 Spot 에 reply |
| Router | Spot | Y | Router 가 Spot 에 request, Spot 이 Router 에 reply |

`DealerSocket.request()` 연결 제약:
- 연결 대상은 전부 Router 여야 한다.
  Dealer 에 Router 와 Dealer 가 섞이면 request 가 실패할 수 있다.
- 바인딩은 이 제약을 런타임에 검증하지 않는다. 사용자 책임이며 API 문서에 명시한다.

#### C API 표면

**공통 타입:**

```c
typedef void (*zlink_reply_handler_fn)(
    zlink_request_result_t result_, zlink_msg_t *parts, size_t part_count, void *userdata);

```

callback 으로 전달된 `parts` 는 borrowed view 다.
callback 반환 시점까지만 유효하다. 밖에서 유지하려면 복사한다.

**Socket API:**

```c
int zlink_dealer_request(void *dealer, zlink_msg_t *parts, size_t part_count,
    zlink_reply_handler_fn handler, void *userdata, zlink_send_flags_t flags,
    uint32_t timeout_ms);

int zlink_router_request(void *router, const zlink_routing_id_t *peer_rid,
    zlink_msg_t *parts, size_t part_count, zlink_reply_handler_fn handler,
    void *userdata, zlink_send_flags_t flags, uint32_t timeout_ms);

int zlink_router_reply(void *router, const zlink_routing_id_t *peer_rid,
    uint64_t request_seq, zlink_msg_t *parts, size_t part_count);

int zlink_router_recv(void *router, const zlink_routing_id_t **peer_rid_out,
    uint64_t *request_seq_out, zlink_msg_t **parts_out,
    size_t *part_count_out, int flags);
```

**SPOT API:**

```c
int zlink_spot_reply_spot(void *spot, ...);
int zlink_spot_reply_router(void *spot, ...);
int zlink_router_request_spot(void *router, ...);
int zlink_router_reply_spot(void *router, ...);
int zlink_router_send_spot(void *router, ...);
int zlink_spot_handler(void *spot, ...);
int zlink_spot_recv(void *spot, ...);
int zlink_router_recv(void *router, ...);
int zlink_spot_send_channel(void *spot, ...);
int zlink_spot_request_channel(void *spot, ...);
int zlink_spot_publish(void *spot, ...);
int zlink_spot_subscribe(void *spot, ...);
```

전체 시그니처는 `core/include/zlink.h` 를 참조한다.

#### 수신 Dispatch 모델

core 가 request-reply dispatch 를 처리한다. 바인딩은 dispatch owner 를 구현하지 않는다.

- `request_seq = 0` 이면 ordinary message.
- `request_seq != 0` 이면 request-reply message.
- core 가 pending map 에서 `source_node_rid + request_seq` 로 매칭한다.
- 매칭 실패한 reply (stray/late reply) 는 drop 한다.
- ROUTER 는 generic `zlink_recv()` 대신 `zlink_router_recv()` typed surface 를
  사용한다. generic `zlink_recv()` 호출 시 `EOPNOTSUPP`.
- ROUTER 의 routed 수신 plane 은 **단일 표면**이다. 일반 ROUTER 트래픽과
  spot-origin routed 트래픽 모두 `zlink_router_recv()` 하나로 받는다.
  `source_spot_rid` 가 `NULL` 이면 일반 ROUTER 트래픽, 채워져 있으면
  spot-origin 트래픽이다.

#### Request API 변형

request 는 두 층으로 나눈다.

- coroutine / await request
- callback completion request

coroutine / await request 는 완성된 request-reply 연산으로 보고, 기본 이름은
항상 `request` 로 둔다.

callback completion request 는 submit 단계가 따로 있으므로, `send/trySend` 와 같은
수준의 쌍을 가져야 한다.

- blocking submit callback request
- nonblocking submit callback request

오버로드가 가능한 언어는 아래 형태를 권장한다.

- `request(parts, timeout)`
- `request(parts, callback, timeout)`
- `tryRequest(parts, callback, timeout)`

오버로드가 어려운 언어는 같은 의미를 아래처럼 짝으로 맞춘다.

- `RequestCallback(...)`
- `TryRequestCallback(...)`

C binding 은 예외다. C 는 public `try_*` request family 를 만들지 않고, 기존
`zlink_*_request(..., flags, timeout)` 형태를 유지한다. 즉 C 에서는 callback
request submit 제어를 별도 함수명이 아니라 flags 로 표현한다.

| | `request(parts, timeout)` | `request(parts, callback, timeout)` | `tryRequest(parts, callback, timeout)` |
|---|---|---|---|
| submit | blocking / suspending | blocking submit | nonblocking submit |
| reply 전달 | 반환값 `List<Message>` | callback | callback |
| submit 실패 시 | 예외 또는 에러 반환 | 예외 또는 에러 반환 (callback 등록 안 됨) | backpressure는 `false`/동등 표현, 그 외는 예외 또는 에러 |
| reply 실패 시 | 예외 또는 에러 반환 (ETIMEDOUT 등) | callback (`result != OK`) | callback (`result != OK`) |
| flags | 없음 | 없음 | 없음 |

- 에러 처리는 Error Handling Policy 를 따른다.
  callback request 의 submit 실패도 언어 관용구를 그대로 적용한다:
  exception 언어 (C++/Java/.NET/Node/Python) 는 예외, return-based 언어
  (C/Go/Rust) 는 에러 반환.
- reply 결과는 callback 이 정확히 한 번 전달한다.
  `(RequestResult result, List<Message> parts)`

#### SPOT Request-Reply

SPOT 직접 전달 위에서도 같은 request-reply 프로토콜을 사용한다.
`SPOT routed envelope -> request-reply envelope -> payload` 순서로 싣는다.
SPOT reply 도 ctx 없이 상대 주소 + request_seq 로 보낸다.
같은 Spot 에서 여러 request 를 동시에 outstanding 상태로 둘 수 있다.
high-level request 완료는 첫 reply 1건으로 끝난다.

#### Timeout

- timeout 은 core 가 관리한다. 바인딩은 timeout 로직을 구현하지 않는다.
- 기본 timeout: `5000ms`. per-call > socket default > 구현 기본 `5000ms`.
- `timeout_ms = 0` 이면 socket default timeout 을 사용한다.
- timeout 은 send 대기 + reply 대기를 합산한 전체 경과 시간에 적용된다.
- timeout 시 core 가 pending map 에서 제거하고 callback 에 `ZLINK_REQUEST_TIMED_OUT` 전달.
- timeout 후 late reply 는 core 가 drop 한다.

#### Pending map

- `request_seq` 채번, pending 등록, reply 매칭, timeout 제거 모두 core 에서 한다.
- 바인딩은 pending map 을 별도로 유지하지 않는다.
- 바인딩이 유지하는 것은 callback → Future/Promise resolve 매핑뿐이다.

#### Wire format

- `request_seq` 는 부호 없는 64비트 정수 (8바이트, network byte order).
- 시작값 `1`. `0` 은 ordinary message 예약값.
- overflow 시 `1` 로 wrap. outstanding 충돌값은 건너뛴다.
- envelope 은 4개 control part: protocol id, version, message type, request_seq.
- SPOT routed 조합 시 8개 SPOT control part + 4개 request-reply control part + payload.
- 바인딩은 envelope 을 직접 파싱하지 않는다. core 가 처리한다.

#### 반환 타입

- `request()` 성공 시 **reply payload `List<Message>` 만** 반환한다
  (`Vec<Message>` / `IReadOnlyList<Message>` / `Message[]` /
  `tuple[Message, ...]` 등 언어별 리스트 타입).
- caller 는 이미 자기가 보낸 request 의 대상 routing_id 와 request_seq 를
  알고 있으므로, 그걸 wrap 한 `Received` 를 되돌려받을 필요가 없다.
- 별도 `Reply` 타입은 만들지 않는다.
- multipart reply 지원이 목적이므로 단일 `Message` 가 아닌 리스트 형태다.
  단일 part reply 는 `parts[0]` 으로 꺼낸다.
- request handler (서버 측) 는 `peer_rid`, `request_seq`, payload 를 함께
  전달한다. 별도 `Request` 타입이나 `onRequest` 전용 callback 은 만들지
  않는다. (server 측은 누가 어떤 request_seq 로 보냈는지 알아야 하므로
  차이가 있다.)

#### 소유권

- `request()` / `reply()` 호출 시 메시지 ownership 은 기존 send 계약을 따른다.
- request callback 으로 전달된 `parts` 는 borrowed view 다.
  callback 반환 후 무효. 바인딩은 이를 복사해 언어별 리스트 타입 또는
  `Vec<Message>` 로 전달한다.
- 소켓 close 시 core 가 pending map 의 모든 미완료 request 를 `ZLINK_REQUEST_TERMINATED` callback 으로 reject 한다.

#### Callback 계약

- callback 은 정확히 한 번 호출된다.
  성공이면 `result = OK` + reply parts, 실패면 `result != OK` +
  empty/null/Err 경로로 전달된다.
- core callback 시그니처: `void(zlink_request_result_t result_, zlink_msg_t *parts_, size_t part_count_, void *userdata_)`
- 언어별 패턴 (per-function `RequestError` 계승):
  - C++: `std::function<void(request_result_t, std::vector<message_t>)>`
  - Java: `BiConsumer<RequestResult, List<Message>>`
  - .NET: `Action<RequestResult, IReadOnlyList<Message>>`
  - Node: `(result: RequestResult, parts: Message[]) => void`
  - Python: `callback(result: RequestResult, parts: list[Message])`
  - Go: `func(RequestResult, []*Message)` (실패 시 nil/empty 허용)
  - Rust: `FnOnce(Result<Vec<Message>, RequestError>)` (Rust 관용구;
    `RequestError::code` 가 `RequestResult` 에 대응)

### SPOT Messaging Policy

> 언어별 SPOT 인터페이스는 `cpp/`, `java/`, `dotnet/`, `node/`, `python/`, `go/`, `rust/` 를 참조한다.

SPOT public surface 는 channel-aware 경로를 기본으로 둔다. 즉
`publish(service_name, ...)`, `sendChannel(...)`, `requestChannel(...)` 가
우선 경로다. 여기서 `publish()`는 현재 공개 헤더 이름 때문에
`service_name` 인자를 그대로 쓰지만, 의미는 channel 이름으로 읽는다.
직접 주소 지정 routed messaging 은 선택적으로 추가할 수 있는 보조 typed
surface 다. request-reply 는 routed messaging 위에 얹어진다.

#### Pub/Sub 메시징

service-aware SPOT pub/sub 는 `service_name + topic` 기반 발행/구독 모델이다.

```c
/* 발행 */
int zlink_spot_publish(void *spot, const char *service_name,
    const char *topic_id,
    zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags);

/* 구독 수신 */
int zlink_spot_subscribe(void *spot, zlink_routing_id_t *source_rid_out,
    zlink_msg_t **parts_out, size_t *part_count_out,
    char *service_name_out, size_t *service_name_len_out,
    char *topic_id_out, size_t *topic_id_len_out, zlink_send_flags_t flags);

/* 구독 필터 */
int zlink_set_subscription(void *handle, const char *filter);
int zlink_unset_subscription(void *handle, const char *filter);
```

바인딩 규칙:
- C API 는 publish 를 위한 별도 no-wait 함수 이름을 따로 두지 않는다.
- non-blocking publish 는 `zlink_spot_publish(..., ZLINK_DONTWAIT)` 를 호출하고
  errno 를 `zlink_send_result_t` 로 분류한다. 바인딩은 별도 `tryPublish` 나
  `publishNoWait` 를 두지 않는다.
- `subscribe` 수신은 `service_name + topic + parts` 를 돌려주는 typed receive
  surface 로 노출한다.
- topic filter 설정은 typed subscription API 로 노출한다.
- service-aware send/publish 의 실패는 `SubmitError` 로 승격된다.
  - `NOT_FOUND`: 해당 `service_name` 또는 `channel_name` 에 attach 된 대상이 없음
  - `NOT_CONNECTED`: attachment 는 있으나 active/send-ready 경로가 없음
  - `BACKPRESSURED`: 경로는 있으나 HWM 도달
  - `NOT_ADMITTED`: 대상 peer 가 drain 상태라 신규 submit 거부

#### Routed Direct Messaging

SPOT routed direct messaging 은 특정 Router peer 또는 routed reply 대상에 직접
메시지를 보낸다. `Spot` facade에는 더 이상 direct spot send/request가 없고,
직접적인 spot 대상 ordinary send/request는 router 표면에서만 다룬다.

```c
/* router -> spot */
int zlink_router_send_spot(void *router,
    const zlink_routing_id_t *dest_node_rid,
    const zlink_routing_id_t *dest_spot_rid,
    zlink_msg_t *parts, size_t part_count, zlink_send_flags_t flags);
```

바인딩 규칙:
- routed send 는 기존 `sendRid` 패턴과 동일하다.
- 목적지 주소는 `peer_rid` 또는 reply 대상의
  `dest_node_rid + dest_spot_rid` 로 지정한다.
- routed recv 는 아래 Event Dispatcher 의 handler/recv surface 를 사용한다.

#### SPOT Lifecycle / Attachment

```c
void *zlink_spot_new(void *node);          /* SPOT facade 생성 */
int zlink_spot_destroy(void **spot_p);     /* SPOT facade 해제 */

void *zlink_spot_node_new(void *ctx);      /* SPOT Node 런타임 생성 */
int zlink_spot_node_destroy(void **node_p);/* SPOT Node 해제 */
int zlink_spot_node_bind(void *node, const char *endpoint);
int zlink_spot_node_connect_peer(void *node, const char *peer_endpoint);
int zlink_spot_node_disconnect_peer(void *node, const char *peer_endpoint);
int zlink_spot_node_attach_discovery(void *node, void *discovery);
int zlink_spot_node_attach_channel_dealer(void *node, void *discovery, void *dealer);
int zlink_spot_node_attach_channel_dealer_manual(void *node, const char *channel_name, void *dealer);
int zlink_spot_node_attach_pub_ingress(void *node, void *pub);
```

바인딩 규칙:
- `SpotNode` 와 `Spot` 은 별도 typed handle 로 노출한다.
- `Spot` 은 `SpotNode` 위에 올라가는 facade 다. `SpotNode` 해제 시 `Spot` 도 무효가 된다.
- SPOT channel view는 `attach_discovery()`로 닫고, 다른 channel 호출은
  `attach_channel_dealer()` 또는 `attach_channel_dealer_manual()`로 붙인다.
- `attach_pub_ingress()`는 일반 `PUB -> Spot` 입력 경로를 여는 전용 표면이다.
- `connect_peer` / `disconnect_peer` 는 raw peer topology 전용 control
  path 다. service-aware public surface 의 중심 API 로 설명하면 안 된다.

### SPOT Event Dispatcher Policy

core 는 callback 기반 event dispatcher 모델을 제공한다.
하나의 I/O thread context 안에서 여러 이벤트 소스
(sub recv, routed recv, timer, send-ready) 를 동기화 없이 처리할 수 있다.

핵심 원리:
- handler callback 을 등록하면 core I/O thread 가 이벤트 발생 시 callback 을 호출한다.
- 모든 callback 은 같은 thread context 에서 실행되므로 lock 없이 상태를 공유할 수 있다.
- callback 안에서 recv, send, reply 를 호출해도 동기화 문제가 없다.
- timer 도 같은 context 에서 실행된다.

#### Callback 등록 API

```c
/* raw STREAM direct recv callback */
int zlink_recv_handler(void *s, zlink_socket_msg_handler_fn handler, void *userdata);

/* raw STREAM packet callback */
int zlink_stream_packet_handler(void *stream, zlink_stream_packet_handler_fn handler,
    void *userdata);

/* writable 알림 callback 등록 */
int zlink_send_ready_handler(void *s, zlink_send_ready_handler_fn handler, void *userdata);

/* SPOT typed recv callback */
int zlink_spot_handler(void *spot, zlink_spot_handler_fn handler, void *userdata);
```

규칙:
- callback 등록은 한 subject 당 하나만 가능하다.
  이미 등록된 상태에서 다시 등록하면 `EBUSY`.
- `zlink_recv_handler()` 는 raw `STREAM` 에만 허용한다.
- `zlink_stream_packet_handler()` 도 raw `STREAM` 에만 허용하며,
  `recv` / raw callback / packet callback 세 모드는 서로 배타적이다.
- raw `PAIR`, `DEALER`, `ROUTER`, `SUB`, `XSUB` 는 direct receive callback
  install surface 를 두지 않는다. data plane 은 recv-only 이다.
- callback 등록 후 같은 subject 에 대한 direct recv 와 해당 data-plane
  `ZLINK_POLLIN` 등록은 `EBUSY` 로 실패할 수 있다. 정확한 적용 범위는
  STREAM / SPOT 의 타입별 규칙을 따른다.
- callback 은 replace-only 다. `NULL` 전달은 허용하지 않는다.

#### Spot Dispatch Event Handler

Spot 의 핵심 event dispatcher 는 `zlink_spot_dispatch_event_handler()` 다.
이 handler 를 등록하면 Spot 에 관련된 모든 이벤트가 하나의 callback 으로 올라온다.
같은 `spot` 에 대해서는 callback 이 순차적으로 전달되어야 한다. 구현은 같은
`spot` 의 dispatch callback 을 동시에 호출하거나 재진입 호출해서는 안 된다.
callback 안에서 event 종류를 확인하고 recv 를 호출하면서 Spot 메시징을
순차적으로 처리할 수 있어야 한다.

이 직렬화는 `spot` 단위다. 서로 다른 `spot` 사이에는 전역 직렬화를 요구하지
않는다. 구현은 다른 Spot 들을 병렬로 처리할 수 있어야 하며, 그 과정에서도
같은 `spot` 의 순차 처리 계약은 유지되어야 한다.

```c
typedef enum zlink_spot_dispatch_event_t {
    ZLINK_SPOT_DISPATCH_EVENT_SUBSCRIBE_READABLE = 1,  /* pub/sub 메시지 도착 */
    ZLINK_SPOT_DISPATCH_EVENT_ROUTED_READABLE    = 2,  /* routed/request 메시지 도착 */
    ZLINK_SPOT_DISPATCH_EVENT_TIMER_READABLE     = 3   /* timer fire */
} zlink_spot_dispatch_event_t;

typedef void (*zlink_spot_dispatch_event_handler_fn)(
    void *spot, zlink_spot_dispatch_event_t event, void *userdata);

zlink_handler_result_t zlink_spot_dispatch_event_handler(void *spot,
    zlink_spot_dispatch_event_handler_fn handler, void *userdata);
```

사용 패턴:
- dispatch event handler 를 등록한다.
- callback 이 호출되면 `event` 를 확인한다.
- 같은 `spot` 의 활성 dispatch callback 안에서는 기본 recv surface 를 사용할 수 있다.
- `SUBSCRIBE_READABLE` 이면 `zlink_spot_subscribe()` 또는
  `zlink_spot_subscription_event()` 로 pub/sub plane 을 drain 한다.
- `ROUTED_READABLE` 이면 `zlink_spot_recv()` 로 routed/request 메시지를 recv 한다.
- `TIMER_READABLE` 이면 `zlink_timer_recv()` 로 timer fire 를 recv 한다.
- dispatch event 는 readable 알림이다. callback 1회가 메시지 1개를 뜻하지는 않는다.
- callback 안에서는 해당 plane 을 더 이상 읽을 것이 없을 때까지 drain 할 수 있어야 한다.
- 같은 `spot` 의 dispatch callback 은 직렬화되므로 Spot 메시징을 순차적으로 처리할 수 있다.
- 서로 다른 `spot` 은 병렬 처리될 수 있으므로 고성능 room 실행 모델을 구성할 수 있다.

#### Spot Timer API

Spot 소유 timer 는 `zlink_spot_timer_new(spot)` 로 생성하고, 이후 공통
`zlink_timer_*` 함수로 제어한다.

```c
void *zlink_spot_timer_new(void *spot);

/* 이후 공통 timer API 사용 */
int zlink_timer_destroy(void **timer_p);
int zlink_timer_start(void *timer, uint64_t interval_ns, uint64_t repeat_count);
int zlink_timer_stop(void *timer);

typedef void (*zlink_timer_handler_fn)(
    void *timer, uint64_t fire_count, void *userdata);

int zlink_timer_handler(void *timer,
    zlink_timer_handler_fn handler, void *userdata);
int zlink_timer_recv(void *timer, uint64_t *fire_count_out);
```

규칙:
- timer 는 `zlink_spot_timer_new(spot)` 로 Spot 에 종속하여 생성한다.
- 생성 후에는 `zlink_timer_start`, `zlink_timer_stop`, `zlink_timer_recv`,
  `zlink_timer_handler`, `zlink_timer_destroy` 공통 API로 제어한다.
- `interval_ns` 는 나노초 단위다. `repeat_count = 0` 이면 무한 반복.
- timer fire 는 dispatch event handler 에 `TIMER_READABLE` 로 올라온다.
- timer handler callback 을 직접 등록하거나 `zlink_timer_recv()` 로 polling 할 수 있다.
- dispatch callback 안에서는 `zlink_timer_recv()` 로 pending fire 를 순차 처리할 수 있다.

바인딩 규칙:
- timer 는 typed wrapper 로 노출한다.
- `interval_ns` 는 언어별 Duration 타입으로 변환한다.
- timer 와 dispatch event 를 통합하여, 사용자는 callback 등록만으로
  sub recv + routed recv + timer 를 동기화 없이 처리할 수 있어야 한다.

#### Dispatch 모델 요약

```
zlink_spot_dispatch_event_handler callback
  (serialized per spot, non-reentrant)
  ├── SUBSCRIBE_READABLE → zlink_spot_subscribe() or
  │                        zlink_spot_subscription_event()
  ├── ROUTED_READABLE    → zlink_spot_recv()    (routed / request 메시지)
  └── TIMER_READABLE     → zlink_timer_recv()   (timer fire)
```

같은 `spot` 에 대해서는 이 callback 안에서 recv, send, reply 를 순차적으로
처리할 수 있어야 한다.
서로 다른 `spot` 은 필요하면 병렬로 실행될 수 있어야 한다.
callback 안에서는 event 로 알려진 plane 을 drain 할 수 있어야 한다.

#### Receive-model 요약

| 소켓 타입 | 수신 경로 |
|-----------|----------|
| `PAIR` / `DEALER` | `zlink_recv()` 만 (recv-only) |
| `SUB` / `XSUB` | `zlink_subscribe()` 만 (recv-only) |
| `ROUTER` | `zlink_router_recv()` 만 (recv-only). request completion 은 `zlink_reply_handler_fn` 으로 유지 |
| `STREAM` | 아래 세 모드 중 하나 (상호 배타). raw recv / `zlink_recv_handler()` / `zlink_stream_packet_handler()` |
| `SPOT` | `zlink_spot_recv()` + `zlink_spot_subscribe()` + `zlink_spot_dispatch_event_handler()`. 호환용 `zlink_spot_handler()` 도 제공되며 dispatch event handler 와 routed 축에서 상호 배타 |

바인딩은 위 계약을 그대로 반영한다. 소켓 클래스에 해당 수신 경로만 노출하고,
금지된 callback install surface 는 base 클래스 어디에서도 우회 접근되지
않도록 한다.

#### Typed Receive Surface

SPOT 수신은 여러 typed surface 를 제공한다.
바인딩은 이 typed surface 위에 언어별 handler/callback 표면을 얹는다.

#### Spot 수신

```c
typedef void (*zlink_spot_handler_fn)(
    const zlink_routing_id_t *source_rid,
    const zlink_routing_id_t *spot_rid,
    uint64_t request_seq,
    zlink_msg_t *parts, size_t part_count, void *userdata);

zlink_handler_result_t zlink_spot_handler(void *spot, zlink_spot_handler_fn handler, void *userdata);
zlink_recv_result_t zlink_spot_recv(void *spot, ...);
```

- `request_seq = 0` 이면 ordinary routed message 또는 pub/sub message 다.
- `request_seq != 0` 이면 request-reply message 다.
- `source_rid + spot_rid` 는 발신자 주소이며 reply target 으로 사용한다.
- `zlink_spot_handler()` 와 `zlink_spot_recv()` 는 같은 수신 plane 을 공유한다.
  동시에 허용하지 않는다. 충돌 시 `EBUSY`.

#### Router 수신 (routed 통합 recv-only 표면)

```c
zlink_recv_result_t zlink_router_recv(void *router,
    const zlink_routing_id_t **source_node_rid_out,
    const zlink_routing_id_t **source_spot_rid_out,
    uint64_t *request_seq_out,
    zlink_msg_t **parts_out, size_t *part_count_out,
    zlink_recv_flags_t flags);
```

- ROUTER 의 routed 수신은 단일 plane 이다. 일반 ROUTER 트래픽과
  spot-origin routed 트래픽을 하나의 recv 로 받는다.
- `source_spot_rid == NULL` 이면 일반 ROUTER 트래픽 (reply 는
  `zlink_router_reply` 사용). `source_spot_rid` 가 채워져 있으면 spot-origin
  트래픽 (reply 는 `zlink_router_reply_spot` 사용).
- `request_seq == 0` 이면 fire-and-forget. `request_seq != 0` 이면 request.
- 바인딩은 ROUTER data-plane callback install surface 를 별도로 노출하지 않는다.
  request completion callback 은 `request(...)` 경로에서만 유지한다.

#### Pub/Sub 수신

- raw `SUB`, `XSUB` 는 recv-only 이다.
- 바인딩은 `zlink_subscribe()` typed receive surface 를 노출한다.
- direct topic callback install surface 는 raw pub/sub family 에 두지 않는다.

#### Service Monitor

Discovery service view 를 모니터링하는 event surface 다.

```c
typedef void (*zlink_service_monitor_handler_fn)(
    const zlink_service_event_t *event, void *userdata);

void *zlink_service_monitor_open(void *target,
    const zlink_service_monitor_open_options_t *options);
int zlink_service_monitor_handler(void *monitor,
    zlink_service_monitor_handler_fn handler, void *userdata);
int zlink_service_monitor_recv(void *monitor,
    zlink_service_monitor_event_t *out, int flags);
```

이벤트 종류:
- `ZLINK_SERVICE_EVENT_PEER_ADDED` — peer 추가
- `ZLINK_SERVICE_EVENT_PEER_REMOVED` — peer 제거
- `ZLINK_SERVICE_EVENT_PEER_READY` — peer 연결 완료
- `ZLINK_SERVICE_EVENT_SUBJECT_ADDED` — 주제 추가
- `ZLINK_SERVICE_EVENT_SUBJECT_REMOVED` — 주제 제거
- `ZLINK_SERVICE_EVENT_SUBJECT_READY` — 주제 준비 완료

바인딩 규칙:
- monitor 는 typed handle 로 노출한다.
- Discovery 지원 바인딩에서는 `ServiceMonitor` 를 노출해야 한다.
- SpotNode/Spot public API 는 `ServiceMonitor` 를 열지 않는다.
  SpotNode 는 별도 typed monitor recv surface를 공개 계약으로 두지 않는다.
- event 수신은 handler callback 또는 direct recv 로 제공한다.
- event mask 필터링은 open 옵션으로 설정한다.
- `zlink_service_event_t` 는 바인딩이 언어별 typed event object 로 변환한다.

#### SPOT Node Status Query

```c
int zlink_spot_node_status_snapshot(void *node, zlink_spot_node_status_t *out);
int zlink_spot_node_peers_snapshot(void *node,
    zlink_spot_node_peer_entry_t **entries_out, size_t *count_out);
int zlink_spot_node_peers_query(void *node,
    const zlink_spot_node_peer_filter_t *filter,
    zlink_spot_node_peer_entry_t **entries_out, size_t *count_out);
int zlink_spot_node_subjects_snapshot(void *node,
    zlink_spot_node_subject_entry_t **entries_out, size_t *count_out);
```

바인딩 규칙:
- snapshot 결과는 언어별 typed domain object 배열로 변환한다.
- filter query 는 typed filter builder 또는 struct 로 노출한다.
- 반환된 배열의 메모리는 바인딩이 적절히 해제해야 한다.

### SpotNode Node-Level Options

SpotNode의 node-level 옵션은 `zlink_set_spot_node_option()` 계열로 다룬다.

## Option Policy

### Public Option Surface
- **public raw `setOption(key, value)` / `getOption(key)` bag 은 금지.**
- **public raw `setsockopt/getsockopt` bag 도 금지.**
- 공용 옵션은 언어에 맞는 typed surface (facade) 로만 노출한다.
- 특화 옵션도 언어에 맞는 capability surface (facade) 로만 노출한다.
- raw enum key + 범용 setter/getter 를 돌리는 public 경로가 spec 에
  남아 있으면 정책 위반. (`set_option(ZLINK_OPT_*, value)` 같은 C 계약이
  바인딩 public API 로 올라오면 안 됨. 바인딩 내부에서 native 호출 경로는
  허용.)
- typed facade 가 이미 있으면 **raw 경로를 중복 노출하지 않는다** — 사용자가
  두 방식 중 고를 필요 없게 한다.
- 예:
  - Java/.NET: `CommonSocketOptions`, `RouterSocketOptions`
  - Go: typed method set, capability interface
  - Rust: typed builder, method set, newtype
  - Python/Node: property, namespace object, capability object, typed method set

#### Option Facade Canonical Type Names
- 각 바인딩은 아래 canonical facade 타입을 제공해야 한다.
- 타입 이름은 언어 케이싱 관례만 변형한다.

| Facade | 내용 | 적용 소켓 |
|---|---|---|
| `CommonSocketOptions` | linger, sendHwm, recvHwm, sendTimeout, recvTimeout, immediate, connectTimeout, ipv6, tcpNoDelay, tcpKeepalive, heartbeatInterval/Ttl/Timeout, maxMsgSize, backlog, reconnectInterval/Max | 전체 |
| `RouterSocketOptions` | mandatory (bool), handover (bool), probe (bool), connectRoutingId (RoutingId) | Router |
| `DealerSocketOptions` | probe (bool) | Dealer |
| `StreamSocketOptions` | notify (bool) | Stream |
| `PubSocketOptions` | verbose (bool), verboser (bool), noDrop (bool), manual (bool) | Pub, XPub |
| `SubSocketOptions` | topicsCount (int, read-only) | Sub, XSub |

- 각 facade의 option 항목은 `core/include/zlink.h`의 해당 option enum 값을
  기준으로 한다.
- facade 내 option 값 타입은 Option Value Types 정책을 따른다.

### Option Value Types
- option 값은 가능한 한 의미 기반 타입으로 노출한다.
- 정책:
  - `0/1` 옵션: `boolean`
  - 유한 상태 집합: `enum`
  - 시간 의미: `Duration` 또는 언어 표준 시간 타입
  - binary identifier: `RoutingId` 같은 value object
  - 진짜 수치 설정: `int`/`long`
  - 문자열/바이트: `String`/`byte[]`
- option 이름만 enum이고 값은 raw `int`인 형태는 충분하지 않다.

## Performance Policy
- 성능은 별도 최적화 항목이 아니라 public API 설계의 일부다.
- canonical hot path는 숨은 비용이 가장 적은 경로여야 한다.
- hot path에서는 다음을 기본적으로 금지한다.
  - 숨은 payload 복사
  - 숨은 배열/리스트 재할당
  - 불필요한 UTF-8 인코딩/디코딩
  - 바인딩 레이어의 중복 포장
  - 결과를 만들기 위한 불필요한 boxing/unboxing
- 편의 API는 기본 경로보다 비용이 더 크면 문서화해야 한다.
- callback path와 direct receive path는 payload shape뿐 아니라 비용 모델도
  과도하게 벌어지면 안 된다.
- zero-copy, borrowed, owned 경로가 다르면 ownership과 함께 비용 모델도
  문서화해야 한다.
- 성능 검증 강도는 언어와 런타임 특성에 따라 달라질 수 있다.
- 다만 모든 바인딩은 hot path에서 불필요한 복사, 할당, 변환을 줄이는 방향을
  기본 정책으로 삼아야 한다.

### High-Performance Buffer Ecosystem Policy (Recommended)
- canonical public contract 는 계속 `Message` / `List<Message>` / `Received` /
  `TopicMessage` 를 기준으로 유지한다.
- 다만 send / publish / request / reply 입력 경로에서는, **해당 언어에서 사실상
  표준급이고 copy 감소 효과가 큰 버퍼 생태계 타입**을 adapter surface 로
  지원하는 것을 권장한다.
- 이 지원은 canonical contract 를 대체하지 않는다.
  - recv 결과를 외부 라이브러리 타입으로 바꾸지 않는다.
  - domain object 필드 타입을 외부 라이브러리 타입으로 바꾸지 않는다.
  - 지원하더라도 `Message` 생성 / 입력 adapter / `from_*` helper /
    `impl IntoMultipart` 같은 진입점으로 제한한다.
- 지원 기준:
  - 그 언어의 네트워킹/IO 생태계에서 널리 쓰이는가
  - zero-copy 또는 copy 감소 효과가 실질적인가
  - 특정 프레임워크 종속을 public surface 전체에 강제하지 않는가
- 비기준:
  - niche 라이브러리
  - 특정 회사/프로젝트 내부에서만 주로 쓰는 버퍼 타입
  - canonical type 을 대체하려는 wrapper

권장 우선순위:

| 언어 | 권장 지원 | 수준 | 비고 |
|---|---|---|---|
| Java | Netty `ByteBuf` | Recommended | 네트워크 스택에서 매우 흔하고 direct/off-heap 경로 가치가 큼 |
| Java | Agrona `DirectBuffer` | Optional | 저지연 계열에서 유용하지만 Netty보다 우선순위는 낮음 |
| .NET | `ReadOnlyMemory<byte>` / `ReadOnlySequence<byte>` / `IBufferWriter<byte>` | Recommended | 표준 버퍼 생태계. copy 감소 효과가 큼 |
| .NET | `PipeReader` / `PipeWriter` | Optional | `System.IO.Pipelines` 사용자층에 유용 |
| Rust | `bytes::Bytes` / `BytesMut` | Recommended | async/network 생태계에서 사실상 표준급 |
| Python | buffer protocol / `memoryview` | Recommended | `bytes` / `bytearray` 외 zero-copy 입력 경로 확보 |
| Node | `Buffer` / `Uint8Array` | Baseline | 사실상 기본 지원 범주 |
| Go | `[]byte` / `[][]byte` | Baseline | 언어 기본 경로가 이미 hot path 표준 |

- 설계 규칙:
  - adapter 는 input-side convenience 여야 한다. canonical return type 을
    바꾸지 않는다.
  - 언어 표준 라이브러리나 런타임이 아닌 **third-party buffer type** 은
    가능하면 core binding 이 아니라 별도 extension module 로 분리한다.
    예를 들어 Java `ByteBuffer` 는 core 에 둘 수 있지만, Netty `ByteBuf` 는
    별도 Netty extension 에 두는 방향이 맞다.
  - adapter 지원 여부 때문에 overload 폭이 과도하게 늘어나면 안 된다.
    가능하면 `MessageLike`, `IntoMultipart`, buffer protocol 같은 **한 개의
    통합 진입점**으로 흡수한다.
  - 외부 버퍼 타입을 받더라도 ownership / retain / release 규칙은 바인딩이
    문서로 명확히 정의해야 한다.
  - 프레임워크별 객체 수명 규칙 (`ByteBuf.retain/release`, pooled buffer 등)을
    사용자가 추측하게 두면 안 된다.
  - "지원 가능" 과 "zero-copy 보장" 을 혼동하지 않는다. zero-copy 보장이
    불가능하면 문서에 copy 가능성을 명시한다.

### Codec / Serializer Extension Module Policy
- `Message` 와 multipart transport 자체는 계속 canonical binding core contract 다.
- protobuf / json / messagepack codec-aware domain conversion 은
  **binding core 위에 올라가는 정식 별도 extension contract** 로 취급한다.
- 단, `C` binding 은 예외다. `C`는 raw transport contract 를 기본 public surface 로
  유지하며, codec-aware domain conversion 을 기본 binding contract 로 요구하지
  않는다.
- 따라서 `Parse(...)`, `Serialize(...)`, `ToMessage(...)`, `FromMessage(...)`
  같은 helper 를 public 으로 노출할 수 있다. 다만 이 helper 는 binding core
  package/module 에 섞으면 안 된다.
- Required rules:
  - binding core package/module 은 codec-agnostic 해야 한다.
  - binding core 가 protobuf/json/messagepack dependency 를 필수 의존성으로
    끌고 들어오면 안 된다.
  - `C` binding 은 raw byte/message contract 만 정식으로 유지하면 되며,
    protobuf/json helper 를 public contract 로 추가할 의무가 없다.
  - `C`를 제외한 binding 은 codec extension layer 를 public contract 로 두며,
    `protobuf`, `json`, `messagepack` 세 codec 을 지원해야 한다.
  - `C`를 제외한 binding 의 `protobuf`, `json`, `messagepack` extension 은
    각각 **core binding 과 별도 배포 단위** 로 제공해야 한다.
  - third-party buffer adapter extension 도 같은 원칙을 따른다.
    core binding 과 별도 배포 단위로 제공해야 하며, core binding 이 그
    extension dependency 를 필수로 요구하면 안 된다.
  - codec extension 은 core binding 에 의존할 수 있지만, core binding 이 codec
    extension 에 의존하면 안 된다.
  - codec extension 이 추가되어도 canonical recv/request/reply contract 는 계속
    `Message`, `List<Message>`, `Received`, `TopicMessage` 기준으로 유지한다.
  - codec extension 은 transport 결과 타입을 domain object 로 바꾸는 helper 를
    추가할 수 있지만, raw transport contract 자체를 대체하면 안 된다.
  - serializer 선택 규칙은 public contract 로 명시해야 한다.
    예: type marker 기반 선택, explicit parser object, schema object.
- 이유:
  - raw transport 사용자에게 특정 codec dependency 를 강제하지 않기 위함이다.
  - 언어별 codec 생태계 선택이 다르므로 core binding 이 한 구현체에 잠기지
    않게 하기 위함이다.
  - high-level domain helper 와 low-level transport ownership 계약을 분리해서
    변경 파급을 줄이기 위함이다.

JSON codec baseline by language:

| Language | JSON baseline |
|---|---|
| C | none required |
| C++ | `nlohmann/json` |
| .NET | `System.Text.Json` |
| Java | `Jackson` |
| Node | built-in `JSON.parse` / `JSON.stringify` |
| Python | stdlib `json` |
| Go | `encoding/json` |
| Rust | `serde_json` |

- 이 표는 "json codec extension 을 public 으로 노출할 때 기본으로 삼는 구현체"를
  뜻한다.
- 다른 json 라이브러리를 추가 지원할 수는 있다. 다만 public contract 와 sample,
  test, 기본 동작 기준은 위 표를 따른다.
- Node 는 built-in JSON 이 plain object encode/decode 의 기준이며, typed
  validation 은 별도 schema/parser object 위에 얹을 수 있다.

MessagePack codec baseline by language:

| Language | MessagePack baseline |
|---|---|
| C | none required |
| C++ | `msgpack-c` |
| .NET | `MessagePack for C#` |
| Java | `jackson-dataformat-msgpack` |
| Node | `@msgpack/msgpack` |
| Python | `msgpack` |
| Go | `vmihailenco/msgpack/v5` |
| Rust | `rmp-serde` |

- 이 표는 "messagepack codec extension 을 public 으로 노출할 때 기본으로 삼는
  구현체"를 뜻한다.
- 다른 messagepack 라이브러리를 추가 지원할 수는 있다. 다만 public contract 와
  sample, test, 기본 동작 기준은 위 표를 따른다.

Repository placement and distribution units for codec extension modules:

| Language | Core binding root | Codec extension distribution units | Repo root |
|---|---|---|---|
| C | `bindings/c/include/zlink/`, `bindings/c/src/` | none required | n/a |
| C++ | `bindings/cpp/include/zlink/` | `zlink-codec-protobuf`, `zlink-codec-json`, `zlink-codec-messagepack` | `bindings/cpp/codecs/zlink-codec-protobuf/`, `bindings/cpp/codecs/zlink-codec-json/`, `bindings/cpp/codecs/zlink-codec-messagepack/` |
| .NET | `bindings/dotnet/src/Zlink/` | NuGet `Zlink.Codecs.Protobuf`, NuGet `Zlink.Codecs.Json`, NuGet `Zlink.Codecs.MessagePack` | `bindings/dotnet/codecs/Zlink.Codecs.Protobuf/`, `bindings/dotnet/codecs/Zlink.Codecs.Json/`, `bindings/dotnet/codecs/Zlink.Codecs.MessagePack/` |
| Java | `bindings/java/src/main/java/dev/kairoscode/zlink/` | Maven `zlink-codec-protobuf`, Maven `zlink-codec-json`, Maven `zlink-codec-messagepack` | `bindings/java/codecs/zlink-codec-protobuf/`, `bindings/java/codecs/zlink-codec-json/`, `bindings/java/codecs/zlink-codec-messagepack/` |
| Node | `bindings/node/src/` | npm `@ulalax/zlink-codec-protobuf`, npm `@ulalax/zlink-codec-json`, npm `@ulalax/zlink-codec-messagepack` | `bindings/node/packages/zlink-codec-protobuf/`, `bindings/node/packages/zlink-codec-json/`, `bindings/node/packages/zlink-codec-messagepack/` |
| Python | `bindings/python/src/zlink/` | PyPI `zlink-codec-protobuf`, PyPI `zlink-codec-json`, PyPI `zlink-codec-messagepack` | `bindings/python/codecs/zlink_codec_protobuf/`, `bindings/python/codecs/zlink_codec_json/`, `bindings/python/codecs/zlink_codec_messagepack/` |
| Go | `bindings/go/` | Go module `zlink/codec/proto`, Go module `zlink/codec/json`, Go module `zlink/codec/messagepack` | `bindings/go/codec/proto/`, `bindings/go/codec/json/`, `bindings/go/codec/messagepack/` |
| Rust | `bindings/rust/src/` | crate `zlink-codec-protobuf`, crate `zlink-codec-json`, crate `zlink-codec-messagepack` | `bindings/rust/crates/zlink-codec-protobuf/`, `bindings/rust/crates/zlink-codec-json/`, `bindings/rust/crates/zlink-codec-messagepack/` |

- placement rules:
  - codec helper source 를 core socket/message namespace 와 같은 디렉터리에 직접
    섞지 않는다.
  - 언어별 spec 문서(`doc/spec/bindings/<lang>/README.md`)는 해당 binding 이 이
    codec extension 을 public 으로 노출할 때, **배포 패키지 이름**과 그 안의
    public package / namespace / crate / module 이름을 함께 명시해야 한다.
  - sample 과 tests 도 core binding sample/test 와 codec extension sample/test 를
    분리한다.

### External Buffer Attach / Release Hook Policy
- C API 의 `zlink_msg_init_data(..., zlink_free_fn*, hint)` 는 **external buffer
  attach + release hook** 능력을 제공한다.
- 바인딩은 이 능력을 **언어 관용구와 메모리 모델에 맞을 때만** public 으로
  노출한다.
- 기본 원칙:
  - **copy-based `Message` 생성 경로는 모든 바인딩에서 Required**
  - **release hook 없는 borrowed zero-copy wrap API 는 managed 언어 public
    surface 에 두지 않는다**
  - external buffer attach 는 **release 시점을 public contract 로 닫을 수 있을
    때만** 허용한다
- 허용:
  - C++
    - `from_external(..., zlink_free_fn*, hint)` 같은 형태로 external attach 허용
    - release hook 이 explicit 하므로 public contract 로 닫을 수 있다
- 비권장/금지:
  - Java / .NET / Go / Rust / Python / Node
    - generic public borrowed wrap (`wrapDirect`, `wrapNative`, `wrap_buffer`
      등) 금지
    - 이유: send 후 backing buffer lifetime, retain/release, arena/session,
      GC 와의 상호작용을 public contract 로 안전하게 닫기 어렵다
- 예외:
  - 특정 언어에서 releaser/owner contract 를 **명시적이고 안전한 public 타입**
    으로 닫을 수 있다면 advanced API 로 재검토할 수 있다
  - 다만 이 경우에도 canonical 기본 경로는 copy-based 생성이어야 한다

## Boundary Cost Policy
- 경계 검증은 가장 이른 안전한 위치에서 한 번 수행하는 것을 우선한다.
- 같은 검증을 여러 레이어에서 반복하면 이유가 명확해야 한다.
- 고정 크기 native struct에 들어가는 값은 truncation 대신 즉시 오류를 반환한다.
- 문자열, topic, routing id, metadata 같은 경계 값은 다음을 함께 고려한다.
  - 길이 상한
  - 인코딩 비용
  - 복사 횟수
  - 재할당 정책
- core의 고정 크기 struct 필드에 대응하는 바인딩 입력의 길이 상한:

  | 필드 | C struct 크기 | 바인딩 검증 책임 |
  |------|--------------|----------------|
  | `RoutingId` | `data[255]` | 값 객체 생성 시 255바이트 초과 시 즉시 오류 반환 |
  | topic / filter | C 문자열 (null-terminated) | 바인딩은 embedded null 문자 포함 시 즉시 오류 반환. 길이 상한은 core가 처리하므로 바인딩에서 별도 길이 검증하지 않는다 |
  | service_name | `char[256]` | 255바이트 초과 시 즉시 오류 반환 |
  | endpoint | `char[256]` | 255바이트 초과 시 즉시 오류 반환 |
  | metadata | `zlink_msg_t` (가변) | core가 처리, 바인딩은 null 검증만 |

- 바인딩은 고정 크기 필드에 들어가는 값이 상한을 넘으면 truncation 없이
  즉시 예외/오류를 반환한다.
- public 도메인 객체를 만들 때 불필요한 중간 컬렉션 생성은 피한다.
- helper나 sample이 느린 경로를 canonical path처럼 보이게 만들면 안 된다.

## Admission State Policy

`AdmissionState` 는 peer-level 수용/드레인 상태를 제어하는 canonical surface 다.
모든 바인딩이 공개해야 한다.

핵심 API:
- `zlink_set_admission_state(handle, state)` / `zlink_get_admission_state(handle, state_out)`
- enum `zlink_admission_state_t { SERVING = 1, DRAINING = 2 }`
- submit 결과 `ZLINK_SUBMIT_NOT_ADMITTED` (값 13) — drain 상태의 peer 로 submit 시 반환
- socket monitor 이벤트 `ZLINK_EVENT_PEER_ADMISSION_CHANGED` (bit 15)
- service monitor 이벤트 `ZLINK_SERVICE_MONITOR_EVENT_PEER_ADMISSION_CHANGED` (bit 8)
- `zlink_spot_node_peer_entry_t.admission_state` / `zlink_member_peer_entry_t.admission_state`

바인딩 규칙:
- `AdmissionState` 를 typed enum 으로 노출한다 (`SERVING`, `DRAINING`).
  언어별 스타일: `AdmissionState.Serving` (C#/.NET), `AdmissionState::Serving`
  (C++/Rust), `ADMISSION_STATE_SERVING` (Go), `admission_state.serving`
  (Python), `AdmissionState.SERVING` (Java), `AdmissionState.Serving`
  (Node/TS).
- `setAdmissionState(state)` / `getAdmissionState()` 또는 언어 관례에 맞는
  property/method 로 SOCKET / SPOT handle 에 노출한다.
- `NOT_ADMITTED` 를 `SubmitError` 계열에 포함하여 caller 가 drain 상태 거부를
  구분할 수 있게 한다.
- `PEER_ADMISSION_CHANGED` 이벤트 bit 은 기존 socket monitor / service
  monitor surface 에 typed value 로 노출한다. raw bit 값만 내보내지 않는다.
- `SpotNodePeerEntry` / `MemberPeerEntry` 도메인 객체는 `admissionState` /
  `admission_state` 필드를 포함해야 한다.

## Monitor Policy
- monitor plane도 같은 규칙을 따른다.
- public monitor receive는 `recv()` 하나로 제공한다.
  - blocking/non-blocking 은 flags 파라미터 또는 언어별 관례로 제어한다.
- monitor event는 data plane과 별도지만, blocking/non-blocking 구분 방식은
  동일해야 한다.
- monitor는 socket의 상태 변화, readiness 변화, lifecycle event를 관찰하는
  별도 plane 이다.
- monitor payload는 message data plane payload와 혼동되면 안 된다.
- monitor event type은 typed event surface 또는 동등한 의미 surface로
  노출해야 한다.
- monitor consumer는 raw integer mask만이 아니라 event 의미를 읽을 수 있어야
  한다.
- monitor lifecycle은 관찰 대상 socket lifecycle과의 관계가 설명 가능해야 한다.
  - monitor open 시점
  - monitor close 시점
  - observed socket close 이후의 동작
- monitor는 data plane을 대체하는 API가 아니다.
- monitor의 readiness/state event 의미는 data plane contract와 충돌하지
  않아야 한다.
- monitor sample과 test는 다음을 보여야 한다.
  - event 수신 성공 경로
  - non-blocking empty 경로
  - socket state 변화와 monitor event의 관계

## Error Policy

### Binding Validation vs Native Error
- 입력 값의 형식/범위 오류는 바인딩이 즉시 막는다.
- socket 상태, 연결 상태, transport 상태, protocol 상태 오류는 코어가
  결정하고 바인딩은 그대로 caller에 전달한다.

### Binding Must Validate
- truncation 가능성이 있는 값
- overflow 가능성이 있는 값
- fixed-size native struct에 들어가는 값
- 명백한 길이 상한이 있는 값
- offset/length 범위 오류
- null 불가 인자
- enum 범위 밖의 값

이 경우 바인딩 예외를 사용한다.
- Java: `IllegalArgumentException`, `IndexOutOfBoundsException`,
  `NullPointerException`
- .NET: `ArgumentException`, `ArgumentOutOfRangeException`,
  `ArgumentNullException`
- Go: 즉시 `error` 반환 또는 `panic` (프로그래머 오류)
- Rust: compile-time 보장 (`NonZero`, newtype) 또는 `panic!` / `Result<T, E>`

### Native Must Decide
- peer 없음
- backpressure
- readiness 부족
- callback mode와 direct recv 충돌
- socket type/state/runtime 문제
- transport, TLS, endpoint, protocol 오류

이 경우 바인딩은 native 오류를 언어별 관용구로 변환하여 caller에 전달한다.
Exception 언어는 예외를 던지고, return-based 언어는 에러 값을 반환한다.
- C++: `throw zlink_error_t`
- Java: `throw ZlinkException`
- .NET: `throw ZlinkException`
- Node: `throw ZlinkError` (extends `Error`)
- Python: `raise ZlinkError` (extends `Exception`)
- Go: `return err` (`ZlinkError` 또는 동등한 typed error)
- Rust: `Err(E)` (`Result<T, E>`; 여러 함수군이 섞일 때만 `ZlinkError`)

### Error Code 표

zlink 에서 사용하는 코드와 의미. 바인딩은 이 코드를 언어별 에러 타입에
매핑하여 caller 가 원인을 구분할 수 있게 한다.

코드는 두 계층으로 나뉜다.

1. **Public result enum 코드 (0–703)** — 공개 C API 함수의 반환 enum 값.
   바인딩이 직접 마주하고 언어별 에러 타입으로 노출해야 하는 값이다.
   전체 정의는 [core/errno-map.md](../core/errno-map.md) 참조.
2. **Internal errno** — `zlink_errno()` 로 조회되는 내부 raw errno.
   `INTERNAL_ERROR` 같은 coarse bucket 의 상세 원인 조회용. 바인딩은 이 값을
   `internalErrno` / `internal_errno` 필드로 노출한다 (디버깅 전용).

#### Public Result Enum 카탈로그

바인딩은 아래 8 개 enum 의 **모든 값을 누락 없이** 언어별 표현으로 매핑해야
한다. OK (0) 는 모든 enum 에 공통이며 에러로 취급하지 않는다.

##### `zlink_submit_result_t` (send, request submit, reply submit)

| 값 | 상수 | 내부 errno | 분류 | 의미 |
|----|------|-----------|------|------|
| 0 | `OK` | — | 성공 | 제출 성공 |
| 1 | `BACKPRESSURED` | `EAGAIN` | 제어 흐름 | send 큐 포화 (HWM) |
| 2 | `NOT_CONNECTED` | `ENOTCONN`, `EHOSTUNREACH` | 제어 흐름 | 대상 peer/경로 미연결 |
| 3 | `NOT_FOUND` | `ENOENT` | 제어 흐름 | 대상 peer/spot/route 없음 |
| 13 | `NOT_ADMITTED` | — | 제어 흐름 | peer 가 `ZLINK_ADMISSION_DRAINING` 상태라 신규 submit 거부 |
| 4 | `TERMINATED` | `ETERM` | 런타임/생명주기 | context 종료됨 |
| 5 | `INVALID_HANDLE` | `EFAULT` | caller 계약 위반 | NULL handle / invalid pointer |
| 6 | `INVALID_ARGUMENT` | `EINVAL` | caller 계약 위반 | 잘못된 인자 |
| 7 | `NOT_SUPPORTED` | `ENOTSUP` | caller 계약 위반 | 해당 소켓 타입에서 지원 안 함 |
| 8 | `INVALID_STATE` | `EFSM`, `EBUSY` | caller 계약 위반 | 소켓/handle 상태 오류 |
| 9 | `THREAD_VIOLATION` | `EMTHREAD` | caller 계약 위반 | 잘못된 스레드에서 접근 |
| 10 | `OUT_OF_MEMORY` | `ENOMEM` | 내부 실패 | 메모리 할당 실패 |
| 11 | `SEQ_EXHAUSTED` | `EBUSY` | 내부 실패 | request seq 공간 고갈 |
| 12 | `INTERNAL_ERROR` | `EPROTO` 등 | 내부 실패 | 내부 submit 실패 (상세는 `zlink_errno()`) |

##### `zlink_request_result_t` (request completion callback)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | `0` | reply payload 수신 성공 |
| 101 | `TIMED_OUT` | `ETIMEDOUT` | `timeout_ms` 내 reply 미도착 |
| 102 | `NOT_FOUND` | `ENOENT` | 대상 없음, 에러 reply 로 완료 |
| 103 | `TERMINATED` | `ETERM` | (예약) 명시적 종료 완료 경로 |
| 104 | `PROTOCOL_ERROR` | `EPROTO` | reply envelope / error reply payload 손상 |

##### `zlink_recv_result_t` (recv, subscribe, subscription event, monitor recv, timer recv)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | — | 수신 성공 |
| 201 | `NO_DATA` | `EAGAIN` | non-blocking recv 데이터 없음 / source 고갈 |
| 202 | `BUSY` | `EBUSY` | handler 이미 attach 됨 |
| 203 | `TERMINATED` | `ETERM` 외 | context 종료 또는 분류되지 않은 recv 내부 실패 |
| 204 | `INVALID_HANDLE` | `EFAULT` | NULL / invalid handle |
| 205 | `NOT_SUPPORTED` | `ENOTSUP` | recv 미지원 소켓 타입 |

##### `zlink_handler_result_t` (handler 등록)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | — | handler 등록 성공 |
| 301 | `INVALID_ARGUMENT` | `EINVAL` | NULL handler |
| 302 | `BUSY` | `EBUSY` | handler 이미 attach 됨 |
| 303 | `NOT_SUPPORTED` | `ENOTSUP` | 미지원 subject |
| 304 | `DEADLOCK` | `EDEADLK` | reentrant 호출 (send-ready handler 전용) |
| 305 | `INVALID_HANDLE` | `EFAULT` | NULL / invalid handle |

##### `zlink_close_result_t` (close, destroy)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | — | close/destroy 성공 |
| 401 | `BUSY` | `EBUSY` | in-flight callback / API 호출 |
| 402 | `SHUTDOWN` | `ESHUTDOWN` | 이미 close 됨 |
| 403 | `INVALID_HANDLE` | `EFAULT` | NULL / invalid handle |

##### `zlink_bind_result_t` (bind)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | — | bind 성공 |
| 501 | `INVALID_ARGUMENT` | `EINVAL` | 잘못된 endpoint |
| 502 | `ADDR_IN_USE` | `EADDRINUSE` | 주소 이미 사용 중 |
| 503 | `NOT_SUPPORTED` | `ENOTSUP` | 미지원 transport |
| 504 | `INVALID_HANDLE` | `EFAULT` | NULL / invalid handle |

##### `zlink_connect_result_t` (connect, disconnect, unbind)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | — | connect/disconnect/unbind 성공 |
| 601 | `INVALID_ARGUMENT` | `EINVAL` | 잘못된 endpoint |
| 602 | `NOT_SUPPORTED` | `ENOTSUP` | 미지원 transport |
| 603 | `INVALID_HANDLE` | `EFAULT` | NULL / invalid handle |

##### `zlink_config_result_t` (option set/get, message lifecycle, snapshot, poller mutation, proxy, timer config)

| 값 | 상수 | 내부 errno | 의미 |
|----|------|-----------|------|
| 0 | `OK` | — | 설정 성공 |
| 701 | `INVALID_HANDLE` | `EFAULT` | NULL / invalid handle |
| 702 | `INVALID_ARGUMENT` | `EINVAL`, `EBUSY` | 잘못된 인자 또는 config 계층 conflict |
| 703 | `NOT_SUPPORTED` | `ENOTSUP` | 미지원 옵션 |

##### Non-OK 값 총합

- 총 **40 개** non-OK 코드 (submit 13 + request 4 + recv 5 + handler 5 +
  close 3 + bind 4 + connect 3 + config 3 = 40). 값 범위:
  1–13, 101–104, 201–205, 301–305, 401–403, 501–504, 601–603, 701–703.
- 값 범위는 enum 간 겹치지 않으므로 단일 `int` 로 유일하게 구분된다.
- 바인딩은 40 개 값 모두에 대해 언어별 에러 표현을 제공해야 한다. 누락 시
  caller 가 해당 원인을 구분할 방법이 없다.

언어별 enum/상수 매핑 스타일은 아래 `언어별 ErrorCode 매핑` 절을 참조한다.

#### POSIX 표준 errno

POSIX 에서 해당 상수가 정의되지 않은 플랫폼에서는 `ZLINK_HAUSNUMERO` 기반
대체 값을 사용한다. 바인딩은 상수 이름으로 비교해야 하며 정수 값에 직접
의존하면 안 된다.

| errno | 대체 값 (POSIX 미정의 시) | 의미 | 대표 발생 상황 |
|-------|-------------------------|------|--------------|
| `ENOTSUP` | HAUSNUMERO + 1 | 지원하지 않는 작업 | 해당 소켓 타입에서 불가능한 작업 |
| `EPROTONOSUPPORT` | HAUSNUMERO + 2 | 프로토콜 미지원 | 지원하지 않는 프로토콜 요청 |
| `ENOBUFS` | HAUSNUMERO + 3 | 버퍼 공간 부족 | 내부 버퍼 할당 실패 |
| `ENETDOWN` | HAUSNUMERO + 4 | 네트워크가 다운됨 | transport 레이어 장애 |
| `EADDRINUSE` | HAUSNUMERO + 5 | 주소가 이미 사용 중 | bind 시 endpoint 충돌 |
| `EADDRNOTAVAIL` | HAUSNUMERO + 6 | 주소를 사용할 수 없음 | 잘못된 endpoint 형식 |
| `ECONNREFUSED` | HAUSNUMERO + 7 | 연결 거부됨 | 대상이 연결을 거부 |
| `EINPROGRESS` | HAUSNUMERO + 8 | 작업 진행 중 | 비동기 연결 진행 중 |
| `ENOTSOCK` | HAUSNUMERO + 9 | 소켓이 아닌 대상 | 잘못된 handle 전달 |
| `EMSGSIZE` | HAUSNUMERO + 10 | 메시지 크기 초과 | 메시지가 설정된 최대 크기 초과 |
| `EAFNOSUPPORT` | HAUSNUMERO + 11 | 주소 체계 미지원 | 지원하지 않는 주소 체계 |
| `ENETUNREACH` | HAUSNUMERO + 12 | 네트워크에 도달 불가 | 라우팅 불가 |
| `ECONNABORTED` | HAUSNUMERO + 13 | 연결이 중단됨 | 연결이 비정상 종료 |
| `ECONNRESET` | HAUSNUMERO + 14 | 연결이 재설정됨 | peer 가 연결을 강제 종료 |
| `ENOTCONN` | HAUSNUMERO + 15 | 연결되지 않은 상태 | 연결 전에 send/recv 시도 |
| `ETIMEDOUT` | HAUSNUMERO + 16 | 작업 시간 초과 | request reply timeout, 연결 timeout |
| `EHOSTUNREACH` | HAUSNUMERO + 17 | 대상에 도달할 수 없음 | peer 미연결, 라우팅 불가 |
| `ENETRESET` | HAUSNUMERO + 18 | 네트워크가 재설정됨 | 네트워크 연결 끊김 |
| `EAGAIN` | (POSIX 표준) | 자원이 일시적으로 사용 불가 | non-blocking send 시 HWM 도달 (backpressure) |
| `EINVAL` | (POSIX 표준) | 잘못된 인자 | 범위 초과, 잘못된 옵션 값 |
| `ECANCELED` | (POSIX 표준) | 작업이 취소됨 | caller 가 request 를 취소 |

`ZLINK_HAUSNUMERO` 값은 `156384712` 이다.

#### zlink 전용 errno

zlink 고유 오류 코드. POSIX errno 와 충돌하지 않도록 `ZLINK_HAUSNUMERO`
기반 오프셋을 사용한다.

| 대체 값 | 상수 | 의미 | 대표 발생 상황 |
|--------|------|------|--------------|
| HAUSNUMERO + 51 | `EFSM` | 유한 상태 기계 오류 | 소켓 상태에서 허용되지 않는 작업 (예: callback 모드에서 direct recv) |
| HAUSNUMERO + 52 | `ENOCOMPATPROTO` | 호환되지 않는 프로토콜 | 서로 다른 프로토콜 버전의 peer 연결 |
| HAUSNUMERO + 53 | `ETERM` | 컨텍스트/소켓 종료 | context 또는 소켓이 close 된 상태에서 작업 시도 |
| HAUSNUMERO + 54 | `EMTHREAD` | I/O 스레드 부족 | context 의 I/O 스레드가 부족 |

#### 언어별 ErrorCode 매핑

각 바인딩은 Public Result Enum 카탈로그의 40 개 non-OK 코드를 언어별
enum/상수로 매핑하여 타입 안전한 분기를 제공한다.

| 언어 | 처리 | ErrorCode 타입 | 접근 방식 |
|------|------|---------------|----------|
| C | return | 함수별 typed enum (`zlink_*_result_t`) | 반환값 자체 |
| C++ | throw | 통합 `ErrorCode` enum | `zlink_error_t.code()` |
| Java | throw | 통합 `ErrorCode` enum | `ZlinkException.getCode()` |
| .NET | throw | 통합 `ErrorCode` enum | `ZlinkException.Code` |
| Node | throw | 통합 `ErrorCode` enum (또는 string 상수) | `ZlinkError.code` |
| Python | throw | 통합 `ErrorCode` enum | `ZlinkError.code` |
| Go | return | 통합 `ErrorCode` typed int 상수 | `ZlinkError.Code()` |
| Rust | return (`Result`) | 통합 `ErrorCode` enum variant | `ZlinkError.code()` |

- 통합 enum 의 각 variant 는 Public Result Enum 카탈로그의 40 개 값과
  1:1 대응한다. 원본 C 의 enum 분리 (submit / recv / handler / close /
  bind / connect / config / request) 를 유지하거나, 언어 관용구에 따라
  단일 enum 으로 통합해도 된다. 둘 중 어떤 스타일이든 **값은 누락 없이 모두
  표현해야 한다**.
- 상수/variant 이름은 원본 `UPPER_SNAKE_CASE` 를 그대로 쓰거나 언어 스타일
  (`PascalCase` / `camelCase`) 로 변환한다. 숫자 값과 의미는 고정이다.
- `internalErrno` / `internal_errno` 필드는 별도로 제공하며, 주로
  `INTERNAL_ERROR` 같은 coarse bucket 의 상세 원인 조회용이다.

### Request-Reply Error Policy

request-reply 는 Per-Function Error Type Hierarchy 의 **`RequestError`**
(request completion) 과 **`SubmitError`** (request submit) 두 하위 타입을
사용한다. `RequestError` 는 `zlink_request_result_t` 에 대응하며,
`SubmitError` 는 `zlink_submit_result_t` 에 대응한다.

오류 코드는 두 계층으로 나뉜다.

**Wire error reply 코드** — peer 가 보내는 protocol-level error reply.
wire 에서 사용 가능한 errno 는 3개로 제한된다: `ENOENT`, `EOPNOTSUPP`, `EINVAL`.

**API/completion 코드** — core 가 callback 에 전달하는 errno:

| errno | 발생 시점 |
|-------|----------|
| `ENOENT` | 대상 peer/spot 을 찾지 못함 (wire 또는 local) |
| `EOPNOTSUPP` | peer 종류 불일치 또는 지원 안 함 |
| `EINVAL` | 잘못된 파라미터 |
| `ETIMEDOUT` | reply 대기 중 timeout 초과 |
| `EPROTO` | envelope parse 실패 또는 잘못된 remote reply |
| `EBUSY` | 수신 표면 충돌 (handler 중복 등록) |

**request 오류 (`RequestError`):**

| 상황 | `request()` |
|------|------------|
| backpressure | writable 대기 (timeout 에 합산) |
| timeout | `RequestError(TIMED_OUT)` |
| 대상 없음 | `RequestError(NOT_FOUND)` |
| remote error reply | `RequestError(해당 코드)` |
| 소켓 close | `RequestError(TERMINATED)` |
| protocol error | `RequestError(PROTOCOL_ERROR)` |
| pending map 에 없는 reply | 무시 |

**reply 오류 (`SubmitError`):**

| 상황 | `reply()` |
|------|-----------|
| 성공 | 정상 반환 |
| backpressure | `SubmitError(BACKPRESSURED)` |
| not connected | `SubmitError(NOT_CONNECTED)` |
| 기타 실패 | `SubmitError(해당 submit 코드)` |

- async request 는 완료 실패를 async completion 경로 (Future reject / await
  error) 로 전달한다.
- callback request 는 **submit 실패를 즉시 throw/return** 하고, submit 성공 후의
  완료 실패만 callback 의 `RequestResult` / `RequestError` 로 전달한다.
- 함수군별 하위 에러 타입을 사용한다 (Per-Function Error Type Hierarchy 참조).
  - submit 실패: `SubmitException` / `SubmitError`
  - request 완료 실패: `RequestException` / `RequestError`
- 언어별 표현:
  - Java: `SubmitException` / `RequestException` — `getCode()` 로 원인 구분 (unchecked)
  - .NET: `ZlinkSubmitException` / `ZlinkRequestException` — `Code` property
  - Node: `SubmitError` / `RequestError` — `code` property
  - Python: `SubmitError` / `RequestError` — `code` attribute
  - C++: `submit_error_t` / `request_error_t` — `.code()` 메서드
  - Go: `*SubmitError` / `*RequestError` — `Code()` 메서드 (interface)
  - Rust: `Err(SubmitError{..})` / `Err(RequestError{..})`, 또는 다중 함수군
    경계에서는 `Err(ZlinkError::Submit(..))` / `Err(ZlinkError::Request(..))`
    — `.code()` 메서드

## Length and Range Boundary Policy
- 검증 책임은 두 층으로 나눈다.
- 값 객체가 존재하는 타입:
  - 값 객체 생성 시점에 canonical validation을 수행한다.
  - 예: `RoutingId`, typed enum wrapper, bounded identifier
- 값 객체가 존재하지 않거나 호출 문맥 의존 변환이 필요한 타입:
  - native 호출 직전에 검증한다.
  - 예: `Duration -> int millis`, offset/length slicing, output buffer sizing
- native 호출 직전 재검증은 아래 경우에만 필수다.
  - 값 객체를 거치지 않는 raw 경로가 존재하는 경우
  - 값 객체 생성 후 호출 직전 추가 변환이 들어가는 경우
  - 값 객체가 아닌 복합 입력 조합에서 overflow/truncation이 생길 수 있는 경우
- truncation 후 native로 넘기는 동작은 금지한다.

예:
- `RoutingId`는 `zlink_routing_id_t`의 `data[255]` 계약을 넘기지 않아야 한다.
- `Duration -> int millis` 변환은 overflow를 허용하면 안 된다.
- topic, subscription, metadata처럼 고정 출력 버퍼가 개입되는 경로는 길이와
  재할당 정책이 명확해야 한다.

## Ownership Policy
- `Message` ownership은 코어 계약과 일치해야 한다.
- 모든 바인딩은 내부적으로 C API를 호출하므로, GC 언어를 포함한 전 언어에서
  native message의 ownership을 올바르게 관리해야 한다.
- ownership 경로:
  - send 성공: ownership이 native로 이동한다. 바인딩은 이후 접근하면 안 된다.
  - send 실패: restore 가능한 경로와 consume되는 경로를 혼동하지 않는다.
  - recv: native가 생성한 메시지의 ownership을 바인딩이 넘겨받는다. 바인딩이
    해제 책임을 진다.
  - 생성 후 미전송: 바인딩이 직접 생성한 메시지를 전송하지 않았다면 반드시
    명시적으로 close/해제해야 한다. GC가 managed wrapper만 수거할 뿐, native
    메모리는 해제하지 않으므로 누수가 발생한다.
- callback delivery와 direct receive는 동일한 payload shape를 가져야 한다.
- callback 후 frame validity는 계약으로 명확해야 한다.

## Naming Policy
- 메서드명은 언어 관례만 반영한다.
- 개념 이름은 바인딩 간 최대한 동일하게 유지한다.
- 아래 목록은 의미 기준 canonical name 이다.
- 실제 바인딩 메서드명은 다음 두 가지 변형만 허용한다.
  1. **케이싱 변형**: 언어 관례에 맞게 camelCase/PascalCase/snake_case를
     변환한다. 단어 구성은 바뀌지 않는다.
     - 예: `connectPeer` → Go: `ConnectPeer`, Python: `connect_peer`,
       C++: `connect_peer`, Rust: `connect_peer`
  2. **overload 불가 언어의 최소 접미사**: Go와 Rust처럼 overloading이 없는
     언어에서, 동일 동작의 파라미터 변형을 구분하기 위해 최소한의 접미사를
     허용한다. 이 접미사는 동작 구분이며, 파라미터 인코딩이 아니다.
     - 예: `send` → Go: `Send` / `SendTo`, Rust: `send` / `send_to`
     - 허용 접미사 범위: `To` 수준의 최소 동작 구분 접미사까지만 허용한다.
       파라미터 타입이나 의미를 풀어쓴 접미사는 금지한다.
       - 허용: `SendTo`, `send_to`
       - 금지: `SendWithRoutingId`, `send_routed`, `send_multipart`
     - 접미사 허용은 overloading도 keyword/optional parameter도 없는
       언어(Go, Rust)에만 적용된다.
     - 접미사 없이 시그니처로 구분 가능한 언어에서는 접미사를 사용하지
       않는다.
       - overloading: Java, C#, C++
       - keyword / optional parameter: Python
       - optional / union type: Node/TypeScript
- **그 외의 단어 교체, 단어 생략, 다른 단어 대체는 허용하지 않는다.**
  - 금지 예: `onReceive`를 `recvHandler`로 바꾸는 것 → 단어 교체
  - 금지 예: `querySnapshot`을 `snapshot`으로 줄이는 것 → 단어 생략이므로,
    canonical 이름 자체를 `snapshot`으로 정의해야 한다
- 케이싱이나 접미사가 달라져도 역할 구분과 의미 계약은 같아야 한다.
- 예: `receiveSubscriptionEvent` → Python: `receive_subscription_event`,
  Go: `ReceiveSubscriptionEvent`
- 추천 canonical 이름:
  - `bind`, `connect`, `close`
  - `send`
  - `recv`
  - `publish`
  - `subscribe`
  - `receiveSubscriptionEvent`
  - `setSubscription`, `unsetSubscription`
  - `onReceive`, `onDispatchEvent`, `onRoutedReceive`, `onSendReady`

### Method Name Conciseness
- 이 규칙은 public API에 엄격히 적용한다.
- internal/private API는 파라미터 인코딩이 가독성을 높이면 허용한다.
  - 내부 코드는 overloading 없이 명시적 이름이 더 읽기 좋을 수 있다.
  - 예: internal helper에서 `sendRouted(id, msg)`는 허용
- 메서드 이름은 동작(action)만 표현한다.
- 파라미터의 존재, 타입, 개수를 이름에 반복하지 않는다.
- 시그니처가 이미 설명하는 것을 이름에 다시 쓰면 안 된다.
- 동작 자체가 다른 경우(예: `send` vs `publish`)는 이름이 달라야 한다.
- 입력만 다른 경우(예: routing id 유무)는 이름을 늘리지 않는다.

안티패턴과 올바른 패턴:

| 안티패턴 | 올바른 패턴 | 이유 |
|---|---|---|
| `sendWithRoutingId(id, msg)` | `send(id, msg)` | `RoutingId` 타입이 이미 의미를 전달 |
| `sendMultipartMessages(parts)` | `send(parts)` | multipart-only이므로 이름에 반복 불필요 |
| `publishToTopic(topic, msg)` | `publish(topic, msg)` | publish는 topic이 있는 동작 |
| `recvWithTimeout(timeout)` | `recv(timeout)` | 시그니처로 충분 |
| `setLingerTimeoutMilliseconds(ms)` | `setLinger(duration)` | 타입이 단위를 전달 |

파라미터 조합이 다를 때 이름을 늘리는 대신 각 언어의 고유 disambiguation
메커니즘을 사용한다.

- Java / C# / C++: overloading
  - 이름은 하나, 시그니처가 구분
  - 예: `send(Message msg)`, `send(RoutingId id, Message msg)`
- Go: 가변 인자 / functional option / 별도 메서드
  - overloading이 없으므로 동작 의미가 다른 경우에만 최소 접미사를 허용한다
  - 예: `Send(msg Message)`, `SendTo(id RoutingId, msg Message)`
  - 파라미터를 그대로 이름에 넣지 않는다
- Python: keyword argument / optional parameter
  - 이름은 하나, keyword가 구분
  - 예: `send(self, message, *, routing_id=None)`
- Node/TypeScript: optional parameter / union type
  - 이름은 하나, 타입이 구분
  - 예: `send(message: Message)`, `send(routingId: RoutingId, message: Message)`
- Rust: trait bound / `Option<T>` / newtype
  - overloading이 없으므로 `impl Into<T>`, `Option<T>`, strong newtype으로 구분
  - 예: `send(msg: impl Into<Message>)`,
    `send_to(id: RoutingId, msg: impl Into<Message>)`
  - 동작 의미가 다른 경우에만 최소 접미사를 허용한다
  - 파라미터를 그대로 이름에 넣지 않는다

언어별 정리:

| 언어 | disambiguation 방식 | 이름에 파라미터 인코딩 |
|---|---|---|
| Java | overloading | 금지 |
| C# | overloading | 금지 |
| C++ | overloading + strong type | 금지 |
| Go | 별도 메서드 / functional option | 금지, 동작 구분 접미사만 허용 |
| Python | keyword / optional | 금지 |
| Node/TS | optional / union | 금지 |
| Rust | trait bound / Option / newtype | 금지, 동작 구분 접미사만 허용 |

## Compatibility Policy
- 호환성보다 일관된 public surface를 우선할 수 있다.
- deprecated compatibility layer는 가능한 빨리 제거한다.
- canonical path 외에 동일 기능의 우회 표면을 public 으로 함께 두지 않는다.
- flag 타입 정책:
  - public method signature 의 `SendFlag` / `ReceiveFlag` 는 public API
    contract 의 일부가 아니다.
  - 필요한 경우 internal helper 또는 package/private helper 로만 유지한다.
  - public 노출 타입 자체도 삭제 또는 internal 이동을 우선한다.

## Cross-Language Alignment

### Shared Behavioral Contract
- blocking send/receive 계열은 실패 시 언어별 에러 경로 (exception 언어는
  예외, return-based 언어는 에러 반환)
- non-blocking receive 는 "데이터 없음"도 동일한 에러 경로로 전달
  (result code 로 구분). 별도 `try*` API 는 제공하지 않는다.
- non-blocking send 는 explicit outcome (submit result code)
- multipart-only
- typed option surface

### Language-Specific Return Style
- C API
  - raw contract와 함수별 typed result enum
  - multipart-only 기준 surface
  - blocking API + explicit non-blocking entry (`flags` 파라미터)
- C++
  - RAII와 typed wrapper
  - multipart-only 기준 surface
  - 실패는 `throw zlink_error_t` (`SubmitResult` 코드 포함)
- .NET
  - typed option surface + `ZlinkException`
  - multipart-only 기준 surface
  - 실패는 `throw ZlinkException` (`Code` 포함)
- Java
  - domain object + `ZlinkException`
  - multipart-only 기준 surface
  - 실패는 `throw ZlinkException` (`getCode()` 포함)
- Go
  - `(T, error)` + strong type + explicit error check
  - multipart-only 기준 surface
  - 모든 실패는 `error` 반환 (`SubmitResult` 코드 포함)
- Rust
  - `Result<T, E>` + strong newtype + ownership
  - multipart-only 기준 surface
  - 단일 함수군은 `BindError` / `SubmitError` 같은 concrete error,
    다중 함수군은 `ZlinkError`
- Node/Python
  - 언어 관례를 따르되 의미 계약은 동일
  - multipart-only 기준 surface
  - 모든 실패는 `throw` / `raise` (`SubmitResult` 코드 포함)

언어별 표면은 달라도 의미 계약은 같아야 한다.

### Cross-Language Capability Table
| Area | C API | C++ | .NET | Java | Go | Rust | Node | Python |
|---|---|---|---|---|---|---|---|---|
| Multipart-only public surface | Required | Required | Required | Required | Required | Required | Required | Required |
| Blocking API named directly | Yes | Yes | Yes | Yes | Yes | Yes | Yes | Yes |
| Non-blocking receive uses `try*` | N/A raw entry | Required | Required | Required | Required | Required | Required | Required |
| Non-blocking send explicit outcome | Core enum/result | Required | Required | Required | Required | Required | Required | Required |
| Public flags overloads | Raw C only | High-level public surface: No | No | No | No | No | No | No |
| Typed option surface | N/A raw C options | Required | Required | Required | Required | Required | Required | Required |
| Socket Capability Matrix 준수 | Core 기준 | Required | Required | Required | Required | Required | Required | Required |
| `onReceive` callback | STREAM raw fn ptr | Optional | Optional | Optional | Optional | Optional | Optional | Optional |
| `onDispatchEvent` callback | SPOT raw fn ptr | 구현 시 Required | 구현 시 Required | 구현 시 Required | 구현 시 Required | 구현 시 Required | 구현 시 Required | 구현 시 Required |
| `onRoutedReceive` callback | SPOT raw fn ptr | 구현 시 Required | 구현 시 Required | 구현 시 Required | 구현 시 Required | 구현 시 Required | 구현 시 Required | 구현 시 Required |
| `onSendReady` callback | Raw fn ptr | Required | Required | Required | Required | Required | Required | Required |
| StreamSocket `connect` 차단 | N/A | Required | Required | Required | Required | Required | Required | Required |
| Monitor typed event surface | Raw struct | Required | Required | Required | Required | Required | Required | Required |

## Testing Policy
- reflection/surface test로 canonical public API를 고정한다.
- 공통 검증 항목:
  - 타입별 capability 분리 여부
  - raw option bag 비노출
  - `try*` naming convention 준수 여부
- contract test로 바인딩 ↔ native 계약을 검증한다.
  - FFI/native 호출 매핑이 올바른지
  - managed ↔ native 경계의 타입 변환이 올바른지
  - native handle lifecycle과 리소스 정리가 누수 없이 동작하는지
- behavior test로 바인딩 레이어가 core 계약을 올바르게 중계하는지 검증한다.
- ownership 회귀 테스트를 유지한다.
- callback mode와 direct mode의 충돌 규칙도 테스트한다.
- 정책 변경 시 필수 테스트 규칙:
  - public surface 변경: reflection/surface test 동반
  - contract 계약 변경: contract test 동반
  - blocking/non-blocking 계약 변경: behavior test 동반
  - ownership/receive shape 변경: callback regression 또는 ownership test 동반
  - option surface 변경: typed option reflection test와 negative capability test 동반
- 성능 회귀 검증은 별도 Perf Policy가 담당한다.
- Test Matrix에 정의되지 않은 테스트 항목이 기존 코드에 남아 있다면 삭제한다.
  - migration 검증, core 기능 재검증, 자동화 불가능한 리뷰 항목 등이 테스트로
    작성되어 있으면 정리 대상이다.
  - 테스트는 이 문서의 Test Matrix 카테고리에 해당하는 항목만 유지한다.

### Test Execution Script Policy
- 각 바인딩은 전체 테스트를 한번에 실행할 수 있는 스크립트를 제공해야 한다.
- 실행 스크립트는 `bindings/<언어>/tests/` 디렉토리에 위치해야 한다.
- 스크립트는 반복 실행 가능하고 성공/실패를 요약해서 보여줘야 한다.
- 권장 형태:
  - `tests/run_tests.sh`
  - `tests/run_tests.ps1`
  - language-specific test runner entry

### Bug Discovery Policy
- 테스트 또는 perf 작성/실행 중 버그를 발견한 경우 다음 절차를 따른다.
- 바인딩 라이브러리 버그:
  - 해당 바인딩에서 직접 수정한다.
  - 수정과 함께 회귀 테스트를 추가한다.
- core 라이브러리 버그:
  - 바인딩에서 core 버그를 직접 수정하지 않는다.
  - `bindings/<언어>/bug/` 디렉토리에 버그 리포트를 작성한다.
  - 리포트에는 최소한 다음을 포함한다.
    - 재현 조건 (소켓 타입, 패턴, 메시지 크기, transport 등)
    - 기대 동작
    - 실제 동작
    - 재현 코드 또는 테스트 참조
  - 바인딩 측에서 workaround가 필요하면 workaround임을 명시하고 bug 리포트를
    참조한다.

## Test Matrix
- 이 섹션은 각 바인딩이 최소한 가져야 할 테스트 항목을 정리한다.
- 바인딩별 표면은 달라도 아래 의미 계약은 모두 검증해야 한다.
- `Surface Tests`, `Contract Tests`, `Behavior Tests`, `Send Failure Contract Tests`,
  `Receive Failure Contract Tests`, `Boundary Validation Tests`, `Option Tests`,
  `Ownership Tests`, `Monitor Tests`는 기본적으로 `Required`다.

### Surface Tests
- canonical public API reflection/surface test
- socket type capability 분리 확인
- typed option surface 존재 확인
- raw option bag 비노출 확인
- monitor canonical surface 존재 확인
  - `recv()`

### Contract Tests
- FFI/native 호출 매핑 검증
  - 바인딩 public API 호출이 올바른 C API 함수에 매핑되는지 확인
  - 파라미터 전달과 반환값 변환이 올바른지 확인
- managed ↔ native 경계 타입 변환 검증
  - 언어 타입에서 C 타입으로의 변환이 올바른지 확인
  - C 타입에서 언어 타입으로의 변환이 올바른지 확인
- 리소스 lifecycle 검증
  - context/socket native handle 생성과 해제가 누수 없이 동작하는지 확인
  - 예외/오류 경로에서도 native 리소스가 정리되는지 확인

### Behavior Tests
- 바인딩 레이어가 core 계약을 올바르게 중계하는지 검증한다.
- 목적은 core 메시징 기능 재검증이 아니라 바인딩 경로의 정확성 확인이다.
- blocking 경로:
  - `send` → core send 중계 성공
  - `recv` → core recv 중계 성공
  - `publish` → core publish 중계 성공
  - `subscribe` → core subscribe 중계 성공
  - routed `send` → routing id 포함 중계 성공
- non-blocking 경로:
  - `recv` non-blocking → 데이터 없음 시 empty 반환
  - `subscribe` non-blocking → 데이터 없음 시 empty 반환
  - `receiveSubscriptionEvent` non-blocking → 데이터 없음 시 empty 반환
  - `send` 실패 시 예외 또는 오류 경로 확인
  - `publish` 실패 시 예외 또는 오류 경로 확인

### Send Failure Contract Tests
- blocking `send` failure가 예외 또는 언어별 오류 경로로 caller에 전달되는지 확인
- blocking `publish` failure가 예외 또는 언어별 오류 경로로 caller에 전달되는지 확인
- `send` backpressure 예외 확인
- `send` not-ready 예외 확인
- `publish` backpressure 또는 not-ready 예외 확인
- `EAGAIN` 외 오류가 무시되지 않는지 확인

### Receive Failure Contract Tests
- callback mode와 direct recv 충돌 시 native 계약대로 오류가 전달되는지 확인
- direct recv 불가 상태에서 empty/null로 숨기지 않는지 확인
- `EAGAIN`만 empty/non-success 결과로 처리되는지 확인

### Boundary Validation Tests
- `RoutingId` 최대 길이 경계 (255바이트 OK)
- `RoutingId` 초과 길이 즉시 오류 반환 (256바이트 이상 → 예외)
- `Duration -> int millis` overflow 경계
- offset/length bounds 검증
- null 불가 인자 검증
- enum 범위 밖 값 검증
- `service_name` 255바이트 초과 즉시 오류 반환 (고정 크기 `char[256]`)
- `endpoint` 255바이트 초과 즉시 오류 반환 (고정 크기 `char[256]`)
- topic/filter에 embedded null 문자 포함 시 즉시 오류 반환

### Option Tests
- common option typed getter/setter
- socket type별 typed option getter/setter
- 잘못된 소켓 타입에서 option capability 접근 차단
- raw integer 대신 enum/boolean surface가 제공되는지 확인

### Ownership Tests
- send 성공 시 ownership 이동 계약 (native에 넘어감, 바인딩이 이후 접근 금지)
- send 실패 시 restore 또는 caller ownership 유지 계약
- 생성 후 send하지 않은 메시지의 명시적 close/해제 (close 없으면 native 메모리 누수)
- recv 결과 ownership 계약 (바인딩이 받아서 해제 책임)
- callback 후 frame validity 계약
- multipart receive shape와 callback delivery shape 일치 여부

### Monitor Tests
- blocking monitor `recv` 성공 경로
- non-blocking monitor recv empty path
- monitor callback/state 변화와 data plane readiness 일치 여부

### Note: Performance and Sample Verification
- 성능 회귀 검증은 Perf Policy (`doc/perf/`)가 담당한다. Test Matrix에 중복하지
  않는다.
- sample/helper의 canonical API 준수, send 실패 무시 방지, legacy surface
  우회 방지는 Review Checklist에서 검증한다. 자동화 테스트 항목이 아니다.

## Sample Policy
- 샘플 제작 규칙은 [`doc/spec/sample/SAMPLE_POLICY.md`](../sample/SAMPLE_POLICY.md)
  를 단일 기준 문서로 사용한다.
- 이 문서는 `core/samples/`와 `bindings/*/samples/`를 함께 포괄한다.
- 바인딩 샘플을 추가, 수정, 리뷰할 때는 위 문서를 기준으로 판단한다.

## Perf Policy

perf 코드는 데모가 아니라 바인딩 라이브러리의 성능을 측정하고 개선하기 위한
코드다. perf 의 1차 목적은 바인딩 레이어의 비용을 드러내고, 병목과 회귀를
식별하고, 개선 작업의 전후 차이를 측정하는 것이다.

**perf 정책의 단일 기준은 `doc/perf/` 정책 문서다.** CLI 옵션, 기본값, 출력
포맷, RESULT line 형식, 패턴/transport matrix, phase 규칙, 결과 저장, 실패
처리, 환경 변수 등 모든 세부 규격은 아래 문서를 따른다. 본 섹션에서 중복
정의하지 않는다.

- [`doc/perf/PERF_POLICY.md`](../../perf/PERF_POLICY.md) — 공통 perf 정책
  (공통 원칙, 디렉터리 구조, RESULT 형식, 결과 저장, 출력 형식, 실패 처리,
  환경 변수, 리팩토링 원칙, 언어별 적용 범위)
- [`doc/perf/PERF_SINGLE_TEST_POLICY.md`](../../perf/PERF_SINGLE_TEST_POLICY.md) — single suite 정책
- [`doc/perf/PERF_MULTI_TEST_POLICY.md`](../../perf/PERF_MULTI_TEST_POLICY.md) — multi suite 정책

### 바인딩 perf 원칙

- perf 코드는 `doc/perf` 정책을 준수한다.
- `core/perf` 에서 제공하는 패턴과 시나리오를 기준으로 한다.
- core perf와 비교 가능한 시나리오를 유지하면서, 각 언어 스타일에 맞게 작성한다.
- 측정 anchor point, phase 의미, metric 집합, RESULT line 의미를 바꾸지 않는다.
- perf 정책은 성능 측정 surface를 공식 제공하는 바인딩에서는 `Required`다.
  perf 코드를 아직 제공하지 않는 바인딩에는 `Target`으로 본다.

### 바인딩 API Spec 문서

각 바인딩의 API surface는 아래 문서를 참조한다.
perf 정책은 [`doc/perf/PERF_POLICY.md`](../../perf/PERF_POLICY.md)에서 전 언어 공통으로 관리한다.

| 바인딩 | API Spec |
|--------|----------|
| Node.js | [`NODE_API_SPEC.md`](NODE_API_SPEC.md) |
| Python | [`PYTHON_API_SPEC.md`](PYTHON_API_SPEC.md) |

### Perf Review Checklist

- 이 perf 가 바인딩 라이브러리 비용을 측정하고 있는가
- 핵심 send/recv/callback 경로가 perf 파일 본문에서 직접 읽히는가
- 각 패턴이 별도 파일로 분리되어 있는가
- `core/perf` 패턴과 정렬되어 있는가
- `doc/perf` 정책을 준수하는가

## Script Location Policy
- 실행 스크립트는 실행 대상과 같은 디렉토리에 위치한다.
- 바인딩 루트가 아니라 각 하위 디렉토리에 둔다.

| 용도 | 위치 | 스크립트 예시 |
|------|------|---------------|
| 테스트 | `bindings/<언어>/tests/` | `run_tests.sh` |
| 샘플 | `bindings/<언어>/samples/` | `run_samples.sh` |
| perf | `bindings/<언어>/perf/` | `run_benchmarks.sh`, `run_benchmarks_multi.sh` |

- Windows 지원이 필요한 경우 `.ps1` 도 함께 제공한다.
- 바인딩 루트(`bindings/<언어>/`)에 `run_samples.sh` 같은 wrapper를
  두지 않는다. 이 위치의 wrapper는 `samples/run_samples.sh`와 중복되고,
  어느 것이 정답인지 혼선을 만든다.
- CI나 전체 검증을 위해 테스트+샘플+perf를 한번에 실행하는 orchestration
  스크립트가 필요하면 `bindings/<언어>/run_all.sh` 같은 이름으로 둘 수 있다.
  이 스크립트는 개별 `tests/run_tests.sh`, `samples/run_samples.sh` 등을
  호출하는 진입점이며, 개별 스크립트를 대체하지 않는다.

## Review Checklist
- public API가 multipart-only인가
- blocking/non-blocking이 이름으로 분리되었는가
- public flags 오버로드가 남아 있지 않은가
- raw option bag이 public에 남아 있지 않은가
- option 값이 enum/boolean/value object로 승격되었는가
- 타입별 capability가 제대로 닫혀 있는가
- blocking send 실패가 예외 또는 오류 경로로 반드시 caller에 전달되는가
- `send` 실패가 backpressure/not-ready를 포함해 모든 오류를 예외로 전달하는가
- binding이 truncation/overflow를 선검증하는가
- native 상태 오류를 바인딩이 임의로 추론하지 않는가
- reflection test와 behavior test가 같이 있는가
- 값 객체 검증과 호출 직전 검증의 책임 위치가 설명 가능한가
- legacy flag 타입이 public contract에서 제거되었는가
- sample code가 canonical API만 사용하는가
- helper가 blocking send 실패를 무시하지 않는가
- helper가 deprecated/legacy surface를 우회 호출하지 않는가

## POSD-Based Implementation Completion Policy
- 이 섹션은 바인딩 구현을 완성하고 리팩터링할 때 적용하는 POSD 기반 절차를
  정의한다.
- 바인딩은 기능 나열이 아니라 구조적 정확성을 기준으로 완성한다.
- 완성 기준은 Socket Capability Matrix, Callback API Policy, Option Policy,
  Test Matrix, Sample Policy다.
- 리팩터링은 코드를 이동하는 것이 아니라 시스템 복잡도를 줄이는 것이다.

### 완성 순서
- 바인딩 구현은 아래 순서를 따른다.
- 각 단계는 이전 단계의 결과에 의존한다.
- 한 단계를 건너뛰고 다음 단계를 진행하지 않는다.

#### 1단계: Capability Matrix 정렬
- Socket Capability Matrix를 기준으로 각 소켓 타입의 public API를 검토한다.
- 있어야 하는데 없는 API를 추가한다.
- 있으면 안 되는데 노출된 API를 제거하거나 internal로 이동한다.
- 검증: surface test가 matrix와 일치해야 한다.
- 대표 위반 예:
  - StreamSocket에 `connect()` 노출 → 제거
  - Node에 `onSendReady` 없음 → 추가
  - 잘못된 소켓에 publish/subscribe 노출 → 제거

#### 2단계: 이름 정규화
- Naming Policy와 Callback API Policy 기준으로 canonical 이름을 맞춘다.
- 이름만 다르고 의미가 같은 API는 canonical 이름으로 통일한다.
- deprecated alias는 제거한다.
- 검증: surface test에서 canonical 이름 존재를 확인한다.
- 대표 위반 예:
  - `recvHandler` → `onReceive`
  - `spotDispatchHandler` → `onDispatchEvent`
  - `on_topic_message` → `subscribe`

#### 3단계: 깊은 모듈 구조
- POSD deep module 원칙에 따라 public 타입의 깊이를 확보한다.
- 각 public 타입이 단순 pass-through가 아니라 내부에서 검증, ownership,
  shape 규칙을 캡슐화하는지 확인한다.
- 얕은 래퍼 판별 기준:
  - native 함수를 1:1로 감싸기만 하고 새 의미를 추가하지 않는가
  - 호출자가 native 계약(시퀀스, 크기, 인코딩)을 알아야 사용할 수 있는가
  - 동일 규칙이 여러 소켓 타입에 중복 구현되어 있는가
- 얕은 래퍼를 발견하면:
  - 검증을 값 객체 또는 facade 내부로 이동한다
  - 중복 규칙을 한 모듈에 모은다
  - pass-through만 하는 public 타입은 제거하거나 internal에 병합한다
- 대표 위반 예:
  - RoutingId 길이 검증이 각 소켓 타입마다 중복 → RoutingId 값 객체 하나로 모은다
  - monitor event가 raw int → typed event surface로 승격한다
  - option value가 raw int → enum/boolean/Duration으로 승격한다

#### 4단계: 변경 파급 제거
- 같은 규칙이 여러 곳에 흩어진 지점을 찾아서 한 모듈에 모은다.
- 판별 기준:
  - 정책 하나가 바뀌면 2개 이상의 파일을 고쳐야 하는가
  - 새 소켓 타입을 추가할 때 기존 코드를 N곳 수정해야 하는가
- 대표 위반 예:
  - send failure contract 규칙이 소켓 타입마다 별도 구현
  - blocking/non-blocking 분기가 소켓 타입마다 별도 구현
  - option validation이 각 option setter마다 별도 구현

#### 5단계: 정보 은닉 강화
- public API가 native 세부사항을 노출하는 지점을 찾아서 facade 뒤로 숨긴다.
- 판별 기준:
  - 사용자가 errno, flag 상수, native struct 크기를 알아야 하는가
  - 사용자가 internal sequencing(호출 순서)을 기억해야 하는가
  - public API에 native handle, raw pointer, raw buffer가 노출되는가
- 대표 위반 예:
  - raw `setSockOptRaw` / `setOption(int, byte[])` 가 public
  - monitor event에 raw int mask가 그대로 노출
  - SendFlag/ReceiveFlag가 public 타입으로 남아 있음

#### 6단계: 테스트 Matrix 완성
- Test Matrix의 모든 카테고리에 대해 테스트를 작성하거나 보강한다.
- 완성 기준:
  - Surface test가 Socket Capability Matrix를 검증한다
  - Contract test가 FFI 매핑과 lifecycle을 검증한다
  - Behavior test가 blocking/non-blocking 경로를 검증한다
  - Send/Receive Failure test가 오류 계약을 검증한다
  - Boundary test가 값 경계를 검증한다
  - Option test가 typed surface를 검증한다
  - Ownership test가 send/recv ownership을 검증한다
  - Monitor test가 recv를 검증한다

#### 7단계: 샘플 정렬
- Canonical Sample Set 기준으로 샘플을 완성한다.
- 각 샘플이 canonical API만 사용하는지 확인한다.
- 1-5단계에서 이름이나 API가 바뀌었다면 샘플도 같이 갱신한다.

### 리팩터링 판단 기준
- 다음 질문에 "예"이면 리팩터링이 필요한 지점이다.
  - 이 public 타입을 제거하면 사용자가 잃는 것이 없는가 → 얕은 래퍼
  - 이 규칙을 고치면 3개 이상의 파일을 건드려야 하는가 → 변경 파급
  - 사용자가 이 API를 쓰려면 다른 API의 내부 동작을 알아야 하는가 → 정보 누출
  - 같은 능력이 2개 이상의 이름으로 노출되는가 → 중복 surface
  - 사용자가 호출 순서를 기억해야 올바르게 동작하는가 → 시간 순서 의존

### 리팩터링 종료 조건
- 리팩터링은 아래 조건이 모두 충족될 때까지 반복한다.
- 하나라도 남아 있으면 완료가 아니다.
- 판단은 POSD 관점에서 수행한다.
- 종료 조건의 범위는 해당 바인딩이 구현하기로 한 scope에 한정한다.
  - `Required` 항목: 모든 바인딩에 적용
  - `Recommended` 항목(예: 샘플): 공개 배포 바인딩에 적용
  - `Target` 항목(예: Registry): 해당 바인딩이 구현한 경우에만 적용
  - 구현하지 않기로 한 `Target` 컴포넌트는 종료 조건에서 제외한다.

1. **Capability Matrix 완전 정렬**
   - Socket Capability Matrix의 모든 `Y` 항목이 public API에 존재한다.
   - Socket Capability Matrix의 모든 `—` 항목이 public API에 노출되지 않는다.
   - 해당 바인딩이 구현하는 서비스 계층 컴포넌트의 Capability Matrix도
     동일하게 정렬한다.
   - `Target`으로 표시된 컴포넌트(Registry, RegistryQueryClient)는 해당
     바인딩이 구현하지 않으면 종료 조건에서 제외한다.
   - Surface test가 이를 검증하고 통과한다.

2. **이름 정규화 완료**
   - 모든 public API가 Naming Policy의 canonical 이름을 사용한다.
   - deprecated alias가 남아 있지 않다.
   - Callback API Policy의 canonical 이름(`onReceive`, `onDispatchEvent`,
     `onRoutedReceive`, `onSendReady`)이 해당 capability에 맞게 존재한다.

3. **얕은 래퍼 제거**
   - native 함수를 1:1로 감싸기만 하는 public 타입이 없다.
   - 모든 public 타입이 검증, ownership, shape 규칙 중 하나 이상을 캡슐화한다.

4. **변경 파급 해소**
   - 동일 규칙이 2개 이상의 모듈에 중복 구현되어 있지 않다.
   - 정책 변경 시 수정해야 할 파일이 1개다.

5. **정보 은닉 확보**
   - public API에 raw option bag, raw flag, raw native struct, raw errno가
     노출되지 않는다.
   - 사용자가 internal sequencing을 알지 않아도 API를 올바르게 사용할 수 있다.

6. **Test Matrix 완성**
   - Test Matrix의 9개 카테고리 전체에 대해 테스트가 존재하고 통과한다.

7. **Sample 정렬 완료**
   - Canonical Sample Set의 모든 샘플이 존재한다.
   - 해당 바인딩이 구현하는 서비스 계층 샘플도 포함한다.
   - 구현하지 않는 `Target` 컴포넌트의 샘플은 제외한다.
   - 모든 샘플이 canonical API만 사용한다.
   - deprecated/legacy 경로를 사용하는 샘플이 없다.

8. **Dead code 제거 완료**
   - 리팩터링 과정에서 발생한 모든 불필요한 코드가 제거되었다.
   - deprecated alias, legacy wrapper, 사용되지 않는 import/using/require가
     남아 있지 않다.
   - Capability Matrix에서 `—`로 표시된 API의 구현 코드가 internal에도 불필요하게
     남아 있지 않다.
   - 이름 정규화로 교체된 옛 이름의 함수/메서드/타입이 남아 있지 않다.
   - 호출되지 않는 private/internal helper가 남아 있지 않다.
   - 참조되지 않는 상수, enum 값, 타입 alias가 남아 있지 않다.
   - 주석으로 처리된 코드 블록(`// removed`, `// deprecated`, `// TODO: remove`)이
     남아 있지 않다.
   - 빈 파일, 빈 클래스, 빈 모듈이 남아 있지 않다.
   - dead code는 "나중에 쓸 수 있으니까" 남겨 두지 않는다. 필요하면 git
     history에서 복원한다.

### 리팩터링 반복 규칙
- 1-7단계를 한 번 수행한 뒤, 종료 조건을 다시 점검한다.
- 앞 단계의 변경이 뒤 단계에 영향을 줄 수 있으므로, 종료 조건이 하나라도
  미충족이면 해당 단계부터 다시 수행한다.
- 종료 조건 8개가 모두 충족될 때까지 반복한다.
- "더 고칠 곳이 보이지 않는다"가 아니라 "종료 조건 8개가 모두 통과한다"가
  완료 기준이다.

### 리팩터링 금지 사항
- 구조 개선을 이유로 의미 계약을 바꾸면 안 된다.
- 내부 리팩터링으로 public API의 시그니처가 달라지면 안 된다.
  - 시그니처가 달라져야 하면 그것은 API 변경이지 리팩터링이 아니다.
- 성능 개선을 이유로 correctness를 타협하면 안 된다.
- "나중에 쓸 수 있으니까" 미리 추상화를 만들면 안 된다.
- 한 번만 쓰이는 코드를 utility/helper로 빼면 안 된다.

## Non-Normative Backlog: Implementation Follow-Ups
- 이 섹션은 규범 본문이 아니라 backlog다.
- 정책은 확정됐지만 각 바인딩 구현에 아직 남아 있을 수 있는 대표 정리 항목을
  기록한다.
- 항목은 바인딩별 리뷰와 리팩터링 backlog의 기본 체크리스트로 사용한다.

### Public vs Internal Boundary Follow-Ups

- Java:
  - public package에 남아 있는 internal 성격 타입(`SocketCore`,
    `MessagePlane`, request/reply support helper 등)을 internal package 또는
    implementation package로 이동해야 한다.
  - JPMS를 사용한다면 documented public package만 export 하도록 정리해야 한다.
- .NET:
  - `InternalsVisibleTo`는 test 지원 범위로만 제한해야 한다.
  - perf 프로젝트가 internal surface에 접근하지 않도록 assembly visibility를
    다시 닫아야 한다.
- C:
  - helper substrate와 public C binding header가 실제로 분리되면,
    `core/include/zlink.h` 중심 설명을 public C binding header 기준으로 다시
    정리해야 한다.
  - 설치되는 public header와 private substrate header의 경계를 문서와 패키징에
    함께 반영해야 한다.

### Value Validation Follow-Ups
- `RoutingId`
  - 값 객체 생성 시 길이 상한 검증
  - raw 경로가 남아 있다면 native 호출 직전 재검증
- `Duration` 기반 옵션
  - `int millis` 변환 overflow 검증
  - 음수 허용/비허용 계약 명시
- topic/filter/string identifier
  - 고정 크기 output buffer 경로의 재할당 정책 점검
  - truncation 없이 전체 문자열을 처리하는지 점검
- offset/length 기반 byte API
  - bounds 검증 일관화
- enum wrapper가 없는 raw 정수 옵션
  - enum 또는 boolean 승격 후보 조사

### Public Surface Follow-Ups
- `SendFlag` / `ReceiveFlag`
  - public method signature 제거 여부 재확인
  - public 타입 자체 삭제 또는 internal 이동 여부 결정
- monitor plane
  - `recv()` canonical surface 유지 여부 확인
- callback API
  - callback payload shape가 direct receive shape와 동일한지 재확인
- 단일 메시지 편의 메서드
  - public receive/subscribe 편의 오버로드 잔존 여부 점검

### Option Surface Follow-Ups
- raw option bag 잔존 여부 조사
- socket type별 option capability 누수 여부 조사
- option value가 아직 `int`에 머무는 항목 목록화
- context option도 같은 기준으로 typed facade 적용 여부 검토

### Error Contract Follow-Ups
- binding validation 예외와 native 예외가 혼재된 경로 조사
- 바인딩이 errno를 임의로 해석하는 경로 조사
- `EAGAIN` 외 오류를 잘못 empty/bool 경로로 숨기는 코드 조사
- blocking send 실패를 무시하는 helper/sample 조사

### Performance Follow-Ups
- hot path send/recv 경로의 숨은 복사 조사
- `Message`, `Received`, `TopicMessage` 생성 과정의 불필요한 컬렉션/배열
  할당 조사
- callback path와 direct path 비용 차이 조사
- string/topic/routing-id 변환의 인코딩/디코딩 비용 조사
- sample과 helper가 느린 대체 경로를 기본 사용법처럼 노출하는지 조사

### POSD Follow-Ups
- 얕은 래퍼만 제공하는 public 타입 조사
- 한 규칙이 여러 모듈에 흩어진 변경 파급 지점 조사
- 사용자가 internal sequencing을 알아야 하는 temporal API 조사
- facade 뒤로 숨길 수 있는 raw/native 개념 누수 지점 조사

### Ownership and Callback Follow-Ups
- send failure restore 경로와 consume 경로가 문서와 일치하는지 점검
- callback 후 frame validity 계약 재검증
- callback mode와 direct recv 충돌 시 native 계약대로 오류가 전달되는지 점검

### Test Follow-Ups
- public surface 변경마다 reflection test 존재 여부 확인
- value boundary 검증 테스트 추가
  - 예: `RoutingId` 최대 길이
  - 예: `Duration` overflow
- option negative capability 테스트 보강
- ownership/callback regression 유지 여부 확인

## Binding Requirements

| Binding | 언어 버전 | 런타임/프레임워크 | 빌드 툴 |
|---------|-----------|-------------------|---------|
| C++ | C++17 | — | CMake 3.10+ |
| .NET | C# 12 | .NET 8.0 | MSBuild |
| Java | Java 22 | JDK 22 | Gradle 8.10.2 |
| Go | Go 1.22+ | — | Go modules |
| Rust | Rust 2024 edition | MSRV 1.85+ | Cargo |
| Node | TypeScript 5.8 | Node 22+ | npm |
| Python | Python 3.9 | CPython 3.9+ | setuptools 68+ |
- 각 바인딩의 정확한 버전은 해당 프로젝트 설정 파일이 기준이다.
  - C++: `CMakeLists.txt`
  - .NET: `Zlink.csproj`
  - Java: `build.gradle`, `gradle-wrapper.properties`
  - Go: `go.mod`
  - Node: `package.json`, `tsconfig.json`
  - Python: `pyproject.toml`

## API Reference

각 바인딩은 해당 언어의 표준 문서 도구로 API 레퍼런스를 생성한다.

| Binding | 문서 도구 | 생성 명령 | 출력 위치 |
|---------|-----------|-----------|-----------|
| C++ | Doxygen | `doxygen Doxyfile` | `cpp/doxygen/html/` |
| Java | Javadoc (Gradle) | `./gradlew javadoc` | `java/build/docs/javadoc/` |
| Python | Sphinx + autodoc | `sphinx-build -b html docs docs/_build/html` | `python/docs/_build/html/` |
| Node | TypeDoc | `npx typedoc` | `node/typedoc/html/` |
| .NET | DocFX | `docfx docfx.json` | `dotnet/_site/` |
| Go | godoc / pkgsite | `go doc ./...` | (동적 서버) |
| Rust | rustdoc | `cargo doc --no-deps` | `rust/target/doc/zlink/` |

- 생성 명령은 각 바인딩 디렉터리에서 실행한다.
- 출력 디렉터리는 `.gitignore`로 추적에서 제외한다.
- 각 바인딩의 `README.*.md` 파일에 상세 생성 절차와 스코프가 명시되어 있다.

## Related Docs
- `bindings/cpp/`
- `bindings/dotnet/`
- `bindings/java/`
- `bindings/go/`
- `bindings/rust/`
- `bindings/node/`
- `bindings/python/`
