[스펙 목차](../README.ko.md)

# Draft -- Spot Routed Request API

> 이 문서는 **구현 전 초안**이다.
> 현재 공개 계약이 아니며, `core/include/zlink.h`와
> `bindings/c/include/zlink_c.h`에 없는 API를 보장하지 않는다.
> 구현과 공개 헤더가 확정되면 정식 spec 문서에 나누어 반영한다.

## 1. 목적

이 초안은 `Spot` handle이 routed request를 **직접 시작**할 수 있도록, 빠진 공개
API를 정리한다.

현재 공개 표면은 reply 쪽은 이미 갖추고 있지만 request 시작 쪽은 일부가 비어
있다. 이 비대칭 때문에 아래 같은 사용 모델을 공개 API만으로는 닫을 수 없다.

- `spot requester -> spot replier`
- `spot requester -> router replier`

특히 multi perf의 `MULTI_SPOT_REQREP`처럼 `Spot`이 requester 역할을 맡는
패턴은 이 request 시작 surface가 없으면 정식 구현을 붙일 수 없다.

이 문서의 목표는 아래 두 가지다.

- 현재 공개 API에서 무엇이 비어 있는지 분명하게 적는다.
- request/reply 짝이 맞도록 core helper substrate와 C API wrapper에 추가할
  함수 목록과 의미를 정리한다.

## 2. 현재 공개 API 상태

현재 공개 헤더에는 아래 API가 있다.

- `zlink_spot_request_channel(_part)`
- `zlink_spot_reply_spot(_part)`
- `zlink_spot_reply_router(_part)`
- `zlink_router_request_spot(_part)`
- `zlink_router_reply_spot(_part)`
- `zlink_router_send_spot(_part)`

이 조합을 request/reply 경로 기준으로 정리하면 아래와 같다.

| 경로 | request 시작 API | reply 반환 API | 상태 |
|------|------------------|----------------|------|
| `router -> spot` | `zlink_router_request_spot(_part)` | `zlink_spot_reply_router(_part)` | 있음 |
| `spot -> spot` | 없음 | `zlink_spot_reply_spot(_part)` | 비어 있음 |
| `spot -> router` | 없음 | `zlink_router_reply_spot(_part)` | 비어 있음 |
| `spot -> channel` | `zlink_spot_request_channel(_part)` | channel request 경로에 종속 | 별도 모델 |

핵심 문제는 간단하다.

- `Spot`은 routed request에 대한 **reply surface**는 있다.
- 하지만 `Spot`이 routed request를 **시작하는 surface**는 없다.

즉 현재 공개 API는 "들어온 routed request에 답장하는 Spot"은 표현할 수 있지만,
"다른 Spot 또는 Router로 routed request를 시작하는 Spot"은 표현하지 못한다.

이 문서에서 중요하게 보는 기준은 "당장 특정 perf 패턴을 붙일 수 있는가"가
아니다. 기준은 **request/reply surface의 짝이 맞는가**이다.

따라서 이 초안은 아래 네 API를 모두 **함께 추가해야 하는 API**로 본다.

- core:
  - `zlink_spot_request_spot_part(...)`
  - `zlink_spot_request_router_part(...)`
- C API:
  - `zlink_spot_request_spot(...)`
  - `zlink_spot_request_router(...)`

## 3. 이 초안이 추가하려는 API

이 초안은 새로운 recv API나 reply API를 추가하지 않는다.
빠진 것은 request 시작 surface이므로, 그 부분만 보강한다.

추가 대상은 두 층이다.

- core public helper substrate: `*_part`
- C API wrapper: multipart 배열 wrapper

이때 추가 대상을 일부만 고르지 않는다.
이미 reply 쪽이 `spot` origin과 `router` origin을 모두 나누고 있으므로,
request 시작 surface도 같은 축으로 둘 다 추가해야 짝이 맞는다.

### 3.1 범위에 포함하는 것

이 초안이 직접 다루는 것은 아래 네 API다.

- `zlink_spot_request_spot_part(...)`
- `zlink_spot_request_router_part(...)`
- `zlink_spot_request_spot(...)`
- `zlink_spot_request_router(...)`

즉 이 문서는 `Spot` routed **request 시작** surface를 public contract에 넣는
문서다.

### 3.2 범위에 포함하지 않는 것

이 초안은 아래 항목은 이번 변경 범위에 넣지 않는다.

- `zlink_spot_reply_spot(...)`, `zlink_spot_reply_router(...)`
  이미 공개 계약이 있으므로 새로 정의하지 않는다.
- `zlink_spot_recv(...)`
  수신 관찰 surface는 이미 있으므로 새로 정의하지 않는다.
- `zlink_spot_request_progress_internal(...)`
  request completion progress 경로는 기존 모델을 그대로 따른다.
- `zlink_spot_send_spot(...)`, `zlink_spot_send_router(...)`
  one-way direct send surface는 이 초안의 대상이 아니다.

마지막 항목을 따로 적는 이유는 분명하다.
이번 초안의 목적은 "현재 reply 쪽에 이미 존재하는 request/reply 짝을 맞추는 것"이다.
one-way send는 현재 reply 짝과 직접 연결되지 않으므로, 같은 routed 주소 체계를
쓰더라도 별도 기능으로 다루는 편이 맞다.

## 4. Core Helper Substrate 초안

### 4.1 `zlink_spot_request_spot_part`

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_spot_part (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);
```

의도는 `Spot`이 특정 remote `Spot`으로 routed request를 시작하는 것이다.

- `spot_`는 local `Spot` handle이다.
- `dest_node_rid_`는 target `SpotNode`의 routing id다.
- `dest_spot_rid_`는 target `Spot`의 routed id다.
- `handler_`는 reply completion callback이다.
- `timeout_ms_`는 accepted request의 reply timeout이다.

이 API의 reply는 기존 `zlink_spot_reply_spot(_part)`와 짝을 이룬다.

### 4.2 `zlink_spot_request_router_part`

```c
ZLINK_EXPORT zlink_submit_result_t zlink_spot_request_router_part (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *part_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  zlink_part_flag_t part_flag_,
  uint32_t timeout_ms_);
```

의도는 `Spot`이 특정 remote `ROUTER` peer로 routed request를 시작하는 것이다.

- `spot_`는 local `Spot` handle이다.
- `peer_rid_`는 target `ROUTER` peer의 routing id다.
- 나머지 multipart, callback, timeout 의미는 기존 request 계열과 같다.

이 API의 reply는 기존 `zlink_router_reply_spot(_part)`와 짝을 이룬다.

## 5. C API Wrapper 초안

### 5.1 `zlink_spot_request_spot`

```c
ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_spot (
  void *spot_,
  const zlink_routing_id_t *dest_node_rid_,
  const zlink_routing_id_t *dest_spot_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);
```

이 wrapper는 `parts_` / `part_count_`를 받아 내부적으로
`zlink_spot_request_spot_part()`를 호출하는 형태를 가정한다.

의미와 검증 규칙은 가능한 한 기존 `zlink_router_request_spot()`과 같게 맞춘다.
달라지는 것은 requester가 `ROUTER`가 아니라 `Spot`이라는 점뿐이다.

### 5.2 `zlink_spot_request_router`

```c
ZLINK_C_EXPORT zlink_submit_result_t zlink_spot_request_router (
  void *spot_,
  const zlink_routing_id_t *peer_rid_,
  zlink_msg_t *parts_,
  size_t part_count_,
  zlink_reply_handler_fn handler_,
  void *userdata_,
  zlink_send_flags_t flags_,
  uint32_t timeout_ms_);
```

이 wrapper는 `parts_` / `part_count_`를 받아 내부적으로
`zlink_spot_request_router_part()`를 호출하는 형태를 가정한다.

의미와 검증 규칙은 가능한 한 기존 `zlink_router_request()`와 같게 맞춘다.
달라지는 것은 requester가 `ROUTER`가 아니라 `Spot`이라는 점뿐이다.

## 6. 현재 정식 spec과의 관계

현재 정식 `spot` spec은 `Spot` public surface에서 direct SPOT send/request가
제거되었다고 적고 있다. 이 문구는 현재 공개 헤더 기준으로는 맞다.

하지만 이 초안은 request/reply 짝이 맞지 않는 상태를 그대로 둘 수 없다고 본다.
따라서 구현이 확정되면 정식 spec은 아래처럼 다시 정리되어야 한다.

- `Spot` public surface는 여전히 channel send/request를 기본 high-level 경로로 둔다.
- 동시에 routed request에 한해서는 direct public surface를 다시 가진다.
- 다만 이번 초안은 one-way direct send까지 함께 복원하는 문서는 아니다.

즉 정식 문서가 바뀌더라도 의미는 "Spot이 모든 direct addressing을 다시 갖는다"가
아니라, "request/reply 짝을 맞추기 위해 direct routed request surface를 공개한다"에
가깝다.

## 7. 공통 계약 초안

이 초안은 새 API의 세부 동작을 기존 request 계열과 최대한 같게 두는 방향을
전제로 한다. 그래야 binding과 내부 request tracker가 별도 예외 규칙 없이 붙는다.

### 7.1 공통 의미

- 새 API는 모두 asynchronous request submit surface다.
- submit이 accepted 되면 reply completion은 `handler_`로 정확히 한 번 돌아와야
  한다.
- 정상 reply, timeout, terminate, 내부 오류에 대한 completion 분류는 기존
  `zlink_reply_handler_fn` 모델과 같은 축을 따른다.
- 새 API는 `Spot` handle만 지원 대상으로 한다.

### 7.2 multipart 규칙

multipart 전송 규칙은 기존 `zlink_spot_request_channel_part()`와
`zlink_router_request_spot_part()`의 규칙을 그대로 따른다.

즉 이 초안은 multipart 처리 방식 자체를 새로 정의하지 않는다.
대상 주소 지정만 channel-name 기반에서 routed target 기반으로 넓힌다.

### 7.3 reply pairing

새 request API와 기존 reply API의 짝은 아래처럼 고정한다.

| request 시작 API | reply API |
|------------------|-----------|
| `zlink_spot_request_spot(_part)` | `zlink_spot_reply_spot(_part)` |
| `zlink_spot_request_router(_part)` | `zlink_router_reply_spot(_part)` |

이 규칙은 request 시작 주체와 reply 반환 주체를 분명하게 맞추기 위해 필요하다.

### 7.4 수신 측 관찰점

이 초안은 새 request를 위해 새로운 recv surface를 추가하지 않는다.
기존 recv API로 아래처럼 관찰할 수 있어야 한다.

- `spot -> spot` request:
  target `Spot`은 `zlink_spot_recv(_part)`에서
  `source_node_rid`, `source_spot_rid`, `request_seq`를 본다.
- `spot -> router` request:
  target `ROUTER`는 `zlink_router_recv(_part)`에서
  `source_node_rid`, `source_spot_rid`, `request_seq`를 본다.

즉 새 API가 바꾸는 것은 submit surface이지, inbound request 관찰 방식이 아니다.

## 8. 반환값과 실패 의미 초안

반환 타입은 기존 request 계열과 같은 `zlink_submit_result_t`를 사용한다.

세부 매핑은 구현 시점에 기존 request API와 맞춰 확정하되, 이 초안은 아래 방향을
가정한다.

- `ZLINK_SUBMIT_OK`
  request가 accepted 되어 이후 completion을 받아야 하는 상태에 들어갔다.
- `ZLINK_SUBMIT_BACKPRESSURED`
  non-blocking submit에서 즉시 보낼 수 없다.
- `ZLINK_SUBMIT_NOT_ADMITTED`
  target peer를 알고는 있지만 admission 상태 때문에 새 request를 허용하지 않는다.
- `ZLINK_SUBMIT_NOT_FOUND`
  지정한 target `Spot` 또는 target `ROUTER`를 찾을 수 없다.
- `ZLINK_SUBMIT_NOT_CONNECTED`
  target은 알고 있지만 현재 연결된 경로가 없다.
- `ZLINK_SUBMIT_INVALID_HANDLE`
  `spot_`가 유효한 `Spot` handle이 아니다.
- `ZLINK_SUBMIT_INVALID_ARGUMENT`
  RID, multipart 인자, callback 인자가 잘못되었다.
- `ZLINK_SUBMIT_SEQ_EXHAUSTED`
  request sequence를 더 할당할 수 없다.

이외의 오류는 기존 request 계열과 같은 기준으로
`INVALID_STATE`, `THREAD_VIOLATION`, `OUT_OF_MEMORY`, `INTERNAL_ERROR`,
`TERMINATED`를 재사용하는 방향을 둔다.

특히 `spot -> spot` 경로는 기존 내부 submit 경로가 이미 admission 거부를 별도
submit 결과로 구분하고 있으므로, 정식화 단계에서도 이 분류를 빠뜨리면 안 된다.
`spot -> router` 경로도 같은 의미 체계를 따르는 편이 맞다.

## 9. 구현 메모

이 초안은 "새로운 reply 모델"을 제안하는 문서가 아니다.
이미 있는 reply surface와 recv surface를 유지한 채, 빠진 request 시작 surface를
request/reply 짝이 맞는 형태로 채우는 것이 목적이다.

구현이 끝나면 아래 문서를 함께 맞춰야 한다.

- `core/include/zlink.h`
- `bindings/c/include/zlink_c.h`
- `bindings/c/src/zlink_c.c`
- 바인딩별 native 선언 레이어와 vendored public header 복사본
- 관련 request/reply 테스트
- `doc/spec/core/service/spot.ko.md`
- `doc/spec/core/service/spot.md`
- `doc/spec/bindings/c/` 아래의 C binding 문서

그 전까지는 이 문서를 현재 공개 계약으로 읽으면 안 된다.

### 9.1 함께 수정해야 하는 바인딩 계층

새 core public API가 생기면 C wrapper만 고치고 끝나지 않는다.
현재 저장소 구조상 아래 계층도 함께 따라와야 한다.

- C wrapper 구현
- C++/Go 등에서 들고 있는 vendored `zlink.h` 복사본
- Rust/Java/.NET/Python/Node의 native symbol 선언 레이어
- request/reply 샘플과 계약 테스트

즉 이 초안의 직접 대상은 core와 C API지만, 실제 구현 완료 기준은 "다른 바인딩이
새 public header와 충돌하지 않는 상태"까지 포함한다.

## 10. 회귀 테스트 초안

이 초안은 새 API를 추가하는 작업이므로, "새 기능이 동작하는가"만 보면 부족하다.
기존 request/reply 경로와 existing public contract가 그대로 유지되는지도 함께
확인해야 한다.

테스트는 아래 두 묶음으로 나누는 편이 맞다.

- 새 API 계약 테스트
- 기존 API 회귀 테스트

### 10.1 새 API 계약 테스트

새로 추가하는 네 API는 각각 아래 항목을 확인해야 한다.

- `zlink_spot_request_spot_part(...)`
  multipart staging, 마지막 part submit, invalid RID, null handler, timeout
  completion, 정상 reply completion
- `zlink_spot_request_router_part(...)`
  multipart staging, 마지막 part submit, invalid RID, null handler, timeout
  completion, 정상 reply completion
- `zlink_spot_request_spot(...)`
  C wrapper가 multipart 배열을 정확히 `*_part` 호출로 넘기는지
- `zlink_spot_request_router(...)`
  C wrapper가 multipart 배열을 정확히 `*_part` 호출로 넘기는지

경로 기준으로는 아래 시나리오가 최소 세트다.

- `spot requester -> spot replier -> spot reply`
- `spot requester -> router replier -> router reply`
- local target dispatch
- remote target dispatch
- non-blocking submit에서 backpressure 반환
- admission 거부에서 `ZLINK_SUBMIT_NOT_ADMITTED` 반환
- request timeout
- terminate/close 중 pending request completion

### 10.2 기존 API 회귀 테스트

이번 변경 뒤에도 아래 기존 계약은 깨지면 안 된다.

- `zlink_spot_request_channel(_part)`
  channel request가 계속 attach된 `DEALER` 경로로만 나가는지
- `zlink_router_request_spot(_part)`
  기존 `router -> spot` request/reply 동작이 그대로 유지되는지
- `zlink_spot_reply_spot(_part)`
  `spot -> spot` reply 경로가 바뀌지 않는지
- `zlink_spot_reply_router(_part)`
  `spot -> router` reply 경로가 바뀌지 않는지
- `zlink_spot_recv(_part)`, `zlink_router_recv(_part)`
  inbound request 관찰 surface가 바뀌지 않는지
- `zlink_spot_request_progress_internal(...)`
  request completion progress pump가 새 request 종류까지 포함해 정상 동작하는지

특히 아래 회귀는 반드시 따로 확인해야 한다.

- 기존 channel request/reply가 routed direct request 추가 때문에 다른 pending
  request key와 충돌하지 않는지
- 기존 router-origin request와 새 spot-origin request가 request sequence 또는
  completion dispatch에서 서로 섞이지 않는지
- dispatch callback 안의 recv drain 모델이 기존처럼 유지되는지

### 10.3 권장 테스트 위치

저장소 구조상 아래 위치에 테스트가 같이 들어가는 편이 맞다.

- core request/reply contract test
- `bindings/c` contract test
- 필요한 경우 multi perf smoke test

multi perf smoke test는 성능 수치 확인보다 먼저, 아래 최소 조건을 보는 용도로
사용한다.

- handshake 뒤 active 진입
- requester submit 성공
- replier recv 관찰
- reply completion 집계

즉 perf 실행은 벤치마크이기도 하지만, 이번 변경에서는 routed `Spot` request
surface가 end-to-end로 실제 동작하는지 보는 회귀 확인 수단도 된다.

다만 여기서 말하는 perf smoke는 기본적으로 `spot -> spot` 경로 확인에 가깝다.
`spot -> router` 경로는 perf만으로 대체하지 말고, 별도 contract 또는 sample 기반
e2e 테스트를 둬야 한다.

### 10.4 `spot -> router` 별도 e2e 회귀

`spot_request_router`는 `spot_request_spot`과 주소 지정 방식만 다른 단순 별칭이
아니다. target 식별, local dispatch 판별, remote ingress 경로가 다를 수 있으므로
별도 end-to-end 확인이 필요하다.

최소 시나리오는 아래처럼 잡는 편이 맞다.

- single process local dispatch:
  local `Spot`이 local `ROUTER`로 request를 보내고 `router_reply_spot()`으로 reply
  받기
- cross-process remote dispatch:
  remote `ROUTER` peer로 request를 보내고 reply completion 받기
- invalid peer rid:
  잘못된 `peer_rid_`에 대해 `INVALID_ARGUMENT` 또는 `NOT_FOUND` 축으로 실패 확인
- connected but not admitted:
  admission 거부 상황에서 `NOT_ADMITTED` 확인

이 경로는 `spot -> spot` 회귀와 분리해서 이름이 보이는 독립 테스트로 두는 편이
좋다.

## 11. `bindings/c/perf` 적용 계획

`bindings/c/perf`에는 이번 API를 곧바로 문서 반영용 wiring으로만 붙이지 말고,
기존 실패 지점을 해소하는 순서로 적용하는 편이 맞다.

우선순위는 아래처럼 잡는다.

- core public API 추가
  `zlink_spot_request_spot(_part)`, `zlink_spot_request_router(_part)`를 먼저 연다.
- C wrapper 추가
  `bindings/c/include/zlink_c.h`, `bindings/c/src/zlink_c.c`를 맞춘다.
- 기존 `MULTI_SPOT_REQREP` 구현 교체
  `bindings/c/perf/multi/src/perf_multi_spot_reqrep_client.cpp`에서 임시 우회 경로 대신
  새 `spot_request_spot` public surface를 사용한다.
- smoke 재검증
  `run_comparison.py SPOT_REQREP` 기준으로 handshake 이후 actual reply completion이
  올라오는지 다시 확인한다.

`bindings/c/perf`에서 이번 초안과 직접 연결되는 대상은 최소 아래 항목이다.

- `bindings/c/perf/multi/src/perf_multi_spot_reqrep_client.cpp`
- `bindings/c/perf/multi/src/perf_multi_spot_reqrep_server.cpp`
- `bindings/c/perf/run_comparison.py`
- `bindings/c/perf/run_benchmarks_multi.sh`

검증 기준도 문서에 맞춰 분명하게 두는 편이 좋다.

- requester가 새 public `Spot` routed request API로 submit한다.
- replier는 기존처럼 `dispatch_event` callback 안에서 recv drain 후 reply한다.
- ops/s 집계가 다시 non-zero로 올라온다.
- `PERF_ALLOW_MULTI=1` smoke에서 timeout 없이 종료된다.

## 12. `bindings/c/samples` 적용 계획

현재 `bindings/c/samples`에는 `spot_recv_sample.c`는 있지만 direct routed request
예제는 없다. 따라서 이번 변경이 들어가면 샘플도 같이 보강하는 편이 맞다.

권장 방향은 아래 둘 중 하나다.

- 새 샘플 추가:
  `spot_request_reply_sample.c` 또는 그에 준하는 이름으로
  `spot -> spot`, `spot -> router` request/reply를 보여준다.
- 기존 샘플 확장:
  `spot_recv_sample.c`는 recv/reply 예제로 남기고, request 시작 예제는 별도 파일로
  분리한다.

이 초안 기준으로는 **별도 샘플 파일 추가**가 더 낫다.
수신 예제와 request 시작 예제의 목적이 다르기 때문이다.

최소 적용 계획은 아래처럼 잡는다.

- `bindings/c/samples/CMakeLists.txt`
  새 sample target 추가
- `bindings/c/samples/run_samples.sh`
  새 smoke 항목 추가
- 새 sample source
  `zlink_spot_request_spot()`와 `zlink_spot_request_router()`를 각각 한 번씩 보여주는
  예제 추가

샘플 검증 포인트는 아래 정도면 충분하다.

- request submit 성공
- reply handler 호출
- `request_seq` 기반 reply가 정상 귀속
- 기존 sample smoke를 깨지 않음
