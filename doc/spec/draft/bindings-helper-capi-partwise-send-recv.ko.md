[스펙 목차](../README.ko.md)

# Draft -- Bindings Helper C API for Part-wise Send/Recv

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`에 없는 API나 정책을
> 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 기존 공개 C API 옆에 **bindings 구현용 helper C API**를 추가하는
방향을 정의한다.

핵심 목표는 아래와 같다.

- multipart 메시지를 한 번에 `zlink_msg_t* + part_count` 배열로 올리는 현재
  모델 아래에, bindings 구현이 사용할 수 있는 **part-by-part send/recv helper**
  를 추가한다.
- 이 helper는 기본 socket `send/recv`뿐 아니라 `publish`, routed
  `request/reply`, service-level `request/reply`, subscribe 계열에도 같은
  방식으로 적용할 수 있어야 한다.
- bindings 라이브러리는 이 helper 위에 지금의 `Received`, `List<Message>`,
  callback, request/reply convenience API를 계속 제공할 수 있게 한다.
- Java 같은 바인딩이 `Received + Message[]` aggregate를 매번 먼저 만들지 않고도
  hot path를 더 얇게 구현할 수 있게 한다.
- 특정 `header/body` 패턴으로 사용성을 제한하지 않는다. 일반 multipart를 그대로
  지원한다.

이 문서의 목표는 공개 C API를 바로 바꾸는 것이 아니다. 먼저 기존 공개 C API
옆에 bindings 친화적인 helper를 두고, 그 위에 각 bindings 공개 API를 다시
올릴 수 있게 하는 것이다.

관련 문서:
- C binding 계층 계획은
  [c-binding-layer-plan.ko.md](c-binding-layer-plan.ko.md) 에서 별도로 다룬다.

## 2. 배경

현재 공개 C API의 기본 send/recv 표면은 multipart aggregate 중심이다.

- `zlink_send()` / `zlink_send_rid()`는 `zlink_msg_t *parts_`와
  `part_count_`를 한 번에 받는다.
- `zlink_recv()` / `zlink_router_recv()`는 `parts_out_`와
  `part_count_out_`로 multipart aggregate를 한 번에 돌려준다.
- `zlink_publish()`, `zlink_router_request()`, `zlink_router_reply()`,
  `zlink_dealer_request()`, `zlink_spot_send_*()`, `zlink_spot_request_*()`,
  `zlink_spot_reply_*()`, `zlink_spot_subscribe()`도 같은 aggregate payload
  모델을 쓴다.

이 모델은 C 사용자에게는 단순하지만, bindings 구현 입장에서는 아래 비용을 만든다.

- core가 먼저 multipart aggregate를 만든다.
- bindings는 그 aggregate를 다시 `Message[]`, `Received`, `RoutingId` 같은
  언어별 객체로 올린다.
- 결과적으로 "네이티브 aggregate 생성 -> 언어 객체 aggregate 생성"이
  연속으로 일어난다.

이 비용은 특히 Java에서 크게 드러났다.

- `Message`와 `Received` materialization 비용이 크다.
- FFM 자체보다 "aggregate를 만들고 다시 풀어 담는 과정"이 더 큰 병목으로
  보인다.
- JNI로 일부 primitive를 감싸면 `msg_t` 연산 자체는 더 빨라질 수 있지만,
  aggregate recv 모델이 그대로면 그 이점이 대부분 사라진다.

즉 지금 병목은 "복사 한 번"보다 **메시지 묶음 모델 자체**에 더 가깝다.

## 3. 설계 원칙

이번 초안은 아래 원칙을 기준으로 한다.

### 3.1 현재 공개 multipart API는 그대로 유지한다

다음 계층은 계속 필요하다.

- `zlink_send(parts, part_count, ...)`
- `zlink_recv(..., parts_out, part_count_out, ...)`
- `zlink_router_recv(...)`
- `zlink_publish(...)`
- `zlink_spot_subscribe(...)`
- `zlink_*_request(...)`, `zlink_*_reply(...)`

이 API들은 C 사용자와 기존 bindings compatibility를 위해 그대로 유지하는 쪽이
맞다.

이번 초안은 이 API들을 곧바로 제거하는 문서가 아니다. 기존 공개 C API는 그대로
두고, bindings 구현이 사용할 수 있는 더 낮은 수준 helper를 옆에 추가하는
문서다.

### 3.2 helper는 multipart 일반성을 유지해야 한다

이 초안은 `header/body` 두 파트 전용 API를 기본 방향으로 두지 않는다.

그 이유는 아래와 같다.

- 실사용에서는 `header/body`, `name/payload`, `meta/header/body`처럼 형태가
  다양하다.
- 두 파트 전용 helper는 특정 패턴에는 유리하지만, 일반 multipart 라이브러리로서의
  성격을 약하게 만든다.
- bindings는 특정 패턴을 강요하지 않아야 한다.

따라서 helper는 "한 번에 한 part를 보내고 받으며, 현재 메시지에 part가 더
남았는지 알 수 있는 모델"이 되어야 한다.

### 3.3 bindings는 현재 convenience 계층을 유지한다

bindings는 계속 아래 같은 상위 모델을 제공할 수 있어야 한다.

- `Received`
- `List<Message>`
- request/reply convenience
- callback 기반 dispatch

즉 공개 C 표면은 유지하고, bindings는 helper 위에 현재 형태의 상위 API를
올리는 방향이 맞다.

### 3.4 routed metadata는 part 경계와 함께 설명돼야 한다

`ROUTER`, `SPOT`, request/reply는 payload part 외에도 metadata가 있다.

- source routing id
- source spot routing id
- request sequence

part-by-part recv 모델로 바꾸더라도, 이 metadata가 현재 multipart 메시지와 어떻게
묶이는지 규칙이 분명해야 한다.

## 4. helper 계층 적용 범위

helper는 특정 함수 하나만을 위한 것이 아니라, "multipart payload를 보내거나 받는
모든 계열"에 공통으로 적용할 수 있어야 한다.

최소 적용 대상은 아래와 같다.

- 기본 socket send/recv (`PAIR`, `DEALER`, `STREAM` 포함)
- routed send/recv
- publish/subscribe
- dealer/router request/reply
- spot send/request/reply/subscribe

즉 helper는 `send` 하나만의 보조 함수가 아니라, 기존 aggregate multipart 계열
전체 아래에 놓이는 공통 하위 계층이다.

## 5. send helper 초안

### 5.1 part 경계 표현

helper send는 "part 하나를 보낸다"는 모델로 둔다.

이때 part 경계는 기존 `zlink_send_flags_t`에 다시 섞지 않고, helper 전용 인자로
분리한다.

```c
typedef enum zlink_part_flag_t {
  ZLINK_PART_FINAL = 0,
  ZLINK_PART_MORE = 1
} zlink_part_flag_t;
```

- `ZLINK_PART_MORE`이면 뒤에 같은 논리 메시지의 다음 part가 더 이어진다.
- `ZLINK_PART_FINAL`이면 현재 part가 마지막 part다.

즉 `flags_`는 `DONTWAIT` 같은 제출 동작만 표현하고, multipart 경계는
`zlink_part_flag_t`가 표현한다.

이 규칙은 새로운 wire 동작을 추가하는 것이 아니다. core 내부는 이미
`ZLINK_SNDMORE`를 사용해 multipart frame 경계를 유지하고 있다. 이번 helper는
그 내부 경계를 bindings가 직접 쓸 수 있는 공개 helper 계약으로 드러내는 것이다.

중요한 점은 아래와 같다.

- `MORE` 호출도 내부 queue나 socket 경로로 frame이 진행될 수 있다.
- 하지만 `MORE` 단계는 아직 **논리 메시지 제출 완료**가 아니다.
- helper 계약에서 논리 메시지는 `FINAL` 호출이 성공했을 때만 닫힌다.
- 따라서 bindings와 상위 convenience API는 `FINAL` 이전 상태를 "열린 multipart
  시퀀스"로 취급해야 한다.

즉 "실제 전송이 전혀 일어나지 않는다"가 아니라, **논리 메시지 commit 경계가
`FINAL`에 있다**고 보는 것이 맞다.

### 5.1.1 열린 multipart 시퀀스 규칙

같은 handle에서 `MORE`로 시작한 multipart 시퀀스는 `FINAL`로 닫히기 전까지
다음 규칙을 따른다.

- 열린 multipart 시퀀스는 다른 논리 메시지와 interleave되면 안 된다.
- 즉 `MORE` 이후에는 같은 시퀀스의 다음 `*_part()`만 이어서 호출할 수 있다.
- `FINAL`이 성공하면 열린 시퀀스는 닫히고, 그 다음 새 논리 메시지를 시작할 수
  있다.
- `MORE` 이후 다른 종류의 send helper를 섞어 호출하는 것은 계약 위반으로 본다.

이 규칙이 있어야 aggregate convenience API가 helper 위에 안정적으로 다시
올라갈 수 있고, bindings도 열린 multipart state를 단순하게 추적할 수 있다.

### 5.2 기본 send helper

```c
ZLINK_EXPORT zlink_submit_result_t zlink_send_part (
  void *s_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);

ZLINK_EXPORT zlink_submit_result_t zlink_send_part_rid (
  void *s_,
  const zlink_routing_id_t *target_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);
```

### 5.3 publish helper

```c
ZLINK_EXPORT zlink_submit_result_t zlink_publish_part (
  void *subject_,
  const char *topic_id_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_);
```

### 5.4 request helper

request 계열 helper도 payload는 part-by-part로 주되, request completion 계약은
현재와 같은 request 단위로 유지한다.

```c
ZLINK_EXPORT zlink_submit_result_t zlink_router_request_part (
  void *router_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_);

ZLINK_EXPORT zlink_submit_result_t zlink_dealer_request_part (
  void *dealer_,
  zlink_msg_t *part_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_,
  zlink_reply_handler_fn handler_,
  void *userdata_);
```

의미는 아래와 같다.

- 첫 part부터 마지막 part까지 여러 번 helper를 호출해 요청 payload를 구성한다.
- `part_flag_ == FINAL`이 들어간 마지막 호출에서만 실제 submit이 완료된다.
- `timeout_ms_`, `handler_`, `userdata_`는 같은 논리 요청 전체에 귀속된다.

같은 원칙은 `spot request/reply` 계열에도 그대로 확장할 수 있다.

### 5.4.1 request/reply part helper 실패 규칙

request 계열은 일반 send보다 submit 경계를 더 엄격하게 적어야 한다.

- `MORE` 호출 성공:
  - 현재 요청 시퀀스에 part 하나가 추가된다.
  - 아직 request submission은 완료되지 않는다.
- `MORE` 호출 실패:
  - 현재 열린 요청 시퀀스는 실패로 간주한다.
  - 이미 시작된 요청 시퀀스는 내부에서 폐기되고, caller는 같은 요청을
    처음부터 다시 구성해야 한다.
- `FINAL` 호출 성공:
  - 마지막 part가 추가되고 request submission이 완료된다.
  - 이 시점부터 timeout, reply callback, request sequence 추적이 시작된다.
- `FINAL` 호출 실패:
  - request submission은 완료되지 않는다.
  - 이미 열린 요청 시퀀스는 내부에서 폐기된다.
  - caller는 같은 part 시퀀스를 이어서 복구하는 것이 아니라, 새 요청으로 다시
    시작해야 한다.

즉 request helper는 "중간 상태를 이어 붙여 복구하는 API"가 아니라,
"실패하면 현재 열린 요청 시퀀스를 버리고 처음부터 다시 구성하는 API"로 본다.

이 규칙은 현재 aggregate request 구현이 내부 multipart send transaction과
rollback에 의존하는 방식과도 맞는다.

## 6. recv helper 초안

### 6.1 기본 recv helper

helper recv는 "part 하나를 caller가 준 `zlink_msg_t`에 받는다"는 모델로 둔다.

이 helper는 기본 socket recv 계열에 해당한다. 즉 `PAIR`, `DEALER`, `STREAM`처럼
현재 `zlink_recv()`를 쓰는 family는 모두 이 모델로 다시 쌓을 수 있어야 한다.

```c
ZLINK_EXPORT zlink_recv_result_t zlink_recv_part (
  void *s_,
  zlink_routing_id_t *source_rid_out_,
  zlink_msg_t *part_out_,
  int *has_more_out_,
  zlink_recv_flags_t flags_);
```

- 성공 시 `part_out_`에 현재 논리 메시지의 한 part가 들어간다.
- `has_more_out_ != 0`이면 같은 논리 메시지에 아직 다음 part가 더 남았다는 뜻이다.
- `has_more_out_ == 0`이면 현재 part가 마지막 part다.
- `source_rid_out_`는 현재 메시지의 source routing id를 돌려준다.

`recv_more`는 별도 flag로 다시 넣지 않는다. recv는 결과로 받는 것이 더 맞다.

### 6.2 subscribe helper

topic 계열 helper는 추가 metadata를 함께 준다.

```c
ZLINK_EXPORT zlink_recv_result_t zlink_subscribe_part (
  void *sub_,
  zlink_routing_id_t *source_rid_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_msg_t *part_out_,
  int *has_more_out_,
  zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_recv_result_t zlink_spot_subscribe_part (
  void *spot_,
  zlink_routing_id_t *source_rid_out_,
  char *service_name_out_,
  size_t *service_name_len_out_,
  char *topic_id_out_,
  size_t *topic_id_len_out_,
  zlink_msg_t *part_out_,
  int *has_more_out_,
  zlink_recv_flags_t flags_);
```

### 6.3 routed recv helper

`ROUTER`, `SPOT`, request/reply는 metadata가 더 있으므로 routed variant를 별도로
둔다.

```c
ZLINK_EXPORT zlink_recv_result_t zlink_router_recv_part (
  void *router_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t *part_out_,
  int *has_more_out_,
  zlink_recv_flags_t flags_);

ZLINK_EXPORT zlink_recv_result_t zlink_spot_recv_part (
  void *spot_,
  const zlink_routing_id_t **source_node_rid_out_,
  const zlink_routing_id_t **source_spot_rid_out_,
  uint64_t *request_seq_out_,
  zlink_msg_t *part_out_,
  int *has_more_out_,
  zlink_recv_flags_t flags_);
```

이 초안에서는 같은 논리 메시지에 속한 각 part recv 호출에서 metadata가 같은 값으로
반복돼도 된다고 본다. 이렇게 해야 bindings가 첫 part에서만 metadata를 읽어
캐시하든, 매 호출에서 읽든 규칙이 단순해진다.

### 6.3.1 routed metadata lifetime

`source_node_rid_out_`, `source_spot_rid_out_`로 돌려주는 포인터는 caller-owned
storage가 아니다. helper는 현재 논리 메시지에 대응하는 내부 metadata view를
가리켜도 된다.

구현과 bindings는 아래 규칙을 기준으로 맞춘다.

- 같은 논리 메시지의 part를 연속으로 받는 동안에는 같은 값을 반복해서 돌려줄 수
  있다.
- 이 포인터는 최소한 **다음 recv-like 호출 전까지** 유효해야 한다.
- caller가 다음 recv-like 호출 뒤에도 값을 보존하려면 자기 storage로 복사해야
  한다.
- `request_seq_out_`는 값 복사이므로 별도 lifetime 규칙이 필요 없다.

이 규칙은 현재 aggregate recv의 "thread-local view를 바로 넘기고, 장기 보관이
필요하면 caller가 복사한다"는 방향과도 일관된다.

## 7. ownership 규칙 초안

### 7.1 send

`zlink_*_part()` helper는 기존 aggregate send와 같은 소유권 규칙을 따른다.

- 호출이 시작되면 `part_` ownership은 callee로 넘어간다.
- 어떤 return path에서도 caller는 `part_`를 moved-from handle로 취급해야 한다.
- caller는 같은 `zlink_msg_t`를 다시 사용하려면 새로 init해야 한다.

추가로 multipart 시퀀스 수준에서는 아래 규칙을 둔다.

- `MORE`가 성공한 뒤 열린 시퀀스가 존재하는 동안, caller는 그 시퀀스를 `FINAL`로
  닫거나 실패로 끝났다고 보고 새 시퀀스를 처음부터 다시 시작해야 한다.
- helper는 열린 시퀀스를 caller가 부분 복구하는 모델을 제공하지 않는다.

### 7.2 recv

`zlink_*_recv_part()` helper는 caller-provided message storage 모델을 따른다.

- caller는 `part_out_`를 유효한 `zlink_msg_t` storage로 제공한다.
- 성공 시 payload ownership은 caller에게 있다.
- caller는 사용 후 `zlink_msg_close(part_out_)`를 호출해야 한다.
- 다음 recv 전에 같은 `part_out_`를 재사용하려면 caller가 다시 init-ready 상태로
  준비해야 한다.

이 모델은 현재 `parts_out_` aggregate view처럼 "parts array는 thread-local view고
caller가 `free()`하면 안 된다"는 규칙보다 단순하다.

## 8. 기존 aggregate API와의 관계

### 8.1 aggregate send

현재 aggregate send는 helper 위에 다시 올릴 수 있다.

```c
for each part except last:
  zlink_send_part(..., maybe_DONTWAIT, ZLINK_PART_MORE)

last part:
  zlink_send_part(..., maybe_DONTWAIT, ZLINK_PART_FINAL)
```

즉 `zlink_send()` / `zlink_send_rid()`는 convenience API로 남길 수 있다.
같은 방식으로 `zlink_publish()`, `zlink_router_request()`,
`zlink_dealer_request()`, `zlink_spot_send_*()`, `zlink_spot_request_*()`,
`zlink_spot_reply_*()`도 helper 위에 다시 올릴 수 있다.

### 8.2 aggregate recv

현재 aggregate recv도 helper 위에 다시 올릴 수 있다.

```c
recv first part
while has_more:
  append storage for next part
  recv next part
```

즉 current public recv 표면을 바꾸지 않고도, bindings 구현은 part-by-part helper
위로 다시 정리할 수 있다.

### 8.3 bindings

bindings는 아래 중 하나를 선택할 수 있다.

- 현재처럼 `Received` aggregate를 만들어 반환한다.
- 더 낮은 수준 public API를 추가해서 `Message 하나 + hasMore` 모델을 노출한다.
- 특정 hot path에서만 helper를 직접 써서 aggregate materialization을 줄인다.

즉 helper 계층은 아래를 한 번에 단순화하기 위한 것이다.

- `send(List<Message>)`
- `dealer.recv()`
- `publish(List<Message>)`
- `subscribe()`
- `request(List<Message>)`
- routed `recv -> Received`
- service `recv -> Received`

## 9. bindings 관점의 기대 효과

이 모델이 도움이 되는 이유는 아래와 같다.

- core가 먼저 multipart aggregate를 만들지 않아도 된다.
- bindings가 `Message[]`와 `Received`를 꼭 한 번에 만들 필요가 없다.
- Java 같은 바인딩은 `Message` 하나씩 materialize하고, 필요할 때만 aggregate를
  만들 수 있다.
- JNI를 쓰는 경우에도 `msg_t` primitive 수준의 이득이 실제로 살아날 가능성이
  커진다.

즉 이 초안의 핵심은 "단일 part만 빠르게 하자"가 아니라, **multipart를 유지한 채
bindings aggregate 생성 시점을 뒤로 미루자**는 데 있다.

## 10. open question

### 10.1 recv metadata 반복 여부

같은 논리 메시지의 각 part recv 호출에서 routed metadata를 매번 같은 값으로 돌려줄
지, 첫 part에서만 의미 있게 채울지는 구현 전에 결정해야 한다.

현재 초안은 **매 part마다 같은 값으로 돌려줘도 된다** 쪽을 기본으로 둔다.

이유는 아래와 같다.

- bindings 구현이 단순하다.
- part-by-part API를 직접 쓰는 caller도 state machine을 더 적게 가진다.

### 10.2 helper 노출 범위

이 helper를 일반 C 사용자 1차 표면으로 보지 않고, bindings 구현을 위한 stable
helper layer로 둘지가 구현 전략 문제다.

이 초안의 기본 방향은 "기존 공개 C API를 대체하는 새 주 표면"이 아니라,
"existing C API 옆에 bindings용 helper를 추가한다" 쪽이다.

### 10.3 callback 모델과의 관계

현재 callback도 multipart aggregate를 한 번에 넘기는 모델이 많다.

part-by-part helper가 들어오면 callback도 아래 둘 중 하나를 택해야 한다.

- 지금처럼 aggregate callback 유지
- low-level callback은 part-by-part로 두고, 상위 callback은 bindings가 구성

이 초안은 send/recv helper가 먼저이며, callback 재설계는 별도 draft로 넘긴다.

## 11. 후속 작업

이 초안이 구현으로 확정되면, 다음 작업이 이어져야 한다.

1. `core/include/zlink.h`에 `*_part` helper를 추가한다.
2. 내부 aggregate recv/send 구현을 helper 위로 다시 정리한다.
3. C binding 계층 계획 문서인
   [c-binding-layer-plan.ko.md](c-binding-layer-plan.ko.md)를 기준으로 C binding
   표면을 정의한다.
4. `.NET`, `Java` bindings가 helper를 실제로 쓰도록 recv/send hot path를
   다시 구현한다.

위 순서는 선택 사항이 아니다. 이 문서의 작성 완료를 구현 시작 신호로 본다면,
helper 구현만 하고 멈추는 것이 아니라 아래 연결 작업까지 이어서 수행해야 한다.

- helper `*_part` C API 추가
- 기존 aggregate 공개 C API를 helper 위로 재정리
- C binding 계층 계획 문서 반영
- `.NET`, `Java` bindings hot path 재구성
- helper 기반 경로로 성능 재측정
- helper 적용 이후 남는 POSD 기반 리팩터링 항목 정리 및 반영

## 11.1 구현 준비 체크리스트

이 초안은 아래 항목이 충족되면 코드 작업에 들어갈 수 있는 수준으로 본다.

- `MORE`와 `FINAL`의 의미를 "frame 경계"가 아니라 "논리 메시지 commit 경계"로
  이해하고 구현한다.
- 열린 multipart 시퀀스는 interleave되지 않는다고 가정한다.
- request helper는 실패 시 열린 시퀀스를 폐기하고, caller가 처음부터 다시
  구성한다고 가정한다.
- routed recv metadata 포인터는 다음 recv-like 호출 전까지 유효하다고 가정한다.
- aggregate public API는 helper 위에 다시 쌓되, helper의 ownership 규칙을 그대로
  따른다.

즉 구현 시작 전에 더 필요한 결정은 callback 계층 재설계 정도뿐이며,
send/recv helper 자체의 기본 계약은 이 문서로 충분하다고 본다.

## 11.2 이 문서의 "작성 완료" 의미

이 문서에서 "작성 완료"는 단순히 초안 본문이 더 이상 길어지지 않는 상태를 뜻하지
않는다. 아래 조건이 충족되면 이 문서는 구현 착수 가능한 상태로 본다.

1. `MORE/FINAL` 의미가 확정돼 있다.
2. request/reply part helper 실패 규칙이 확정돼 있다.
3. routed metadata lifetime이 확정돼 있다.
4. aggregate public API가 helper 위에 다시 올라간다는 방향이 확정돼 있다.
5. C binding 계층 계획 문서와 연결이 완료돼 있다.

즉 이 문서의 작성 완료를 요청하는 것은 사실상 아래 작업을 묶어 시작하라는 의미와
같다.

- helper C API 헤더/구현 작업 시작
- aggregate C API 재정리 시작
- C binding 계층 문서/구현 작업 시작
- `.NET`, `Java` helper 기반 경로 적용 작업 시작
- helper 적용 이후 POSD 기반 구조 정리 작업 시작

따라서 이후 작업자는 "문서는 끝났지만 실제 코드는 다음 턴에 별도 검토"처럼
끊지 않고, 이 초안의 후속 작업 섹션을 그대로 구현 순서로 따라가야 한다.

## 11.3 구현 착수 후 작업 순서

이 문서가 작성 완료 상태라고 판단되면, 구현은 아래 순서로 이어서 진행한다.

1. `core/include/zlink.h`에 `*_part` helper 선언을 추가한다.
2. core 내부에서 aggregate send/recv 구현을 helper 기반으로 다시 정리한다.
3. helper 계약에 맞춰 필요한 errno/result/ownership 테스트를 추가한다.
4. [c-binding-layer-plan.ko.md](c-binding-layer-plan.ko.md)를 기준으로 C binding
   계층을 정리한다.
5. `.NET`, `Java` bindings hot path를 helper 기반으로 재구성한다.
6. helper 경로 적용 후 binding perf를 다시 측정하고, 기존 aggregate 경로와
   비교한다.
7. helper 적용 뒤에도 남아 있는 POSD 위반 요소를 다시 검토하고, 더 이상
   진행할 POSD 기반 리팩터링 항목이 없을 때까지 구조를 정리한다.

즉 이 문서는 단독 설계 메모가 아니라, 실제 구현 순서를 여는 문서다.

## 11.4 POSD 기반 후속 리팩터링

helper C API 적용만으로 작업이 끝난 것으로 보지 않는다. helper를 도입한 뒤에는
구조가 POSD 원칙에 맞게 더 단순하고 깊은 모듈 구조로 정리되었는지까지 확인해야
한다.

여기서 말하는 POSD 기반 리팩터링은 아래 항목을 뜻한다.

- helper와 aggregate convenience의 책임이 명확히 분리돼 있는지 확인
- bindings hot path가 불필요한 wrapper, 중복 aggregate, 얕은 pass-through 표면을
  계속 들고 있지 않은지 확인
- C binding, `.NET`, `Java`가 같은 substrate 위에서 설명 가능한 구조로
  정리됐는지 확인
- 변경 파급이 한 모듈 안에서 끝나야 할 규칙이 여러 계층에 흩어져 있지 않은지
  확인

이 문서와 연결된 작업은 아래 조건이 모두 충족될 때 완료로 본다.

1. helper C API가 구현돼 있다.
2. aggregate C API와 C binding 계층이 helper 위로 재정리돼 있다.
3. `.NET`, `Java` bindings hot path가 helper 기반으로 재구성돼 있다.
4. helper 적용 후 binding perf 재측정이 끝나 있다.
5. 남아 있는 POSD 기반 리팩터링 항목을 다시 검토했을 때, 더 이상 진행해야 할
   구조 정리 항목이 없다고 판단할 수 있다.

즉 "기능 구현 완료"가 아니라 "helper 적용 후 남은 POSD 리팩터링 backlog가 더
없을 때"를 이 문서와 연결된 작업의 최종 완료 기준으로 둔다.

## 12. 결론

이 초안의 방향은 아래 한 줄로 정리할 수 있다.

- 기존 공개 C API는 그대로 두고,
- bindings 구현용으로 `part-by-part + has_more` helper C API를 추가하며,
- bindings convenience API는 그 helper 위에 다시 올린다.

이 방향은 특정 `header/body` 패턴을 강요하지 않으면서도, bindings가 현재보다 훨씬
얇은 recv/send 경로를 만들 수 있게 해 준다.
