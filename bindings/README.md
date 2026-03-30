# Bindings API Policy

## 목적
이 문서는 `bindings/` 전체의 public API 정책을 정의한다.

이 문서의 목적은 각 언어 바인딩이 제각각 다른 표면과 예외 규칙을 갖는 것을
막고, `core/include/zlink.h`를 기준으로 설명 가능하고 일관된 공통 계약을
강제하는 데 있다.

이 문서는 단순 스타일 가이드가 아니다. 다음을 위한 설계 기준 문서다.
- public API 설계 기준
- 리뷰 기준
- 리팩터링 기준
- 샘플과 테스트 기준

이 문서의 의도는 다음과 같다.
- 언어별로 이름만 비슷하고 의미가 다른 API를 없앤다.
- 같은 능력을 여러 방식으로 중복 노출하는 얕은 표면을 없앤다.
- raw option bag, legacy convenience, 암묵적 ownership, 숨은 failure path를
  줄인다.
- binding 사용자가 internal sequencing, native 세부사항, hidden transport
  switch를 알지 않아도 되게 만든다.
- POSD 원칙에 맞는 깊은 모듈과 낮은 change amplification 구조를 유도한다.
- correctness뿐 아니라 비용 모델, 샘플 품질, 테스트 가능성까지 공통 기준으로
  묶는다.

기준은 항상 `core/include/zlink.h` 이다. 각 바인딩은 코어 계약을 따르되,
표현 방식은 언어 관례에 맞게 선택할 수 있다. 다만 의미 계약은 바뀌면 안 된다.

이 문서는 “각 언어가 어떻게 보일 수 있는가”보다 “각 언어가 무엇을 보장해야
하는가”를 정의한다.

## 문서 해석 규칙
- 이 문서의 정책 본문은 기본적으로 규범 문서다.
- 아래 용어는 다음 의미로 해석한다.
  - `Required`: 현재 리뷰와 구현에서 기본적으로 지켜야 하는 항목
  - `Recommended`: 강하게 권장하지만 바인딩 특성에 따라 단계적으로 적용할 수 있는 항목
  - `Target`: 장기적으로 맞춰가야 하는 목표 항목
- 별도 표시가 없으면 정책 본문은 `Required`로 본다.
- `Non-Normative Backlog: Implementation Follow-Ups` 섹션은 규범 본문이 아니라
  비규범 backlog다.
- backlog 항목은 현재 미준수 가능성을 추적하기 위한 것이며, 문서 본문의 의미 계약을 대체하지 않는다.

## 핵심 원칙
- 코어 계약은 `zlink.h`가 단일 기준이다.
- public API는 multipart 모델을 기준으로 설계한다.
- blocking과 non-blocking은 이름으로 구분한다.
- 동일한 능력을 여러 방식으로 중복 노출하지 않는다.
- 값의 의미는 `int`가 아니라 enum, boolean, value object로 올린다.
- raw option bag은 public에 노출하지 않는다.
- 바인딩은 코어의 상태 오류를 추론하지 않는다.
- 입력 값의 형식, 범위, overflow, truncation 위험은 바인딩이 먼저 막는다.
- 구조는 POSD 원칙에 따라 깊은 모듈, 정보 은닉, 낮은 change amplification을
  우선한다.
- 이 문서는 의미 계약을 우선 정의한다.
- 언어별 표면은 각 언어 관례에 맞게 달라질 수 있지만, 의미 계약은 같아야
  한다.

## POSD Structure Policy
- 바인딩 설계는 John Ousterhout의 POSD 원칙을 따른다.
- public API는 사용자가 알아야 할 개념 수를 줄여야 한다.
- 내부 구현 복잡도는 facade, value object, domain object 뒤로 숨겨야 한다.
- shallow wrapper는 지양한다.
  - 단순히 native 함수 이름만 바꾸고 새 의미를 추가하지 못하는 public
    wrapper는 늘리지 않는다.
- 같은 능력을 여러 타입과 여러 이름으로 반복 노출하지 않는다.
- 변화가 한 곳에서 끝나야 할 규칙은 한 모듈에 모은다.
  - 예: routing id 길이 제한
  - 예: send failure contract
  - 예: typed option ownership
- 시간 순서에 의존하는 temporal decomposition을 줄인다.
  - 예: 사용자가 `setOption` 조합 순서를 기억해야 하는 API 금지
- public API는 “무엇을 할 수 있는지”를 드러내고, “내부에서 어떻게 배선되는지”를
  드러내지 않아야 한다.
- 값 객체와 결과 객체는 깊은 모듈로 취급한다.
  - 호출자에게는 작은 인터페이스를 주고, 내부에서는 검증, ownership, shape
    규칙을 함께 캡슐화해야 한다

## Public Surface Rules

### Multipart Only
- send/receive public surface는 multipart 기준으로 통일한다.
- single-message receive convenience overload는 public에 두지 않는다.
- 단일 part 전송 convenience는 허용할 수 있다.
  - 예: `send(Message part)`는 `send(List<Message> parts)`의 얇은 convenience
- 수신 결과는 언어에 맞는 도메인 객체 또는 동등한 multipart 표현으로
  반환한다.

### Blocking vs Non-Blocking
- blocking API는 기본 동작 이름을 사용한다.
  - 예: `send`, `recv`, `publish`, `subscribe`,
    `receiveSubscriptionEvent`
- non-blocking API는 `try*` 이름을 사용한다.
  - 예: `trySend`, `tryRecv`, `tryPublish`, `trySubscribe`,
    `tryReceiveSubscriptionEvent`
- public `flags` 파라미터로 blocking/non-blocking을 전환하지 않는다.
- `SendFlag` / `ReceiveFlag` 같은 transport switch는 internal helper로만
  사용할 수 있다.

### Explicit Non-Blocking Send Outcome
- non-blocking send 계열은 `bool` 하나로 성공/실패를 숨기지 않는다.
- send 실패 원인 구분이 코어에 있다면 그대로 enum으로 surface 한다.
- 표준 send 결과 enum:

```text
Sent
Backpressured
NotReady
```

- managed layer는 errno heuristic으로 `Backpressured`와 `NotReady`를
  추론하지 않는다.
- 이 구분은 코어가 직접 제공해야 한다.

### Send Failure Contract
- blocking send 계열:
  - `send`, `publish`, routed `send`
  - 성공 시 정상 반환
  - 실패 시 반드시 예외 또는 언어별 오류 경로로 surface 한다
  - 실패를 `false`, `null`, empty result로 숨기지 않는다
- non-blocking send 계열:
  - `trySend`, `tryPublish`
  - `Backpressured`, `NotReady`는 정상 결과값으로 반환한다
  - `EAGAIN` 계열 외의 오류는 반드시 예외 또는 언어별 오류 경로로 surface 한다
  - managed layer가 send 실패 원인을 임의 해석해서 예외를 삼키면 안 된다
- binding helper, wrapper, sample 코드도 blocking send 실패를 무시하거나
  swallow 하면 안 된다

### Receive Outcome
- non-blocking receive 계열은 “데이터 없음”만 정상 경로로 표현한다.
- 언어별 canonical 표현은 언어 관례를 따른다.
- 예:
  - Java: `Optional<T>`
  - .NET: `bool TryReceive(out ...)`
  - Node/Python: empty/null/None 계열
- `EAGAIN` 외 오류는 예외 또는 언어별 오류 경로를 유지한다.

## Domain Object Policy
- Java, C#, Node, Python은 가능하면 `out` 파라미터나 raw tuple보다
  도메인 객체를 우선한다.
- 최소 핵심 도메인 모델:
  - `Message`
  - `RoutingId`
  - `Received`
  - `TopicMessage`
  - `SubscriptionEvent`
  - `SendResult`
- 결과 객체는 payload shape, ownership, optional routing metadata를 함께
  설명해야 한다.
- convenience는 결과 객체 메서드로 둔다.
  - 예: `singlePartOrThrow()`

## Socket Type Capability Policy
- 소켓 타입별 능력은 타입 자체에만 노출한다.
- 관련 없는 소켓은 관련 없는 함수에 접근할 수 없어야 한다.
  - 예: `PairSocket`에 publish/subscribe/xpub control surface 금지
  - 예: `StreamSocket`에 일반 connect surface 금지
- 소켓 타입별 option도 타입별 capability facade로만 노출한다.

## Option Policy

### Public Option Surface
- public raw `setOption/getOption` bag은 금지한다.
- public raw `setsockopt/getsockopt` bag도 금지한다.
- 공용 옵션은 언어에 맞는 typed surface로 노출한다.
- 특화 옵션도 언어에 맞는 capability surface로 노출한다.
- 예:
  - Java/.NET: `CommonSocketOptions`, `RouterSocketOptions`
  - Python/Node: property, namespace object, capability object, typed method set

### Option Value Types
- option 값은 가능한 한 의미 기반 타입으로 surface 한다.
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
  - managed layer의 중복 포장
  - 결과를 만들기 위한 불필요한 boxing/unboxing
- convenience API는 canonical path보다 비용이 더 크면 문서화해야 한다.
- callback path와 direct receive path는 payload shape뿐 아니라 비용 모델도
  과도하게 벌어지면 안 된다.
- zero-copy, borrowed, owned 경로가 다르면 ownership과 함께 비용 모델도
  문서화해야 한다.
- 성능 검증 강도는 언어와 런타임 특성에 따라 달라질 수 있다.
- 다만 모든 바인딩은 hot path에서 불필요한 복사, 할당, 변환을 줄이는 방향을
  기본 정책으로 삼아야 한다.

## Boundary Cost Policy
- 경계 검증은 가장 이른 안전한 위치에서 한 번 수행하는 것을 우선한다.
- 같은 검증을 여러 레이어에서 반복하면 이유가 명확해야 한다.
- 고정 크기 native struct에 들어가는 값은 truncation 대신 fail-fast 한다.
- 문자열, topic, routing id, metadata 같은 경계 값은 다음을 함께 고려한다.
  - 길이 상한
  - 인코딩 비용
  - 복사 횟수
  - 재할당 정책
- public 도메인 객체를 만들 때 불필요한 중간 컬렉션 생성은 피한다.
- helper나 sample이 느린 경로를 canonical path처럼 보이게 만들면 안 된다.

## Monitor Policy
- monitor plane도 같은 규칙을 따른다.
- public monitor receive는:
  - blocking: `recv()`
  - non-blocking: `tryRecv()`
- public `recv(flags)`는 두지 않는다.
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
  결정하고 바인딩은 그대로 surface 한다.

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

### Native Must Decide
- peer 없음
- backpressure
- readiness 부족
- callback mode와 direct recv 충돌
- socket type/state/runtime 문제
- transport, TLS, endpoint, protocol 오류

이 경우 바인딩은 native 오류를 예외로 surface 한다.
- Java: `ZlinkException`
- .NET: `ZlinkException`

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
- send 시도 시작 후 ownership이 이동하는 경로는 문서와 구현이 일치해야 한다.
- 실패 시 restore 가능한 경로와 consume되는 경로를 혼동하지 않는다.
- callback delivery와 direct receive는 동일한 payload shape를 가져야 한다.
- callback 후 frame validity는 계약으로 명확해야 한다.

## Naming Policy
- 메서드명은 언어 관례만 반영한다.
- 개념 이름은 바인딩 간 최대한 동일하게 유지한다.
- 아래 목록은 의미 기준 canonical name 이다.
- 실제 바인딩 메서드명은 각 언어 관례에 맞게 변형될 수 있다.
- 이름이 달라져도 역할 구분과 의미 계약은 같아야 한다.
- 특히 길거나 복합적인 이름은 언어별로 자연스럽게 조정할 수 있다.
- 예:
  - `receiveSubscriptionEvent`
  - `tryReceiveSubscriptionEvent`
- 추천 canonical 이름:
  - `bind`, `connect`, `close`
  - `send`, `trySend`
  - `recv`, `tryRecv`
  - `publish`, `tryPublish`
  - `subscribe`, `trySubscribe`
  - `receiveSubscriptionEvent`, `tryReceiveSubscriptionEvent`
  - `setSubscription`, `unsetSubscription`
  - `onReceive`, `onSubscribe`, `onSendReady`

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
| Python | keyword / optional | 금지 |
| Node/TS | optional / union | 금지 |
| Rust | trait bound / Option / newtype | 금지, 동작 구분 접미사만 허용 |

## Compatibility Policy
- 호환성보다 일관된 public surface를 우선할 수 있다.
- deprecated compatibility layer는 가능한 빨리 제거한다.
- 새 canonical path를 도입할 때 기존 우회 표면을 같이 남겨 두지 않는다.
- legacy flag 타입 정책:
  - public method signature에서 제거된 `SendFlag` / `ReceiveFlag`는 더 이상
    public API contract의 일부가 아니다.
  - 구현 migration 기간에는 internal helper 또는 package/private helper로만
    유지할 수 있다.
  - canonical public surface가 전 바인딩에 정착하면, public 노출 타입 자체도
    삭제 또는 internal 이동을 우선한다.

## Cross-Language Alignment

### Shared Behavioral Contract
- blocking send/receive 계열은 실패 시 예외 또는 언어별 오류 경로
- non-blocking receive는 “데이터 없음”만 비예외 경로
- non-blocking send는 explicit outcome
- multipart-only
- typed option surface

### Language-Specific Return Style
- C API
  - raw contract와 errno
  - multipart-only 기준 surface
  - blocking API + explicit non-blocking entry
- C++
  - RAII와 typed wrapper
  - multipart-only 기준 surface
  - `try*`와 explicit send outcome을 지원해야 한다
- .NET
  - `Try*` + `out` + enum result
  - multipart-only 기준 surface
- Java
  - domain object + `Optional<T>` + enum result
  - multipart-only 기준 surface
- Node/Python
  - 언어 관례를 따르되 의미 계약은 동일
  - multipart-only 기준 surface
  - non-blocking receive는 empty/null/None 계열
  - non-blocking send는 bool이 아니라 explicit outcome을 사용해야 한다

언어별 표면은 달라도 의미 계약은 같아야 한다.

### Cross-Language Capability Table
| Area | C API | C++ | .NET | Java | Node | Python |
|---|---|---|---|---|---|---|
| Multipart-only public surface | Required | Required | Required | Required | Required | Required |
| Blocking API named directly | Yes | Yes | Yes | Yes | Yes | Yes |
| Non-blocking receive uses `try*` | N/A raw entry | Required | Required | Required | Required | Required |
| Non-blocking send explicit outcome | Core enum/result | Required | Required | Required | Required | Required |
| Public flags overloads | Raw C only | High-level public surface: No | No | No | No | No |
| Typed option surface | N/A raw C options | Required | Required | Required | Required | Required |

## Testing Policy
- reflection/surface test로 canonical public API를 고정한다.
- 공통 검증 항목:
  - public flag overload 제거
  - single-message receive convenience 제거
  - `try*` 추가 여부
  - 타입별 capability 분리 여부
  - raw option bag 비노출
- behavior test로 blocking/non-blocking 계약을 검증한다.
- ownership 회귀 테스트를 유지한다.
- callback mode와 direct mode의 충돌 규칙도 테스트한다.
- 정책 변경 시 필수 테스트 규칙:
  - public surface 변경: reflection/surface test 동반
  - blocking/non-blocking 계약 변경: behavior test 동반
  - ownership/receive shape 변경: callback regression 또는 ownership test 동반
  - option surface 변경: typed option reflection test와 negative capability test 동반
- `Recommended`: 성능/비용 모델 관련 최소 검증:
  - hot path에 불필요한 복사나 할당이 새로 들어가지 않았는지 점검
  - callback/direct path의 비용 모델이 과도하게 벌어지지 않는지 점검
  - helper/sample이 blocking send 실패를 숨기거나 느린 경로를 canonical로
    유도하지 않는지 점검

## Test Matrix
- 이 섹션은 각 바인딩이 최소한 가져야 할 테스트 항목을 정리한다.
- 바인딩별 표면은 달라도 아래 의미 계약은 모두 검증해야 한다.
- `Surface Tests`, `Send/Receive Behavior Tests`, `Send Failure Contract Tests`,
  `Receive Failure Contract Tests`, `Boundary Validation Tests`, `Option Tests`,
  `Ownership Tests`, `Monitor Tests`는 기본적으로 `Required`다.
- `Performance and Cost Tests`, `Sample and Helper Tests`는 기본적으로
  `Recommended`다.

### Surface Tests
- canonical public API reflection/surface test
- removed legacy surface 부재 확인
  - public flags overload 제거
  - single-message receive convenience 제거
  - raw option bag 비노출
- socket type capability 분리 확인
- typed option surface 존재 확인
- monitor canonical surface 존재 확인
  - `recv()`
  - `tryRecv()`

### Send/Receive Behavior Tests
- blocking `send` 성공 경로
- blocking `recv` 성공 경로
- blocking `publish` 성공 경로
- blocking `subscribe` 성공 경로
- routed blocking `send` 성공 경로
- non-blocking `tryRecv` empty path
- non-blocking `trySubscribe` empty path
- non-blocking `tryReceiveSubscriptionEvent` empty path
- non-blocking `trySend` success path
- non-blocking `tryPublish` success path

### Send Failure Contract Tests
- blocking `send` failure가 예외 또는 언어별 오류 경로로 surface 되는지 확인
- blocking `publish` failure가 예외 또는 언어별 오류 경로로 surface 되는지 확인
- `trySend` backpressure 결과 확인
- `trySend` not-ready 결과 확인
- `tryPublish` backpressure 또는 not-ready 결과 확인
- `EAGAIN` 외 오류가 `try*`에서 swallow 되지 않는지 확인

### Receive Failure Contract Tests
- callback mode와 direct recv 충돌 시 native 계약대로 surface 되는지 확인
- direct recv 불가 상태에서 empty/null로 숨기지 않는지 확인
- `EAGAIN`만 empty/non-success 결과로 처리되는지 확인

### Boundary Validation Tests
- `RoutingId` 최대 길이 경계
- `RoutingId` 초과 길이 fail-fast
- `Duration -> int millis` overflow 경계
- offset/length bounds 검증
- null 불가 인자 검증
- enum 범위 밖 값 검증
- topic/filter/string identifier 길이 경계

### Option Tests
- common option typed getter/setter
- socket type별 typed option getter/setter
- 잘못된 소켓 타입에서 option capability 접근 차단
- raw integer 대신 enum/boolean surface가 제공되는지 확인

### Ownership Tests
- send 성공 시 ownership 이동 계약
- send 실패 시 restore 또는 caller ownership 유지 계약
- recv 결과 ownership 계약
- callback 후 frame validity 계약
- multipart receive shape와 callback delivery shape 일치 여부

### Monitor Tests
- blocking monitor `recv` 성공 경로
- non-blocking monitor `tryRecv` empty path
- monitor callback/state 변화와 data plane readiness 일치 여부

### Performance and Cost Tests
- hot path send에서 불필요한 복사/할당 회귀 여부
- hot path recv에서 불필요한 복사/할당 회귀 여부
- callback path와 direct path 비용 모델 과도한 벌어짐 여부
- 느린 fallback path가 canonical helper로 노출되지 않는지 점검

### Sample and Helper Tests
- sample code가 canonical API만 사용하는지 확인
- helper가 blocking send 실패를 swallow 하지 않는지 확인
- helper가 deprecated/legacy surface를 우회 호출하지 않는지 확인

## Sample Policy
- 샘플은 문서이자 실행 가능한 검증 수단이어야 한다.
- 샘플은 실제 메시징을 수행하고 결과를 확인해야 한다.
- 샘플은 canonical public API만 사용해야 한다.
- deprecated, legacy, raw option bag, raw flags 경로를 샘플에서 사용하면 안 된다.
- 샘플 정책은 기본적으로 `Recommended`다.
- 다만 공개적으로 배포되는 바인딩, 릴리즈 대상 바인딩, 또는 사용자 onboarding 경로를
  제공하는 바인딩에는 `Required`로 간주한다.

### Sample Structure Rules
- recv/direct 버전과 callback 버전은 반드시 개별 파일로 분리한다.
- 한 파일이 두 수신 모델을 동시에 설명하면 안 된다.
- 샘플 파일명만 보고도 패턴을 알 수 있어야 한다.
- 권장 이름 패턴:
  - `*_recv_sample`
  - `*_callback_sample`
  - `*_monitor_sample`
- 각 샘플은 하나의 핵심 패턴만 설명해야 한다.
  - direct recv pattern
  - callback pattern
  - routed messaging pattern
  - pub/sub pattern
  - monitor pattern

### Sample Content Rules
- 샘플은 핵심 로직이 한눈에 보이게 작성한다.
- 보일러플레이트와 과도한 helper 의존을 줄인다.
- 핵심 메시징 흐름은 샘플 본문에서 직접 보여야 한다.
- 샘플은 최소한 다음 흐름을 드러내야 한다.
  - context/socket 생성
  - endpoint bind/connect
  - subscription 설정이 필요한 경우 subscription 설정
  - send/publish
  - recv/subscribe 또는 callback 설치
  - 수신 결과 확인
  - 종료/정리

### Sample Runtime Verification Rules
- 모든 샘플은 실제 메시지를 주고받아 동작을 확인해야 한다.
- compile-only 예제로 끝나면 안 된다.
- 샘플은 최소한 아래 중 해당되는 항목을 검증해야 한다.
  - expected payload 수신
  - expected topic 수신
  - expected routing id 수신
  - callback 실제 호출
  - monitor event 실제 수신
- 샘플은 성공 시 명확한 성공 출력 또는 zero exit code를 가져야 한다.
- 실패 시 예외 또는 non-zero exit code로 실패를 드러내야 한다.

### Sample Coverage Expectations
- 각 바인딩은 최소한 다음 샘플 그룹을 목표(`Target`)로 한다.
- direct recv 계열
  - PAIR 또는 동등한 기본 send/recv
  - PUB/SUB 또는 동등한 topic publish/subscribe
  - ROUTER/DEALER 또는 동등한 routed messaging
  - STREAM direct recv
- callback 계열
  - direct recv callback
  - topic subscribe callback
  - STREAM callback
  - send-ready 또는 유사 readiness callback이 있으면 해당 샘플
- monitor 계열
  - readiness/state event 확인 샘플
- service/spot 계열이 있는 바인딩은 canonical sample을 별도로 제공한다.

### Stream Socket Policy
- STREAM socket은 direct recv 방식과 callback 방식 둘 다 지원해야 한다.
- 따라서 각 바인딩은 STREAM에 대해 다음 둘을 모두 가져야 한다.
  - blocking/non-blocking direct receive surface
  - callback receive surface
- STREAM sample도 recv 버전과 callback 버전을 개별 파일로 제공하는 것을 원칙으로 한다.
- STREAM payload는 zlink의 canonical message contract를 따른다.
- `len32be`, length-prefixed framing, 또는 그와 동등한 별도 프레이밍 규약은
  zlink binding public contract의 일부가 아니다.
- 바인딩은 STREAM을 설명할 때 존재하지 않는 framing 개념을 만들어 문서화하거나
  sample에 암묵적으로 넣으면 안 된다.

### Sample Execution Script Policy
- 각 바인딩은 전체 샘플을 실행해서 동작 확인할 수 있는 스크립트를 제공해야 한다.
- 스크립트는 repository 안에 두고 반복 실행 가능해야 한다.
- 스크립트는 샘플 목록을 명시적으로 실행해야 한다.
- 스크립트는 성공/실패를 요약해서 보여줘야 한다.
- 일부 샘플만 수동으로 돌리는 방식에 의존하면 안 된다.
- 이 항목은 샘플을 공식 제공하는 바인딩에서는 `Required`, 초기 단계 바인딩에서는
  `Recommended`다.
- 권장 형태:
  - `run_samples.sh`
  - `run_samples.ps1`
  - language-specific task runner entry

### Sample Verification Requirements
- 새 canonical sample 추가 시 다음을 같이 확인해야 한다.
  - 개별 샘플 단독 실행 성공
  - 전체 샘플 실행 스크립트 포함
  - 전체 샘플 실행 스크립트에서 성공
- sample review 시 다음을 확인한다.
  - recv/callback 버전이 개별 파일로 분리되어 있는가
  - canonical API만 사용하는가
  - 핵심 로직이 helper 뒤에 숨지 않았는가
  - 실제 메시징을 하고 결과를 확인하는가
  - 전체 샘플 실행 스크립트에 포함되어 있는가

## Perf Policy
- perf 코드는 데모가 아니라 바인딩 라이브러리의 성능을 측정하고 개선하기 위한
  코드다.
- perf 의 1차 목적은 바인딩 레이어의 비용을 드러내고, 병목과 회귀를 식별하고,
  개선 작업의 전후 차이를 측정하는 것이다.
- perf 코드는 다음 기준을 반드시 따른다.
  - `core/perf` 에서 제공하는 패턴과 시나리오를 기준으로 한다
  - `doc/perf` 정책을 준수한다
- core perf가 C API 형태로 제공되더라도, 각 언어 perf 코드는 성능 테스트의
  목적을 해치지 않는 범위에서 해당 언어의 스타일에 맞게 작성한다.
- 즉 perf 코드는 다음 둘을 동시에 만족해야 한다.
  - core perf와 비교 가능한 시나리오
  - 각 언어 사용자에게 자연스러운 스타일
- perf 정책은 성능 측정 surface를 공식 제공하는 바인딩에서는 `Required`다.
- perf 코드를 아직 제공하지 않는 바인딩에는 `Target`으로 본다.

### Perf Design Rules
- perf 코드는 바인딩 성능을 측정하는 코드여야 한다.
- benchmark harness 자체의 복잡도, 불필요한 helper, 과도한 추상화로 핵심
  비용이 가려지면 안 된다.
- 핵심 로직의 가시성이 높아야 한다.
- send/recv/publish/subscribe/callback 핵심 경로가 perf 파일 본문에서
  직접 읽혀야 한다.
- 느린 fallback path, extra logging, debug-only conversion을 perf hot path에
  넣으면 안 된다.
- perf 는 correctness 예제가 아니라 cost measurement 코드이므로, 편의성보다
  측정 충실도를 우선한다.

### Perf Structure Rules
- 각 perf 패턴은 별도 파일로 제공해야 한다.
- 한 파일이 여러 messaging 패턴을 섞으면 안 된다.
- 패턴별 파일 분리 예:
  - pair throughput/latency
  - pubsub throughput/latency
  - routed messaging throughput/latency
  - stream throughput/latency
  - callback delivery cost
- direct recv 패턴과 callback 패턴도 가능하면 별도 파일로 분리한다.
- 파일명만 보고 어떤 패턴의 perf 인지 알 수 있어야 한다.

### Perf Alignment Rules
- 각 언어 perf 는 `core/perf` 의 목적과 형태를 기준으로 맞춘다.
- 다만 구현 표면은 각 언어 스타일을 반영할 수 있다.
  - C++: RAII, typed wrapper
  - .NET: idiomatic object model
  - Java: domain object / typed API
  - Node/Python: 해당 언어 관례
- 단, 언어 스타일을 반영한다는 이유로 측정 대상이 바뀌면 안 된다.
- perf 간 비교 가능성을 유지하려면 다음을 맞춰야 한다.
  - 메시징 패턴
  - 측정 단위
  - warmup / run 구조
  - 성공 조건과 종료 조건

### Perf Verification Requirements
- perf 코드는 실제 측정 가능한 실행 entry를 제공해야 한다.
- perf 실행 경로는 문서화되어야 한다.
- 새로운 perf 추가 시 다음을 확인해야 한다.
  - 개별 perf 실행 가능
  - 패턴 설명 가능
  - 핵심 메시징 경로가 보이는지 확인
  - core perf / doc perf 정책과 충돌하지 않는지 확인

### Perf Review Checklist
- 이 perf 가 바인딩 라이브러리 비용을 측정하고 있는가
- harness 복잡도가 핵심 비용을 가리고 있지 않은가
- 핵심 로직 가시성이 충분한가
- 각 패턴이 별도 파일로 분리되어 있는가
- 언어 스타일은 반영하되 측정 목적을 해치지 않았는가
- `core/perf` 패턴과 정렬되어 있는가
- `doc/perf` 정책을 준수하는가

## Review Checklist
- public API가 multipart-only인가
- blocking/non-blocking이 이름으로 분리되었는가
- public flags 오버로드가 남아 있지 않은가
- raw option bag이 public에 남아 있지 않은가
- option 값이 enum/boolean/value object로 승격되었는가
- 타입별 capability가 제대로 닫혀 있는가
- blocking send 실패가 예외 또는 오류 경로로 반드시 surface 되는가
- `trySend`가 `Backpressured`/`NotReady`만 결과값으로 반환하고 나머지를 숨기지 않는가
- binding이 truncation/overflow를 선검증하는가
- native 상태 오류를 managed layer가 임의 추론하지 않는가
- reflection test와 behavior test가 같이 있는가
- 값 객체 검증과 호출 직전 검증의 책임 위치가 설명 가능한가
- legacy flag 타입이 public contract에서 제거되었는가

## Non-Normative Backlog: Implementation Follow-Ups
- 이 섹션은 규범 본문이 아니라 backlog다.
- 정책은 확정됐지만 각 바인딩 구현에 아직 남아 있을 수 있는 대표 정리 항목을
  기록한다.
- 항목은 바인딩별 리뷰와 리팩터링 backlog의 기본 체크리스트로 사용한다.

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
  - `recv()` / `tryRecv()` canonical surface 유지 여부 확인
- callback API
  - callback payload shape가 direct receive shape와 동일한지 재확인
- single-message convenience
  - public receive/subscribe convenience overload 잔존 여부 점검

### Option Surface Follow-Ups
- raw option bag 잔존 여부 조사
- socket type별 option capability 누수 여부 조사
- option value가 아직 `int`에 머무는 항목 목록화
- context option도 같은 기준으로 typed facade 적용 여부 검토

### Error Contract Follow-Ups
- binding validation 예외와 native 예외가 혼재된 경로 조사
- managed layer가 errno를 임의 해석하는 경로 조사
- `EAGAIN` 외 오류를 잘못 empty/bool 경로로 숨기는 코드 조사
- blocking send 실패를 무시하거나 swallow 하는 helper/sample 조사

### Performance Follow-Ups
- hot path send/recv 경로의 숨은 복사 조사
- `Message`, `Received`, `TopicMessage` 생성 과정의 불필요한 컬렉션/배열
  할당 조사
- callback path와 direct path 비용 차이 조사
- string/topic/routing-id 변환의 인코딩/디코딩 비용 조사
- sample과 helper가 느린 fallback 경로를 canonical usage처럼 노출하는지 조사

### POSD Follow-Ups
- shallow wrapper만 제공하는 public 타입 조사
- 한 규칙이 여러 모듈에 흩어진 change amplification 지점 조사
- 사용자가 internal sequencing을 알아야 하는 temporal API 조사
- facade 뒤로 숨길 수 있는 raw/native 개념 누수 지점 조사

### Ownership and Callback Follow-Ups
- send failure restore 경로와 consume 경로가 문서와 일치하는지 점검
- callback 후 frame validity 계약 재검증
- callback mode와 direct recv 충돌 시 native 계약대로 surface 되는지 점검

### Test Follow-Ups
- public surface 변경마다 reflection test 존재 여부 확인
- value boundary 검증 테스트 추가
  - 예: `RoutingId` 최대 길이
  - 예: `Duration` overflow
- option negative capability 테스트 보강
- ownership/callback regression 유지 여부 확인

## Related Docs
- `bindings/cpp/`
- `bindings/dotnet/`
- `bindings/java/`
- `bindings/node/`
- `bindings/python/`
